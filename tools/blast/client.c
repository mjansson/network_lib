/* client.c  -  Network blast tool  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 * 
 * https://github.com/rampantpixels/network_lib
 * 
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <foundation/foundation.h>
#include <network/network.h>

#include "errorcodes.h"


extern hash_t HASH_BLAST;


int blast_client( network_address_t** target, char** files )
{
	return BLAST_RESULT_OK;
}
