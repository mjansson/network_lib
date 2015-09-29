/* udp.c  -  Network library  -  Public Domain  -  2014 Mattias Jansson / Rampant Pixels
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/rampantpixels/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/udp.h>
#include <network/address.h>
#include <network/event.h>
#include <network/internal.h>

#include <foundation/foundation.h>

static socket_t*
_udp_socket_allocate(void);

static void
_udp_socket_open(socket_t*, unsigned int);

static int
_udp_socket_connect(socket_t*, const network_address_t*, unsigned int);

static size_t
_udp_socket_buffer_read(socket_t*, size_t);

static size_t
_udp_socket_buffer_write(socket_t*);

static void
_udp_stream_initialize(socket_t*, stream_t*);

static socket_t*
_udp_socket_allocate(void) {
	socket_t* sock = _socket_allocate();
	if (!sock)
		return 0;

	sock->open_fn = _udp_socket_open;
	sock->connect_fn = _udp_socket_connect;
	sock->read_fn = _udp_socket_buffer_read;
	sock->write_fn = _udp_socket_buffer_write;
	sock->stream_initialize_fn = _udp_stream_initialize;

	return sock;
}

object_t
udp_socket_create(void) {
	socket_t* sock = _udp_socket_allocate();
	return sock ? sock->id : 0;
}

static void
_udp_socket_open(socket_t* sock, unsigned int family) {
	socket_base_t* sockbase;

	if (sock->base < 0)
		return;

	sockbase = _socket_base + sock->base;
	if (family == NETWORK_ADDRESSFAMILY_IPV6)
		sockbase->fd = (int)socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	else
		sockbase->fd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sockbase->fd < 0) {
		int err = NETWORK_SOCKET_ERROR;
		string_const_t errmsg = system_error_message(err);
		log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
		           STRING_CONST("Unable to open UDP socket 0x%llx (0x%" PRIfixPTR " : %d): %.*s (%d)"), sock->id, sock,
		           sockbase->fd, STRING_FORMAT(errmsg), err);
		sockbase->fd = SOCKET_INVALID;
	}
	else {
		log_debugf(HASH_NETWORK, STRING_CONST("Opened UDP socket 0x%llx (0x%" PRIfixPTR " : %d)"), sock->id,
		           sock,
		           sockbase->fd);
	}
}

static int
_udp_socket_connect(socket_t* sock, const network_address_t* address, unsigned int timeoutms) {
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
		log_debugf(HASH_NETWORK, STRING_CONST("Failed to connect socket 0x%llx (0x%" PRIfixPTR
		                                      " : %d) to remote host %.*s: %.*s"),
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
		log_debugf(HASH_NETWORK, STRING_CONST("%s socket 0x%llx (0x%" PRIfixPTR
		                                      " : %d) to remote host %.*s"),
		           (sockbase->state == SOCKETSTATE_CONNECTING) ? "Connecting" : "Connected", sock->id, sock,
		           sockbase->fd, STRING_FORMAT(address_str));
	}
#endif

	return 0;
}

static size_t
_udp_socket_buffer_read(socket_t* sock, size_t wanted_size) {
	socket_base_t* sockbase;
	size_t available;
	size_t max_read = 0;
	size_t try_read;
	size_t total_read = 0;
	bool is_blocking = false;
	long ret;

	if (sock->base < 0)
		return 0;

	if (sock->offset_write_in == sock->offset_read_in) {
		sock->offset_write_in = sock->offset_read_in = 0;
		max_read = _network_config.socket_read_buffer_size - 1;
	}
	else if (sock->offset_write_in > sock->offset_read_in) {
		max_read = _network_config.socket_read_buffer_size - sock->offset_write_in;
		if (!sock->offset_read_in)
			--max_read; //Don't read if write ptr wraps to 0 and equals read ptr (then the entire buffer is discarded)
	}
	else
		max_read = sock->offset_read_in - sock->offset_write_in -
		           1; //-1 so write ptr doesn't end up equal to read ptr (then the entire buffer is discarded)

	if (!max_read)
		return 0;

	sockbase = _socket_base + sock->base;
	if (sockbase->state != SOCKETSTATE_CONNECTED) {
		//Must use recvfrom for unconnected datagram I/O
		FOUNDATION_ASSERT_FAILFORMAT_LOG(HASH_NETWORK,
		                                 "Trying to stream read from an unconnected UDP socket 0x%llx (0x%" PRIfixPTR " : %d) in state %u",
		                                 sock->id, sock, sockbase->fd, sockbase->state);
		return 0;
	}

	is_blocking = ((sockbase->flags & SOCKETFLAG_BLOCKING) != 0);
	available = _socket_available_fd(sockbase->fd);
	if (available) {
		try_read = (max_read < available) ? max_read : available;
	}
	else {
		if (!wanted_size || !is_blocking)
			return 0;
		try_read = max_read;
	}

	ret = recv(sockbase->fd, (char*)sock->buffer_in + sock->offset_write_in, (int)try_read, 0);
	if (!ret) {
#if BUILD_ENABLE_DEBUG_LOG
		char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), sock->address_remote,
		                                                 true);
		log_debugf(HASH_NETWORK, STRING_CONST("Socket closed gracefully on remote end 0x%llx (0x%" PRIfixPTR
		                                      " : %d): %.*s"), sock->id, sock, sockbase->fd, STRING_FORMAT(address_str));
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
#if BUILD_ENABLE_LOG
		if (!available && (ret == (int)try_read))
			log_warnf(HASH_NETWORK, WARNING_SUSPICIOUS,
			          STRING_CONST("Socket 0x%llx (0x%" PRIfixPTR
			                       " : %d): potential partial blocking UDP read %d of %d bytes (%u available)"),
			          sock->id, sock, sockbase->fd, ret, try_read, available);
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 0
		log_debugf(HASH_NETWORK, STRING_CONST("Socket 0x%llx (0x%" PRIfixPTR
		                                      " : %d) read %d of %u (%u were available, %u wanted) bytes from UDP socket to buffer position %d"),
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
		FOUNDATION_ASSERT_MSG(sock->offset_write_in <= _network_config.socket_read_buffer_size,
		                      "Buffer overwrite");
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
			          STRING_CONST("Socket recv() failed on UDP socket 0x%llx (0x%" PRIfixPTR " : %d): %.*s (%d)"),
			          sock->id, sock,
			          sockbase->fd, STRING_FORMAT(errmsg), sockerr);
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
		total_read += _udp_socket_buffer_read(sock, wanted_size - try_read);

	return total_read;
}

static size_t
_udp_socket_buffer_write(socket_t* sock) {
	socket_base_t* sockbase;
	size_t sent = 0;
	if (sock->base < 0)
		return 0;

	sockbase = _socket_base + sock->base;
	if (sockbase->state != SOCKETSTATE_CONNECTED) {
		//Must use recvfrom for unconnected datagram I/O
		FOUNDATION_ASSERT_FAILFORMAT_LOG(HASH_NETWORK,
		                                 "Trying to stream send from an unconnected UDP socket 0x%llx (0x%" PRIfixPTR " : %d) in state %u",
		                                 sock->id, sock, sockbase->fd, sockbase->state);
		return 0;
	}

	while (sent < sock->offset_write_out) {
		long res = send(sockbase->fd, (const char*)sock->buffer_out + sent,
		                (int)(sock->offset_write_out - sent), 0);
		if (res > 0) {
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
			const unsigned char* src = (const unsigned char*)sock->buffer_out + sent;
			char buffer[34];
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 0
			log_debugf(HASH_NETWORK, STRING_CONST("Socket 0x%llx (0x%" PRIfixPTR
			                                      " : %d) wrote %d of %d bytes bytes to UDP socket from buffer position %d"), sock->id, sock,
			           sockbase->fd, res, sock->offset_write_out - sent, sent);
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
					          STRING_CONST("Partial UDP socket send() on 0x%llx (0x%" PRIfixPTR
					                       " : %d): %d of %d bytes written to socket (SO_ERROR %d))"),
					          sock->id, sock, sockbase->fd, sent, sock->offset_write_out, serr);
					sockbase->flags |= SOCKETFLAG_REFLUSH;
				}
				else {
					log_warnf(HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
					          STRING_CONST("Socket send() failed on UDP socket 0x%llx (0x%" PRIfixPTR
					                       " : %d): %s (%d) (SO_ERROR %d)"),
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
_udp_stream_initialize(socket_t* sock, stream_t* stream) {
	stream->inorder = 0;
	stream->reliable = 0;
	stream->path = string_allocate_format(STRING_CONST("udp://%llx"), sock->id);
}

network_datagram_t
udp_socket_recvfrom(object_t id, network_address_t const** address) {
	network_datagram_t datagram = { 0, 0 };

	socket_t* sock;
	socket_base_t* sockbase;
	network_address_ip_t* addr_ip;
	size_t available;
	size_t max_read = _network_config.socket_read_buffer_size;
	size_t try_read;
	bool is_blocking = false;
	long ret;

	if (address)
		*address = 0;

	sock = _socket_lookup(id);
	if (!sock || (sock->base < 0))
		goto exit;

	sockbase = _socket_base + sock->base;
	if ((sockbase->fd == SOCKET_INVALID) || !sock->address_local)
		goto exit;
	if (sockbase->state != SOCKETSTATE_NOTCONNECTED) {
		FOUNDATION_ASSERT_FAILFORMAT_LOG(HASH_NETWORK,
		                                 "Trying to datagram read from a connected UDP socket 0x%llx (0x%" PRIfixPTR " : %d) in state %u",
		                                 sock->id, sock, sockbase->fd, sockbase->state);
		goto exit;
	}

	is_blocking = ((sockbase->flags & SOCKETFLAG_BLOCKING) != 0);
	available = _socket_available_fd(sockbase->fd);
	if (available) {
		try_read = (max_read < available) ? max_read : available;
	}
	else {
		if (!is_blocking)
			goto exit;
		try_read = max_read;
	}

	if (!sock->address_remote || (sock->address_remote->family != sock->address_local->family)) {
		if (sock->address_remote)
			memory_deallocate(sock->address_remote);
		sock->address_remote = network_address_clone(sock->address_local);
	}
	addr_ip = (network_address_ip_t*)sock->address_remote;

	ret = recvfrom(sockbase->fd, (char*)sock->buffer_in, (int)try_read, 0, &addr_ip->saddr,
	               &addr_ip->address_size);
	if (ret > 0) {
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
		const unsigned char* src = (const unsigned char*)sock->buffer_in;
		char dump_buffer[66];
#endif
#if BUILD_ENABLE_LOG
		if (!available && (ret == (int)try_read))
			log_warnf(HASH_NETWORK, WARNING_SUSPICIOUS,
			          STRING_CONST("Socket 0x%llx (0x%" PRIfixPTR
			                       " : %d): potential partial blocking UDP datagram read %d of %d bytes (%u available)"),
			          sock->id, sock, sockbase->fd, ret, try_read, available);
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 0
		log_debugf(HASH_NETWORK, STRING_CONST("Socket 0x%llx (0x%" PRIfixPTR
		                                      " : %d) read %d of %u (%u were available) bytes from UDP socket to datagram"),
		           sock->id, sock, sockbase->fd, ret, try_read, available);
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

		datagram.data = sock->buffer_in;
		datagram.size = ret;

		if (address)
			*address = sock->address_remote;
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
			          STRING_CONST("Socket recvfrom() failed on UDP socket 0x%llx (0x%" PRIfixPTR " : %d): %.*s (%d)"),
			          sock->id, sock,
			          sockbase->fd, STRING_FORMAT(errmsg), sockerr);
		}
	}

	//Trigger read events again (or else poll->read->poll with same amount of buffered data will not trigger event)
	sockbase->last_event = 0;

exit:

	if (sock)
		socket_destroy(id);

	return datagram;
}

uint64_t
udp_socket_sendto(object_t id, const network_datagram_t datagram,
                  const network_address_t* address) {
	socket_t* sock;
	socket_base_t* sockbase;
	const network_address_ip_t* addr_ip;
	long res = 0;

	if (!address)
		return 0;

	sock = _socket_lookup(id);
	if (!sock)
		goto exit;

	if (_socket_create_fd(sock, address->family) == SOCKET_INVALID)
		goto exit;

	sockbase = _socket_base + sock->base;
	if (sockbase->state != SOCKETSTATE_NOTCONNECTED) {
		FOUNDATION_ASSERT_FAILFORMAT_LOG(HASH_NETWORK,
		                                 "Trying to datagram send from a connected UDP socket 0x%llx (0x%" PRIfixPTR " : %d) in state %u",
		                                 sock->id, sock, sockbase->fd, sockbase->state);
		goto exit;
	}
	if (_socket_create_fd(sock, address->family) == SOCKET_INVALID) {
		FOUNDATION_ASSERT_FAILFORMAT_LOG(HASH_NETWORK,
		                                 "Trying to datagram send from an invalid UDP socket 0x%llx (0x%" PRIfixPTR " : %d) in state %u",
		                                 sock->id, sock, sockbase->fd, sockbase->state);
		goto exit;
	}
	addr_ip = (const network_address_ip_t*)address;

	res = sendto(sockbase->fd, datagram.data, (int)datagram.size, 0, &addr_ip->saddr,
	             addr_ip->address_size);
	if (res > 0) {
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
		const unsigned char* src = (const unsigned char*)datagram.data;
		char buffer[34];
#endif
#if BUILD_ENABLE_LOG
		if ((uint64_t)res != datagram.size)
			log_warnf(HASH_NETWORK, WARNING_SUSPICIOUS,
			          STRING_CONST("Socket 0x%llx (0x%" PRIfixPTR " : %d): partial UDP datagram write %d of %d bytes"),
			          sock->id, sock, sockbase->fd, res, (unsigned int)datagram.size);
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 0
		log_debugf(HASH_NETWORK, STRING_CONST("Socket 0x%llx (0x%" PRIfixPTR
		                                      " : %d) wrote %d of %d bytes bytes to UDP socket"),
		           sock->id, sock, sockbase->fd, res, (unsigned int)datagram.size);
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
	}
	else {
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
			          STRING_CONST("Unable to UDP socket non-block sendto() on 0x%llx (0x%" PRIfixPTR
			                       " : %d) (SO_ERROR %d)"),
			          sock->id, sock, sockbase->fd, serr);
		}
		else {
			log_warnf(HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
			          STRING_CONST("Socket sendto() failed on UDP socket 0x%llx (0x%" PRIfixPTR
			                       " : %d): %s (%d) (SO_ERROR %d)"),
			          sock->id, sock, sockbase->fd, system_error_message(sockerr), sockerr, serr);
		}

		res = 0;
	}

	if (!sock->address_local)
		_socket_store_address_local(sock, address->family);

exit:

	if (sock)
		socket_destroy(id);

	return res;
}

