/* datagram.h  -  Network blast tool  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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


typedef enum packet_type_t
{
    PACKET_HANDSHAKE = 0,
    PACKET_TERMINATE
} packet_type_t;


#define PACKET_DECLARE \
    uint32_t        type:3; \
    uint32_t        size:13; \
    uint32_t        __unused_bits:16;


typedef struct packet_t
{
    PACKET_DECLARE;
} packet_t;

