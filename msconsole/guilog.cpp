// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "cstring.h"

#include "guilog.h"

#include "logpane.h"
#include "date.h"

// time()
#include <time.h>


/*! \class GuiLog guilog.h

    The GuiLog class redirects log lines to a suitable widget - which
    is generally not shown. Because of this, msconsole doesn't need to
    connect to the logd.
*/

GuiLog::GuiLog()
    : Logger()
{
    // nothing
}


class LogItem
    : public QListViewItem
{
public:
    LogItem( QListView * parent,
             QListViewItem * after,
             const String & id, Log::Facility f, Log::Severity s,
             const String & m );

    QString text( int ) const;

    QString key( int, bool ) const;

    QString transaction;
    Log::Facility facility;
    Log::Severity severity;
    QString message;
    uint time;
    uint number;
};

static uint uniq;


LogItem::LogItem( QListView * parent,
                  QListViewItem * after,
                  const String & id, Log::Facility f, Log::Severity s,
                  const String & m )
    : QListViewItem( parent, after ),
      transaction( QString::fromLatin1( id.data(), id.length() ) ),
      facility( f ), severity( s ),
      message( QString::fromLatin1( m.data(), m.length() ) ),
      time( ::time( 0 ) ), number( ++uniq )
{
}


QString LogItem::text( int col ) const
{
    QString r;
    switch( col ) {
    case 0:
        r = transaction;
        break;
    case 1:
        { // a new scope so the Date object doesn't cross a label
            Date date;
            date.setUnixTime( time );
            r = QString::fromLatin1( date.isoTime().cstr() );
        }
        break;
    case 2:
        r = QString::fromLatin1( Log::facility( facility ) );
        break;
    case 3:
        r = QString::fromLatin1( Log::severity( severity ) );
        break;
    case 4:
        r = message;
        break;
    default:
        break;
    }
    return r;
}

QString LogItem::key( int col, bool ) const
{
    QString r;
    switch( col ) {
    case 0:
        r = transaction;
        break;
    case 1:
        r.sprintf( "%08x %08x", time, number );
        break;
    case 2:
        r[0] = '0' + (uint)facility;
        break;
    case 3:
        r[0] = '0' + (uint)severity;
        break;
    case 4:
        r = message;
        break;
    default:
        break;
    }
    return r;
}


static QListViewItem * item;
static LogPane * logPane;
static uint counter;


void GuiLog::send( const String & id,
                   Log::Facility f, Log::Severity s,
                   const String & m )
{
    if ( !::logPane )
        return;

    ::item = new LogItem( ::logPane->listView(), item, id, f, s, m );
    ::counter++;
    if ( ::counter < 128 )
        return;
    ::counter = 0;
    uint n = ::logPane->maxLines();
    if ( (uint)::logPane->listView()->childCount() <= n )
        return;

    QListViewItem * i = ::logPane->listView()->firstChild();
    n = (uint)::logPane->listView()->childCount() - n;
    while ( n && i && i != ::item ) {
        QListViewItem * t = i;
        i = i->nextSibling();
        delete t;
        n--;
    }
}


void GuiLog::commit( const String &, Log::Severity )
{
}


/*! Records that GuiLog should store all its log messages using \a
    view. The initial value is 0, which means that log messages are
    discarded.

    Calling setLogPane does not move older log lines into \a view.
*/

void GuiLog::setLogPane( LogPane * view )
{
    ::logPane = view;
}


/*! Returns the a pointer to the list view currently used for
    output. The initial value is 0, meaning that output is discarded.
*/

LogPane * GuiLog::logPane()
{
    return ::logPane;
}
