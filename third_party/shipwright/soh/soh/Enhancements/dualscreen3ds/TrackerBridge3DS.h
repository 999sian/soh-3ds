#pragma once

#include "SettingsBridge3DS.h"

#ifdef __cplusplus
extern "C" {
#endif

// Read-only bottom-screen view of save and check-tracker state. Returned
// strings remain valid until the next Soh3dsTracker_* call.
int Soh3dsTracker_PageCount(void);
const char* Soh3dsTracker_PageName(int page);
int Soh3dsTracker_RowCount(int page);
int Soh3dsTracker_Row(int page, int row, Soh3dsSettingsRow* out);

#ifdef __cplusplus
}
#endif
