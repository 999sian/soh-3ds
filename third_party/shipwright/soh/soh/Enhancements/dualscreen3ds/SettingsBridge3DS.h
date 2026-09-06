#pragma once
// SoH-3DS: C view of SoH's port-menu widget tree for the bottom-screen
// Settings tab (implementation: SettingsBridge3DS.cpp). Pages are the menu's
// sidebars ("Quality of Life", "Audio", ...); rows are their widgets in
// column order, filtered to what a touch list can show. Row visibility is
// re-evaluated on every RowCount/Adjust call (SoH's preFunc protocol), so
// query RowCount before Row within a frame. Strings are valid until the next
// call.

#ifdef __cplusplus
extern "C" {
#endif

enum Soh3dsRowKind {
    SOH3DS_ROW_HEADING, // separator text
    SOH3DS_ROW_TEXT,    // static text
    SOH3DS_ROW_TOGGLE,  // checkbox: `on`
    SOH3DS_ROW_CHOICE,  // combobox: `value` is the current option label
    SOH3DS_ROW_SLIDER,  // int/float slider: `value` formatted
};

typedef struct Soh3dsSettingsRow {
    const char* label;
    const char* value;
    const char* tooltip;
    int kind;
    int on;
    int disabled;
} Soh3dsSettingsRow;

int Soh3dsSettings_PageCount(void); // builds the tree on first call
const char* Soh3dsSettings_PageName(int page);
int Soh3dsSettings_RowCount(int page);
int Soh3dsSettings_Row(int page, int row, Soh3dsSettingsRow* out);
// dir: +1 next/increase/toggle, -1 previous/decrease/toggle.
void Soh3dsSettings_Adjust(int page, int row, int dir);

#ifdef __cplusplus
}
#endif
