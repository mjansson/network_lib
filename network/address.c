/* address.c  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/rampantpixels/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/network.h>
#include <network/internal.h>

#include <foundation/foundation.h>

network_address_t*
network_address_clone(const network_address_t* address) {
	network_address_t* cloned = 0;
	if (address) {
		cloned = memory_allocate(HASH_NETWORK, sizeof(network_address_t) + address->address_size, 0,
		                         MEMORY_PERSISTENT);
		memcpy(cloned, address, sizeof(network_address_t) + address->address_size);
	}
	return cloned;
}

network_address_t**
network_address_resolve(const char* address, size_t length) {
	network_address_t** addresses = 0;
	string_t localaddress = (string_t) {0, 0};
	const char* final_address = address;
	size_t portdelim;
	int ret;
	struct addrinfo hints;
	struct addrinfo* result = 0;

	if (!address)
		return addresses;

	error_context_push(STRING_CONST("resolving network address"), address, length);

	//Special case - port only
	if (string_find_first_not_of(address, length, STRING_CONST("0123456789"), 0) == STRING_NPOS) {
		int port = string_to_int(address, length);
		if ((port > 0) && (port <= 65535)) {
			if (network_supports_ipv4()) {
				network_address_ipv4_t any;
				network_address_ipv4_initialize(&any);
				network_address_ip_set_port((network_address_t*)&any, (unsigned int)port);
				array_push(addresses, network_address_clone((network_address_t*)&any));
			}
			if (network_supports_ipv6()) {
				network_address_ipv6_t any;
				network_address_ipv6_initialize(&any);
				network_address_ip_set_port((network_address_t*)&any, (unsigned int)port);
				array_push(addresses, network_address_clone((network_address_t*)&any));
			}
			error_context_pop();

			return addresses;
		}
	}

	portdelim = string_rfind(address, length, ':', STRING_NPOS);

	if (string_find_first_not_of(address, length,
	                             STRING_CONST("[abcdefABCDEF0123456789.:]"), 0) == STRING_NPOS) {
		//ipv6 hex format has more than one :
		if ((portdelim != STRING_NPOS) && (string_find(address, length, ':', 0) != portdelim)) {
			//ipv6 hex format with port is of format [addr]:port
			if ((address[0] != '[') || (address[portdelim - 1] != ']'))
				portdelim = STRING_NPOS;
		}
	}

	if (portdelim != STRING_NPOS) {
		localaddress = string_clone(address, length);
		localaddress.str[portdelim] = 0;
		final_address = localaddress.str;

		if ((localaddress.str[0] == '[') && (localaddress.str[portdelim - 1] == ']')) {
			localaddress.str[portdelim - 1] = 0;
			final_address++;
			--portdelim;
		}
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM;

	ret = getaddrinfo(final_address, (portdelim != STRING_NPOS) ? final_address + portdelim + 1 : 0,
	                  &hints, &result);
	if (ret == 0) {
		struct addrinfo* curaddr;
		for (curaddr = result; curaddr; curaddr = curaddr->ai_next) {
			if (curaddr->ai_family == AF_INET) {
				network_address_ipv4_t* ipv4;
				ipv4 = memory_allocate(HASH_NETWORK, sizeof(network_address_ipv4_t), 0,
				                       MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
				ipv4->family = NETWORK_ADDRESSFAMILY_IPV4;
				ipv4->address_size = sizeof(struct sockaddr_in);
				memcpy(&ipv4->saddr, curaddr->ai_addr, sizeof(struct sockaddr_in));

				array_push(addresses, (network_address_t*)ipv4);
			}
			else if (curaddr->ai_family == AF_INET6) {
				network_address_ipv6_t* ipv6;
				ipv6 = memory_allocate(HASH_NETWORK, sizeof(network_address_ipv6_t), 0,
				                       MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
				ipv6->family = NETWORK_ADDRESSFAMILY_IPV6;
				ipv6->address_size = sizeof(struct sockaddr_in6);
				memcpy(&ipv6->saddr, curaddr->ai_addr, sizeof(struct sockaddr_in6));

				array_push(addresses, (network_address_t*)ipv6);
			}
		}
	}
	else {
		int err = NETWORK_RESOLV_ERROR;
		string_const_t errmsg = system_error_message(err);
		log_warnf(HASH_NETWORK, WARNING_INVALID_VALUE,
		          STRING_CONST("Unable to resolve network address '%.*s' (%s): %.*s (%d)"),
		          (int)length, address, final_address, STRING_FORMAT(errmsg), err);
	}

	string_deallocate(localaddress.str);

	error_context_pop();

	return addresses;
}

string_t
network_address_to_string(char* buffer, size_t capacity, const network_address_t* address,
                          bool numeric) {
	if (address) {
		if (address->family == NETWORK_ADDRESSFAMILY_IPV4) {
			char host[NI_MAXHOST] = {0};
			char service[NI_MAXSERV] = {0};
			const network_address_ipv4_t* addr_ipv4 = (const network_address_ipv4_t*)address;
			int ret = getnameinfo((const struct sockaddr*)&addr_ipv4->saddr, addr_ipv4->address_size, host,
			                      NI_MAXHOST, service, NI_MAXSERV,
			                      NI_NUMERICSERV | (numeric ? NI_NUMERICHOST : 0));
			if (ret == 0) {
				if (addr_ipv4->saddr.sin_port != 0)
					return string_format(buffer, capacity, STRING_CONST("%s:%s"), host, service);
				else
					return string_format(buffer, capacity, STRING_CONST("%s"), host);
			}
		}
		else if (address->family == NETWORK_ADDRESSFAMILY_IPV6) {
			char host[NI_MAXHOST] = {0};
			char service[NI_MAXSERV] = {0};
			const network_address_ipv6_t* addr_ipv6 = (const network_address_ipv6_t*)address;
			int ret = getnameinfo((const struct sockaddr*)&addr_ipv6->saddr, addr_ipv6->address_size, host,
			                      NI_MAXHOST, service, NI_MAXSERV,
			                      NI_NUMERICSERV | (numeric ? NI_NUMERICHOST : 0));
			if (ret == 0) {
				if (addr_ipv6->saddr.sin6_port != 0)
					return string_format(buffer, capacity, STRING_CONST("[%s]:%s"), host, service);
				else
					return string_format(buffer, capacity, STRING_CONST("%s"), host);
			}
		}
	}
	else {
		return string_copy(buffer, capacity, STRING_CONST("<null>"));
	}
	return string_copy(buffer, capacity, STRING_CONST("<invalid address>"));
}

network_address_t*
network_address_ipv4_initialize(network_address_ipv4_t* address) {
	memset(address, 0, sizeof(network_address_ipv4_t));
	address->saddr.sin_family = AF_INET;
#if FOUNDATION_PLATFORM_WINDOWS
	address->saddr.sin_addr.s_addr = INADDR_ANY;
#endif
#if FOUNDATION_PLATFORM_APPLE
	address->saddr.sin_len = sizeof(address->saddr);
#endif
	address->family = NETWORK_ADDRESSFAMILY_IPV4;
	address->address_size = sizeof(struct sockaddr_in);
	return (network_address_t*)address;
}

network_address_t*
network_address_ipv6_initialize(network_address_ipv6_t* address) {
	memset(address, 0, sizeof(network_address_ipv6_t));
	address->saddr.sin6_family = AF_INET6;
	address->saddr.sin6_addr = in6addr_any;
#if FOUNDATION_PLATFORM_APPLE
	address->saddr.sin6_len = sizeof(address->saddr);
#endif
	address->family = NETWORK_ADDRESSFAMILY_IPV6;
	address->address_size = sizeof(struct sockaddr_in6);
	return (network_address_t*)address;
}

void
network_address_ip_set_port(network_address_t* address, unsigned int port) {
	if (address->family == NETWORK_ADDRESSFAMILY_IPV4) {
		network_address_ipv4_t* addr_ipv4 = (network_address_ipv4_t*)address;
		addr_ipv4->saddr.sin_port = htons((unsigned short)port);
	}
	else if (address->family == NETWORK_ADDRESSFAMILY_IPV6) {
		network_address_ipv6_t* addr_ipv6 = (network_address_ipv6_t*)address;
		addr_ipv6->saddr.sin6_port = htons((unsigned short)port);
	}
}

unsigned int
network_address_ip_port(const network_address_t* address) {
	if (address->family == NETWORK_ADDRESSFAMILY_IPV4) {
		const network_address_ipv4_t* addr_ipv4 = (const network_address_ipv4_t*)address;
		return ntohs(addr_ipv4->saddr.sin_port);
	}
	else if (address->family == NETWORK_ADDRESSFAMILY_IPV6) {
		const network_address_ipv6_t* addr_ipv6 = (const network_address_ipv6_t*)address;
		return ntohs(addr_ipv6->saddr.sin6_port);
	}
	return 0;
}

void
network_address_ipv4_set_ip(network_address_t* address, uint32_t ip) {
	if (address && address->family == NETWORK_ADDRESSFAMILY_IPV4)
		((network_address_ipv4_t*)address)->saddr.sin_addr.s_addr = byteorder_bigendian32(ip);
}

uint32_t
network_address_ipv4_ip(const network_address_t* address) {
	if (address && address->family == NETWORK_ADDRESSFAMILY_IPV4) {
		const network_address_ipv4_t* address_ipv4 = (const network_address_ipv4_t*)address;
		return byteorder_bigendian32(address_ipv4->saddr.sin_addr.s_addr);
	}
	return 0;
}

uint32_t
network_address_ipv4_make_ip(unsigned char c0, unsigned char c1, unsigned char c2, unsigned char c3) {
	return (((uint32_t)c0) << 24U) | (((uint32_t)c1) << 16U) | (((uint32_t)c2) << 8U) | ((uint32_t)c3);
}

void
network_address_ipv6_set_ip(network_address_t* address, struct in6_addr ip) {
	if (address && address->family == NETWORK_ADDRESSFAMILY_IPV6)
		((network_address_ipv6_t*)address)->saddr.sin6_addr = ip;
}

NETWORK_API struct in6_add
network_address_ipv6_ip(const network_address_t* address) {
	if (address && address->family == NETWORK_ADDRESSFAMILY_IPV6) {
		const network_address_ipv6_t* address_ipv6 = (const network_address_ipv6_t*)address;
		return address_ipv6->saddr.sin6_addr;
	}
	struct in6_addr noaddr;
	memset(&noaddr, 0, sizeof(noaddr));
	return noaddr;
}

network_address_family_t
network_address_type(const network_address_t* address) {
	return address->family;
}

network_address_t**
network_address_local(void) {
	network_address_t** addresses = 0;

#if FOUNDATION_PLATFORM_WINDOWS
	PIP_ADAPTER_ADDRESSES adapter;
	IP_ADAPTER_ADDRESSES* adapter_address = 0;
	unsigned long address_size = 8000;
	int num_retries = 4;
	unsigned long ret = 0;
#endif

	error_context_push(STRING_CONST("getting local network addresses"), nullptr, 0);

#if FOUNDATION_PLATFORM_WINDOWS

	do {
		adapter_address = memory_allocate(HASH_NETWORK, (unsigned int)address_size, 0,
		                                  MEMORY_TEMPORARY | MEMORY_ZERO_INITIALIZED);

		ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST, 0,
		                           adapter_address, &address_size);
		if (ret == ERROR_BUFFER_OVERFLOW) {
			memory_deallocate(adapter_address);
			adapter_address = 0;
		}
		else {
			break;
		}
	}
	while (num_retries-- > 0);

	if (!adapter_address || (ret != NO_ERROR)) {
		string_const_t errmsg = system_error_message(ret);
		if (adapter_address)
			memory_deallocate(adapter_address);
		log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
		           STRING_CONST("Unable to get adapters addresses: %.*s (%d)"), STRING_FORMAT(errmsg), ret);
		error_context_pop();
		return 0;
	}

	for (adapter = adapter_address; adapter; adapter = adapter->Next) {
		IP_ADAPTER_UNICAST_ADDRESS* unicast;

		if (adapter->TunnelType == TUNNEL_TYPE_TEREDO)
			continue;

		if (adapter->OperStatus != IfOperStatusUp)
			continue;

		for (unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
			if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
				network_address_ipv4_t* ipv4 = memory_allocate(HASH_NETWORK, sizeof(network_address_ipv4_t), 0,
				                                               MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
				ipv4->family = NETWORK_ADDRESSFAMILY_IPV4;
				ipv4->address_size = sizeof(struct sockaddr_in);
				memcpy(&ipv4->saddr, unicast->Address.lpSockaddr, sizeof(struct sockaddr_in));

				array_push(addresses, (network_address_t*)ipv4);
			}
			else if (unicast->Address.lpSockaddr->sa_family == AF_INET6) {
				network_address_ipv6_t* ipv6 = memory_allocate(HASH_NETWORK, sizeof(network_address_ipv6_t), 0,
				                                               MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
				ipv6->family = NETWORK_ADDRESSFAMILY_IPV6;
				ipv6->address_size = sizeof(struct sockaddr_in6);
				memcpy(&ipv6->saddr, unicast->Address.lpSockaddr, sizeof(struct sockaddr_in6));

				array_push(addresses, (network_address_t*)ipv6);
			}
		}
	}

	memory_deallocate(adapter_address);

#elif FOUNDATION_PLATFORM_ANDROID

	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (!sock) {
		log_error(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
		          "Unable to get interface addresses: Unable to create socket");
		error_context_pop();
		return 0;
	}

	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(1234);
	sin.sin_addr.s_addr = 0x50505050;
	if (connect(sock, (struct sockaddr*)&sin, sizeof(struct sockaddr_in)) == 0) {
		socklen_t socklen = sizeof(struct sockaddr_in);
		if (getsockname(sock, (struct sockaddr*)&sin, &socklen) == 0) {
			network_address_ipv4_t* ipv4 = memory_allocate(HASH_NETWORK, sizeof(network_address_ipv4_t), 0,
			                                               MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
			ipv4->family = NETWORK_ADDRESSFAMILY_IPV4;
			ipv4->address_size = sizeof(struct sockaddr_in);
			ipv4->saddr.sin_family = AF_INET;
			memcpy(&ipv4->saddr, &sin, sizeof(struct sockaddr_in));

			array_push(addresses, (network_address_t*)ipv4);
		}
		else {
			log_error(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
			          "Unable to get interface addresses: Unable to get socket name");
			error_context_pop();
			return 0;
		}
	}
	else {
		log_error(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
		          "Unable to get interface addresses: Unable to connect socket");
		error_context_pop();
		return 0;
	}

	close(sock);

	log_info(HASH_TEST, "Got interface addresses");

#else

	struct ifaddrs* ifaddr = 0;
	struct ifaddrs* ifa = 0;

	if (getifaddrs(&ifaddr) < 0) {
		const string_const_t errmsg = system_error_message(NETWORK_RESOLV_ERROR);
		log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Unable to get interface addresses: %.*s"),
		           STRING_FORMAT(errmsg));
		error_context_pop();
		return 0;
	}

	for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr)
			continue;

		if (ifa->ifa_addr->sa_family == AF_INET) {
			network_address_ipv4_t* ipv4 = memory_allocate(HASH_NETWORK, sizeof(network_address_ipv4_t), 0,
			                                               MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
			ipv4->family = NETWORK_ADDRESSFAMILY_IPV4;
			ipv4->address_size = sizeof(struct sockaddr_in);
			memcpy(&ipv4->saddr, ifa->ifa_addr, sizeof(struct sockaddr_in));

			array_push(addresses, (network_address_t*)ipv4);
		}
		else if (ifa->ifa_addr->sa_family == AF_INET6) {
			network_address_ipv6_t* ipv6 = memory_allocate(HASH_NETWORK, sizeof(network_address_ipv6_t), 0,
			                                               MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
			ipv6->family = NETWORK_ADDRESSFAMILY_IPV6;
			ipv6->address_size = sizeof(struct sockaddr_in6);
			memcpy(&ipv6->saddr, ifa->ifa_addr, sizeof(struct sockaddr_in6));

			array_push(addresses, (network_address_t*)ipv6);
		}
	}

	freeifaddrs(ifaddr);

#endif

	error_context_pop();

	return addresses;
}

network_address_family_t
network_address_family(const network_address_t* address) {
	return address->family;
}

bool
network_address_equal(const network_address_t* first, const network_address_t* second) {
	if (first == second)
		return true;
	if (!first || !second || (first->address_size != second->address_size))
		return false;
	return memcmp(first, second, first->address_size) == 0;
}

void
network_address_deallocate(network_address_t* address) {
	memory_deallocate(address);
}

void
network_address_array_deallocate(network_address_t** addresses) {
	unsigned int iaddr, asize;
	for (iaddr = 0, asize = array_size(addresses); iaddr < asize; ++iaddr)
		network_address_deallocate(addresses[iaddr]);
	array_deallocate(addresses);
}
