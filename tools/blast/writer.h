/* writer.h  -  Network blast tool  -  Public Domain  -  2015 Mattias Jansson / Rampant Pixels
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

typedef struct blast_writer_t blast_writer_t;

typedef void (*write_prepare_fn)(blast_writer_t* writer, uint64_t offset, int size);
typedef void* (*write_map_fn)(blast_writer_t* writer, uint64_t offset, int size);
typedef void (*write_unmap_fn)(blast_writer_t* writer, void* buffer, uint64_t offset, int size);

struct blast_writer_t {
	string_t         name;
	void*            data;
	uint64_t         size;
	write_prepare_fn prepare;
	write_map_fn     map;
	write_unmap_fn   unmap;
};

extern blast_writer_t*
blast_writer_open(const char* name, size_t namesize, uint64_t datasize);

extern void
blast_writer_close(blast_writer_t* writer);
