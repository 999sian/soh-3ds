#include "libultraship/controller/controldeck/ControlDeck.h"

#include "ship/Context.h"
#include "libultraship/controller/controldevice/controller/Controller.h"
#include "libultraship/controller/controldevice/controller/mapping/ControllerDefaultMappings.h"
#include "ship/utils/StringHelper.h"
#include <imgui.h>
#include "ship/controller/controldevice/controller/mapping/mouse/WheelHandler.h"

#ifdef __3DS__
#include <cstdio>
#include "libultraship/bridge/consolevariablebridge.h"
#include "libultraship/controller/controldeck/Controls3DS.h"
#include "ship/controller/controldevice/controller/mapping/sdl/SDLButtonToButtonMapping.h"
#include "ship/controller/controldevice/controller/mapping/sdl/SDLAxisDirectionToButtonMapping.h"
// MUST be file scope with C linkage. A block-scope `extern` inside
// `namespace LUS` mangles to LUS::Soh3dsPadProbe, which is a different symbol:
// weak, never defined, always null, so the probe silently never fires and its
// definition is then garbage-collected. That mistake cost three builds.
extern "C" void Soh3dsPadProbe(unsigned int button, int stickX, int stickY) __attribute__((weak));
extern "C" void Soh3dsControls_TransformPad(uint32_t physicalButtons, uint16_t* buttons) __attribute__((weak));

namespace {
std::shared_ptr<Ship::Controller> Soh3dsControls_Controller() {
    auto context = Ship::Context::GetRawInstance();
    return context == nullptr || context->GetControlDeck() == nullptr ? nullptr
                                                                      : context->GetControlDeck()->GetControllerByPort(0);
}

uint32_t Soh3dsControls_ReadPhysicalButtons() {
    auto context = Ship::Context::GetRawInstance();
    if (context == nullptr || context->GetControlDeck() == nullptr) {
        return 0;
    }
    uint32_t held = 0;
    for (const auto& [instanceId, gamepad] : context->GetControlDeck()
                                                   ->GetConnectedPhysicalDeviceManager()
                                                   ->GetConnectedSDLGamepadsForPort(0)) {
        (void)instanceId;
        const auto button = [gamepad, &held](SDL_GameControllerButton source, uint32_t bit) {
            if (SDL_GameControllerGetButton(gamepad, source)) {
                held |= bit;
            }
        };
        button(SDL_CONTROLLER_BUTTON_A, 1u << 0);
        button(SDL_CONTROLLER_BUTTON_B, 1u << 1);
        button(SDL_CONTROLLER_BUTTON_X, 1u << 2);
        button(SDL_CONTROLLER_BUTTON_Y, 1u << 3);
        button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 1u << 4);
        button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1u << 5);
        button(SDL_CONTROLLER_BUTTON_DPAD_UP, 1u << 8);
        button(SDL_CONTROLLER_BUTTON_DPAD_DOWN, 1u << 9);
        button(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 1u << 10);
        button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 1u << 11);
        button(SDL_CONTROLLER_BUTTON_START, 1u << 12);
        button(SDL_CONTROLLER_BUTTON_BACK, 1u << 13);
        constexpr int16_t kTriggerThreshold = SDL_JOYSTICK_AXIS_MAX / 4;
        if (SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > kTriggerThreshold) {
            held |= 1u << 6;
        }
        if (SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > kTriggerThreshold) {
            held |= 1u << 7;
        }
    }
    return held;
}
} // namespace

extern "C" unsigned Soh3dsControls_CurrentMappingMask(int physical) {
    const auto controller = Soh3dsControls_Controller();
    if (controller == nullptr || physical < 0 || physical >= 14) {
        return 0;
    }
    unsigned result = 0;
    for (const auto& [bitmask, logicalButton] : controller->GetAllButtons()) {
        if (logicalButton == nullptr) {
            continue;
        }
        for (const auto& [id, mapping] : logicalButton->GetAllButtonMappings()) {
            if (mapping != nullptr && mapping->GetPhysicalDeviceType() == Ship::PhysicalDeviceType::SDLGamepad &&
                Soh3dsControls_MappingIdMatches(physical, id.c_str())) {
                result |= static_cast<unsigned>(bitmask);
            }
        }
    }
    return result;
}

extern "C" int Soh3dsControls_GetCStickDeadzone() {
    const auto controller = Soh3dsControls_Controller();
    return controller == nullptr ? 20 : controller->GetRightStick()->GetDeadzonePercentage();
}

