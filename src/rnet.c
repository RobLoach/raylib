/*********************************************************************************************
*
*   rnet - A simple and easy-to-use network module for raylib
*
*   FEATURES:
*       - Provides a simple and (hopefully) easy to use wrapper around the Berkeley socket API
*
*   DEPENDENCIES:
*       raylib.h    - TraceLog
*       rnet.h      - platform-specific network includes
*
*   CONTRIBUTORS:
*       Jak Barnes (github: @syphonx) (Feb. 2019) - Initial version
*
*
*   LICENSE: zlib/libpng
*
*   Copyright (c) 2019 Jak Barnes (github: @syphonx) and Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

//----------------------------------------------------------------------------------
// Check if config flags have been externally provided on compilation line
//----------------------------------------------------------------------------------

#include "rnet.h"

#include "raylib.h"

#include <assert.h> // Required for: assert()
#include <stdio.h>  // Required for: FILE, fopen(), fclose(), fread()
#include <stdlib.h> // Required for: malloc(), free()
#include <string.h> // Required for: strcmp(), strncmp()

//----------------------------------------------------------------------------------
// Module defines
//----------------------------------------------------------------------------------

#define NET_DEBUG_ENABLED (1)

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------

typedef struct _SocketAddress
{
	struct sockaddr address;
} _SocketAddress;

typedef struct _SocketAddressIPv4
{
	struct sockaddr_in address;
} _SocketAddressIPv4;

typedef struct _SocketAddressIPv6
{
	struct sockaddr_in6 address;
} _SocketAddressIPv6;

typedef struct _SocketAddressStorage
{
	struct sockaddr_storage address;
} _SocketAddressStorage;

typedef struct _AddressInformation
{
	struct addrinfo addr;
} _AddressInformation;



//----------------------------------------------------------------------------------
// Global module forward declarations
//----------------------------------------------------------------------------------

static void        PrintSocket(struct sockaddr_storage *addr, const int family, const int socktype, const int protocol);
static const char *SocketAddressToString(struct sockaddr_storage *sockaddr);
static bool        IsIPv4Address(const char *ip);
static bool        IsIPv6Address(const char *ip);
static void *      GetSocketPortPtr(struct sockaddr_storage *sa);
static void *      GetSocketAddressPtr(struct sockaddr_storage *sa);
static bool        IsSocketValid(Socket *sock);
static void        SocketSetLastError(int err);
static int         SocketGetLastError();
static char *      SocketGetLastErrorString();
static char *      SocketErrorCodeToString(int err);
static bool        SocketSetDefaults(SocketConfig *config);
static bool        InitSocket(Socket *sock, struct addrinfo *addr);
static bool        CreateSocket(SocketConfig *config, SocketResult *outresult);
static bool        SocketSetBlocking(Socket *sock);
static bool        SocketSetNonBlocking(Socket *sock);
static bool        SocketSetOptions(SocketConfig *config, Socket *sock);
static void        SocketSetHints(SocketConfig *config, struct addrinfo *hints);

//----------------------------------------------------------------------------------
// Global module implementation
//----------------------------------------------------------------------------------

// Print socket information
static void PrintSocket(struct sockaddr_storage *addr, const int family, const int socktype, const int protocol)
{
	switch (family)
	{
		case AF_UNSPEC: { TraceLog(LOG_DEBUG, "\tFamily: Unspecified");
		}
		break;
		case AF_INET:
		{
			TraceLog(LOG_DEBUG, "\tFamily: AF_INET (IPv4)");
			TraceLog(LOG_INFO, "\t- IPv4 address %s", SocketAddressToString(addr));
		}
		break;
		case AF_INET6:
		{
			TraceLog(LOG_DEBUG, "\tFamily: AF_INET6 (IPv6)");
			TraceLog(LOG_INFO, "\t- IPv6 address %s", SocketAddressToString(addr));
		}
		break;
		case AF_NETBIOS:
		{
			TraceLog(LOG_DEBUG, "\tFamily: AF_NETBIOS (NetBIOS)");
		}
		break;
		default: { TraceLog(LOG_DEBUG, "\tFamily: Other %ld", family);
		}
		break;
	}
	TraceLog(LOG_DEBUG, "\tSocket type:");
	switch (socktype)
	{
		case 0: TraceLog(LOG_DEBUG, "\t- Unspecified"); break;
		case SOCK_STREAM:
			TraceLog(LOG_DEBUG, "\t- SOCK_STREAM (stream)");
			break;
		case SOCK_DGRAM:
			TraceLog(LOG_DEBUG, "\t- SOCK_DGRAM (datagram)");
			break;
		case SOCK_RAW: TraceLog(LOG_DEBUG, "\t- SOCK_RAW (raw)"); break;
		case SOCK_RDM:
			TraceLog(LOG_DEBUG, "\t- SOCK_RDM (reliable message datagram)");
			break;
		case SOCK_SEQPACKET:
			TraceLog(LOG_DEBUG, "\t- SOCK_SEQPACKET (pseudo-stream packet)");
			break;
		default: TraceLog(LOG_DEBUG, "\t- Other %ld", socktype); break;
	}
	TraceLog(LOG_DEBUG, "\tProtocol:");
	switch (protocol)
	{
		case 0: TraceLog(LOG_DEBUG, "\t- Unspecified"); break;
		case IPPROTO_TCP: TraceLog(LOG_DEBUG, "\t- IPPROTO_TCP (TCP)"); break;
		case IPPROTO_UDP: TraceLog(LOG_DEBUG, "\t- IPPROTO_UDP (UDP)"); break;
		default: TraceLog(LOG_DEBUG, "\t- Other %ld", protocol); break;
	}
}

// Convert network ordered socket address to human readable string (127.0.0.1)
static const char *SocketAddressToString(struct sockaddr_storage *sockaddr)
{
	static const char* ipv6[INET6_ADDRSTRLEN];
	assert(sockaddr != NULL);
	assert(sockaddr->ss_family == AF_INET || sockaddr->ss_family == AF_INET6);
	switch (sockaddr->ss_family)
	{
		case AF_INET:
		{
			struct sockaddr_in *s = ((struct sockaddr_in *) sockaddr);
			return inet_ntop(AF_INET, &s->sin_addr, ipv6, INET_ADDRSTRLEN);
		}
		break;
		case AF_INET6:
		{
			struct sockaddr_in6 *s = ((struct sockaddr_in6 *) sockaddr);
			return inet_ntop(AF_INET6, &s->sin6_addr, ipv6, INET6_ADDRSTRLEN);
		}
		break;
	}
	return NULL;
}

// Check if the null terminated string ip is a valid IPv4 address
static bool IsIPv4Address(const char *ip)
{
	struct sockaddr_in sa;
	int                result = inet_pton(AF_INET, ip, &(sa.sin_addr));
	return result != 0;
}

// Check if the null terminated string ip is a valid IPv6 address
static bool IsIPv6Address(const char *ip)
{
	struct sockaddr_in6 sa;
	int                 result = inet_pton(AF_INET6, ip, &(sa.sin6_addr));
	return result != 0;
}

// Return a pointer to the port from the correct address family (IPv4, or IPv6)
static void *GetSocketPortPtr(struct sockaddr_storage *sa)
{
	if (sa->ss_family == AF_INET)
	{
		return &(((struct sockaddr_in *) sa)->sin_port);
	}

	return &(((struct sockaddr_in6 *) sa)->sin6_port);
}

// Return a pointer to the address from the correct address family (IPv4, or IPv6)
static void *GetSocketAddressPtr(struct sockaddr_storage *sa)
{
	if (sa->ss_family == AF_INET)
	{
		return &(((struct sockaddr_in *) sa)->sin_addr);
	}

	return &(((struct sockaddr_in6 *) sa)->sin6_addr);
}

// Is the socket in a valid state?
static bool IsSocketValid(Socket *sock)
{
	if (sock != NULL)
	{
		return (sock->channel != INVALID_SOCKET);
	}
	return false;
}

// Sets the error code that can be retrieved through the WSAGetLastError function.
static void SocketSetLastError(int err)
{
#if defined(_WIN32)
	WSASetLastError(err);
#else
	errno = err;
#endif
}

// Returns the error status for the last Sockets operation that failed
static int SocketGetLastError()
{
#if defined(_WIN32)
	return WSAGetLastError();
#else
	return errno;
#endif
}

// Returns a human-readable string representing the last error message
static char *SocketGetLastErrorString()
{
	return SocketErrorCodeToString(SocketGetLastError());
}

// Returns a human-readable string representing the error message (err)
static char *SocketErrorCodeToString(int err)
{
#if defined(_WIN32)
	static char gaiStrErrorBuffer[GAI_STRERROR_BUFFER_SIZE];
	sprintf(gaiStrErrorBuffer, "%ws", gai_strerror(err));
	return gaiStrErrorBuffer;
#else
	return gai_strerror(err);
#endif
}

// Set the defaults in the supplied SocketConfig if they're not already set
static bool SocketSetDefaults(SocketConfig *config)
{
	if (config->backlog_size == 0)
	{
		config->backlog_size = SOCKET_MAX_QUEUE_SIZE;
	}

	return true;
}

// Create the socket channel
static bool InitSocket(Socket *sock, struct addrinfo *addr)
{
	switch (sock->type)
	{
		case SOCKET_TCP:
			if (addr->ai_family == AF_INET)
			{
				sock->channel = socket(AF_INET, SOCK_STREAM, 0);
			}
			else
			{
				sock->channel = socket(AF_INET6, SOCK_STREAM, 0);
			}
			break;
		case SOCKET_UDP:
			if (addr->ai_family == AF_INET)
			{
				sock->channel = socket(AF_INET, SOCK_DGRAM, 0);
			}
			else
			{
				sock->channel = socket(AF_INET6, SOCK_DGRAM, 0);
			}
			break;
		default: 
			TraceLog(LOG_WARNING, "Invalid socket type specified.");
			break;
	}
	return IsSocketValid(sock);
}

//	CreateSocket() - Interally called by CreateSocket()
//
//	This here is the bread and butter of the socket API, This function will
//	attempt to open a socket, bind and listen to it based on the config passed in
//
//	SocketConfig* config - Configuration for which socket to open
//	SocketResult* result - The results of this function (if any, including errors)
//
//	e.g.
//	SocketConfig server_config = {				SocketConfig client_config = {
//		.host        = "127.0.0.1",				.host = "127.0.0.1",
//		.port        = 8080,					.port = 8080,
//		.server      = true,				};
//		.nonblocking = true,
//	};
//	SocketResult server_res;				SocketResult client_res;
static bool CreateSocket(SocketConfig *config, SocketResult *outresult)
{
	bool             success = true;
	int              addrstatus;
	struct addrinfo  hints; // Address flags (IPV4, IPV6, UDP?)
	struct addrinfo *res;   // A pointer to the resulting address list
	outresult->socket->channel = INVALID_SOCKET;
	outresult->status          = RESULT_FAILURE;

	// Set the socket type
	outresult->socket->type = config->type;

	//	Set the hints based on information in the config
	//
	//	AI_CANONNAME	Causes the ai_canonname of the result to the filled  out with the host's canonical (real) name.
	//	AI_PASSIVE:		Causes the result's IP address to be filled out with INADDR_ANY (IPv4)or in6addr_any (IPv6);
	//	Note:	This causes a subsequent call to bind() to auto-fill the IP address
	//			of the struct sockaddr with the address of the current host.
	//
	SocketSetHints(config, &hints);

	// Populate address information
	addrstatus = getaddrinfo(config->host, // e.g. "www.example.com" or IP (Can be null if AI_PASSIVE flag is set
							 config->port, // e.g. "http" or port number
							 &hints,       // e.g. SOCK_STREAM/SOCK_DGRAM
							 &res          // The struct to populate
	);

	// Did we succeed?
	if (addrstatus != 0)
	{
		outresult->socket->status = SocketGetLastError();
		TraceLog(LOG_WARNING,
				 "Socket Error: %s",
				 SocketErrorCodeToString(outresult->socket->status));
		SocketSetLastError(0);
		TraceLog(LOG_WARNING,
				 "Failed to get resolve host %s:%s: %s",
				 config->host,
				 config->port,
				 SocketGetLastErrorString());
		return (success = false);
	}
	else
	{
		char      hoststr[NI_MAXHOST];
		char      portstr[NI_MAXSERV];
		socklen_t client_len = sizeof(struct sockaddr_storage);
		int       rc         = getnameinfo((struct sockaddr *) res->ai_addr, client_len, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
		TraceLog(LOG_INFO, "Successfully resolved host %s:%s", hoststr, portstr);
	}

	// Walk the address information linked-list
	struct addrinfo *it;
	for (it = res; it != NULL; it = it->ai_next)
	{
		// Initialise the socket
		if (!InitSocket(outresult->socket, it))
		{
			outresult->socket->status = SocketGetLastError();
			TraceLog(LOG_WARNING,
					 "Socket Error: %s",
					 SocketErrorCodeToString(outresult->socket->status));
			SocketSetLastError(0);
			continue;
		}

		// Set socket options
		if (!SocketSetOptions(config, outresult->socket))
		{
			outresult->socket->status = SocketGetLastError();
			TraceLog(LOG_WARNING,
					 "Socket Error: %s",
					 SocketErrorCodeToString(outresult->socket->status));
			SocketSetLastError(0);
			freeaddrinfo(res);
			return (success = false);
		}
	}

	if (!IsSocketValid(outresult->socket))
	{
		outresult->socket->status = SocketGetLastError();
		TraceLog(
			LOG_WARNING, "Socket Error: %s", SocketErrorCodeToString(outresult->status));
		SocketSetLastError(0);
		freeaddrinfo(res);
		return (success = false);
	}

	if (success)
	{
		outresult->status         = RESULT_SUCCESS;
		outresult->socket->ready  = 0;
		outresult->socket->status = 0;
		if (!(config->type == SOCKET_UDP))
		{
			outresult->socket->isServer = config->server;
		}
		switch (res->ai_addr->sa_family)
		{
			case AF_INET:
			{
				outresult->socket->addripv4 = (struct _SocketAddressIPv4 *) malloc(
					sizeof(*outresult->socket->addripv4));
				if (outresult->socket->addripv4 != NULL)
				{
					memset(outresult->socket->addripv4, 0,
						   sizeof(*outresult->socket->addripv4));
					if (outresult->socket->addripv4 != NULL)
					{
						memcpy(&outresult->socket->addripv4->address,
							   (struct sockaddr_in *) res->ai_addr, sizeof(struct sockaddr_in));
						outresult->socket->isIPv6 = false;
						char      hoststr[NI_MAXHOST];
						char      portstr[NI_MAXSERV];
						socklen_t client_len = sizeof(struct sockaddr_storage);
						getnameinfo(
							(struct sockaddr *) &outresult->socket->addripv4->address, client_len, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
						TraceLog(LOG_INFO, "Socket address set to %s:%s", hoststr, portstr);
					}
				}
			}
			break;
			case AF_INET6:
			{
				outresult->socket->addripv6 = (struct _SocketAddressIPv6 *) malloc(
					sizeof(*outresult->socket->addripv6));
				if (outresult->socket->addripv6 != NULL)
				{
					memset(outresult->socket->addripv6, 0,
						   sizeof(*outresult->socket->addripv6));
					if (outresult->socket->addripv6 != NULL)
					{
						memcpy(&outresult->socket->addripv6->address,
							   (struct sockaddr_in6 *) res->ai_addr, sizeof(struct sockaddr_in6));
						outresult->socket->isIPv6 = true;
						char      hoststr[NI_MAXHOST];
						char      portstr[NI_MAXSERV];
						socklen_t client_len = sizeof(struct sockaddr_storage);
						getnameinfo(
							(struct sockaddr *) &outresult->socket->addripv6->address, client_len, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
						TraceLog(LOG_INFO, "Socket address set to %s:%s", hoststr, portstr);
					}
				}
			}
			break;
		}
	}
	freeaddrinfo(res);
	return success;
}

// Set the state of the Socket sock to blocking
static bool SocketSetBlocking(Socket *sock)
{
	bool ret = true;
#if defined(_WIN32)
	unsigned long mode = 0;
	ret                = ioctlsocket(sock->channel, FIONBIO, &mode);
#else
	const int flags = fcntl(sock->channel, F_GETFL, 0);
	if (!(flags & O_NONBLOCK))
	{
		TraceLog(LOG_DEBUG, "Socket was already in blocking mode");
		return ret;
	}

	ret = (0 == fcntl(sock->channel, F_SETFL, (flags ^ O_NONBLOCK)));
#endif
	return ret;
}

// Set the state of the Socket sock to non-blocking
static bool SocketSetNonBlocking(Socket *sock)
{
	bool ret = true;
#if defined(_WIN32)
	unsigned long mode = 1;
	ret                = ioctlsocket(sock->channel, FIONBIO, &mode);
#else
	const int flags = fcntl(sock->channel, F_GETFL, 0);
	if ((flags & O_NONBLOCK))
	{
		TraceLog(LOG_DEBUG, "Socket was already in non-blocking mode");
		return ret;
	}
	ret = (0 == fcntl(sock->channel, F_SETFL, (flags | O_NONBLOCK)));
#endif
	return ret;
}

// Set options specified in SocketConfig to Socket sock
static bool SocketSetOptions(SocketConfig *config, Socket *sock)
{
	for (int i = 0; i < SOCKET_MAX_SOCK_OPTS; i++)
	{
		SocketOpt *opt = &config->sockopts[i];
		if (opt->id == 0)
		{
			break;
		}

		if (setsockopt(sock->channel, SOL_SOCKET, opt->id, opt->value, opt->valueLen) < 0)
		{
			return false;
		}
	}

	return true;
}

// Set "hints" in an addrinfo struct, to be passed to getaddrinfo.
static void SocketSetHints(SocketConfig *config, struct addrinfo *hints)
{
	if (config == NULL || hints == NULL)
	{
		return;
	}
	memset(hints, 0, sizeof(*hints));

	// Check if the ip supplied in the config is a valid ipv4 ip ipv6 address
	if (IsIPv4Address(config->host))
	{
		hints->ai_family = AF_INET;
		hints->ai_flags |= AI_NUMERICHOST;
	}
	else
	{
		if (IsIPv6Address(config->host))
		{
			hints->ai_family = AF_INET6;
			hints->ai_flags |= AI_NUMERICHOST;
		}
		else
		{
			hints->ai_family = AF_UNSPEC;
		}
	}

	if (config->type == SOCKET_UDP)
	{
		hints->ai_socktype = SOCK_DGRAM;
	}
	else
	{
		hints->ai_socktype = SOCK_STREAM;
	}

	// Set passive unless UDP client
	if (!(config->type == SOCKET_UDP) || config->server)
	{
		hints->ai_flags = AI_PASSIVE;
	}
}

//----------------------------------------------------------------------------------
// Module implementation
//----------------------------------------------------------------------------------

//	Initialise the network (requires for windows platforms only)
bool InitNetwork()
{
#if defined(_WIN32)
	WORD    wVersionRequested;
	WSADATA wsaData;
	int     err;

	wVersionRequested = MAKEWORD(2, 2);
	err               = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0)
	{
		TraceLog(LOG_WARNING, "WinSock failed to initialise.");
		return false;
	}
	else
	{
		TraceLog(LOG_INFO, "WinSock initialised.");
	}

	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		TraceLog(LOG_WARNING, "WinSock failed to initialise.");
		WSACleanup();
		return false;
	}

	return true;
#else
	return true;
#endif
}

//	Cleanup, and close the network
void CloseNetwork()
{
#if defined(_WIN32)
	WSACleanup();
#endif
}

//	Protocol-independent name resolution from an address to an ANSI host name
//	and from a port number to the ANSI service name.
//
//	The flags parameter can be used to customize processing of the getnameinfo function
//
//	The following flags are available:
//
//	NAME_INFO_DEFAULT		0x00	// No flags set
//	NAME_INFO_NOFQDN		0x01	// Only return nodename portion for local hosts
//	NAME_INFO_NUMERICHOST	0x02	// Return numeric form of the host's address
//	NAME_INFO_NAMEREQD		0x04	// Error if the host's name not in DNS
//	NAME_INFO_NUMERICSERV	0x08	// Return numeric form of the service (port #)
//	NAME_INFO_DGRAM			0x10	// Service is a datagram service
void ResolveIP(const char *ip, const char *port, int flags, char *host, char *serv)
{
	// Variables
	int              status; // Status value to return (0) is success
	struct addrinfo  hints;  // Address flags (IPV4, IPV6, UDP?)
	struct addrinfo *res;    // A pointer to the resulting address list

	// Set the hints
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // Either IPv4 or IPv6 (AF_INET, AF_INET6)
	hints.ai_protocol = 0; // Automatically select correct protocol (IPPROTO_TCP), (IPPROTO_UDP)

	// Populate address information
	status = getaddrinfo(ip,     // e.g. "www.example.com" or IP
						 port,   // e.g. "http" or port number
						 &hints, // e.g. SOCK_STREAM/SOCK_DGRAM
						 &res    // The struct to populate
	);

	// Did we succeed?
	if (status != 0)
	{
		TraceLog(LOG_WARNING, "Failed to get resolve host %s:%s: %s", ip, port, gai_strerror(errno));
	}
	else
	{
		TraceLog(LOG_DEBUG, "Resolving... %s::%s", ip, port);
	}

	// Attempt to resolve network byte order ip to hostname
	switch (res->ai_family)
	{
		case AF_INET:
			status = getnameinfo(&*((struct sockaddr *) res->ai_addr),
								 sizeof(*((struct sockaddr_in *) res->ai_addr)),
								 host,
								 NI_MAXHOST,
								 serv,
								 NI_MAXSERV,
								 flags);
			break;
		case AF_INET6:
			status = getnameinfo(&*((struct sockaddr_in6 *) res->ai_addr),
								 sizeof(*((struct sockaddr_in6 *) res->ai_addr)),
								 host,
								 NI_MAXHOST,
								 serv,
								 NI_MAXSERV,
								 flags);
			break;
		default: break;
	}

	if (status != 0)
	{
		TraceLog(LOG_WARNING, "Failed to resolve ip %s: %s", ip, SocketGetLastErrorString());
	}
	else
	{
		TraceLog(LOG_DEBUG, "Successfully resolved %s::%s to %s", ip, port, host);
	}

	// Free the pointer to the data returned by addrinfo
	freeaddrinfo(res);
}

//	Protocol-independent translation from an ANSI host name to an address
//
//	e.g.
//	const char* address	= "127.0.0.1" (local address)
//	const char* port	= "80"
//
//  Parameters:
//      const char* address - A pointer to a NULL-terminated ANSI string that contains a host (node) name or a numeric host address string.
//      const char* service - A pointer to a NULL-terminated ANSI string that contains either a service name or port number represented as a string.
//
//	Returns:
//		The total amount of addresses found, -1 on error
//
int ResolveHost(const char *address, const char *service, int addressType, int flags, AddressInformation *outAddr)
{
	// Variables
	int              status; // Status value to return (0) is success
	struct addrinfo  hints;  // Address flags (IPV4, IPV6, UDP?)
	struct addrinfo *res;    // will point to the results
	struct addrinfo *iterator;
	assert(((address != NULL || address != 0) || (service != NULL || service != 0)));
	assert(((addressType == AF_INET) || (addressType == AF_INET6) || (addressType == AF_UNSPEC)));

	// Set the hints
	memset(&hints, 0, sizeof hints);
	hints.ai_family = addressType; // Either IPv4 or IPv6 (ADDRESS_TYPE_IPV4, ADDRESS_TYPE_IPV6)
	hints.ai_protocol = 0; // Automatically select correct protocol (IPPROTO_TCP), (IPPROTO_UDP)
	hints.ai_flags = flags;
	assert(hints.ai_addrlen == NULL || hints.ai_addrlen == 0);
	assert(hints.ai_canonname == NULL || hints.ai_canonname == 0);
	assert(hints.ai_addr == NULL || hints.ai_addr == 0);
	assert(hints.ai_next == NULL || hints.ai_next == 0);

	// When the address is NULL, populate the IP for me
	if (address == NULL)
	{
		if ((hints.ai_flags & AI_PASSIVE) == 0)
		{
			hints.ai_flags |= AI_PASSIVE;
		}
	}

	TraceLog(LOG_INFO, "Resolving host...");

	// Populate address information
	status = getaddrinfo(address, // e.g. "www.example.com" or IP
						 service, // e.g. "http" or port number
						 &hints,  // e.g. SOCK_STREAM/SOCK_DGRAM
						 &res     // The struct to populate
	);

	// Did we succeed?
	if (status != 0)
	{
		int error = SocketGetLastError();
		SocketSetLastError(0);
		TraceLog(LOG_WARNING, "Failed to get resolve host: %s", SocketErrorCodeToString(error));
		return -1;
	}
	else
	{
		TraceLog(LOG_INFO, "Successfully resolved host %s:%s", address, service);
	}

	// Calculate the size of the address information list
	int size = 0;
	for (iterator = res; iterator != NULL; iterator = iterator->ai_next)
	{
		size++;
	}

	// Validate the size is > 0, otherwise return
	if (size <= 0)
	{
		TraceLog(LOG_WARNING, "Error, no addresses found.");
		return -1;
	}

	// If not address list was allocated, allocate it dynamically with the known address size
	if (outAddr == NULL)
	{
		outAddr = (AddressInformation *) malloc(size * sizeof(AddressInformation));
	}

	// Dynamically allocate an array of address information structs
	if (outAddr != NULL)
	{
		int i;
		for (i = 0; i < size; ++i)
		{
			outAddr[i] = AllocAddress();
			if (outAddr[i] == NULL)
			{
				break;
			}
		}
		outAddr[i] = NULL;
		if (i != size)
		{
			outAddr = NULL;
		}
	}
	else
	{
		TraceLog(LOG_WARNING,
				 "Error, failed to dynamically allocate memory for the address list");
		return -1;
	}

	// Copy all the address information from res into outAddrList
	int i = 0;
	for (iterator = res; iterator != NULL; iterator = iterator->ai_next)
	{
		if (i < size)
		{
			outAddr[i]->addr.ai_flags    = iterator->ai_flags;
			outAddr[i]->addr.ai_family   = iterator->ai_family;
			outAddr[i]->addr.ai_socktype = iterator->ai_socktype;
			outAddr[i]->addr.ai_protocol = iterator->ai_protocol;
			outAddr[i]->addr.ai_addrlen  = iterator->ai_addrlen;
			*outAddr[i]->addr.ai_addr    = *iterator->ai_addr;
#if NET_DEBUG_ENABLED
			TraceLog(LOG_DEBUG, "GetAddressInformation");
			TraceLog(LOG_DEBUG, "\tFlags: 0x%x", iterator->ai_flags);
			PrintSocket(outAddr[i]->addr.ai_addr,
						outAddr[i]->addr.ai_family,
						outAddr[i]->addr.ai_socktype,
						outAddr[i]->addr.ai_protocol);
			TraceLog(LOG_DEBUG, "Length of this sockaddr: %d", outAddr[i]->addr.ai_addrlen);
			TraceLog(LOG_DEBUG, "Canonical name: %s", iterator->ai_canonname);
#endif
			i++;
		}
	}

	// Free the pointer to the data returned by addrinfo
	freeaddrinfo(res);

	// Return the total count of addresses found
	return size;
}

//	This here is the bread and butter of the socket API, This function will
//	attempt to open a socket, bind and listen to it based on the config passed in
//
//	SocketConfig* config - Configuration for which socket to open
//	SocketResult* result - The results of this function (if any, including errors)
//
//	e.g.
//	SocketConfig server_config = {			SocketConfig client_config = {
//		.host        = "127.0.0.1",			    .host = "127.0.0.1",
//		.port        = 8080,				    .port = 8080,
//		.server      = true,				};
//		.nonblocking = true,
//	};
//	SocketResult server_res;				SocketResult client_res;
bool SocketCreate(SocketConfig *config, SocketResult *result)
{
	// Socket creation result
	bool success = true;

	// Make sure we've not received a null config or result pointer
	if (config == NULL || result == NULL)
	{
		return (success = false);
	}

	// Set the defaults based on the config
	if (!SocketSetDefaults(config))
	{
		TraceLog(LOG_WARNING, "Configuration Error.");
		success = false;
	}
	else
	{
		// Create the socket
		if (CreateSocket(config, result))
		{
			if (config->nonblocking)
			{
				SocketSetNonBlocking(result->socket);
			}
			else
			{
				SocketSetBlocking(result->socket);
			}
		}
		else
		{
			success = false;
		}
	}
	return success;
}

// Bind a socket to a local address
// Note: The bind function is required on an unconnected socket before subsequent calls to the listen function.
bool SocketBind(SocketConfig *config, SocketResult *result)
{
	bool success                       = false;
	result->status                     = RESULT_FAILURE;
	struct sockaddr_storage *sock_addr = NULL;

	// Don't bind to a socket that isn't configured as a server
	if (!IsSocketValid(result->socket) || !config->server)
	{
		TraceLog(LOG_WARNING,
				 "Cannot bind to socket marked as \"Client\" in SocketConfig.");
		success = false;
	}
	else
	{
		if (result->socket->isIPv6)
		{
			sock_addr = (struct sockaddr_storage *) &result->socket->addripv6->address;
		}
		else
		{
			sock_addr = (struct sockaddr_storage *) &result->socket->addripv4->address;
		}
		if (sock_addr != NULL)
		{
			if (bind(result->socket->channel, (struct sockaddr *) sock_addr, sizeof(*sock_addr)) != SOCKET_ERROR)
			{
				TraceLog(LOG_INFO, "Successfully bound socket.");
				success = true;
			}
			else
			{
				result->socket->status = SocketGetLastError();
				TraceLog(LOG_WARNING, "Socket Error: %s",
						 SocketErrorCodeToString(result->socket->status));
				SocketSetLastError(0);
				success = false;
			}
		}
	}
	// Was the bind a success?
	if (success)
	{
		result->status         = RESULT_SUCCESS;
		result->socket->ready  = 0;
		result->socket->status = 0;
		socklen_t sock_len     = sizeof(*sock_addr);
		if (getsockname(result->socket->channel, (struct sockaddr *) sock_addr, &sock_len) < 0)
		{
			TraceLog(LOG_WARNING, "Couldn't get socket address");
		}
		else
		{
			struct sockaddr_in *s = (struct sockaddr_in *) sock_addr;
			// result->socket->address.host = s->sin_addr.s_addr;
			// result->socket->address.port = s->sin_port;

			//
			result->socket->addripv4
				= (struct _SocketAddressIPv4 *) malloc(sizeof(*result->socket->addripv4));
			if (result->socket->addripv4 != NULL)
			{
				memset(result->socket->addripv4, 0, sizeof(*result->socket->addripv4));
			}
			memcpy(&result->socket->addripv4->address, (struct sockaddr_in *) &s->sin_addr, sizeof(struct sockaddr_in));
			//
		}
	}
	return success;
}

// Listens (and queues) incoming connections requests for a bound port.
bool SocketListen(SocketConfig *config, SocketResult *result)
{
	bool success   = false;
	result->status = RESULT_FAILURE;

	// Don't bind to a socket that isn't configured as a server
	if (!IsSocketValid(result->socket) || !config->server)
	{
		TraceLog(LOG_WARNING,
				 "Cannot listen on socket marked as \"Client\" in SocketConfig.");
		success = false;
	}
	else
	{
		// Don't listen on UDP sockets
		if (!(config->type == SOCKET_UDP))
		{
			if (listen(result->socket->channel, config->backlog_size) != SOCKET_ERROR)
			{
				TraceLog(LOG_INFO, "Started listening on socket...");
				success = true;
			}
			else
			{
				success = false;
				result->socket->status = SocketGetLastError();
				TraceLog(LOG_WARNING, "Socket Error: %s",
						 SocketErrorCodeToString(result->socket->status));
				SocketSetLastError(0);
			}
		}
		else
		{
			TraceLog(LOG_WARNING,
					 "Cannot listen on socket marked as \"UDP\" (datagram) in SocketConfig.");
			success = false;
		}
	}

	// Was the listen a success?
	if (success)
	{
		result->status         = RESULT_SUCCESS;
		result->socket->ready  = 0;
		result->socket->status = 0;
	}
	return success;
}

// Connect the socket to the destination specified by "host" and "port" in SocketConfig
bool SocketConnect(SocketConfig *config, SocketResult *result)
{
	bool success   = true;
	result->status = RESULT_FAILURE;

	// Only bind to sockets marked as server
	if (config->server)
	{
		TraceLog(LOG_WARNING,
				 "Cannot connect to socket marked as \"Server\" in SocketConfig.");
		success = false;
	}
	else
	{
		if (IsIPv4Address(config->host))
		{
			struct sockaddr_in ip4addr;
			ip4addr.sin_family = AF_INET;
			unsigned long hport;
			hport            = strtoul(config->port, NULL, 0);
			ip4addr.sin_port = htons(hport);
			inet_pton(AF_INET, config->host, &ip4addr.sin_addr);
			int connect_result = connect(result->socket->channel, (struct sockaddr *) &ip4addr, sizeof(ip4addr));
			if (connect_result == SOCKET_ERROR)
			{
				result->socket->status = SocketGetLastError();
				SocketSetLastError(0);
				switch (result->socket->status)
				{
					case WSAEWOULDBLOCK:
					{
						success = true;
						break;
					}
					default:
					{
						TraceLog(LOG_WARNING, "Socket Error: %s",
								 SocketErrorCodeToString(result->socket->status));
						success = false;
						break;
					}
				}
			}
			else
			{
				TraceLog(LOG_INFO, "Successfully connected to socket.");
				success = true;
			}
		}
		else
		{
			if (IsIPv6Address(config->host))
			{
				struct sockaddr_in6 ip6addr;
				ip6addr.sin6_family = AF_INET6;
				unsigned long hport;
				hport             = strtoul(config->port, NULL, 0);
				ip6addr.sin6_port = htons(hport);
				inet_pton(AF_INET6, config->host, &ip6addr.sin6_addr);
				int connect_result = connect(result->socket->channel, (struct sockaddr *) &ip6addr, sizeof(ip6addr));
				if (connect_result == SOCKET_ERROR)
				{
					result->socket->status = SocketGetLastError();
					SocketSetLastError(0);
					switch (result->socket->status)
					{
						case WSAEWOULDBLOCK:
						{
							success = true;
							break;
						}
						default:
						{
							TraceLog(LOG_WARNING, "Socket Error: %s",
									 SocketErrorCodeToString(result->socket->status));
							success = false;
							break;
						}
					}
				}
				else
				{
					TraceLog(LOG_INFO, "Successfully connected to socket.");
					success = true;
				}
			}
		}
	}

	if (success)
	{
		result->status         = RESULT_SUCCESS;
		result->socket->ready  = 0;
		result->socket->status = 0;
	}

	return success;
}

//	Closes an existing socket
//
//	SocketChannel socket - The id of the socket to close
void SocketClose(Socket *sock)
{
	if (sock != NULL)
	{
		if (sock->channel != INVALID_SOCKET)
		{
			closesocket(sock->channel);
		}
	}
}

// Returns the sockaddress for a specific socket in a generic storage struct
SocketAddressStorage SocketGetPeerAddress(Socket *sock)
{
	if (sock->isServer)
	{
		return NULL;
	}
	if (sock->isIPv6)
	{
		return sock->addripv6;
	}
	else
	{
		return sock->addripv4;
	}
}

// Return the address-type appropriate host portion of a socket address
char *GetSocketAddressHost(SocketAddressStorage storage)
{
	assert(storage->address.ss_family == AF_INET || storage->address.ss_family == AF_INET6);
	return SocketAddressToString((struct sockaddr_storage *) storage);
}

// Return the address-type appropriate port(service) portion of a socket address
short GetSocketAddressPort(SocketAddressStorage storage)
{
	return ntohs(GetSocketPortPtr(storage));
}

//	The accept function permits an incoming connection attempt on a socket.
//
//	SocketChannel	listener	- The socket to listen for incoming connections on (i.e. server)
//	SocketResult*	out			- The result of this function (if any, including errors)
//
//	e.g.
//
//	SocketResult connection;
//	bool         connected = false;
//	if (!connected)
//	{
//		if (SocketAccept(server_res.socket.channel, &connection))
//		{
//			connected = true;
//		}
//	}
Socket *SocketAccept(Socket *server, SocketConfig *config)
{
	if (!server->isServer || server->type == SOCKET_UDP)
	{
		return NULL;
	}
	struct sockaddr_storage sock_addr;
	socklen_t               sock_alen;
	Socket *                sock;
	sock          = AllocSocket();
	server->ready = 0;
	sock_alen     = sizeof(sock_addr);
	sock->channel = accept(server->channel, (struct sockaddr *) &sock_addr, &sock_alen);
	if (sock->channel == INVALID_SOCKET)
	{
		sock->status = SocketGetLastError();
		TraceLog(LOG_WARNING, "Socket Error: %s", SocketErrorCodeToString(sock->status));
		SocketSetLastError(0);
		SocketClose(sock);
		return NULL;
	}
	(config->nonblocking) ? SocketSetNonBlocking(sock) : SocketSetBlocking(sock);
	sock->isServer = false;
	sock->ready    = 0;
	sock->type     = server->type;
	switch (sock_addr.ss_family)
	{
		case AF_INET:
		{
			struct sockaddr_in *s = ((struct sockaddr_in *) &sock_addr);
			sock->addripv4 = (struct _SocketAddressIPv4 *) malloc(sizeof(*sock->addripv4));
			if (sock->addripv4 != NULL)
			{
				memset(sock->addripv4, 0, sizeof(*sock->addripv4));
				memcpy(&sock->addripv4->address, (struct sockaddr_in *) &s->sin_addr, sizeof(struct sockaddr_in));
				TraceLog(LOG_INFO, "Server: Got connection from %s::%hu", SocketAddressToString((struct sockaddr_storage *) s),
						 ntohs(sock->addripv4->address.sin_port));
			}
		}
		break;
		case AF_INET6:
		{
			struct sockaddr_in6 *s = ((struct sockaddr_in6 *) &sock_addr);
			sock->addripv6 = (struct _SocketAddressIPv6 *) malloc(sizeof(*sock->addripv6));
			if (sock->addripv6 != NULL)
			{
				memset(sock->addripv6, 0, sizeof(*sock->addripv6));
				memcpy(&sock->addripv6->address, (struct sockaddr_in6 *) &s->sin6_addr, sizeof(struct sockaddr_in6));
				TraceLog(LOG_INFO, "Server: Got connection from %s::%hu", SocketAddressToString((struct sockaddr_storage *) s),
						 ntohs(sock->addripv6->address.sin6_port));
			}
		}
		break;
	}
	return sock;
}

// Verify that the channel is in the valid range
static int ValidChannel(int channel)
{
	if ((channel < 0) || (channel >= SOCKET_MAX_UDPCHANNELS))
	{
		TraceLog(LOG_WARNING, "Invalid channel");
		return 0;
	}
	return 1;
}

// Set the socket channel
int SocketSetChannel(Socket *socket, int channel, const IPAddress *address)
{
	struct UDPChannel *binding;
	if (socket == NULL)
	{
		TraceLog(LOG_WARNING, "Passed a NULL socket");
		return (-1);
	}
	if (channel == -1)
	{
		for (channel = 0; channel < SOCKET_MAX_UDPCHANNELS; ++channel)
		{
			binding = &socket->binding[channel];
			if (binding->numbound < SOCKET_MAX_UDPADDRESSES)
			{
				break;
			}
		}
	}
	else
	{
		if (!ValidChannel(channel))
		{
			return (-1);
		}
		binding = &socket->binding[channel];
	}
	if (binding->numbound == SOCKET_MAX_UDPADDRESSES)
	{
		TraceLog(LOG_WARNING, "No room for new addresses");
		return (-1);
	}
	binding->address[binding->numbound++] = *address;
	return (channel);
}

// Remove the socket channel
void SocketUnsetChannel(Socket *socket, int channel)
{
	if ((channel >= 0) && (channel < SOCKET_MAX_UDPCHANNELS))
	{
		socket->binding[channel].numbound = 0;
	}
}

/* Allocate/free a single UDP packet 'size' bytes long.
   The new packet is returned, or NULL if the function ran out of memory.
 */
