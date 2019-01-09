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
#include <network/socket.h>
#include <network/address.h>
#include <network/internal.h>

#include <foundation/foundation.h>

#if FOUNDATION_PLATFORM_POSIX
#  include <netinet/tcp.h>
#endif

static void
_tcp_socket_open(socket_t*, unsigned int);

static void
_tcp_stream_initialize(socket_t*, stream_t*);

socket_t*
tcp_socket_allocate(void) {
	socket_t* sock = memory_allocate(HASH_NETWORK, sizeof(socket_t), 0,
	                                 MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
	tcp_socket_initialize(sock);
	return sock;
}

void
tcp_socket_initialize(socket_t* sock) {
	_socket_initialize(sock);

	sock->type = NETWORK_SOCKETTYPE_TCP;
	sock->open_fn = _tcp_socket_open;
	sock->stream_initialize_fn = _tcp_stream_initialize;
}

bool
tcp_socket_listen(socket_t* sock) {
#if BUILD_ENABLE_LOG
	char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
#endif
	if ((sock->fd == NETWORK_SOCKET_INVALID) ||
	    (sock->state != SOCKETSTATE_NOTCONNECTED) ||
	    !sock->address_local) {
		//Must be locally bound
		return false;
	}

	if (listen(sock->fd, SOMAXCONN) != 0) {
#if BUILD_ENABLE_LOG
		string_t address = network_address_to_string(buffer, sizeof(buffer), sock->address_local, true);
		int sockerr = NETWORK_SOCKET_ERROR;
		string_const_t errmsg = system_error_message(sockerr);
		log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
		           STRING_CONST("Unable to listen on TCP/IP socket (0x%" PRIfixPTR " : %d) %.*s: %.*s (%d)"),
		           (uintptr_t)sock, sock->fd, STRING_FORMAT(address), STRING_FORMAT(errmsg), sockerr);
#endif
		return false;
	}

#if BUILD_ENABLE_LOG
	string_t address = network_address_to_string(buffer, sizeof(buffer), sock->address_local, true);
	log_infof(HASH_NETWORK,
	          STRING_CONST("Listening on TCP/IP socket (0x%" PRIfixPTR " : %d) %.*s"),
	          (uintptr_t)sock, sock->fd, STRING_FORMAT(address));
#endif
	sock->state = SOCKETSTATE_LISTENING;

	if (sock->beacon)
		socket_set_beacon(sock, sock->beacon);

	return true;
}

socket_t*
tcp_socket_accept(socket_t* sock, unsigned int timeoutms) {
	socket_t* accepted;
	network_address_t* address_remote;
	network_address_ip_t* address_ip;
	socklen_t address_len;
	int err = 0;
	int fd;
	bool blocking;

	if (sock->fd == NETWORK_SOCKET_INVALID)
		return 0;

	if ((sock->state != SOCKETSTATE_LISTENING) ||
	        (sock->fd == NETWORK_SOCKET_INVALID) ||
	        !sock->address_local) { //Must be locally bound
		log_errorf(HASH_NETWORK, ERROR_INVALID_VALUE,
		           STRING_CONST("Unable to accept on a non-listening/unbound TCP/IP socket (%" PRIfixPTR
		                        " : %d) state %d)"),
		           (uintptr_t)sock, sock->fd, sock->state);
		return 0;
	}

	blocking = ((sock->flags & SOCKETFLAG_BLOCKING) != 0);

	if ((timeoutms != NETWORK_TIMEOUT_INFINITE) && blocking)
		socket_set_blocking(sock, false);

	address_remote = network_address_clone(sock->address_local);
	address_ip = (network_address_ip_t*)address_remote;
	address_len = address_remote->address_size;

	fd = (int)accept(sock->fd, &address_ip->saddr, &address_len);
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
				FD_SET(sock->fd, &fdread);
				FD_SET(sock->fd, &fderr);

				tval.tv_sec  = timeoutms / 1000;
				tval.tv_usec = (timeoutms % 1000) * 1000;

				ret = select(sock->fd + 1, &fdread, 0, &fderr, (timeoutms != NETWORK_TIMEOUT_INFINITE) ? &tval : nullptr);
				if (ret > 0) {
					address_len = address_remote->address_size;
					fd = (int)accept(sock->fd, &address_ip->saddr, &address_len);
				}
			}
		}
	}

	if ((timeoutms != NETWORK_TIMEOUT_INFINITE) && blocking)
		socket_set_blocking(sock, true);

	if (fd < 0) {
		log_debugf(HASH_NETWORK, STRING_CONST("Accept returned invalid socket fd: %d"), fd);
		memory_deallocate(address_remote);
		return 0;
	}

	accepted = tcp_socket_allocate();
	if (!accepted) {
		log_debugf(HASH_NETWORK, STRING_CONST("Unable to allocate socket for accepted fd: %d"), fd);
		_socket_close_fd(fd);
		return 0;
	}

	accepted->fd = fd;
	accepted->state = SOCKETSTATE_CONNECTED;
	accepted->family = address_ip->family;
	accepted->address_remote = (network_address_t*)address_remote;

	_socket_store_address_local(accepted, (int)address_ip->family);

