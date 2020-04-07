/* server.c  -  Network blast tool  -  Public Domain  -  2013 Mattias Jansson
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/mjansson/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include "blast.h"
#include "server.h"
#include "writer.h"

#define BLAST_SERVER_TIMEOUT 30

typedef struct blast_server_source_t {
	network_address_t* address;
	socket_t* sock;
	uint64_t token;
	bool got_payload;
	tick_t last_recv;
	blast_writer_t* writer;
	uint32_t ack[PACKET_ACK_HISTORY];
	int ack_offset;
	tick_t last_ack;
	int last_ack_offset;
} blast_server_source_t;

typedef struct blast_server_t {
	blast_server_source_t** sources;
	uint64_t token_counter;
} blast_server_t;

static blast_server_source_t*
blast_server_source_allocate(void) {
	blast_server_source_t* source =
	    memory_allocate(HASH_BLAST, sizeof(blast_server_source_t), 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
	memset(source->ack, 0xFF, sizeof(uint32_t) * PACKET_ACK_HISTORY);
	return source;
}

static void
blast_server_source_deallocate(blast_server_source_t* source) {
	blast_writer_close(source->writer);
	socket_deallocate(source->sock);
	memory_deallocate(source->address);
	memory_deallocate(source);
}

static unsigned int
blast_server_time_until_ack(blast_server_t* server) {
	FOUNDATION_UNUSED(server);
	return 10;
}

static void
blast_server_send_ack(blast_server_source_t* source) {
	packet_ack_t packet;
	int first_acks = 0;
	int second_acks = 0;

	if (!source->writer || !source->got_payload) {
		source->last_ack = time_current();
		return;
	}

	packet.type = PACKET_ACK;
	packet.token = source->token;
	packet.timestamp = 0;
	packet.seq = 0;

	if (source->ack_offset < PACKET_ACK_COUNT) {
		first_acks = (PACKET_ACK_COUNT - source->ack_offset);
		memcpy(packet.ack, source->ack + (PACKET_ACK_HISTORY - first_acks), sizeof(uint32_t) * (size_t)first_acks);
	}
	second_acks = PACKET_ACK_COUNT - first_acks;
	memcpy(packet.ack + first_acks, source->ack + (source->ack_offset - second_acks),
	       sizeof(uint32_t) * (size_t)second_acks);

	/*log_infof(HASH_BLAST,
	          STRING_CONST("Send ACKs [%d:%d up to %d] (ack seq %d) for transfer of '%.*s' size %lld with token %d"),
	          first_acks, second_acks, source->ack_offset, packet.ack[0], STRING_FORMAT(source->writer->name),
	          source->writer->size, source->token);*/

	udp_socket_sendto(source->sock, &packet, sizeof(packet), source->address);

	source->last_ack = time_current();
	source->last_ack_offset = source->ack_offset;
}

static void
blast_server_send_acks(blast_server_t* server) {
	unsigned int isrc, ssize;
	for (isrc = 0, ssize = array_size(server->sources); isrc < ssize; ++isrc) {
		blast_server_source_t* source = server->sources[isrc];
		// TODO: Base on round-trip time and back-off rate (back-off reset on recv packet)
		if (time_elapsed(source->last_ack) > REAL_C(0.05)) {
			blast_server_send_ack(source);
		}
	}
}

static void
blast_server_queue_ack(blast_server_source_t* source, uint32_t ack) {
	int send_trigger;

	// log_infof( HASH_BLAST, "Queue ACK for seq %d in slot %d", ack, source->ack_offset );

	source->ack[source->ack_offset++] = ack;
	if (source->ack_offset >= PACKET_ACK_HISTORY)
		source->ack_offset = 0;

	send_trigger = source->last_ack_offset + PACKET_ACK_COUNT;
	if (send_trigger >= PACKET_ACK_HISTORY)
		send_trigger -= PACKET_ACK_HISTORY;
	if (source->ack_offset == send_trigger)
		blast_server_send_ack(source);
}

static bool
blast_server_has_ack(blast_server_source_t* source, uint32_t ack) {
	int iack;
	for (iack = 0; iack < PACKET_ACK_HISTORY; ++iack) {
		if (source->ack[iack] == ack)
			return true;
	}
	return false;
}