SocketDataPacket *AllocPacket(int size)
{
	SocketDataPacket *packet;
	int               error;

	error  = 1;
	packet = (SocketDataPacket *) malloc(sizeof(*packet));
	if (packet != NULL)
	{
		packet->maxlen = size;
		packet->data   = (uint8_t *) malloc(size);
		if (packet->data != NULL)
		{
			error = 0;
		}
	}
	if (error)
	{
		FreePacket(packet);
		packet = NULL;
	}
	return (packet);
}

int ResizePacket(SocketDataPacket *packet, int newsize)
{
	uint8_t *newdata;

	newdata = (uint8_t *) malloc(newsize);
	if (newdata != NULL)
	{
		free(packet->data);
		packet->data   = newdata;
		packet->maxlen = newsize;
	}
	return (packet->maxlen);
}

void FreePacket(SocketDataPacket *packet)
{
	if (packet)
	{
		free(packet->data);
		free(packet);
	}
}

/* Allocate/Free a UDP packet vector (array of packets) of 'howmany' packets,
   each 'size' bytes long.
   A pointer to the packet array is returned, or NULL if the function ran out
   of memory.
 */
SocketDataPacket **AllocPacketList(int howmany, int size)
{
	SocketDataPacket **packetV;

	packetV = (SocketDataPacket **) malloc((howmany + 1) * sizeof(*packetV));
	if (packetV != NULL)
	{
		int i;
		for (i = 0; i < howmany; ++i)
		{
			packetV[i] = AllocPacket(size);
			if (packetV[i] == NULL)
			{
				break;
			}
		}
		packetV[i] = NULL;

		if (i != howmany)
		{ 
			FreePacketList(packetV);
			packetV = NULL;
		}
	}
	return (packetV);
}

