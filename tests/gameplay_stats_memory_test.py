#!/usr/bin/env python3
"""Exercise the real breakdown renderer: row output, long names and save bounds."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'third_party/shipwright/soh/soh/Enhancements/gameplaystats.cpp').read_text()
body = source[source.index('void DrawGameplayStatsBreakdownTab()'):source.index('void DrawGameplayStatsOptionsTab()')]
formatter = source[source.index('std::string formatTimestampGameplayStat('):source.index('std::string formatIntGameplayStat(')]
# Keep any production display array in the fixture so the old implementation
# runs too; output checks cover its intra-array fixed-name overwrite as well.
display = re.search(r'^TimestampInfo sceneTimestampDisplay\[.*?;', source, re.M)
save_header = (ROOT / 'third_party/shipwright/soh/include/z64save.h').read_text()
scene_type = re.search(r'typedef struct \{[^{}]*\} SceneTimestamp;', save_header).group()
code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <fmt/format.h>
using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t;
struct ImVec4 { float x, y, z, w; };
struct ImVec2 { float x, y; };
#define COLOR_GREY ImVec4(0.78f, 0.78f, 0.78f, 1.0f)
#define CVAR_GAMEPLAY_STATS(x) x
#define ARRAY_COUNT(x) (sizeof(x) / sizeof((x)[0]))
#define SCENE_GROTTOS 62
bool roomBreakdown = false;
int CVarGetInteger(const char*, int) { return roomBreakdown; }
namespace spdlog { namespace fmt_lib = fmt; }
enum { ImGuiStyleVar_CellPadding, ImGuiTableFlags_BordersOuter, ImGuiTableColumnFlags_WidthStretch };
namespace ImGui {
void PushStyleVar(int, ImVec2) {}
void BeginTable(const char*, int, int) {}
void TableSetupColumn(const char*, int) {}
void EndTable() {}
void PopStyleVar(int) {}
}
struct TimestampInfo { char name[40]; u32 time; ImVec4 color; bool isRoom; };
''' + scene_type + r'''
struct Stats {
    SceneTimestamp sceneTimestamps[8191]{};
    u32 tsIdx = 0, sceneNum = 1, roomNum = 4, roomTimer = 60, sceneTimer = 100;
};
struct { struct { Stats stats; } ship; } gSaveContext;
#define CURRENT_MODE_TIMER (roomBreakdown ? gSaveContext.ship.stats.roomTimer : gSaveContext.ship.stats.sceneTimer)
std::string ResolveSceneID(u32 scene, u32) {
    if (scene == SCENE_GROTTOS) return "Grotto";
    if (scene == 2) return std::string(100, 'L');
    return "Forest";
}
struct Row { std::string name, value; ImVec4 color; };
std::vector<Row> rows;
void GameplayStatsRow(const char* name, const std::string& value, ImVec4 color = {1, 1, 1, 1}) {
    rows.push_back({name, value, color});
}
''' + (display.group() if display else '') + '\n' + formatter + body + r'''
int main() {
    auto& stats = gSaveContext.ship.stats;
    auto& a = stats.sceneTimestamps[0];
    a.scene = 1; a.room = 2; a.sceneTime = 123; a.roomTime = 45; a.isRoom = false;
    auto& b = stats.sceneTimestamps[1];
    b.scene = 1; b.room = 3; b.sceneTime = 0; b.roomTime = 70; b.isRoom = true;
    auto& c = stats.sceneTimestamps[2];
    c.scene = SCENE_GROTTOS; c.room = 1; c.sceneTime = 90; c.roomTime = 20; c.isRoom = false;
    stats.tsIdx = 3;
    DrawGameplayStatsBreakdownTab();
    assert(rows.size() == 3);
    assert(rows[0].name == "Forest" && rows[0].value == "0:00:12.3");
    assert(rows[1].name == "Grotto" && rows[1].value == "0:00:09.0");
    assert(rows[2].name == "Forest" && rows[2].value == "0:00:05.0");
    assert(rows[0].color.x == 0.78f && rows[2].color.x == 1.0f);
    roomBreakdown = true; rows.clear();
    DrawGameplayStatsBreakdownTab();
    assert(rows.size() == 4);
    assert(rows[0].name == "Forest Room 2" && rows[0].value == "0:00:04.5");
    assert(rows[1].name == "Forest Room 3" && rows[1].value == "0:00:07.0");
    assert(rows[2].name == "Grotto" && rows[2].value == "0:00:02.0");
    assert(rows[3].name == "Forest Room 4" && rows[3].value == "0:00:03.0");
    // A long resolved label must not overwrite adjacent timestamp fields.
    a.scene = 2; stats.tsIdx = 1; rows.clear();
    DrawGameplayStatsBreakdownTab();
    assert(rows[0].name == std::string(100, 'L') + " Room 2");
    assert(rows[0].value == "0:00:04.5");
    // Empty history still displays the live timer. A corrupt saved count must
    // never index beyond the fixed history that belongs to the save format.
    stats.tsIdx = 0; rows.clear(); DrawGameplayStatsBreakdownTab(); assert(rows.size() == 1);
    stats = Stats{};
    auto& last = stats.sceneTimestamps[8190];
    last.scene = 1; last.room = 9; last.roomTime = 20;
    stats.tsIdx = UINT32_MAX; rows.clear(); DrawGameplayStatsBreakdownTab();
    assert(rows.size() == 2 && rows[0].name == "Forest Room 9");
    puts("Statistics rows, room/grotto modes, long names, empty history and save bounds passed");
}
'''
with tempfile.TemporaryDirectory(prefix='soh-stats-memory-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-g', '-O1',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                    str(p / 'test.cpp'), '-lfmt', '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
