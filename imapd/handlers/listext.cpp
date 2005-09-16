// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "listext.h"

#include "string.h"
#include "stringlist.h"
#include "mailbox.h"
#include "query.h"
#include "user.h"


class ListextData
    : public Garbage
{
public:
    ListextData():
        selectQuery( 0 ),
        subscribed( 0 ),
        reference( 0 ),
        extended( false ),
        returnSubscribed( false ), returnChildren( false ),
        selectSubscribed( false ), selectRemote( false ),
        selectRecursiveMatch( false )
    {}

    Query * selectQuery;
    List<Mailbox> * subscribed;
    Mailbox * reference;
    StringList patterns;

    bool extended;
    bool returnSubscribed;
    bool returnChildren;
    bool selectSubscribed;
    bool selectRemote;
    bool selectRecursiveMatch;
};


/*! \class Listext listext.h

    The Listext class implements the extended List command, ie. the
    List command from imap4rev1 with the extensions added since.

    The extension grammar is intentionally kept minimal, since it's
    still a draft. Currently based on draft-ietf-imapext-list-extensions-09.

    Mailstore does not support remote mailboxes, so the listext option
    to show remote mailboxes is silently ignored.

    This class contains a few utility functions used by Lsub, since
    the two share so much behaviour, namely match(), reference() and
    combinedName().
*/


/*!  Constructs an empty List handler. */

Listext::Listext()
    : d( new ListextData )
{
}


/*! Note that the extensions are always parsed, even if the no
    extension has been advertised using CAPABILITY.
*/

void Listext::parse()
{
    // list = "LIST" [SP list-select-opts] SP mailbox SP mbox-or-pat

    space();

    if ( present( "(" ) ) {
        d->extended = true;
        // list-select-opts = "(" [list-select-option
        //                    *(SP list-select-option)] ")"
        // list-select-option = "SUBSCRIBED" / "REMOTE" / "MATCHPARENT" /
        //                      option-extension
        addSelectOption( atom().lower() );
        while ( present( " " ) )
            addSelectOption( atom().lower() );
        require( ")" );
        space();
    }

    reference();
    space();

    // mbox-or-pat = list-mailbox / patterns
    // patterns = "(" list-mailbox *(SP list-mailbox) ")"
    if ( present( "(" ) ) {
        d->extended = true;

        d->patterns.append( listMailbox() );
        while ( present( " " ) )
            d->patterns.append( listMailbox() );
        require( ")" );
    }
    else {
        d->patterns.append( listMailbox() );
    }

    // list-return-opts = "RETURN (" [return-option *(SP return-option)] ")"
    if ( present( "return (" ) ) {
        d->extended = true;

        addReturnOption( atom().lower() );
        while ( present( " " ) )
            addReturnOption( atom().lower() );
        require( ")" );
    }
    end();

    if ( d->selectRecursiveMatch && !d->selectSubscribed )
        error( Bad, "Recursivematch alone won't do" );

    if ( d->selectSubscribed )
        d->returnSubscribed = true;

    if ( d->returnSubscribed )
        d->subscribed = new List<Mailbox>;
}


void Listext::execute()
{
    if ( d->returnSubscribed || d->selectSubscribed ) {
        if ( !d->selectQuery ) {
            d->selectQuery = new Query( "select mailbox from subscriptions "
                                        "where owner=$1", this );
            d->selectQuery->bind( 1, imap()->user()->id() );
            d->selectQuery->execute();
        }
        Row * r = 0;
        while ( (r=d->selectQuery->nextRow()) != 0 )
            d->subscribed->append( Mailbox::find( r->getInt( "mailbox" ) ) );
        if ( !d->selectQuery->done() )
            return;
        if ( d->selectQuery->failed() )
            respond( "* NO Unable to get list of selected mailboxes: " +
                     d->selectQuery->error() );
    }

    StringList::Iterator it( d->patterns );
    while ( it ) {
        if ( it->isEmpty() )
            respond( "LIST \"/\" \"\"" );
        else if ( it->startsWith( "/" ) )
            listChildren( Mailbox::root(), *it );
        else
            listChildren( d->reference, *it );
        ++it;
    }

    finish();
}


/*! Parses and remembers the return \a option, or emits a suitable
    error. \a option must be in lower case.*/

void Listext::addReturnOption( const String & option )
{
    if ( option == "subscribed" )
        d->returnSubscribed = true;
    else if ( option == "children" )
        d->returnChildren = true;
    else
        error( Bad, "Unknown return option: " + option );
}


/*! Parses the selection \a option, or emits a suitable error. \a
    option must be lower-cased. */

void Listext::addSelectOption( const String & option )
{
    if ( option == "subscribed" )
        d->selectSubscribed = true;
    else if ( option == "remote" )
        d->selectRemote = true;
    else if ( option == "recursivematch" )
        d->selectRecursiveMatch = true;
    else
        error( Bad, "Unknown selection option: " + option );
}


/*! This extremely slow pattern matching helper checks that \a pattern
    (starting at character \a p) matches \a name (starting at
    character \a n), and returns 2 in case of match, 1 if a child of
    \a name might match, and 0 if neither is the case.
*/