void FreePacketList(SocketDataPacket **packetV)
{
	if (packetV)
	{
		int i;
		for (i = 0; packetV[i]; ++i)
		{
			FreePacket(packetV[i]);
		}
		free(packetV);
	}
}

//	Send 'len' bytes of 'data' over the non-server socket 'sock'
//
//	Example
int SocketSend(Socket *sock, const void *datap, int length)
{
	int                  sent    = 0;
	int                  left    = length;
	int                  status  = -1;
	int                  numsent = 0;
	const unsigned char *data    = (const unsigned char *) datap;

	// Server sockets are for accepting connections only
	if (sock->isServer)
	{
		TraceLog(LOG_WARNING, "Cannot send information on a server socket");
		return -1;
	}

	// Which socket are we trying to send data on
	switch (sock->type)
	{
		case SOCKET_TCP:
		{
			SocketSetLastError(0);
			do
			{
				length = send(sock->channel, (const char *) data, left, 0);
				if (length > 0)
				{
					sent += length;
					left -= length;
					data += length;
				}
			} while ((left > 0) && // While we still have bytes left to send
					 ((length > 0) || // The amount of bytes we actually sent is > 0
					  (SocketGetLastError() == WSAEINTR)) // The socket was interupted
			);

			if (length == SOCKET_ERROR)
			{
				sock->status = SocketGetLastError();
				TraceLog(LOG_DEBUG, "Socket Error: %s", SocketErrorCodeToString(sock->status));
				SocketSetLastError(0);
			}
			else
			{
				TraceLog(LOG_DEBUG, "Successfully sent \"%s\" (%d bytes)", datap, sent);
			}

			return sent;
		}
		break;
		case SOCKET_UDP:
		{
			SocketSetLastError(0);
			if (sock->isIPv6)
			{
				status = sendto(sock->channel, (const char *) data, left, 0,
								(struct sockaddr *) &sock->addripv6->address,
								sizeof(sock->addripv6->address));
			}
			else
			{
				status = sendto(sock->channel, (const char *) data, left, 0,
								(struct sockaddr *) &sock->addripv4->address,
								sizeof(sock->addripv4->address));
			}
			if (sent >= 0)
			{
				sock->status = 0;
				++numsent;
				TraceLog(LOG_DEBUG, "Successfully sent \"%s\" (%d bytes)", datap, status);
			}
			else
			{
				sock->status = SocketGetLastError();
				TraceLog(LOG_DEBUG, "Socket Error: %s", SocketGetLastErrorString(sock->status));
				SocketSetLastError(0);
				return 0;
			}
			return numsent;
		}
		break;
		default: break;
	}
	return -1;
}

