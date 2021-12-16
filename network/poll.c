/* poll.c  -  Network library  -  Public Domain  -  2013 Mattias Jansson
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/mjansson/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/poll.h>
#include <network/socket.h>
#include <network/address.h>
#include <network/internal.h>

#include <foundation/foundation.h>

#if FOUNDATION_PLATFORM_POSIX
#include <unistd.h>
#include <errno.h>
#elif FOUNDATION_PLATFORM_WINDOWS
#include <foundation/windows.h>
#endif
#if FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
#include <sys/epoll.h>
#elif FOUNDATION_PLATFORM_MACOS || FOUNDATION_PLATFORM_IOS
#include <sys/poll.h>
#endif

#define network_poll_push_event(events, capacity, count, evt, sock) \
	do {                                                            \
		if ((count) < (capacity)) {                                 \
			(events)[(count)].event = (evt);                        \
			(events)[(count)].socket = (sock);                      \
			++(count);                                              \
		}                                                           \
	} while (false)

network_poll_t*
network_poll_allocate(unsigned int max_sockets) {
	network_poll_t* poll;
	size_t memsize = sizeof(network_poll_t) + sizeof(network_poll_slot_t) * max_sockets;
#if FOUNDATION_PLATFORM_APPLE
	memsize += sizeof(struct pollfd) * max_sockets;
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	memsize += sizeof(struct epoll_event) * max_sockets;
#endif
	poll = memory_allocate(HASH_NETWORK, memsize, 8, MEMORY_PERSISTENT);
	network_poll_initialize(poll, max_sockets);
	return poll;
}

void
network_poll_initialize(network_poll_t* pollobj, unsigned int max_sockets) {
	pollobj->sockets_count = 0;
	pollobj->sockets_max = max_sockets;
#if FOUNDATION_PLATFORM_APPLE
	pollobj->pollfds = pointer_offset(pollobj->slots, sizeof(network_poll_slot_t) * max_sockets);
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	pollobj->events = pointer_offset(pollobj->slots, sizeof(network_poll_slot_t) * max_sockets);
	pollobj->fd_poll = epoll_create((int)max_sockets);
#endif
}

void
network_poll_finalize(network_poll_t* pollobj) {
#if FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	close(pollobj->fd_poll);
#else
	FOUNDATION_UNUSED(pollobj);
#endif
}

void
network_poll_deallocate(network_poll_t* pollobj) {
	network_poll_finalize(pollobj);
	memory_deallocate(pollobj);
}

size_t
network_poll_sockets_count(network_poll_t* pollobj) {
	return pollobj->sockets_count;
}

void
network_poll_sockets(network_poll_t* pollobj, socket_t** sockets, size_t max_sockets) {
	size_t is;
	size_t sockets_count = (pollobj->sockets_count < max_sockets) ? pollobj->sockets_count : max_sockets;
	for (is = 0; is < sockets_count; ++is)
		sockets[is] = pollobj->slots[is].sock;
}

static void
network_poll_update_slot(network_poll_t* pollobj, size_t slot, socket_t* sock) {
#if FOUNDATION_PLATFORM_APPLE
	if (sock->fd != NETWORK_SOCKET_INVALID) {
		pollobj->pollfds[slot].fd = sock->fd;
		pollobj->pollfds[slot].events =
		    ((sock->state == SOCKETSTATE_CONNECTING) ? POLLOUT : POLLIN) | POLLERR | POLLHUP;
	} else {
		pollobj->pollfds[slot].fd = 0;
		pollobj->pollfds[slot].events = 0;
	}
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	struct epoll_event event;
	bool add = false;
	if (pollobj->slots[slot].fd != sock->fd) {
		add = true;
		if (pollobj->slots[slot].fd != NETWORK_SOCKET_INVALID) {
			epoll_ctl(pollobj->fd_poll, EPOLL_CTL_DEL, pollobj->slots[slot].fd, &event);
			pollobj->slots[slot].fd = NETWORK_SOCKET_INVALID;
		}
	}
	if (sock->fd != NETWORK_SOCKET_INVALID) {
		event.events = ((sock->state == SOCKETSTATE_CONNECTING) ? EPOLLOUT : EPOLLIN) | EPOLLERR | EPOLLHUP;
		event.data.fd = (int)slot;
		epoll_ctl(pollobj->fd_poll, add ? EPOLL_CTL_ADD : EPOLL_CTL_MOD, sock->fd, &event);
	}
#endif
	pollobj->slots[slot].fd = sock->fd;
}

bool
network_poll_add_socket(network_poll_t* pollobj, socket_t* sock) {
	size_t slot = pollobj->sockets_count;
	if (slot < pollobj->sockets_max) {
		log_debugf(HASH_NETWORK, STRING_CONST("Network poll: Adding socket (0x%" PRIfixPTR " : %d)"), (uintptr_t)sock,
		           sock->fd);

		pollobj->slots[slot].sock = sock;
		pollobj->slots[slot].fd = sock->fd;
		++pollobj->sockets_count;

		network_poll_update_slot(pollobj, slot, sock);

		return true;
	}
	return false;
}

void
network_poll_update_socket(network_poll_t* pollobj, socket_t* sock) {
	size_t islot, sockets_count = pollobj->sockets_count;
	for (islot = 0; islot < sockets_count; ++islot) {
		if (pollobj->slots[islot].sock == sock)
			network_poll_update_slot(pollobj, islot, sock);
	}
}

void
network_poll_remove_socket(network_poll_t* pollobj, socket_t* sock) {
	size_t islot, sockets_count = pollobj->sockets_count;
	for (islot = 0; islot < sockets_count; ++islot) {
		if (pollobj->slots[islot].sock == sock) {
#if FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
			int fd_remove = pollobj->slots[islot].fd;
#endif
			log_debugf(HASH_NETWORK, STRING_CONST("Network poll: Removing socket (0x%" PRIfixPTR " : %d)"),
			           (uintptr_t)pollobj->slots[islot].sock, pollobj->slots[islot].fd);

			// Swap with last slot and erase
			if (islot < sockets_count - 1) {
				memcpy(pollobj->slots + islot, pollobj->slots + (sockets_count - 1), sizeof(network_poll_slot_t));
#if FOUNDATION_PLATFORM_APPLE
				memcpy(pollobj->pollfds + islot, pollobj->pollfds + (sockets_count - 1), sizeof(struct pollfd));
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
				// Mod the moved socket
				struct epoll_event event;
				event.events =
				    ((pollobj->slots[sockets_count - 1].sock->state == SOCKETSTATE_CONNECTING) ? EPOLLOUT : EPOLLIN) |
				    EPOLLERR | EPOLLHUP;
				event.data.fd = (int)islot;
				epoll_ctl(pollobj->fd_poll, EPOLL_CTL_MOD, pollobj->slots[sockets_count - 1].fd, &event);
#endif
			}
			memset(pollobj->slots + (sockets_count - 1), 0, sizeof(network_poll_slot_t));
#if FOUNDATION_PLATFORM_APPLE
			memset(pollobj->pollfds + (sockets_count - 1), 0, sizeof(struct pollfd));
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
			struct epoll_event event;
			epoll_ctl(pollobj->fd_poll, EPOLL_CTL_DEL, fd_remove, &event);
#endif
			sockets_count = --pollobj->sockets_count;
		}
	}
}

bool
network_poll_has_socket(network_poll_t* pollobj, socket_t* sock) {
	for (size_t islot = 0, ssize = pollobj->sockets_count; islot < ssize; ++islot) {
		if (pollobj->slots[islot].sock == sock)
			return true;
	}
	return false;
}

size_t
network_poll(network_poll_t* pollobj, network_poll_event_t* events, size_t capacity, unsigned int timeoutms) {
	int avail = 0;
	size_t events_count = 0;

#if FOUNDATION_PLATFORM_WINDOWS
	// TODO: Refactor to keep fd_set across loop and rebuild on change (add/remove)
	int fd_count = 0;
	int ret = 0;
	size_t islot;
	fd_set fdread, fdwrite, fderr;
#endif

	if (!pollobj->sockets_count)
		return events_count;

#if FOUNDATION_PLATFORM_APPLE

	int ret = poll(pollobj->pollfds, (nfds_t)pollobj->sockets_count, (int)timeoutms);

#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID

	int ret = epoll_wait(pollobj->fd_poll, pollobj->events, (int)pollobj->sockets_count + 1, (int)timeoutms);
	int polled_count = ret;

#elif FOUNDATION_PLATFORM_WINDOWS

	FD_ZERO(&fdread);
	FD_ZERO(&fdwrite);
	FD_ZERO(&fderr);

	for (islot = 0; islot < pollobj->sockets_count; ++islot) {
		int fd = pollobj->slots[islot].fd;
		if (fd != NETWORK_SOCKET_INVALID) {
			socket_t* sock = pollobj->slots[islot].sock;

			FD_SET(fd, &fdread);
			if (sock->state == SOCKETSTATE_CONNECTING)
				FD_SET(fd, &fdwrite);
			FD_SET(fd, &fderr);

			if (fd >= fd_count)
				fd_count = fd + 1;
		}
	}

	if (!fd_count) {
		return events_count;
	} else {
		struct timeval tv;

		tv.tv_sec = timeoutms / 1000;
		tv.tv_usec = (timeoutms % 1000) * 1000;

		ret = select(fd_count, &fdread, &fdwrite, &fderr, (timeoutms != NETWORK_TIMEOUT_INFINITE) ? &tv : nullptr);
	}

#else
#error Not implemented
#endif

	if (ret < 0) {
		int err = NETWORK_SOCKET_ERROR;
		string_const_t errmsg = system_error_message(err);
		log_warnf(HASH_NETWORK, WARNING_SUSPICIOUS, STRING_CONST("Error in socket poll: %.*s (%d)"),
		          STRING_FORMAT(errmsg), err);
		if (!avail)
			return events_count;
		ret = avail;
	}
	if (!avail && !ret)
		return events_count;

#if FOUNDATION_PLATFORM_APPLE

	struct pollfd* pfd = pollobj->pollfds;
	network_poll_slot_t* slot = pollobj->slots;
	for (size_t islot = 0; islot < pollobj->sockets_count; ++islot, ++pfd, ++slot) {
		socket_t* sock = slot->sock;
		bool update_slot = false;
		bool had_error = false;
		if (pfd->revents & POLLERR) {
			update_slot = true;
			had_error = true;
			network_poll_push_event(events, capacity, events_count, NETWORKEVENT_ERROR, sock);
			socket_close(sock);
		}
		if (pfd->revents & POLLHUP) {
			update_slot = true;
			had_error = true;
			network_poll_push_event(events, capacity, events_count, NETWORKEVENT_HANGUP, sock);
			socket_close(sock);
		}
		if (!had_error && (pfd->revents & POLLIN)) {
			if (sock->state == SOCKETSTATE_LISTENING) {
				network_poll_push_event(events, capacity, events_count, NETWORKEVENT_CONNECTION, sock);
			} else {
				network_poll_push_event(events, capacity, events_count, NETWORKEVENT_DATAIN, sock);
			}
		}
		if (!had_error && (sock->state == SOCKETSTATE_CONNECTING) && (pfd->revents & POLLOUT)) {
			int serr = 0;
			socklen_t slen = sizeof(int);
			getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, (void*)&serr, &slen);
			if (!serr) {
				sock->state = SOCKETSTATE_CONNECTED;
				network_poll_push_event(events, capacity, events_count, NETWORKEVENT_CONNECTED, sock);
			} else {
				had_error = true;
				network_poll_push_event(events, capacity, events_count, NETWORKEVENT_ERROR, sock);
				socket_close(sock);
			}
			update_slot = true;
		}
		if (update_slot)
			network_poll_update_slot(pollobj, islot, sock);
	}

#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID

	struct epoll_event* event = pollobj->events;
	for (int i = 0; i < polled_count; ++i, ++event) {
		socket_t* sock = pollobj->slots[event->data.fd].sock;
		bool update_slot = false;
		bool had_error = false;
		if (event->events & EPOLLERR) {
			update_slot = true;
			had_error = true;
			network_poll_push_event(events, capacity, events_count, NETWORKEVENT_ERROR, sock);
			socket_close(sock);
		}
		if (event->events & EPOLLHUP) {
			update_slot = true;
			had_error = true;
			network_poll_push_event(events, capacity, events_count, NETWORKEVENT_HANGUP, sock);
			socket_close(sock);
		}
		if (!had_error && (event->events & EPOLLIN)) {
			if (sock->state == SOCKETSTATE_LISTENING) {
				network_poll_push_event(events, capacity, events_count, NETWORKEVENT_CONNECTION, sock);
			} else {
				network_poll_push_event(events, capacity, events_count, NETWORKEVENT_DATAIN, sock);
			}
		}
		if (!had_error && (sock->state == SOCKETSTATE_CONNECTING) && (event->events & EPOLLOUT)) {
			int serr = 0;
			socklen_t slen = sizeof(int);
			getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, (void*)&serr, &slen);
			if (!serr) {
				sock->state = SOCKETSTATE_CONNECTED;
				network_poll_push_event(events, capacity, events_count, NETWORKEVENT_CONNECTED, sock);
			} else {
				had_error = true;
				network_poll_push_event(events, capacity, events_count, NETWORKEVENT_ERROR, sock);
				socket_close(sock);
			}
			update_slot = true;
		}
		if (update_slot)
			network_poll_update_slot(pollobj, (size_t)event->data.fd, sock);
	}

#elif FOUNDATION_PLATFORM_WINDOWS

	for (islot = 0; islot < pollobj->sockets_count; ++islot) {
		int fd = pollobj->slots[islot].fd;
		socket_t* sock = pollobj->slots[islot].sock;
		bool update_slot = false;

		if (FD_ISSET(fd, &fdread)) {
			if (sock->state == SOCKETSTATE_LISTENING) {
				network_poll_push_event(events, capacity, events_count, NETWORKEVENT_CONNECTION, sock);
			} else {  // SOCKETSTATE_CONNECTED
				network_poll_push_event(events, capacity, events_count, NETWORKEVENT_DATAIN, sock);
			}
		}
		if ((sock->state == SOCKETSTATE_CONNECTING) && FD_ISSET(fd, &fdwrite)) {
			update_slot = true;
			socket_set_state(sock, SOCKETSTATE_CONNECTED);
			network_poll_push_event(events, capacity, events_count, NETWORKEVENT_CONNECTED, sock);
		}
		if (FD_ISSET(fd, &fderr)) {
			update_slot = true;
			network_poll_push_event(events, capacity, events_count, NETWORKEVENT_HANGUP, sock);
			socket_close(sock);
		}
		if (update_slot)
			network_poll_update_slot(pollobj, islot, sock);
	}
#else
#error Not implemented
#endif

	return events_count;
}
