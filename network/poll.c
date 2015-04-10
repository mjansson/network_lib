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
#include <network/event.h>
#include <network/socket.h>
#include <network/address.h>
#include <network/internal.h>

#include <foundation/foundation.h>

#if FOUNDATION_PLATFORM_POSIX
#  include <unistd.h>
#  include <errno.h>
#elif FOUNDATION_PLATFORM_WINDOWS
#  include <foundation/windows.h>
#  define FAR
#endif
#if FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
#  include <sys/epoll.h>
#elif FOUNDATION_PLATFORM_MACOSX || FOUNDATION_PLATFORM_IOS
#  include <sys/poll.h>
#endif

typedef struct network_poll_slot_t
{
	object_t             sock;
	int                  base;
	int                  fd;
} network_poll_slot_t;

typedef FOUNDATION_ALIGNED_STRUCT( network_poll_t, 8 )
{
	object_t             queue_add[BUILD_SIZE_POLL_QUEUE];
	object_t             queue_remove[BUILD_SIZE_POLL_QUEUE];
	unsigned int         timeout;
	unsigned int         max_sockets;
	unsigned int         num_sockets;
#if FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	int                  fd_poll;
	struct epoll_event*  events;
#elif FOUNDATION_PLATFORM_APPLE
	struct pollfd*       pollfds;
#endif
	network_poll_slot_t  slots[];
} network_poll_t;