//	Receive up to 'maxlen' bytes of data over the non-server socket 'sock',
//	and store them in the buffer pointed to by 'data'.
//	This function returns the actual amount of data received.  If the return
//	value is less than or equal to zero, then either the remote connection was
//	closed, or an unknown socket error occurred.
int SocketReceive(Socket *sock, void *data, int maxlen)
{
	int                     len     = 0;
	int                     numrecv = 0;
	int                     status  = 0;
	socklen_t               sock_len;
	struct sockaddr_storage sock_addr;
	char                    ip[INET6_ADDRSTRLEN];

	// Server sockets are for accepting connections only
	if (sock->isServer && sock->type == SOCKET_TCP)
	{
		sock->status = SocketGetLastError();
		TraceLog(LOG_DEBUG, "Socket Error: %s",
				 "Server sockets cannot be used to receive data");
		SocketSetLastError(0);
		return 0;
	}

	// Which socket are we trying to send data on
	switch (sock->type)
	{
		case SOCKET_TCP:
		{
			SocketSetLastError(0);
			do
			{
				len = recv(sock->channel, (char *) data, maxlen, 0);
			} while (SocketGetLastError() == WSAEINTR);

			if (len > 0)
			{
				// Who sent the packet?
				if (sock->type == SOCKET_UDP)
				{
					TraceLog(
						LOG_DEBUG, "Received data from: %s", inet_ntop(sock_addr.ss_family, GetSocketAddressPtr((struct sockaddr *) &sock_addr), ip, sizeof(ip)));
				}
				((unsigned char *) data)[len] = '\0'; // Add null terminating character to the end of the stream
				TraceLog(LOG_DEBUG, "Received \"%s\" (%d bytes)", data, len);
			}
			sock->ready = 0;
			return len;
		}
		break;
		case SOCKET_UDP:
		{
			SocketSetLastError(0);
			sock_len = sizeof(sock_addr);
			status   = recvfrom(sock->channel, // The receving channel
                              data,   // A pointer to the data buffer to fill
                              maxlen, // The max length of the data to fill
                              0,      // Flags
                              (struct sockaddr *) &sock_addr, // The address of the recevied data
                              &sock_len // The length of the received data address
            );
			if (status >= 0)
			{
				++numrecv;
			}
			else
			{
				sock->status = SocketGetLastError();
				switch (sock->status)
				{
					case WSAEWOULDBLOCK: { break;
					}
					default:
					{
						TraceLog(LOG_WARNING, "Socket Error: %s", SocketErrorCodeToString(sock->status));
						break;
					}
				}
				SocketSetLastError(0);
				return 0;
			}
			sock->ready = 0;
			return numrecv;
		}
		break;
	}
	return -1;
}

