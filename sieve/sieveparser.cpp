// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "sieveparser.h"

#include "sievescript.h"
#include "sieveproduction.h"
#include "stringlist.h"



/*! \class SieveParser sieveparser.h

    The SieveParser class does all the ABNF-related work for sieve
    scripts. The SieveProduction class and its subclasses do the other
    part of parsing: The Sieve ABNF grammar doesn't guarantee that if
    has a test as argument, etc.
*/


/*!  Constructs a Sieve parser for \a s. Doesn't actually do anything;
     the caller must call commands() or other functions to parse the
     script.
*/

SieveParser::SieveParser( const String &s  )
    : AbnfParser( s )
{
}


/*! Parses/skips a bracket comment: bracket-comment = "/" "*"
    *not-star 1*STAR *(not-star-slash *not-star 1*STAR) "/"

    No "*" "/" allowed inside a comment. (No * is allowed unless it is
    the last character, or unless it is followed by a character that
    isn't a slash.)
*/

void SieveParser::bracketComment()
{
    if ( !present( "/" "*" ) )
        return;
    int i = input().find( "*" "/", pos() );
    if ( i < 0 )
        setError( "Bracket comment not terminated" );
    else
        step( i+2-pos() );
}


/*! Parses/skips a comment: comment = bracket-comment / hash-comment.

*/

void SieveParser::comment()
{
    bracketComment();
    hashComment();
}


/*! Parses/skips a hash-cmment: hash-comment = "#" *octet-not-crlf
    CRLF
*/

void SieveParser::hashComment()
{
    if ( !present( "#" ) )
        return;
    int i = input().find( "\r" "\n", pos() );
    if ( i < 0 )
        setError( "Could not find CRLF in hash comment" );
    else
        step( i+2-pos() );

}


/*! identifier = (ALPHA / "_") *(ALPHA / DIGIT / "_")

    Records an error if no identifier is present.
*/

String SieveParser::identifier()
{
    String r;
    char c = nextChar();
    while ( ( c == '_' ) ||
            ( c >= 'a' && c <= 'z' ) ||
            ( c >= 'A' && c <= 'Z' ) ||
            ( c >= '0' && c <= '9' && !r.isEmpty() ) ) {
        r.append( c );
        step();
        c = nextChar();
    }
    if ( r.isEmpty() )
        setError( "Could not find an identifier" );
    return r; // XXX - better to return r.lower()?

}


/*! multi-line = "text:" *(SP / HTAB) (hash-comment / CRLF)
    *(multiline-literal / multiline-dotstuff) "." CRLF

    multiline-literal = [octet-not-period *octet-not-crlf] CRLF

    multiline-dotstuff = "." 1*octet-not-crlf CRLF

    Returns an empty string if the cursor doesn't point at a
    multi-line string.
*/

String SieveParser::multiLine()
{
    String r;
    require( "text:" );
    while ( nextChar() == ' ' || nextChar() == '\t' )
        step();
    if ( !present( "\r\n" ) )
        hashComment();
    while ( !atEnd() && !present( ".\r\n" ) ) {
        if ( nextChar() == '.' )
            step();
        while ( !atEnd() && nextChar() != '\r' ) {
            r.append( nextChar() );
            step();
        }
        require( "\r\n" );
    }
    return r;
}


/*! number = 1*DIGIT [ QUANTIFIER ]

    QUANTIFIER = "K" / "M" / "G"

    Returns 0 and calls setError() in case of error.
*/

uint SieveParser::number()
{
    bool ok = false;
    String d = digits( 1, 30 );
    uint n = d.number( &ok );
    uint f = 1;
    if ( present( "k" ) )
        f = 1024;
    else if ( present( "m" ) )
        f = 1024*1024;
    else if ( present( "g" ) )
        f = 1024*1024*1024;

    if ( !ok )
        setError( "Number " + d + " is too large" );
    else if ( n > UINT_MAX / f )
        setError( "Number " + fn( n ) + " is too large when scaled by " +
                  fn( f ) );
    n = n * f;
    return n;
}


/*! quoted-string = DQUOTE quoted-text DQUOTE

    quoted-text = *(quoted-safe / quoted-special / quoted-other)

    quoted-other = "\" octet-not-qspecial

    quoted-safe = CRLF / octet-not-qspecial

    quoted-special     = "\" ( DQUOTE / "\" )
*/

String SieveParser::quotedString()
{
    String r;
    require( "\"" );
    while ( !atEnd() && !nextChar() == '"' ) {
        if ( present( "\r\n" ) ) {
            r.append( "\r\n" );
        }
        else {
            if ( nextChar() == '\\' )
                step();
            r.append( nextChar() );
            step();
        }
    }
    require( "\"" );
    return r;
}


/*! tag = ":" identifier */

String SieveParser::tag()
{
    require( ":" );
    return ":" + identifier();
}


/*! Parses and skips whatever whitespace is at pos(). */

