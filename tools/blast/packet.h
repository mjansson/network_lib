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
    PACKET_CONTROL = 3,
    PACKET_TERMINATE = 7
} packet_type_t;

#define PACKET_TIMESTAMP_BITS 20ULL
#define PACKET_TIMESTAMP_MASK ( ( 1ULL << PACKET_TIMESTAMP_BITS ) - 1ULL )

#define PACKET_SEQ_BITS 32ULL
#define PACKET_SEQ_MASK ( ( 1ULL << PACKET_SEQ_BITS ) - 1ULL )

#define PACKET_TOKEN_BITS 9
#define PACKET_TOKEN_MASK ( ( 1ULL << PACKET_TOKEN_BITS ) - 1ULL )
//Chunk size * maximum sequence gives max transmission size
//1020 * ((1 << 32) - 1) = 4078 GiB ~= 4 TiB
#define PACKET_CHUNK_SIZE 1020ULL

#define PACKET_NAME_MAXSIZE 256
#define PACKET_DATA_MAXSIZE ( PACKET_CHUNK_SIZE * ( ( 1ULL << PACKET_SEQ_BITS ) - 1ULL ) )

#define PACKET_ACK_COUNT 32
#define PACKET_ACK_HISTORY 128

#define PACKET_DECLARE \
    uint64_t        type:3; \
    uint64_t        token:9; \
    uint64_t        timestamp:20; \
    uint64_t        seq:32

typedef struct packet_t
{
    PACKET_DECLARE;
} packet_t;

typedef struct packet_handshake_t
{
    PACKET_DECLARE;
    uint64_t datasize;
    uint32_t namesize;
    char     name[PACKET_NAME_MAXSIZE];
} packet_handshake_t;

typedef struct packet_payload_t
{
    PACKET_DECLARE;
    uint8_t  data[PACKET_CHUNK_SIZE];
} packet_payload_t;

typedef struct packet_ack_t
{
    PACKET_DECLARE;
    uint32_t ack[PACKET_ACK_COUNT];
} packet_ack_t;