// Does the socket have it's 'ready' flag set?
bool IsSocketReady(Socket *sock)
{
	return (sock != NULL) && (sock->ready);
}

// Check if the socket is considered connected
bool IsSocketConnected(Socket *sock)
{
#if defined(_WIN32)
	FD_SET writefds;
	FD_ZERO(&writefds);
	FD_SET(sock->channel, &writefds);
	struct timeval timeout;
	timeout.tv_sec  = 1;
	timeout.tv_usec = 1000000000UL;
	int total       = select(0, NULL, &writefds, NULL, &timeout);
	if (total == -1)
	{ // Error
		sock->status = SocketGetLastError();
		TraceLog(LOG_WARNING, "Socket Error: %s", SocketErrorCodeToString(sock->status));
		SocketSetLastError(0);
	}
	else if (total == 0)
	{ // Timeout
		return false;
	}
	else
	{
		if (FD_ISSET(sock->channel, &writefds))
		{
			return true;
		}
	}
	return false;
#else
	return true;
#endif
}

// Allocate and return a SocketResult struct
SocketResult *AllocSocketResult()
{
	struct SocketResult *res;
	res = (struct SocketResult *) malloc(sizeof(*res));
	if (res != NULL)
	{
		memset(res, 0, sizeof(*res));
		if ((res->socket = AllocSocket()) == NULL)
		{
			free(res);
			res = NULL;
		}
	}
	return res;
}

