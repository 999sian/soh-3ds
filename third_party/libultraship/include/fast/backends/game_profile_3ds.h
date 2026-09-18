#pragma once

#include <stdbool.h>
#include <stdint.h>

// Main game/render thread only. These inclusive scopes may overlap; room
// timing covers the synchronous game-side request/finish work, not worker CPU.
enum Soh3dsGameProfileSection {
    SOH3DS_GAME_UPDATE,
    SOH3DS_GAME_PLAY_UPDATE,
    SOH3DS_GAME_COMMANDS,
    SOH3DS_GAME_ACTOR_UPDATE,
    SOH3DS_GAME_ACTOR_DRAW,
    SOH3DS_GAME_COLLISION,
    SOH3DS_GAME_ANIMATION,
    SOH3DS_GAME_ROOM,
    SOH3DS_GAME_COUNT
};

#ifdef __cplusplus
extern "C" {
#endif
#ifdef __3DS__
extern bool gSoh3dsRenderProfileEnabled __attribute__((weak));
uint64_t Soh3dsGameProfileBegin(unsigned section) __attribute__((weak));
void Soh3dsGameProfileEnd(unsigned section, uint64_t start) __attribute__((weak));

typedef struct Soh3dsGameProfileToken {
    uint64_t start;
    unsigned section;
} Soh3dsGameProfileToken;

static inline Soh3dsGameProfileToken Soh3dsGameProfileStart(unsigned section) {
    Soh3dsGameProfileToken token = { 0, section };
    if (&gSoh3dsRenderProfileEnabled != 0 && gSoh3dsRenderProfileEnabled &&
        Soh3dsGameProfileBegin != 0 && Soh3dsGameProfileEnd != 0) {
        token.start = Soh3dsGameProfileBegin(section);
    }
    return token;
}

static inline void Soh3dsGameProfileFinish(Soh3dsGameProfileToken* token) {
    if (token->start != 0) Soh3dsGameProfileEnd(token->section, token->start);
}

// devkitARM supports cleanup in both C and C++; it also closes early returns.
#define SOH3DS_GAME_PROFILE_SCOPE(name, section) \
    Soh3dsGameProfileToken name __attribute__((cleanup(Soh3dsGameProfileFinish))) = \
        Soh3dsGameProfileStart(section)
#else
#define SOH3DS_GAME_PROFILE_SCOPE(name, section) ((void)0)
#endif
#ifdef __cplusplus
}
#endif