static int _network_poll_process_pending( network_poll_t* pollobj )
{
	unsigned int max_sockets = pollobj->max_sockets;
	unsigned int num_sockets = pollobj->num_sockets;
	int num_events = 0;
	int iqueue;
	unsigned int islot;

	//Remove pending sockets
	for( iqueue = 0; iqueue < BUILD_SIZE_POLL_QUEUE; ++iqueue )
	{
		if( pollobj->queue_remove[iqueue] )
		{
			object_t sock = pollobj->queue_remove[iqueue];
			pollobj->queue_remove[iqueue] = 0;

			for( islot = 0; islot < num_sockets; ++islot )
			{
				if( pollobj->slots[islot].sock == sock )
				{
#if FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
					int fd_remove = pollobj->slots[islot].fd;
#endif

					log_debugf( HASH_NETWORK, "Network poll: Removing queued socket 0x%llx %d", pollobj->slots[islot].sock, pollobj->slots[islot].fd );

					//Swap with last slot and erase
					if( islot < pollobj->num_sockets - 1 )
					{
						memcpy( pollobj->slots + islot, pollobj->slots + ( num_sockets - 1 ), sizeof( network_poll_slot_t ) );
#if FOUNDATION_PLATFORM_APPLE
						memcpy( pollobj->pollfds + islot, pollobj->pollfds + ( num_sockets - 1 ), sizeof( struct pollfd ) );
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
						//Mod the moved socket
						FOUNDATION_ASSERT( pollobj->slots[islot].base >= 0 );
						struct epoll_event event;
						event.events = ( ( _socket_base[ pollobj->slots[islot].base ].state == SOCKETSTATE_CONNECTING ) ? EPOLLOUT : EPOLLIN ) | EPOLLERR | EPOLLHUP;
						event.data.fd = (int)islot;
						epoll_ctl( pollobj->fd_poll, EPOLL_CTL_MOD, pollobj->slots[islot].fd, &event );
#endif
					}
					memset( pollobj->slots + ( num_sockets - 1 ), 0, sizeof( network_poll_slot_t ) );
#if FOUNDATION_PLATFORM_APPLE
					memset( pollobj->pollfds + ( num_sockets - 1 ), 0, sizeof( struct pollfd ) );
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
					struct epoll_event event;
					epoll_ctl( pollobj->fd_poll, EPOLL_CTL_DEL, fd_remove, &event );
#endif
					num_sockets = --pollobj->num_sockets;
				}
			}
			{
				socket_t* sockptr = _socket_lookup( sock );
				if( sockptr )
				{
					FOUNDATION_ASSERT( sockptr->base >= 0 );
					_socket_base[ sockptr->base ].flags &= ~SOCKETFLAG_POLLED;
					socket_destroy( sock );
				}
			}
			//Free the reference created when socket was added to poll
			socket_destroy( sock );
		}
	}

	//Add pending sockets
	for( iqueue = 0; ( num_sockets < max_sockets ) && ( iqueue < BUILD_SIZE_POLL_QUEUE ); ++iqueue )
	{
		if( pollobj->queue_add[iqueue] )
		{
			socket_t* sock;
			socket_base_t* sockbase;

			object_t sockobj = pollobj->queue_add[iqueue];
			pollobj->queue_add[iqueue] = 0;

			sock = _socket_lookup( sockobj );
			if( !sock )
				continue;

			FOUNDATION_ASSERT( sock->base >= 0 );
			sockbase = _socket_base + sock->base;

			log_debugf( HASH_NETWORK, "Network poll: Adding queued socket 0x%llx %d", sockobj, sockbase->fd );

			sockbase->flags &= ~( SOCKETFLAG_CONNECTION_PENDING | SOCKETFLAG_ERROR_PENDING | SOCKETFLAG_HANGUP_PENDING );
			sockbase->flags |= SOCKETFLAG_POLLED;
			sockbase->last_event = 0;

			pollobj->slots[ num_sockets ].sock = sockobj;
			pollobj->slots[ num_sockets ].base = sock->base;
			pollobj->slots[ num_sockets ].fd = sockbase->fd;

			if( sockbase->state == SOCKETSTATE_CONNECTING )
				_socket_poll_state( sockbase );

#if FOUNDATION_PLATFORM_APPLE
			pollobj->pollfds[ num_sockets ].fd = sockbase->fd;
			pollobj->pollfds[ num_sockets ].events = ( ( sockbase->state == SOCKETSTATE_CONNECTING ) ? POLLOUT : POLLIN ) | POLLERR | POLLHUP;
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
			struct epoll_event event;
			event.events = ( ( sockbase->state == SOCKETSTATE_CONNECTING ) ? EPOLLOUT : EPOLLIN ) | EPOLLERR | EPOLLHUP;
			event.data.fd = (int)pollobj->num_sockets;
			epoll_ctl( pollobj->fd_poll, EPOLL_CTL_ADD, sockbase->fd, &event );
#endif
			num_sockets = ++pollobj->num_sockets;

			if( sockbase->fd == SOCKET_INVALID )
			{
				network_event_post( NETWORKEVENT_HANGUP, sockobj );
				++num_events;
			}

			socket_destroy( sockobj );
		}
	}

	return num_events;
}


network_poll_t* network_poll_allocate( unsigned int num_sockets )
{
	network_poll_t* poll = memory_allocate( HASH_NETWORK, sizeof( network_poll_t ) + sizeof( network_poll_slot_t ) * num_sockets, 8, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED );
	poll->max_sockets = num_sockets;
#if FOUNDATION_PLATFORM_APPLE
	poll->pollfds = memory_allocate( HASH_NETWORK, sizeof( struct pollfd ) * num_sockets, 8, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED );
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	poll->fd_poll = epoll_create( num_sockets );
	poll->events = memory_allocate( HASH_NETWORK, sizeof( struct epoll_event ) * num_sockets, 8, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED );
#endif
	return poll;
}