// Free an allocated SocketResult
void FreeSocketResult(SocketResult **result)
{
	if (*result != NULL)
	{
		if ((*result)->socket != NULL)
		{
			FreeSocket(&((*result)->socket));
		}
		free(*result);
		*result = NULL;
	}
}

// Allocate a Socket
Socket *AllocSocket()
{
	// Allocate a socket if one already hasn't been
	struct Socket *sock;
	sock = (Socket *) malloc(sizeof(*sock));
	if (sock != NULL)
	{
		memset(sock, 0, sizeof(*sock));
	}
	else
	{
		TraceLog(
			LOG_WARNING, "Ran out of memory attempting to allocate a socket");
		SocketClose(sock);
		free(sock);
		sock = NULL;
	}
	return sock;
}

// Free an allocated Socket
void FreeSocket(Socket **sock)
{
	if (*sock != NULL)
	{
		free(*sock);
		*sock = NULL;
	}
}

// Allocate a SocketSet
SocketSet *AllocSocketSet(int max)
{
	struct SocketSet *set;
	int               i;

	set = (struct SocketSet *) malloc(sizeof(*set));
	if (set != NULL)
	{
		set->numsockets = 0;
		set->maxsockets = max;
		set->sockets = (struct Socket **) malloc(max * sizeof(*set->sockets));
		if (set->sockets != NULL)
		{
			for (i = 0; i < max; ++i)
			{
				set->sockets[i] = NULL;
			}
		}
		else
		{
			free(set);
			set = NULL;
		}
	}
	return (set);
}

