// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef PERMISSIONS_H
#define PERMISSIONS_H

#include "event.h"
#include "string.h"


class Permissions
    : public EventHandler
{
public:
    Permissions( class Mailbox *, class User *,
                 class EventHandler * );

    enum Right {
        Lookup, // l
        Read, // r
        KeepSeen, // s
        Write, // w
        Insert, // i
        Post, // p
        CreateMailboxes, // k
        DeleteMailbox, // x
        DeleteMessages, // t
        Expunge, // e
        Admin, // a
        WriteSharedAnnotation, // n
        // New rights go above this line.
        NumRights
    };

    bool ready();
    void execute();
    bool allowed( Right );

    String string() const;

    static char rightChar( Permissions::Right );

    static String all();

private:
    class PermissionData *d;
};


#endif
