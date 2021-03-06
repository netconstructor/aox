// Copyright 2009 The Archiveopteryx Developers <info@aox.org>

#include "database.h"

#include "list.h"
#include "estring.h"
#include "allocator.h"
#include "configuration.h"
#include "eventloop.h"
#include "schema.h"
#include "scope.h"
#include "graph.h"
#include "event.h"
#include "query.h"
#include "file.h"
#include "log.h"

#include "postgres.h"

// time_t, time
#include <time.h>


static uint backendNumber;
List< Query > *Database::queries;
static GraphableNumber * queryQueueLength = 0;
static GraphableNumber * busyDbConnections = 0;
static GraphableNumber * totalDbConnections = 0;
static List< Database > *handles;
static time_t lastExecuted;
static time_t lastCreated;
static Database::User loginAs;
static EString * username;
static EString * password;
static List<EventHandler> * whenIdle;


static void newHandle()
{
    Scope x;
    if ( handles && !handles->isEmpty() ) {
        Log * l = handles->firstElement()->log();
        if ( l->parent() )
            l = l->parent();
        if ( l )
            x.setLog( l );
    }
    (void)new Postgres;
}


/*! \class Database database.h
    This class represents a connection to the database server.

    The Query and Transaction classes provide the recommended database
    interface. You should never need to use this class directly.

    This is the abstract base class for Postgres (and any other database
    interface classes we implement). It's responsible for validating the
    database configuration, maintaining a pool of database handles, and
    accepting queries into a common queue via submit().
*/

Database::Database()
    : Connection()
{
    number = ++::backendNumber;
    setType( Connection::DatabaseClient );
    setState( Database::Connecting );
    lastCreated = time( 0 );
}


/*! This setup function reads and validates the database configuration
    to the best of its limited ability (since connection negotiation
    must be left to subclasses). It logs a disaster if it fails.

    It creates \a desired database handles at startup and will log in
    as \a user with the password \a pass.

    This function expects to be called from ::main().
*/

void Database::setup( uint desired, const EString & user,
                      const EString & pass )
{
    if ( !queries ) {
        queries = new List< Query >;
        Allocator::addEternal( queries, "list of queries" );
    }

    if ( !handles ) {
        handles = new List< Database >;
        Allocator::addEternal( handles, "list of database handles" );
    }

    if ( ::username )
        Allocator::removeEternal( ::username );
    ::username = new EString( user );
    Allocator::addEternal( ::username, "database username" );

    if ( ::password )
        Allocator::removeEternal( ::password );
    ::password = new EString( pass );
    Allocator::addEternal( ::password, "database password" );

    EString db = Configuration::text( Configuration::Db ).lower();

    EString dbt = db.section( "+", 1 );

    if ( dbt != "pg" && dbt != "pgsql" && dbt != "postgres" ) {
        ::log( "Unsupported database type: " + db, Log::Disaster );
        return;
    }

    addInitialHandles( desired );
}


/*! Creates \a desired connections to the database.
*/

void Database::addInitialHandles( uint desired )
{
    Endpoint srv( Configuration::DbAddress, Configuration::DbPort );

    if ( desired == 0 ) {
        uint max = Configuration::scalar( Configuration::DbMaxHandles );
        desired = 3;
        if ( Configuration::toggle( Configuration::Security ) &&
             srv.protocol() == Endpoint::Unix )
            desired = max;
        if ( desired > max )
            desired = max;
    }

    while ( desired ) {
        newHandle();
        desired--;
    }
}


/*! \overload
    This function is provided as a convenience for the (majority of)
    callers that use the default values for \a desired (0) and \a login
    (Database::DbUser). It infers the correct username and password and
    forwards the call to setup() with the appropriate parameters.
*/

void Database::setup( uint desired, Database::User login )
{
    EString user;
    EString pass;

    ::loginAs = login;
    if ( login == Database::DbUser ) {
        user = Configuration::text( Configuration::DbUser );
        pass = Configuration::text( Configuration::DbPassword );
    }
    else if ( login == Database::DbOwner ) {
        user = Configuration::text( Configuration::DbOwner );
        pass = Configuration::text( Configuration::DbOwnerPassword );
    }
    else if ( login == Database::Superuser ) {
        user = Configuration::compiledIn( Configuration::PgUser );
        pass = "";
    }

    setup( desired, user, pass );
}