static void
blast_server_process_handshake(blast_server_t* server, socket_t* sock, void* data, size_t size,
                               const network_address_t* address) {
	packet_handshake_t* handshake = data;
	blast_server_source_t* source = 0;
	unsigned int isrc, ssize;
	char addrbuf[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];

	string_t addr = network_address_to_string(addrbuf, sizeof(addrbuf), address, true);

	if (handshake->datasize > PACKET_DATA_MAXSIZE) {
		log_warnf(HASH_BLAST, WARNING_INVALID_VALUE, STRING_CONST("Invalid data size %" PRIsize " from %.*s"),
		          (size_t)handshake->datasize, STRING_FORMAT(addr));
		return;
	}

	if (!handshake->namesize || (handshake->namesize > PACKET_NAME_MAXSIZE)) {
		log_warnf(HASH_BLAST, WARNING_INVALID_VALUE, STRING_CONST("Invalid name size %d from %.*s"),
		          handshake->namesize, STRING_FORMAT(addr));
		return;
	}

	log_infof(HASH_BLAST, STRING_CONST("Got handshake packet from %.*s (seq %d, timestamp %" PRItick ")"),
	          STRING_FORMAT(addr), (int)handshake->seq, (tick_t)handshake->timestamp);

	for (isrc = 0, ssize = array_size(server->sources); isrc < ssize; ++isrc) {
		if (network_address_equal(server->sources[isrc]->address, address)) {
			source = server->sources[isrc];
			break;
		}
	}

	if (source) {
		if (source->writer && !string_equal(STRING_ARGS(source->writer->name), handshake->name, handshake->namesize)) {
			log_infof(HASH_BLAST, STRING_CONST("Source re-initializing with new writer"));
			blast_writer_close(source->writer);
			source->writer = 0;
		}
	} else {
		source = blast_server_source_allocate();
		source->sock = sock;
		source->address = network_address_clone(address);
		array_push(server->sources, source);
	}

	if (!source->writer) {
		source->writer = blast_writer_open(handshake->name, (size_t)handshake->namesize, handshake->datasize);
		source->token = (++server->token_counter) & PACKET_TOKEN_MASK;
		source->last_ack = time_current();
		log_infof(
		    HASH_BLAST, STRING_CONST("Prepare transfer of '%.*s' size %" PRIsize " with token %" PRIu64 " from %.*s"),
		    (int)handshake->namesize, handshake->name, (size_t)handshake->datasize, source->token, STRING_FORMAT(addr));
	}

	handshake->token = source->token;
	udp_socket_sendto(sock, data, size, address);

	source->last_recv = time_current();
}

static void
blast_server_process_payload(blast_server_t* server, socket_t* sock, void* data, size_t size,
                             const network_address_t* address) {
	packet_payload_t* packet = (packet_payload_t*)data;
	blast_server_source_t* source = 0;
	unsigned int isrc, ssize;
	void* buffer;
	uint64_t offset;

	for (isrc = 0, ssize = array_size(server->sources); isrc < ssize; ++isrc) {
		if ((server->sources[isrc]->sock == sock) && (network_address_equal(server->sources[isrc]->address, address))) {
			source = server->sources[isrc];
			break;
		}
	}

	if (!source) {
		log_warnf(HASH_BLAST, WARNING_SUSPICIOUS, STRING_CONST("Got payload from unknown source"));
		return;
	}

	if (size != sizeof(packet_payload_t)) {
		log_warnf(HASH_BLAST, WARNING_SUSPICIOUS, STRING_CONST("Got invalid sized payload"));
		return;
	}

	source->got_payload = true;
	source->last_recv = time_current();

	if (!source->writer) {
		log_warnf(HASH_BLAST, WARNING_SUSPICIOUS, STRING_CONST("Got payload from uninitialized source"));
		return;
	}

	if (blast_server_has_ack(source, (uint32_t)packet->seq)) {
		log_infof(HASH_BLAST, STRING_CONST("Had previous ACK of seq %" PRIu64 ", ignore write and re-ACK"),
		          packet->seq);
		blast_server_queue_ack(source, (uint32_t)packet->seq);
		return;
	}

	offset = packet->seq * PACKET_CHUNK_SIZE;
	if (offset >= source->writer->size) {
		log_warnf(HASH_BLAST, WARNING_SUSPICIOUS, STRING_CONST("Got invalid payload seq %" PRIu64 " out of range"),
		          packet->seq);
		return;
	}

	buffer = source->writer->map(source->writer, offset, PACKET_CHUNK_SIZE);
	if (!buffer) {
		log_warnf(HASH_BLAST, WARNING_SUSPICIOUS,
		          STRING_CONST("Unable to map chunk for payload seq %" PRIu64 " out of range"), packet->seq);
		return;
	}

	blast_server_queue_ack(source, (uint32_t)packet->seq);

	memcpy(buffer, packet->data, PACKET_CHUNK_SIZE);
	source->writer->unmap(source->writer, buffer, offset, PACKET_CHUNK_SIZE);

	// log_infof( HASH_BLAST, "Wrote payload @ offset %lld (seq %lld) in transfer of '%s' size %lld with token %d",
	// offset, packet->seq, source->writer->name, source->writer->size, source->token );
}

