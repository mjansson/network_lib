/* errorcodes.h  -  Network blast tool  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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


//Error codes returned by blast tool
#define BLAST_RESULT_OK                                   0

#define BLAST_ERROR_UNABLE_TO_CREATE_SOCKET              -1
#define BLAST_ERROR_UNABLE_TO_OPEN_FILE                  -2
#define BLAST_ERROR_UNABLE_TO_READ_FILE                  -3
