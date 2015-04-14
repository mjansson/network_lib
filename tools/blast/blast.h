/* blast.h  -  Network blast tool  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/rampantpixels/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#pragma once

#include <foundation/foundation.h>
#include <network/network.h>

#include "errorcodes.h"
#include "hashstrings.h"

#include "packet.h"


extern bool blast_should_exit( void );
extern void blast_process_system_events( void );