/*! Adds \a q to the queue of submitted queries and sets its state to
    Query::Submitted. The first available handle will process it.
*/

void Database::submit( Query *q )
{
    queries->append( q );
    q->setState( Query::Submitted );
    runQueue();
}


/*! Adds the queries in the list \a q to the queue of submitted queries,
    and sets their state to Query::Submitted. The first available handle
    will process them (but it's not guaranteed that the same handle will
    process them all. Use a Transaction if you depend on ordering).
*/

void Database::submit( List< Query > *q )
{
    List< Query >::Iterator it( q );
    while ( it ) {
        it->setState( Query::Submitted );
        queries->append( it );
        ++it;
    }
    runQueue();
}


/*! This extremely evil function shuts down all Database handles. It's
    used only by lib/installer to reconnect to the database.  Once
    it's done, setup() may be called again with an appropriately
    altered configuration.

    Don't try this at home, kids.
*/

void Database::disconnect()
{
    List< Database >::Iterator it( handles );
    handles = 0;
    while ( it ) {
        it->react( Shutdown );
        ++it;
    }
}



/*! This private function is used to make idle handles process the queue
    of queries, and is called by the two variants of submit().
*/

void Database::runQueue()
{
    int connecting = 0;
    int busy = 0;

    if ( !queryQueueLength )
        queryQueueLength = new GraphableNumber( "query-queue-length" );
    if ( !busyDbConnections )
        busyDbConnections = new GraphableNumber( "active-db-connections" );

    // First, we give each idle handle a Query to process

    Query * first = queries->firstElement();

    List< Database >::Iterator it( handles );
    while ( it ) {
        State st = it->state();

        if ( st != Connecting && // connecting isn't working
             st != Broken && // broken isn't working
             ( !it->usable() || // processing a query is working
               st == InTransaction || // occupied by a transaction is, too
               st == FailedTransaction ) )
            busy++;

        if ( st == Idle && it->usable() ) {
            it->processQueue();
            if ( queries->isEmpty() ) {
                queryQueueLength->setValue( 0 );
                busyDbConnections->setValue( busy );
                return;
            }
        }
        else if ( st == Connecting ) {
            connecting++;
        }

        ++it;
    }

    queryQueueLength->setValue( queries->count() );
    busyDbConnections->setValue( busy );

    // If there's nothing to do, or we did get something done, then we
    // don't even consider opening a new database connection.
    if ( queries->isEmpty() || first != queries->firstElement() )
        return;

    // Even if we want to, we cannot create unix-domain handles when
    // we're running within chroot.
    if ( server().protocol() == Endpoint::Unix &&
         !server().address().startsWith( File::root() ) )
        return;

    // And even if we're asked to, we don't create handles while
    // shutting down.
    if ( EventLoop::global()->inShutdown() )
        return;

    // We create at most one new handle per interval.
    int interval = Configuration::scalar( Configuration::DbHandleInterval );
    if ( time( 0 ) - lastCreated < interval )
        return;

    // If we don't have too many, we can create another handle!
    uint max = Configuration::scalar( Configuration::DbMaxHandles );
    if ( handles->count() < max )
        newHandle();
}


/*! \fn virtual void Database::processQueue()
    Instructs the Database object to send any queries whose state is
    Query::Submitted to the server.
*/


/*! Sets the state of this Database handle to \a s, which must be one of
    Connecting, Idle, InTransaction, FailedTransaction.
*/

void Database::setState( State s )
{
    st = s;
}


/*! Returns the current state of this Database object. */

Database::State Database::state() const
{
    return st;
}


/*! Adds \a d to the pool of active database connections. */

void Database::addHandle( Database * d )
{
    handles->append( d );
    if ( !totalDbConnections )
        totalDbConnections = new GraphableNumber( "total-db-connections" );
    totalDbConnections->setValue( handles->count() );
}


/*! Removes \a d from the pool of active database connections. */

