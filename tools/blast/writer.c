/* writer.c  -  Network blast tool  -  Public Domain  -  2015 Mattias Jansson
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/mjansson/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include "blast.h"
#include "writer.h"

static uint8_t writer_chunk[PACKET_CHUNK_SIZE];

static void
blast_writer_prepare(blast_writer_t* writer, uint64_t offset, int size) {
	FOUNDATION_UNUSED(writer);
	FOUNDATION_UNUSED(offset);
	FOUNDATION_UNUSED(size);
}

static void*
blast_writer_map(blast_writer_t* writer, uint64_t offset, int size) {
	FOUNDATION_UNUSED(writer);
	FOUNDATION_UNUSED(offset);
	FOUNDATION_UNUSED(size);
	return writer_chunk;
}

static void
blast_writer_unmap(blast_writer_t* writer, void* buffer, uint64_t offset, int size) {
	FOUNDATION_UNUSED(writer);
	FOUNDATION_UNUSED(buffer);
	FOUNDATION_UNUSED(offset);
	FOUNDATION_UNUSED(size);
}

blast_writer_t*
blast_writer_open(const char* name, size_t namesize, uint64_t datasize) {
	blast_writer_t* writer =
	    memory_allocate(HASH_BLAST, sizeof(blast_writer_t), 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
	writer->name = string_clone(name, namesize);
	writer->size = datasize;
	writer->prepare = blast_writer_prepare;
	writer->map = blast_writer_map;
	writer->unmap = blast_writer_unmap;
	return writer;
}

void
blast_writer_close(blast_writer_t* writer) {
	string_deallocate(writer->name.str);
	memory_deallocate(writer);
}
