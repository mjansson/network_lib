/* udp.c  -  Network library  -  Public Domain  -  2014 Mattias Jansson
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/mjansson/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/udp.h>
#include <network/address.h>
#include <network/internal.h>

#include <foundation/foundation.h>

static void
_udp_socket_open(socket_t*, unsigned int);

static void
_udp_stream_initialize(socket_t*, stream_t*);

socket_t*
udp_socket_allocate(void) {
	socket_t* sock = memory_allocate(HASH_NETWORK, sizeof(socket_t), 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
	udp_socket_initialize(sock);
	return sock;
}

void
udp_socket_initialize(socket_t* sock) {
	_socket_initialize(sock);

	sock->type = NETWORK_SOCKETTYPE_UDP;
	sock->open_fn = _udp_socket_open;
	sock->stream_initialize_fn = _udp_stream_initialize;
}

static void
_udp_socket_open(socket_t* sock, unsigned int family) {
	if (sock->fd != NETWORK_SOCKET_INVALID)
		return;

	sock->fd = (int)socket((family == NETWORK_ADDRESSFAMILY_IPV6) ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock->fd < 0) {
		int err = NETWORK_SOCKET_ERROR;
		string_const_t errmsg = system_error_message(err);
		log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
		           STRING_CONST("Unable to open UDP socket (0x%" PRIfixPTR " : %d): %.*s (%d)"), (uintptr_t)sock,
		           sock->fd, STRING_FORMAT(errmsg), err);
		sock->fd = NETWORK_SOCKET_INVALID;
	} else {
		log_debugf(HASH_NETWORK, STRING_CONST("Opened UDP socket (0x%" PRIfixPTR " : %d)"), (uintptr_t)sock, sock->fd);
	}
}

void
_udp_stream_initialize(socket_t* sock, stream_t* stream) {
	stream->inorder = 0;
	stream->reliable = 0;
	stream->path = string_allocate_format(STRING_CONST("udp://%" PRIfixPTR), (uintptr_t)sock);
}

size_t
udp_socket_recvfrom(socket_t* sock, void* buffer, size_t capacity, network_address_t const** address) {
	network_address_ip_t* addr_ip;
	long ret;

	if (address)
		*address = 0;

	if ((sock->fd == NETWORK_SOCKET_INVALID) || !sock->address_local)
		return 0;

	if (sock->state != SOCKETSTATE_NOTCONNECTED) {
		FOUNDATION_ASSERT_FAILFORMAT_LOG(
		    HASH_NETWORK, "Trying to datagram read from a connected UDP socket (0x%" PRIfixPTR " : %d) in state %u",
		    (uintptr_t)sock, sock->fd, sock->state);
		return 0;
	}

	if (!sock->address_remote || (sock->address_remote->family != sock->address_local->family)) {
		if (sock->address_remote)
			memory_deallocate(sock->address_remote);
		sock->address_remote = network_address_clone(sock->address_local);
	}
	addr_ip = (network_address_ip_t*)sock->address_remote;

	ret = recvfrom(sock->fd, (char*)buffer, (network_send_size_t)capacity, 0, &addr_ip->saddr, &addr_ip->address_size);
	if (ret > 0) {
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
		const unsigned char* src = (const unsigned char*)buffer;
		char dump_buffer[66];
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 0
		{
			char addr_buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
			string_t address_str =
			    network_address_to_string(addr_buffer, sizeof(addr_buffer), sock->address_remote, true);
			log_debugf(HASH_NETWORK,
			           STRING_CONST("Socket (0x%" PRIfixPTR " : %d) read %d of %" PRIsize " bytes from %.*s"),
			           (uintptr_t)sock, sock->fd, (int)ret, capacity, STRING_FORMAT(address_str));
		}
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
				log_debug(HASH_NETWORK, dump_buffer, (size_t)ofs - 1);
			}
		}
#endif

		if (address)
			*address = sock->address_remote;

		return (size_t)ret;
	}

	int sockerr = NETWORK_SOCKET_ERROR;

#if FOUNDATION_PLATFORM_WINDOWS
	int serr = 0;
	int slen = sizeof(int);
	getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, (char*)&serr, &slen);
#else
	int serr = 0;
	socklen_t slen = sizeof(int);
	getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, (void*)&serr, &slen);
#endif

#if FOUNDATION_PLATFORM_WINDOWS
	if (sockerr != WSAEWOULDBLOCK)
#else
	if (sockerr != EAGAIN)
#endif
	{
		string_const_t errmsg = system_error_message(sockerr);
		log_warnf(
		    HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
		    STRING_CONST("Socket recvfrom() failed on UDP socket (0x%" PRIfixPTR " : %d): %.*s (%d) (SO_ERROR %d)"),
		    (uintptr_t)sock, sock->fd, STRING_FORMAT(errmsg), sockerr, serr);
	}

	return 0;
}

size_t
udp_socket_sendto(socket_t* sock, const void* buffer, size_t size, const network_address_t* address) {
	const network_address_ip_t* addr_ip;
	long ret = 0;

	if (!address)
		return 0;

	if (sock->state != SOCKETSTATE_NOTCONNECTED) {
		FOUNDATION_ASSERT_FAILFORMAT_LOG(
		    HASH_NETWORK, "Trying to datagram send from a connected UDP socket (0x%" PRIfixPTR " : %d) in state %u",
		    (uintptr_t)sock, sock->fd, sock->state);
		return 0;
	}
	if (_socket_create_fd(sock, address->family) == NETWORK_SOCKET_INVALID) {
		FOUNDATION_ASSERT_FAILFORMAT_LOG(
		    HASH_NETWORK, "Trying to datagram send from an invalid UDP socket (0x%" PRIfixPTR " : %d) in state %u",
		    (uintptr_t)sock, sock->fd, sock->state);
		return 0;
	}
	addr_ip = (const network_address_ip_t*)address;

	ret = sendto(sock->fd, buffer, (network_send_size_t)size, 0, &addr_ip->saddr, addr_ip->address_size);
	if (ret > 0) {
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
		const unsigned char* src = (const unsigned char*)buffer;
		char dumpbuffer[34];
#endif
#if BUILD_ENABLE_LOG
		char addr_buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_t address_str = network_address_to_string(addr_buffer, sizeof(addr_buffer), address, true);
		if ((size_t)ret != size) {
			log_warnf(HASH_NETWORK, WARNING_SUSPICIOUS,
			          STRING_CONST("Socket (0x%" PRIfixPTR " : %d): partial UDP datagram write %d of %" PRIsize
			                       " bytes to %.*s"),
			          (uintptr_t)sock, sock->fd, (int)ret, size, STRING_FORMAT(address_str));
		} else {
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 0
			log_debugf(HASH_NETWORK,
			           STRING_CONST("Socket (0x%" PRIfixPTR " : %d) wrote %d of %" PRIsize " bytes to %.*s"),
			           (uintptr_t)sock, sock->fd, (int)ret, size, STRING_FORMAT(address_str));
#endif
		}
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
		for (long row = 0; row <= (ret / 8); ++row) {
			long ofs = 0, col = 0, cols = 8;
			if ((row + 1) * 8 > ret)
				cols = ret - (row * 8);
			for (; col < cols; ++col, ofs += 3) {
				string_format(dumpbuffer + ofs, 34 - (unsigned int)ofs, STRING_CONST("%02x"), *(src + (row * 8) + col));
				*(dumpbuffer + ofs + 2) = ' ';
			}
			if (ofs) {
				*(dumpbuffer + ofs - 1) = 0;
				log_debug(HASH_NETWORK, dumpbuffer, (size_t)ofs - 1);
			}
		}
#endif

		if (!sock->address_local)
			_socket_store_address_local(sock, (int)address->family);

		return (size_t)ret;
	}

	int sockerr = NETWORK_SOCKET_ERROR;

#if FOUNDATION_PLATFORM_WINDOWS
	int serr = 0;
	int slen = sizeof(int);
	getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, (char*)&serr, &slen);
#else
	int serr = 0;
	socklen_t slen = sizeof(int);
	getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, (void*)&serr, &slen);
#endif

#if FOUNDATION_PLATFORM_WINDOWS
	if (sockerr != WSAEWOULDBLOCK)
#else
	if (sockerr != EAGAIN)
#endif
	{
		string_const_t errmsg = system_error_message(sockerr);
		log_warnf(HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
		          STRING_CONST("Socket sendto() failed on UDP socket (0x%" PRIfixPTR " : %d): %.*s (%d) (SO_ERROR %d)"),
		          (uintptr_t)sock, sock->fd, STRING_FORMAT(errmsg), sockerr, serr);
	}

	return 0;
}
