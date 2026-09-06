// SoH-3DS: native walker over SoH's declarative port menu so the bottom-screen
// Settings tab can expose every enhancement/setting without ImGui. The menu
// tree (SohMenu::AddMenuSettings/AddMenuEnhancements) is pure data; it is
// built lazily on the first query because InitOTR skips it for boot time.
// Each page is one sidebar with its columns flattened; only rows a touch list
// can show are kept (CVar checkbox/combobox/sliders, headings, text).
#ifdef __3DS__
#include "SettingsBridge3DS.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/UIWidgetOptions.hpp"
#include "soh/ShipInit.hpp"

namespace {

struct Page {
    std::string name;
    std::vector<WidgetInfo*> widgets;
};

std::vector<Page> sPages;
bool sBuilt = false;
int sVisiblePage = -1;
std::vector<int> sVisible; // indices into sPages[sVisiblePage].widgets after preFuncs
char sLabel[96];
char sValue[48];

bool Keep(const WidgetInfo& w) {
    switch (w.type) {
        case WIDGET_CVAR_CHECKBOX:
        case WIDGET_CVAR_COMBOBOX:
        case WIDGET_CVAR_SLIDER_INT:
        case WIDGET_CVAR_SLIDER_FLOAT:
            return w.cVar != nullptr;
        case WIDGET_SEPARATOR_TEXT:
        case WIDGET_TEXT:
            return true;
        default:
            return false;
    }
}

bool Adjustable(const WidgetInfo& w) {
    return w.type == WIDGET_CVAR_CHECKBOX || w.type == WIDGET_CVAR_COMBOBOX || w.type == WIDGET_CVAR_SLIDER_INT ||
           w.type == WIDGET_CVAR_SLIDER_FLOAT;
}

void Build() {
    if (sBuilt) {
        return;
    }
    sBuilt = true;
    auto menu = SohGui::GetSohMenu();
    if (menu == nullptr) {
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    // Settings + Enhancements only: Randomizer needs Rando::Settings, Network
    // is stubbed, Dev Tools are ImGui windows. Each Add* is self-contained.
    if (menu->MenuEntries().empty()) {
        menu->AddMenuSettings();
        menu->AddMenuEnhancements();
    }
    size_t widgets = 0;
    for (const auto& section : menu->MenuOrder()) {
        auto& entry = menu->MenuEntries().at(section);
        for (const auto& sidebarName : entry.sidebarOrder) {
            // Desktop-only pages: renderer/window knobs whose callbacks reach
            // the window backend, and ImGui notifications that never draw here.
            if (section == "Settings" && (sidebarName == "Graphics" || sidebarName == "Notifications")) {
                continue;
            }
            auto& sidebar = entry.sidebars.at(sidebarName);
            Page page;
            page.name = sidebarName;
            bool adjustable = false;
            for (auto& column : sidebar.columnWidgets) {
                for (auto& w : column) {
                    // Its callback inserts/removes a sidebar, which reallocates
                    // the widget vectors this walker points into.
                    if (w.name.rfind("Search In Sidebar", 0) == 0) {
                        continue;
                    }
                    if (Keep(w)) {
                        page.widgets.push_back(&w);
                        adjustable |= Adjustable(w);
                    }
                }
            }
            if (adjustable) {
                widgets += page.widgets.size();
                sPages.push_back(std::move(page));
            }
        }
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    char line[96];
    snprintf(line, sizeof(line), "soh-3ds settings: %zu pages, %zu rows, built in %lld ms\n", sPages.size(), widgets,
             (long long)ms);
    fputs(line, stderr);
}

// Run the per-frame visibility protocol Menu::MenuDrawItem uses (disabledMap
// refresh, ResetDisables, preFunc) and cache which rows of the page survive.
void Refresh(int page) {
    sVisible.clear();
    sVisiblePage = page;
    if (page < 0 || page >= static_cast<int>(sPages.size())) {
        return;
    }
    auto menu = SohGui::GetSohMenu();
    for (auto& [key, info] : menu->DisabledMap()) {
        info.active = info.evaluation(info);
    }
    const bool raceDisable = CVarGetInteger(CVAR_SETTING("DisableChanges"), 0) != 0;
    auto& widgets = sPages[page].widgets;
    for (size_t i = 0; i < widgets.size(); i++) {
        WidgetInfo& w = *widgets[i];
        if (w.preFunc != nullptr) {
            w.ResetDisables();
            w.preFunc(w);
            if (w.isHidden) {
                continue;
            }
            if (!w.activeDisables.empty()) {
                w.options->disabled = true;
            }
        }
        if (w.raceDisable && raceDisable) {
            w.options->disabled = true;
        }
        sVisible.push_back(static_cast<int>(i));
    }
}

// "Label##id" -> "Label"; a '%' in a slider label takes the value.
void FormatLabel(const WidgetInfo& w, int ivalue, float fvalue, bool isFloat) {
    std::string name = w.name.substr(0, w.name.find("##"));
    if (name.find('%') != std::string::npos && (w.type == WIDGET_CVAR_SLIDER_INT || w.type == WIDGET_CVAR_SLIDER_FLOAT)) {
        if (isFloat) {
            snprintf(sLabel, sizeof(sLabel), name.c_str(), fvalue);
        } else {
            snprintf(sLabel, sizeof(sLabel), name.c_str(), ivalue);
        }
    } else {
        snprintf(sLabel, sizeof(sLabel), "%s", name.c_str());
    }
}

WidgetInfo* Visible(int page, int row) {
    if (page != sVisiblePage) {
        Refresh(page);
    }
    if (row < 0 || row >= static_cast<int>(sVisible.size())) {
        return nullptr;
    }
    return sPages[page].widgets[sVisible[row]];
}

void Commit(WidgetInfo& w) {
    ShipInit::Init(w.cVar);
    if (w.callback != nullptr) {
        w.callback(w);
    }
    // Callbacks can clear dependent CVars; persist the complete change.
    CVarSave();
}

} // namespace

extern "C" int Soh3dsSettings_PageCount(void) {
    Build();
    return static_cast<int>(sPages.size());
}

extern "C" const char* Soh3dsSettings_PageName(int page) {
    Build();
    if (page < 0 || page >= static_cast<int>(sPages.size())) {
        return "";
    }
    return sPages[page].name.c_str();
}

extern "C" int Soh3dsSettings_RowCount(int page) {
    Build();
    Refresh(page);
    return static_cast<int>(sVisible.size());
}

extern "C" int Soh3dsSettings_Row(int page, int row, Soh3dsSettingsRow* out) {
    Build();
    WidgetInfo* wp = Visible(page, row);
    if (wp == nullptr || out == nullptr) {
        return 0;
    }
    WidgetInfo& w = *wp;
    sValue[0] = '\0';
    out->on = 0;
    out->disabled = w.options->disabled ? 1 : 0;
    out->tooltip = w.options->tooltip.c_str();
    switch (w.type) {
        case WIDGET_SEPARATOR_TEXT:
            out->kind = SOH3DS_ROW_HEADING;
            FormatLabel(w, 0, 0.0f, false);
            break;
        case WIDGET_TEXT:
            out->kind = SOH3DS_ROW_TEXT;
            FormatLabel(w, 0, 0.0f, false);
            break;
        case WIDGET_CVAR_CHECKBOX: {
            auto opt = std::static_pointer_cast<UIWidgets::CheckboxOptions>(w.options);
            out->kind = SOH3DS_ROW_TOGGLE;
            out->on = CVarGetInteger(w.cVar, opt->defaultValue) != 0;
            FormatLabel(w, 0, 0.0f, false);
            break;
        }
        case WIDGET_CVAR_COMBOBOX: {
            auto opt = std::static_pointer_cast<UIWidgets::ComboboxOptions>(w.options);
            out->kind = SOH3DS_ROW_CHOICE;
            const int32_t key = CVarGetInteger(w.cVar, static_cast<int32_t>(opt->defaultIndex));
            auto it = opt->comboMap.find(key);
            snprintf(sValue, sizeof(sValue), "%s", it != opt->comboMap.end() ? it->second : "?");
            FormatLabel(w, 0, 0.0f, false);
            break;
        }
        case WIDGET_CVAR_SLIDER_INT: {
            auto opt = std::static_pointer_cast<UIWidgets::IntSliderOptions>(w.options);
            out->kind = SOH3DS_ROW_SLIDER;
            const int32_t value = CVarGetInteger(w.cVar, opt->defaultValue);
            FormatLabel(w, value, 0.0f, false);
            if (opt->format != nullptr && strchr(opt->format, '%') != nullptr) {
                snprintf(sValue, sizeof(sValue), opt->format, value);
            } else if (w.name.find('%') == std::string::npos) {
                snprintf(sValue, sizeof(sValue), "%d", value);
            }
            break;
        }
        case WIDGET_CVAR_SLIDER_FLOAT: {
            auto opt = std::static_pointer_cast<UIWidgets::FloatSliderOptions>(w.options);
            out->kind = SOH3DS_ROW_SLIDER;
            const float value = CVarGetFloat(w.cVar, opt->defaultValue);
            if (opt->isPercentage) {
                FormatLabel(w, 0, value * 100.0f, true);
                snprintf(sValue, sizeof(sValue), "%.0f%%", value * 100.0f);
            } else {
                FormatLabel(w, 0, value, true);
                const bool bare = opt->format == nullptr || strcmp(opt->format, "%f") == 0;
                snprintf(sValue, sizeof(sValue), bare ? "%.2f" : opt->format, value);
            }
            break;
        }
        default:
            return 0;
    }
    out->label = sLabel;
    out->value = sValue;
    return 1;
}

extern "C" void Soh3dsSettings_Adjust(int page, int row, int dir) {
    Build();
    Refresh(page);
    WidgetInfo* wp = Visible(page, row);
    if (wp == nullptr || wp->options->disabled || dir == 0) {
        return;
    }
    WidgetInfo& w = *wp;
    switch (w.type) {
        case WIDGET_CVAR_CHECKBOX: {
            auto opt = std::static_pointer_cast<UIWidgets::CheckboxOptions>(w.options);
            CVarSetInteger(w.cVar, CVarGetInteger(w.cVar, opt->defaultValue) ? 0 : 1);
            break;
        }
        case WIDGET_CVAR_COMBOBOX: {
            auto opt = std::static_pointer_cast<UIWidgets::ComboboxOptions>(w.options);
            if (opt->comboMap.empty()) {
                return;
            }
            const int32_t key = CVarGetInteger(w.cVar, static_cast<int32_t>(opt->defaultIndex));
            auto it = opt->comboMap.find(key);
            if (it == opt->comboMap.end()) {
                it = opt->comboMap.begin();
            } else if (dir > 0) {
                ++it;
                if (it == opt->comboMap.end()) {
                    it = opt->comboMap.begin();
                }
            } else {
                if (it == opt->comboMap.begin()) {
                    it = opt->comboMap.end();
                }
                --it;
            }
            CVarSetInteger(w.cVar, it->first);
            break;
        }
        case WIDGET_CVAR_SLIDER_INT: {
            auto opt = std::static_pointer_cast<UIWidgets::IntSliderOptions>(w.options);
            int32_t value = CVarGetInteger(w.cVar, opt->defaultValue) + dir * (opt->step > 0 ? opt->step : 1);
            value = value < opt->min ? opt->min : value > opt->max ? opt->max : value;
            CVarSetInteger(w.cVar, value);
            break;
        }
        case WIDGET_CVAR_SLIDER_FLOAT: {
            auto opt = std::static_pointer_cast<UIWidgets::FloatSliderOptions>(w.options);
            float value = CVarGetFloat(w.cVar, opt->defaultValue) + dir * (opt->step > 0.0f ? opt->step : 0.01f);
            value = value < opt->min ? opt->min : value > opt->max ? opt->max : value;
            CVarSetFloat(w.cVar, value);
            break;
        }
        default:
            return;
    }
    Commit(w);
}
#endif
