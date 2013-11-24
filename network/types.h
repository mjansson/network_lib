/* types.h  -  Network library  -  Internal use only  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams.
 *
 * All rights reserved. No part of this library, code or built products may be used without
 * the explicit consent from Rampant Pixels AB
 * 
 */

#pragma once

/*! \file types.h
    Network abstraction on top of foundation streams */

#include <foundation/platform.h>
#include <foundation/types.h>

#include <network/build.h>


#if defined( NETWORK_COMPILE ) && NETWORK_COMPILE
#  ifdef __cplusplus
#  define NETWORK_EXTERN extern "C"
#  define NETWORK_API extern "C"
#  else
#  define NETWORK_EXTERN extern
#  define NETWORK_API extern
#  endif
#else
#  ifdef __cplusplus
#  define NETWORK_EXTERN extern "C"
#  define NETWORK_API extern "C"
#  else
#  define NETWORK_EXTERN extern
#  define NETWORK_API extern
#  endif
#endif


typedef enum _network_address_family
{
	NETWORK_ADDRESSFAMILY_IPV4     = 0,
	NETWORK_ADDRESSFAMILY_IPV6
} network_address_family_t;


typedef enum _socket_state
{
	SOCKETSTATE_NOTCONNECTED       = 0,
	SOCKETSTATE_CONNECTING,
	SOCKETSTATE_CONNECTED,
	SOCKETSTATE_LISTENING,
	SOCKETSTATE_DISCONNECTED
} socket_state_t;

typedef enum _network_event_id
{
	NETWORKEVENT_CONNECTION = 0,
	NETWORKEVENT_CONNECTED,
	NETWORKEVENT_DATAIN,
	NETWORKEVENT_ERROR,
	NETWORKEVENT_HANGUP,
	NETWORKEVENT_TIMEOUT
} network_event_id;

//! HTTP status codes
typedef enum _http_status
{
	HTTP_UNKNOWN                                             = 0,

	HTTP_CONTINUE                                            = 100,
	HTTP_SWITCHING_PROTOCOLS                                 = 101,

	HTTP_OK                                                  = 200,
	HTTP_CREATED                                             = 201,
	HTTP_ACCEPTED                                            = 202,
	HTTP_NON_AUTHORATIVE_INFORMATION                         = 203,
	HTTP_NO_CONTENT                                          = 204,
	HTTP_RESET_CONTENT                                       = 205,
	HTTP_PARTIAL_CONTENT                                     = 206,

	HTTP_MULTIPLE_CHOICES                                    = 300,
	HTTP_MOVED_PERMANENTLY                                   = 301,
	HTTP_FOUND                                               = 302,
	HTTP_SEE_OTHER                                           = 303,
	HTTP_NOT_MODIFIED                                        = 304,
	HTTP_USE_PROXY                                           = 305,
	HTTP_SWITCH_PROXY                                        = 306,
	HTTP_TEMPORARY_REDIRECT                                  = 307,

	HTTP_BAD_REQUEST                                         = 400,
	HTTP_UNAUTHORIZED                                        = 401,
	HTTP_PAYMENT_REQUIRED                                    = 402,
	HTTP_FORBIDDEN                                           = 403,
	HTTP_METHOD_NOT_ALLOWED                                  = 405,
	HTTP_NOT_ACCEPTABLE                                      = 406,
	HTTP_PROXY_AUTHENTICATION_REQUIRED                       = 407,
	HTTP_REQUEST_TIMEOUT                                     = 408,
	HTTP_CONFLICT                                            = 409,
	HTTP_GONE                                                = 410,
	HTTP_LENGTH_REQUIRED                                     = 411,
	HTTP_PRECONDITION_FAILED                                 = 412,
	HTTP_REQUEST_ENTITY_TOO_LARGE                            = 413,
	HTTP_REQUEST_URI_TOO_LONG                                = 414,
	HTTP_UNSUPPORTED_MEDIA_TYPE                              = 415,
	HTTP_REQUEST_RANGE_NOT_SATISFIABLE                       = 416,
	HTTP_EXPECTATION_FAILED                                  = 417,

	HTTP_INTERNAL_SERVER_ERROR                               = 500,
	HTTP_NOT_IMPLEMENTED                                     = 501,
	HTTP_BAD_GATEWAY                                         = 502,
	HTTP_SERVICE_UNAVAILABLE                                 = 503,
	HTTP_GATEWAY_TIMEOUT                                     = 504,
	HTTP_VERSION_NOT_SUPPORTED                               = 505
} http_status_t;

typedef struct _network_address     network_address_t;
typedef struct _network_poll        network_poll_t;