void Database::removeHandle( Database * d )
{
    if ( !handles )
        return;

    handles->remove( d );
    if ( !totalDbConnections )
        totalDbConnections = new GraphableNumber( "total-db-connections" );
    totalDbConnections->setValue( handles->count() );
    if ( !handles->isEmpty() )
        return;

    if ( !EventLoop::global()->inShutdown() )
        addInitialHandles( 3 );

    if ( server().protocol() == Endpoint::Unix &&
         !server().address().startsWith( File::root() ) )
        ::log( "All database handles closed; cannot create any new ones.",
               Log::Disaster );
}


/*! Returns the configured Database type, which may currently be only
    postgres.
*/

EString Database::type()
{
    return Configuration::text( Configuration::Db );
}


/*! Returns an Endpoint representing the address of the database server
    (as specified by db-address and db-port). The Endpoint may not be
    valid.
*/

Endpoint Database::server()
{
    return Endpoint( Configuration::DbAddress, Configuration::DbPort );
}


/*! Returns the address of the database server (db-address). */

EString Database::address()
{
    return Configuration::text( Configuration::DbAddress );
}


/*! Returns the database server port (db-port). */

uint Database::port()
{
    return Configuration::scalar( Configuration::DbPort );
}


/*! Returns the configured database name (db-name). */

EString Database::name()
{
    return Configuration::text( Configuration::DbName );
}


/*! Returns the database username used for this connection, as specified
    to (or inferred by) setup().
*/

EString Database::user()
{
    return *::username;
}


/*! Returns the configured database password (db-password or
    db-owner-password). */

EString Database::password()
{
    return *::password;
}


/*! Returns the number of database handles currently connected to the
    database.
*/

uint Database::numHandles()
{
    if ( !::handles )
        return 0;
    uint n = 0;
    List<Database>::Iterator it( ::handles );
    while ( it ) {
        if ( it->state() != Connecting )
            n++;
        ++it;
    }
    return n;
}


/*! This static function records the time at which a Database subclass
    issues a query to the database server. It is used to manage the
    creation of new database handles.
*/

void Database::recordExecution()
{
    lastExecuted = time( 0 );
}


/*! Returns true if this Database handle is currently able to process
    queries, and false if it's busy processing queries, is shutting
    down, or for any other reason unwilling to process new queries.
    The default implementation always returns true; subclasses may
    override that behaviour.
*/

bool Database::usable() const
{
    return true;
}


/*! Returns an nonzero positive integer which is unique to this
    database handler.
*/

uint Database::connectionNumber() const
{
    return number;
}


/*! This function returns DbOwner or DbUser, as specified in the call to
    Database::setup().
*/

Database::User Database::loginAs()
{
    return ::loginAs;
}


/*! Instructs the database system to call \a h once as soon as the
    database system becomes completely idle (no queries queued or
    executing).

    \a h is not executed right away, even if the database is currently
    idle, but rather the next time the database system becomes idle.
*/

void Database::notifyWhenIdle( class EventHandler * h )
{
    if ( !::whenIdle ) {
        ::whenIdle = new List<EventHandler>;
        Allocator::addEternal( ::whenIdle,
                               "eventhandlers to call when the db idles" );
    }
    ::whenIdle->append( h );
}


/*! Returns true if all database handles are idle and there's no queued work
    for them. Returns false in all other cases.
*/

bool Database::idle()
{
    List< Database >::Iterator it( handles );
    while ( it ) {
        if ( !it->usable() )
            return false;
        ++it;
    }

    if ( queries && !queries->isEmpty() )
        return false;

    return true;
}


/*! Checks whether all handles are idle and usable, and there's no
    queued work. If the database system really is idle, calls and
    forgets the EventHandler objects recorded by notifyWhenIdle().
*/

void Database::reactToIdleness()
{
    if ( !queries->isEmpty() )
        return;

    if ( !::whenIdle )
        return;

    if ( !idle() )
        return;

    List<EventHandler>::Iterator i( ::whenIdle );
    Allocator::removeEternal( ::whenIdle );
    ::whenIdle = 0;
    while ( i ) {
        i->notify();
        ++i;
    }
}


/*! This function is called by a database client to ensure that the
    schema is as they expect; for the moment all it does is to check
    that the revision matches the latest known. \a owner is notified
    when the check is completed.

    The function expects to be called from ::main() after
    Database::setup(). It needs a runnning EventLoop.
*/

