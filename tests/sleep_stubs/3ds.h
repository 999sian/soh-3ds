#pragma once
#include <cstdint>
enum APT_HookType { APTHOOK_ONSUSPEND, APTHOOK_ONRESTORE, APTHOOK_ONSLEEP, APTHOOK_ONWAKEUP, APTHOOK_ONEXIT };
struct aptHookCookie { int unused; };
using aptHookFn = void (*)(APT_HookType, void*);
void aptHook(aptHookCookie*, aptHookFn, void*);
void aptUnhook(aptHookCookie*);
