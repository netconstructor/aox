// Copyright 2009 The Archiveopteryx Developers <info@aox.org>

#include "user.h"

#include "helperrowcreator.h"
#include "configuration.h"
#include "transaction.h"
#include "address.h"
#include "mailbox.h"
#include "query.h"


class UserData
    : public Garbage
{
public:
    UserData()
        : id( 0 ), inbox( 0 ), inboxId( 0 ), home( 0 ), address( 0 ), quota( 0 ),
          q( 0 ), result( 0 ), t( 0 ), user( 0 ),
          state( User::Unverified ),
          mode( LoungingAround )
    {}

    UString login;
    UString secret;
    UString ldapdn;
    uint id;
    Mailbox * inbox;
    uint inboxId;
    Mailbox * home;
    Address * address;
    int64 quota;
    Query * q;
    Query * result;
    Transaction * t;
    EventHandler * user;
    EString error;
    User::State state;

    enum Operation {
        LoungingAround,
        Creating,
        Refreshing,
        ChangingSecret
    };
    Operation mode;
};


/*! \class User user.h

    The User class models a single Archiveopteryx user, which may be
    able to log in, own Mailbox objects, etc.
*/


/*! Constructs an empty User. The result does not map to anything in
    the database.
*/

User::User()
    : d( new UserData )
{
    // nothing
}


/*! Returns the user's state, which is either Unverified (the object has
    made no attempt to refresh itself from the database), Refreshed (the
    object was successfully refreshed) or Nonexistent (the object tried
    to refresh itself, but there was no corresponsing user in the
    database).

    The state is Unverified initially and is changed by refresh().
*/

User::State User::state() const
{
    return d->state;
}


/*! Sets this User's id() to \a id. */

void User::setId( uint id )
{
    d->id = id;
}


/*! Returns the user's ID, ie. the primary key from the database, used
    to link various other tables to this user.
*/

uint User::id() const
{
    return d->id;
}


/*! Sets this User object to have login \a string. The database is not
    updated - \a string is not used except to create Query objects
    during e.g. refresh().
*/

void User::setLogin( const UString & string )
{
    d->login = string;
}


/*! Returns the User's login string, which is an empty string
    initially and is set up by refresh().
*/

UString User::login() const
{
    return d->login;
}


/*! Sets this User to have \a secret as password. The database isn't
    updated unless e.g. create() is called.
*/

void User::setSecret( const UString & secret )
{
    d->secret = secret;
}


/*! Returns the User's secret (password), which is an empty string
    until refresh() has fetched the database contents.
*/

UString User::secret() const
{
    return d->secret;
}


/*! Returns the user's LDAP DN, or an empty string if this user
    doesn't have an LDAP DN. The LDAP DN is only used for performing
    LDAP authentication (by LdapRelay).
*/

UString User::ldapdn() const
{
    return d->ldapdn;
}


/*! Returns a pointer to the user's inbox, or a null pointer if this
    object doesn't know it or if the user has none.
*/

Mailbox * User::inbox() const
{
    if ( !d->inbox )
        d->inbox = Mailbox::find( d->inboxId );
    return d->inbox;
}


/*! Sets this User object to have address \a a. The database is not
    updated - \a a is not used except maybe to search in refresh().
*/

void User::setAddress( Address * a )
{
    d->address = a;
}


/*! Returns the address belonging to this User object, or a null
    pointer if this User has no Address.
*/

Address * User::address()
{
    if ( !d->address ) {
        // XXX: This does not match the documentation above.
        EString dom = Configuration::hostname();
        uint i = dom.find( '.' );
        if ( i > 0 )
            dom = dom.mid( i+1 );
        d->address = new Address( "", d->login.utf8(), dom );
    }
    return d->address;
}


/*! Returns the user's "home directory" - the mailbox under which all
    of the user's mailboxes reside.

    This is read-only since at the moment, the Archiveopteryx servers
    only permit one setting: "/users/" + login. However, the database
    permits more namespaces than just "/users", so one day this may
    change.
*/

Mailbox * User::home() const
{
    return d->home;
}


/*! Returns the mailbox to which \a name refers, or a null pointer if
    there is no such mailbox.
*/

Mailbox * User::mailbox( const UString & name ) const
{
    return Mailbox::find( mailboxName( name ) );
}