void network_poll_deallocate( network_poll_t* pollobj )
{
	int iqueue;
	unsigned int islot;

	_network_poll_process_pending( pollobj );

	for( iqueue = 0; iqueue < BUILD_SIZE_POLL_QUEUE; ++iqueue )
	{
		if( pollobj->queue_add[iqueue] )
		{
			object_t sock = pollobj->queue_add[iqueue];
			pollobj->queue_add[iqueue] = 0;

			socket_destroy( sock );
		}
	}

	for( islot = 0; islot < pollobj->num_sockets; ++islot )
	{
		object_t sockobj = pollobj->slots[islot].sock;
		if( sockobj )
		{
			socket_t* sock = _socket_lookup( sockobj );
			if( sock )
			{
				FOUNDATION_ASSERT( sock->base >= 0 );
				_socket_base[ sock->base ].flags &= ~SOCKETFLAG_POLLED;
				socket_destroy( sockobj );
			}

			socket_destroy( sockobj );
		}
	}

#if FOUNDATION_PLATFORM_APPLE
	memory_deallocate( pollobj->pollfds );
#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	close( pollobj->fd_poll );
	memory_deallocate( pollobj->events );
#endif
	memory_deallocate( pollobj );
}


unsigned int network_poll_num_sockets( network_poll_t* pollobj )
{
	return pollobj->num_sockets;
}


void network_poll_sockets( network_poll_t* pollobj, object_t* sockets, unsigned int max_sockets )
{
	unsigned int is;
	unsigned int num_sockets = ( pollobj->num_sockets < max_sockets ) ? pollobj->num_sockets : max_sockets;
	for( is = 0; is < num_sockets; ++is )
		sockets[is] = pollobj->slots[is].sock;
}


unsigned int network_poll_timeout( network_poll_t* pollobj )
{
	return pollobj->timeout;
}


void network_poll_set_timeout( network_poll_t* pollobj, unsigned int timeoutms )
{
	pollobj->timeout = timeoutms;
}


bool network_poll_add_socket( network_poll_t* pollobj, object_t sock )
{
	int tries = 0;
	FOUNDATION_ASSERT( pollobj );
	FOUNDATION_ASSERT( sock );
	do
	{
		int iqueue;
		for( iqueue = 0; iqueue < BUILD_SIZE_POLL_QUEUE; ++iqueue )
		{
			if( !pollobj->queue_add[iqueue] && atomic_cas64( (atomic64_t*)( pollobj->queue_add + iqueue ), sock, 0 ) )
			{
				//Add reference
				socket_t* sockptr = _socket_lookup( sock );
				if( sockptr )
				{
					int jqueue;
					for( jqueue = 0; jqueue < BUILD_SIZE_POLL_QUEUE; ++jqueue )
					{
						if( pollobj->queue_remove[jqueue] == sock )
							atomic_cas64( (atomic64_t*)( pollobj->queue_remove + jqueue ), 0, sock );
					}
					return true;
				}

				pollobj->queue_add[iqueue] = 0;
				return false;
			}
		}
		if( !tries )
			log_warn( HASH_NETWORK, WARNING_PERFORMANCE, "Unable to add socket to poll, no free queue slots" );
		thread_sleep( pollobj->timeout );
	} while( ++tries < 32 );
	log_error( HASH_NETWORK, ERROR_OUT_OF_MEMORY, "Unable to add socket to poll, no free queue slots" );
	return false;
}


void network_poll_remove_socket( network_poll_t* pollobj, object_t sock )
{
	int tries = 0;
	FOUNDATION_ASSERT( pollobj );
	FOUNDATION_ASSERT( sock );
	do
	{
		int iqueue;
		for( iqueue = 0; iqueue < BUILD_SIZE_POLL_QUEUE; ++iqueue )
		{
			if( !pollobj->queue_remove[iqueue] && atomic_cas64( (atomic64_t*)( pollobj->queue_remove + iqueue ), sock, 0 ) )
			{
				int jqueue;
				for( jqueue = 0; jqueue < BUILD_SIZE_POLL_QUEUE; ++jqueue )
				{
					if( pollobj->queue_add[jqueue] == sock )
						atomic_cas64( (atomic64_t*)( pollobj->queue_add + jqueue ), 0, sock );
				}
				return;
			}
		}
		if( !tries )
			log_warn( HASH_NETWORK, WARNING_PERFORMANCE, "Unable to remove socket from poll, no free queue slots" );
		thread_sleep( pollobj->timeout );
	} while( ++tries < 32 );
	log_error( HASH_NETWORK, ERROR_OUT_OF_MEMORY, "Unable to remove socket from poll, no free queue slots" );
}


