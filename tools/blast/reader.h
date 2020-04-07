/* reader.h  -  Network blast tool  -  Public Domain  -  2015 Mattias Jansson
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/mjansson/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#pragma once

typedef struct blast_reader_t blast_reader_t;

typedef void (*read_cache_fn)(blast_reader_t* reader, uint64_t offset, int size);
typedef void (*read_uncache_fn)(blast_reader_t* reader, uint64_t offset, int size);
typedef void* (*read_map_fn)(blast_reader_t* reader, uint64_t offset, int size);
typedef void (*read_unmap_fn)(blast_reader_t* reader, void* buffer, uint64_t offset, int size);

typedef struct blast_reader_t {
	string_t name;
	void* data;
	int id;
	uint64_t size;
	read_cache_fn cache;
	read_uncache_fn uncache;
	read_map_fn map;
	read_unmap_fn unmap;
} blast_reader_t;

extern blast_reader_t*
blast_reader_open(string_t source);

extern void
blast_reader_close(blast_reader_t* reader);
