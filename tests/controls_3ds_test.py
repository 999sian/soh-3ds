#!/usr/bin/env python3
"""Exercise the production native-controls bridge with host CVar/input boundaries."""

import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "third_party/shipwright/soh/soh/Enhancements/dualscreen3ds"
ULTRA = ROOT / "third_party/libultraship/include"
INPUT_POLICY = ROOT / "third_party/libultraship/src/libultraship/controller/controldeck/Controls3DS.cpp"

fixture = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <libultraship/controller/controldeck/Controls3DS.h>

static std::map<std::string, int> ints;
static std::map<std::string, float> floats;
static unsigned currentMappings[14];
static int deadzone = 20;
static int deadzoneWrites = 0;
static int saves = 0;

extern "C" int32_t CVarGetInteger(const char* key, int32_t fallback) {
    auto it = ints.find(key);
    return it == ints.end() ? fallback : it->second;
}
extern "C" float CVarGetFloat(const char* key, float fallback) {
    auto it = floats.find(key);
    return it == floats.end() ? fallback : it->second;
}
extern "C" void CVarSetInteger(const char* key, int32_t value) { ints[key] = value; }
extern "C" void CVarSetFloat(const char* key, float value) { floats[key] = value; }
extern "C" void CVarSave() { ++saves; }
extern "C" unsigned Soh3dsControls_CurrentMappingMask(int physical) {
    return physical >= 0 && physical < 14 ? currentMappings[physical] : 0;
}
extern "C" int Soh3dsControls_GetCStickDeadzone() { return deadzone; }
extern "C" void Soh3dsControls_SetCStickDeadzone(int value) {
    deadzone = value;
    ++deadzoneWrites;
}

#include "ControlsBridge3DS.cpp"

static int FindRow(const char* label) {
    Soh3dsSettingsRow row{};
    for (int i = 0; i < Soh3dsControls_RowCount(); ++i) {
        assert(Soh3dsControls_Row(i, &row));
        if (std::strcmp(row.label, label) == 0) return i;
    }
    std::cerr << "missing row: " << label << '\n';
    std::exit(1);
}

static std::string Value(const char* label) {
    Soh3dsSettingsRow row{};
    assert(Soh3dsControls_Row(FindRow(label), &row));
    return row.value;
}