bool network_poll_has_socket( network_poll_t* pollobj, object_t sock )
{
	int iqueue;
	unsigned int islot;
	unsigned int num_sockets;
	FOUNDATION_ASSERT( pollobj );
	num_sockets = pollobj->num_sockets;
	for( islot = 0; islot < num_sockets; ++islot )
	{
		if( pollobj->slots[islot].sock == sock )
		{
			for( iqueue = 0; iqueue < BUILD_SIZE_POLL_QUEUE; ++iqueue )
			{
				if( pollobj->queue_remove[iqueue] == sock )
					return false;
			}
			return true;
		}
	}
	for( iqueue = 0; iqueue < BUILD_SIZE_POLL_QUEUE; ++iqueue )
	{
		if( pollobj->queue_add[iqueue] == sock )
			return true;
	}
	return false;
}


int network_poll( network_poll_t* pollobj )
{
	int avail = 0;

#if FOUNDATION_PLATFORM_WINDOWS
	//TODO: Refactor to keep fd_set across loop and rebuild on change (add/remove)
	int num_fd = 0;
	int ret = 0;
	unsigned int islot;
	fd_set fdread, fdwrite, fderr;
#endif

	int num_events = _network_poll_process_pending( pollobj );

	if( !pollobj->num_sockets )
		return num_events ? num_events : -1;

#if FOUNDATION_PLATFORM_APPLE

	int ret = poll( pollobj->pollfds, pollobj->num_sockets, pollobj->timeout );

#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID

	int ret = epoll_wait( pollobj->fd_poll, pollobj->events, pollobj->num_sockets + 1, pollobj->timeout );
	int num_polled = ret;

#elif FOUNDATION_PLATFORM_WINDOWS

	FD_ZERO( &fdread );
	FD_ZERO( &fdwrite );
	FD_ZERO( &fderr );

	for( islot = 0; islot < pollobj->num_sockets; ++islot )
	{
		int fd = pollobj->slots[islot].fd;

		FD_SET( fd, &fdread );
		FD_SET( fd, &fdwrite );
		FD_SET( fd, &fderr );

		if( fd >= num_fd )
			num_fd = fd + 1;
	}

	if( !num_fd )
	{
		return num_events ? num_events : -1;
	}
	else
	{
		struct timeval tv;

		tv.tv_sec  = pollobj->timeout / 1000;
		tv.tv_usec = ( pollobj->timeout % 1000 ) * 1000;

		ret = select( num_fd, &fdread, &fdwrite, &fderr, &tv );
	}

#else
#  error Not implemented
#endif

	if( ret < 0 )
	{
		log_warnf( HASH_NETWORK, WARNING_SUSPICIOUS, "Error in socket poll: %s", system_error_message( NETWORK_SOCKET_ERROR ) );
		if( !avail )
			return -2;
		ret = avail;
	}
	if( !avail && !ret )
		return num_events;

#if FOUNDATION_PLATFORM_APPLE

	struct pollfd* pfd = pollobj->pollfds;
	network_poll_slot_t* slot = pollobj->slots;
	for( unsigned int i = 0; i < pollobj->num_sockets; ++i, ++pfd, ++slot )
	{
		object_t sockobj = slot->sock;
		socket_base_t* sockbase = _socket_base + slot->base;
		int fd = slot->fd;
		if( pfd->revents & POLLIN )
		{
			if( sockbase->state == SOCKETSTATE_LISTENING )
			{
				if( !( sockbase->flags & SOCKETFLAG_CONNECTION_PENDING ) )
				{
					sockbase->flags |= SOCKETFLAG_CONNECTION_PENDING;
					network_event_post( NETWORKEVENT_CONNECTION, sockobj );
				}
			}
			else
			{
				int sockavail = _socket_available_fd( fd );
				if( sockavail > 0 )
				{
					if( sockavail != sockbase->last_event )
					{
						sockbase->last_event = sockavail;
						network_event_post( NETWORKEVENT_DATAIN, sockobj );
					}
				}
				else
				{
					if( !( sockbase->flags & SOCKETFLAG_HANGUP_PENDING ) )
					{
						sockbase->flags |= SOCKETFLAG_HANGUP_PENDING;
						network_event_post( NETWORKEVENT_HANGUP, sockobj );
					}
				}
			}
			++num_events;
		}
		if( ( sockbase->state == SOCKETSTATE_CONNECTING ) && ( pfd->revents & POLLOUT ) )
		{
			sockbase->state = SOCKETSTATE_CONNECTED;
			pfd->events = POLLIN | POLLERR | POLLHUP;
			network_event_post( NETWORKEVENT_CONNECTED, sockobj );
			++num_events;
		}
		if( pfd->revents & POLLERR )
		{
			socket_close( sockobj );
			pfd->events = POLLOUT | POLLERR | POLLHUP;
			if( !( sockbase->flags & SOCKETFLAG_ERROR_PENDING ) )
			{
				sockbase->flags |= SOCKETFLAG_ERROR_PENDING;
				network_event_post( NETWORKEVENT_ERROR, sockobj );
			}
			++num_events;
		}
		if( pfd->revents & POLLHUP )
		{
			socket_close( sockobj );
			pfd->events = POLLOUT | POLLERR | POLLHUP;
			if( !( sockbase->flags & SOCKETFLAG_HANGUP_PENDING ) )
			{
				sockbase->flags |= SOCKETFLAG_HANGUP_PENDING;
				network_event_post( NETWORKEVENT_HANGUP, sockobj );
			}
			++num_events;
		}
	}

#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID

	struct epoll_event* event = pollobj->events;
	for( int i = 0; i < num_polled; ++i, ++event )
	{
		FOUNDATION_ASSERT( pollobj->slots[ event->data.fd ].base >= 0 );

		object_t sockobj = pollobj->slots[ event->data.fd ].sock;
		socket_base_t* sockbase = _socket_base + pollobj->slots[ event->data.fd ].base;
		int fd = pollobj->slots[ event->data.fd ].fd;
		FOUNDATION_ASSERT( sockbase->object == sockobj ); //Sanity check, if this fails socket was deleted or reassigned without added/removed on poll object
		if( event->events & EPOLLIN )
		{
			if( sockbase->state == SOCKETSTATE_LISTENING )
			{
				if( !( sockbase->flags & SOCKETFLAG_CONNECTION_PENDING ) )
				{
					sockbase->flags |= SOCKETFLAG_CONNECTION_PENDING;
					network_event_post( NETWORKEVENT_CONNECTION, sockobj );
				}
			}
			else
			{
				int sockavail = _socket_available_fd( fd );
				if( sockavail > 0 )
				{
					if( sockavail != sockbase->last_event )
					{
						sockbase->last_event = sockavail;
						network_event_post( NETWORKEVENT_DATAIN, sockobj );
					}
				}
				else
				{
					if( !( sockbase->flags & SOCKETFLAG_HANGUP_PENDING ) )
					{
						sockbase->flags |= SOCKETFLAG_HANGUP_PENDING;
						network_event_post( NETWORKEVENT_HANGUP, sockobj );
					}
				}
			}
			++num_events;
		}
		if( ( sockbase->state == SOCKETSTATE_CONNECTING ) && ( event->events & EPOLLOUT ) )
		{
			sockbase->state = SOCKETSTATE_CONNECTED;
			struct epoll_event mod_event;
			mod_event.events = EPOLLIN | EPOLLERR | EPOLLHUP;
			mod_event.data.fd = event->data.fd;
			epoll_ctl( pollobj->fd_poll, EPOLL_CTL_MOD, fd, &mod_event );
			network_event_post( NETWORKEVENT_CONNECTED, sockobj );
			++num_events;
		}
		if( event->events & EPOLLERR )
		{
			socket_close( sockobj );
			struct epoll_event del_event;
			epoll_ctl( pollobj->fd_poll, EPOLL_CTL_DEL, fd, &del_event );
			if( !( sockbase->flags & SOCKETFLAG_ERROR_PENDING ) )
			{
				sockbase->flags |= SOCKETFLAG_ERROR_PENDING;
				network_event_post( NETWORKEVENT_ERROR, sockobj );
			}
			++num_events;
		}
		if( event->events & EPOLLHUP )
		{
			socket_close( sockobj );
			struct epoll_event del_event;
			epoll_ctl( pollobj->fd_poll, EPOLL_CTL_DEL, fd, &del_event );
			if( !( sockbase->flags & SOCKETFLAG_HANGUP_PENDING ) )
			{
				sockbase->flags |= SOCKETFLAG_HANGUP_PENDING;
				network_event_post( NETWORKEVENT_HANGUP, sockobj );
			}
			++num_events;
		}
	}

#elif FOUNDATION_PLATFORM_WINDOWS

	for( islot = 0; islot < pollobj->num_sockets; ++islot )
	{
		int fd = pollobj->slots[islot].fd;
		object_t sockobj = pollobj->slots[islot].sock;
		socket_base_t* sockbase = _socket_base + pollobj->slots[islot].base;
		FOUNDATION_ASSERT( sockbase->object == sockobj ); //Sanity check, if this fails socket was deleted or reassigned without added/removed on poll object

		if( FD_ISSET( fd, &fdread ) )
		{
			if( sockbase->state == SOCKETSTATE_LISTENING )
			{
				if( !( sockbase->flags & SOCKETFLAG_CONNECTION_PENDING ) )
				{
					sockbase->flags |= SOCKETFLAG_CONNECTION_PENDING;
					network_event_post( NETWORKEVENT_CONNECTION, sockobj );
				}
			}
			else //SOCKETSTATE_CONNECTED
			{
				int sockavail = _socket_available_fd( fd );
				if( ( sockbase->state == SOCKETSTATE_CONNECTED ) && ( sockavail > 0 ) )
				{
					if( sockavail != sockbase->last_event )
					{
						sockbase->last_event = sockavail;
						network_event_post( NETWORKEVENT_DATAIN, sockobj );
					}
				}
				else
				{
					if( !( sockbase->flags & SOCKETFLAG_HANGUP_PENDING ) )
					{
						sockbase->flags |= SOCKETFLAG_HANGUP_PENDING;
						network_event_post( NETWORKEVENT_HANGUP, sockobj );
					}
				}
			}
			++num_events;
		}
		if( ( sockbase->state == SOCKETSTATE_CONNECTING ) && FD_ISSET( fd, &fdwrite ) )
		{
			sockbase->state = SOCKETSTATE_CONNECTED;
			network_event_post( NETWORKEVENT_CONNECTED, sockobj );
			++num_events;
		}
		if( FD_ISSET( fd, &fderr ) )
		{
			socket_close( sockobj );
			if( !( sockbase->flags & SOCKETFLAG_HANGUP_PENDING ) )
			{
				sockbase->flags |= SOCKETFLAG_HANGUP_PENDING;
				network_event_post( NETWORKEVENT_HANGUP, sockobj );
			}
			++num_events;
		}
	}
#else
#  error Not implemented
#endif

	return num_events;
}


void* network_poll_thread( object_t thread, void* poll_raw )
{
	network_poll_t* pollobj = poll_raw;
	while( !thread_should_terminate( thread ) )
	{
		int ret = network_poll( pollobj );
		if( ( ret <= 0 ) && ( pollobj->timeout > 0 ) )
		{
			network_event_post( NETWORKEVENT_TIMEOUT, 0 );
			if( ret < 0 )
				thread_sleep( pollobj->timeout );
		}
		if( ret >= 0 )
		{
			thread_yield();
		}
	}
	return 0;
}

