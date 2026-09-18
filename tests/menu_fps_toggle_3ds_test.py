#!/usr/bin/env python3
"""Verify the 20/30/60 FPS toggle on the 3DS bottom screen menu."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BOTTOM_SCREEN = ROOT / "third_party/shipwright/soh/soh/Enhancements/dualscreen3ds/BottomScreen3DS.c"
source = BOTTOM_SCREEN.read_text()

def extract_func(src, signature):
    idx = src.index(signature)
    brace = src.index("{", idx)
    depth = 1
    i = brace + 1
    while depth > 0:
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
        i += 1
    return src[idx:i]

bs_row_count = extract_func(source, "static int Bs_RowCount(int page)")
bs_get_row = extract_func(source, "static int Bs_GetRow(int page, int row, Soh3dsSettingsRow* out)")
bs_adjust_row = extract_func(source, "static void Bs_AdjustRow(int page, int row, int dir)")

test_source = f"""
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#define CVAR_ENHANCEMENT(x) ("gEnhancements." x)
#define CVAR_SETTING(x) ("gSettings." x)
#define SOH3DS_TAB_TRACKER 3
#define SOH3DS_TAB_SETTINGS 4

enum Soh3dsRowKind {{
    SOH3DS_ROW_HEADING,
    SOH3DS_ROW_TEXT,
    SOH3DS_ROW_TOGGLE,
    SOH3DS_ROW_CHOICE,
    SOH3DS_ROW_SLIDER,
}};

struct Soh3dsSettingsRow {{
    const char* label;
    const char* value;
    const char* tooltip;
    int kind;
    int on;
    int disabled;
}};

static struct {{
    int tab;
    int page;
}} sUi = {{ SOH3DS_TAB_SETTINGS, 0 }};

std::map<std::string, int> gIntCvars;
std::map<std::string, float> gFloatCvars;
static bool sOldModel = false;
static int sSaveCount = 0;

int Soh3dsUsesOldProfile(void) {{ return sOldModel ? 1 : 0; }}
int Soh3dsTracker_RowCount(int) {{ return 0; }}
int Soh3dsTracker_Row(int, int, Soh3dsSettingsRow*) {{ return 0; }}
int Soh3dsControls_RowCount() {{ return 0; }}
int Soh3dsControls_Row(int, Soh3dsSettingsRow*) {{ return 0; }}
void Soh3dsControls_Adjust(int, int) {{}}
int Soh3dsSettings_RowCount(int) {{ return 0; }}
int Soh3dsSettings_Row(int, int, Soh3dsSettingsRow*) {{ return 0; }}
void Soh3dsSettings_Adjust(int, int, int) {{}}

int CVarGetInteger(const char* name, int def) {{
    auto it = gIntCvars.find(name);
    return it != gIntCvars.end() ? it->second : def;
}}

void CVarSetInteger(const char* name, int val) {{
    gIntCvars[name] = val;
}}

float CVarGetFloat(const char* name, float def) {{
    auto it = gFloatCvars.find(name);
    return it != gFloatCvars.end() ? it->second : def;
}}

void CVarSetFloat(const char* name, float val) {{
    gFloatCvars[name] = val;
}}

void CVarSave(void) {{
    ++sSaveCount;
}}

{bs_row_count}

{bs_get_row}

{bs_adjust_row}

int main() {{
    // 1. Verify Page 0 row count is 5
    assert(Bs_RowCount(0) == 5);

    // 2. Check row definitions
    Soh3dsSettingsRow row;
    assert(Bs_GetRow(0, 0, &row) == 1 && row.kind == SOH3DS_ROW_HEADING && strcmp(row.label, "Dual screen") == 0);
    assert(Bs_GetRow(0, 1, &row) == 1 && row.kind == SOH3DS_ROW_TOGGLE);
    assert(Bs_GetRow(0, 2, &row) == 1 && row.kind == SOH3DS_ROW_CHOICE);
    assert(Bs_GetRow(0, 3, &row) == 1 && row.kind == SOH3DS_ROW_HEADING && strcmp(row.label, "Performance") == 0);
    assert(Bs_GetRow(0, 4, &row) == 1 && row.kind == SOH3DS_ROW_CHOICE && strcmp(row.label, "Target FPS") == 0);

    // 3. New 3DS default (30 FPS)
    sOldModel = false;
    gIntCvars[CVAR_SETTING("InterpolationFPS")] = 30;
    gIntCvars[CVAR_SETTING("MatchRefreshRate")] = 0;
    assert(Bs_GetRow(0, 4, &row) == 1);
    assert(row.disabled == 0);
    assert(strcmp(row.value, "30 FPS") == 0);

    // 4. Step forward (+1): 30 -> 60
    Bs_AdjustRow(0, 4, 1);
    assert(gIntCvars[CVAR_SETTING("InterpolationFPS")] == 60);
    assert(gIntCvars[CVAR_SETTING("MatchRefreshRate")] == 1);
    assert(Bs_GetRow(0, 4, &row) == 1);
    assert(strcmp(row.value, "60 FPS") == 0);

    // 5. Step forward (+1): 60 -> 20
    Bs_AdjustRow(0, 4, 1);
    assert(gIntCvars[CVAR_SETTING("InterpolationFPS")] == 20);
    assert(gIntCvars[CVAR_SETTING("MatchRefreshRate")] == 0);
    assert(Bs_GetRow(0, 4, &row) == 1);
    assert(strcmp(row.value, "20 FPS") == 0);

    // 6. Step forward (+1): 20 -> 30
    Bs_AdjustRow(0, 4, 1);
    assert(gIntCvars[CVAR_SETTING("InterpolationFPS")] == 30);
    assert(gIntCvars[CVAR_SETTING("MatchRefreshRate")] == 0);
    assert(Bs_GetRow(0, 4, &row) == 1);
    assert(strcmp(row.value, "30 FPS") == 0);

    // 7. Step backward (-1): 30 -> 20
    Bs_AdjustRow(0, 4, -1);
    assert(gIntCvars[CVAR_SETTING("InterpolationFPS")] == 20);
    assert(gIntCvars[CVAR_SETTING("MatchRefreshRate")] == 0);
    assert(Bs_GetRow(0, 4, &row) == 1);
    assert(strcmp(row.value, "20 FPS") == 0);

    // 8. Step backward (-1): 20 -> 60
    Bs_AdjustRow(0, 4, -1);
    assert(gIntCvars[CVAR_SETTING("InterpolationFPS")] == 60);
    assert(gIntCvars[CVAR_SETTING("MatchRefreshRate")] == 1);
    assert(Bs_GetRow(0, 4, &row) == 1);
    assert(strcmp(row.value, "60 FPS") == 0);

    // 9. Old 3DS Model: locked to 20 FPS and disabled
    sOldModel = true;
    assert(Bs_GetRow(0, 4, &row) == 1);
    assert(row.disabled == 1);
    assert(strcmp(row.value, "20 FPS") == 0);
    int prevSaves = sSaveCount;
    Bs_AdjustRow(0, 4, 1);
    // Should NOT have adjusted or saved
    assert(sSaveCount == prevSaves);

    std::printf("Bottom screen 20/30/60 FPS toggle tests PASSED!\\n");
    return 0;
}}
"""

with tempfile.TemporaryDirectory(prefix="soh-fps-toggle-test-") as tmpdir:
    tmpdir = Path(tmpdir)
    cpp_file = tmpdir / "test.cpp"
    bin_file = tmpdir / "test"
    cpp_file.write_text(test_source)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-O2", str(cpp_file), "-o", str(bin_file)], check=True)
    subprocess.run([str(bin_file)], check=True)
