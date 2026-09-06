#include "libultraship/controller/controldeck/Controls3DS.h"

#include <cstdio>
#include <cstring>

extern "C" bool Soh3dsControls_MappingIdMatches(int physical, const char* mappingId) {
    if (mappingId == nullptr || physical < 0 || physical >= 14) {
        return false;
    }
    // SDL_GameControllerButton values are stable ABI values. The 3DS backend
    // exposes ZL/ZR through the positive halves of trigger axes 4/5.
    static constexpr int buttons[14] = { 0, 1, 2, 3, 9, 10, -1, -1, 11, 12, 13, 14, 6, 4 };
    const char* button = std::strstr(mappingId, "-SDLB");
    if (buttons[physical] >= 0 && button != nullptr) {
        int parsed = -1;
        char tail = '\0';
        return std::sscanf(button, "-SDLB%d%c", &parsed, &tail) == 1 && parsed == buttons[physical];
    }
    const char* axis = std::strstr(mappingId, "-SDLA");
    if ((physical == 6 || physical == 7) && axis != nullptr) {
        int parsed = -1;
        char direction = '\0';
        char tail = '\0';
        const int wanted = physical == 6 ? 4 : 5;
        return std::sscanf(axis, "-SDLA%d-AD%c%c", &parsed, &direction, &tail) == 2 && parsed == wanted &&
               direction == 'P';
    }
    return false;
}
