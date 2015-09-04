/* main.c  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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
#include <foundation/foundation.h>
#include <test/test.h>

application_t
test_address_application(void) {
	application_t app;
	memset(&app, 0, sizeof(app));
	app.name = string_const(STRING_CONST("Network address tests"));
	app.short_name = string_const(STRING_CONST("test_address"));
	app.config_dir = string_const(STRING_CONST("test_address"));
	app.flags = APPLICATION_UTILITY;
	return app;
}

memory_system_t
test_address_memory_system(void) {
	return memory_system_malloc();
}

foundation_config_t 
test_address_foundation_config(void) {
	foundation_config_t config;
	memset(&config, 0, sizeof(config));
	return config;
}

int
test_address_initialize(void) {
	log_set_suppress(HASH_NETWORK, ERRORLEVEL_INFO);
	return network_initialize(32);
}

void
test_address_finalize(void) {
	network_finalize();
}

DECLARE_TEST(address, local) {
	bool found_localhost_ipv4 = false;
	bool found_localhost_ipv6 = false;
	unsigned int iaddr, iother, addrsize;
	bool has_ipv4 = network_supports_ipv4();
	bool has_ipv6 = network_supports_ipv6();
	network_address_t** addresses = network_address_local();
	unsigned int expected_addresses = (has_ipv4 ? 1 : 0) + (has_ipv6 ? 1 : 0);
	char buffer[512];
	string_t hostname;

	EXPECT_GE(array_size(addresses), expected_addresses);

	hostname = system_hostname(buffer, sizeof(buffer));
	log_debugf(HASH_NETWORK, STRING_CONST("%u local addresses (%*s)"), array_size(addresses),
	           STRING_FORMAT(hostname));
	for (iaddr = 0, addrsize = array_size(addresses); iaddr < addrsize; ++iaddr) {
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), addresses[iaddr], true);
		log_debugf(HASH_NETWORK, "  %*s", STRING_FORMAT(address_str));

		if (string_equal(STRING_ARGS(address_str), STRING_CONST("127.0.0.1")))
			found_localhost_ipv4 = true;
		if (string_equal(STRING_ARGS(address_str), STRING_CONST("::1")))
			found_localhost_ipv6 = true;

		memory_deallocate(addresses[iaddr]);

		for (iother = iaddr + 1; iother < addrsize; ++iother) {
			bool addr_equal = network_address_equal(addresses[iaddr], addresses[iother]);
			EXPECT_FALSE(addr_equal);
		}
	}
	array_deallocate(addresses);

	EXPECT_TRUE(!has_ipv4 || found_localhost_ipv4);
	EXPECT_TRUE(!has_ipv6 || found_localhost_ipv6);

	return 0;
}

DECLARE_TEST(address, resolve) {
	unsigned int iaddr, addrsize;
	char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
	bool has_ipv4 = network_supports_ipv4();
	bool has_ipv6 = network_supports_ipv6();
	unsigned int expected_addresses = (has_ipv4 ? 1 : 0) + (has_ipv6 ? 1 : 0);

	network_address_t** addresses = network_address_resolve(STRING_CONST("localhost"));
	log_debugf(HASH_NETWORK, STRING_CONST("localhost -> %u addresses"), array_size(addresses));
	EXPECT_EQ(array_size(addresses), expected_addresses);
	for (iaddr = 0, addrsize = array_size(addresses); iaddr < addrsize; ++iaddr) {
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), addresses[iaddr], true);
		log_debugf(HASH_NETWORK, "  %*s", STRING_FORMAT(address_str));
		EXPECT_TRUE(string_equal(STRING_ARGS(address_str), STRING_CONST("127.0.0.1")) ||
		            string_equal(STRING_ARGS(address_str), STRING_CONST("::1")) ||
		            string_match_pattern(STRING_ARGS(address_str), STRING_CONST("fe80:*")));
	}
	network_address_array_deallocate(addresses);

	addresses = network_address_resolve(STRING_CONST("localhost:80"));
	log_debugf(HASH_NETWORK, STRING_CONST("localhost:80 -> %u addresses"), array_size(addresses));
	EXPECT_EQ(array_size(addresses), expected_addresses);
	for (iaddr = 0, addrsize = array_size(addresses); iaddr < addrsize; ++iaddr) {
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), addresses[iaddr], true);
		log_debugf(HASH_NETWORK, STRING_CONST("  %s"), STRING_FORMAT(address_str));
		EXPECT_TRUE(string_equal(STRING_ARGS(address_str), STRING_CONST("127.0.0.1:80")) ||
		            string_equal(STRING_ARGS(address_str), STRING_CONST("[::1]:80")) ||
		            string_match_pattern(STRING_ARGS(address_str), STRING_CONST("[fe80:*]:80")));
	}
	network_address_array_deallocate(addresses);

	if (has_ipv4) {
		addresses = network_address_resolve(STRING_CONST("127.0.0.1"));
		log_debugf(HASH_NETWORK, STRING_CONST("127.0.0.1 -> %u addresses"), array_size(addresses));
		EXPECT_EQ(array_size(addresses), 1);
		for (iaddr = 0, addrsize = array_size(addresses); iaddr < addrsize; ++iaddr) {
			string_t address_str = network_address_to_string(buffer, sizeof(buffer), addresses[iaddr], true);
			log_debugf(HASH_NETWORK, STRING_CONST("  %*s"), STRING_FORMAT(address_str));
			EXPECT_TRUE(string_equal(STRING_ARGS(address_str), STRING_CONST("127.0.0.1")));
		}
		network_address_array_deallocate(addresses);
	}

	if (has_ipv6) {
		addresses = network_address_resolve(STRING_CONST("::1"));
		log_debugf(HASH_NETWORK, STRING_CONST("::1 -> %u addresses"), array_size(addresses));
		EXPECT_EQ(array_size(addresses), 1);
		for (iaddr = 0, addrsize = array_size(addresses); iaddr < addrsize; ++iaddr) {
			string_t address_str = network_address_to_string(buffer, sizeof(buffer), addresses[iaddr], true);
			log_debugf(HASH_NETWORK, STRING_CONST("  %*s"), STRING_FORMAT(address_str));
			EXPECT_TRUE(string_equal(STRING_ARGS(address_str), STRING_CONST("::1")));
		}
		network_address_array_deallocate(addresses);
	}

	if (has_ipv4) {
		addresses = network_address_resolve(STRING_CONST("127.0.0.1:512"));
		log_debugf(HASH_NETWORK, "127.0.0.1:512 -> %u addresses", array_size(addresses));
		EXPECT_EQ(array_size(addresses), 1);
		for (iaddr = 0, addrsize = array_size(addresses); iaddr < addrsize; ++iaddr) {
			string_t address_str = network_address_to_string(buffer, sizeof(buffer), addresses[iaddr], true);
			log_debugf(HASH_NETWORK, STRING_CONST("  %*s"), STRING_FORMAT(address_str));
			EXPECT_TRUE(string_equal(STRING_ARGS(address_str), STRING_CONST("127.0.0.1:512")));
		}
		network_address_array_deallocate(addresses);
	}

	if (has_ipv6) {
		addresses = network_address_resolve(STRING_CONST("[::1]:512"));
		log_debugf(HASH_NETWORK, STRING_CONST("[::1]:512 -> %u addresses"), array_size(addresses));
		EXPECT_EQ(array_size(addresses), 1);
		for (iaddr = 0, addrsize = array_size(addresses); iaddr < addrsize; ++iaddr) {
			string_t address_str = network_address_to_string(buffer, sizeof(buffer), addresses[iaddr], true);
			log_debugf(HASH_NETWORK, STRING_CONST("  %*s"), STRING_FORMAT(address_str));
			EXPECT_TRUE(string_equal(STRING_ARGS(address_str), STRING_CONST("[::1]:512")));
		}
		network_address_array_deallocate(addresses);
	}

	addresses = network_address_resolve(STRING_CONST("zion.rampantpixels.com:1234"));
	log_debugf(HASH_NETWORK, STRING_CONST("zion.rampantpixels.com:1234 -> %u addresses"), array_size(addresses));
	EXPECT_EQ(array_size(addresses), expected_addresses);
	for (iaddr = 0, addrsize = array_size(addresses); iaddr < addrsize; ++iaddr) {
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), addresses[iaddr], true);
		log_debugf(HASH_NETWORK, STRING_CONST("  %*s"), STRING_FORMAT(address_str));
	}
	network_address_array_deallocate(addresses);

	addresses = network_address_resolve(STRING_CONST("www.google.com"));
	log_debugf(HASH_NETWORK, STRING_CONST("www.google.com -> %u addresses"), array_size(addresses));
	EXPECT_GE(array_size(addresses), expected_addresses);
	for (iaddr = 0, addrsize = array_size(addresses); iaddr < addrsize; ++iaddr) {
		string_t address_str = network_address_to_string(buffer, sizeof(buffer), addresses[iaddr], true);
		log_debugf(HASH_NETWORK, STRING_CONST("  %*s"), STRING_FORMAT(address_str));
	}
	network_address_array_deallocate(addresses);

	return 0;
}

DECLARE_TEST(address, any) {
	string_t address_str;
	char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
	bool has_ipv4 = network_supports_ipv4();
	bool has_ipv6 = network_supports_ipv6();
	network_address_t** any_resolve;

	if (has_ipv4) {
		network_address_t* any = network_address_ipv4_any();
		EXPECT_NE(any, 0);
		address_str = network_address_to_string(buffer, sizeof(buffer), any, true);
		log_debugf(HASH_NETWORK, STRING_CONST("IPv4 any: %*s"), STRING_FORMAT(address_str));
		EXPECT_STRINGEQ(address_str, string_const(STRING_CONST("0.0.0.0")));

		any_resolve = network_address_resolve(STRING_ARGS(address_str));
		EXPECT_EQ(array_size(any_resolve), 1);
		EXPECT_TRUE(network_address_equal(any, any_resolve[0]));

		network_address_array_deallocate(any_resolve);
		memory_deallocate(any);
	}

	if (has_ipv6) {
		network_address_t* any = network_address_ipv6_any();
		EXPECT_NE(any, 0);
		address_str = network_address_to_string(buffer, sizeof(buffer), any, true);
		log_debugf(HASH_NETWORK, STRING_CONST("IPv6 any: %*s"), STRING_FORMAT(address_str));
		EXPECT_STRINGEQ(address_str, string_const(STRING_CONST("::")));

		any_resolve = network_address_resolve(STRING_ARGS(address_str));
		EXPECT_EQ(array_size(any_resolve), 1);
		EXPECT_TRUE(network_address_equal(any, any_resolve[0]));

		network_address_array_deallocate(any_resolve);
		memory_deallocate(any);
	}

	return 0;
}

DECLARE_TEST(address, port) {
	network_address_t** any_resolve;
	char buffer[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
	bool has_ipv4 = network_supports_ipv4();
	bool has_ipv6 = network_supports_ipv6();
	string_t address_str;

	if (has_ipv4) {
		network_address_t* any = network_address_ipv4_any();
		EXPECT_EQ(network_address_ip_port(any), 0);

		network_address_ip_set_port(any, 80);
		EXPECT_EQ(network_address_ip_port(any), 80);

		address_str = network_address_to_string(buffer, sizeof(buffer), any, true);
		log_debugf(HASH_NETWORK, STRING_CONST("IPv4 any: %*s"), STRING_FORMAT(address_str));
		EXPECT_STRINGEQ(address_str, string_const(STRING_CONST("0.0.0.0:80")));

		any_resolve = network_address_resolve(STRING_ARGS(address_str));
		EXPECT_EQ(array_size(any_resolve), 1);
		EXPECT_TRUE(network_address_equal(any, any_resolve[0]));

		network_address_array_deallocate(any_resolve);
		memory_deallocate(any);
	}

	if (has_ipv6) {
		network_address_t* any = network_address_ipv6_any();
		EXPECT_EQ(network_address_ip_port(any), 0);

		network_address_ip_set_port(any, 80);
		EXPECT_EQ(network_address_ip_port(any), 80);

		address_str = network_address_to_string(buffer, sizeof(buffer), any, true);
		log_debugf(HASH_NETWORK, STRING_CONST("IPv6 any: %*s"), STRING_FORMAT(address_str));
		EXPECT_STRINGEQ(address_str, string_const(STRING_CONST("[::]:80")));

		any_resolve = network_address_resolve(STRING_ARGS(address_str));
		EXPECT_EQ(array_size(any_resolve), 1);
		EXPECT_TRUE(network_address_equal(any, any_resolve[0]));

		network_address_array_deallocate(any_resolve);
		memory_deallocate(any);
	}

	return 0;
}

DECLARE_TEST(address, family) {
	bool has_ipv4 = network_supports_ipv4();
	bool has_ipv6 = network_supports_ipv6();
	network_address_t** any_resolve;

	if (has_ipv4) {
		network_address_t* any = network_address_ipv4_any();
		EXPECT_EQ(network_address_family(any), NETWORK_ADDRESSFAMILY_IPV4);

		network_address_ip_set_port(any, 80);
		EXPECT_EQ(network_address_family(any), NETWORK_ADDRESSFAMILY_IPV4);

		any_resolve = network_address_resolve(STRING_CONST("0.0.0.0:80"));
		EXPECT_EQ(array_size(any_resolve), 1);
		EXPECT_EQ(network_address_family(any_resolve[0]), NETWORK_ADDRESSFAMILY_IPV4);

		network_address_array_deallocate(any_resolve);
		memory_deallocate(any);
	}

	if (has_ipv6) {
		network_address_t* any = network_address_ipv6_any();
		EXPECT_EQ(network_address_family(any), NETWORK_ADDRESSFAMILY_IPV6);

		network_address_ip_set_port(any, 80);
		EXPECT_EQ(network_address_family(any), NETWORK_ADDRESSFAMILY_IPV6);

		any_resolve = network_address_resolve(STRING_CONST("[::]:80"));
		EXPECT_EQ(array_size(any_resolve), 1);
		EXPECT_EQ(network_address_family(any_resolve[0]), NETWORK_ADDRESSFAMILY_IPV6);

		network_address_array_deallocate(any_resolve);
		memory_deallocate(any);
	}

	return 0;
}

void
test_address_declare(void) {
	ADD_TEST(address, local);
	ADD_TEST(address, resolve);
	ADD_TEST(address, any);
	ADD_TEST(address, port);
	ADD_TEST(address, family);
}

test_suite_t test_address_suite = {
	test_address_application,
	test_address_memory_system,
	test_address_foundation_config,
	test_address_declare,
	test_address_initialize,
	test_address_finalize
};

#if FOUNDATION_PLATFORM_ANDROID || FOUNDATION_PLATFORM_IOS

int
test_address_run(void) {
	test_suite = test_address_suite;
	return test_run_all();
}

#else

test_suite_t
test_suite_define(void) {
	return test_address_suite;
}

#endif