void Database::checkSchema( EventHandler * owner )
{
    Schema::checkRevision( owner );
}


/*! This function checks that the server doesn't have privileged access
    to the database. It notifies \a owner when the check is complete. A
    disaster is logged if the server is connected to the database as an
    unduly privileged user.

    The function expects to be called from ::main() after
    Database::checkSchema().
*/

void Database::checkAccess( EventHandler * owner )
{
    class AccessChecker
        : public EventHandler
    {
    public:
        Log * l;
        Query * q;
        Query * result;

        AccessChecker( EventHandler * owner )
            : l( new Log ), q( 0 ), result( 0 )
        {
            result = new Query( owner );
        }

        void execute()
        {
            if ( !q ) {
                q = new Query( "select not exists (select * from "
                               "information_schema.table_privileges where "
                               "privilege_type='DELETE' and table_name="
                               "'messages' and grantee=$1) and not exists "
                               "(select u.usename from pg_catalog.pg_class c "
                               "left join pg_catalog.pg_user u on "
                               "(u.usesysid=c.relowner) where c.relname="
                               "'messages' and u.usename=$1) as allowed",
                               this );
                q->bind( 1, Configuration::text( Configuration::DbUser ) );
                q->execute();
            }

            if ( !q->done() )
                return;

            Row * r = q->nextRow();
            if ( q->failed() || !r ||
                 r->getBoolean( "allowed" ) == false )
            {
                EString s( "Refusing to start because we have too many "
                          "privileges on the messages table in secure "
                          "mode." );
                result->setError( s );
                l->log( s, Log::Disaster );
                if ( q->failed() ) {
                    l->log( "Query: " + q->description(), Log::Disaster );
                    l->log( "Error: " + q->error(), Log::Disaster );
                }
            }
            else {
                result->setState( Query::Completed );
            }

            result->notify();
        }
    };

    AccessChecker * a = new AccessChecker( owner );
    a->execute();
}


/*! Returns the number of handles we think we need at this
    time. Mostly computed based on recent workload.
*/

uint Database::handlesNeeded()
{
    if ( server().protocol() == Endpoint::Unix || !::busyDbConnections )
        return handles->count();

    uint i = Configuration::scalar( Configuration::DbHandleInterval );
    uint t = (uint)time( 0 );

    // we start by looking at the maximum number we've needed in the
    // past four minutes
    uint needed = ::busyDbConnections->maximumSince( t - 2*i );

    // we drop only once per five seconds
    uint recently = ::totalDbConnections->maximumSince( t - 5 );
    if ( needed < recently - 1 )
        needed = recently - 1;

    // we do need a handle
    if ( needed < 1 )
        needed = 1;

    // now that we know...
    return needed;
}


/*! Returns the number of completely idle handles at the moment. */

uint Database::idleHandles()
{
    uint r = 0;
    List< Database >::Iterator it( handles );
    while ( it ) {
        if ( it->state() == Idle )
            r++;
        ++it;
    }
    return r;
}


/*! \fn void Database::cancel( class Query * query )
    Cancels the given \a query if it is being executed by this database object.
    Does nothing otherwise.
*/


/*! This function issues a cancel request for the query \a q, if it is
    currently being executed. If not, it does nothing.
*/

void Database::cancelQuery( Query * q )
{
    List<Database>::Iterator it( handles );
    while ( it ) {
        it->cancel( q );
        ++it;
    }
}


/*! This static function returns the schema revision current at the time
    this server was compiled.
*/

uint Database::currentRevision()
{
    return 95;
}


/*! Removes a submitted transaction from the global list and returns a
    list contains just that transaction.

    If \a transactionOK is true, the list is permitted to start a
    Transaction. If not, only standalone queries are considered.

    Returns an empty list if no suitable queries can be found.
*/

List< Query > * Database::firstSubmittedQuery( bool transactionOK )
{
    List<Query>::Iterator i( queries );
    if ( !transactionOK )
        while ( i && i->transaction() )
            ++i;
    List<Query> * r = new List<Query>();
    if ( i ) {
        r->append( i );
        queries->take( i );
    }
    return r;
}