/*! Returns the canonical name of the mailbox to which \a name
    refers. This need not be the name of an existing mailbox, or even
    well-formed.
*/

UString User::mailboxName( const UString & name ) const
{
    if ( name.titlecased() == "INBOX" )
        return inbox()->name();
    if ( name.startsWith( "/" ) )
        return name;
    UString n;
    n = home()->name();
    n.append( '/' );
    n.append( name );
    return n;
}


/*! Returns true if this user is known to exist in the database, and
    false if it's unknown or doesn't exist.
*/

bool User::exists()
{
    return d->id > 0;
}


void User::execute()
{
    switch( d->mode ) {
    case UserData::Creating:
        createHelper();
        break;
    case UserData::Refreshing:
        refreshHelper();
        break;
    case UserData::ChangingSecret:
        csHelper();
        break;
    case UserData::LoungingAround:
        break;
    }
}


static PreparedStatement * psl;
static PreparedStatement * psa;


/*! Starts refreshing this object from the database, and remembers to
    call \a user when the refresh is complete.
*/

void User::refresh( EventHandler * user )
{
    if ( d->q )
        return;
    d->user = user;
    if ( !psl ) {
        psl = new PreparedStatement(
            "select u.id, u.login, u.secret, u.ldapdn, "
            "a.name, a.localpart, a.domain, u.quota, "
            "al.mailbox as inbox, n.name as parentspace "
            "from users u "
            "join namespaces n on (u.parentspace=n.id) "
            "left join aliases al on (u.alias=al.id) "
            "left join addresses a on (al.address=a.id) "
            "where lower(u.login)=lower($1)"
        );

        psa = new PreparedStatement(
            "select u.id, u.login, u.secret, u.ldapdn, "
            "a.name, a.localpart, a.domain, u.quota, "
            "al.mailbox as inbox, n.name as parentspace "
            "from users u "
            "join namespaces n on (u.parentspace=n.id) "
            "join aliases al on (u.alias=al.id) "
            "join addresses a on (al.address=a.id) "
            "where lower(a.localpart)=$1 and lower(a.domain)=$2"
        );
    }
    if ( !d->login.isEmpty() ) {
        d->q = new Query( *psl, this );
        d->q->bind( 1, d->login );
    }
    else if ( d->address ) {
        d->q = new Query( *psa, this );
        d->q->bind( 1, d->address->localpart().lower() );
        d->q->bind( 2, d->address->domain().lower() );
    }
    if ( d->q ) {
        d->q->execute();
        d->mode = UserData::Refreshing;
    }
    else {
        d->state = Nonexistent;
    }
}


/*! Parses the query results for refresh(). */

void User::refreshHelper()
{
    if ( !d->q || !d->q->done() )
        return;

    d->state = Nonexistent;
    Row * r = d->q->nextRow();
    if ( r ) {
        d->id = r->getInt( "id" );
        d->login = r->getUString( "login" );
        if ( r->isNull( "secret" ) )
            d->secret.truncate();
        else
            d->secret = r->getUString( "secret" );
        d->inboxId = r->getInt( "inbox" );
        if ( r->isNull( "ldapdn" ) )
            d->ldapdn.truncate();
        else
            d->ldapdn = r->getUString( "ldapdn" );
        UString tmp = r->getUString( "parentspace" );
        tmp.append( '/' );
        tmp.append( d->login );
        d->home = Mailbox::obtain( tmp, true );
        d->home->setOwner( d->id );
        if ( r->isNull( "localpart" ) ) {
            d->address = new Address( "", "", "" );
        }
        else {
            UString n = r->getUString( "name" );
            EString l = r->getEString( "localpart" );
            EString h = r->getEString( "domain" );
            d->address = new Address( n, l, h );
        }
        d->quota = r->getBigint( "quota" );
        d->state = Refreshed;
    }
    if ( d->user )
        d->user->execute();
}


/*! This function is used to create a user on behalf of \a owner.

    It returns a pointer to a Query that can be used to track the
    progress of the operation. If (and only if) this Query hasn't
    already failed upon return from this function, the caller must
    call execute() to initiate the operation.

    The query may fail immediately if the user is not valid(), or if it
    already exists().

    This function (indeed, this whole class) is overdue for change.
*/

