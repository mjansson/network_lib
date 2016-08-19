/* poll.c  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/rampantpixels/network_lib
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
#  include <unistd.h>
#  include <errno.h>
#elif FOUNDATION_PLATFORM_WINDOWS
#  include <foundation/windows.h>
#endif
#if FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
#  include <sys/epoll.h>
#elif FOUNDATION_PLATFORM_MACOSX || FOUNDATION_PLATFORM_IOS
#  include <sys/poll.h>
#endif

#define network_poll_push_event(events, capacity, num, evt, sock) \
	do { if ((num) < (capacity)) { \
		(events)[(num)].event = (evt); \
		(events)[(num)].socket = (sock); \
		++(num); \
	} } while (false)

network_poll_t*
network_poll_allocate(unsigned int num_sockets) {
	network_poll_t* poll;
	size_t memsize = sizeof(network_poll_t) +
	                 sizeof(network_poll_slot_t) * num_sockets;
#if FOUNDATION_PLATFORM_APPLE
	memsize += sizeof(struct pollfd) * num_sockets;
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	memsize += sizeof(struct epoll_event) * num_sockets;
#endif
	poll = memory_allocate(HASH_NETWORK, memsize, 8, MEMORY_PERSISTENT);
	network_poll_initialize(poll, num_sockets);
	return poll;
}

void
network_poll_initialize(network_poll_t* pollobj, unsigned int num_sockets) {
	pollobj->num_sockets = 0;
	pollobj->max_sockets = num_sockets;
#if FOUNDATION_PLATFORM_APPLE
	pollobj->pollfds = pointer_offset(pollobj->slots, sizeof(network_poll_slot_t) * num_sockets);
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	pollobj->events = pointer_offset(pollobj->slots, sizeof(network_poll_slot_t) * num_sockets);
	pollobj->fd_poll = epoll_create(num_sockets);
#endif
}

void
network_poll_finalize(network_poll_t* pollobj) {
#if FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	close(pollobj->fd_poll);
#endif
}

void
network_poll_deallocate(network_poll_t* pollobj) {
	network_poll_finalize(pollobj);
	memory_deallocate(pollobj);
}

size_t
network_poll_num_sockets(network_poll_t* pollobj) {
	return pollobj->num_sockets;
}

void
network_poll_sockets(network_poll_t* pollobj, socket_t** sockets, size_t max_sockets) {
	size_t is;
	size_t num_sockets = (pollobj->num_sockets < max_sockets) ? pollobj->num_sockets : max_sockets;
	for (is = 0; is < num_sockets; ++is)
		sockets[is] = pollobj->slots[is].sock;
}

static void
network_poll_update_slot(network_poll_t* pollobj, size_t slot, socket_t* sock) {
#if FOUNDATION_PLATFORM_APPLE
	if (sock->fd != NETWORK_SOCKET_INVALID) {
		pollobj->pollfds[slot].fd = sock->fd;
		pollobj->pollfds[slot].events = ((sock->state == SOCKETSTATE_CONNECTING) ? POLLOUT :
		                                 POLLIN) | POLLERR | POLLHUP;
	}
	else {
		pollobj->pollfds[slot].fd = 0;
		pollobj->pollfds[slot].events = 0;
	}
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	struct epoll_event event;
	if (slot->fd != sock->fd) {
		if (slot->fd != NETWORK_SOCKET_INVALID)
			epoll_ctl(pollobj->fd_poll, EPOLL_CTL_DEL, slot->fd, &event);
	}
	if (sock->fd != NETWORK_SOCKET_INVALID) {
		event.events = ((sock->state == SOCKETSTATE_CONNECTING) ? EPOLLOUT : EPOLLIN) |
		               EPOLLERR | EPOLLHUP;
		event.data.fd = (int)slot;
		if (slot->fd != NETWORK_SOCKET_INVALID)
			epoll_ctl(pollobj->fd_poll, EPOLL_CTL_ADD, sock->fd, &event);
		else
			epoll_ctl(pollobj->fd_poll, EPOLL_CTL_MOD, sock->fd, &event);
	}
#endif
	pollobj->slots[slot].fd = sock->fd;
}

bool
network_poll_add_socket(network_poll_t* pollobj, socket_t* sock) {
	size_t slot = pollobj->num_sockets;
	if (slot < pollobj->max_sockets) {
		log_debugf(HASH_NETWORK, STRING_CONST("Network poll: Adding socket (0x%" PRIfixPTR " : %d)"),
		           (uintptr_t)sock, sock->fd);

		pollobj->slots[slot].sock = sock;
		pollobj->slots[slot].fd = sock->fd;
		++pollobj->num_sockets;

		network_poll_update_slot(pollobj, slot, sock);

		return true;
	}
	return false;
}

void
network_poll_update_socket(network_poll_t* pollobj, socket_t* sock) {
	size_t islot, num_sockets = pollobj->num_sockets;
	for (islot = 0; islot < num_sockets; ++islot) {
		if (pollobj->slots[islot].sock == sock)
			network_poll_update_slot(pollobj, islot, sock);
	}
}

void
network_poll_remove_socket(network_poll_t* pollobj, socket_t* sock) {
	size_t islot, num_sockets = pollobj->num_sockets;
	for (islot = 0; islot < num_sockets; ++islot) {
		if (pollobj->slots[islot].sock == sock) {
#if FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
			int fd_remove = pollobj->slots[islot].fd;
#endif
			log_debugf(HASH_NETWORK,
			           STRING_CONST("Network poll: Removing socket (0x%" PRIfixPTR " : %d)"),
			           (uintptr_t)pollobj->slots[islot].sock, pollobj->slots[islot].fd);

			//Swap with last slot and erase
			if (islot < pollobj->num_sockets - 1) {
				memcpy(pollobj->slots + islot, pollobj->slots + (num_sockets - 1), sizeof(network_poll_slot_t));
#if FOUNDATION_PLATFORM_APPLE
				memcpy(pollobj->pollfds + islot, pollobj->pollfds + (num_sockets - 1), sizeof(struct pollfd));
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
				//Mod the moved socket
				FOUNDATION_ASSERT(pollobj->slots[islot].base >= 0);
				struct epoll_event event;
				event.events = ((_socket_base[ pollobj->slots[islot].base ].state == SOCKETSTATE_CONNECTING) ?
				                EPOLLOUT : EPOLLIN) | EPOLLERR | EPOLLHUP;
				event.data.fd = (int)islot;
				epoll_ctl(pollobj->fd_poll, EPOLL_CTL_MOD, pollobj->slots[islot].fd, &event);
#endif
			}
			memset(pollobj->slots + (num_sockets - 1), 0, sizeof(network_poll_slot_t));
#if FOUNDATION_PLATFORM_APPLE
			memset(pollobj->pollfds + (num_sockets - 1), 0, sizeof(struct pollfd));
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
			struct epoll_event event;
			epoll_ctl(pollobj->fd_poll, EPOLL_CTL_DEL, fd_remove, &event);
#endif
			num_sockets = --pollobj->num_sockets;
		}
	}
}

bool
network_poll_has_socket(network_poll_t* pollobj, socket_t* sock) {
	size_t islot, num_sockets;
	num_sockets = pollobj->num_sockets;
	for (islot = 0; islot < num_sockets; ++islot) {
		if (pollobj->slots[islot].sock == sock)
			return true;
	}
	return false;
}

size_t
network_poll(network_poll_t* pollobj, network_poll_event_t* events, size_t capacity,
             unsigned int timeoutms) {
	int avail = 0;
	size_t num_events = 0;

#if FOUNDATION_PLATFORM_WINDOWS
	//TODO: Refactor to keep fd_set across loop and rebuild on change (add/remove)
	int num_fd = 0;
	int ret = 0;
	size_t islot;
	fd_set fdread, fdwrite, fderr;
#endif

	if (!pollobj->num_sockets)
		return num_events;

#if FOUNDATION_PLATFORM_APPLE

	int ret = poll(pollobj->pollfds, (nfds_t)pollobj->num_sockets, (int)timeoutms);

#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID

	int ret = epoll_wait(pollobj->fd_poll, pollobj->events, pollobj->num_sockets + 1, timeoutms);
	int num_polled = ret;

#elif FOUNDATION_PLATFORM_WINDOWS

	FD_ZERO(&fdread);
	FD_ZERO(&fdwrite);
	FD_ZERO(&fderr);

	for (islot = 0; islot < pollobj->num_sockets; ++islot) {
		int fd = pollobj->slots[islot].fd;
		if (fd != NETWORK_SOCKET_INVALID) {
			socket_t* sock = pollobj->slots[islot].sock;

			FD_SET(fd, &fdread);
			if (sock->state == SOCKETSTATE_CONNECTING)
				FD_SET(fd, &fdwrite);
			FD_SET(fd, &fderr);

			if (fd >= num_fd)
				num_fd = fd + 1;
		}
	}

	if (!num_fd) {
		return num_events;
	}
	else {
		struct timeval tv;

		tv.tv_sec  = timeoutms / 1000;
		tv.tv_usec = (timeoutms % 1000) * 1000;

		ret = select(num_fd, &fdread, &fdwrite, &fderr,
		             (timeoutms != NETWORK_TIMEOUT_INFINITE) ? &tv : nullptr);
	}

#else
#  error Not implemented
#endif

	if (ret < 0) {
		int err = NETWORK_SOCKET_ERROR;
		string_const_t errmsg = system_error_message(err);
		log_warnf(HASH_NETWORK, WARNING_SUSPICIOUS, STRING_CONST("Error in socket poll: %.*s (%d)"),
		          STRING_FORMAT(errmsg), err);
		if (!avail)
			return num_events;
		ret = avail;
	}
	if (!avail && !ret)
		return num_events;

#if FOUNDATION_PLATFORM_APPLE

	struct pollfd* pfd = pollobj->pollfds;
	network_poll_slot_t* slot = pollobj->slots;
	for (size_t i = 0; i < pollobj->num_sockets; ++i, ++pfd, ++slot) {
		socket_t* sock = slot->sock;
		bool update_slot = false;
		if (pfd->revents & POLLIN) {
			if (sock->state == SOCKETSTATE_LISTENING) {
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_CONNECTION, sock);
			}
			else {
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_DATAIN, sock);
			}
		}
		if ((sock->state == SOCKETSTATE_CONNECTING) && (pfd->revents & POLLOUT)) {
			update_slot = true;
			sock->state = SOCKETSTATE_CONNECTED;
			network_poll_push_event(events, capacity, num_events, NETWORKEVENT_CONNECTED, sock);
		}
		if (pfd->revents & POLLERR) {
			update_slot = true;
			network_poll_push_event(events, capacity, num_events, NETWORKEVENT_ERROR, sock);
			socket_close(sock);
		}
		if (pfd->revents & POLLHUP) {
			update_slot = true;
			network_poll_push_event(events, capacity, num_events, NETWORKEVENT_HANGUP, sock);
			socket_close(sock);
		}
		if (update_slot)
			network_poll_update_slot(pollobj, event->data.fd, sock);
	}

#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID

	struct epoll_event* event = pollobj->events;
	for (int i = 0; i < num_polled; ++i, ++event) {
		FOUNDATION_ASSERT(pollobj->slots[ event->data.fd ].base >= 0);

		socket_t* sock = pollobj->slots[ event->data.fd ].sock;
		bool update_slot = false;
		if (event->events & EPOLLIN) {
			if (sock->state == SOCKETSTATE_LISTENING) {
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_CONNECTION, sock);
			}
			else {
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_DATAIN, sock);
			}
		}
		if ((sock->state == SOCKETSTATE_CONNECTING) && (event->events & EPOLLOUT)) {
			update_slot = true;
			sock->state = SOCKETSTATE_CONNECTED;
			network_poll_push_event(events, capacity, num_events, NETWORKEVENT_CONNECTED, sock);
		}
		if (event->events & EPOLLERR) {
			update_slot = true;
			network_poll_push_event(events, capacity, num_events, NETWORKEVENT_ERROR, sock);
			socket_close(sock);
		}
		if (event->events & EPOLLHUP) {
			update_slot = true;
			network_poll_push_event(events, capacity, num_events, NETWORKEVENT_HANGUP, sock);
			socket_close(sock);
		}
		if (update_slot)
			network_poll_update_slot(pollobj, event->data.fd, sock);
	}

#elif FOUNDATION_PLATFORM_WINDOWS

	for (islot = 0; islot < pollobj->num_sockets; ++islot) {
		int fd = pollobj->slots[islot].fd;
		socket_t* sock = pollobj->slots[islot].sock;
		bool update_slot = false;

		if (FD_ISSET(fd, &fdread)) {
			if (sock->state == SOCKETSTATE_LISTENING) {
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_CONNECTION, sock);
			}
			else { //SOCKETSTATE_CONNECTED
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_DATAIN, sock);
			}
		}
		if ((sock->state == SOCKETSTATE_CONNECTING) && FD_ISSET(fd, &fdwrite)) {
			update_slot = true;
			sock->state = SOCKETSTATE_CONNECTED;
			network_poll_push_event(events, capacity, num_events, NETWORKEVENT_CONNECTED, sock);
		}
		if (FD_ISSET(fd, &fderr)) {
			update_slot = true;
			network_poll_push_event(events, capacity, num_events, NETWORKEVENT_HANGUP, sock);
			socket_close(sock);
		}
		if (update_slot)
			network_poll_update_slot(pollobj, islot, sock);
	}
#else
#  error Not implemented
#endif

	return num_events;
}
