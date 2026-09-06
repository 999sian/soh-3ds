#pragma once

#include "Menu.h"

extern "C" {
#include "z64.h"
}

#ifdef __cplusplus
extern "C" {
#endif
void enableBetaQuest();
void disableBetaQuest();
#ifdef __cplusplus
}
#endif

namespace SohGui {
static std::map<int32_t, const char*> languages = {
    { LANGUAGE_ENG, "English" },
    { LANGUAGE_GER, "German" },
    { LANGUAGE_FRA, "French" },
    { LANGUAGE_JPN, "Japanese" },
};
void UpdateMenuTricks();
void UpdateMenuLocations();
void MarkRandomizerMenusDirty();

class SohMenu : public Ship::Menu {
  public:
    SohMenu(const std::string& consoleVariable, const std::string& name);

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
    void Draw() override;

    void AddSidebarEntry(std::string sectionName, std::string sidbarName, uint32_t columnCount);
    WidgetInfo& AddWidget(WidgetPath& pathInfo, std::string widgetName, WidgetType widgetType);
    void AddMenuElements();
    void AddMenuSettings();
    void AddMenuEnhancements();
    void AddMenuDevTools();
    void AddMenuRandomizer();
    void AddMenuNetwork();
    static void UpdateLanguageMap(std::map<int32_t, const char*>& languageMap);
    // SoH-3DS: the bottom-screen settings tab walks the widget tree natively
    // (no ImGui); Ship::Menu keeps these protected.
    const std::vector<std::string>& MenuOrder() const { return menuOrder; }
    std::unordered_map<std::string, MainMenuEntry>& MenuEntries() { return menuEntries; }
    std::unordered_map<uint32_t, disabledInfo>& DisabledMap() { return disabledMap; }

  private:
    char mGitCommitHashTruncated[8];
    bool mIsTaggedVersion;
    bool mMenuElementsInitialized = false;
};
} // namespace SohGui
