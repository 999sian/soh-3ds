//
//  SohGui.cpp
//  soh
//
//  Created by David Chavez on 24.08.22.
//

#include <imgui.h>

#include "SohGui.hpp"

#ifdef __APPLE__
#include <fast/backends/gfx_metal.h>
#endif

#ifdef __SWITCH__
#include <port/switch/SwitchImpl.h>
#endif

#include "soh/Enhancements/debugger/MessageViewer.h"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/TimeDisplay/TimeDisplay.h"
#include "soh/Enhancements/mod_menu.h"
#include "soh/Network/Anchor/Anchor.h"

// SoH-3DS: per-window memory tracing. Every previous "hang" in startup was the
// heap running out, with the allocation-failure path never returning, so report
// used/free at each window construction rather than guessing again.
#ifdef __3DS__
#include <malloc.h>
#include <cstdio>
// SoH-3DS: route through the boot-status console/stderr like the InitOTR
// breadcrumbs; attributes SetupGuiElements' boot cost per window.
extern "C" void Soh3dsBootStatus(const char* msg) __attribute__((weak));
#define SOH3DS_GUI_TRACE(msg) (Soh3dsBootStatus != nullptr ? Soh3dsBootStatus("gui: " msg) : (void)0)
#else
#define SOH3DS_GUI_TRACE(msg) ((void)0)
#endif

namespace SohGui {

// MARK: - Properties
static const char* bunnyHoodOptions[3] = { "Disabled", "Faster Run & Longer Jump", "Faster Run" };

static const inline std::vector<std::pair<const char*, const char*>> audioBackends = {
#ifdef _WIN32
    { "wasapi", "Windows Audio Session API" },
#endif
#if defined(__linux)
    { "pulse", "PulseAudio" },
#endif
#ifdef __APPLE__
    { "coreaudio", "Core Audio" },
#endif
    { "sdl", "SDL Audio" }
};

// MARK: - Helpers

std::string GetWindowButtonText(const char* text, bool menuOpen) {
    char buttonText[100] = "";
    if (menuOpen) {
        strcat(buttonText, ICON_FA_CHEVRON_RIGHT " ");
    }
    strcat(buttonText, text);
    if (!menuOpen) {
        strcat(buttonText, "  ");
    }
    return buttonText;
}

// MARK: - Delegates

std::shared_ptr<Ship::GuiWindow> mConsoleWindow;
std::shared_ptr<SohStatsWindow> mStatsWindow;
std::shared_ptr<Ship::GuiWindow> mGfxDebuggerWindow;

std::shared_ptr<SohMenu> mSohMenu;
std::shared_ptr<ModMenuWindow> mModMenuWindow;
std::shared_ptr<AudioEditor> mAudioEditorWindow;
std::shared_ptr<InputViewer> mInputViewer;
std::shared_ptr<InputViewerSettingsWindow> mInputViewerSettings;
std::shared_ptr<CosmeticsEditorWindow> mCosmeticsEditorWindow;
std::shared_ptr<ActorViewerWindow> mActorViewerWindow;
std::shared_ptr<ColViewerWindow> mColViewerWindow;
std::shared_ptr<SaveEditorWindow> mSaveEditorWindow;
std::shared_ptr<HookDebuggerWindow> mHookDebuggerWindow;
std::shared_ptr<DLViewerWindow> mDLViewerWindow;
std::shared_ptr<ValueViewerWindow> mValueViewerWindow;
std::shared_ptr<MessageViewer> mMessageViewerWindow;
std::shared_ptr<GameplayStatsWindow> mGameplayStatsWindow;
std::shared_ptr<CheckTracker::CheckTrackerSettingsWindow> mCheckTrackerSettingsWindow;
std::shared_ptr<CheckTracker::CheckTrackerWindow> mCheckTrackerWindow;
std::shared_ptr<EntranceTracker::EntranceTrackerSettingsWindow> mEntranceTrackerSettingsWindow;
std::shared_ptr<EntranceTracker::EntranceTrackerWindow> mEntranceTrackerWindow;
std::shared_ptr<HintTracker::HintTrackerSettingsWindow> mHintTrackerSettingsWindow;
std::shared_ptr<HintTracker::HintTrackerWindow> mHintTrackerWindow;
std::shared_ptr<ItemTrackerSettingsWindow> mItemTrackerSettingsWindow;
std::shared_ptr<ItemTrackerWindow> mItemTrackerWindow;
std::shared_ptr<TimeSplitWindow> mTimeSplitWindow;
std::shared_ptr<PlandomizerWindow> mPlandomizerWindow;
std::shared_ptr<SohModalWindow> mModalWindow;
std::shared_ptr<Notification::Window> mNotificationWindow;
std::shared_ptr<TimeDisplayWindow> mTimeDisplayWindow;
std::shared_ptr<AnchorRoomWindow> mAnchorRoomWindow;

UIWidgets::Colors GetMenuThemeColor() {
    return mSohMenu->GetMenuThemeColor();
}

std::shared_ptr<SohMenu> GetSohMenu() {
    return mSohMenu;
}

void SetupMenu() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    mSohMenu = std::make_shared<SohMenu>(CVAR_WINDOW("Menu"), "Port Menu");
    gui->SetMenu(mSohMenu);

