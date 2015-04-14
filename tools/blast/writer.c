/* writer.c  -  Network blast tool  -  Public Domain  -  2015 Mattias Jansson / Rampant Pixels
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/rampantpixels/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include "blast.h"
#include "writer.h"


blast_writer_t* blast_writer_open( const char* name, uint64_t size )
{
    FOUNDATION_UNUSED( name );
    FOUNDATION_UNUSED( size );
    return 0;
}


void blast_writer_close( blast_writer_t* writer )
{
    FOUNDATION_UNUSED( writer );
}