#if BUILD_ENABLE_LOG
	{
		char listenbuf[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		char localbuf[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		char remotebuf[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_t listenstr = network_address_to_string(listenbuf, sizeof(listenbuf),
		                                               sock->address_local, true);
		string_t localstr = network_address_to_string(localbuf, sizeof(localbuf),
		                                              accepted->address_local, true);
		string_t remotestr = network_address_to_string(remotebuf, sizeof(remotebuf),
		                                               accepted->address_remote, true);
		log_infof(HASH_NETWORK,
		          STRING_CONST("Accepted connection on TCP/IP socket (0x%"
		                       PRIfixPTR" : %d) %.*s: created socket (0x%" PRIfixPTR " : %d) %.*s with remote address %.*s"),
		          (uintptr_t)sock, sock->fd, STRING_FORMAT(listenstr), (uintptr_t)accepted, accepted->fd,
		          STRING_FORMAT(localstr), STRING_FORMAT(remotestr));
	}
#endif

	return accepted;
}

bool
tcp_socket_delay(socket_t* sock) {
	return ((sock->flags & SOCKETFLAG_TCPDELAY) != 0);
}

void
tcp_socket_set_delay(socket_t* sock, bool delay) {
	int flag;
	sock->flags = (delay ?
	               sock->flags | SOCKETFLAG_TCPDELAY :
	               sock->flags & ~SOCKETFLAG_TCPDELAY);
	flag = (delay ? 0 : 1);
	if (sock->fd != NETWORK_SOCKET_INVALID)
		setsockopt(sock->fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&flag, sizeof(int));
}

static void
_tcp_socket_open(socket_t* sock, unsigned int family) {
	if (sock->fd != NETWORK_SOCKET_INVALID)
		return;

	sock->fd = (int)socket((family == NETWORK_ADDRESSFAMILY_IPV6) ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock->fd < 0) {
		int err = NETWORK_SOCKET_ERROR;
		string_const_t errmsg = system_error_message(err);
		log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
		           STRING_CONST("Unable to open TCP/IP socket (0x%" PRIfixPTR " : %d): %.*s (%d)"),
		           (uintptr_t)sock, sock->fd, STRING_FORMAT(errmsg), err);
		sock->fd = NETWORK_SOCKET_INVALID;
	}
	else {
		log_debugf(HASH_NETWORK, STRING_CONST("Opened TCP/IP socket (0x%" PRIfixPTR " : %d)"),
		           (uintptr_t)sock, sock->fd);
		tcp_socket_set_delay(sock, sock->flags & SOCKETFLAG_TCPDELAY);
	}
}

void
_tcp_stream_initialize(socket_t* sock, stream_t* stream) {
	stream->inorder = 1;
	stream->reliable = 1;
	stream->path = string_allocate_format(STRING_CONST("tcp://%" PRIfixPTR), (uintptr_t)sock);
}

