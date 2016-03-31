/* socket.c  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/rampantpixels/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/socket.h>
#include <network/address.h>
#include <network/internal.h>
#include <network/hashstrings.h>

#include <foundation/foundation.h>

socket_base_t*           _socket_base;
atomic32_t               _socket_base_next;
int32_t                  _socket_base_size;

static stream_vtable_t   _socket_stream_vtable;

static void
_socket_set_blocking_fd(int fd, bool block);

static socket_stream_t*
_socket_stream_allocate(socket_t* sock);

static void
_socket_stream_finalize(stream_t* stream);

static size_t
_socket_stream_available_nonblock_read(const socket_stream_t* stream);

static size_t
_socket_stream_buffered_in(const socket_stream_t* stream);

static void
_socket_stream_doflush(socket_stream_t* stream);

static bool
_socket_stream_eos(stream_t* stream);

static void
_socket_stream_buffer_read(stream_t* stream);

static void
_socket_stream_flush(stream_t* stream);

static void
_socket_stream_truncate(stream_t* stream, size_t size);

static size_t
_socket_stream_size(stream_t* stream);

static void
_socket_stream_seek(stream_t* stream, ssize_t offset, stream_seek_mode_t direction);

static size_t
_socket_stream_tell(stream_t* stream);

static tick_t
_socket_stream_last_modified(const stream_t* stream);

void
_socket_initialize(socket_t* sock) {
	sock->base = -1;
}

int
_socket_allocate_base(socket_t* sock) {
	if (sock->base >= 0)
		return sock->base;

	//TODO: Better allocation scheme
	do {
		int base = atomic_exchange_and_add32(&_socket_base_next, 1);
		if (base > _socket_base_size) {
			atomic_store32(&_socket_base_next, 0);
			continue;
		}
		socket_base_t* sockbase = _socket_base + base;
		if (atomic_cas_ptr(&sockbase->sock, sock, nullptr)) {
			sock->base = base;
			sockbase->fd = SOCKET_INVALID;
			sockbase->flags = 0;
			sockbase->state = SOCKETSTATE_NOTCONNECTED;
			return base;
		}
	}
	while (true);

	return -1;
}

int
_socket_create_fd(socket_t* sock, network_address_family_t family) {
	socket_base_t* sockbase;

	if (_socket_allocate_base(sock) < 0) {
		log_errorf(HASH_NETWORK, ERROR_OUT_OF_MEMORY,
		           STRING_CONST("Unable to allocate base for socket 0x%" PRIfixPTR), sock);
		return SOCKET_INVALID;
	}

	sockbase = _socket_base + sock->base;

	if (sockbase->fd != SOCKET_INVALID) {
		if (sock->family != family) {
			FOUNDATION_ASSERT_FAILFORMAT_LOG(HASH_NETWORK,
			                                 "Trying to switch family on existing socket (0x%" PRIfixPTR " : %d) from %u to %u",
			                                 sock, sockbase->fd, sock->family, family);
			return SOCKET_INVALID;
		}
	}

	if (sockbase->fd == SOCKET_INVALID) {
		sock->open_fn(sock, family);
		if (sockbase->fd != SOCKET_INVALID) {
			sock->family = family;
			socket_set_blocking(sock, sockbase->flags & SOCKETFLAG_BLOCKING);
			socket_set_reuse_address(sock, sockbase->flags & SOCKETFLAG_REUSE_ADDR);
			socket_set_reuse_port(sock, sockbase->flags & SOCKETFLAG_REUSE_PORT);
		}
	}

	return sockbase->fd;
}

void
socket_deallocate(socket_t* sock) {
	if (!sock)
		return;

#if BUILD_ENABLE_DEBUG_LOG
	{
		int fd = SOCKET_INVALID;
		if (sock->base >= 0)
			fd = _socket_base[ sock->base ].fd;
		log_debugf(HASH_NETWORK, STRING_CONST("Deallocating socket (0x%" PRIfixPTR " : %d)"),
		           sock, fd);
	}
#endif

	socket_close(sock);

	if (!FOUNDATION_VALIDATE_MSG(!sock->stream, "Socket deallocated while still holding stream"))
		stream_deallocate((stream_t*)sock->stream);

	if (sock->base >= 0) {
		socket_base_t* sockbase = _socket_base + sock->base;
		atomic_store_ptr(&sockbase->sock, nullptr);
		sock->base = -1;
	}

	memory_deallocate(sock);
}

bool
socket_bind(socket_t* sock, const network_address_t* address) {
	bool success = false;
	socket_base_t* sockbase;
	const network_address_ip_t* address_ip;

	FOUNDATION_ASSERT(address);

	if (_socket_create_fd(sock, address->family) == SOCKET_INVALID)
		return false;

	sockbase = _socket_base + sock->base;
	address_ip = (const network_address_ip_t*)address;
	if (bind(sockbase->fd, &address_ip->saddr, address_ip->address_size) == 0) {
		//Store local address
		_socket_store_address_local(sock, address_ip->family);
		success = true;
#if BUILD_ENABLE_LOG
		{
			char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
			string_t address_str = network_address_to_string(buffer, sizeof(buffer), sock->address_local, true);
			log_infof(HASH_NETWORK, STRING_CONST("Bound socket (0x%" PRIfixPTR " : %d) to local address %.*s"),
			          sock, sockbase->fd, STRING_FORMAT(address_str));
		}
#endif
	}
	else {
#if BUILD_ENABLE_LOG
		char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		int sockerr = NETWORK_SOCKET_ERROR;
		string_const_t errmsg = system_error_message(sockerr);
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), address, true);
		log_warnf(HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
		          STRING_CONST("Unable to bind socket (0x%" PRIfixPTR " : %d) to local address %.*s: %.*s (%d)"),
		          sock, sockbase->fd, STRING_FORMAT(address_str), STRING_FORMAT(errmsg), sockerr);
#endif
	}

	return success;
}

static int
_socket_connect(socket_t* sock, const network_address_t* address, unsigned int timeoutms) {
	socket_base_t* sockbase;
	const network_address_ip_t* address_ip;
	bool blocking;
	bool failed = true;
	int err = 0;
#if BUILD_ENABLE_DEBUG_LOG
	string_const_t error_message = string_null();
#endif

	if (sock->base < 0)
		return 0;

	sockbase = _socket_base + sock->base;
	blocking = ((sockbase->flags & SOCKETFLAG_BLOCKING) != 0);

	if ((timeoutms > 0) && blocking)
		socket_set_blocking(sock, false);

	address_ip = (const network_address_ip_t*)address;
	err = connect(sockbase->fd, &address_ip->saddr, address_ip->address_size);
	if (!err) {
		failed = false;
		sockbase->state = SOCKETSTATE_CONNECTED;
	}
	else {
		bool in_progress = false;
		err = NETWORK_SOCKET_ERROR;
#if FOUNDATION_PLATFORM_WINDOWS
		in_progress = (err == WSAEWOULDBLOCK);
#else //elif FOUDATION_PLATFORM_POSIX
		in_progress = (err == EINPROGRESS);
#endif
		if (in_progress) {
			if (!timeoutms) {
				failed = false;
				sockbase->state = SOCKETSTATE_CONNECTING;
			}
			else {
				struct timeval tv;
				fd_set fdwrite, fderr;
				int ret;

				FD_ZERO(&fdwrite);
				FD_ZERO(&fderr);
				FD_SET(sockbase->fd, &fdwrite);
				FD_SET(sockbase->fd, &fderr);

				tv.tv_sec  = timeoutms / 1000;
				tv.tv_usec = (timeoutms % 1000) * 1000;

				ret = select((int)(sockbase->fd + 1), 0, &fdwrite, &fderr, &tv);
				if (ret > 0) {
#if FOUNDATION_PLATFORM_WINDOWS
					int serr = 0;
					int slen = sizeof(int);
					getsockopt(sockbase->fd, SOL_SOCKET, SO_ERROR, (char*)&serr, &slen);
#else
					int serr = 0;
					socklen_t slen = sizeof(int);
					getsockopt(sockbase->fd, SOL_SOCKET, SO_ERROR, (void*)&serr, &slen);
#endif
					if (!serr) {
						failed = false;
						sockbase->state = SOCKETSTATE_CONNECTED;
					}
					else {
						err = serr;
#if BUILD_ENABLE_DEBUG_LOG
						error_message = string_const(STRING_CONST("select indicated socket error"));
#endif
					}
				}
				else if (ret < 0) {
					err = NETWORK_SOCKET_ERROR;
#if BUILD_ENABLE_DEBUG_LOG
					error_message = string_const(STRING_CONST("select failed"));
#endif
				}
				else {
#if FOUNDATION_PLATFORM_WINDOWS
					err = WSAETIMEDOUT;
#else
					err = ETIMEDOUT;
#endif
#if BUILD_ENABLE_DEBUG_LOG
					error_message = string_const(STRING_CONST("select timed out"));
#endif
				}
			}
		}
#if BUILD_ENABLE_DEBUG_LOG
		else {
			error_message = string_const(STRING_CONST("connect error"));
		}
#endif
	}

	if ((timeoutms > 0) && blocking)
		socket_set_blocking(sock, true);

	if (failed) {
#if BUILD_ENABLE_DEBUG_LOG
		char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), address, true);
		log_debugf(HASH_NETWORK,
		           STRING_CONST("Failed to connect TCP/IP socket (0x%" PRIfixPTR " : %d) to remote host %.*s: %.*s"),
		           sock, sockbase->fd, STRING_FORMAT(address_str), STRING_FORMAT(error_message));
#endif
		return err;
	}

	memory_deallocate(sock->address_remote);
	sock->address_remote = network_address_clone(address);

	if (!sock->address_local)
		_socket_store_address_local(sock, address_ip->family);

#if BUILD_ENABLE_DEBUG_LOG
	{
		char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), address, true);
		log_debugf(HASH_NETWORK,
		           STRING_CONST("%s socket (0x%" PRIfixPTR " : %d) to remote host %.*s"),
		           (sockbase->state == SOCKETSTATE_CONNECTING) ? "Connecting" : "Connected",
		           sock, sockbase->fd, STRING_FORMAT(address_str));
	}
#endif

	return 0;
}

bool
socket_connect(socket_t* sock, const network_address_t* address, unsigned int timeout) {
	int err;
	socket_base_t* sockbase;

	FOUNDATION_ASSERT(address);

	if (_socket_create_fd(sock, address->family) == SOCKET_INVALID)
		return false;

	sockbase = _socket_base + sock->base;
	if (sockbase->state != SOCKETSTATE_NOTCONNECTED) {
#if BUILD_ENABLE_LOG
		char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), address, true);
		log_warnf(HASH_NETWORK, WARNING_SUSPICIOUS,
		          STRING_CONST("Unable to connect already connected socket (0x%" PRIfixPTR
		                       " : %d) to remote address %.*s"), sock, sockbase->fd, STRING_FORMAT(address_str));
#endif
		return false;
	}

	err = _socket_connect(sock, address, timeout);
	if (err) {
#if BUILD_ENABLE_LOG
		char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_const_t errmsg = system_error_message(err);
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), address, true);
		log_warnf(HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
		          STRING_CONST("Unable to connect socket (0x%" PRIfixPTR
		                       " : %d) to remote address %.*s: %.*s (%d)"), sock, sockbase->fd, STRING_FORMAT(address_str),
		          STRING_FORMAT(errmsg), err);
#endif
		return false;
	}

	memory_deallocate(sock->address_remote);
	sock->address_remote = network_address_clone(address);

	return true;
}

bool
socket_blocking(const socket_t* sock) {
	if (sock->base >= 0) {
		socket_base_t* sockbase = _socket_base + sock->base;
		return ((sockbase->flags & SOCKETFLAG_BLOCKING) != 0);
	}
	return false;
}

void
socket_set_blocking(socket_t* sock, bool block) {
	if (_socket_allocate_base(sock) < 0)
		return;

	socket_base_t* sockbase = _socket_base + sock->base;
	sockbase->flags = (block ? sockbase->flags | SOCKETFLAG_BLOCKING : sockbase->flags &
	                   ~SOCKETFLAG_BLOCKING);
	if (sockbase->fd != SOCKET_INVALID)
		_socket_set_blocking_fd(sockbase->fd, block);
}

bool
socket_reuse_address(const socket_t* sock) {
	bool reuse = false;
	if (sock->base >= 0) {
		socket_base_t* sockbase = _socket_base + sock->base;
		reuse = ((sockbase->flags & SOCKETFLAG_REUSE_ADDR) != 0);
	}
	return reuse;
}

void
socket_set_reuse_address(socket_t* sock, bool reuse) {
	socket_base_t* sockbase;
	int fd;

	if (_socket_allocate_base(sock) < 0)
		return;

	sockbase = _socket_base + sock->base;
	sockbase->flags = (reuse ? sockbase->flags | SOCKETFLAG_REUSE_ADDR : sockbase->flags &
	                   ~SOCKETFLAG_REUSE_ADDR);
	fd = sockbase->fd;
	if (fd != SOCKET_INVALID) {
#if FOUNDATION_PLATFORM_WINDOWS
		BOOL optval = reuse ? 1 : 0;
		int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));
#else
		int optval = reuse ? 1 : 0;
		int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#endif
		if (ret < 0) {
			const int sockerr = NETWORK_SOCKET_ERROR;
			const string_const_t errmsg = system_error_message(sockerr);
			log_warnf(HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
			          STRING_CONST("Unable to set reuse address option on socket (0x%" PRIfixPTR
			                       " : %d): %.*s (%d)"),
			          sock, sockbase->fd, STRING_FORMAT(errmsg), sockerr);
			FOUNDATION_UNUSED(sockerr);
		}
	}
}

bool
socket_reuse_port(const socket_t* sock) {
	bool reuse = false;
	if (sock->base >= 0) {
		socket_base_t* sockbase = _socket_base + sock->base;
		reuse = ((sockbase->flags & SOCKETFLAG_REUSE_PORT) != 0);
	}
	return reuse;
}

void
socket_set_reuse_port(socket_t* sock, bool reuse) {
	socket_base_t* sockbase;
	int fd;

#if FOUNDATION_PLATFORM_WINDOWS
	FOUNDATION_UNUSED(fd);
#endif

	if (_socket_allocate_base(sock) < 0)
		return;

	sockbase = _socket_base + sock->base;
	sockbase->flags = (reuse ? sockbase->flags | SOCKETFLAG_REUSE_PORT : sockbase->flags &
	                   ~SOCKETFLAG_REUSE_PORT);
#ifdef SO_REUSEPORT
	fd = sockbase->fd;
	if (fd != SOCKET_INVALID) {
#if !FOUNDATION_PLATFORM_WINDOWS
		int optval = reuse ? 1 : 0;
		int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
		if (ret < 0) {
			const int sockerr = NETWORK_SOCKET_ERROR;
			const string_const_t errmsg = system_error_message(sockerr);
			log_warnf(HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
			          STRING_CONST("Unable to set reuse port option on socket (0x%" PRIfixPTR " : %d): %.*s (%d)"),
			          sock, sockbase->fd, STRING_FORMAT(errmsg), sockerr);
			FOUNDATION_UNUSED(sockerr);
		}
#endif
	}
#endif
}

bool
socket_set_multicast_group(socket_t* sock, network_address_t* address, bool allow_loopback) {
	socket_base_t* sockbase;
	int fd;
	unsigned char ttl = 1;
	unsigned char loopback = (allow_loopback ? 1 : 0);
	struct ip_mreq req;

	if (_socket_allocate_base(sock) < 0)
		return false;

	sockbase = _socket_base + sock->base;
	fd = sockbase->fd;
	if (fd == SOCKET_INVALID)
		return false;

	//TODO: TTL 1 means local network, should be split out to separate control function
	setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
	setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loopback, sizeof(loopback));

	memset(&req, 0, sizeof(req));
	req.imr_multiaddr.s_addr = ((network_address_ipv4_t*)address)->saddr.sin_addr.s_addr;
	req.imr_interface.s_addr = INADDR_ANY;
	if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&req, sizeof(req)) != 0) {
		const int sockerr = NETWORK_SOCKET_ERROR;
		const string_const_t errmsg = system_error_message(sockerr);
		log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
		           STRING_CONST("Add multicast group failed on socket (0x%" PRIfixPTR " : %d): %.*s (%d)"),
		           sock, fd, STRING_FORMAT(errmsg), sockerr);
		FOUNDATION_UNUSED(sockerr);
		return false;
	}

	return true;
}

const network_address_t*
socket_address_local(const socket_t* sock) {
	return sock->address_local;
}

const network_address_t*
socket_address_remote(const socket_t* sock) {
	return sock->address_remote;
}

socket_state_t
socket_state(const socket_t* sock) {
	socket_state_t state = SOCKETSTATE_NOTCONNECTED;
	if (sock->base >= 0)
		state = _socket_poll_state(_socket_base + sock->base);
	return state;
}

size_t
socket_available_read(const socket_t* sock) {
	if (sock->base >= 0)
		return _socket_available_fd(_socket_base[ sock->base ].fd);
	return 0;
}

size_t
socket_read(socket_t* sock, void* buffer, size_t size) {
	socket_base_t* sockbase;
	size_t read;
	long ret;

	if (sock->base < 0)
		return 0;

	sockbase = _socket_base + sock->base;
	ret = recv(sockbase->fd, (char*)buffer, (int)size, 0);
	if (ret > 0) {
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
		const unsigned char* src = (const unsigned char*)buffer;
		char dump_buffer[66];
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 0
		log_debugf(HASH_NETWORK,
		           STRING_CONST("Socket (0x%" PRIfixPTR " : %d) read %d of %" PRIsize " bytes"),
		           sock, sockbase->fd, ret, size);
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
		for (long row = 0; row <= (ret / 8); ++row) {
			long ofs = 0, col = 0, cols = 8;
			if ((row + 1) * 8 > ret)
				cols = ret - (row * 8);
			for (; col < cols; ++col, ofs += 3) {
				string_format(dump_buffer + ofs, 66 - (unsigned int)ofs, STRING_CONST("%02x"),
				              *(src + (row * 8) + col));
				*(dump_buffer + ofs + 2) = ' ';
			}
			if (ofs) {
				*(dump_buffer + ofs - 1) = 0;
				log_debug(HASH_NETWORK, dump_buffer, ofs - 1);
			}
		}
#endif

		read = (size_t)ret;
		sock->bytes_read += read;

		return read;
	}

	if (ret == 0) {
#if BUILD_ENABLE_DEBUG_LOG
		char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), sock->address_remote,
		                                                 true);
		log_debugf(HASH_NETWORK,
		           STRING_CONST("Socket closed gracefully on remote end (0x%" PRIfixPTR " : %d): %.*s"),
		           sock, sockbase->fd, STRING_FORMAT(address_str));
#endif
		socket_close(sock);
	}
	else {
		int sockerr = NETWORK_SOCKET_ERROR;
#if FOUNDATION_PLATFORM_WINDOWS
		if (sockerr != WSAEWOULDBLOCK)
#else
		if (sockerr != EAGAIN)
#endif
		{
			string_const_t errmsg = system_error_message(sockerr);
			log_warnf(HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
			          STRING_CONST("Socket recv() failed on socket (0x%" PRIfixPTR " : %d): %.*s (%d)"),
			          sock, sockbase->fd, STRING_FORMAT(errmsg), sockerr);
		}

#if FOUNDATION_PLATFORM_WINDOWS
		if ((sockerr == WSAENETDOWN) || (sockerr == WSAENETRESET) || (sockerr == WSAENOTCONN) ||
		        (sockerr == WSAECONNABORTED) || (sockerr == WSAECONNRESET) || (sockerr == WSAETIMEDOUT))
#else
		if ((sockerr == ECONNRESET) || (sockerr == EPIPE) || (sockerr == ETIMEDOUT))
#endif
		{
			socket_close(sock);
		}

		_socket_poll_state(sockbase);
	}

	return 0;
}

size_t
socket_write(socket_t* sock, const void* buffer, size_t size) {
	socket_base_t* sockbase;
	size_t total_write = 0;

	if (sock->base < 0)
		return 0;

	sockbase = _socket_base + sock->base;
	while (total_write < size) {
		const char* current = (const char*)pointer_offset_const(buffer, total_write);
		int remain = (int)(size - total_write);

		long res = send(sockbase->fd, current, remain, 0);
		if (res > 0) {
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
			const unsigned char* src = (const unsigned char*)current;
			char buffer[34];
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 0
			log_debugf(HASH_NETWORK,
			           STRING_CONST("Socket (0x%" PRIfixPTR " : %d) wrote %d of %d bytes (offset %" PRIsize ")"),
			           sock, sockbase->fd, res, remain, total_write);
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
			for (long row = 0; row <= (res / 8); ++row) {
				long ofs = 0, col = 0, cols = 8;
				if ((row + 1) * 8 > res)
					cols = res - (row * 8);
				for (; col < cols; ++col, ofs += 3) {
					string_format(buffer + ofs, 34 - (unsigned int)ofs, STRING_CONST("%02x"), *(src + (row * 8) + col));
					*(buffer + ofs + 2) = ' ';
				}
				if (ofs) {
					*(buffer + ofs - 1) = 0;
					log_debug(HASH_NETWORK, buffer, ofs - 1);
				}
			}
#endif
			total_write += res;
		}
		else if (res <= 0) {
			int sockerr = NETWORK_SOCKET_ERROR;

#if FOUNDATION_PLATFORM_WINDOWS
			int serr = 0;
			int slen = sizeof(int);
			getsockopt(sockbase->fd, SOL_SOCKET, SO_ERROR, (char*)&serr, &slen);
#else
			int serr = 0;
			socklen_t slen = sizeof(int);
			getsockopt(sockbase->fd, SOL_SOCKET, SO_ERROR, (void*)&serr, &slen);
#endif

#if FOUNDATION_PLATFORM_WINDOWS
			if (sockerr == WSAEWOULDBLOCK)
#else
			if (sockerr == EAGAIN)
#endif
			{
				log_warnf(HASH_NETWORK, WARNING_SUSPICIOUS,
				          STRING_CONST("Partial socket send() on (0x%" PRIfixPTR
				                       " : %d): %" PRIsize" of %" PRIsize " bytes written to socket (SO_ERROR %d)"),
				          sock, sockbase->fd, total_write, size, serr);
			}
			else {
				log_warnf(HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
				          STRING_CONST("Socket send() failed on socket (0x%" PRIfixPTR " : %d): %s (%d) (SO_ERROR %d)"),
				          sock, sockbase->fd, system_error_message(sockerr), sockerr, serr);
			}

#if FOUNDATION_PLATFORM_WINDOWS
			if ((sockerr == WSAENETDOWN) || (sockerr == WSAENETRESET) || (sockerr == WSAENOTCONN) ||
			        (sockerr == WSAECONNABORTED) || (sockerr == WSAECONNRESET) || (sockerr == WSAETIMEDOUT))
#else
			if ((sockerr == ECONNRESET) || (sockerr == EPIPE) || (sockerr == ETIMEDOUT))
#endif
			{
				socket_close(sock);
			}

			if (sockbase->state != SOCKETSTATE_NOTCONNECTED)
				_socket_poll_state(sockbase);

			break;
		}
	}

	sock->bytes_written += total_write;

	return total_write;
}

stream_t*
socket_stream(socket_t* sock) {
	socket_stream_t* stream = 0;

	if (!sock->stream) {
		stream = _socket_stream_allocate(sock);
		sock->stream = stream;

		if (sock->stream_initialize_fn)
			sock->stream_initialize_fn(sock, (stream_t*)stream);
	}
	else {
		stream = sock->stream;
	}

	return (stream_t*)stream;
}

//Returns -1 if nothing available and socket closed, 0 if nothing available but still open, >0 if data available
int
_socket_available_fd(int fd) {
	bool closed = false;
	int available = 0;

	if (fd == SOCKET_INVALID)
		return -1;

#if FOUNDATION_PLATFORM_WINDOWS
	{
		u_long avail = 0;
		if (ioctlsocket(fd, FIONREAD, &avail) < 0)
			closed = true;
		available = (int)avail;
	}
#elif FOUNDATION_PLATFORM_POSIX
	if (ioctl(fd, FIONREAD, &available) < 0)
		closed = true;
#else
#  error Not implemented
#endif

	return (!available && closed) ? -1 : available;
}

void
socket_close(socket_t* sock) {
	int fd = SOCKET_INVALID;
	network_address_t* local_address = sock->address_local;
	network_address_t* remote_address = sock->address_remote;

	if (sock->base >= 0) {
		socket_base_t* sockbase = _socket_base + sock->base;

		fd = sockbase->fd;
		sockbase->fd    = SOCKET_INVALID;
		sockbase->state = SOCKETSTATE_NOTCONNECTED;
	}

	log_debugf(HASH_NETWORK, STRING_CONST("Closing socket (0x%" PRIfixPTR " : %d)"),
	           sock, fd);

	sock->address_local  = nullptr;
	sock->address_remote = nullptr;

	if (fd != SOCKET_INVALID) {
		_socket_set_blocking_fd(fd, false);
		_socket_close_fd(fd);
	}

	if (local_address)
		memory_deallocate(local_address);
	if (remote_address)
		memory_deallocate(remote_address);
}

void
_socket_close_fd(int fd) {
#if FOUNDATION_PLATFORM_WINDOWS
	shutdown(fd, SD_BOTH);
	closesocket(fd);
#elif FOUNDATION_PLATFORM_POSIX
	shutdown(fd, SHUT_RDWR);
	close(fd);
#else
#  error Not implemented
#endif
}

void
_socket_set_blocking_fd(int fd, bool block) {
#if FOUNDATION_PLATFORM_WINDOWS
	unsigned long param = block ? 0 : 1;
	ioctlsocket(fd, FIONBIO, &param);
#elif FOUNDATION_PLATFORM_POSIX
	const int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, block ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
#else
#  error Not implemented
#endif
}

socket_state_t
_socket_poll_state(socket_base_t* sockbase) {
	socket_t* sock = atomic_load_ptr(&sockbase->sock);
	struct timeval tv;
	fd_set fdwrite, fderr;
	int available;

	if ((sockbase->state == SOCKETSTATE_NOTCONNECTED) || (sockbase->state == SOCKETSTATE_DISCONNECTED))
		return sockbase->state;

	switch (sockbase->state) {
	case SOCKETSTATE_CONNECTING:
		FD_ZERO(&fdwrite);
		FD_ZERO(&fderr);
		FD_SET(sockbase->fd, &fdwrite);
		FD_SET(sockbase->fd, &fderr);

		tv.tv_sec  = 0;
		tv.tv_usec = 0;

		select((int)(sockbase->fd + 1), 0, &fdwrite, &fderr, &tv);

		if (FD_ISSET(sockbase->fd, &fderr)) {
			log_debugf(HASH_NETWORK,
			           STRING_CONST("Socket (0x%" PRIfixPTR " : %d): error in state CONNECTING"),
			           sock, sockbase->fd);
			socket_close(sock);
		}
		else if (FD_ISSET(sockbase->fd, &fdwrite)) {
#if BUILD_ENABLE_DEBUG_LOG
			log_debugf(HASH_NETWORK,
			           STRING_CONST("Socket (0x%" PRIfixPTR " : %d): CONNECTING -> CONNECTED"),
			           sock, sockbase->fd);
#endif
			sockbase->state = SOCKETSTATE_CONNECTED;
		}

		break;

	case SOCKETSTATE_CONNECTED:
		available = _socket_available_fd(sockbase->fd);
		if (available < 0) {
#if BUILD_ENABLE_DEBUG_LOG
			log_debugf(HASH_NETWORK,
			           STRING_CONST("Socket (0x%" PRIfixPTR " : %d): hangup in CONNECTED"),
			           sock, sockbase->fd);
#endif
			sockbase->state = SOCKETSTATE_DISCONNECTED;
			//Fall through to disconnected check for close
		}
		else
			break;

	case SOCKETSTATE_DISCONNECTED:
		if (!socket_available_read(sock)) {
			log_debugf(HASH_NETWORK,
			           STRING_CONST("Socket (0x%" PRIfixPTR " : %d): all data read in DISCONNECTED"),
			           sock, sockbase->fd);
			socket_close(sock);
		}
		break;

	default:
		break;
	}

	return sockbase->state;
}

void
_socket_store_address_local(socket_t* sock, int family) {
	socket_base_t* sockbase;
	network_address_ip_t* address_local = 0;

	FOUNDATION_ASSERT(sock);
	if (sock->base < 0)
		return;

	sockbase = _socket_base + sock->base;
	if (family == NETWORK_ADDRESSFAMILY_IPV4) {
		address_local = memory_allocate(HASH_NETWORK, sizeof(network_address_ipv4_t), 0,
		                                MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
		address_local->family = NETWORK_ADDRESSFAMILY_IPV4;
		address_local->address_size = sizeof(struct sockaddr_in);
	}
	else if (family == NETWORK_ADDRESSFAMILY_IPV6) {
		address_local = memory_allocate(HASH_NETWORK, sizeof(network_address_ipv6_t), 0,
		                                MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
		address_local->family = NETWORK_ADDRESSFAMILY_IPV6;
		address_local->address_size = sizeof(struct sockaddr_in6);
	}
	else {
		FOUNDATION_ASSERT_FAILFORMAT_LOG(HASH_NETWORK,
		                                 "Unable to get local address for socket (0x%" PRIfixPTR
		                                 " : %d): Unsupported address family %u", sock, sockbase->fd, family);
		return;
	}
	getsockname(sockbase->fd, &address_local->saddr, (socklen_t*)&address_local->address_size);
	memory_deallocate(sock->address_local);
	sock->address_local = (network_address_t*)address_local;
}

static socket_stream_t*
_socket_stream_allocate(socket_t* sock) {
	size_t size = sizeof(socket_stream_t) + _network_config.stream_read_buffer_size +
	              _network_config.stream_write_buffer_size;

	socket_stream_t* sockstream = memory_allocate(HASH_NETWORK, size, 0,
	                                              MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
	stream_t* stream = (stream_t*)sockstream;

	//Network streams are always little endian by default
	stream_initialize(stream, BYTEORDER_LITTLEENDIAN);

	stream->type = STREAMTYPE_SOCKET;
	stream->sequential = 1;
	stream->mode = STREAM_OUT | STREAM_IN | STREAM_BINARY;
	stream->vtable = &_socket_stream_vtable;
	sockstream->buffer_in = sockstream->buffers;
	sockstream->buffer_out = pointer_offset(sockstream->buffer_in,
	                                        _network_config.stream_read_buffer_size);
	sockstream->socket = sock;

	return sockstream;
}

static void
_socket_stream_finalize(stream_t* stream) {
	socket_stream_t* sockstream;
	socket_t* sock;

	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);

	sockstream = (socket_stream_t*)stream;
	sock = sockstream->socket;

	FOUNDATION_ASSERT_MSGFORMAT(sock->stream == sockstream,
	                            "Socket (0x%" PRIfixPTR " : %d): Deallocating stream mismatch, stream is 0x%" PRIfixPTR
	                            ", socket stream is 0x%" PRIfixPTR, sock,
	                            (sock->base >= 0) ? _socket_base[ sock->base ].fd : SOCKET_INVALID, sockstream, sock->stream);
	sock->stream = 0;
	sockstream->socket = 0;
}

static size_t
_socket_stream_available_nonblock_read(const socket_stream_t* stream) {
	return (stream->write_in - stream->read_in) + socket_available_read(stream->socket);
}

static void
_socket_stream_doflush(socket_stream_t* stream) {
	socket_t* sock;
	socket_base_t* sockbase;
	size_t written;

	if (!stream->write_out)
		return;
	sock = stream->socket;
	if (sock->base < 0)
		return;

	sockbase = _socket_base + sock->base;
	if (sockbase->state != SOCKETSTATE_CONNECTED)
		return;

	written = socket_write(sock, stream->buffer_out, stream->write_out);
	if (written) {
		if (written < stream->write_out) {
			memmove(stream->buffer_out, stream->buffer_out + written, stream->write_out - written);
			stream->write_out -= written;
		}
		else {
			stream->write_out = 0;
		}
	}
}

static size_t
_socket_stream_read(stream_t* stream, void* buffer, size_t size) {
	socket_stream_t* sockstream;
	socket_t* sock;

	socket_base_t* sockbase;
	size_t was_read = 0;
	size_t copy;
	bool try_again;
	size_t want_read;

	sockstream = (socket_stream_t*)stream;
	sock = sockstream->socket;
	if (sock->base < 0)
		return 0;

	sockbase = _socket_base + sock->base;

	if ((sockbase->state != SOCKETSTATE_CONNECTED) && (sockbase->state != SOCKETSTATE_DISCONNECTED))
		goto exit;

	if (!size)
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
			log_debugf(HASH_NETWORK, STRING_CONST("Socket stream (0x%" PRIfixPTR
			                                      " : %d) read %" PRIsize" of %" PRIsize " bytes from buffer position %" PRIsize),
			           sock, sockbase->fd, copy, want_read, sockstream->read_in);
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
			sockstream->write_in = socket_read(sock, sockstream->buffer_in,
			                                   _network_config.stream_read_buffer_size);
			if (sockstream->write_in > 0)
				try_again = true;
		}
	}
	while ((was_read < size) && try_again);

	if (was_read < size) {
		if (was_read)
			log_warnf(HASH_NETWORK, WARNING_SUSPICIOUS,
			          STRING_CONST("Socket stream (0x%" PRIfixPTR " : %d): partial read %d of %d bytes"),
			          sock, sockbase->fd, was_read, size);
		_socket_poll_state(sockbase);
	}

exit:

	return was_read;
}

static size_t
_socket_stream_write(stream_t* stream, const void* buffer, size_t size) {
	socket_stream_t* sockstream;
	socket_t* sock;
	socket_base_t* sockbase;
	size_t was_written = 0;
	size_t remain;

	sockstream = (socket_stream_t*)stream;
	sock = sockstream->socket;
	if (sock->base < 0)
		return 0;

	sockbase = _socket_base + sock->base;
	remain = _network_config.stream_write_buffer_size - sockstream->write_out;

	if (sockbase->state != SOCKETSTATE_CONNECTED)
		goto exit;

	if (!size || !buffer)
		goto exit;

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

		_socket_stream_doflush(sockstream);

		if (sockbase->state != SOCKETSTATE_CONNECTED) {
			log_warnf(HASH_NETWORK, WARNING_SUSPICIOUS,
			          STRING_CONST("Socket stream (0x%" PRIfixPTR " : %d): partial write %d of %d bytes"),
			          sock, sockbase->fd, was_written, (unsigned int)size);
			break;
		}

		remain = _network_config.stream_write_buffer_size - sockstream->write_out;

	}
	while (remain);

exit:

	return was_written;
}

static bool
_socket_stream_eos(stream_t* stream) {
	socket_stream_t* sockstream;
	socket_t* sock;
	socket_base_t* sockbase;
	bool eos = false;
	socket_state_t state;

	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);

	sockstream = (socket_stream_t*)stream;
	sock = sockstream->socket;
	if (sock->base < 0)
		return true;

	sockbase = _socket_base + sock->base;
	state = _socket_poll_state(sockbase);
	if (((state != SOCKETSTATE_CONNECTED) || (sockbase->fd == SOCKET_INVALID)) &&
	        !_socket_stream_available_nonblock_read(sockstream))
		eos = true;

	return eos;
}

static size_t
_socket_stream_available_read(stream_t* stream) {
	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);
	return _socket_stream_available_nonblock_read((socket_stream_t*)stream);
}

static void
_socket_stream_buffer_read(stream_t* stream) {
	socket_stream_t* sockstream;
	socket_t* sock;
	socket_base_t* sockbase;
	int available;

	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);

	sockstream = (socket_stream_t*)stream;
	sock = sockstream->socket;
	if (sock->base < 0)
		return;

	sockbase = _socket_base + sock->base;
	if ((sockbase->state != SOCKETSTATE_CONNECTED) || (sockbase->fd == SOCKET_INVALID))
		return;
	if (sockstream->write_in)
		return;

	available = _socket_available_fd(sockbase->fd);
	if (available > 0) {
		size_t was_read = socket_read(sock, sockstream->buffer_in, _network_config.stream_read_buffer_size);
		if (was_read > 0)
			sockstream->write_in += was_read;
	}
}

static void
_socket_stream_flush(stream_t* stream) {
	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);

	_socket_stream_doflush((socket_stream_t*)stream);
}

static void
_socket_stream_truncate(stream_t* stream, size_t size) {
	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);
	FOUNDATION_UNUSED(stream);
	FOUNDATION_UNUSED(size);
}

static size_t
_socket_stream_size(stream_t* stream) {
	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);
	FOUNDATION_UNUSED(stream);
	return 0;
}

static void
_socket_stream_seek(stream_t* stream, ssize_t offset, stream_seek_mode_t direction) {
	socket_stream_t* sockstream;
	socket_t* sock;

	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);

	sockstream = (socket_stream_t*)stream;
	sock = sockstream->socket;

	if ((direction != STREAM_SEEK_CURRENT) || (offset < 0)) {
		log_error(HASH_NETWORK, ERROR_UNSUPPORTED,
		          STRING_CONST("Invalid call, only forward seeking allowed on sockets"));
	}
	else {
		_socket_stream_read(stream, 0, offset);
	}
}

static size_t
_socket_stream_tell(stream_t* stream) {
	socket_stream_t* sockstream;
	socket_t* sock;

	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);

	sockstream = (socket_stream_t*)stream;
	sock = sockstream->socket;

	return sock->bytes_read;
}

static tick_t
_socket_stream_last_modified(const stream_t* stream) {
	FOUNDATION_ASSERT(stream);
	FOUNDATION_ASSERT(stream->type == STREAMTYPE_SOCKET);
	return time_current();
}

int
socket_module_initialize(size_t max_sockets) {
	_socket_base = memory_allocate(HASH_NETWORK, sizeof(socket_base_t) * max_sockets, 16,
	                               MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
	_socket_base_size = (int)max_sockets;
	atomic_store32(&_socket_base_next, 0);

	_socket_stream_vtable.read = _socket_stream_read;
	_socket_stream_vtable.write = _socket_stream_write;
	_socket_stream_vtable.eos = _socket_stream_eos;
	_socket_stream_vtable.flush = _socket_stream_flush;
	_socket_stream_vtable.truncate = _socket_stream_truncate;
	_socket_stream_vtable.size = _socket_stream_size;
	_socket_stream_vtable.seek = _socket_stream_seek;
	_socket_stream_vtable.tell = _socket_stream_tell;
	_socket_stream_vtable.lastmod = _socket_stream_last_modified;
	_socket_stream_vtable.buffer_read = _socket_stream_buffer_read;
	_socket_stream_vtable.available_read = _socket_stream_available_read;
	_socket_stream_vtable.finalize = _socket_stream_finalize;
	_socket_stream_vtable.clone = 0;

	return 0;
}

void
socket_module_finalize(void) {
	if (_socket_base)
		memory_deallocate(_socket_base);
	_socket_base = 0;
}
