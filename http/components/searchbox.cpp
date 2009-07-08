// Copyright 2009 The Archiveopteryx Developers <info@aox.org>

#include "searchbox.h"

#include "link.h"
#include "webpage.h"


/*! \class SearchBox searchbox.h
    This class merely displays a search box on the page.
*/

/*! Creates a new SearchBox component. */

SearchBox::SearchBox()
    : PageComponent( "searchbox" )
{
}


void SearchBox::execute()
{
    Link * l = page()->link();
    UString query = l->argument( "query" );
    Link action;
    action.setType( l->type() );
    action.setMailbox( l->mailbox() );
    EString s( "<form action=\"" );
    s.append( action.canonical() );
    s.append( "\"><input type=text name=query value=\"" );
    if ( !query.isEmpty() )
        s.append( quoted( query ) );
    s.append( "\"><input type=submit value=Search>" );
    s.append( "</form>" );
    setContents( s );
}