    mModalWindow = std::make_shared<SohModalWindow>(CVAR_WINDOW("ModalWindow"), "Modal Window");
    gui->AddGuiWindow(mModalWindow);
    SOH3DS_GUI_TRACE("mModalWindow");
    mModalWindow->Show();
}

void SetupMenuElements() {
    mSohMenu->AddMenuElements();
}

void SetupGuiElements() {
    SOH3DS_GUI_TRACE("entry");
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();

    mConsoleWindow = std::make_shared<SohConsoleWindow>(CVAR_WINDOW("SohConsole"), "Console##SoH", ImVec2(820, 630));
    gui->AddGuiWindow(mConsoleWindow);
    SOH3DS_GUI_TRACE("mConsoleWindow");

    mGfxDebuggerWindow =
        std::make_shared<SohGfxDebuggerWindow>(CVAR_WINDOW("SohGfxDebugger"), "GfxDebugger##SoH", ImVec2(820, 630));
    gui->AddGuiWindow(mGfxDebuggerWindow);
    SOH3DS_GUI_TRACE("mGfxDebuggerWindow");

    mStatsWindow = std::make_shared<SohStatsWindow>(CVAR_WINDOW("SohStats"), "Stats##Soh", ImVec2(400, 100));
    gui->AddGuiWindow(mStatsWindow);
    SOH3DS_GUI_TRACE("mStatsWindow");

    /*mInputEditorWindow = gui->GetGuiWindow("Controller Configuration");
    if (mInputEditorWindow == nullptr) {
        SPDLOG_ERROR("Could not find input editor window");
    }*/

    mModMenuWindow = std::make_shared<ModMenuWindow>(CVAR_WINDOW("ModMenu"), "Mod Menu", ImVec2(820, 630));
    gui->AddGuiWindow(mModMenuWindow);
    SOH3DS_GUI_TRACE("mModMenuWindow");
    mAudioEditorWindow = std::make_shared<AudioEditor>(CVAR_WINDOW("AudioEditor"), "Audio Editor", ImVec2(820, 630));
    gui->AddGuiWindow(mAudioEditorWindow);
    SOH3DS_GUI_TRACE("mAudioEditorWindow");
    mInputViewer = std::make_shared<InputViewer>(CVAR_WINDOW("InputViewer"), "Input Viewer");
    gui->AddGuiWindow(mInputViewer);
    SOH3DS_GUI_TRACE("mInputViewer");
    mInputViewerSettings = std::make_shared<InputViewerSettingsWindow>(CVAR_WINDOW("InputViewerSettings"),
                                                                       "Input Viewer Settings", ImVec2(500, 525));
    gui->AddGuiWindow(mInputViewerSettings);
    SOH3DS_GUI_TRACE("mInputViewerSettings");
    SOH3DS_GUI_TRACE("CosmeticsEditor: before ctor");
    mCosmeticsEditorWindow =
        std::make_shared<CosmeticsEditorWindow>(CVAR_WINDOW("CosmeticsEditor"), "Cosmetics Editor", ImVec2(550, 520));
    SOH3DS_GUI_TRACE("CosmeticsEditor: after ctor");
    gui->AddGuiWindow(mCosmeticsEditorWindow);
    SOH3DS_GUI_TRACE("mCosmeticsEditorWindow");
    mActorViewerWindow =
        std::make_shared<ActorViewerWindow>(CVAR_WINDOW("ActorViewer"), "Actor Viewer", ImVec2(520, 600));
    gui->AddGuiWindow(mActorViewerWindow);
    SOH3DS_GUI_TRACE("mActorViewerWindow");
    mColViewerWindow =
        std::make_shared<ColViewerWindow>(CVAR_WINDOW("CollisionViewer"), "Collision Viewer", ImVec2(520, 600));
    gui->AddGuiWindow(mColViewerWindow);
    SOH3DS_GUI_TRACE("mColViewerWindow");
    mSaveEditorWindow = std::make_shared<SaveEditorWindow>(CVAR_WINDOW("SaveEditor"), "Save Editor", ImVec2(520, 600));
    gui->AddGuiWindow(mSaveEditorWindow);
    SOH3DS_GUI_TRACE("mSaveEditorWindow");
    mHookDebuggerWindow =
        std::make_shared<HookDebuggerWindow>(CVAR_WINDOW("HookDebugger"), "Hook Debugger", ImVec2(1250, 850));
    gui->AddGuiWindow(mHookDebuggerWindow);
    SOH3DS_GUI_TRACE("mHookDebuggerWindow");
    mDLViewerWindow =
        std::make_shared<DLViewerWindow>(CVAR_WINDOW("DisplayListViewer"), "Display List Viewer", ImVec2(520, 600));
    gui->AddGuiWindow(mDLViewerWindow);
    SOH3DS_GUI_TRACE("mDLViewerWindow");
    mValueViewerWindow =
        std::make_shared<ValueViewerWindow>(CVAR_WINDOW("ValueViewer"), "Value Viewer", ImVec2(520, 600));
    gui->AddGuiWindow(mValueViewerWindow);
    SOH3DS_GUI_TRACE("mValueViewerWindow");
    mMessageViewerWindow =
        std::make_shared<MessageViewer>(CVAR_WINDOW("MessageViewer"), "Message Viewer", ImVec2(520, 600));
    gui->AddGuiWindow(mMessageViewerWindow);
    SOH3DS_GUI_TRACE("mMessageViewerWindow");
    mGameplayStatsWindow =
        std::make_shared<GameplayStatsWindow>(CVAR_WINDOW("GameplayStats"), "Gameplay Stats", ImVec2(480, 550));
    gui->AddGuiWindow(mGameplayStatsWindow);
    SOH3DS_GUI_TRACE("mGameplayStatsWindow");
    mCheckTrackerWindow = std::make_shared<CheckTracker::CheckTrackerWindow>(CVAR_WINDOW("CheckTracker"),
                                                                             "Check Tracker", ImVec2(400, 540));
    gui->AddGuiWindow(mCheckTrackerWindow);
    SOH3DS_GUI_TRACE("mCheckTrackerWindow");
    mCheckTrackerSettingsWindow = std::make_shared<CheckTracker::CheckTrackerSettingsWindow>(
        CVAR_WINDOW("CheckTrackerSettings"), "Check Tracker Settings", ImVec2(600, 375));
    gui->AddGuiWindow(mCheckTrackerSettingsWindow);
    SOH3DS_GUI_TRACE("mCheckTrackerSettingsWindow");
    mEntranceTrackerWindow = std::make_shared<EntranceTracker::EntranceTrackerWindow>(
        CVAR_WINDOW("EntranceTracker"), "Entrance Tracker", ImVec2(500, 750));
    gui->AddGuiWindow(mEntranceTrackerWindow);
    SOH3DS_GUI_TRACE("mEntranceTrackerWindow");
    mEntranceTrackerSettingsWindow = std::make_shared<EntranceTracker::EntranceTrackerSettingsWindow>(
        CVAR_WINDOW("EntranceTrackerSettings"), "Entrance Tracker Settings", ImVec2(600, 375));
    gui->AddGuiWindow(mEntranceTrackerSettingsWindow);
    SOH3DS_GUI_TRACE("mEntranceTrackerSettingsWindow");
    mHintTrackerWindow =
        std::make_shared<HintTracker::HintTrackerWindow>(CVAR_WINDOW("HintTracker"), "Hint Tracker", ImVec2(500, 600));
    gui->AddGuiWindow(mHintTrackerWindow);
    SOH3DS_GUI_TRACE("mHintTrackerWindow");
    mHintTrackerSettingsWindow = std::make_shared<HintTracker::HintTrackerSettingsWindow>(
        CVAR_WINDOW("HintTrackerSettings"), "Hint Tracker Settings", ImVec2(600, 375));
    gui->AddGuiWindow(mHintTrackerSettingsWindow);
    SOH3DS_GUI_TRACE("mHintTrackerSettingsWindow");
    mItemTrackerWindow =
        std::make_shared<ItemTrackerWindow>(CVAR_WINDOW("ItemTracker"), "Item Tracker", ImVec2(350, 600));
    gui->AddGuiWindow(mItemTrackerWindow);
    SOH3DS_GUI_TRACE("mItemTrackerWindow");
    mItemTrackerSettingsWindow = std::make_shared<ItemTrackerSettingsWindow>(CVAR_WINDOW("ItemTrackerSettings"),
                                                                             "Item Tracker Settings", ImVec2(733, 472));
    gui->AddGuiWindow(mItemTrackerSettingsWindow);
    SOH3DS_GUI_TRACE("mItemTrackerSettingsWindow");
    mTimeSplitWindow = std::make_shared<TimeSplitWindow>(CVAR_WINDOW("TimeSplits"), "Time Splits", ImVec2(450, 660));
    gui->AddGuiWindow(mTimeSplitWindow);
    SOH3DS_GUI_TRACE("mTimeSplitWindow");
    mPlandomizerWindow =
        std::make_shared<PlandomizerWindow>(CVAR_WINDOW("PlandomizerEditor"), "Plandomizer Editor", ImVec2(850, 760));
    gui->AddGuiWindow(mPlandomizerWindow);
    SOH3DS_GUI_TRACE("mPlandomizerWindow");
    mNotificationWindow = std::make_shared<Notification::Window>(CVAR_WINDOW("Notifications"), "Notifications Window");
    gui->AddGuiWindow(mNotificationWindow);
    SOH3DS_GUI_TRACE("mNotificationWindow");
    mNotificationWindow->Show();
    mTimeDisplayWindow = std::make_shared<TimeDisplayWindow>(CVAR_WINDOW("TimeDisplayEnabled"), "Additional Timers");
    gui->AddGuiWindow(mTimeDisplayWindow);
    SOH3DS_GUI_TRACE("mTimeDisplayWindow");
    mAnchorRoomWindow = std::make_shared<AnchorRoomWindow>(CVAR_WINDOW("AnchorRoom"), "Anchor Room");
    gui->AddGuiWindow(mAnchorRoomWindow);
    SOH3DS_GUI_TRACE("mAnchorRoomWindow");
}