void SieveParser::whitespace()
{
    uint p;
    do {
        p = pos();
        switch( nextChar() ) {
        case '#':
        case '/':
            comment();
            break;
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            step();
            break;
        }
    } while ( pos() > p );
}


/*! ADDRESS-PART = ":localpart" / ":domain" / ":all" */

String SieveParser::addressPart()
{
    if ( present( ":localpart" ) )
        return ":localpart";
    else if ( present( ":domain" ) )
        return ":domain";
    else if ( present( ":all" ) )
        return ":all";
    setError( "Address-part not found "
              "(should be :localpart, :domain or :all)" );
    return "";
}


/*! Parses, constructs and returns a SieveArgument object. Never
    returns 0.

    argument     = string-list / number / tag
*/

SieveArgument * SieveParser::argument()
{
    SieveArgument * sa = new SieveArgument;
    sa->setStart( pos() );

    if ( nextChar() == ':' )
        sa->setTag( tag() );
    else if ( nextChar() >= '0' && nextChar() <= '9' )
        sa->setNumber( number() );
    else
        sa->setStringList( stringList() );

    sa->setError( error() );
    sa->setEnd( pos() );
    return sa;
}


/*! arguments = *argument [test / test-list]

    This is tricky. The first difficult bit. This class takes either a
    list of SieveArgument objects, or a SieveTest, or a list of
    SieveTest objects. We implement both the arguments and tests
    productions in this function.
*/

class SieveArgumentList * SieveParser::arguments()
{
    SieveArgumentList * sal = new SieveArgumentList;
    sal->setStart( pos() );

    uint m = 0;
    while ( ok() ) {
        m = mark();
        SieveArgument * sa = argument();
        if ( ok() )
            sal->append( sa );
    }
    restore( m );

    if ( present( "(" ) ) {
        // it's a test-list
        sal->append( test() );
        while ( present( "," ) )
            sal->append( test() );
        require( ")" );
    }
    else if ( ok() ) {
        // it's either a test or nothing
        m = mark();
        SieveTest * st = test();
        if ( ok() )
            sal->append( st );
        else
            restore( m );
    }

    sal->setError( error() );
    sal->setEnd( pos() );
    return sal;
}


/*! block = "{" *command "}"
*/

class SieveBlock * SieveParser::block()
{
    SieveBlock * sb = new SieveBlock;
    sb->setStart( pos() );
    require( "{" );

    uint m = 0;
    while ( ok() ) {
        mark();
        SieveCommand * c = command();
        if ( ok() )
            sb->append( c );
    }
    restore( m );

    require( "}" );
    sb->setError( error() );
    sb->setEnd( pos() );
    return sb;
}


/*! command = identifier arguments ( ";" / block )
*/

class SieveCommand * SieveParser::command()
{
    SieveCommand * sc = new SieveCommand;
    sc->setStart( pos() );

    sc->setIdentifier( identifier() );
    sc->setArguments( arguments() );
    if ( !present( ";" ) )
        sc->setBlock( block() );

    sc->setError( error() );
    sc->setEnd( pos() );
    return sc;
}


/*! commands = *command

    start = commands

    Never returns a null pointer.
*/

List<SieveCommand> * SieveParser::commands()
{
    List<SieveCommand> * l = new List<SieveCommand>;
    uint m = 0;
    while ( ok() ) {
        m = mark();
        SieveCommand * c = command();
        if ( ok() )
            l->append( c );
    }
    restore( m );
    return l;
}


/*! COMPARATOR = ":comparator" string

    Returns just the string, not ":comparator" or the whitespace.
*/

String SieveParser::comparator()
{
    require( ":comparator" );
    return string();
}


/*! MATCH-TYPE = ":is" / ":contains" / ":matches"

    Returns the match-type in lower case, with the ":" prefix.
*/

String SieveParser::matchType()
{
    if ( present( ":is" ) )
        return ":is";
    else if ( present( ":contains" ) )
        return ":contains";
    else if ( present( ":matches" ) )
        return ":matches";
    setError( "Expected match-type (:is/:contains/:matches)" );
    return "";
}


/*! string = quoted-string / multi-line

*/

String SieveParser::string()
{
    if ( nextChar() == '"' )
        return quotedString();
    return multiLine();
}


/*! string-list = "[" string *("," string) "]" / string

    If there is only a single string, the brackets are optional.

    Never returns a null pointer.
*/

StringList * SieveParser::stringList()
{
    StringList * l = new StringList;
    if ( present( "[" ) ) {
        step();
        l->append( string() );
        while ( present( "," ) )
            l->append( string() );
        require( "]" );
    }
    else {
        l->append( string() );
    }
    return l;
}


/*! test = identifier arguments
*/

class SieveTest * SieveParser::test()
{
    SieveTest * t = new SieveTest;
    t->setStart( pos() );
    t->setIdentifier( identifier() );
    if ( ok() )
        t->setArguments( arguments() );
    t->setError( error() );
    t->setEnd( pos() );
    return t;
}
