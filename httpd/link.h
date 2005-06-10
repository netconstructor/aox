// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef LINK_H
#define LINK_H

#include "string.h"


class Link {
public:
    Link();
    Link( const String & );

    enum Type {
        ArchiveMailbox,
        WebmailMailbox,
        Webmail,
        ArchiveMessage,
        ArchivePart,
        WebmailMessage,
        WebmailPart,
        Favicon,
        Unknown
    };

    Type type() const;

    class Mailbox *mailbox() const;
    uint uid() const;
    String part() const;

    String string() const;

private:
    void parse( const String & );
    void parseMailbox( const String * );
    void parseUid( const String * );
    void parsePart( const String * );

private:
    class LinkData *d;
};


#endif
