// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef STORE_H
#define STORE_H

class Flag;
class Query;
class Mailbox;

#include "command.h"


class Store
    : public Command
{
public:
    Store( bool u );
    Store( IMAP *, const IntegerSet &, bool, class Transaction * );

    void parse();
    void execute();

private:
    class StoreData * d;

private:
    bool processFlagNames();
    bool processAnnotationNames();
    bool removeFlags( bool opposite = false );
    bool addFlags();
    bool replaceFlags();
    void replaceAnnotations();
    void parseAnnotationEntry();
    String entryName();
};


#endif