void Destroy() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    gui->RemoveAllGuiWindows();

    mNotificationWindow = nullptr;
    mModalWindow = nullptr;
    mItemTrackerWindow = nullptr;
    mItemTrackerSettingsWindow = nullptr;
    mEntranceTrackerWindow = nullptr;
    mEntranceTrackerSettingsWindow = nullptr;
    mCheckTrackerWindow = nullptr;
    mCheckTrackerSettingsWindow = nullptr;
    mHintTrackerWindow = nullptr;
    mHintTrackerSettingsWindow = nullptr;
    mGameplayStatsWindow = nullptr;
    mDLViewerWindow = nullptr;
    mValueViewerWindow = nullptr;
    mMessageViewerWindow = nullptr;
    mSaveEditorWindow = nullptr;
    mHookDebuggerWindow = nullptr;
    mColViewerWindow = nullptr;
    mActorViewerWindow = nullptr;
    mCosmeticsEditorWindow = nullptr;
    mModMenuWindow = nullptr;
    mAudioEditorWindow = nullptr;
    mStatsWindow = nullptr;
    mConsoleWindow = nullptr;
    mGfxDebuggerWindow = nullptr;
    mInputViewer = nullptr;
    mInputViewerSettings = nullptr;
    mTimeSplitWindow = nullptr;
    mPlandomizerWindow = nullptr;
    mTimeDisplayWindow = nullptr;
    mAnchorRoomWindow = nullptr;
}

void RegisterPopup(std::string title, std::string message, std::string button1, std::string button2,
                   std::function<void()> button1callback, std::function<void()> button2callback) {
    mModalWindow->RegisterPopup(title, message, button1, button2, button1callback, button2callback);
}

size_t PopupsQueued() {
    return mModalWindow->PopupsQueued();
}

bool DismissPopup(std::string title) {
    if (mModalWindow->IsPopupOpen(title)) {
        mModalWindow->DismissPopup();
        return true;
    }
    return false;
}

void ShowRandomizerSettingsMenu() {
    CVarSetString(CVAR_SETTING("Menu.ActiveHeader"), "Randomizer");
    CVarSetString(CVAR_SETTING("Menu.RandomizerSidebarSection"), "General");
    mSohMenu->Show();
}

void ShowEscMenu() {
    mSohMenu->Show();
}
} // namespace SohGui
