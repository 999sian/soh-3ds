// Native 3DS control settings. This module deliberately owns policy and CVars;
// libultraship only supplies raw console buttons/current mappings at the input
// boundary, so Current/manual never rewrites a saved desktop or console profile.
#if defined(__3DS__) || defined(SOH3DS_CONTROLS_TEST)

#include "ControlsBridge3DS.h"

#include <array>
#include <cstdio>
#include <cstring>

#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/libultra/controller.h>

extern "C" unsigned Soh3dsControls_CurrentMappingMask(int physical);
extern "C" int Soh3dsControls_GetCStickDeadzone(void);
extern "C" void Soh3dsControls_SetCStickDeadzone(int value);

namespace {

constexpr const char* kLayoutCVar = "gSoh3dsControls.Layout";
constexpr const char* kMappingPrefix = "gSoh3dsControls.Mapping.";
constexpr const char* kSensitivityX = "gSettings.FreeLook.CameraSensitivity.X";
constexpr const char* kSensitivityY = "gSettings.FreeLook.CameraSensitivity.Y";
constexpr const char* kAimSensitivityX = "gSettings.FirstPersonCameraSensitivity.X";
constexpr const char* kAimSensitivityY = "gSettings.FirstPersonCameraSensitivity.Y";
constexpr const char* kInvertX = "gSettings.FreeLook.InvertXAxis";
constexpr const char* kInvertY = "gSettings.FreeLook.InvertYAxis";
constexpr const char* kAimInvertX = "gSettings.Controls.InvertAimingXAxis";
constexpr const char* kAimInvertY = "gSettings.Controls.InvertAimingYAxis";

enum Layout { LAYOUT_CURRENT = 0, LAYOUT_OOT3D = 1, LAYOUT_CUSTOM = 2 };
enum Row {
    ROW_LAYOUT_HEADING,
    ROW_LAYOUT,
    ROW_FACE_DIAGRAM,
    ROW_SHOULDER_DIAGRAM,
    ROW_BUTTON_HEADING,
    ROW_BUTTON_FIRST,
    ROW_BUTTON_LAST = ROW_BUTTON_FIRST + 13,
    ROW_CAMERA_HEADING,
    ROW_DEADZONE,
    ROW_SENSITIVITY_X,
    ROW_SENSITIVITY_Y,
    ROW_INVERT_X,
    ROW_INVERT_Y,
    ROW_COUNT,
};

struct Physical {
    const char* label;
    const char* key;
    uint32_t bit;
};

constexpr std::array<Physical, 14> kPhysical = { {
    { "A button", "A", SOH3DS_PHYS_A },
    { "B button", "B", SOH3DS_PHYS_B },
    { "X button", "X", SOH3DS_PHYS_X },
    { "Y button", "Y", SOH3DS_PHYS_Y },
    { "L button", "L", SOH3DS_PHYS_L },
    { "R button", "R", SOH3DS_PHYS_R },
    { "ZL button", "ZL", SOH3DS_PHYS_ZL },
    { "ZR button", "ZR", SOH3DS_PHYS_ZR },
    { "D-pad Up", "DUp", SOH3DS_PHYS_DUP },
    { "D-pad Down", "DDown", SOH3DS_PHYS_DDOWN },
    { "D-pad Left", "DLeft", SOH3DS_PHYS_DLEFT },
    { "D-pad Right", "DRight", SOH3DS_PHYS_DRIGHT },
    { "Start button", "Start", SOH3DS_PHYS_START },
    { "Select button", "Select", SOH3DS_PHYS_SELECT },
} };

struct Logical {
    const char* name;
    uint16_t mask;
};

constexpr std::array<Logical, 15> kLogical = { {
    { "None", 0 },
    { "A", BTN_A },
    { "B", BTN_B },
    { "Z target", BTN_Z },
    { "R shield", BTN_R },
    { "N64 L", BTN_L },
    { "Start", BTN_START },
    { "C Up", BTN_CUP },
    { "C Down", BTN_CDOWN },
    { "C Left", BTN_CLEFT },
    { "C Right", BTN_CRIGHT },
    { "D-pad Up", BTN_DUP },
    { "D-pad Down", BTN_DDOWN },
    { "D-pad Left", BTN_DLEFT },
    { "D-pad Right", BTN_DRIGHT },
} };

constexpr uint16_t kManagedButtons = BTN_A | BTN_B | BTN_Z | BTN_R | BTN_L | BTN_START | BTN_CUP | BTN_CDOWN |
                                     BTN_CLEFT | BTN_CRIGHT | BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT;

constexpr std::array<uint16_t, 14> kOot3dPreset = {
    BTN_A,      BTN_B,   BTN_CLEFT, BTN_CDOWN, BTN_Z,      BTN_R,     BTN_L,
    BTN_CRIGHT, BTN_CUP, BTN_DDOWN, BTN_DLEFT, BTN_DRIGHT, BTN_START, 0,
};

std::array<std::array<char, 96>, ROW_COUNT> sLabels{};
std::array<std::array<char, 48>, ROW_COUNT> sValues{};
bool sWaitForRelease = false;

int GetLayout() {
    const int layout = CVarGetInteger(kLayoutCVar, LAYOUT_CURRENT);
    return layout >= LAYOUT_CURRENT && layout <= LAYOUT_CUSTOM ? layout : LAYOUT_CURRENT;
}

void MappingKey(int physical, char* out, size_t size) {
    std::snprintf(out, size, "%s%s", kMappingPrefix, kPhysical[physical].key);
}

uint16_t MappingFor(int layout, int physical) {
    if (physical < 0 || physical >= static_cast<int>(kPhysical.size())) {
        return 0;
    }
    if (layout == LAYOUT_OOT3D) {
        return kOot3dPreset[physical];
    }
    if (layout == LAYOUT_CUSTOM) {
        char key[64];
        MappingKey(physical, key, sizeof(key));
        return static_cast<uint16_t>(CVarGetInteger(key, 0)) & kManagedButtons;
    }
    return static_cast<uint16_t>(Soh3dsControls_CurrentMappingMask(physical)) & kManagedButtons;
}

const char* LogicalName(uint16_t mask) {
    for (const auto& logical : kLogical) {
        if (logical.mask == mask) {
            return logical.name;
        }
    }
    return "Multiple";
}

void StoreMapping(int physical, uint16_t mask) {
    char key[64];
    MappingKey(physical, key, sizeof(key));
    CVarSetInteger(key, mask);
}

void CaptureLayout(int layout) {
    for (int i = 0; i < static_cast<int>(kPhysical.size()); ++i) {
        StoreMapping(i, MappingFor(layout, i));
    }
}

void SetManagedCameraDefaults() {
    CVarSetInteger("gSettings.FreeLook.Enabled", 1);
    CVarSetInteger("gSettings.Controls.RightStickAim", 1);
    CVarSetInteger("gSettings.FirstPersonCameraSensitivity.Enabled", 1);
}

void SetTextRow(Soh3dsSettingsRow* out, int row, const char* label, const char* value, int kind,
                const char* tooltip = "") {
    std::snprintf(sLabels[row].data(), sLabels[row].size(), "%s", label);
    std::snprintf(sValues[row].data(), sValues[row].size(), "%s", value);
    out->label = sLabels[row].data();
    out->value = sValues[row].data();
    out->tooltip = tooltip;
    out->kind = kind;
    out->on = 0;
    out->disabled = 0;
}

void FormatDiagram(int row, bool shoulders) {
    const int layout = GetLayout();
    if (!shoulders) {
        std::snprintf(sLabels[row].data(), sLabels[row].size(), "A:%s  B:%s  X:%s  Y:%s",
                      LogicalName(MappingFor(layout, 0)), LogicalName(MappingFor(layout, 1)),
                      LogicalName(MappingFor(layout, 2)), LogicalName(MappingFor(layout, 3)));
    } else {
        std::snprintf(sLabels[row].data(), sLabels[row].size(), "L:%s R:%s ZL:%s ZR:%s",
                      LogicalName(MappingFor(layout, 4)), LogicalName(MappingFor(layout, 5)),
                      LogicalName(MappingFor(layout, 6)), LogicalName(MappingFor(layout, 7)));
    }
}

float Clamp(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

} // namespace

extern "C" int Soh3dsControls_RowCount(void) {
    return ROW_COUNT;
}

extern "C" int Soh3dsControls_Row(int row, Soh3dsSettingsRow* out) {
    if (out == nullptr || row < 0 || row >= ROW_COUNT) {
        return 0;
    }
    if (row == ROW_LAYOUT_HEADING) {
        SetTextRow(out, row, "Control layout", "", SOH3DS_ROW_HEADING);
    } else if (row == ROW_LAYOUT) {
        static constexpr const char* names[] = { "Current", "OoT3D", "Custom" };
        SetTextRow(out, row, "Layout", names[GetLayout()], SOH3DS_ROW_CHOICE,
                   "Current keeps saved mappings. OoT3D applies a console preset.");
    } else if (row == ROW_FACE_DIAGRAM || row == ROW_SHOULDER_DIAGRAM) {
        SetTextRow(out, row, "", "", SOH3DS_ROW_TEXT);
        FormatDiagram(row, row == ROW_SHOULDER_DIAGRAM);
        out->label = sLabels[row].data();
    } else if (row == ROW_BUTTON_HEADING) {
        SetTextRow(out, row, "Physical buttons", "", SOH3DS_ROW_HEADING);
    } else if (row >= ROW_BUTTON_FIRST && row <= ROW_BUTTON_LAST) {
        const int physical = row - ROW_BUTTON_FIRST;
        SetTextRow(out, row, kPhysical[physical].label, LogicalName(MappingFor(GetLayout(), physical)),
                   SOH3DS_ROW_CHOICE,
                   "Changing one button captures the current layout and preserves every other button");
    } else if (row == ROW_CAMERA_HEADING) {
        SetTextRow(out, row, "C-stick camera", "", SOH3DS_ROW_HEADING);
    } else if (row == ROW_DEADZONE) {
        char value[24];
        std::snprintf(value, sizeof(value), "%d%%", Soh3dsControls_GetCStickDeadzone());
        SetTextRow(out, row, "C-stick deadzone", value, SOH3DS_ROW_SLIDER);
    } else if (row == ROW_SENSITIVITY_X || row == ROW_SENSITIVITY_Y) {
        const bool x = row == ROW_SENSITIVITY_X;
        const float value = CVarGetFloat(x ? kSensitivityX : kSensitivityY, 1.0f);
        char formatted[24];
        std::snprintf(formatted, sizeof(formatted), "%.0f%%", value * 100.0f);
        SetTextRow(out, row, x ? "Camera X sensitivity" : "Camera Y sensitivity", formatted, SOH3DS_ROW_SLIDER);
    } else if (row == ROW_INVERT_X || row == ROW_INVERT_Y) {
        const bool x = row == ROW_INVERT_X;
        SetTextRow(out, row, x ? "Invert camera X" : "Invert camera Y", "", SOH3DS_ROW_TOGGLE);
        out->on = CVarGetInteger(x ? kInvertX : kInvertY, x ? 0 : 1) != 0;
    } else {
        return 0;
    }
    return 1;
}

extern "C" void Soh3dsControls_Adjust(int row, int dir) {
    if (dir == 0 || row < 0 || row >= ROW_COUNT) {
        return;
    }
    if (row == ROW_LAYOUT) {
        const int old = GetLayout();
        const int next = dir > 0 ? LAYOUT_OOT3D : LAYOUT_CURRENT;
        if (old == next) {
            return;
        }
        CVarSetInteger(kLayoutCVar, next);
        if (next == LAYOUT_OOT3D) {
            SetManagedCameraDefaults();
        }
        sWaitForRelease = true;
    } else if (row >= ROW_BUTTON_FIRST && row <= ROW_BUTTON_LAST) {
        const int physical = row - ROW_BUTTON_FIRST;
        const int oldLayout = GetLayout();
        if (oldLayout != LAYOUT_CUSTOM) {
            CaptureLayout(oldLayout);
        }
        const uint16_t current = MappingFor(oldLayout, physical);
        int choice = -1;
        for (int i = 0; i < static_cast<int>(kLogical.size()); ++i) {
            if (kLogical[i].mask == current) {
                choice = i;
                break;
            }
        }
        if (choice < 0) {
            choice = 0;
        } else if (dir > 0) {
            choice = (choice + 1) % static_cast<int>(kLogical.size());
        } else {
            choice = (choice + static_cast<int>(kLogical.size()) - 1) % static_cast<int>(kLogical.size());
        }
        StoreMapping(physical, kLogical[choice].mask);
        CVarSetInteger(kLayoutCVar, LAYOUT_CUSTOM);
        SetManagedCameraDefaults();
        sWaitForRelease = true;
    } else if (row == ROW_DEADZONE) {
        const int old = Soh3dsControls_GetCStickDeadzone();
        const int next = old + (dir > 0 ? 5 : -5);
        Soh3dsControls_SetCStickDeadzone(next < 0 ? 0 : next > 50 ? 50 : next);
    } else if (row == ROW_SENSITIVITY_X || row == ROW_SENSITIVITY_Y) {
        const bool x = row == ROW_SENSITIVITY_X;
        const char* freeLook = x ? kSensitivityX : kSensitivityY;
        const char* aiming = x ? kAimSensitivityX : kAimSensitivityY;
        const float next = Clamp(CVarGetFloat(freeLook, 1.0f) + (dir > 0 ? 0.1f : -0.1f), 0.5f, 2.0f);
        CVarSetFloat(freeLook, next);
        CVarSetFloat(aiming, next);
        SetManagedCameraDefaults();
    } else if (row == ROW_INVERT_X || row == ROW_INVERT_Y) {
        const bool x = row == ROW_INVERT_X;
        const char* freeLook = x ? kInvertX : kInvertY;
        const char* aiming = x ? kAimInvertX : kAimInvertY;
        const int next = CVarGetInteger(freeLook, x ? 0 : 1) ? 0 : 1;
        CVarSetInteger(freeLook, next);
        CVarSetInteger(aiming, next);
    } else {
        return;
    }
    CVarSave();
}

extern "C" void Soh3dsControls_TransformPad(uint32_t physicalButtons, uint16_t* buttons) {
    if (buttons == nullptr) {
        return;
    }
    if (sWaitForRelease) {
        *buttons &= static_cast<uint16_t>(~kManagedButtons);
        if (physicalButtons == 0) {
            sWaitForRelease = false;
        }
        return;
    }
    const int layout = GetLayout();
    if (layout == LAYOUT_CURRENT) {
        return;
    }
    uint16_t mapped = 0;
    for (int i = 0; i < static_cast<int>(kPhysical.size()); ++i) {
        if ((physicalButtons & kPhysical[i].bit) != 0) {
            mapped |= MappingFor(layout, i);
        }
    }
    *buttons = static_cast<uint16_t>((*buttons & ~kManagedButtons) | mapped);
}

#endif
