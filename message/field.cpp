// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "field.h"

#include "date.h"
#include "ustring.h"
#include "address.h"
#include "datefield.h"
#include "mimefields.h"
#include "addressfield.h"
#include "parser.h"
#include "utf.h"


static struct {
    const char * name;
    HeaderField::Type type;
} fieldNames[] = {
    { "From", HeaderField::From },
    { "Resent-From", HeaderField::ResentFrom },
    { "Sender", HeaderField::Sender },
    { "Resent-Sender", HeaderField::ResentSender },
    { "Return-Path", HeaderField::ReturnPath },
    { "Reply-To", HeaderField::ReplyTo },
    { "To", HeaderField::To },
    { "Cc", HeaderField::Cc },
    { "Bcc", HeaderField::Bcc },
    { "Resent-To", HeaderField::ResentTo },
    { "Resent-Cc", HeaderField::ResentCc },
    { "Resent-Bcc", HeaderField::ResentBcc },
    { "Message-Id", HeaderField::MessageId },
    { "Resent-Message-Id", HeaderField::ResentMessageId },
    { "In-Reply-To", HeaderField::InReplyTo },
    { "References", HeaderField::References },
    { "Date", HeaderField::Date },
    { "Orig-Date", HeaderField::OrigDate },
    { "Resent-Date", HeaderField::ResentDate },
    { "Subject", HeaderField::Subject },
    { "Comments", HeaderField::Comments },
    { "Keywords", HeaderField::Keywords },
    { "Content-Type", HeaderField::ContentType },
    { "Content-Transfer-Encoding", HeaderField::ContentTransferEncoding },
    { "Content-Disposition", HeaderField::ContentDisposition },
    { "Content-Description", HeaderField::ContentDescription },
    { "Content-Language", HeaderField::ContentLanguage },
    { "Content-Location", HeaderField::ContentLocation },
    { "Content-Md5", HeaderField::ContentMd5 },
    { "Content-Id", HeaderField::ContentId },
    { "Mime-Version", HeaderField::MimeVersion },
    { "Received", HeaderField::Received },
    { 0, HeaderField::Other },
};


class HeaderFieldData
    : public Garbage
{
public:
    HeaderFieldData()
        : type( HeaderField::Other ), hasData( false )
    {}

    HeaderField::Type type;
    String name, data, error;
    bool hasData;
};


/*! \class HeaderField field.h
    This class models a single RFC 822 header field (e.g. From).

    This class is responsible for parsing and verifying header fields.
    Each field has a type(), name(), and value(). It is valid() if no
    error() was recorded during parsing by the various functions that
    parse() field values, e.g. parseText()).

    Users may obtain HeaderField objects only via create().
*/


/*! This private function is used by create() and assemble() to create a
    HeaderField object of a type appropriate to the given \a name.
*/

HeaderField *HeaderField::fieldNamed( const String &name )
{
    int i = 0;
    String n = name.headerCased();
    while ( fieldNames[i].name && n != fieldNames[i].name )
        i++;

    HeaderField::Type t = fieldNames[i].type;
    HeaderField *hf;

    switch ( t ) {
    default:
        hf = new HeaderField( fieldNames[i].type );
        break;

    case From:
    case ResentFrom:
    case Sender:
    case ResentSender:
    case ReturnPath:
    case ReplyTo:
    case To:
    case Cc:
    case Bcc:
    case ResentTo:
    case ResentCc:
    case ResentBcc:
    case MessageId:
    case ResentMessageId:
    case References:
        hf = new AddressField( t );
        break;

    case Date:
    case OrigDate:
    case ResentDate:
        hf = new DateField( t );
        break;

    case ContentType:
        hf = new ::ContentType;
        break;

    case ContentTransferEncoding:
        hf = new ::ContentTransferEncoding;
        break;

    case ContentDisposition:
        hf = new ::ContentDisposition;
        break;

    case ContentLanguage:
        hf = new ::ContentLanguage;
        break;
    }

    hf->setName( n );
    return hf;
}


/*! This static function returns a pointer to a new HeaderField object
    that represents the given field \a name (case-insensitive) and its
    \a value (which is parsed appropriately).

    This function is for use by the message parser.
*/

HeaderField *HeaderField::create( const String &name,
                                  const String &value )
{
    HeaderField *hf = fieldNamed( name );
    hf->parse( value );
    return hf;
}


