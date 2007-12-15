// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef SORT_H
#define SORT_H

#include "search.h"


class Sort
    : public Search
{
public:
    Sort( bool );
    void parse();
    void execute();

private:
    class SortData * d;
};


#endif