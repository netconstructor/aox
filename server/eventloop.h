// Copyright 2009 The Archiveopteryx Developers <info@aox.org>

#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include "list.h"


class Connection;


class EventLoop
    : public Garbage
{
public:
    EventLoop();
    virtual ~EventLoop();

    virtual void start();
    virtual void stop( uint = 0 );
    virtual void addConnection( Connection * );
    virtual void removeConnection( Connection * );
    void closeAllExcept( Connection *, Connection * );
    void closeAllExceptListeners();
    void flushAll();

    void dispatch( Connection *, bool, bool, uint );

    bool inStartup() const;
    void setStartup( bool );

    bool inShutdown() const;

    List< Connection > *connections() const;

    static void setup( EventLoop * = 0 );
    static EventLoop * global();
    static void shutdown();
    static void freeMemorySoon();

    virtual void addTimer( class Timer * );
    virtual void removeTimer( class Timer * );

    void setConnectionCounts();

    void shutdownSSL();

    void setMemoryUsage( uint );
    uint memoryUsage() const;

private:
    class LoopData *d;
};


#endif
