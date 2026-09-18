#pragma once
#include "../transport_test/3ds.h"
constexpr uint64_t SYSCLOCK_ARM11=268123480;
bool dspIsComponentLoaded();
Result DSP_LoadComponent(const void*,unsigned,uint16_t,uint16_t,bool*);
Result DSP_UnloadComponent();
Result DSP_ConvertProcessAddressFromDspDram(unsigned,u32*);
uint64_t svcGetSystemTick();
Result svcSleepThread(s64);