static bool
blast_server_read(blast_server_t* server, socket_t* sock) {
	const network_address_t* address = 0;
	union {
		char buffer[PACKET_DATABUF_SIZE];
		packet_t packet;
	} databuf;
	size_t size = udp_socket_recvfrom(sock, databuf.buffer, sizeof(databuf.buffer), &address);
	if (size == 0)
		return false;
	while (size > 0) {
		packet_t* packet = &databuf.packet;
		if (packet->type == PACKET_HANDSHAKE) {
			blast_server_process_handshake(server, sock, databuf.buffer, size, address);
		} else if (packet->type == PACKET_PAYLOAD) {
			blast_server_process_payload(server, sock, databuf.buffer, size, address);
		} else {
			log_warnf(HASH_BLAST, WARNING_SUSPICIOUS, STRING_CONST("Unknown datagram on socket"));
		}
		size = udp_socket_recvfrom(sock, databuf.buffer, sizeof(databuf.buffer), &address);
	}
	return true;
}

static void
blast_server_tick(blast_server_t* server) {
	unsigned int isrc, ssize;
	for (isrc = 0, ssize = array_size(server->sources); isrc < ssize;) {
		if (time_elapsed(server->sources[isrc]->last_recv) > BLAST_SERVER_TIMEOUT) {
			char addrbuf[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
			string_t addr = network_address_to_string(addrbuf, sizeof(addrbuf), server->sources[isrc]->address, true);
			log_infof(HASH_BLAST, STRING_CONST("Deleting inactive source from %.*s"), STRING_FORMAT(addr));

			blast_server_source_deallocate(server->sources[isrc]);

			array_erase(server->sources, isrc);
			--ssize;
		} else {
			blast_server_send_acks(server);
			++isrc;
		}
	}
}

static int
blast_server_run(bool daemon, network_poll_t* poll, blast_server_t* server) {
	int result = BLAST_RESULT_OK;
	bool complete;

	if (daemon) {
		// TODO: Implement
	}

	network_poll_event_t events[64];
	while (!blast_should_exit()) {
		size_t num_events =
		    network_poll(poll, events, sizeof(events) / sizeof(events[0]), blast_server_time_until_ack(server));
		size_t ievt;

		for (ievt = 0; ievt < num_events; ++ievt) {
			switch (events[ievt].event) {
				case NETWORKEVENT_DATAIN:
					complete = blast_server_read(server, events[ievt].socket);
					if (complete) {
					}
					break;
				case NETWORKEVENT_CONNECTION:
				case NETWORKEVENT_CONNECTED:
				case NETWORKEVENT_ERROR:
				case NETWORKEVENT_HANGUP:
				default:
					break;
			}
		}
		blast_process_system_events();

		blast_server_tick(server);
	}

	return result;
}

static blast_server_t*
blast_server_allocate(void) {
	return (blast_server_t*)memory_allocate(HASH_BLAST, sizeof(blast_server_t), 0,
	                                        MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
}

static void
blast_server_deallocate(blast_server_t* server) {
	unsigned int isrc, ssize;
	for (isrc = 0, ssize = array_size(server->sources); isrc < ssize; ++isrc)
		blast_server_source_deallocate(server->sources[isrc]);
	memory_deallocate(server);
}

int
blast_server(network_address_t** bind, bool daemon) {
	unsigned int isock, asize, added = 0;
	int result = BLAST_RESULT_OK;
	unsigned int port = 0;
	network_poll_t* poll = 0;
	blast_server_t* server = 0;

	poll = network_poll_allocate(array_size(bind));

	for (isock = 0, asize = array_size(bind); isock < asize; ++isock) {
		socket_t* sock = udp_socket_allocate();

		socket_set_blocking(sock, false);

		if (!network_address_ip_port(bind[isock]) && port)
			network_address_ip_set_port(bind[isock], port);

		if (socket_bind(sock, bind[isock])) {
			const network_address_t* address = socket_address_local(sock);
			char addrbuf[NETWORK_ADDRESS_NUMERIC_MAX_LENGTH];
			string_t addr = network_address_to_string(addrbuf, sizeof(addrbuf), address, true);
			log_infof(HASH_BLAST, STRING_CONST("Listening to %.*s"), STRING_FORMAT(addr));

			network_poll_add_socket(poll, sock);

			if (!port)
				port = network_address_ip_port(address);

			++added;
		}
	}

	server = blast_server_allocate();

	if (added)
		result = blast_server_run(daemon, poll, server);
	else
		log_warnf(HASH_BLAST, WARNING_SUSPICIOUS, STRING_CONST("No bind address given"));

	blast_server_deallocate(server);

	network_poll_deallocate(poll);

	return result;
}
