"""Compile the real settings policy against minimal SDK/output boundaries."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = root / 'src/compat3ds/logging_3ds.cpp'
assert source.exists(), 'Menu-controlled logging policy is missing'
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    for directory in ('sys', 'spdlog', 'libultraship/bridge'):
        (tmp / directory).mkdir(parents=True)
    (tmp / '3ds.h').write_text('enum {debugDevice_NULL,debugDevice_SVC}; void consoleDebugInit(int);')
    (tmp / 'sys/iosupport.h').write_text('enum {STD_OUT,STD_ERR}; extern const void* devoptab_list[2];')
    (tmp / 'spdlog/spdlog.h').write_text('namespace spdlog { namespace level { enum level_enum {info,off}; } void set_level(level::level_enum); }')
    (tmp / 'libultraship/bridge/consolevariablebridge.h').write_text('int CVarGetInteger(const char*,int);')
    test = tmp / 'test.cpp'
    test.write_text(r'''
#include "ship/utils/logging_3ds.h"
#include <3ds.h>
#include <sys/iosupport.h>
#include <spdlog/spdlog.h>
#include <cassert>
#include <map>
#include <string>
std::map<std::string,int> values;
int CVarGetInteger(const char* key,int fallback) { auto p=values.find(key);return p==values.end()?fallback:p->second; }
int devices[3];
const void* devoptab_list[2]={&devices[0],&devices[0]};
int selected=-1;
void consoleDebugInit(int device) { selected=device;devoptab_list[STD_ERR]=&devices[device]; }
spdlog::level::level_enum level=spdlog::level::info;
void spdlog::set_level(spdlog::level::level_enum v) {::level=v;}
int main() {
 assert(Soh3dsLoggingFlags()==0);
 Soh3dsApplyLoggingSettings();
 assert(Soh3dsLoggingFlags()==0 && selected==debugDevice_NULL && level==spdlog::level::off);
 values["gDeveloperTools.FrameLogging"]=1;
 Soh3dsApplyLoggingSettings();assert(Soh3dsLoggingFlags()==0);
 values["gDeveloperTools.DebugLogging"]=1;
 Soh3dsApplyLoggingSettings();
 assert(Soh3dsLoggingEnabled(SOH3DS_LOG_GENERAL|SOH3DS_LOG_FRAMES));
 assert(!Soh3dsLoggingEnabled(SOH3DS_LOG_PROFILE));
 assert(selected==debugDevice_SVC && devoptab_list[STD_OUT]==devoptab_list[STD_ERR]);
 // A visible boot/setup console must not be replaced by log routing.
 devoptab_list[STD_OUT]=&devices[2];
 values["gDeveloperTools.DebugLogging"]=0;
 Soh3dsApplyLoggingSettings();
 assert(Soh3dsLoggingFlags()==0 && selected==debugDevice_NULL && level==spdlog::level::off);
 assert(devoptab_list[STD_OUT]==&devices[2]);
}
''')
    exe = tmp / 'test'
    subprocess.run(['c++', '-std=c++17', '-I'+str(tmp),
                    '-I'+str(root/'third_party/libultraship/include'), str(source), str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Logging defaults off, master gates traces, live changes route output, visible console preserved')
