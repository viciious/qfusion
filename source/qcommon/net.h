#ifndef QFUSION_NET_H
#define QFUSION_NET_H

// net.h -- quake's interface to the networking layer

#ifdef __cplusplus
extern "C" {
#endif

#define PACKET_HEADER           10          // two ints, and a short

#define MAX_RELIABLE_COMMANDS   64          // max string commands buffered for restransmit
#define MAX_PACKETLEN           1400        // max size of a network packet
#define MAX_MSGLEN              32768       // max length of a message, which may be fragmented into multiple packets

// wsw: Medar: doubled the MSGLEN as a temporary solution for multiview on bigger servers
#define FRAGMENT_SIZE           ( MAX_PACKETLEN - 96 )
#define FRAGMENT_LAST       (    1 << 14 )
#define FRAGMENT_BIT            ( 1 << 31 )

typedef enum {
	NA_NOTRANSMIT,      // wsw : jal : fakeclients
	NA_LOOPBACK,
	NA_IP,
	NA_IP6,
} netadrtype_t;

typedef struct netadr_ipv4_s {
	uint8_t ip [4];
	unsigned short port;
} netadr_ipv4_t;

typedef struct netadr_ipv6_s {
	uint8_t ip [16];
	unsigned short port;
	unsigned long scope_id;
} netadr_ipv6_t;

typedef struct netadr_s {
	netadrtype_t type;
	union {
		netadr_ipv4_t ipv4;
		netadr_ipv6_t ipv6;
	} address;
} netadr_t;

typedef enum {
	SOCKET_LOOPBACK,
	SOCKET_UDP
#ifdef TCP_SUPPORT
	, SOCKET_TCP
#endif
} socket_type_t;

typedef struct {
	bool open;

	socket_type_t type;
	netadr_t address;
	bool server;

#ifdef TCP_SUPPORT
	bool connected;
#endif
	netadr_t remoteAddress;

	socket_handle_t handle;
} socket_t;

typedef enum {
	CONNECTION_FAILED = -1,
	CONNECTION_INPROGRESS = 0,
	CONNECTION_SUCCEEDED = 1
} connection_status_t;

typedef enum {
	NET_ERR_UNKNOWN = -1,
	NET_ERR_NONE = 0,

	NET_ERR_CONNRESET,
	NET_ERR_INPROGRESS,
	NET_ERR_MSGSIZE,
	NET_ERR_WOULDBLOCK,
	NET_ERR_UNSUPPORTED,
} net_error_t;

void        NET_Init( void );
void        NET_Shutdown( void );

bool        NET_OpenSocket( socket_t *socket, socket_type_t type, const netadr_t *address, bool server );
void        NET_CloseSocket( socket_t *socket );

#ifdef TCP_SUPPORT
connection_status_t     NET_Connect( socket_t *socket, const netadr_t *address );
connection_status_t     NET_CheckConnect( socket_t *socket );
bool        NET_Listen( const socket_t *socket );
int         NET_Accept( const socket_t *socket, socket_t *newsocket, netadr_t *address );
#endif

int         NET_GetPacket( const socket_t *socket, netadr_t *address, struct msg_s *message );
bool        NET_SendPacket( const socket_t *socket, const void *data, size_t length, const netadr_t *address );

int         NET_Get( const socket_t *socket, netadr_t *address, void *data, size_t length );
int         NET_Send( const socket_t *socket, const void *data, size_t length, const netadr_t *address );
int64_t     NET_SendFile( const socket_t *socket, int file, size_t offset, size_t count, const netadr_t *address );

void        NET_Sleep( int msec, socket_t *sockets[] );
int         NET_Monitor( int msec, socket_t *sockets[],
						 void ( *read_cb )( socket_t *socket, void* ),
						 void ( *write_cb )( socket_t *socket, void* ),
						 void ( *exception_cb )( socket_t *socket, void* ), void *privatep[] );
const char *NET_ErrorString( void );

#ifndef _MSC_VER
void NET_SetErrorString( const char *format, ... ) __attribute__( ( format( printf, 1, 2 ) ) );
#else
void NET_SetErrorString( _Printf_format_string_ const char *format, ... );
#endif

void        NET_SetErrorStringFromLastError( const char *function );
void        NET_ShowIP( void );
int         NET_SetSocketNoDelay( socket_t *socket, int nodelay );

const char *NET_SocketTypeToString( socket_type_t type );
const char *NET_SocketToString( const socket_t *socket );
char       *NET_AddressToString( const netadr_t *address );
bool        NET_StringToAddress( const char *s, netadr_t *address );

unsigned short  NET_GetAddressPort( const netadr_t *address );
void            NET_SetAddressPort( netadr_t *address, unsigned short port );

bool    NET_CompareAddress( const netadr_t *a, const netadr_t *b );
bool    NET_CompareBaseAddress( const netadr_t *a, const netadr_t *b );
bool    NET_IsLANAddress( const netadr_t *address );
bool    NET_IsLocalAddress( const netadr_t *address );
bool    NET_IsAnyAddress( const netadr_t *address );
void    NET_InitAddress( netadr_t *address, netadrtype_t type );
void    NET_BroadcastAddress( netadr_t *address, int port );

#ifdef __cplusplus
}
#endif

#endif