// Free an allocated SocketSet
void FreeSocketSet(SocketSet *set)
{
	if (set)
	{
		free(set->sockets);
		free(set);
	}
}

// Add a Socket "sock" to the SocketSet "set"
int AddSocket(SocketSet *set, Socket *sock)
{
	if (sock != NULL)
	{
		if (set->numsockets == set->maxsockets)
		{
			TraceLog(LOG_DEBUG, "Socket Error: %s", "SocketSet is full");
			SocketSetLastError(0);
			return (-1);
		}
		set->sockets[set->numsockets++] = (struct Socket *) sock;
	}
	else
	{
		TraceLog(LOG_DEBUG, "Socket Error: %s", "Socket was null");
		SocketSetLastError(0);
		return (-1);
	}
	return (set->numsockets);
}

// Remove a Socket "sock" to the SocketSet "set"
int RemoveSocket(SocketSet *set, Socket *sock)
{
	int i;

	if (sock != NULL)
	{
		for (i = 0; i < set->numsockets; ++i)
		{
			if (set->sockets[i] == (struct Socket *) sock)
			{
				break;
			}
		}
		if (i == set->numsockets)
		{
			TraceLog(LOG_DEBUG, "Socket Error: %s", "Socket not found");
			SocketSetLastError(0);
			return (-1);
		}
		--set->numsockets;
		for (; i < set->numsockets; ++i)
		{
			set->sockets[i] = set->sockets[i + 1];
		}
	}
	return (set->numsockets);
}

// Check the sockets in the socket set for pending information
int CheckSockets(SocketSet *set, unsigned int timeout)
{
	int            i;
	SOCKET         maxfd;
	int            retval;
	struct timeval tv;
	fd_set         mask;

	/* Find the largest file descriptor */
	maxfd = 0;
	for (i = set->numsockets - 1; i >= 0; --i)
	{
		if (set->sockets[i]->channel > maxfd)
		{
			maxfd = set->sockets[i]->channel;
		}
	}

	// Check the file descriptors for available data
	do
	{
		SocketSetLastError(0);

		// Set up the mask of file descriptors
		FD_ZERO(&mask);
		for (i = set->numsockets - 1; i >= 0; --i)
		{
			FD_SET(set->sockets[i]->channel, &mask);
		} // Set up the timeout
		tv.tv_sec  = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;

		/* Look! */
		retval = select(maxfd + 1, &mask, NULL, NULL, &tv);
	} while (SocketGetLastError() == WSAEINTR);

	// Mark all file descriptors ready that have data available
	if (retval > 0)
	{
		for (i = set->numsockets - 1; i >= 0; --i)
		{
			if (FD_ISSET(set->sockets[i]->channel, &mask))
			{
				set->sockets[i]->ready = 1;
			}
		}
	}
	return (retval);
} 

