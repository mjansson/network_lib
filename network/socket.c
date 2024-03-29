/* socket.c  -  Network library  -  Public Domain  -  2013 Mattias Jansson
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/mjansson/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/socket.h>
#include <network/address.h>
#include <network/internal.h>
#include <network/hashstrings.h>

#include <foundation/foundation.h>

static void
socket_set_blocking_fd(int fd, bool block);

void
socket_initialize(socket_t* sock) {
	memset(sock, 0, sizeof(socket_t));
	sock->fd = NETWORK_SOCKET_INVALID;
	sock->flags = 0;
	sock->state = SOCKETSTATE_NOTCONNECTED;
}

int
socket_create_fd(socket_t* sock, network_address_family_t family) {
	if (sock->fd != NETWORK_SOCKET_INVALID) {
		if (sock->family != family) {
			FOUNDATION_ASSERT_FAILFORMAT_LOG(
			    HASH_NETWORK, "Trying to switch family on existing socket (0x%" PRIfixPTR " : %d) from %u to %u",
			    (uintptr_t)sock, sock->fd, sock->family, family);
			return NETWORK_SOCKET_INVALID;
		}
		return sock->fd;
	}

	sock->open_fn(sock, family);
	if (sock->fd != NETWORK_SOCKET_INVALID) {
		sock->family = family;
		socket_set_blocking(sock, sock->flags & SOCKETFLAG_BLOCKING);
		socket_set_reuse_address(sock, sock->flags & SOCKETFLAG_REUSE_ADDR);
		socket_set_reuse_port(sock, sock->flags & SOCKETFLAG_REUSE_PORT);
	}

	return sock->fd;
}

bool
socket_create(socket_t* sock) {
	socket_create_fd(sock, sock->family);
	return (sock->fd >= 0);
}

void
socket_finalize(socket_t* sock) {
	log_debugf(HASH_NETWORK, STRING_CONST("Finalizing socket (0x%" PRIfixPTR " : %d)"), (uintptr_t)sock, sock->fd);
	if (sock->beacon)
		socket_set_beacon(sock, 0);
	socket_close(sock);
#if FOUNDATION_PLATFORM_WINDOWS
	if (sock->event)
		CloseHandle(sock->event);
#endif
}

void
socket_deallocate(socket_t* sock) {
	if (!sock)
		return;
	socket_finalize(sock);
	memory_deallocate(sock);
}

network_socket_type_t
socket_type(socket_t* sock) {
	return sock->type;
}

bool
socket_bind(socket_t* sock, const network_address_t* address) {
	bool success = false;
	const network_address_ip_t* address_ip;

	FOUNDATION_ASSERT(address);

	if (socket_create_fd(sock, address->family) == NETWORK_SOCKET_INVALID)
		return false;

	address_ip = (const network_address_ip_t*)address;
	if (bind(sock->fd, &address_ip->saddr, (socklen_t)address_ip->address_size) == 0) {
		// Store local address
		socket_store_address_local(sock, (int)address_ip->family);
		success = true;
#if BUILD_ENABLE_LOG
		{
			char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
			string_t address_str = network_address_to_string(buffer, sizeof(buffer), sock->address_local, true);
			log_infof(HASH_NETWORK, STRING_CONST("Bound socket (0x%" PRIfixPTR " : %d) to local address %.*s"),
			          (uintptr_t)sock, sock->fd, STRING_FORMAT(address_str));
		}
#endif
	} else {
#if BUILD_ENABLE_LOG
		char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		int sockerr = NETWORK_SOCKET_ERROR;
		string_const_t errmsg = system_error_message(sockerr);
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), address, true);
		log_warnf(HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
		          STRING_CONST("Unable to bind socket (0x%" PRIfixPTR " : %d) to local address %.*s: %.*s (%d)"),
		          (uintptr_t)sock, sock->fd, STRING_FORMAT(address_str), STRING_FORMAT(errmsg), sockerr);
#endif
	}

	if (success && sock->beacon)
		socket_set_beacon(sock, sock->beacon);

	return success;
}

#if FOUNDATION_COMPILER_CLANG
#if __has_warning("-Wreserved-identifier")
#pragma clang diagnostic ignored "-Wreserved-identifier"
#endif
#endif

static int
socket_connect_impl(socket_t* sock, const network_address_t* address, unsigned int timeoutms) {
	const network_address_ip_t* address_ip;
	bool blocking;
	bool failed = true;
	int err = 0;
#if BUILD_ENABLE_DEBUG_LOG
	string_const_t error_message = string_null();
#endif

	if (sock->fd == NETWORK_SOCKET_INVALID)
		return 0;

	blocking = ((sock->flags & SOCKETFLAG_BLOCKING) != 0);

	if ((timeoutms != NETWORK_TIMEOUT_INFINITE) && blocking)
		socket_set_blocking(sock, false);

	socket_set_state(sock, SOCKETSTATE_CONNECTING);

	address_ip = (const network_address_ip_t*)address;
	err = connect(sock->fd, &address_ip->saddr, (socklen_t)address_ip->address_size);
	if (!err) {
		failed = false;
		socket_set_state(sock, SOCKETSTATE_CONNECTED);
	} else {
		bool in_progress = false;
		err = NETWORK_SOCKET_ERROR;
#if FOUNDATION_PLATFORM_WINDOWS
		in_progress = (err == WSAEWOULDBLOCK);
#else  // elif FOUNDATION_PLATFORM_POSIX
		in_progress = (err == EINPROGRESS);
#endif
		if (in_progress) {
			if (!timeoutms) {
				failed = false;
			} else {
				struct timeval tv;
				fd_set fdwrite, fderr;
				int ret;

				FD_ZERO(&fdwrite);
				FD_ZERO(&fderr);
				FD_SET(sock->fd, &fdwrite);
				FD_SET(sock->fd, &fderr);

				tv.tv_sec = timeoutms / 1000;
				tv.tv_usec = (timeoutms % 1000) * 1000;

				ret = select((int)(sock->fd + 1), 0, &fdwrite, &fderr,
				             (timeoutms != NETWORK_TIMEOUT_INFINITE) ? &tv : nullptr);
				if (ret > 0) {
#if FOUNDATION_PLATFORM_WINDOWS
					int serr = 0;
					int slen = sizeof(int);
					getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, (char*)&serr, &slen);
#else
					int serr = 0;
					socklen_t slen = sizeof(int);
					getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, (void*)&serr, &slen);
#endif
					if (!serr && !FD_ISSET(sock->fd, &fderr)) {
						failed = false;
						if (FD_ISSET(sock->fd, &fdwrite))
							socket_set_state(sock, SOCKETSTATE_CONNECTED);
					} else {
						err = serr;
#if BUILD_ENABLE_DEBUG_LOG
						error_message = string_const(STRING_CONST("select indicated socket error"));
#endif
					}
				} else if (ret < 0) {
					err = NETWORK_SOCKET_ERROR;
#if BUILD_ENABLE_DEBUG_LOG
					error_message = string_const(STRING_CONST("select failed"));
#endif
				} else {
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

	if ((timeoutms != NETWORK_TIMEOUT_INFINITE) && blocking)
		socket_set_blocking(sock, true);

	if (failed) {
#if BUILD_ENABLE_DEBUG_LOG
		char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), address, true);
		log_debugf(HASH_NETWORK,
		           STRING_CONST("Failed to connect TCP/IP socket (0x%" PRIfixPTR " : %d) to remote host %.*s: %.*s"),
		           (uintptr_t)sock, sock->fd, STRING_FORMAT(address_str), STRING_FORMAT(error_message));
#endif
		socket_set_state(sock, SOCKETSTATE_NOTCONNECTED);
		return err;
	}

	memory_deallocate(sock->address_remote);
	sock->address_remote = network_address_clone(address);

	if (!sock->address_local)
		socket_store_address_local(sock, (int)address_ip->family);

#if BUILD_ENABLE_DEBUG_LOG
	{
		char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), address, true);
		log_debugf(HASH_NETWORK, STRING_CONST("%s socket (0x%" PRIfixPTR " : %d) to remote host %.*s"),
		           (sock->state == SOCKETSTATE_CONNECTING) ? "Connection pending for" : "Connected", (uintptr_t)sock,
		           sock->fd, STRING_FORMAT(address_str));
	}
#endif

	return 0;
}

bool
socket_connect(socket_t* sock, const network_address_t* address, unsigned int timeoutms) {
	int err;

	FOUNDATION_ASSERT(address);

	if (socket_create_fd(sock, address->family) == NETWORK_SOCKET_INVALID)
		return false;

	if (sock->state != SOCKETSTATE_NOTCONNECTED) {
#if BUILD_ENABLE_LOG
		char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), address, true);
		log_warnf(
		    HASH_NETWORK, WARNING_SUSPICIOUS,
		    STRING_CONST("Unable to connect already connected socket (0x%" PRIfixPTR " : %d) to remote address %.*s"),
		    (uintptr_t)sock, sock->fd, STRING_FORMAT(address_str));
#endif
		return false;
	}

	err = socket_connect_impl(sock, address, timeoutms);
	if (err) {
#if BUILD_ENABLE_LOG
		char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_const_t errmsg = system_error_message(err);
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), address, true);
		log_warnf(HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
		          STRING_CONST("Unable to connect socket (0x%" PRIfixPTR " : %d) to remote address %.*s: %.*s (%d)"),
		          (uintptr_t)sock, sock->fd, STRING_FORMAT(address_str), STRING_FORMAT(errmsg), err);
#endif
		return false;
	}

	memory_deallocate(sock->address_remote);
	sock->address_remote = network_address_clone(address);

	return true;
}

bool
socket_blocking(const socket_t* sock) {
	return ((sock->flags & SOCKETFLAG_BLOCKING) != 0);
}

void
socket_set_blocking(socket_t* sock, bool block) {
	sock->flags = (block ? sock->flags | SOCKETFLAG_BLOCKING : sock->flags & ~SOCKETFLAG_BLOCKING);
	if (sock->fd != NETWORK_SOCKET_INVALID)
		socket_set_blocking_fd(sock->fd, block);
}

bool
socket_reuse_address(const socket_t* sock) {
	return ((sock->flags & SOCKETFLAG_REUSE_ADDR) != 0);
}

void
socket_set_reuse_address(socket_t* sock, bool reuse) {
	sock->flags = (reuse ? sock->flags | SOCKETFLAG_REUSE_ADDR : sock->flags & ~SOCKETFLAG_REUSE_ADDR);
	if (sock->fd != NETWORK_SOCKET_INVALID) {
#if FOUNDATION_PLATFORM_WINDOWS
		BOOL optval = reuse ? 1 : 0;
		int ret = setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));
#else
		int optval = reuse ? 1 : 0;
		int ret = setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#endif
		if (ret < 0) {
			const int sockerr = NETWORK_SOCKET_ERROR;
			const string_const_t errmsg = system_error_message(sockerr);
			log_warnf(HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
			          STRING_CONST("Unable to set reuse address option on socket (0x%" PRIfixPTR " : %d): %.*s (%d)"),
			          (uintptr_t)sock, sock->fd, STRING_FORMAT(errmsg), sockerr);
			FOUNDATION_UNUSED(sockerr);
		}
	}
}

bool
socket_reuse_port(const socket_t* sock) {
	return ((sock->flags & SOCKETFLAG_REUSE_PORT) != 0);
}

void
socket_set_reuse_port(socket_t* sock, bool reuse) {
	sock->flags = (reuse ? sock->flags | SOCKETFLAG_REUSE_PORT : sock->flags & ~SOCKETFLAG_REUSE_PORT);
#ifdef SO_REUSEPORT
	if (sock->fd != NETWORK_SOCKET_INVALID) {
#if !FOUNDATION_PLATFORM_WINDOWS
		int optval = reuse ? 1 : 0;
		int ret = setsockopt(sock->fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
		if (ret < 0) {
			const int sockerr = NETWORK_SOCKET_ERROR;
			const string_const_t errmsg = system_error_message(sockerr);
			log_warnf(HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
			          STRING_CONST("Unable to set reuse port option on socket (0x%" PRIfixPTR " : %d): %.*s (%d)"),
			          (uintptr_t)sock, sock->fd, STRING_FORMAT(errmsg), sockerr);
			FOUNDATION_UNUSED(sockerr);
		}
#endif
	}
#endif
}

bool
socket_set_multicast_group(socket_t* sock, const network_address_t* multicast_address,
                           const network_address_t* local_address, bool allow_loopback) {
	unsigned char ttl = 1;
	unsigned char loopback = (allow_loopback ? 1 : 0);

	if ((sock->fd == NETWORK_SOCKET_INVALID) || !multicast_address)
		return false;

	// TODO: TTL 1 means local network, should be split out to separate control function
	setsockopt(sock->fd, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
	setsockopt(sock->fd, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loopback, sizeof(loopback));

	if (sock->family == NETWORK_ADDRESSFAMILY_IPV6) {
		if ((multicast_address->family != NETWORK_ADDRESSFAMILY_IPV6) ||
		    (local_address && (local_address->family != NETWORK_ADDRESSFAMILY_IPV6))) {
			log_errorf(
			    HASH_NETWORK, ERROR_INVALID_VALUE,
			    STRING_CONST("Add multicast group failed on socket (0x%" PRIfixPTR " : %d): Invalid address family"),
			    (uintptr_t)sock, sock->fd);
			return false;
		}

		struct ipv6_mreq req;
		memset(&req, 0, sizeof(req));
		req.ipv6mr_multiaddr = ((const network_address_ipv6_t*)multicast_address)->saddr.sin6_addr;
		if (setsockopt(sock->fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, (char*)&req, sizeof(req))) {
			const int sockerr = NETWORK_SOCKET_ERROR;
			const string_const_t errmsg = system_error_message(sockerr);
			log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
			           STRING_CONST("Add multicast group failed on socket (0x%" PRIfixPTR " : %d): %.*s (%d)"),
			           (uintptr_t)sock, sock->fd, STRING_FORMAT(errmsg), sockerr);
			FOUNDATION_UNUSED(sockerr);
			return false;
		}
		if (local_address) {
			unsigned int ifindex = 0;
			if (setsockopt(sock->fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, (const char*)&ifindex, sizeof(ifindex)) != 0) {
				const int sockerr = NETWORK_SOCKET_ERROR;
				const string_const_t errmsg = system_error_message(sockerr);
				log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
				           STRING_CONST("Set multicast interface failed on socket (0x%" PRIfixPTR " : %d): %.*s (%d)"),
				           (uintptr_t)sock, sock->fd, STRING_FORMAT(errmsg), sockerr);
				FOUNDATION_UNUSED(sockerr);
				return false;
			}
		}
	} else if (sock->family == NETWORK_ADDRESSFAMILY_IPV4) {
		if ((multicast_address->family != NETWORK_ADDRESSFAMILY_IPV4) ||
		    (local_address && (local_address->family != NETWORK_ADDRESSFAMILY_IPV4))) {
			log_errorf(
			    HASH_NETWORK, ERROR_INVALID_VALUE,
			    STRING_CONST("Add multicast group failed on socket (0x%" PRIfixPTR " : %d): Invalid address family"),
			    (uintptr_t)sock, sock->fd);
			return false;
		}

		struct ip_mreq req;
		memset(&req, 0, sizeof(req));
		req.imr_multiaddr.s_addr = ((const network_address_ipv4_t*)multicast_address)->saddr.sin_addr.s_addr;
		if (local_address)
			req.imr_interface.s_addr = ((const network_address_ipv4_t*)local_address)->saddr.sin_addr.s_addr;
		if (setsockopt(sock->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&req, sizeof(req)) != 0) {
			const int sockerr = NETWORK_SOCKET_ERROR;
			const string_const_t errmsg = system_error_message(sockerr);
			log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
			           STRING_CONST("Add multicast group failed on socket (0x%" PRIfixPTR " : %d): %.*s (%d)"),
			           (uintptr_t)sock, sock->fd, STRING_FORMAT(errmsg), sockerr);
			FOUNDATION_UNUSED(sockerr);
			return false;
		}
		if (local_address && (((const network_address_ipv4_t*)local_address)->saddr.sin_addr.s_addr != INADDR_ANY)) {
			if (setsockopt(sock->fd, IPPROTO_IP, IP_MULTICAST_IF,
			               (const char*)&((const network_address_ipv4_t*)local_address)->saddr.sin_addr,
			               sizeof(((const network_address_ipv4_t*)local_address)->saddr.sin_addr)) != 0) {
				const int sockerr = NETWORK_SOCKET_ERROR;
				const string_const_t errmsg = system_error_message(sockerr);
				log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
				           STRING_CONST("Set multicast interface failed on socket (0x%" PRIfixPTR " : %d): %.*s (%d)"),
				           (uintptr_t)sock, sock->fd, STRING_FORMAT(errmsg), sockerr);
				FOUNDATION_UNUSED(sockerr);
				return false;
			}
		}
	} else {
		log_errorf(HASH_NETWORK, ERROR_INVALID_VALUE,
		           STRING_CONST("Add multicast group failed on socket (0x%" PRIfixPTR " : %d): Unknown socket family"),
		           (uintptr_t)sock, sock->fd);
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
	return (sock->fd != NETWORK_SOCKET_INVALID) ? sock->state : SOCKETSTATE_NOTCONNECTED;
}

socket_state_t
socket_poll_state(socket_t* sock) {
	struct timeval tv;
	fd_set fdwrite, fderr;
	int available;

	if ((sock->state == SOCKETSTATE_NOTCONNECTED) || (sock->state == SOCKETSTATE_DISCONNECTED))
		return sock->state;

	switch (sock->state) {
		case SOCKETSTATE_CONNECTING:
			FD_ZERO(&fdwrite);
			FD_ZERO(&fderr);
			FD_SET(sock->fd, &fdwrite);
			FD_SET(sock->fd, &fderr);

			tv.tv_sec = 0;
			tv.tv_usec = 0;

			select((int)(sock->fd + 1), 0, &fdwrite, &fderr, &tv);

			if (FD_ISSET(sock->fd, &fderr)) {
				log_debugf(HASH_NETWORK, STRING_CONST("Socket (0x%" PRIfixPTR " : %d): error in state CONNECTING"),
				           (uintptr_t)sock, sock->fd);
				socket_close(sock);
			} else if (FD_ISSET(sock->fd, &fdwrite)) {
#if BUILD_ENABLE_DEBUG_LOG
				log_debugf(HASH_NETWORK, STRING_CONST("Socket (0x%" PRIfixPTR " : %d): CONNECTING -> CONNECTED"),
				           (uintptr_t)sock, sock->fd);
#endif
				socket_set_state(sock, SOCKETSTATE_CONNECTED);
			}
			break;

		case SOCKETSTATE_CONNECTED:
			available = socket_available_fd(sock->fd);
			if (available < 0) {
#if BUILD_ENABLE_DEBUG_LOG
				log_debugf(HASH_NETWORK, STRING_CONST("Socket (0x%" PRIfixPTR " : %d): hangup in CONNECTED"),
				           (uintptr_t)sock, sock->fd);
#endif
				socket_set_state(sock, SOCKETSTATE_DISCONNECTED);
				// Fall through to disconnected check for close
			} else
				break;

		case SOCKETSTATE_DISCONNECTED:
			if (!socket_available_read(sock)) {
				log_debugf(HASH_NETWORK, STRING_CONST("Socket (0x%" PRIfixPTR " : %d): all data read in DISCONNECTED"),
				           (uintptr_t)sock, sock->fd);
				socket_close(sock);
			}
			break;

		default:
			break;
	}

	return sock->state;
}

int
socket_fd(socket_t* sock) {
	return sock->fd;
}

size_t
socket_available_read(const socket_t* sock) {
	return (sock->fd != NETWORK_SOCKET_INVALID) ? (unsigned int)socket_available_fd(sock->fd) : 0;
}

size_t
socket_read(socket_t* sock, void* buffer, size_t size) {
	size_t read;
	long ret;

	if ((sock->fd == NETWORK_SOCKET_INVALID) || !size)
		return 0;

	ret = recv(sock->fd, (char*)buffer, (network_send_size_t)size, 0);
	if (ret > 0) {
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
		const unsigned char* src = (const unsigned char*)buffer;
		char dump_buffer[66];
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 0
		log_debugf(HASH_NETWORK, STRING_CONST("Socket (0x%" PRIfixPTR " : %d) read %d of %" PRIsize " bytes"),
		           (uintptr_t)sock, sock->fd, (int)ret, size);
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

		read = (size_t)ret;
		sock->bytes_read += read;

		return read;
	}

	if (ret == 0) {
#if BUILD_ENABLE_DEBUG_LOG
		char addrbuffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
		string_t address_str = network_address_to_string(addrbuffer, sizeof(addrbuffer), sock->address_remote, true);
		log_debugf(HASH_NETWORK, STRING_CONST("Socket closed gracefully on remote end (0x%" PRIfixPTR " : %d): %.*s"),
		           (uintptr_t)sock, sock->fd, STRING_FORMAT(address_str));
#endif
		socket_close(sock);
	} else {
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
			          (uintptr_t)sock, sock->fd, STRING_FORMAT(errmsg), sockerr);
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

		socket_poll_state(sock);
	}

	return 0;
}

size_t
socket_write(socket_t* sock, const void* buffer, size_t size) {
	size_t total_write = 0;

	if ((sock->fd == NETWORK_SOCKET_INVALID) || !size)
		return 0;

	while (total_write < size) {
		const char* current = (const char*)pointer_offset_const(buffer, total_write);
		size_t remain = size - total_write;

		long res = send(sock->fd, current, (network_send_size_t)remain, 0);
		if (res > 0) {
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
			const unsigned char* src = (const unsigned char*)current;
			char dumpbuffer[34];
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 0
			log_debugf(
			    HASH_NETWORK,
			    STRING_CONST("Socket (0x%" PRIfixPTR " : %d) wrote %d of %" PRIsize " bytes (offset %" PRIsize ")"),
			    (uintptr_t)sock, sock->fd, (int)res, remain, total_write);
#endif
#if BUILD_ENABLE_NETWORK_DUMP_TRAFFIC > 1
			for (long row = 0; row <= (res / 8); ++row) {
				long ofs = 0, col = 0, cols = 8;
				if ((row + 1) * 8 > res)
					cols = res - (row * 8);
				for (; col < cols; ++col, ofs += 3) {
					string_format(dumpbuffer + ofs, 34 - (unsigned int)ofs, STRING_CONST("%02x"),
					              *(src + (row * 8) + col));
					*(dumpbuffer + ofs + 2) = ' ';
				}
				if (ofs) {
					*(dumpbuffer + ofs - 1) = 0;
					log_debug(HASH_NETWORK, dumpbuffer, (size_t)ofs - 1);
				}
			}
#endif
			total_write += (unsigned long)res;
		} else if (res <= 0) {
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
			if (sockerr == WSAEWOULDBLOCK)
#else
			if (sockerr == EAGAIN)
#endif
			{
				if (serr) {
					log_warnf(HASH_NETWORK, WARNING_SUSPICIOUS,
					          STRING_CONST("Partial socket send() on (0x%" PRIfixPTR " : %d): %" PRIsize " of %" PRIsize
					                       " bytes written to socket (SO_ERROR %d)"),
					          (uintptr_t)sock, sock->fd, total_write, size, serr);
				}
			} else {
				const string_const_t errstr = system_error_message(sockerr);
				log_warnf(
				    HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL,
				    STRING_CONST("Socket send() failed on socket (0x%" PRIfixPTR " : %d): %.*s (%d) (SO_ERROR %d)"),
				    (uintptr_t)sock, sock->fd, STRING_FORMAT(errstr), sockerr, serr);
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

			if (sock->state != SOCKETSTATE_NOTCONNECTED)
				socket_poll_state(sock);

			break;
		}
	}

	sock->bytes_written += total_write;

	return total_write;
}

// Returns -1 if nothing available and socket closed, 0 if nothing available but still open, >0 if data available
int
socket_available_fd(int fd) {
	bool closed = false;
	int available = 0;

	if (fd == NETWORK_SOCKET_INVALID)
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
#error Not implemented
#endif

	return (!available && closed) ? -1 : available;
}

void
socket_close(socket_t* sock) {
	int fd = NETWORK_SOCKET_INVALID;
	network_address_t* local_address = sock->address_local;
	network_address_t* remote_address = sock->address_remote;

	if (sock->fd != NETWORK_SOCKET_INVALID) {
		fd = sock->fd;
		sock->fd = NETWORK_SOCKET_INVALID;
		sock->family = 0;
		socket_set_state(sock, SOCKETSTATE_NOTCONNECTED);
	}

	sock->address_local = nullptr;
	sock->address_remote = nullptr;

	if (fd != NETWORK_SOCKET_INVALID) {
		log_debugf(HASH_NETWORK, STRING_CONST("Closing socket (0x%" PRIfixPTR " : %d)"), (uintptr_t)sock, fd);
		socket_set_blocking_fd(fd, false);
		socket_close_fd(fd);
	}

	if (local_address)
		memory_deallocate(local_address);
	if (remote_address)
		memory_deallocate(remote_address);
}

void
socket_close_fd(int fd) {
#if FOUNDATION_PLATFORM_WINDOWS
	shutdown(fd, SD_BOTH);
	closesocket(fd);
#elif FOUNDATION_PLATFORM_POSIX
	shutdown(fd, SHUT_RDWR);
	close(fd);
#else
#error Not implemented
#endif
}

void
socket_set_blocking_fd(int fd, bool block) {
#if FOUNDATION_PLATFORM_WINDOWS
	unsigned long param = block ? 0 : 1;
	ioctlsocket(fd, FIONBIO, &param);
#elif FOUNDATION_PLATFORM_POSIX
	const int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, block ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
#else
#error Not implemented
#endif
}

void
socket_store_address_local(socket_t* sock, int family) {
	network_address_ip_t* address_local = 0;

	FOUNDATION_ASSERT(sock);
	if (sock->fd == NETWORK_SOCKET_INVALID)
		return;

	if (family == NETWORK_ADDRESSFAMILY_IPV4) {
		address_local = memory_allocate(HASH_NETWORK, sizeof(network_address_ipv4_t), 0,
		                                MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
		address_local->family = NETWORK_ADDRESSFAMILY_IPV4;
		address_local->address_size = sizeof(struct sockaddr_in);
	} else if (family == NETWORK_ADDRESSFAMILY_IPV6) {
		address_local = memory_allocate(HASH_NETWORK, sizeof(network_address_ipv6_t), 0,
		                                MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
		address_local->family = NETWORK_ADDRESSFAMILY_IPV6;
		address_local->address_size = sizeof(struct sockaddr_in6);
	} else {
		FOUNDATION_ASSERT_FAILFORMAT_LOG(HASH_NETWORK,
		                                 "Unable to get local address for socket (0x%" PRIfixPTR
		                                 " : %d): Unsupported address family %u",
		                                 (uintptr_t)sock, sock->fd, family);
		return;
	}
	getsockname(sock->fd, &address_local->saddr, (socklen_t*)&address_local->address_size);
	memory_deallocate(sock->address_local);
	sock->address_local = (network_address_t*)address_local;
}

void
socket_set_beacon(socket_t* sock, beacon_t* beacon) {
#if FOUNDATION_PLATFORM_WINDOWS
	if (sock->event && sock->beacon)
		beacon_remove_handle(sock->beacon, sock->event);
	if (!sock->event)
		sock->event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
	sock->beacon = beacon;
	if (sock->beacon) {
		if (sock->state == SOCKETSTATE_LISTENING) {
			WSAEventSelect(sock->fd, sock->event, FD_ACCEPT);
			beacon_add_handle(beacon, sock->event);
		} else if ((sock->type == NETWORK_SOCKETTYPE_UDP) || (sock->state == SOCKETSTATE_CONNECTED)) {
			WSAEventSelect(sock->fd, sock->event, FD_READ | FD_CLOSE);
			beacon_add_handle(beacon, sock->event);
		} else if ((sock->type == NETWORK_SOCKETTYPE_TCP) && (sock->state == SOCKETSTATE_CONNECTING)) {
			WSAEventSelect(sock->fd, sock->event, FD_WRITE | FD_CLOSE);
			beacon_add_handle(beacon, sock->event);
		}
	}
#else
	if (sock->beacon && (sock->fd != NETWORK_SOCKET_INVALID))
		beacon_remove_fd(sock->beacon, sock->fd);
	sock->beacon = beacon;
	if (sock->beacon && (sock->fd != NETWORK_SOCKET_INVALID))
		beacon_add_fd(sock->beacon, sock->fd);
#endif
}

void
socket_set_state(socket_t* sock, socket_state_t state) {
	sock->state = state;
	if (sock->beacon)
		socket_set_beacon(sock, sock->beacon);
}