int main() {
    // The live mapping query parses the concrete persisted mapping IDs. Wrong
    // physical indices or accepting a trigger direction's opposite half would
    // make the native diagram disagree with the controller that is read.
    assert(Soh3dsControls_MappingIdMatches(0, "P0-B32768-SDLB0"));
    assert(!Soh3dsControls_MappingIdMatches(0, "P0-B32768-SDLB1"));
    assert(Soh3dsControls_MappingIdMatches(6, "P0-B32-SDLA4-ADP"));
    assert(!Soh3dsControls_MappingIdMatches(6, "P0-B32-SDLA4-ADN"));
    assert(Soh3dsControls_MappingIdMatches(13, "P0-B16-SDLB4"));

    // A change that made ordinary boot select a managed layout would break
    // existing saved/manual mappings. Current/manual must be a pure pass-through.
    assert(Value("Layout") == "Current");
    uint16_t buttons = BTN_Z | BTN_DLEFT;
    Soh3dsControls_TransformPad(SOH3DS_PHYS_A | SOH3DS_PHYS_X, &buttons);
    assert(buttons == (BTN_Z | BTN_DLEFT));
    assert(saves == 0);

    // Applying the preset is coherent and held input is suppressed through a
    // complete release, so rebinding cannot manufacture a same-frame press.
    Soh3dsControls_Adjust(FindRow("Layout"), +1);
    assert(Value("Layout") == "OoT3D");
    buttons = BTN_R;
    Soh3dsControls_TransformPad(SOH3DS_PHYS_A, &buttons);
    assert(buttons == 0);
    buttons = BTN_R;
    Soh3dsControls_TransformPad(0, &buttons);
    assert(buttons == 0);
    buttons = 0;
    Soh3dsControls_TransformPad(SOH3DS_PHYS_A | SOH3DS_PHYS_X | SOH3DS_PHYS_ZR, &buttons);
    assert(buttons == (BTN_A | BTN_CLEFT | BTN_CRIGHT));

    // Selecting Current/manual stops policy mapping without altering the user's
    // underlying mappings.
    Soh3dsControls_Adjust(FindRow("Layout"), -1);
    assert(Value("Layout") == "Current");
    buttons = BTN_L;
    Soh3dsControls_TransformPad(SOH3DS_PHYS_A, &buttons);
    assert(buttons == 0); // release barrier after switching
    Soh3dsControls_TransformPad(0, &buttons);
    buttons = BTN_L;
    Soh3dsControls_TransformPad(SOH3DS_PHYS_A, &buttons);
    assert(buttons == BTN_L);

    // Editing one physical button captures every current mapping, then changes
    // only that row. This preserves all other mappings in the custom layout.
    currentMappings[0] = BTN_A;
    currentMappings[1] = BTN_B;
    currentMappings[4] = BTN_Z;
    currentMappings[5] = BTN_R;
    currentMappings[8] = BTN_CUP;
    Soh3dsControls_Adjust(FindRow("A button"), +1);
    assert(Value("Layout") == "Custom");
    assert(Value("A button") == "B");
    buttons = 0;
    Soh3dsControls_TransformPad(0, &buttons);
    buttons = 0;
    Soh3dsControls_TransformPad(SOH3DS_PHYS_A | SOH3DS_PHYS_L | SOH3DS_PHYS_DUP, &buttons);
    assert(buttons == (BTN_B | BTN_Z | BTN_CUP));

    // An existing profile can have free-look disabled after its one-time
    // controller setup. The native page must expose a persistent recovery
    // toggle without changing the user's button layout.
    ints["gSettings.FreeLook.Enabled"] = 0;
    ints["gSoh3dsInputLayout"] = 1;
    const int cameraRow = FindRow("Enable C-stick camera");
    Soh3dsSettingsRow camera{};
    assert(Soh3dsControls_Row(cameraRow, &camera));
    assert(camera.kind == SOH3DS_ROW_TOGGLE && !camera.on);
    const auto mappingsBeforeCamera = currentMappings[0];
    const int savesBeforeCamera = saves;
    Soh3dsControls_Adjust(cameraRow, +1);
    assert(Soh3dsControls_Row(cameraRow, &camera) && camera.on);
    assert(ints["gSettings.FreeLook.Enabled"] == 1);
    assert(saves == savesBeforeCamera + 1);
    assert(Value("Layout") == "Custom");
    assert(currentMappings[0] == mappingsBeforeCamera);
    Soh3dsControls_Adjust(cameraRow, -1);
    assert(Soh3dsControls_Row(cameraRow, &camera) && !camera.on);
    assert(saves == savesBeforeCamera + 2);
    Soh3dsControls_Adjust(FindRow("Camera X sensitivity"), +1);
    assert(ints["gSettings.FreeLook.Enabled"] == 0);

    // Sliders clamp at their supported boundaries and write through the real
    // engine-facing deadzone/camera configuration boundaries.
    const int dzRow = FindRow("C-stick deadzone");
    for (int i = 0; i < 20; ++i) Soh3dsControls_Adjust(dzRow, -1);
    assert(deadzone == 0);
    for (int i = 0; i < 20; ++i) Soh3dsControls_Adjust(dzRow, +1);
    assert(deadzone == 50);
    assert(deadzoneWrites > 0);

    const int sxRow = FindRow("Camera X sensitivity");
    const int syRow = FindRow("Camera Y sensitivity");
    for (int i = 0; i < 30; ++i) Soh3dsControls_Adjust(sxRow, -1);
    for (int i = 0; i < 30; ++i) Soh3dsControls_Adjust(syRow, +1);
    assert(std::fabs(floats["gSettings.FreeLook.CameraSensitivity.X"] - 0.5f) < 0.001f);
    assert(std::fabs(floats["gSettings.FirstPersonCameraSensitivity.X"] - 0.5f) < 0.001f);
    assert(std::fabs(floats["gSettings.FreeLook.CameraSensitivity.Y"] - 2.0f) < 0.001f);
    assert(std::fabs(floats["gSettings.FirstPersonCameraSensitivity.Y"] - 2.0f) < 0.001f);

    Soh3dsControls_Adjust(FindRow("Invert camera X"), +1);
    assert(ints["gSettings.FreeLook.InvertXAxis"] == 1);
    assert(ints["gSettings.Controls.InvertAimingXAxis"] == 1);
    Soh3dsControls_Adjust(FindRow("Invert camera Y"), +1);
    assert(ints["gSettings.FreeLook.InvertYAxis"] == 0);
    assert(ints["gSettings.Controls.InvertAimingYAxis"] == 0);

    Soh3dsSettingsRow invalid{};
    assert(!Soh3dsControls_Row(-1, &invalid));
    assert(!Soh3dsControls_Row(Soh3dsControls_RowCount(), &invalid));
    assert(!Soh3dsControls_Row(0, nullptr));
    std::cout << "native 3DS controls policy regression passed\n";
}
'''

with tempfile.TemporaryDirectory(prefix="soh-controls-3ds-") as temporary:
    temporary = Path(temporary)
    source = temporary / "controls_test.cpp"
    source.write_text(fixture)
    binary = temporary / "controls_test"
    subprocess.run([
        os.environ.get("CXX", "g++"), "-std=c++20", "-O1", "-Wall", "-Wextra",
        "-Werror", "-DSOH3DS_CONTROLS_TEST", "-I", str(BRIDGE), "-I", str(ULTRA),
        str(source), str(INPUT_POLICY), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