uint Listext::match( const String & pattern, uint p,
                     const String & name, uint n )
{
    uint r = 0;
    while ( p <= pattern.length() ) {
        if ( pattern[p] == '*' || pattern[p] == '%' ) {
            bool star = false;
            while ( pattern[p] == '*' || pattern[p] == '%' ) {
                if ( pattern[p] == '*' )
                    star = true;
                p++;
            }
            uint i = n;
            if ( star )
                i = name.length();
            else
                while ( i < name.length() && name[i] != '/' )
                    i++;
            while ( i >= n ) {
                uint s = match( pattern, p, name, i );
                if ( s == 2 )
                    return 2;
                if ( s == 1 )
                    r = 1;
                i--;
            }
        }
        else if ( p == pattern.length() && n == name.length() ) {
            // ran out of pattern and name at the same time. success.
            return 2;
        }
        else if ( pattern[p] == name[n] ) {
            // nothing. proceed.
            p++;
        }
        else if ( pattern[p] == '/' && n == name.length() ) {
            // we ran out of name and the pattern wants a child.
            return 1;
        }
        else {
            // plain old mismatch.
            return r;
        }
        n++;
    }
    return r;
}


/*! Considers whether the mailbox \a m or any of its children may match
    the pattern \a p, and if so, emits list responses. (Calls itself
    recursively to handle children.)
*/

void Listext::list( Mailbox * m, const String & p )
{
    if ( !m )
        return;

    bool matches = false;
    bool matchChildren = false;

    uint s = 0;
    if ( p[0] != '/' && p[0] != '*' )
        s = d->reference->name().length() + 1;

    switch( match( p, 0, m->name(), s ) ) {
    case 0:
        break;
    case 1:
        matchChildren = true;
        break;
    default:
        matchChildren = true;
        matches = true;
        break;
    }

    if ( matches ) {
        if ( d->selectSubscribed ) {
            List<Mailbox>::Iterator it( *d->subscribed );
            while ( it && it != m )
                ++it;
            if ( !it )
                matches = false;
        }
        else {
            if ( m->deleted() )
                matches = false;
        }
    }


    if ( matches )
        sendListResponse( m );

    if ( matchChildren )
        listChildren( m, p );
}


/*! Calls list() for each child of \a mailbox using \a pattern. */

void Listext::listChildren( Mailbox * mailbox, const String & pattern )
{
    List<Mailbox> * c = mailbox->children();
    if ( c ) {
        List<Mailbox>::Iterator it( c );
        while ( it ) {
            list( it, pattern );
            ++it;
        }
    }
}


/*! Sends a LIST or LSUB response for \a mailbox, depending on whether
    \a lsub is false or true.

    Open issue: If \a mailbox is the inbox, what should we send?
    INBOX, or the fully qualified name, or the name relative to the
    user's home directory?
*/

void Listext::sendListResponse( Mailbox * mailbox )
{
    if ( !mailbox )
        return;

    bool childSubscribed = false;
    StringList a;

    // add the easy mailbox attributes
    if ( mailbox->deleted() )
        a.append( "\\nonexistent" );
    if ( mailbox->synthetic() || mailbox->deleted() )
        a.append( "\\noselect" );
    if ( mailbox->hasChildren() )
        a.append( "\\haschildren" );
    else if ( !mailbox->deleted() )
        a.append( "\\hasnochildren" );

    // then there's subscription, which isn't too pretty
    if ( d->subscribed ) {
        List<Mailbox>::Iterator it( *d->subscribed );
        while ( it && it != mailbox )
            ++it;
        if ( it )
            a.append( "\\subscribed" );

        if ( d->selectRecursiveMatch ) {
            // recursivematch is hard work... almost O(world)
            it = d->subscribed->first();
            while ( it && !childSubscribed ) {
                Mailbox * p = it;
                while ( p && p != mailbox )
                    p = p->parent();
                if ( p )
                    childSubscribed = true;
                ++it;
            }
        }
    }

    Mailbox * home = imap()->user()->home();
    Mailbox * p = mailbox;
    while ( p && p != home )
        p = p->parent();
    String name = mailbox->name();
    if ( p )
        name = imapQuoted( name.mid( home->name().length() + 1 ), AString );

    String ext = "";
    if ( childSubscribed )
        ext = " ((\"childinfo\" (\"subscribed\")))";

    respond( "LIST (" + a.join( " " ) + ") \"/\" " + name + ext );
}


/*! Parses a reference name and returns a pointer to the relevant
    mailbox. Returns a null pointer and logs an error if something is
    wrong.
*/

void Listext::reference()
{
    String name = astring();

    if ( name[0] == '/' )
        d->reference = Mailbox::obtain( name, false );
    else if ( name.isEmpty() )
        d->reference = imap()->user()->home();
    else
        d->reference
            = Mailbox::obtain( imap()->user()->home()->name() + "/" + name,
                               false );

    if ( !d->reference )
        error( No, "Cannot find reference name " + name );
}


/*! Returns the combined name formed by interpreting the mailbox \a
    name in the context of the \a reference mailbox.

    If \a name starts with a slash, \a reference isn't dereferenceds,
    so it can be a null pointer. \a name need not be a valid mailbox
    name, it can also be e.g. a pattern.
*/

String Listext::combinedName( Mailbox * reference, const String & name )
{
    if ( name.startsWith( "/" ) )
        return name;
    else if ( reference )
        return reference->name() + "/" + name;

    return imap()->user()->home()->name() + "/" + name;
}


/*! Parses and returns a list-mailbox. This is the same as an atom(),
    except that the three additional characters %, * and ] are
    accepted.
*/

String Listext::listMailbox()
{
    String result;
    char c = nextChar();
    if ( c == '"' || c == '{' )
        return string();
    while ( c > ' ' && c < 127 &&
            c != '(' && c != ')' && c != '{' &&
            c != '"' && c != '\\' )
    {
        result.append( c );
        step();
        c = nextChar();
    }
    if ( result.isEmpty() )
        error( Bad, "list-mailbox expected, saw: " + following() );
    return result;
}
