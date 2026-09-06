#!/usr/bin/env python3
"""Exercise the production 3DS camera-mapping migration with controller boundaries."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'third_party/libultraship/src/libultraship/controller/controldeck/ControlDeck.cpp').read_text()
signature = 'void Soh3dsControls_EnsureCameraMappings('
assert signature in source, 'Missing automatic C-stick mapping recovery'
start = source.index(signature)
end = source.index('\n}', start) + 2
function = source[start:end]
assert 'Soh3dsControls_EnsureCameraMappings(mPorts[0]->GetConnectedController());' in source
fixture = r'''
#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <cstdio>
#include <vector>
constexpr int SDL_CONTROLLER_AXIS_RIGHTX = 2, SDL_CONTROLLER_AXIS_RIGHTY = 3;
std::map<std::string, int> vars;
int saves = 0, mappingSaves = 0;
int CVarGetInteger(const char* key, int fallback) {
    auto it = vars.find(key); return it == vars.end() ? fallback : it->second;
}
void CVarSetInteger(const char* key, int value) { vars[key] = value; }
void CVarSave() { ++saves; }
namespace Ship {
enum Direction { LEFT, RIGHT, UP, DOWN };
enum StickIndex { LEFT_STICK, RIGHT_STICK };
struct SDLAxisDirectionToAxisDirectionMapping {
    int port, stick, direction, axis, sign;
    SDLAxisDirectionToAxisDirectionMapping(int p, int s, int d, int a, int v)
        : port(p), stick(s), direction(d), axis(a), sign(v) {}
    void SaveToConfig() { ++mappingSaves; }
};
struct Stick {
    std::map<Direction, std::vector<std::shared_ptr<SDLAxisDirectionToAxisDirectionMapping>>> mappings;
    int idSaves = 0;
    auto GetAllAxisDirectionMappingByDirection(Direction d) { return mappings[d]; }
    void AddAxisDirectionMapping(Direction d, std::shared_ptr<SDLAxisDirectionToAxisDirectionMapping> m) {
        mappings[d].push_back(m);
    }
    void SaveAxisDirectionMappingIdsToConfig() { ++idSaves; }
};
struct Controller {
    std::shared_ptr<Stick> stick = std::make_shared<Stick>();
    auto GetRightStick() { return stick; }
};
}
''' + function + r'''
int main() {
    // A saved console profile may already have its button-layout migration,
    // but no right-stick mappings at all. Free-look alone cannot fix that.
    vars["gSoh3dsInputLayout"] = 1;
    Soh3dsControls_EnsureCameraMappings(nullptr);
    assert(saves == 0);
    auto c = std::make_shared<Ship::Controller>();
    Soh3dsControls_EnsureCameraMappings(c);
    for (int d = 0; d < 4; ++d) {
        const auto& ms = c->stick->mappings[static_cast<Ship::Direction>(d)];
        assert(ms.size() == 1);
        auto m = ms.front();
        assert(m->port == 0 && m->stick == Ship::RIGHT_STICK && m->direction == d);
        assert(m->axis == (d < 2 ? 2 : 3));
        assert(m->sign == (d % 2 == 0 ? -1 : 1));
    }
    assert(vars["gSettings.FreeLook.Enabled"] == 1);
    assert(mappingSaves == 4 && c->stick->idSaves == 1 && saves == 1);
    Soh3dsControls_EnsureCameraMappings(c);
    assert(mappingSaves == 4 && saves == 1);
    // Migration must preserve custom directions and an explicit Off setting.
    vars.clear(); saves = mappingSaves = 0;
    vars["gSettings.FreeLook.Enabled"] = 0;
    c = std::make_shared<Ship::Controller>();
    auto custom = std::make_shared<Ship::SDLAxisDirectionToAxisDirectionMapping>(0,1,0,4,1);
    c->stick->mappings[Ship::LEFT].push_back(custom);
    Soh3dsControls_EnsureCameraMappings(c);
    assert(c->stick->mappings[Ship::LEFT].front() == custom);
    assert(mappingSaves == 3 && saves == 1);
    assert(vars["gSettings.FreeLook.Enabled"] == 0);
    // A fully configured stick requires no mapping writes on migration.
    vars.erase("gSoh3dsCameraMappingVersion");
    mappingSaves = 0;
    Soh3dsControls_EnsureCameraMappings(c);
    assert(mappingSaves == 0);
    std::puts("C-stick defaults: missing/partial profiles, custom bindings, Off and idempotence pass");
}
'''
with tempfile.TemporaryDirectory(prefix='soh-cstick-defaults-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(fixture)
    subprocess.run([os.getenv('CXX', 'g++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