// Allocate an AddressInformation
AddressInformation AllocAddress()
{
	AddressInformation addressInfo = NULL;
	addressInfo = (AddressInformation) calloc(1, sizeof(*addressInfo));
	if (addressInfo != NULL)
	{
		addressInfo->addr.ai_addr = (struct sockaddr *) calloc(1, sizeof(struct sockaddr));
		if (addressInfo->addr.ai_addr == NULL)
		{
			TraceLog(LOG_WARNING,
					 "Failed to allocate memory for \"struct sockaddr\"");
		}
	}
	else
	{
		TraceLog(LOG_WARNING,
				 "Failed to allocate memory for \"struct AddressInformation\"");
	}
	return addressInfo;
}

// Free an AddressInformation struct
void FreeAddress(AddressInformation *addressInfo)
{
	if (*addressInfo != NULL)
	{
		if ((*addressInfo)->addr.ai_addr != NULL)
		{
			free((*addressInfo)->addr.ai_addr);
			(*addressInfo)->addr.ai_addr = NULL;
		}
		free(*addressInfo);
		*addressInfo = NULL;
	}
}

// Allocate a list of AddressInformation
AddressInformation *AllocAddressList(int size)
{
	AddressInformation *addr;
	addr = (AddressInformation *) malloc(size * sizeof(AddressInformation));
	return addr;
}

// Opaque datatype accessor addrinfo->ai_family
int GetAddressFamily(AddressInformation address)
{
	return address->addr.ai_family;
}

// Opaque datatype accessor addrinfo->ai_socktype
int GetAddressSocketType(AddressInformation address)
{
	return address->addr.ai_socktype;
}

// Opaque datatype accessor addrinfo->ai_protocol
int GetAddressProtocol(AddressInformation address)
{
	return address->addr.ai_protocol;
}

// Opaque datatype accessor addrinfo->ai_canonname
char *GetAddressCanonName(AddressInformation address)
{
	return address->addr.ai_canonname;
}

// Opaque datatype accessor addrinfo->ai_addr
char *GetAddressHostAndPort(AddressInformation address, char *outhost, int *outport)
{
	char *                   ip[INET6_ADDRSTRLEN];
	char *                   result = NULL;
	struct sockaddr_storage *storage = (struct sockaddr_storage *) address->addr.ai_addr;
	switch (storage->ss_family)
	{
		case AF_INET:
		{
			struct sockaddr_in *s = ((struct sockaddr_in *) address->addr.ai_addr);
			result   = inet_ntop(AF_INET, &s->sin_addr, ip, INET_ADDRSTRLEN);
			*outport = ntohs(s->sin_port);
		}
		break;
		case AF_INET6:
		{
			struct sockaddr_in6 *s = ((struct sockaddr_in6 *) address->addr.ai_addr);
			result   = inet_ntop(AF_INET6, &s->sin6_addr, ip, INET6_ADDRSTRLEN);
			*outport = ntohs(s->sin6_port);
		}
		break;
	}
	if (result == NULL)
	{
		TraceLog(LOG_WARNING, "Socket Error: %s", SocketErrorCodeToString(SocketGetLastError()));
		SocketSetLastError(0);
	}
	else
	{
		strcpy(outhost, result);
	}
	return result;
}

//
void PacketSend(Packet *packet)
{
	printf("Sending packet (%s) with size %d\n", packet->data, packet->size);
}

//
void PacketReceive(Packet *packet)
{
	printf("Receiving packet (%s) with size %d\n", packet->data, packet->size);
}

//
void PacketWrite16(Packet *packet, uint16_t value)
{
	printf("Original: 0x%04" PRIX16 " - %" PRIu16 "\n", value, value);
	uint8_t *data = packet->data + packet->offs;
	*data++       = (uint8_t)(value >> 8);
	*data++       = (uint8_t)(value);
	packet->size += sizeof(uint16_t);
	packet->offs += sizeof(uint16_t);
	printf("Network: 0x%04" PRIX16 " - %" PRIu16 "\n", (uint16_t) *data, (uint16_t) *data);
}

//
void PacketWrite32(Packet *packet, uint32_t value)
{
	printf("Original: 0x%08" PRIX32 " - %" PRIu32 "\n", value, value);
	uint8_t *data = packet->data + packet->offs;
	*data++       = (uint8_t)(value >> 24);
	*data++       = (uint8_t)(value >> 16);
	*data++       = (uint8_t)(value >> 8);
	*data++       = (uint8_t)(value);
	packet->size += sizeof(uint32_t);
	packet->offs += sizeof(uint32_t);
	printf("Network: 0x%08" PRIX32 " - %" PRIu32 "\n",
		   (uint32_t)(((intptr_t) packet->data) - packet->offs),
		   (uint32_t)(((intptr_t) packet->data) - packet->offs));
}

//
void PacketWrite64(Packet *packet, uint64_t value)
{
	printf("Original: 0x%016" PRIX64 " - %" PRIu64 "\n", value, value);
	uint8_t *data = packet->data + packet->offs;
	*data++       = (uint8_t)(value >> 56);
	*data++       = (uint8_t)(value >> 48);
	*data++       = (uint8_t)(value >> 40);
	*data++       = (uint8_t)(value >> 32);
	*data++       = (uint8_t)(value >> 24);
	*data++       = (uint8_t)(value >> 16);
	*data++       = (uint8_t)(value >> 8);
	*data++       = (uint8_t)(value);
	packet->size += sizeof(uint64_t);
	packet->offs += sizeof(uint64_t);
	printf("Network: 0x%016" PRIX64 " - %" PRIu64 "\n",
		   (uint64_t)(packet->data - packet->offs),
		   (uint64_t)(packet->data - packet->offs));
}

//
uint16_t PacketRead16(Packet *packet)
{
	uint8_t *data = packet->data + packet->offs;
	packet->size += sizeof(uint16_t);
	packet->offs += sizeof(uint16_t);
	uint16_t value = ((uint16_t) data[0] << 8) | data[1];
	printf("Original: 0x%04" PRIX16 " - %" PRIu16 "\n", value, value);
	return value;
}

//
uint32_t PacketRead32(Packet *packet)
{
	uint8_t *data = packet->data + packet->offs;
	packet->size += sizeof(uint32_t);
	packet->offs += sizeof(uint32_t);
	uint32_t value = ((uint32_t) data[0] << 24) | ((uint32_t) data[1] << 16) | ((uint32_t) data[2] << 8) | data[3];
	printf("Original: 0x%08" PRIX32 " - %" PRIu32 "\n", value, value);
	return value;
}

//
uint64_t PacketRead64(Packet *packet)
{
	uint8_t *data = packet->data + packet->offs;
	packet->size += sizeof(uint64_t);
	packet->offs += sizeof(uint64_t);
	uint64_t value = ((uint64_t) data[0] << 56) | ((uint64_t) data[1] << 48) | ((uint64_t) data[2] << 40) | ((uint64_t) data[3] << 32) | ((uint64_t) data[4] << 24) | ((uint64_t) data[5] << 16) | ((uint64_t) data[6] << 8) | data[7];
	printf("Original: 0x%016" PRIX64 " - %" PRIu64 "\n", value, value);
	return value;
}