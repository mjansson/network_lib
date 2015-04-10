/* packet.h  -  Network blast tool  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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
    PACKET_PAYLOAD = 1,
    PACKET_ACK = 2,
    PACKET_TERMINATE = 3
} packet_type_t;

#define PACKET_TIMESTAMP_BITS 20ULL
#define PACKET_TIMESTAMP_MASK ( ( 1ULL << PACKET_TIMESTAMP_BITS ) - 1ULL )

#define PACKET_SEQ_BITS 31ULL
#define PACKET_SEQ_MASK ( ( 1ULL << PACKET_SEQ_BITS ) - 1ULL )

#define PACKET_DECLARE \
    uint64_t        type:2; \
    uint64_t        size:11; \
    uint64_t        timestamp:20; \
    uint64_t        seq:31


typedef struct packet_t
{
    PACKET_DECLARE;
} packet_t;