/*! This static function returns a pointer to a new HeaderField object
    that represents the given field \a name (case-insensitive) and the
    field \a data retrieved from the database.

    This function is for use by the message fetcher.
*/

HeaderField *HeaderField::assemble( const String &name,
                                    const String &data )
{
    HeaderField *hf = fieldNamed( name );
    hf->reassemble( data );
    return hf;
}


/*! Constructs a HeaderField of type \a t. */

HeaderField::HeaderField( HeaderField::Type t )
    : d( new HeaderFieldData )
{
    d->type = t;
}


/*! Exists only to avoid compiler warnings. */

HeaderField::~HeaderField()
{
}


/*! Returns the type of this header field, as set by the constructor
    based on the name(). Unknown fields have type HeaderField::Other.
*/

HeaderField::Type HeaderField::type() const
{
    return d->type;
}


/*! Returns the canonical name of this header field. */

String HeaderField::name() const
{
    return d->name;
}


/*! Sets the name of this HeaderField to \a n. */

void HeaderField::setName( const String &n )
{
    d->name = n;
}


/*! Returns the canonical, folded (and, if required, RFC 2047-encoded)
    version of the contents of this HeaderField. This is the string we
    can use to form headers that are handed out to clients.
*/

String HeaderField::value()
{
    if ( d->hasData )
        reassemble( d->data );

    return d->data;
}


/*! Sets the value of this HeaderField to \a s. */

void HeaderField::setValue( const String &s )
{
    d->hasData = false;
    d->data = s;
}


/*! Returns the canonical, unfolded, UTF-8 encoded version of value().
    This is the value we store in the database.
*/

String HeaderField::data()
{
    if ( !d->hasData )
        parse( d->data );

    return d->data;
}


/*! Sets the data of this HeaderField to \a s. */

void HeaderField::setData( const String &s )
{
    d->hasData = true;
    d->data = s;
}


/*! Returns true if this header field is valid (or unparsed, as is the
    case for all unknown fields), and false if an error was detected
    during parsing.
*/

bool HeaderField::valid() const
{
    return d->error.isEmpty();
}


/*! Returns a suitable error message if this header field has a known
    parse error, and an empty string if the field is valid() or -- as
    is the case for all unknown fields -- not parsed.
*/

String HeaderField::error() const
{
    return d->error;
}


/*! Records the error text \a s encountered during parsing. */

void HeaderField::setError( const String &s )
{
    d->error = s;
}


/*! Every HeaderField subclass must define a parse() function that takes
    a string \a s from a message and sets the field data(). This default
    function handles fields that are not specially handled by subclasses
    using functions like parseText().
*/

void HeaderField::parse( const String &s )
{
    // Most fields share the same external and database representations.
    // For any that don't (cf. 2047) , we'll just setData() again later.
    setData( s );

    switch ( d->type ) {
    case From:
    case ResentFrom:
    case Sender:
    case ReturnPath:
    case ResentSender:
    case To:
    case Cc:
    case Bcc:
    case ReplyTo:
    case ResentTo:
    case ResentCc:
    case ResentBcc:
    case MessageId:
    case ContentId:
    case ResentMessageId:
    case References:
    case Date:
    case OrigDate:
    case ResentDate:
    case ContentType:
    case ContentTransferEncoding:
    case ContentDisposition:
    case ContentLanguage:
        // These should be handled by their own parse().
        break;

    case Subject:
    case Comments:
        parseText( s );
        break;

    case MimeVersion:
        parseMimeVersion( s );
        break;

    case ContentLocation:
        parseContentLocation( s );
        break;

    case InReplyTo:
    case Keywords:
    case Received:
    case ContentMd5:
    case ContentDescription:
        // no action necessary
        break;

    case Other:
        // no action possible
        break;
    }
}


/*! Like parse(), this function must be reimplemented by subclasses. Its
    responsibility is to use \a s (as retrieved from the database) to
    set the field's value().
*/

void HeaderField::reassemble( const String &s )
{
    switch ( d->type ) {
    default:
        // We assume that most fields share an external and database
        // representation.
        parse( s );
        setValue( data() );
        break;

    case Subject:
    case Comments:
        setValue( wrap( encode( s ) ) );
        break;
    }
}


/*! Parses the *text production, as modified to include encoded-words by
    RFC 2047. This is used to parse the Subject and Comments fields.
*/

void HeaderField::parseText( const String &s )
{
    Parser822 p( unwrap( s ) );
    setData( p.text() );
}