extern "C" void Soh3dsControls_SetCStickDeadzone(int value) {
    const auto controller = Soh3dsControls_Controller();
    if (controller != nullptr) {
        controller->GetRightStick()->SetDeadzone(static_cast<uint8_t>(value < 0 ? 0 : value > 50 ? 50 : value));
    }
}
#endif

namespace LUS {
ControlDeck::ControlDeck(std::vector<CONTROLLERBUTTONS_T> additionalBitmasks,
                         std::shared_ptr<Ship::ControllerDefaultMappings> controllerDefaultMappings,
                         std::unordered_map<CONTROLLERBUTTONS_T, std::string> buttonNames)
    : Ship::ControlDeck(additionalBitmasks, controllerDefaultMappings, buttonNames), mPads(nullptr) {
    std::vector<CONTROLLERBUTTONS_T> bitmasks;
    for (auto [bitmask, name] : buttonNames) {
        bitmasks.push_back(bitmask);
    }
    bitmasks.insert(bitmasks.end(), additionalBitmasks.begin(), additionalBitmasks.end());
    for (int32_t i = 0; i < MAXCONTROLLERS; i++) {
        mPorts.push_back(std::make_shared<Ship::ControlPort>(i, std::make_shared<Controller>(i, bitmasks)));
    }
}

ControlDeck::ControlDeck(std::vector<CONTROLLERBUTTONS_T> additionalBitmasks)
    : ControlDeck(additionalBitmasks, std::make_shared<LUS::ControllerDefaultMappings>(),
                  std::unordered_map<CONTROLLERBUTTONS_T, std::string>({
                      { BTN_A, "A" },
                      { BTN_B, "B" },
                      { BTN_L, "L" },
                      { BTN_R, "R" },
                      { BTN_Z, "Z" },
                      { BTN_START, "Start" },
                      { BTN_CLEFT, "CLeft" },
                      { BTN_CRIGHT, "CRight" },
                      { BTN_CUP, "CUp" },
                      { BTN_CDOWN, "CDown" },
                      { BTN_DLEFT, "DLeft" },
                      { BTN_DRIGHT, "DRight" },
                      { BTN_DUP, "DUp" },
                      { BTN_DDOWN, "DDown" },
                  })) {
}

ControlDeck::ControlDeck() : ControlDeck(std::vector<CONTROLLERBUTTONS_T>()) {
}

OSContPad* ControlDeck::GetPads() {
    return mPads;
}

void ControlDeck::WriteToPad(void* pad) {
    WriteToOSContPad((OSContPad*)pad);
}

void ControlDeck::WriteToOSContPad(OSContPad* pad) {
    SDL_PumpEvents();
    Ship::WheelHandler::GetInstance()->Update();

    if (AllGameInputBlocked()) {
        return;
    }

    mPads = pad;

    for (size_t i = 0; i < mPorts.size(); i++) {
        const std::shared_ptr<Ship::Controller> controller = mPorts[i]->GetConnectedController();

        if (controller != nullptr) {
            controller->ReadToPad(&pad[i]);
        }
    }

#ifdef __3DS__
    // This is the exact boundary where input enters the game: whatever lands in
    // pad[0] here is what the decomp reads. Report port adoption once, then only
    // on change, so a run stays quiet unless something is actually pressed.
    // SoH-3DS: one-time console input layout.
    //
    // The inherited desktop defaults do not suit the 3DS: N64 Z sits on ZL
    // (absent on an Old 3DS), physical R is unused while N64 R sits on ZR, and
    // the C-buttons are bound to the C-stick - which makes the C-stick useless
    // as a camera, since looking around would switch items. Seed the console
    // layout once and record it in gSoh3dsInputLayout, so a later manual remap
    // in the controller menu is never overwritten on the next boot.
    //
    // SDL2's 3DS backend (src/joystick/n3ds/SDL_sysjoystick.c): axes 0/1 are
    // the circle pad, axes 2/3 the C-stick (New 3DS only - hidCstickRead goes
    // through ir:rst, which hidInit only starts when APT_CheckNew3DS is true,
    // so the axes read 0 on an Old 3DS). Physical L/R are buttons b9/b8
    // (leftshoulder/rightshoulder); ZL/ZR are DIGITAL buttons b14/b15 exposed
    // through the trigger axes, so they are bound as axis directions.
    constexpr int32_t kInputLayoutVersion = 1;
    if (CVarGetInteger("gSoh3dsInputLayout", 0) < kInputLayoutVersion) {
        const std::shared_ptr<Ship::Controller> controller = mPorts[0]->GetConnectedController();
        if (controller != nullptr) {
            auto rebind = [&controller](CONTROLLERBUTTONS_T bitmask,
                                        const std::vector<std::shared_ptr<Ship::ControllerButtonMapping>>& mappings) {
                auto button = controller->GetButton(bitmask);
                if (button == nullptr) {
                    return;
                }
                button->ClearAllButtonMappingsForDeviceType(Ship::PhysicalDeviceType::SDLGamepad);
                for (const auto& mapping : mappings) {
                    button->AddButtonMapping(mapping);
                    // AddButtonMapping only touches the in-memory map; the
                    // mapping's own config entry and the button's id list are
                    // separate writes (see ControllerButton::
                    // AddOrEditButtonMappingFromRawPress).
                    mapping->SaveToConfig();
                }
                button->SaveButtonMappingIdsToConfig();
            };
            auto sdlButton = [](CONTROLLERBUTTONS_T bitmask, int32_t sdlControllerButton) {
                return std::make_shared<Ship::SDLButtonToButtonMapping>(0, bitmask, sdlControllerButton);
            };
            auto sdlTrigger = [](CONTROLLERBUTTONS_T bitmask, int32_t axis) {
                return std::make_shared<Ship::SDLAxisDirectionToButtonMapping>(0, bitmask, axis, 1);
            };

            // Z-target on physical L, as requested; N64 L moves to ZL, which
            // OoT never needs (and an Old 3DS therefore loses nothing).
            rebind(BTN_Z, { sdlButton(BTN_Z, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) });
            rebind(BTN_L, { sdlTrigger(BTN_L, SDL_CONTROLLER_AXIS_TRIGGERLEFT) });
            // N64 R on physical R, with ZR kept as an alternate.
            rebind(BTN_R, { sdlButton(BTN_R, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER),
                            sdlTrigger(BTN_R, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) });
            // C-buttons (item slots / first person) move off the C-stick onto
            // the D-pad, freeing axes 2/3 for the camera. The N64 D-pad is
            // cleared so a press cannot fire both.
            rebind(BTN_CUP, { sdlButton(BTN_CUP, SDL_CONTROLLER_BUTTON_DPAD_UP) });
            rebind(BTN_CDOWN, { sdlButton(BTN_CDOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN) });
            rebind(BTN_CLEFT, { sdlButton(BTN_CLEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT) });
            rebind(BTN_CRIGHT, { sdlButton(BTN_CRIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) });
            rebind(BTN_DUP, {});
            rebind(BTN_DDOWN, {});
            rebind(BTN_DLEFT, {});
            rebind(BTN_DRIGHT, {});

            // The right stick already reads axes 2/3; FreeLook is what makes
            // the camera follow it (z_camera.c SetCameraManual).
            CVarSetInteger("gSettings.FreeLook.Enabled", 1); // CVAR_SETTING is a SoH-side macro
            CVarSetInteger("gSoh3dsInputLayout", kInputLayoutVersion);
            CVarSave();
            fprintf(stderr, "soh-3ds input: seeded 3DS layout v%d (Z=L, N64 L=ZL, R=R/ZR, C=dpad, C-stick=camera)\n",
                    kInputLayoutVersion);
        }
    }

    // The controls page's managed layouts use raw console buttons here, after
    // the ordinary mapping read and immediately before the game consumes the
    // pad. Current/manual returns without touching the pad. This keeps preset
    // application atomic and lets the bridge suppress held input until a full
    // release after any layout change.
    if (Soh3dsControls_TransformPad != nullptr) {
        const uint32_t physicalButtons = Soh3dsControls_ReadPhysicalButtons();
        Soh3dsControls_TransformPad(physicalButtons, &pad[0].button);
    }

    static bool sPortsReported = false;
    if (!sPortsReported) {
        sPortsReported = true;
        for (size_t i = 0; i < mPorts.size(); i++) {
            const auto controller = mPorts[i]->GetConnectedController();
            if (controller == nullptr) {
                fprintf(stderr, "soh-3ds input: port %u empty\n", (unsigned)i);
                continue;
            }
            // A port can hold a controller with no bindings, which reads as
            // permanently idle. Count the buttons that actually have an
            // SDLGamepad mapping so "adopted" and "controllable" are separate.
            size_t mapped = 0;
            for (const auto& [bitmask, button] : controller->GetAllButtons()) {
                if (button != nullptr && button->HasMappingsForPhysicalDeviceType(Ship::PhysicalDeviceType::SDLGamepad)) {
                    ++mapped;
                }
            }
            fprintf(stderr, "soh-3ds input: port %u connected, hasConfig=%d, %u buttons mapped to SDLGamepad\n",
                    (unsigned)i, (int)controller->HasConfig(), (unsigned)mapped);
        }
    }
    // SoH-3DS DEV: sdmc:/3ds/soh/autopress.txt = lines of
    // "<frame> <hexmask> [<hold> [<stickX> <stickY>]]" (up to 8), each holding
    // N64 pad buttons (OSContPad mask, e.g. 1000 = START, 0010 = R) and an
    // optional stick position (-80..80) for <hold> frames starting at pad-write
    // ordinal <frame>. The emulator harness has no input injection, and the
    // pause menu / its pages / walking to a spot are unreachable without it.
    // Read once; absent file = inert.
    {
        static int sAutoFrame[8];
        static unsigned sAutoMask[8];
        static int sAutoHold[8];
        static int sAutoStickX[8];
        static int sAutoStickY[8];
        // Right stick too: the C-stick cannot be injected in the emulator, so
        // this is the only way to exercise the FreeLook camera path off-console.
        static int sAutoRightX[8];
        static int sAutoRightY[8];
        static int sAutoCount = 0;
        static bool sAutoRead = false;
        static int sPadWrites = 0;
        if (!sAutoRead) {
            sAutoRead = true;
            if (FILE* f = fopen("autopress.txt", "r")) {
                char line[64];
                while (sAutoCount < 8 && fgets(line, sizeof line, f) != nullptr) {
                    sAutoHold[sAutoCount] = 1;
                    sAutoStickX[sAutoCount] = 0;
                    sAutoStickY[sAutoCount] = 0;
                    sAutoRightX[sAutoCount] = 0;
                    sAutoRightY[sAutoCount] = 0;
                    if (sscanf(line, "%d %x %d %d %d %d %d", &sAutoFrame[sAutoCount], &sAutoMask[sAutoCount],
                               &sAutoHold[sAutoCount], &sAutoStickX[sAutoCount], &sAutoStickY[sAutoCount],
                               &sAutoRightX[sAutoCount], &sAutoRightY[sAutoCount]) >= 2) {
                        ++sAutoCount;
                    }
                }
                fclose(f);
            }
        }
        ++sPadWrites;
        for (int i = 0; i < sAutoCount; ++i) {
            if (sPadWrites >= sAutoFrame[i] && sPadWrites < sAutoFrame[i] + sAutoHold[i]) {
                pad[0].button |= static_cast<uint16_t>(sAutoMask[i]);
                if (sAutoStickX[i] != 0 || sAutoStickY[i] != 0) {
                    pad[0].stick_x = static_cast<int8_t>(sAutoStickX[i]);
                    pad[0].stick_y = static_cast<int8_t>(sAutoStickY[i]);
                }
                if (sAutoRightX[i] != 0 || sAutoRightY[i] != 0) {
                    pad[0].right_stick_x = static_cast<int8_t>(sAutoRightX[i]);
                    pad[0].right_stick_y = static_cast<int8_t>(sAutoRightY[i]);
                }
            }
        }
    }
    static uint16_t sLastButton = 0;
    static int8_t sLastX = 0;
    static int8_t sLastY = 0;
    // stderr is invisible on hardware, so mirror to the bottom screen when the
    // ISG probe is armed. Called UNCONDITIONALLY, outside the change guard
    // below: sLastButton starts at 0, so an idle pad never trips that guard and
    // the probe would emit nothing at all - indistinguishable from a broken
    // probe. The sink dedupes on button mask itself and emits one baseline line
    // on its first call, which is what makes "no output" mean "not working".
    if (Soh3dsPadProbe != nullptr) {
        Soh3dsPadProbe((unsigned)pad[0].button, (int)pad[0].stick_x, (int)pad[0].stick_y);
    }
    static int8_t sLastRX = 0;
    static int8_t sLastRY = 0;
    if (pad[0].button != sLastButton || pad[0].stick_x != sLastX || pad[0].stick_y != sLastY ||
        pad[0].right_stick_x != sLastRX || pad[0].right_stick_y != sLastRY) {
        sLastButton = pad[0].button;
        sLastX = pad[0].stick_x;
        sLastY = pad[0].stick_y;
        sLastRX = pad[0].right_stick_x;
        sLastRY = pad[0].right_stick_y;
        char line[128];
        std::snprintf(line, sizeof(line), "soh-3ds input: pad0 buttons=0x%04X stick=(%d,%d) cstick=(%d,%d)\n",
                      (unsigned)pad[0].button, (int)sLastX, (int)sLastY, (int)sLastRX, (int)sLastRY);
        std::fputs(line, stderr);
    }
#endif
}
} // namespace LUS
