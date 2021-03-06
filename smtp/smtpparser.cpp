// Copyright 2009 The Archiveopteryx Developers <info@aox.org>

#include "smtpparser.h"

#include "address.h"


/*! \class SmtpParser smtpparser.h
    SMTP-specific ABNF parsing functions.

    This subclass of AbnfParser provides functions to parse SMTP
    protocol elements as defined in RFC 2821.
*/

/*! Creates a new SmtpParser object for the string \a s, which is
    assumed to be a complete SMTP command line (not including the
    terminating CRLF), as received from the client.
*/

SmtpParser::SmtpParser( const EString &s )
    : AbnfParser( s )
{
}


/*! Returns an SMTP cmmand, always in lower case. */

EString SmtpParser::command()
{
    EString c;
    c.append( letters( 1, 10 ).lower() );
    if ( c == "mail" || c == "rcpt" ) {
        whitespace();
        c.append( " " );
        c.append( letters( 2, 4 ).lower() );
    }
    return c;
}


/*! Skips whitespace. */

void SmtpParser::whitespace()
{
    uint p;
    do {
        p = pos();
        switch( nextChar() ) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            step();
            break;
        }
    } while ( ok() && pos() > p );
}


/*! Parses and returns a domain. The domain literal form is somewhat
    too flexible (read: totally botched).

    As a hack, a final "." is overlooked if the next character is a
    ">", as in "rcpt to: <user@example.org.>".
*/

EString SmtpParser::domain()
{
    EString r;
    if ( nextChar() == '[' ) {
        uint start = pos();
        while ( !atEnd() && nextChar() != ']' )
            step();
        require( "]" );
        r = input().mid( start, pos() - start );
    }
    else {
        r = subDomain();
        while ( nextChar() == '.' ) {
            step();
            if ( nextChar() != '>' ) {
                r.append( "." );
                r.append( subDomain() );
            }
        }
    }
    return r;
}


/*! Returns the RFC 2821 sub-domain production: sub-domain = Let-dig
    [Ldh-str]
*/

EString SmtpParser::subDomain()
{
    EString r;
    char c = nextChar();
    if ( ( c >= 'a' && c <= 'z' ) ||
         ( c >= 'A' && c <= 'Z' ) ||
         ( c >= '0' && c <= '9' ) ) {
        do {
            r.append( c );
            step();
            c = nextChar();
        } while ( ( ( c >= 'a' && c <= 'z' ) ||
                    ( c >= 'A' && c <= 'Z' ) ||
                    ( c >= '0' && c <= '9' ) ||
                    ( c == '-' ) ) );
    }
    if ( r.isEmpty() && c == '.' )
        setError( "Consecutive dots aren't permitted" );
    else if ( r.isEmpty() )
        setError( "Domain cannot end with a dot" );
    else if ( r[r.length()-1] == '-' )
        setError( "subdomain cannot end with hyphen (" + r + ")" );
    return r;
}


/*! Returns a pointer to an address, which is never a null pointer but
    may point to a somewhat strange address if there is a parse error.
*/

class Address * SmtpParser::address()
{
    bool lt = false;
    if ( present( "<" ) ) {
        lt = true;
        if ( present( "@" ) ) {
            (void)domain();
            while ( present( ",@" ) )
                (void)domain();
            require( ":" );
        }
    }

    EString lp;
    if ( nextChar() == '"' )
        lp = quotedString();
    else
        lp = dotString();
    if ( !present( "@" ) )
        setError( "Address must have both localpart and domain" );
    Address * a = new Address( "", lp, domain() );
    if ( lt )
        require( ">" );
    return a;
}


/*! Returns an RFC 2821 dot-string. */

EString SmtpParser::dotString()
{
    EString r( atom() );
    while ( nextChar() == '.' ) {
        r.append( "." );
        step();
        r.append( atom() );
    }
    return r;
}


/*! Returns a quoted-string as defined in RFC 2822 (and used in RFC
    2821). Does not enforce the ASCII-only rule.
*/

EString SmtpParser::quotedString()
{
    require( "\"" );
    EString r;
    while ( ok() && !atEnd() && nextChar() != '"' ) {
        if ( nextChar() == '\\' )
            step();
        r.append( nextChar() );
        step();
    }
    require( "\"" );
    return r;
}


/*! Returns the atom production from RFC 2821 and RFC 2822. (atext
    from 2822, atom from 2821.)
*/

EString SmtpParser::atom()
{
    char c = nextChar();
    EString r;
    while ( ( c >= 'a' && c <= 'z' ) ||
            ( c >= 'A' && c <= 'Z' ) ||
            ( c >= '0' && c <= '9' ) ||
            c == '!' || c == '#' ||
            c == '$' || c == '%' ||
            c == '&' || c == '\'' ||
            c == '*' || c == '+' ||
            c == '-' || c == '/' ||
            c == '=' || c == '?' ||
            c == '^' || c == '_' ||
            c == '`' || c == '{' ||
            c == '|' || c == '}' ||
            c == '~' ) {
        r.append( c );
        step();
        c = nextChar();
    }
    if ( r.isEmpty() )
        setError( "Expected atom, saw: " + following() );
    return r;
}


/*! Parses and returns an ESMTP parameter name: esmtp-keyword = (ALPHA
    / DIGIT) *(ALPHA / DIGIT / "-")

    Always returns lower case.
*/

EString SmtpParser::esmtpKeyword()
{
    char c = nextChar();
    EString r;
    while ( ( c >= 'a' && c <= 'z' ) ||
            ( c >= 'A' && c <= 'Z' ) ||
            ( c >= '0' && c <= '9' ) ||
            ( c == '-' && !r.isEmpty() ) ) {
        r.append( c );
        step();
        c = nextChar();
    }
    if ( r.isEmpty() )
        setError( "Expected esmtp parameter keyword, saw: " + following() );
    return r.lower();
}


/*! Parses an ESMTP parameter value: esmtp-value = 1*(%d33-60 /
    %d62-127)
*/

EString SmtpParser::esmtpValue()
{
    EString r;
    char c = nextChar();
    while ( !atEnd() && c != '=' && c > 32 && c < 128 ) {
        r.append( c );
        step();
        c = nextChar();
    }
    if ( r.isEmpty() )
        setError( "Expected esmtp parameter value, saw: " + following() );
    return r;
}