/*! Parses the Mime-Version syntax and records the first problem
    found.

    Only version 1.0 is accepted. Since some message generators
    incorrectly send comments, this parser accepts them.
*/

void HeaderField::parseMimeVersion( const String &s )
{
    Parser822 p( s );
    p.comment();
    String v = p.dotAtom();
    p.comment();
    if ( v != "1.0" || !p.atEnd() )
        setError( "Could not parse '" + v.simplified() + "'" );
}


/*! Parses the Content-Location header field and records the first
    problem found.
*/

void HeaderField::parseContentLocation( const String &s )
{
    Parser822 p( s );
    String t;
    char c;

    // We pretend a URI is just something without spaces in it.
    // Why the HELL couldn't this have been quoted?
    p.comment();
    while ( ( c = p.character() ) != '\0' && c != ' ' && c != '\t' )
        t.append( c );
    p.comment();

    if ( !p.atEnd() )
        setError( "Junk at end of '" + value().simplified() + "'" );
}


/*! Returns the name corresponding to the field type \a t, or 0 if there
    is no such field.
*/

const char *HeaderField::fieldName( HeaderField::Type t )
{
    uint i = 0;
    while ( fieldNames[i].name && fieldNames[i].type != t )
        i++;
    return fieldNames[i].name;
}


/*! This static function returns the RFC 2047-encoded version of \a s,
    which is assumed to be a UTF-8 encoded string.

    XXX: This is still really quite suboptimal.
*/

String HeaderField::encode( const String &s )
{
    String t;

    uint n = 0;
    Utf8Codec u;
    uint last = 0;

    do {
        String w;
        n = s.find( ' ', last );
        if ( n > 0 ) {
            w = s.mid( last, n-last );
            n++;
        }
        else {
            w = s.mid( last );
        }
        last = n;

        UString us = u.toUnicode( w );
        Codec *c = Codec::byString( us );
        String cw = c->fromUnicode( us );

        String ew;
        if ( c->name().lower() != "us-ascii" ) {
            ew = "=?" + c->name() + "?";
            String qp = cw.eQP( true );
            String b64 = cw.e64();
            if ( qp.length() < b64.length() ) {
                ew.append( "q?" );
                ew.append( qp );
            }
            else {
                ew.append( "b?" );
                ew.append( b64 );
            }
            ew.append( "?=" );
        }
        else {
            ew = cw;
        }

        t.append( ew );
        if ( last > 0 )
            t.append( " " );
    }
    while ( last > 0 );

    return t;
}


/*! Returns an unwrapped version of the string \a s, where any CRLF-SP
    is replaced by a single space.

    XXX: We use this function to unwrap only Subject and Comments fields
    at the moment, since they're the only ones we transform. Unwrapping
    should eventually be handled in the higher-level parser instead. We
    must assume here that every [CR]LF is actually followed by an SP.
*/

String HeaderField::unwrap( const String &s )
{
    String t;

    uint last = 0;
    uint n = 0;
    while ( n < s.length() ) {
        if ( s[n] == '\012' ||
             ( s[n] == '\015' && s[n+1] == '\012' ) )
        {
            t.append( s.mid( last, n-last ) );
            if ( s[n] == '\015' )
                n++;
            if ( s[n+1] == ' ' || s[n+1] == '\t' ) {
                t.append( " " );
                n++;
            }
            last = n+1;
        }
        n++;
    }
    t.append( s.mid( last ) );

    return t;
}


/*! Returns a version of \a s with long lines wrapped according to the
    rules in RFC [2]822. This function is not static, because it needs
    to look at the field name.

    XXX: Well, except that we ignore the rules right now.
*/

String HeaderField::wrap( const String &s )
{
    String t;

    uint n = 0;
    uint last = 0;
    bool first = true;
    uint l = d->name.length() + 2;

    // We'll consider every space a potential wrapping point, and just
    // try to fit as many tokens onto each line as possible. This is a
    // cheap hack.

    do {
        String w;
        n = s.find( ' ', last );
        if ( n > 0 ) {
            w = s.mid( last, n-last );
            n++;
        }
        else {
            w = s.mid( last );
        }
        last = n;

        if ( first ) {
            first = false;
        }
        else if ( l + 1 + w.length() > 78 ) {
            t.append( "\015\012 " );
            l = 1;
        }
        else {
            t.append( " " );
            l += 1;
        }

        l += w.length();
        t.append( w );
    }
    while ( last > 0 );

    return t;
}
