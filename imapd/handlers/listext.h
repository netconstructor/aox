// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef LISTEXT_H
#define LISTEXT_H

#include "command.h"

class Mailbox;


class Listext
    : public Command
{
public:
    Listext();

    void parse();
    void execute();

    String combinedName( Mailbox *, const String & );

    uint match( const String & pattern, uint p,
                const String & name, uint n );

    String listMailbox();

    Mailbox * top();

private:
    void addReturnOption( const String & );
    void addSelectOption( const String & );

    void list( Mailbox *, const String & );
    void listChildren( Mailbox *, const String & );
    void sendListResponse( Mailbox * );

    void reference();

    void listSubscribedMailbox( Mailbox * );

private:
    class ListextData * d;
};


#endif
