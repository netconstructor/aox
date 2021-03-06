// Copyright 2009 The Archiveopteryx Developers <info@aox.org>

#include "smtphelo.h"

#include "smtpparser.h"
#include "smtpclient.h"
#include "mechanism.h"
#include "scope.h"
#include "smtp.h"

#include <time.h> // time()


/*! \class SmtpHelo smtphelo.h

    Models the three commands HELO, EHLO and LHLO, as specified by RFC
    2821 and RFC 4409. Kept in a separate file to reduce the #include
    clutter and dependency maze.
*/


/*! Parses and executes a HELO/EHLO/LHLO command of type \a t for \a
    s using \a p.
*/

SmtpHelo::SmtpHelo( SMTP * s, SmtpParser * p, Type t )
    : SmtpCommand( s )
{
    Scope x( log() );
    if ( t == Lhlo && s->dialect() != SMTP::Lmtp )
        respond( 500, "LHLO is valid only in LMTP", "5.5.1" );
    else if ( s->dialect() == SMTP::Lmtp && t != Lhlo )
        respond( 500, "Need LHLO in LMTP", "5.5.0" );
    p->whitespace();
    s->setHeloName( p->domain() );
    p->whitespace();
    p->end();
    if ( !p->ok() )
        return;
    respond( 250, Configuration::hostname() );
    if ( t == Ehlo || t == Lhlo ) {
        if ( s->dialect() != SMTP::Lmtp ) {
            EString a( SaslMechanism::allowedMechanisms( "", s->hasTls() ) );
            respond( 0, "AUTH " + a );
            // should we also send AUTH=?
            // respond( 0, "AUTH=" + a );
        }
        respond( 0, "BURL IMAP IMAP://" + Configuration::hostname() );
        if ( s->dialect() == SMTP::Submit ) {
            int delay = 1901520000 - ::time( 0 );
            respond( 0, "FUTURERELEASE " + fn( delay ) + " " +
                     "2030-04-04T08:00:00Z" );
        }
        respond( 0, "ENHANCEDSTATUSCODES" );
        if ( SmtpClient::observedSize() > 0 && s->dialect() == SMTP::Submit )
            respond( 0, "SIZE " + fn( SmtpClient::observedSize() ) );
        respond( 0, "BINARYMIME" );
        respond( 0, "PIPELINING" );
        respond( 0, "8BITMIME" );
        respond( 0, "CHUNKING" );
        if ( !s->hasTls() && Configuration::toggle( Configuration::UseTls ) )
            respond( 0, "STARTTLS" );
        respond( 0, "SIZE" );
        respond( 0, "DSN" );
    }
    finish();
}
