// Minimal SDL_net stand-in for the 3DS.
//
// devkitPro packages no 3ds-SDL2_net, and SoH's multiplayer (soh/soh/Network,
// the Anchor sync) is out of scope for this port. Its *sources* are excluded
// from the build, but Network.h is still pulled in transitively by the
// randomizer check tracker, so the header has to parse.
//
// These are declarations only, deliberately with no definitions: if anything
// ever calls them the link fails loudly rather than silently doing nothing over
// a network that isn't there.

#pragma once

#include <cstdint>

typedef struct {
    uint32_t host;
    uint16_t port;
} IPaddress;

typedef struct _TCPsocket* TCPsocket;
typedef struct _UDPsocket* UDPsocket;
typedef struct _SDLNet_SocketSet* SDLNet_SocketSet;

int SDLNet_Init(void);
void SDLNet_Quit(void);
int SDLNet_ResolveHost(IPaddress* address, const char* host, uint16_t port);
TCPsocket SDLNet_TCP_Open(IPaddress* ip);
void SDLNet_TCP_Close(TCPsocket sock);
int SDLNet_TCP_Send(TCPsocket sock, const void* data, int len);
int SDLNet_TCP_Recv(TCPsocket sock, void* data, int maxlen);
const char* SDLNet_GetError(void);
