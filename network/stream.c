/* stream.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/mjansson/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/stream.h>
#include <network/socket.h>
#include <network/internal.h>

#include <foundation/foundation.h>

static stream_vtable_t socket_stream_vtable;

static size_t
socket_stream_available_nonblock_read(const socket_stream_t* stream) {
	return (stream->write_in - stream->read_in) + socket_available_read(stream->socket);
}

static void
socket_stream_doflush(socket_stream_t* stream) {
	socket_t* sock;
	size_t written;

	if (!stream->write_out)
		return;

	sock = stream->socket;
	if ((sock->fd == NETWORK_SOCKET_INVALID) || (sock->state != SOCKETSTATE_CONNECTED))
		return;

	written = socket_write(sock, stream->buffer_out, stream->write_out);
	if (written) {
		if (written < stream->write_out) {
			memmove(stream->buffer_out, stream->buffer_out + written, stream->write_out - written);
			stream->write_out -= written;
		} else {
			stream->write_out = 0;
		}
	}
}

static size_t
socket_stream_read(stream_t* stream, void* buffer, size_t size) {
	socket_stream_t* sockstream;
	socket_t* sock;

	size_t was_read = 0;
	size_t copy;
	bool try_again;
	size_t want_read;

	sockstream = (socket_stream_t*)stream;
	sock = sockstream->socket;

	if ((sock->fd == NETWORK_SOCKET_INVALID) ||
	    ((sock->state != SOCKETSTATE_CONNECTED) && (sock->state != SOCKETSTATE_DISCONNECTED)) || !size)
		goto exit;

	do {
		try_again = false;

		copy = (sockstream->write_in - sockstream->read_in);

		want_read = size - was_read;
		if (copy > want_read)
			copy = want_read;

		if (copy > 0) {
			if (buffer)
				memcpy(buffer, sockstream->buffer_in + sockstream->read_in, copy);

#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 0
			log_debugf(HASH_NETWORK,
			           STRING_CONST("Socket stream (0x%" PRIfixPTR " : %d) read %" PRIsize " of %" PRIsize
			                        " bytes from buffer position %" PRIsize),
			           (uintptr_t)sock, sock->fd, copy, want_read, sockstream->read_in);
#endif

			was_read += copy;
			sockstream->read_in += copy;
			if (sockstream->read_in == sockstream->write_in) {
				sockstream->read_in = 0;
				sockstream->write_in = 0;
			}
		}

		if (was_read < size) {
			FOUNDATION_ASSERT(sockstream->read_in == 0);
			FOUNDATION_ASSERT(sockstream->write_in == 0);
			sockstream->read_in = 0;
			sockstream->write_in = socket_read(sock, sockstream->buffer_in, sockstream->buffer_in_size);
			if (sockstream->write_in > 0)
				try_again = true;
		}
	} while ((was_read < size) && try_again);

	if (was_read < size) {
		if (was_read)
			log_warnf(
			    HASH_NETWORK, WARNING_SUSPICIOUS,
			    STRING_CONST("Socket stream (0x%" PRIfixPTR " : %d): partial read %" PRIsize " of %" PRIsize " bytes"),
			    (uintptr_t)sock, sock->fd, was_read, size);
		socket_poll_state(sock);
	}

exit:

	return was_read;
}

static size_t
socket_stream_write(stream_t* stream, const void* buffer, size_t size) {
	socket_stream_t* sockstream;
	socket_t* sock;
	size_t was_written = 0;
	size_t remain;

	sockstream = (socket_stream_t*)stream;
	sock = sockstream->socket;

	if ((sock->fd == NETWORK_SOCKET_INVALID) || (sock->state != SOCKETSTATE_CONNECTED) || !size || !buffer)
		goto exit;

	remain = sockstream->buffer_out_size - sockstream->write_out;

	do {
		if (size <= remain) {
			memcpy(sockstream->buffer_out + sockstream->write_out, buffer, (size_t)size);

			sockstream->write_out += (unsigned int)size;
			was_written += size;
			size = 0;

			break;
		}

		if (remain) {
			memcpy(sockstream->buffer_out + sockstream->write_out, buffer, remain);
			buffer = pointer_offset_const(buffer, remain);

			size -= remain;
			was_written += remain;
			sockstream->write_out += remain;
		}

		socket_stream_doflush(sockstream);

		if (sock->state != SOCKETSTATE_CONNECTED) {
			log_warnf(
			    HASH_NETWORK, WARNING_SUSPICIOUS,
			    STRING_CONST("Socket stream (0x%" PRIfixPTR " : %d): partial write %" PRIsize " of %" PRIsize " bytes"),
			    (uintptr_t)sock, sock->fd, was_written, size);
			break;
		}

		remain = sockstream->buffer_out_size - sockstream->write_out;

	} while (remain);

exit:

	return was_written;
}

static bool
socket_stream_eos(stream_t* stream) {
	socket_stream_t* sockstream;
	socket_t* sock;
	bool eos = false;
	socket_state_t state;

	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);

	sockstream = (socket_stream_t*)stream;
	sock = sockstream->socket;
	if (sock->fd == NETWORK_SOCKET_INVALID)
		return true;

	state = socket_poll_state(sock);
	if (((state != SOCKETSTATE_CONNECTED) || (sock->fd == NETWORK_SOCKET_INVALID)) &&
	    !socket_stream_available_nonblock_read(sockstream))
		eos = true;

	return eos;
}

static size_t
socket_stream_available_read(stream_t* stream) {
	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);
	return socket_stream_available_nonblock_read((socket_stream_t*)stream);
}

static void
socket_stream_buffer_read(stream_t* stream) {
	socket_stream_t* sockstream;
	socket_t* sock;
	int available;

	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);

	sockstream = (socket_stream_t*)stream;
	sock = sockstream->socket;
	if ((sock->fd == NETWORK_SOCKET_INVALID) || (sock->state != SOCKETSTATE_CONNECTED) || sockstream->write_in)
		return;

	available = socket_available_fd(sock->fd);
	if (available > 0) {
		size_t was_read = socket_read(sock, sockstream->buffer_in, sockstream->buffer_in_size);
		if (was_read > 0)
			sockstream->write_in += was_read;
	}
}

static void
socket_stream_flush(stream_t* stream) {
	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);

	socket_stream_doflush((socket_stream_t*)stream);
}

static void
socket_stream_truncate(stream_t* stream, size_t size) {
	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);
	FOUNDATION_UNUSED(stream);
	FOUNDATION_UNUSED(size);
}

static size_t
socket_stream_size(stream_t* stream) {
	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);
	FOUNDATION_UNUSED(stream);
	return 0;
}

static void
socket_stream_seek(stream_t* stream, ssize_t offset, stream_seek_mode_t direction) {
	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);

	if ((direction != STREAM_SEEK_CURRENT) || (offset < 0)) {
		log_error(HASH_NETWORK, ERROR_UNSUPPORTED,
		          STRING_CONST("Invalid call, only forward seeking allowed on sockets"));
	} else {
		socket_stream_read(stream, 0, (size_t)offset);
	}
}

static size_t
socket_stream_tell(stream_t* stream) {
	socket_stream_t* sockstream;
	socket_t* sock;

	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);

	sockstream = (socket_stream_t*)stream;
	sock = sockstream->socket;

	return sock->bytes_read;
}

static tick_t
socket_stream_last_modified(const stream_t* stream) {
	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);
	return time_current();
}

stream_t*
socket_stream_allocate(socket_t* sock, size_t buffer_in, size_t buffer_out) {
	size_t size = sizeof(socket_stream_t) + buffer_in + buffer_out;

	socket_stream_t* sockstream = memory_allocate(HASH_NETWORK, size, 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
	sockstream->buffer_in = pointer_offset(sockstream, sizeof(socket_stream_t));
	sockstream->buffer_out = pointer_offset(sockstream->buffer_in, buffer_in);
	sockstream->buffer_in_size = buffer_in;
	sockstream->buffer_out_size = buffer_out;
	socket_stream_initialize(sockstream, sock);
	return (stream_t*)sockstream;
}

void
socket_stream_initialize(socket_stream_t* stream, socket_t* sock) {
	// Network streams are always little endian by default
	stream_initialize((stream_t*)stream, BYTEORDER_LITTLEENDIAN);

	stream->type = STREAMTYPE_SOCKET;
	stream->sequential = 1;
	stream->mode = STREAM_OUT | STREAM_IN | STREAM_BINARY;
	stream->vtable = &socket_stream_vtable;
	stream->socket = sock;

	if (sock->stream_initialize_fn)
		sock->stream_initialize_fn(sock, (stream_t*)stream);
}

static void
socket_stream_finalize_stream(stream_t* stream) {
	FOUNDATION_UNUSED(stream);
}

int
socket_streams_initialize(void) {
	socket_stream_vtable.read = socket_stream_read;
	socket_stream_vtable.write = socket_stream_write;
	socket_stream_vtable.eos = socket_stream_eos;
	socket_stream_vtable.flush = socket_stream_flush;
	socket_stream_vtable.truncate = socket_stream_truncate;
	socket_stream_vtable.size = socket_stream_size;
	socket_stream_vtable.seek = socket_stream_seek;
	socket_stream_vtable.tell = socket_stream_tell;
	socket_stream_vtable.lastmod = socket_stream_last_modified;
	socket_stream_vtable.buffer_read = socket_stream_buffer_read;
	socket_stream_vtable.available_read = socket_stream_available_read;
	socket_stream_vtable.finalize = socket_stream_finalize_stream;
	socket_stream_vtable.clone = 0;
	return 0;
}
