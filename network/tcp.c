/* tcp.c  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/rampantpixels/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/tcp.h>
#include <network/address.h>
#include <network/event.h>
#include <network/internal.h>

#include <foundation/foundation.h>

#if FOUNDATION_PLATFORM_POSIX
#  include <netinet/tcp.h>
#endif

static socket_t*
_tcp_socket_allocate(void);

static void
_tcp_socket_open(socket_t*, unsigned int);

static int
_tcp_socket_connect(socket_t*, const network_address_t*, unsigned int);

static void
_tcp_socket_set_delay(socket_t*, bool);

static size_t
_tcp_socket_buffer_read(socket_t*, size_t);

static size_t
_tcp_socket_buffer_write(socket_t*);

static void
_tcp_stream_initialize(socket_t*, stream_t*);

static socket_t*
_tcp_socket_allocate(void) {
	socket_t* sock = _socket_allocate();
	if (!sock)
		return 0;

	sock->open_fn = _tcp_socket_open;
	sock->connect_fn = _tcp_socket_connect;
	sock->read_fn = _tcp_socket_buffer_read;
	sock->write_fn = _tcp_socket_buffer_write;
	sock->stream_initialize_fn = _tcp_stream_initialize;

	return sock;
}

object_t
tcp_socket_create(void) {
	socket_t* sock = _tcp_socket_allocate();
	return sock ? sock->id : 0;
}

bool
tcp_socket_listen(object_t id) {
#if BUILD_ENABLE_LOG
	char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
#endif
	socket_base_t* sockbase;
	socket_t* sock = _socket_lookup(id);

	if (!sock)
		return false;

	if (sock->base < 0) {
		socket_destroy(id);
		return false;
	}

	sockbase = _socket_base + sock->base;
	if ((sockbase->state != SOCKETSTATE_NOTCONNECTED) ||
	        (sockbase->fd == SOCKET_INVALID) ||
	        !sock->address_local) { //Must be locally bound
		socket_destroy(id);
		return false;
	}

	if (listen(sockbase->fd, SOMAXCONN) == 0) {
#if BUILD_ENABLE_LOG
		string_t address = network_address_to_string(buffer, sizeof(buffer), sock->address_local, true);
		log_infof(HASH_NETWORK,
		          STRING_CONST("Listening on TCP/IP socket 0x%llx (0x%" PRIfixPTR " : %d) %.*s"),
		          sock->id, sock, sockbase->fd, STRING_FORMAT(address));
#endif
		sockbase->state = SOCKETSTATE_LISTENING;
		socket_destroy(id);
		return true;
	}

#if BUILD_ENABLE_LOG
	{
		string_t address = network_address_to_string(buffer, sizeof(buffer), sock->address_local, true);
		int sockerr = NETWORK_SOCKET_ERROR;
		string_const_t errmsg = system_error_message(sockerr);
		log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
		           STRING_CONST("Unable to listen on TCP/IP socket 0x%llx (0x%" PRIfixPTR " : %d) %.*s: %.*s (%d)"),
		           id, sock, sockbase->fd, STRING_FORMAT(address), STRING_FORMAT(errmsg), sockerr);
	}
#endif

	socket_destroy(id);

	return false;
}

object_t
tcp_socket_accept(object_t id, unsigned int timeoutms) {
	socket_base_t* sockbase;
	socket_base_t* acceptbase;
	socket_t* sock;
	socket_t* accepted;
	network_address_t* address_remote;
	network_address_ip_t* address_ip;
	socklen_t address_len;
	int err = 0;
	int fd;
	bool blocking;

	sock = _socket_lookup(id);
	if (!sock)
		return 0;

	if (sock->base < 0) {
		socket_destroy(id);
		return 0;
	}

	sockbase = _socket_base + sock->base;
	if ((sockbase->state != SOCKETSTATE_LISTENING) ||
	        (sockbase->fd == SOCKET_INVALID) ||
	        !sock->address_local) { //Must be locally bound
		log_errorf(HASH_NETWORK, ERROR_INVALID_VALUE,
		           STRING_CONST("Unable to accept on a non-listening/unbound socket (fd %d, state %d)"),
		           sockbase->fd, sockbase->state);
		socket_destroy(id);
		return 0;
	}

	blocking = ((sockbase->flags & SOCKETFLAG_BLOCKING) != 0);

	if ((timeoutms > 0) && blocking)
		_socket_set_blocking(sock, false);

	address_remote = network_address_clone(sock->address_local);
	address_ip = (network_address_ip_t*)address_remote;
	address_len = address_remote->address_size;

	fd = (int)accept(sockbase->fd, &address_ip->saddr, &address_len);
	if (fd < 0) {
		err = NETWORK_SOCKET_ERROR;
		if (timeoutms > 0) {
#if FOUNDATION_PLATFORM_WINDOWS
			if (err == WSAEWOULDBLOCK)
#else
			if (err == EAGAIN)
#endif
			{
				struct timeval tval;
				fd_set fdread, fderr;
				int ret;

				FD_ZERO(&fdread);
				FD_ZERO(&fderr);
				FD_SET(sockbase->fd, &fdread);
				FD_SET(sockbase->fd, &fderr);

				tval.tv_sec  = timeoutms / 1000;
				tval.tv_usec = (timeoutms % 1000) * 1000;

				ret = select(sockbase->fd + 1, &fdread, 0, &fderr, &tval);
				if (ret > 0) {
					address_len = address_remote->address_size;
					fd = (int)accept(sockbase->fd, &address_ip->saddr, &address_len);
				}
			}
		}
	}

	if ((timeoutms > 0) && blocking)
		_socket_set_blocking(sock, true);

	sockbase->flags &= SOCKETFLAG_CONNECTION_PENDING;

	if (fd < 0) {
		log_debugf(HASH_NETWORK, STRING_CONST("Accept returned invalid socket fd: %d"), fd);
		memory_deallocate(address_remote);
		socket_destroy(id);
		return 0;
	}

	accepted = _tcp_socket_allocate();
	if (!accepted) {
		log_debugf(HASH_NETWORK, STRING_CONST("Unable to allocate socket for accepted fd: %d"), fd);
		socket_destroy(id);
		return 0;
	}

	if (_socket_allocate_base(accepted) < 0) {
		log_debugf(HASH_NETWORK, STRING_CONST("Unable to allocate socket base for accepted fd: %d"), fd);
		socket_destroy(accepted->id);
		socket_destroy(id);
		return 0;
	}

	acceptbase = _socket_base + accepted->base;
	acceptbase->fd = fd;
	acceptbase->state = SOCKETSTATE_CONNECTED;
	accepted->address_remote = (network_address_t*)address_remote;

	_socket_store_address_local(accepted, address_ip->family);

#if BUILD_ENABLE_LOG
	{
		char listenbuf[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		char localbuf[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		char remotebuf[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_t address_listen = network_address_to_string(listenbuf, sizeof(listenbuf),
		                                                    sock->address_local, true);
		string_t address_local = network_address_to_string(localbuf, sizeof(localbuf),
		                                                   accepted->address_local, true);
		string_t address_remote = network_address_to_string(remotebuf, sizeof(remotebuf),
		                                                    accepted->address_remote, true);
		log_infof(HASH_NETWORK,
		          STRING_CONST("Accepted connection on TCP/IP socket 0x%llx (0x%"
		                       PRIfixPTR" : %d) %.*s: created socket 0x%llx (0x%" PRIfixPTR " : %d) %.*s with remote address %.*s"),
		          sock->id, sock, sockbase->fd, STRING_FORMAT(address_listen), accepted->id, accepted, acceptbase->fd,
		          STRING_FORMAT(address_local), STRING_FORMAT(address_remote));
	}
#endif

	socket_destroy(id);

	return accepted->id;
}

bool
tcp_socket_delay(object_t id) {
	bool delay = false;
	socket_t* sock = _socket_lookup(id);
	if (sock && (sock->base >= 0)) {
		socket_base_t* sockbase = _socket_base + sock->base;
		delay = ((sockbase->flags & SOCKETFLAG_TCPDELAY) != 0);
		socket_destroy(id);
	}
	return delay;
}

void
tcp_socket_set_delay(object_t id, bool delay) {
	socket_t* sock = _socket_lookup(id);
	if (!sock) {
		log_errorf(HASH_NETWORK, ERROR_INVALID_VALUE,
		           STRING_CONST("Trying to set delay flag on an invalid socket 0x%llx"), id);
		return;
	}

	_tcp_socket_set_delay(sock, delay);

	socket_destroy(id);
}

static void
_tcp_socket_open(socket_t* sock, unsigned int family) {
	socket_base_t* sockbase;

	if (sock->base < 0)
		return;

	sockbase = _socket_base + sock->base;
	if (family == NETWORK_ADDRESSFAMILY_IPV6)
		sockbase->fd = (int)socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	else
		sockbase->fd = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (sockbase->fd < 0) {
		int err = NETWORK_SOCKET_ERROR;
		string_const_t errmsg = system_error_message(err);
		log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
		           STRING_CONST("Unable to open TCP/IP socket 0x%llx (0x%" PRIfixPTR " : %d): %.*s (%d)"),
		           sock->id, sock, sockbase->fd, STRING_FORMAT(errmsg), err);
		sockbase->fd = SOCKET_INVALID;
	}
	else {
		log_debugf(HASH_NETWORK, STRING_CONST("Opened TCP/IP socket 0x%llx (0x%" PRIfixPTR " : %d)"),
		           sock->id, sock, sockbase->fd);

		_tcp_socket_set_delay(sock, sockbase->flags & SOCKETFLAG_TCPDELAY);
	}
}

static int
_tcp_socket_connect(socket_t* sock, const network_address_t* address, unsigned int timeoutms) {
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
		_socket_set_blocking(sock, false);

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
		_socket_set_blocking(sock, true);

	if (failed) {
#if BUILD_ENABLE_DEBUG_LOG
		char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), address, true);
		log_debugf(HASH_NETWORK, 
		           STRING_CONST("Failed to connect socket 0x%llx (0x%" PRIfixPTR " : %d) to remote host %.*s: %.*s"),
		           sock->id, sock, sockbase->fd, STRING_FORMAT(address_str), STRING_FORMAT(error_message));
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
		           STRING_CONST("%s socket 0x%llx (0x%" PRIfixPTR " : %d) to remote host %.*s"),
		           (sockbase->state == SOCKETSTATE_CONNECTING) ? "Connecting" : "Connected", sock->id,
		           sock, sockbase->fd, STRING_FORMAT(address_str));
	}
#endif

	return 0;
}

static void
_tcp_socket_set_delay(socket_t* sock, bool delay) {
	socket_base_t* sockbase;
	int flag;
	FOUNDATION_ASSERT(sock);
	if (sock->base < 0)
		return;
	sockbase = _socket_base + sock->base;
	sockbase->flags = (delay ? sockbase->flags | SOCKETFLAG_TCPDELAY : sockbase->flags &
	                   ~SOCKETFLAG_TCPDELAY);
	flag = (delay ? 0 : 1);
	if (sockbase->fd != SOCKET_INVALID)
		setsockopt(sockbase->fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&flag, sizeof(int));
}

static size_t
_tcp_socket_buffer_read(socket_t* sock, size_t wanted_size) {
	socket_base_t* sockbase;
	size_t available;
	size_t max_read = 0;
	size_t try_read;
	size_t total_read = 0;
	long ret;

	if (sock->base < 0)
		return 0;

	if (sock->offset_write_in >= sock->offset_read_in) {
		max_read = _network_config.socket_read_buffer_size - sock->offset_write_in;
		if (!sock->offset_read_in)
			--max_read; //Don't read if write ptr wraps to 0 and equals read ptr (then the entire buffer is discarded)
	}
	else
		max_read = sock->offset_read_in - sock->offset_write_in -
		           1; //-1 so write ptr doesn't end up equal to read ptr (then the entire buffer is discarded)

	if (!max_read)
		return 0;

	try_read = max_read;
	if (wanted_size && (try_read > wanted_size))
		try_read = wanted_size;

	sockbase = _socket_base + sock->base;
	available = _socket_available_fd(sockbase->fd);
	if (!available && (!wanted_size || ((sockbase->flags & SOCKETFLAG_BLOCKING) == 0)))
		return 0;

	if (available > try_read)
		try_read = (max_read < available) ? max_read : available;

	ret = recv(sockbase->fd, (char*)sock->buffer_in + sock->offset_write_in, (int)try_read, 0);
	if (!ret) {
#if BUILD_ENABLE_DEBUG_LOG
		char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), sock->address_remote,
		                                                 true);
		log_debugf(HASH_NETWORK,
		           STRING_CONST("Socket closed gracefully on remote end 0x%llx (0x%" PRIfixPTR " : %d): %.*s"),
		           sock->id, sock, sockbase->fd, STRING_FORMAT(address_str));
#endif
		_socket_close(sock);
		if (!(sockbase->flags & SOCKETFLAG_HANGUP_PENDING)) {
			sockbase->flags |= SOCKETFLAG_HANGUP_PENDING;
			network_event_post(NETWORKEVENT_HANGUP, sock->id);
		}
		return 0;
	}
	else if (ret > 0) {
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
		const unsigned char* src = (const unsigned char*)sock->buffer_in + sock->offset_write_in;
		char dump_buffer[66];
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 0
		log_debugf(HASH_NETWORK,
		           STRING_CONST("Socket 0x%llx (0x%" PRIfixPTR " : %d) read %d of %u (%u were available, %u wanted) bytes from TCP/IP socket to buffer position %d"),
		           sock->id, sock, sockbase->fd, ret, try_read, available, wanted_size, sock->offset_write_in);
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
		for (long row = 0; row <= (ret / 8); ++row) {
			long ofs = 0, col = 0, cols = 8;
			if ((row + 1) * 8 > ret)
				cols = ret - (row * 8);
			for (; col < cols; ++col, ofs += 3) {
				string_format(dump_buffer + ofs, 66 - (unsigned int)ofs, "%02x", *(src + (row * 8) + col));
				*(dump_buffer + ofs + 2) = ' ';
			}
			if (ofs) {
				*(dump_buffer + ofs - 1) = 0;
				log_debug(HASH_NETWORK, dump_buffer, ofs - 1);
			}
		}
#endif

		sock->offset_write_in += ret;
		total_read += ret;
		FOUNDATION_ASSERT_MSG(sock->offset_write_in <= _network_config.socket_read_buffer_size, "Buffer overwrite");
		if (sock->offset_write_in >= _network_config.socket_read_buffer_size)
			sock->offset_write_in = 0;
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
			          STRING_CONST("Socket recv() failed on TPC/IP socket 0x%llx (0x%" PRIfixPTR " : %d): %.*s (%d)"),
			          sock->id, sock, sockbase->fd, STRING_FORMAT(errmsg), sockerr);
		}

#if FOUNDATION_PLATFORM_WINDOWS
		if ((sockerr == WSAENETDOWN) || (sockerr == WSAENETRESET) || (sockerr == WSAENOTCONN) ||
		        (sockerr == WSAECONNABORTED) || (sockerr == WSAECONNRESET) || (sockerr == WSAETIMEDOUT))
#else
		if ((sockerr == ECONNRESET) || (sockerr == EPIPE) || (sockerr == ETIMEDOUT))
#endif
		{
			_socket_close(sock);
			if (!(sockbase->flags & SOCKETFLAG_HANGUP_PENDING)) {
				sockbase->flags |= SOCKETFLAG_HANGUP_PENDING;
				network_event_post(NETWORKEVENT_HANGUP, sock->id);
			}
		}

		_socket_poll_state(sockbase);
	}

	//If we were at end of read buffer, try more data if wanted
	if ((sockbase->state == SOCKETSTATE_CONNECTED) && (try_read < wanted_size) &&
	        (available > try_read) && (sock->offset_write_in == 0) && (sock->offset_read_in > 1) && (ret > 0))
		total_read += _tcp_socket_buffer_read(sock, wanted_size - try_read);

	return total_read;
}

static size_t
_tcp_socket_buffer_write(socket_t* sock) {
	socket_base_t* sockbase;
	size_t sent = 0;

	if (sock->base < 0)
		return 0;

	sockbase = _socket_base + sock->base;
	while (sent < sock->offset_write_out) {
		long res = send(sockbase->fd, (const char*)sock->buffer_out + sent,
		                (int)(sock->offset_write_out - sent), 0);
		if (res > 0) {
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
			const unsigned char* src = (const unsigned char*)sock->buffer_out + sent;
			char buffer[34];
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 0
			log_debugf(HASH_NETWORK,
			           STRING_CONST("Socket 0x%llx (0x%" PRIfixPTR " : %d) wrote %d of %d bytes bytes to TCP/IP socket from buffer position %d"),
			           sock->id, sock, sockbase->fd, res, sock->offset_write_out - sent, sent);
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
			for (long row = 0; row <= (res / 8); ++row) {
				long ofs = 0, col = 0, cols = 8;
				if ((row + 1) * 8 > res)
					cols = res - (row * 8);
				for (; col < cols; ++col, ofs += 3) {
					string_format(buffer + ofs, 34 - (unsigned int)ofs, "%02x", *(src + (row * 8) + col));
					*(buffer + ofs + 2) = ' ';
				}
				if (ofs) {
					*(buffer + ofs - 1) = 0;
					log_debug(HASH_NETWORK, buffer, ofs - 1);
				}
			}
#endif
			sent += res;
		}
		else {
			if (res < 0) {
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
					          STRING_CONST("Partial TCP socket send() on 0x%llx (0x%" PRIfixPTR
					          " : %d): %d of %d bytes written to socket (SO_ERROR %d)"),
					          sock->id, sock, sockbase->fd, sent, sock->offset_write_out, serr);
					sockbase->flags |= SOCKETFLAG_REFLUSH;
				}
				else {
					log_warnf(HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
					          STRING_CONST("Socket send() failed on TCP/IP socket 0x%llx (0x%" PRIfixPTR " : %d): %s (%d) (SO_ERROR %d)"),
					          sock->id, sock, sockbase->fd, system_error_message(sockerr), sockerr, serr);
				}

#if FOUNDATION_PLATFORM_WINDOWS
				if ((sockerr == WSAENETDOWN) || (sockerr == WSAENETRESET) || (sockerr == WSAENOTCONN) ||
				        (sockerr == WSAECONNABORTED) || (sockerr == WSAECONNRESET) || (sockerr == WSAETIMEDOUT))
#else
				if ((sockerr == ECONNRESET) || (sockerr == EPIPE) || (sockerr == ETIMEDOUT))
#endif
				{
					_socket_close(sock);
					if (!(sockbase->flags & SOCKETFLAG_HANGUP_PENDING)) {
						sockbase->flags |= SOCKETFLAG_HANGUP_PENDING;
						network_event_post(NETWORKEVENT_HANGUP, sock->id);
					}
				}

				if (sockbase->state != SOCKETSTATE_NOTCONNECTED)
					_socket_poll_state(sockbase);
			}

			if (sent) {
				memmove(sock->buffer_out, sock->buffer_out + sent, sock->offset_write_out - sent);
				sock->offset_write_out -= sent;
			}

			return sent;
		}
	}

	sockbase->flags &= ~SOCKETFLAG_REFLUSH;
	sock->offset_write_out = 0;

	return sent;
}

void
_tcp_stream_initialize(socket_t* sock, stream_t* stream) {
	stream->inorder = 1;
	stream->reliable = 1;
	stream->path = string_allocate_format(STRING_CONST("tcp://%llx"), sock->id);
}

