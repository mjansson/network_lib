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
	poll = memory_allocate(HASH_NETWORK, memsize, 8, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
	poll->max_sockets = num_sockets;
#if FOUNDATION_PLATFORM_APPLE
	poll->pollfds = pointer_offset(poll->slots, sizeof(network_poll_slot_t) * num_sockets);
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	poll->events = pointer_offset(poll->slots, sizeof(network_poll_slot_t) * num_sockets);
	poll->fd_poll = epoll_create(num_sockets);
#endif
	return poll;
}

void
network_poll_deallocate(network_poll_t* pollobj) {
#if FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	close(pollobj->fd_poll);
#endif

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

bool
network_poll_add_socket(network_poll_t* pollobj, socket_t* sock) {
	size_t num_sockets = pollobj->num_sockets;
	if ((sock->fd != SOCKET_INVALID) && (num_sockets < pollobj->max_sockets)) {
		log_debugf(HASH_NETWORK, STRING_CONST("Network poll: Adding socket (0x%" PRIfixPTR " : %d)"),
		           (uintptr_t)sock, sock->fd);

		pollobj->slots[ num_sockets ].sock = sock;
		pollobj->slots[ num_sockets ].fd = sock->fd;

		if (sock->state == SOCKETSTATE_CONNECTING)
			socket_poll_state(sock);

#if FOUNDATION_PLATFORM_APPLE
		pollobj->pollfds[ num_sockets ].fd = sock->fd;
		pollobj->pollfds[ num_sockets ].events = ((sock->state == SOCKETSTATE_CONNECTING) ? POLLOUT :
		                                          POLLIN) | POLLERR | POLLHUP;
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
		struct epoll_event event;
		event.events = ((sock->state == SOCKETSTATE_CONNECTING) ? EPOLLOUT : EPOLLIN) | EPOLLERR |
		               EPOLLHUP;
		event.data.fd = (int)pollobj->num_sockets;
		epoll_ctl(pollobj->fd_poll, EPOLL_CTL_ADD, sock->fd, &event);
#endif
		++pollobj->num_sockets;

		return true;
	}
	return false;
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
		socket_t* sock = pollobj->slots[islot].sock;

		FD_SET(fd, &fdread);
		if (sock->state == SOCKETSTATE_CONNECTING)
			FD_SET(fd, &fdwrite);
		FD_SET(fd, &fderr);

		if (fd >= num_fd)
			num_fd = fd + 1;
	}

	if (!num_fd) {
		return num_events;
	}
	else {
		struct timeval tv;

		tv.tv_sec  = timeoutms / 1000;
		tv.tv_usec = (timeoutms % 1000) * 1000;

		ret = select(num_fd, &fdread, &fdwrite, &fderr, &tv);
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
		if (pfd->revents & POLLIN) {
			if (sock->state == SOCKETSTATE_LISTENING) {
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_CONNECTION, sock);
			}
			else {
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_DATAIN, sock);
			}
		}
		if ((sock->state == SOCKETSTATE_CONNECTING) && (pfd->revents & POLLOUT)) {
			sock->state = SOCKETSTATE_CONNECTED;
			pfd->events = POLLIN | POLLERR | POLLHUP;
			network_poll_push_event(events, capacity, num_events, NETWORKEVENT_CONNECTED, sock);
		}
		if (pfd->revents & POLLERR) {
			pfd->events = POLLOUT | POLLERR | POLLHUP;
			network_poll_push_event(events, capacity, num_events, NETWORKEVENT_ERROR, sock);
			socket_close(sock);
		}
		if (pfd->revents & POLLHUP) {
			pfd->events = POLLOUT | POLLERR | POLLHUP;
			network_poll_push_event(events, capacity, num_events, NETWORKEVENT_HANGUP, sock);
			socket_close(sock);
		}
	}

#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID

	struct epoll_event* event = pollobj->events;
	for (int i = 0; i < num_polled; ++i, ++event) {
		FOUNDATION_ASSERT(pollobj->slots[ event->data.fd ].base >= 0);

		socket_t* sock = pollobj->slots[ event->data.fd ].sock;
		int fd = pollobj->slots[ event->data.fd ].fd;
		if ((sock->fd == fd) && (event->events & EPOLLIN)) {
			if (sock->state == SOCKETSTATE_LISTENING) {
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_CONNECTION, sock);
			}
			else {
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_DATAIN, sock);
			}
		}
		if ((sock->state == SOCKETSTATE_CONNECTING) && (event->events & EPOLLOUT)) {
			struct epoll_event mod_event;
			mod_event.events = EPOLLIN | EPOLLERR | EPOLLHUP;
			mod_event.data.fd = event->data.fd;
			epoll_ctl(pollobj->fd_poll, EPOLL_CTL_MOD, fd, &mod_event);
			if (sock->fd == fd) {
				sock->state = SOCKETSTATE_CONNECTED;
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_CONNECTED, sock);
			}
		}
		if (event->events & EPOLLERR) {
			struct epoll_event del_event;
			epoll_ctl(pollobj->fd_poll, EPOLL_CTL_DEL, fd, &del_event);
			if (sock->fd == fd) {
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_ERROR, sock);
				socket_close(sock);
			}
		}
		if (event->events & EPOLLHUP) {
			struct epoll_event del_event;
			epoll_ctl(pollobj->fd_poll, EPOLL_CTL_DEL, fd, &del_event);
			if (sock->fd == fd) {
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_HANGUP, sock);
				socket_close(sock);
			}
		}
	}

#elif FOUNDATION_PLATFORM_WINDOWS

	for (islot = 0; islot < pollobj->num_sockets; ++islot) {
		int fd = pollobj->slots[islot].fd;
		socket_t* sock = pollobj->slots[islot].sock;
		if (sock->fd != fd)
			continue;

		if (FD_ISSET(fd, &fdread)) {
			if (sock->state == SOCKETSTATE_LISTENING) {
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_CONNECTION, sock);
			}
			else { //SOCKETSTATE_CONNECTED
				network_poll_push_event(events, capacity, num_events, NETWORKEVENT_DATAIN, sock);
			}
		}
		if ((sock->state == SOCKETSTATE_CONNECTING) && FD_ISSET(fd, &fdwrite)) {
			sock->state = SOCKETSTATE_CONNECTED;
			network_poll_push_event(events, capacity, num_events, NETWORKEVENT_CONNECTED, sock);
		}
		if (FD_ISSET(fd, &fderr)) {
			network_poll_push_event(events, capacity, num_events, NETWORKEVENT_HANGUP, sock);
			socket_close(sock);
		}
	}
#else
#  error Not implemented
#endif

	return num_events;
}