Query * User::create( EventHandler * owner )
{
    Query *q = new Query( owner );

    if ( !valid() ) {
        q->setError( "Invalid user data." );
    }
    else if ( exists() ) {
        q->setError( "User exists already." );
    }
    else {
        d->q = 0;
        d->t = new Transaction( this );
        d->mode = UserData::Creating;
        d->state = Unverified;
        d->user = owner;
        d->result = q;
    }

    return q;
}


/*! This private function carries out create() work on behalf of
    execute().
*/

void User::createHelper()
{
    Address * a = address();

    if ( !d->q ) {
        if ( !a->id() ) {
            AddressCreator * ac = new AddressCreator( a, d->t );
            ac->execute();
        }

        d->q = new Query( "select name from namespaces where id="
                          "(select max(id) from namespaces)", this );
        d->t->enqueue( d->q );
        d->t->execute();
    }

    if ( d->q->done() && a->id() && !d->inbox ) {
        Row *r = d->q->nextRow();
        if ( !r ) {
            d->t->commit();
            return;
        }

        UString m = r->getUString( "name" );
        m.append( '/' );
        m.append( d->login );
        m.append( "/INBOX" );
        d->inbox = Mailbox::obtain( m, true );
        d->inbox->create( d->t, 0 );

        Query * q1
            = new Query( "insert into aliases (address, mailbox) values "
                         "($1, (select id from mailboxes where name=$2))", 0 );
        q1->bind( 1, a->id() );
        q1->bind( 2, m );
        d->t->enqueue( q1 );

        Query * q2
            = new Query( "insert into users "
                         "(alias,parentspace,login,secret,quota) values "
                         "((select id from aliases where address=$1), "
                         "(select max(id) from namespaces), $2, $3, "
                         "coalesce((select quota from users group by quota"
                         " order by count(*) desc limit 1), 2147483647))", 0 );
        q2->bind( 1, a->id() );
        q2->bind( 2, d->login );
        q2->bind( 3, d->secret );
        d->t->enqueue( q2 );

        Query *q3 =
            new Query( "update mailboxes set "
                       "owner=(select id from users where login=$1) "
                       "where name=$2 or name like $2||'/%'", 0 );
        q3->bind( 1, d->login );
        q3->bind( 2, m );
        d->t->enqueue( q3 );

        d->t->commit();
    }

    if ( !d->t->done() )
        return;

    if ( d->t->failed() ) {
        d->state = Nonexistent;
        d->result->setError( d->t->error() );
    }
    else {
        d->state = Refreshed;
        d->result->setState( Query::Completed );
    }

    d->result->notify();
}


/*! Enqueues a query to remove this user in the Transaction \a t, and
    returns the Query. Does not commit the Transaction.
*/

Query * User::remove( Transaction * t )
{
    Query * q = new Query( "delete from users where login=$1", 0 );
    q->bind( 1, d->login );
    t->enqueue( q );
    return q;
}


/*! This function changes a user's password on behalf of \a owner.

    It returns a pointer to a Query that can be used to track the
    progress of the operation. If (and only if) this Query hasn't
    already failed upon return from this function, the caller must
    call execute() to initiate the operation.
*/

Query * User::changeSecret( EventHandler * owner )
{
    Query *q = new Query( owner );

    d->q = 0;
    d->mode = UserData::ChangingSecret;
    d->user = owner;
    d->result = q;

    return q;
}


/*! Finish the work of changeSecret(). */

void User::csHelper()
{
    if ( !d->q ) {
        d->q =
            new Query( "update users set secret=$1 where login=$2",
                       this );
        d->q->bind( 1, d->secret );
        d->q->bind( 2, d->login );
        d->q->execute();
    }

    if ( !d->q->done() )
        return;

    if ( d->q->failed() )
        d->result->setError( d->q->error() );
    else
        d->result->setState( Query::Completed );

    d->result->notify();
}


/*! Returns true if this user is valid, that is, if it has the
    information that must be present in order to write it to the
    database and do not have defaults.

    Sets error() if applicable.
*/

bool User::valid()
{
    if ( d->login.isEmpty() ) {
        d->error = "Login name must be supplied";
        return false;
    }

    return true;
}


/*! Returns a textual description of the last error seen, or a null
    string if everything is in order. The string is set by valid() and
    perhaps other functions.
*/

EString User::error() const
{
    return d->error;
}


/*! Returns the user's database quota, as recorderd by users.quota. */

int64 User::quota() const
{
    return d->quota;
}
