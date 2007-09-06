// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "spoolmanager.h"

#include "query.h"
#include "timer.h"
#include "mailbox.h"
#include "deliveryagent.h"
#include "configuration.h"
#include "smtpclient.h"
#include "allocator.h"
#include "scope.h"


static SpoolManager * sm;
static bool shutdown;


class SpoolManagerData
    : public Garbage
{
public:
    SpoolManagerData()
        : q( 0 ), remove( 0 ), t( 0 ), row( 0 ), client( 0 ),
          agent( 0 ), uidnext( 0 ), again( false ), spooled( false ), log( 0 )
    {}

    Query * q;
    Query * remove;
    Timer * t;
    Row * row;
    SmtpClient * client;
    DeliveryAgent * agent;
    uint uidnext;
    bool again;
    bool spooled;
    Log * log;
};


/*! \class SpoolManager spoolmanager.h
    This class periodically attempts to deliver mail from the special
    /archiveopteryx/spool mailbox to a smarthost using DeliveryAgent.
    Messages in the spool are marked for deletion when the delivery
    either succeeds, or is permanently abandoned.

    Each archiveopteryx process has only one instance of this class,
    which is created the first time SpoolManager::run() is called.
*/

SpoolManager::SpoolManager()
    : d( new SpoolManagerData )
{
    d->log = new Log( Log::General );
}


void SpoolManager::execute()
{
    // Start a queue run only when the Timer wakes us
    if ( d->t && d->t->active() )
        return;

    Scope x( d->log );

    // Fetch a list of spooled messages.
    if ( !d->q ) {
        log( "Starting queue run" );
        reset();
        d->q =
            new Query( "select mailbox,uid "
                       "from deliveries d left join deleted_messages dm "
                       "using (mailbox,uid) where dm.uid is null", this );
        d->q->execute();
    }

    // For each one, create and run a DeliveryAgent; and if it completes
    // its delivery attempt, delete the spooled message.
    while ( d->row || d->q->hasResults() ) {
        if ( !d->row )
            d->row = d->q->nextRow();

        if ( !d->agent ) {
            if ( !d->client ||
                 !( d->client->state() == Connection::Connecting ||
                    d->client->state() == Connection::Connected ) )
            {
                if ( d->client )
                    log( "Discarding existing SMTP client", Log::Debug );
                d->client = client();
            }

            if ( d->client->state() == Connection::Invalid ||
                 d->client->state() == Connection::Closing )
            {
                log( "Couldn't connect to smarthost. Ending queue run" );
                d->client = 0;
                reset();
                d->t = new Timer( this, 300 );
                return;
            }

            if ( !d->client->ready() )
                return;

            Mailbox * m = Mailbox::find( d->row->getInt( "mailbox" ) );
            if ( m ) {
                d->uidnext = m->uidnext();
                d->agent = new DeliveryAgent( d->client,
                                              m, d->row->getInt( "uid" ),
                                              this );
                d->agent->execute();
            }
        }

        if ( d->agent ) {
            if ( !d->agent->done() )
                return;

            Mailbox * m = Mailbox::find( d->row->getInt( "mailbox" ) );
            if ( m && d->uidnext < m->uidnext() )
                d->again = true;

            if ( !d->remove && d->agent->delivered() ) {
                log( "Deleting delivered message from spool", Log::Debug );
                d->remove =
                    new Query( "insert into deleted_messages "
                               "(mailbox, uid, deleted_by, reason) "
                               "values ($1, $2, null, $3)", this );
                d->remove->bind( 1, d->row->getInt( "mailbox" ) );
                d->remove->bind( 2, d->row->getInt( "uid" ) );
                d->remove->bind( 3, "Smarthost delivery " +
                                 d->agent->log()->id() );
                d->remove->execute();
            }
            else {
                d->spooled = true;
            }

            if ( d->remove && !d->remove->done() )
                return;
        }

        d->row = 0;
        d->agent = 0;
        d->remove = 0;
    }

    if ( !d->q->done() )
        return;

    if ( d->again ) {
        reset();
        d->t = new Timer( this, 0 );
        log( "Restarting to handle newly-spooled messages" );
    }
    else {
        if ( d->client )
            d->client->logout();
        d->client = 0;
        reset();
        log( "Ending queue run" );
        if ( d->spooled )
            d->t = new Timer( this, 300 );
    }
}


/*! Returns a pointer to a new SmtpClient to talk to the smarthost. */

SmtpClient * SpoolManager::client()
{
    Endpoint e( Configuration::text( Configuration::SmartHostAddress ),
                Configuration::scalar( Configuration::SmartHostPort ) );
    return new SmtpClient( e, this );
}


/*! Resets the perishable state of this SpoolManager, i.e. all but the
    Timer and the SmtpClient. Provided for convenience.
*/

void SpoolManager::reset()
{
    delete d->t;
    d->t = 0;
    d->q = 0;
    d->row = 0;
    d->agent = 0;
    d->remove = 0;
    d->again = false;
    d->spooled = false;
}


/*! Causes the spool manager to re-examine the queue and attempt to make
    one or more deliveries, if possible.
*/

void SpoolManager::run()
{
    if ( ::shutdown ) {
        ::log( "Will not send spooled mail due to earlier database problem",
               Log::Error );
        return;
    }
    if ( !::sm ) {
        ::sm = new SpoolManager;
        Allocator::addEternal( ::sm, "spool manager" );
    }
    if ( ::sm->d->t ) {
        Scope x( ::sm->d->log );
        ::sm->log( "Forcing immediate queue run", Log::Debug );
        ::sm->reset();
    }
    ::sm->execute();
}


/*! Creates a SpoolManager object and a timer to ensure that it's
    started once (after which it will ensure that it wakes up once
    in a while). This function expects to be called from ::main().
*/

void SpoolManager::setup()
{
    if ( !::sm ) {
        ::sm = new SpoolManager;
        Allocator::addEternal( ::sm, "spool manager" );
    }
    sm->d->t = new Timer( sm, 60 );
}


/*! Causes the spool manager to stop sending mail, at once. Should
    only be called if we're unable to update a message's "sent" status
    from "unsent" to "sent" and a loop threatens.
*/

void SpoolManager::shutdown()
{
    if ( ::sm && sm->d->t ) {
        delete sm->d->t;
        sm->d->t = 0;
    }
    ::sm = 0;
    ::shutdown = true;
    ::log( "Shutting down outgoing mail due to software problem. "
           "Please contact info@oryx.com", Log::Error );
}
