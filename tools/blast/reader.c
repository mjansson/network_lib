/* reader.c  -  Network blast tool  -  Public Domain  -  2015 Mattias Jansson / Rampant Pixels
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/rampantpixels/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */
#define _CRT_SECURE_NO_WARNINGS 1

#include "blast.h"
#include "reader.h"

#include <foundation/posix.h>

#include <io.h>
#include <fcntl.h>
#include <sys\types.h>
#include <sys\stat.h>
#include <stdio.h>

//#include <sys/mman.h>

static void
blast_reader_cache(blast_reader_t* reader, uint64_t offset, int size) {
    FOUNDATION_UNUSED(reader);
    FOUNDATION_UNUSED(offset);
    FOUNDATION_UNUSED(size);
}

static void
blast_reader_uncache(blast_reader_t* reader, uint64_t offset, int size) {
    FOUNDATION_UNUSED(reader);
    FOUNDATION_UNUSED(offset);
    FOUNDATION_UNUSED(size);
}

static void*
blast_reader_map(blast_reader_t* reader, uint64_t offset, int size) {
    FOUNDATION_UNUSED(size);
    return pointer_offset(reader->data, offset);
}

void
blast_reader_unmap(blast_reader_t* reader, void* buffer, uint64_t offset, int size) {
    FOUNDATION_UNUSED(reader);
    FOUNDATION_UNUSED(buffer);
    FOUNDATION_UNUSED(offset);
    FOUNDATION_UNUSED(size);
}

blast_reader_t*
blast_reader_open(string_t source) {
    void* addr;
    int64_t size;
    blast_reader_t* reader;
    int fd;

    fd = _open(source.str, O_RDONLY);
    if (fd == 0)
        return 0;

    size = _lseek(fd, 0, SEEK_END);
    if (size <= 0) {
        _close(fd);
        return 0;
    }
    _lseek(fd, 0, SEEK_SET);

    addr = memory_allocate(HASH_BLAST, (size_t)size, 0, MEMORY_PERSISTENT);
    _read(fd, addr, (int)size);
    _close(fd);
    /*addr = mmap( 0, size, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0 );
    if( addr == MAP_FAILED )
    {
        int err = system_error();
        log_warnf( HASH_BLAST, WARNING_SYSTEM_CALL_FAIL, "Unable to mmap file '%s' (%d) size %d: %s (%d)", source, fd, size, system_error_message( err ), err );
        close( fd );
        return 0;
    }

    log_infof( HASH_BLAST, "Mapped '%s' size %lld to memory region 0x%" PRIfixPTR, source, size, addr );*/

    reader = memory_allocate(HASH_BLAST, sizeof(blast_reader_t), 0,
                             MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);

    string_const_t filename = path_file_name(STRING_ARGS(source));
    reader->name = string_clone(STRING_ARGS(filename));
    reader->data = addr;
    //reader->id = fd;
    reader->size = (uint64_t)size;
    reader->cache = blast_reader_cache;
    reader->uncache = blast_reader_uncache;
    reader->map = blast_reader_map;
    reader->unmap = blast_reader_unmap;

    return reader;
}

void
blast_reader_close(blast_reader_t* reader) {
    //munmap(reader->data, reader->size);
    //close(reader->id);
    string_deallocate(reader->name.str);
    memory_deallocate(reader);
}
