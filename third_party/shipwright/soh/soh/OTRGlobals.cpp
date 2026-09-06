#include "OTRGlobals.h"
#ifdef __3DS__
#include <malloc.h>
#include "setup/setup_3ds.h"
#endif
#include "OTRAudio.h"

// Single definition of the audio-thread sync block (see OTRAudio.h).
OTRAudioSync audio;
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <vector>
#include <chrono>
#include <optional>
#include <spdlog/common.h>
#include <imgui.h>

#include "ResourceManagerHelpers.h"
#include <fast/Fast3dWindow.h>
#include <libultraship/bridge/audiobridge.h>
#include <libultraship/bridge/gfxdebuggerbridge.h>
#include <libultraship/bridge/windowbridge.h>
#include <ship/Context.h>
#include <ship/resource/File.h>
#include <ship/window/Window.h>
#include <soh/GameVersions.h>

#include "Enhancements/gameconsole.h"
#ifdef _WIN32
#include <Windows.h>
#else
#include <time.h>
#endif
#include <ship/resource/archive/O2rArchive.h>
#include <ship/utils/binarytools/MemoryStream.h>
#include "Enhancements/speechsynthesizer/SpeechSynthesizer.h"
#include "Enhancements/controls/SohInputEditorWindow.h"
#include "Enhancements/audio/AudioCollection.h"
#include "Enhancements/debugconsole.h"
#include "Enhancements/randomizer/randomizer.h"
#include "Enhancements/randomizer/randomizer_entrance_tracker.h"
#include "Enhancements/randomizer/randomizer_check_tracker.h"
#include "Enhancements/randomizer/entrance.h"
#include "Enhancements/randomizer/3drando/fill.hpp"
#include "Enhancements/Presets/Presets.h"
#include "Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/settings.h"
#include "soh/Enhancements/savestates.h"
#include "frame_interpolation.h"
#include "SohGui/SohMenu.h"
#include "SohGui/SohGui.hpp"
#include "variables.h"
#include "z64.h"
#include "macros.h"
#include <ship/window/gui/Fonts.h>
#include <ship/window/FileDropMgr.h>
#include <ship/window/gui/resource/Font.h>
#include <ship/utils/StringHelper.h>
#include "Enhancements/custom-message/CustomMessageManager.h"
#include "util.h"

#include <fast/Fast3dGui.h>
#include <fast/debug/GfxDebugger.h>

#if not defined(__SWITCH__) && not defined(__WIIU__)
#include "Extractor/Extract.h"
#endif

#include <fast/interpreter.h>

#ifdef __APPLE__
#include <SDL_scancode.h>
#else
#include <SDL2/SDL_scancode.h>
#endif

#ifdef __SWITCH__
#include <port/switch/SwitchImpl.h>
#elif defined(__WIIU__)
#include <port/wiiu/WiiUImpl.h>
#include <coreinit/debug.h> // OSFatal
#endif

#include <functions.h>
#include "Enhancements/item-tables/ItemTableManager.h"
#include "Enhancements/Restorations/GetItemManipulation.h"
#include "Enhancements/Lang/Lang.h"
#include "soh/SohGui/ImGuiUtils.h"
#include "ActorDB.h"
#include "SaveManager.h"
#include "soh/Network/CrowdControl/CrowdControl.h"
#include "soh/Network/Sail/Sail.h"
#include "soh/Network/Anchor/Anchor.h"
#include "Enhancements/game-interactor/GameInteractor.h"
#include "Enhancements/randomizer/draw.h"
#include <libultraship/controller/controldeck/ControlDeck.h>
#include <fast/resource/ResourceType.h>

// Resource Types/Factories
#include "soh/resource/type/SohResourceType.h"
#include "soh/resource/type/Skeleton.h"
#include <ship/resource/factory/BlobFactory.h>
#include <fast/resource/factory/DisplayListFactory.h>
#include <fast/resource/factory/MatrixFactory.h>
#include <fast/resource/factory/TextureFactory.h>
#include <fast/resource/factory/VertexFactory.h>
#include "soh/resource/importer/ArrayFactory.h"
#include "soh/resource/importer/AnimationFactory.h"
#include "soh/resource/importer/AudioSampleFactory.h"
#include "soh/resource/importer/AudioSequenceFactory.h"
#include "soh/resource/importer/AudioSoundFontFactory.h"
#include "soh/resource/importer/CollisionHeaderFactory.h"
#include "soh/resource/importer/CutsceneFactory.h"
#include "soh/resource/importer/PathFactory.h"
#include "soh/resource/importer/PlayerAnimationFactory.h"
#include "soh/resource/importer/SceneFactory.h"
#include "soh/resource/importer/SkeletonFactory.h"
#include "soh/resource/importer/SkeletonLimbFactory.h"
#include "soh/resource/importer/TextFactory.h"
#include "soh/resource/importer/BackgroundFactory.h"

#include "soh/config/ConfigUpdaters.h"
#include "soh/ShipInit.hpp"

// SoH-3DS: InitOTR breadcrumbs. A fault here happens before any renderer or
// spdlog sink exists, so stderr (routed to svcOutputDebugString) is the only
// channel. Cheap enough to leave in.
#ifdef __3DS__
// SoH-3DS: breadcrumbs go to stderr (svcOutputDebugString, visible in the
// emulator log) and, once the renderer is up, to the bottom-screen boot
// console - so a minutes-long o2r index on hardware reads as progress, not a
// hang. Implemented by the port main.
// Weak: probe/test binaries that link these objects without the port main
// (which defines the symbol) must stay linkable; the trace then no-ops.
extern "C" void Soh3dsBootStatus(const char* msg) __attribute__((weak));
#define SOH3DS_INIT_TRACE(msg) (Soh3dsBootStatus != nullptr ? Soh3dsBootStatus(msg) : (void)0)
// SoH-3DS: frame pacer hooks (platform/3ds/source/gfx_citro3d.cpp), used by
// RunCommands to drop interpolated frames while the renderer is behind.
extern "C" bool Soh3dsFrameBehind(void);
extern "C" void Soh3dsFrameDropped(void);
#else
#define SOH3DS_INIT_TRACE(msg) ((void)0)
#endif

// SoH-3DS: quit paths call DeinitOTR before exit(0); the header only declares
// it for C translation units.
extern "C" void DeinitOTR(void);

#ifdef _MSC_VER
#define strdup _strdup
#endif

#ifdef __WIIU__
const uint32_t defaultImGuiScale = 3;
#else
const uint32_t defaultImGuiScale = 1;
#endif

const float imguiScaleOptionToValue[4] = { 0.75f, 1.0f, 1.5f, 2.0f };

bool SoH_HandleConfigDrop(char* filePath);

OTRGlobals* OTRGlobals::Instance;
SaveManager* SaveManager::Instance;
CustomMessageManager* CustomMessageManager::Instance;
ItemTableManager* ItemTableManager::Instance;
GameInteractor* GameInteractor::Instance;
AudioCollection* AudioCollection::Instance;
SpeechSynthesizer* SpeechSynthesizer::Instance;
CrowdControl* CrowdControl::Instance;
Sail* Sail::Instance;
Anchor* Anchor::Instance;

extern "C" char** cameraStrings;

extern "C" void PadMgr_ThreadEntry(PadMgr* padMgr);
std::vector<std::shared_ptr<std::string>> cameraStdStrings;

Color_RGB8 kokiriColor = { 0x1E, 0x69, 0x1B };
Color_RGB8 goronColor = { 0x64, 0x14, 0x00 };
Color_RGB8 zoraColor = { 0x00, 0xEC, 0x64 };

int32_t previousImGuiScaleIndex;
float previousImGuiScale;

bool prevAltAssets = false;

// Same as NaviColor type from OoT src (z_actor.c), but modified to be sans alpha channel for Controller LED.
typedef struct {
    Color_RGB8 inner;
    Color_RGB8 outer;
} NaviColor_RGB8;

static NaviColor_RGB8 defaultIdleColor = { { 255, 255, 255 }, { 0, 0, 255 } };
static NaviColor_RGB8 defaultNPCColor = { { 150, 150, 255 }, { 150, 150, 255 } };
static NaviColor_RGB8 defaultEnemyColor = { { 255, 255, 0 }, { 200, 155, 0 } };
static NaviColor_RGB8 defaultPropsColor = { { 0, 255, 0 }, { 0, 255, 0 } };

// Labeled according to ActorCategory (included through ActorDB.h)
const NaviColor_RGB8 LEDColorDefaultNaviColorList[] = {
    defaultPropsColor, // ACTORCAT_SWITCH       Switch
    defaultPropsColor, // ACTORCAT_BG           Background (Prop type 1)
    defaultIdleColor,  // ACTORCAT_PLAYER       Player
    defaultPropsColor, // ACTORCAT_EXPLOSIVE    Bomb
    defaultNPCColor,   // ACTORCAT_NPC          NPC
    defaultEnemyColor, // ACTORCAT_ENEMY        Enemy
    defaultPropsColor, // ACTORCAT_PROP         Prop type 2
    defaultPropsColor, // ACTORCAT_ITEMACTION   Item/Action
    defaultPropsColor, // ACTORCAT_MISC         Misc.
    defaultEnemyColor, // ACTORCAT_BOSS         Boss
    defaultPropsColor, // ACTORCAT_DOOR         Door
    defaultPropsColor, // ACTORCAT_CHEST        Chest
    defaultPropsColor, // ACTORCAT_MAX
};

// OTRTODO: A lot of these left in Japanese are used by the mempak manager. LUS does not currently support mempaks.
// Ignore unused ones.
const char* constCameraStrings[] = {
    "INSUFFICIENT",
    "KEYFRAMES",
    "YOU CAN ADD MORE",
    "FINISHED",
    "PLAYING",
    "DEMO CAMERA TOOL",
    "CANNOT PLAY",
    "KEYFRAME   ",
    "PNT   /      ",
    ">            >",
    "<            <",
    "<          >",
    GFXP_KATAKANA "*ﾌﾟﾚｲﾔ-*",
    "E MODE FIX",
    "E MODE ABS",
    GFXP_HIRAGANA "ｶﾞﾒﾝ" GFXP_KATAKANA "   ﾃﾞﾓ", // OTRTODO: Unused, get a translation! Number 15
    GFXP_HIRAGANA "ｶﾞﾒﾝ   ﾌﾂｳ",                  // OTRTODO: Unused, get a translation! Number 16
    "P TIME  MAX",
    GFXP_KATAKANA "ﾘﾝｸ" GFXP_HIRAGANA "    ｷｵｸ", // OTRTODO: Unused, get a translation! Number 18
    GFXP_KATAKANA "ﾘﾝｸ" GFXP_HIRAGANA "     ﾑｼ", // OTRTODO: Unused, get a translation! Number 19
    "*VIEWPT*",
    "*CAMPOS*",
    "DEBUG CAMERA",
    "CENTER/LOCK",
    "CENTER/FREE",
    "DEMO CONTROL",
    GFXP_KATAKANA "ﾒﾓﾘ" GFXP_HIRAGANA "ｶﾞﾀﾘﾏｾﾝ",
    "p",
    "e",
    "s",
    "l",
    "c",
    GFXP_KATAKANA "ﾒﾓﾘﾊﾟｯｸ",
    GFXP_KATAKANA "ｾｰﾌﾞ",
    GFXP_KATAKANA "ﾛｰﾄﾞ",
    GFXP_KATAKANA "ｸﾘｱ-",
    GFXP_HIRAGANA "ｦﾇｶﾅｲﾃﾞﾈ",
    "FREE      BYTE",
    "NEED      BYTE",
    GFXP_KATAKANA "*ﾒﾓﾘ-ﾊﾟｯｸ*",
    GFXP_HIRAGANA "ｦﾐﾂｹﾗﾚﾏｾﾝ",
    GFXP_KATAKANA "ﾌｧｲﾙ " GFXP_HIRAGANA "ｦ",
    GFXP_HIRAGANA "ｼﾃﾓｲｲﾃﾞｽｶ?",
    GFXP_HIRAGANA "ｹﾞﾝｻﾞｲﾍﾝｼｭｳﾁｭｳﾉ",              // OTRTODO: Unused, get a translation! Number 43
    GFXP_KATAKANA "ﾌｧｲﾙ" GFXP_HIRAGANA "ﾊﾊｷｻﾚﾏｽ", // OTRTODO: Unused, get a translation! Number 44
    GFXP_HIRAGANA "ﾊｲ",
    GFXP_HIRAGANA "ｲｲｴ",
    GFXP_HIRAGANA "ｼﾃｲﾏｽ",
    GFXP_HIRAGANA "ｳﾜｶﾞｷ", // OTRTODO: Unused, get a translation! Number 48
    GFXP_HIRAGANA "ｼﾏｼﾀ",
    "USE       BYTE",
    GFXP_HIRAGANA "ﾆｼｯﾊﾟｲ",
    "E MODE REL",
    "FRAME       ",
    "KEY   /       ",
    "(CENTER)",
    "(ORIG)",
    "(PLAYER)",
    "(ALIGN)",
    "(SET)",
    "(OBJECT)",
    GFXP_KATAKANA "ﾎﾟｲﾝﾄNo.     ", // OTRTODO: Unused, need translation. Number 62
    "FOV              ",
    "N FRAME          ",
    "Z ROT            ",
    GFXP_KATAKANA "ﾓ-ﾄﾞ        ", // OTRTODO: Unused, need translation. Number 65
    "  R FOCUS   ",
    "PMAX              ",
    "DEPTH             ",
    "XROT              ",
    "YROT              ",
    GFXP_KATAKANA "ﾌﾚ-ﾑ         ",
    GFXP_KATAKANA "ﾄ-ﾀﾙ         ",
    GFXP_KATAKANA "ｷ-     /   ",
};

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} OTRVersion;

std::shared_ptr<Fast::Fast3dWindow> sohFast3dWindow;
static OTRVersion DetectOTRVersion(std::string path, bool isMq);
static bool VerifyArchiveVersion(OTRVersion version);
std::string portArchivePath = "";
static bool sohArchiveVersionMatch = false;

OTRGlobals::OTRGlobals() {
    context = Ship::Context::CreateUninitializedInstance("Ship of Harkinian", appShortName, "shipofharkinian.json");

    portArchivePath = Ship::Context::LocateFileAcrossAppDirs("soh.o2r");
    OTRVersion portArchiveVersion = DetectOTRVersion("soh.o2r", false);
    sohArchiveVersionMatch = portArchiveVersion.major == gBuildVersionMajor &&
                             portArchiveVersion.minor == gBuildVersionMinor &&
                             portArchiveVersion.patch == gBuildVersionPatch;

    SOH3DS_INIT_TRACE("Initialize: InitConfiguration");
    context->InitConfiguration();
    SOH3DS_INIT_TRACE("Initialize: InitConsoleVariables");
    context->InitConsoleVariables();

    auto controlDeck = std::make_shared<LUS::ControlDeck>(std::vector<CONTROLLERBUTTONS_T>({
        BTN_CUSTOM_MODIFIER1,
        BTN_CUSTOM_MODIFIER2,
        BTN_CUSTOM_OCARINA_NOTE_D4,
        BTN_CUSTOM_OCARINA_NOTE_F4,
        BTN_CUSTOM_OCARINA_NOTE_A4,
        BTN_CUSTOM_OCARINA_NOTE_B4,
        BTN_CUSTOM_OCARINA_NOTE_D5,
        BTN_CUSTOM_OCARINA_DISABLE_SONGS,
        BTN_CUSTOM_OCARINA_PITCH_UP,
        BTN_CUSTOM_OCARINA_PITCH_DOWN,
    }));
    context->InitControlDeck(controlDeck);
    context->InitResourceManager({ portArchivePath }, {}, 3, true);
    context->InitConsole();

    auto sohInputEditorWindow =
        std::make_shared<SohInputEditorWindow>(CVAR_WINDOW("ControllerConfiguration"), "Configure Controller");
    sohFast3dWindow =
        std::make_shared<Fast::Fast3dWindow>(std::vector<std::shared_ptr<Ship::GuiWindow>>({ sohInputEditorWindow }));
    SOH3DS_INIT_TRACE("ctor: InitWindow start");
    context->InitWindow(sohFast3dWindow);
    SOH3DS_INIT_TRACE("ctor: InitWindow done");

    SohGui::SetupMenu();

    if (sohArchiveVersionMatch) {

        auto overlay = context->GetRawInstance()->GetWindow()->GetGui()->GetGameOverlay();
        overlay->LoadFont("Press Start 2P", 12.0f, "fonts/PressStart2P-Regular.ttf");
        overlay->LoadFont("Fipps", 32.0f, "fonts/Fipps-Regular.otf");
        overlay->SetCurrentFont(CVarGetString(CVAR_GAME_OVERLAY_FONT, "Press Start 2P"));

        SOH3DS_INIT_TRACE("fonts: start");
        fontMonoSmall = CreateFontWithSize(14.0f, "fonts/Inconsolata-Regular.ttf");
        fontMono = CreateFontWithSize(16.0f, "fonts/Inconsolata-Regular.ttf");
        fontMonoLarger = CreateFontWithSize(20.0f, "fonts/Inconsolata-Regular.ttf");
        fontMonoLargest = CreateFontWithSize(24.0f, "fonts/Inconsolata-Regular.ttf");
        fontStandard = CreateFontWithSize(16.0f, "fonts/Montserrat-Regular.ttf");
        fontStandardLarger = CreateFontWithSize(20.0f, "fonts/Montserrat-Regular.ttf");
        fontStandardLargest = CreateFontWithSize(24.0f, "fonts/Montserrat-Regular.ttf");
#ifdef __3DS__
        // SoH-3DS: NotoSansJP-Regular.ttf is 5.45 MB and is only loaded to get
        // the CJK ranges, which the block above already drops - so it would cost
        // 5.45 MB of a 36 MB heap to render Latin text a Latin font already
        // covers. Alias the pointer instead; every fontJapanese user stays valid.
        fontJapanese = fontStandard;
#else
        fontJapanese = CreateFontWithSize(24.0f, "fonts/NotoSansJP-Regular.ttf", true);
#endif
        SOH3DS_INIT_TRACE("fonts: done");
        ImGui::GetIO().FontDefault = fontStandardLarger;
    }

    previousImGuiScaleIndex = -1;
    previousImGuiScale = defaultImGuiScale;
    SOH3DS_INIT_TRACE("ctor: ScaleImGui start");
    ScaleImGui();
    SOH3DS_INIT_TRACE("ctor: OTRGlobals ctor done");
}

typedef enum ExtractSteps {
    ES_PORT_ARCHIVE,
    ES_WINDOWS,
    ES_EXTRACT_ARGS,
    ES_EXTRACT,
    ES_VERIFY,
} ExtractSteps;

typedef enum PromptSteps {
    PS_FILE_CHECK,
    PS_LOCAL,
    PS_FIRST,
    PS_SECOND,
    PS_DUPE,
    PS_WAIT,
    PS_NONE,
} PromptSteps;

typedef enum WindowsSteps {
    WS_TEMP,
    WS_PERMS,
    WS_ONEDRIVE,
    WS_DONE,
} WindowsSteps;

bool IsSubpath(const std::filesystem::path& path, const std::filesystem::path& base) {
    auto rel = std::filesystem::relative(path, base);
    return !rel.empty() && rel.native()[0] != '.';
}

bool PathTestCleanup(FILE* tfile) {
    try {
        if (std::filesystem::exists("./text.txt"))
            std::filesystem::remove("./text.txt");
        if (std::filesystem::exists("./test/"))
            std::filesystem::remove("./test/");
    } catch ([[maybe_unused]] std::filesystem::filesystem_error const& ex) { return false; }
    return true;
}

void CheckAndCreateModFolder() {
    try {
        std::string modsPath = Ship::Context::LocateFileAcrossAppDirs("mods", appShortName);
        if (!std::filesystem::exists(modsPath)) {
            // Create mods folder relative to app dir
            modsPath = Ship::Context::GetPathRelativeToAppDirectory("mods", appShortName);
            std::string filePath = modsPath + "/custom_mod_files_go_here.txt";
            if (std::filesystem::create_directories(modsPath)) {
                std::ofstream(filePath).close();
            }
        }
    } catch ([[maybe_unused]] std::filesystem::filesystem_error const& ex) {
        // Couldn't make the folder, continue silently
        return;
    }
}

namespace SohGui {
extern std::shared_ptr<SohGui::SohMenu> mSohMenu;
}

static bool RemoveArchiveAcrossAppDirs(const std::string& fileName) {
    for (const std::string& path : { Ship::Context::GetPathRelativeToAppDirectory(fileName, appShortName),
                                     Ship::Context::GetPathRelativeToAppBundle(fileName), "./" + fileName }) {
        std::error_code err;
        if (std::filesystem::remove(path, err)) {
            SPDLOG_INFO("Removed outdated archive {}", path);
        } else if (err) {
            SPDLOG_ERROR("Failed to remove outdated archive {}: {}", path, err.message());
        }
    }
    return !std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs(fileName, appShortName));
}

void OTRGlobals::RunExtract(int argc, char* argv[]) {
#ifdef __3DS__
    // Native first-run setup validates/prepares the archives before InitOTR,
    // while the game and resource managers are still unloaded. The desktop
    // ImGui extractor is not used on this platform.
    (void)argc;
    (void)argv;
    fprintf(stderr, "soh-3ds: archives prepared by native first-run setup\n");
    return;
#endif
    bool extractDone = false;
    ExtractSteps extractStep = ES_PORT_ARCHIVE;
    WindowsSteps windowsStep = WS_TEMP;
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(OTRGlobals::Instance->context->GetWindow());
    auto gui = wnd->GetGui();

    OTRVersion vanillaVersion = DetectOTRVersion("oot.o2r", false);
    OTRVersion mqVersion = DetectOTRVersion("oot-mq.o2r", true);

    bool shouldRegen = VerifyArchiveVersion(vanillaVersion) || VerifyArchiveVersion(mqVersion);

    std::filesystem::path ownPath;
    std::vector<std::string> args;
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            args.push_back(argv[i]);
        }
    }
    Extractor extract;
    PromptSteps promptStep = PS_FILE_CHECK;
    bool generatedIsMQ = false;
    std::atomic<size_t> extractCount = 0, totalExtract = 0;

    std::string installPath = std::filesystem::absolute(Ship::Context::GetAppBundlePath()).string();
    std::string dataPath = std::filesystem::absolute(Ship::Context::GetAppDirectoryPath(appShortName)).string();
    std::string file;

#if defined(__SWITCH__)
    SohGui::RegisterPopup("Outdated ROM Archives",
                          "\x1b[2;2HYou've launched the Ship with an old ROM O2R file."
                          "\x1b[4;2HPlease regenerate a new ROM O2R and relaunch."
                          "\x1b[6;2HPress the Home button to exit...",
                          "OK", "", [&]() { exit(1); });
#elif defined(__WIIU__)
    SohGui::RegisterPopup("Outdated ROM Archives",
                          "You've launched the Ship with an old a ROM O2R file.\n\n"
                          "Please generate a ROM O2R and relaunch.\n\n"
                          "Press and hold the Power button to shutdown...",
                          "OK", "", [&]() { exit(1); });
    OSFatal();
#endif

    if (!std::filesystem::exists(installPath + "/assets")) {
        SohGui::RegisterPopup("Extractor assets not found",
                              "No O2R files found. Missing 'assets/' folder needed to generate OTR file.\nPlease "
                              "re-extract them from the download or.\n\nExiting...",
                              "OK", "", [&]() { exit(1); });
    } else if (shouldRegen) {
        if (RemoveArchiveAcrossAppDirs("oot.o2r") && RemoveArchiveAcrossAppDirs("oot-mq.o2r")) {
            SohGui::RegisterPopup(
                "Outdated ROM Archives",
                "Your oot.o2r or oot-mq.o2r were created with incompatible versions of SoH.\nYou will "
                "now be redirected to re-extract them.");
        } else {
            SohGui::RegisterPopup(
                "Outdated ROM Archives",
                "Your oot.o2r or oot-mq.o2r were created with incompatible\nversions of SoH, but they"
                "could not be removed\nautomatically. Please delete them now and re-launch.\nExiting...",
                "OK", "", [&]() { exit(1); });
        }
    }

    std::shared_ptr<BS::thread_pool> threadPool = std::make_shared<BS::thread_pool>(1);
    std::optional<std::future<void>> extractionTask;

#if not defined(__SWITCH__) && not defined(__WIIU__)
    CheckAndCreateModFolder();
#endif

    while (!extractDone) {
        if (SohGui::PopupsQueued() > 0 || extractionTask.has_value()) {
            goto render;
        }
        switch (extractStep) {
            case ES_PORT_ARCHIVE: {
                if (sohArchiveVersionMatch) {
#ifdef _WIN32
                    extractStep = ES_WINDOWS;
#elif (defined(__WIIU__) || defined(__SWITCH__))
                    extractStep = ES_VERIFY;
#else
                    extractStep = args.empty() ? ES_EXTRACT : ES_EXTRACT_ARGS;
#endif
                } else {
                    std::string msg;

#if defined(__SWITCH__)
                    msg = "\x1b[4;2HPlease re-extract it from the download.\n"
                          "\x1b[6;2HPress the Home button to exit...";
#elif defined(__WIIU__)
                    msg = "Please extract the soh.o2r from the Ship of Harkinian download\nto your folder.\n\nPress "
                          "and hold the power\n"
                          "button to shutdown...";
#else
                    msg =
                        "Please extract the soh.o2r from the Ship of Harkinian download to your folder.\n\nExiting...";
#endif
                    std::string title =
                        !std::filesystem::exists(portArchivePath) ? "Missing soh.o2r" : "soh.o2r is outdated";
                    SohGui::RegisterPopup(title, msg, "OK", "", [&]() { exit(1); });
                }
                continue;
            }
            case ES_WINDOWS: {
                switch (windowsStep) {
                    case WS_TEMP: {
#ifdef _WIN32
                        char* tempVar = getenv("TEMP");
                        std::filesystem::path tempPath;
                        try {
                            tempPath = std::filesystem::canonical(tempVar);
                        } catch ([[maybe_unused]] std::filesystem::filesystem_error const& ex) {
                            std::string userPath = getenv("USERPROFILE");
                            userPath.append("\\AppData\\Local\\Temp");
                            tempPath = std::filesystem::canonical(userPath);
                        }
                        wchar_t buffer[MAX_PATH];
                        GetModuleFileName(NULL, buffer, _countof(buffer));
                        ownPath = std::filesystem::canonical(buffer).parent_path();
                        if (IsSubpath(ownPath, tempPath)) {
                            SohGui::RegisterPopup("SoH Path Error",
                                                  "SoH is running in a temp folder.\nExtract the .zip and run again.",
                                                  "OK", "", [&]() { exit(0); });
                        } else {
                            windowsStep = WS_PERMS;
                        }
#endif
                        continue;
                    }
                    case WS_PERMS: {
                        FILE* tfile = fopen("./text.txt", "w");
                        std::filesystem::path tfolder = std::filesystem::path("./test/");
                        bool error = false;
                        try {
                            create_directories(tfolder);
                        } catch ([[maybe_unused]] std::filesystem::filesystem_error const& ex) { error = true; }
                        if (tfile == NULL || error) {
                            SohGui::RegisterPopup("SoH Permissions Error",
                                                  "SoH does not have proper file permissions.\nPlease move it to a "
                                                  "folder that does and run again.",
                                                  "OK", "", [&]() {
                                                      fclose(tfile);
                                                      PathTestCleanup(tfile);
                                                      exit(0);
                                                  });
                        } else {
                            fclose(tfile);
                            if (!PathTestCleanup(tfile)) {
                                SohGui::RegisterPopup("SoH Permissions Error",
                                                      "SoH does not have proper file permissions.\nPlease move it to a "
                                                      "folder that does and run again.",
                                                      "OK", "", [&]() { exit(0); });
                            }
                            windowsStep = WS_ONEDRIVE;
                        }
                        continue;
                    }
                    case WS_ONEDRIVE: {
                        if (ownPath.string().find("OneDrive") != std::string::npos) {
                            SohGui::RegisterPopup("SoH Path Error",
                                                  "SoH appears to be in a OneDrive folder, which will cause issues.\n"
                                                  "Please move it to a folder outside of OneDrive, like the root of a\n"
                                                  "drive (e.g. \"C:\\Games\\SoH\").",
                                                  "OK", "", [&]() { exit(0); });
                        } else {
                            windowsStep = WS_DONE;
                            extractStep = args.empty() ? ES_EXTRACT : ES_EXTRACT_ARGS;
                        }
                        continue;
                    }
                    default:
                        continue;
                }
                break;
            }
            case ES_EXTRACT_ARGS: {
#if !defined(__SWITCH__) && !defined(__WIIU__)
                if (args.empty()) {
                    SohGui::RegisterPopup(
                        "Run Ship of Harkinian", "All files have been processed. Run SoH?", "Yes", "No",
                        [&]() {
                            if (!std::filesystem::exists(Ship::Context::GetAppDirectoryPath(appShortName) +
                                                         "/oot.o2r") &&
                                !std::filesystem::exists(Ship::Context::GetAppDirectoryPath(appShortName) +
                                                         "/oot-mq.o2r")) {
                                extractStep = ES_EXTRACT;
                                promptStep = PS_FILE_CHECK;
                            } else {
                                extractStep = ES_VERIFY;
                            }
                        },
                        [&]() { exit(0); });
                    break;
                }
                file = args.at(0);
                args.erase(args.begin());
                extract = Extractor();
                if (extract.RunFileStandalone(file)) {
                    bool doExtract = true;
                    std::string archive = (extract.IsMasterQuest() ? "oot-mq.o2r" : "oot.o2r");
                    if (std::filesystem::exists(Ship::Context::GetAppDirectoryPath(appShortName) + "/" + archive)) {
                        std::string msg = "Archive for current ROM, " + archive + ", already exists.\nExtract again?";
                        SohGui::RegisterPopup("Confirm Re-extract", msg.c_str(), "Yes", "No", [&]() {
                            extractionTask = threadPool->submit_task([&]() -> void {
                                extract.CallTorch(installPath, Ship::Context::GetAppDirectoryPath(appShortName),
                                                  &extractCount, &totalExtract);
                                extractCount = totalExtract = 0;
                            });
                        });
                    } else {
                        extractionTask = threadPool->submit_task([&]() -> void {
                            extract.CallTorch(installPath, Ship::Context::GetAppDirectoryPath(appShortName),
                                              &extractCount, &totalExtract);
                            extractCount = totalExtract = 0;
                        });
                    }
                } else {
                    bool open = true;
                    std::string msg = "File\n" + std::string(file) + "\nis not a ROM or does not match supported ROMs.";
                    SohGui::RegisterPopup("SoH ROM Error", msg.c_str());
                }
#else
                extractStep = ES_VERIFY;
#endif
                break;
            }
            case ES_EXTRACT: {
                switch (promptStep) {
                    case PS_FILE_CHECK: {
                        const bool ootO2RExists =
                            std::filesystem::exists(
                                Ship::Context::LocateFileAcrossAppDirs("oot-mq.o2r", appShortName)) ||
                            std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs("oot.o2r", appShortName));

                        if (!ootO2RExists) {
                            SohGui::RegisterPopup(
                                "No O2R Files", "No O2R files found. Generate one now?", "Yes", "No",
                                [&]() { promptStep = PS_LOCAL; }, [&]() { exit(0); });
                        } else {
                            extractStep = ES_VERIFY;
                        }
                        continue;
                    }
                    case PS_LOCAL: {
                        extract = Extractor();
                        extract.SetSearchPath(installPath);
                        extract.GetRoms(args);
                        if (installPath != dataPath) {
                            extract.SetSearchPath(dataPath);
                            extract.GetRoms(args);
                        }
                        if (!args.empty()) {
                            promptStep = PS_WAIT;
                            SohGui::RegisterPopup(
                                "ROMs found", "ROMs found in application directory. Would you like to process them?",
                                "Yes", "No", [&]() { extractStep = ES_EXTRACT_ARGS; },
                                [&]() { promptStep = PS_FIRST; });
                        } else {
                            promptStep = PS_FIRST;
                        }
                        continue;
                    }
                    case PS_FIRST: {
                        if (!extract.ManuallySearchForRomMatchingType(RomSearchMode::Both)) {
                            promptStep = PS_FILE_CHECK;
                            continue;
                        }
                        extractionTask = threadPool->submit_task([&]() -> void {
                            extract.CallTorch(installPath, Ship::Context::GetAppDirectoryPath(appShortName),
                                              &extractCount, &totalExtract);
                            generatedIsMQ = extract.IsMasterQuest();
                            promptStep = PS_SECOND;
                            extractCount = 0;
                            totalExtract = 0;
                        });
                        continue;
                    }
                    case PS_SECOND: {
                        SohGui::RegisterPopup(
                            "Extraction Complete", "ROM Extracted. Extract another?", "Yes", "No",
                            [&]() {
                                if (!extract.ManuallySearchForRomMatchingType(generatedIsMQ ? RomSearchMode::Vanilla
                                                                                            : RomSearchMode::MQ)) {
                                    extractStep = ES_VERIFY;
                                } else {
                                    extractionTask = threadPool->submit_task([&]() -> void {
                                        extract.CallTorch(installPath, Ship::Context::GetAppDirectoryPath(appShortName),
                                                          &extractCount, &totalExtract);
                                        extractStep = ES_VERIFY;
                                        extractCount = 0;
                                        totalExtract = 0;
                                    });
                                }
                            },
                            [&]() { extractStep = ES_VERIFY; });
                        continue;
                    }
                    default:
                        break;
                }
                break;
            }
            case ES_VERIFY: {
                const bool ootO2RExists =
                    std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs("oot-mq.o2r", appShortName)) ||
                    std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs("oot.o2r", appShortName));

                if (!ootO2RExists) {
                    SohGui::RegisterPopup("No ROM Archives",
                                          "No ROM O2R files detected. Please generate a ROM O2R and relaunch.", "OK",
                                          "", [&]() { exit(0); });
                }
                extractDone = true;
                continue;
            }
            default:
                break;
        }

    render:
        if (!WindowIsRunning()) {
            // SoH-3DS: see graph.c RunFrame - exit(0) without DeinitOTR tears
            // down NDSP under a live audio thread in static destructors.
            // (Declared at file scope below the includes: OTRGlobals.h only
            // exposes DeinitOTR to C translation units.)
            DeinitOTR();
            exit(0);
        }
        // Process window events for resize, mouse, keyboard events
        wnd->HandleEvents();
        UIWidgets::Colors themeColor =
            static_cast<UIWidgets::Colors>(CVarGetInteger(CVAR_SETTING("Menu.Theme"), UIWidgets::Colors::LightBlue));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, UIWidgets::ColorValues.at(themeColor));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, UIWidgets::ColorValues.at(UIWidgets::Colors::DarkGray));

        // Skip dropped frames
        if (!wnd->IsFrameReady()) {
            continue;
        }
        gui->StartDraw();
        sohFast3dWindow->StartFrame();
        sohFast3dWindow->RunGuiOnly();
        if (extractionTask.has_value()) {
            auto status = extractionTask->wait_for(std::chrono::milliseconds(0));
            if (status == std::future_status::ready) {
                try {
                    extractionTask->get();
                } catch (const std::exception& e) {
                    SohGui::RegisterPopup("Extraction Crashed", e.what(), "Close", "", []() { exit(1); });
                }
                extractionTask.reset();
            } else {
                if (!ImGui::IsPopupOpen("ROM Extraction")) {
                    ImGui::OpenPopup("ROM Extraction");
                }
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
                auto color = UIWidgets::ColorValues.at(THEME_COLOR);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(color.x, color.y, color.z, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(color.x, color.y, color.z, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.3f));
                if (ImGui::BeginPopupModal("ROM Extraction", NULL,
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                               ImGuiWindowFlags_NoSavedSettings)) {
                    float progress = (totalExtract > 0.0f ? (float)extractCount / (float)totalExtract : 0) * 100.0f;
                    auto filename = std::filesystem::path(file).filename().string();
                    ImGui::Text("Extracting %s...%s", filename.c_str(),
                                roundf(progress) == 100.0f ? " Done. Finishing up." : "");
                    std::string overlay =
                        extractCount > 0 ? spdlog::fmt_lib::format("{:.0f}%", progress) : "Starting Up";
                    ImGui::ProgressBar(progress / 100.0f, ImVec2(600.0f, 50.0f), overlay.c_str());
                    ImGui::EndPopup();
                }
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(2);
            }
        }
        gui->EndDraw();
        sohFast3dWindow->EndFrame();
        ImGui::PopStyleColor(2);
    }

#ifdef __SWITCH__
    Ship::Switch::Init(Ship::PreInitPhase);
#elif defined(__WIIU__)
    Ship::WiiU::Init(appShortName);
#endif
}

void InitGfxDebugger() {
    auto dbg =
        std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow())->GetGfxDebugger();

    if (dbg != nullptr) {
        return;
    }

    dbg = std::make_shared<Fast::GfxDebugger>();

    if (dbg != nullptr) {
        SPDLOG_ERROR("Failed to initialize gfx debugger");
    }
}

void OTRGlobals::Initialize() {
    SOH3DS_INIT_TRACE("Initialize: locate oot-mq.o2r");
    std::string mqPath = Ship::Context::LocateFileAcrossAppDirs("oot-mq.o2r", appShortName);
    if (std::filesystem::exists(mqPath)
#ifdef __3DS__
        && Soh3dsGameArchiveReady(true)
#endif
    ) {
        context->GetResourceManager()->GetArchiveManager()->AddArchive(mqPath);
    }
    SOH3DS_INIT_TRACE("Initialize: locate oot.o2r");
    std::string ootPath = Ship::Context::LocateFileAcrossAppDirs("oot.o2r", appShortName);
#ifdef __3DS__
    // std::filesystem mangles 3DS drive prefixes ("sdmc:/x" -> "/sdmc:/x"), so
    // log what actually got resolved before trusting exists().
    fprintf(stderr, "soh-3ds init: oot.o2r resolved to \"%s\" exists=%d\n", ootPath.c_str(),
            (int)std::filesystem::exists(ootPath));
#endif
    if (std::filesystem::exists(ootPath)
#ifdef __3DS__
        && Soh3dsGameArchiveReady(false)
#endif
    ) {
        SOH3DS_INIT_TRACE("Initialize: AddArchive(oot.o2r) start");
        context->GetResourceManager()->GetArchiveManager()->AddArchive(ootPath);
        SOH3DS_INIT_TRACE("Initialize: AddArchive(oot.o2r) done");
    } else {
        SOH3DS_INIT_TRACE("Initialize: oot.o2r unavailable; using Master Quest if present");
    }

    std::unordered_set<uint32_t> ValidHashes = {
        OOT_PAL_MQ,     OOT_NTSC_JP_MQ, OOT_NTSC_US_MQ, OOT_PAL_GC_MQ_DBG, OOT_NTSC_US_10,
        OOT_NTSC_US_11, OOT_NTSC_US_12, OOT_PAL_10,     OOT_PAL_11,        OOT_NTSC_JP_GC_CE,
        OOT_NTSC_JP_GC, OOT_NTSC_US_GC, OOT_PAL_GC,     OOT_PAL_GC_DBG1,   OOT_PAL_GC_DBG2,
    };

#if (_DEBUG)
    auto defaultLogLevel = spdlog::level::trace;
#else
    auto defaultLogLevel = spdlog::level::info;
#endif
    SOH3DS_INIT_TRACE("Initialize: InitConfiguration");
    context->InitConfiguration();
    SOH3DS_INIT_TRACE("Initialize: InitConsoleVariables");
    context->InitConsoleVariables();
    auto logLevel =
        static_cast<spdlog::level::level_enum>(CVarGetInteger(CVAR_DEVELOPER_TOOLS("LogLevel"), defaultLogLevel));
    SOH3DS_INIT_TRACE("Initialize: InitLogging");
    context->InitLogging(logLevel, logLevel);
    Ship::Context::GetRawInstance()->GetLogger()->set_pattern("[%H:%M:%S.%e] [%s:%#] [%^%l%$] %v");

    SOH3DS_INIT_TRACE("Initialize: InitGfxDebugger");
    InitGfxDebugger();
    SOH3DS_INIT_TRACE("Initialize: InitFileDropMgr");
    context->InitFileDropMgr();

    // tell LUS to reserve 3 SoH specific threads (Game, Audio, Save)
#ifdef __3DS__
    // SoH-3DS: no HD texture packs fit in 3DS memory, and with alt assets ON
    // every resource lookup (SETTIMG, VTX, DL, MTX - hundreds per frame) first
    // builds an "alt/" path, hashes it and probes the cache under the mutex,
    // then repeats that inside CheckCache: three lookups to serve one.
    CVarSetInteger(CVAR_SETTING("AltAssets"), 0);
    // 30 fps interpolation by default: 20 Hz game logic, one interpolated
    // frame between ticks (Graph_ProcessGfxCommands alternates 1/2 frames per
    // tick). The renderer paces each frame to two vblanks; a frame that
    // overruns takes the next vblank, so heavy scenes degrade toward 20
    // instead of tearing or double-stepping the game. Registered, not set: a
    // saved slider value wins.
    CVarRegisterInteger(CVAR_SETTING("InterpolationFPS"), 30);
#endif
    prevAltAssets = CVarGetInteger(CVAR_SETTING("AltAssets"), 1);
    SOH3DS_INIT_TRACE("Initialize: SetAltAssetsEnabled");
    context->GetResourceManager()->SetAltAssetsEnabled(prevAltAssets);

    SOH3DS_INIT_TRACE("Initialize: InitCrashHandler");
    context->InitCrashHandler();

    SOH3DS_INIT_TRACE("Initialize: GetWindow()->SetAutoCaptureMouse");
    context->GetWindow()->SetAutoCaptureMouse(CVarGetInteger(CVAR_SETTING("EnableMouse"), 0) &&
                                              CVarGetInteger(CVAR_SETTING("AutoCaptureMouse"), 1));
    context->GetWindow()->SetForceCursorVisibility(CVarGetInteger(CVAR_SETTING("CursorVisibility"), 0));

    SOH3DS_INIT_TRACE("Initialize: InitAudio");
    context->InitAudio({ .SampleRate = 32000,
                         .SampleLength = 1024,
                         // 2048 frames at 32 kHz (~64 ms) matches mario-kart-64-3ds's
                         // reservoir. The earlier 4096 masked synthesis starvation when
                         // the audio thread shared core 0 with the game loop; with the
                         // thread on its own core the deep buffer was pure latency.
                         .DesiredBuffered = 2048 });

    // The menu is set up before audio is initialized, so its list of available audio backends has to be
    // populated here rather than in Menu::InitElement (where the window backends are handled).
    SOH3DS_INIT_TRACE("Initialize: UpdateAudioBackendObjects");
    SohGui::GetSohMenu()->UpdateAudioBackendObjects();

    SPDLOG_INFO("Starting Ship of Harkinian version {} (Branch: {} | Commit: {})", (char*)gBuildVersion,
                (char*)gGitBranch, (char*)gGitCommitHash);

    SOH3DS_INIT_TRACE("InitOTR: GetResourceLoader");
    auto loader = context->GetResourceManager()->GetResourceLoader();
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #1");
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV0>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #2");
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV1>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 1);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #3");
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryVertexV0>(), RESOURCE_FORMAT_BINARY,
                                    "Vertex", static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #4");
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryXMLVertexV0>(), RESOURCE_FORMAT_XML, "Vertex",
                                    static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #5");
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryDisplayListV0>(),
                                    RESOURCE_FORMAT_BINARY, "DisplayList",
                                    static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #6");
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryXMLDisplayListV0>(), RESOURCE_FORMAT_XML,
                                    "DisplayList", static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #7");
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryMatrixV0>(), RESOURCE_FORMAT_BINARY,
                                    "Matrix", static_cast<uint32_t>(Fast::ResourceType::Matrix), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #8");
    loader->RegisterResourceFactory(std::make_shared<Ship::ResourceFactoryBinaryBlobV0>(), RESOURCE_FORMAT_BINARY,
                                    "Blob", static_cast<uint32_t>(Ship::ResourceType::Blob), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #9");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryArrayV0>(), RESOURCE_FORMAT_BINARY,
                                    "Array", static_cast<uint32_t>(SOH::ResourceType::SOH_Array), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #10");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryAnimationV0>(), RESOURCE_FORMAT_BINARY,
                                    "Animation", static_cast<uint32_t>(SOH::ResourceType::SOH_Animation), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #11");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryPlayerAnimationV0>(),
                                    RESOURCE_FORMAT_BINARY, "PlayerAnimation",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_PlayerAnimation), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #12");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinarySceneV0>(), RESOURCE_FORMAT_BINARY,
                                    "Room", static_cast<uint32_t>(SOH::ResourceType::SOH_Room),
                                    0); // Is room scene? maybe?
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #13");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLSceneV0>(), RESOURCE_FORMAT_XML, "Room",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_Room), 0); // Is room scene? maybe?
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #14");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryCollisionHeaderV0>(),
                                    RESOURCE_FORMAT_BINARY, "CollisionHeader",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_CollisionHeader), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #15");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLCollisionHeaderV0>(), RESOURCE_FORMAT_XML,
                                    "CollisionHeader", static_cast<uint32_t>(SOH::ResourceType::SOH_CollisionHeader),
                                    0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #16");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinarySkeletonV0>(), RESOURCE_FORMAT_BINARY,
                                    "Skeleton", static_cast<uint32_t>(SOH::ResourceType::SOH_Skeleton), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #17");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLSkeletonV0>(), RESOURCE_FORMAT_XML,
                                    "Skeleton", static_cast<uint32_t>(SOH::ResourceType::SOH_Skeleton), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #18");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinarySkeletonLimbV0>(),
                                    RESOURCE_FORMAT_BINARY, "SkeletonLimb",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_SkeletonLimb), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #19");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLSkeletonLimbV0>(), RESOURCE_FORMAT_XML,
                                    "SkeletonLimb", static_cast<uint32_t>(SOH::ResourceType::SOH_SkeletonLimb), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #20");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryPathV0>(), RESOURCE_FORMAT_BINARY,
                                    "Path", static_cast<uint32_t>(SOH::ResourceType::SOH_Path), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #21");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLPathV0>(), RESOURCE_FORMAT_XML, "Path",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_Path), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #22");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryCutsceneV0>(), RESOURCE_FORMAT_BINARY,
                                    "Cutscene", static_cast<uint32_t>(SOH::ResourceType::SOH_Cutscene), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #23");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryTextV0>(), RESOURCE_FORMAT_BINARY,
                                    "Text", static_cast<uint32_t>(SOH::ResourceType::SOH_Text), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #24");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLTextV0>(), RESOURCE_FORMAT_XML, "Text",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_Text), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #25");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryAudioSampleV2>(), RESOURCE_FORMAT_BINARY,
                                    "AudioSample", static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSample), 2);

    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #26");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLAudioSampleV0>(), RESOURCE_FORMAT_XML,
                                    "Sample", static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSample), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #27");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryAudioSoundFontV2>(),
                                    RESOURCE_FORMAT_BINARY, "AudioSoundFont",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSoundFont), 2);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #28");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLSoundFontV0>(), RESOURCE_FORMAT_XML,
                                    "SoundFont", static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSoundFont), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #29");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryAudioSequenceV2>(),
                                    RESOURCE_FORMAT_BINARY, "AudioSequence",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSequence), 2);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #30");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLAudioSequenceV0>(), RESOURCE_FORMAT_XML,
                                    "Sequence", static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSequence), 0);
    SOH3DS_INIT_TRACE("InitOTR: RegisterResourceFactory #31");
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryBackgroundV0>(), RESOURCE_FORMAT_BINARY,
                                    "Background", static_cast<uint32_t>(SOH::ResourceType::SOH_Background), 0);

    SOH3DS_INIT_TRACE("Init: Lang::LoadLangs");
    Lang::LoadLangs();

    SOH3DS_INIT_TRACE("Init: SaveStateMgr");
    gSaveStateMgr = std::make_shared<SaveStateMgr>();
    SOH3DS_INIT_TRACE("Init: gRandoContext->InitStaticData");
    gRandoContext->InitStaticData();
    SOH3DS_INIT_TRACE("Init: Rando::Context::CreateInstance");
    gRandoContext = Rando::Context::CreateInstance();
    SOH3DS_INIT_TRACE("Init: AssignContext");
    Rando::Settings::GetInstance()->AssignContext(gRandoContext);
    SOH3DS_INIT_TRACE("Init: InitItemTable");
    Rando::StaticData::InitItemTable(); // RANDOTODO make this not rely on context's logic so it can be initialised in
                                        // InitStaticData
    SOH3DS_INIT_TRACE("Init: new Randomizer");
    gRandomizer = std::make_shared<Randomizer>();

    hasMasterQuest = hasOriginal = false;

    // Move the camera strings from read only memory onto the heap (writable memory)
    // This is in OTRGlobals right now because this is a place that will only ever be run once at the beginning of
    // startup. We should probably find some code in db_camera that does initialization and only run once, and then
    // dealloc on deinitialization.
    SOH3DS_INIT_TRACE("Init: cameraStrings");
    cameraStrings = (char**)malloc(sizeof(constCameraStrings));
    for (size_t i = 0; i < sizeof(constCameraStrings) / sizeof(char*); i++) {
        // OTRTODO: never deallocated...
        cameraStrings[i] = strdup(constCameraStrings[i]);
    }

    SOH3DS_INIT_TRACE("Init: GetGameVersions");
    auto versions = context->GetResourceManager()->GetArchiveManager()->GetGameVersions();

    for (uint32_t version : versions) {
        if (!ValidHashes.contains(version)) {
#if defined(__SWITCH__)
            SPDLOG_ERROR("Invalid OTR File!");
#elif defined(__WIIU__)
            Ship::WiiU::ThrowInvalidOTR();
#else
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Invalid OTR File",
                                     "Attempted to load an invalid OTR file. Try regenerating.", nullptr);
            SPDLOG_ERROR("Invalid OTR File!");
#endif
            exit(1);
        }
        switch (version) {
            case OOT_PAL_MQ:
            case OOT_NTSC_JP_MQ:
            case OOT_NTSC_US_MQ:
            case OOT_PAL_GC_MQ_DBG:
                hasMasterQuest = true;
                break;
            case OOT_NTSC_US_10:
            case OOT_NTSC_US_11:
            case OOT_NTSC_US_12:
            case OOT_PAL_10:
            case OOT_PAL_11:
            case OOT_NTSC_JP_GC_CE:
            case OOT_NTSC_JP_GC:
            case OOT_NTSC_US_GC:
            case OOT_PAL_GC:
            case OOT_PAL_GC_DBG1:
            case OOT_PAL_GC_DBG2:
                hasOriginal = true;
                break;
            default:
                break;
        }
    }
}

OTRGlobals::~OTRGlobals() {
}

void OTRGlobals::ScaleImGui() {
    // SoH-3DS: ImGui::GetStyle() is GImGui->Style with no null check, so calling
    // this before a context exists computes &((ImGuiContext*)0)->Style - 0xC74 -
    // and ScaleAllSizes then faults reading 0xC7C.
    //
    // On desktop the context happens to exist by the time anything calls this.
    // On 3DS it does not: something in the randomizer's static-data init reaches
    // here while the window/Gui is still being built. Guarding at the call site
    // rather than chasing the caller, because "scale the UI before the UI
    // exists" is a no-op under any caller.
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    int32_t imGuiScaleIndex = CVarGetInteger(CVAR_SETTING("ImGuiScale"), defaultImGuiScale);
    if (imGuiScaleIndex == previousImGuiScaleIndex) {
        return;
    }

    float scale = imguiScaleOptionToValue[imGuiScaleIndex];
    float newScale = scale / previousImGuiScale;
    ImGui::GetStyle().ScaleAllSizes(newScale);
    ImGui::GetIO().FontGlobalScale = scale;
    previousImGuiScale = scale;
    previousImGuiScaleIndex = imGuiScaleIndex;
}

bool OTRGlobals::HasMasterQuest() {
    return hasMasterQuest;
}

bool OTRGlobals::HasOriginal() {
    return hasOriginal;
}

uint32_t OTRGlobals::GetInterpolationFPS() {
    if (CVarGetInteger(CVAR_SETTING("MatchRefreshRate"), 0)) {
        return Ship::Context::GetRawInstance()->GetWindow()->GetCurrentRefreshRate();
    } else if (CVarGetInteger(CVAR_VSYNC_ENABLED, 1) ||
               !Ship::Context::GetRawInstance()->GetWindow()->CanDisableVerticalSync()) {
        return std::min<uint32_t>(Ship::Context::GetRawInstance()->GetWindow()->GetCurrentRefreshRate(),
                                  CVarGetInteger(CVAR_SETTING("InterpolationFPS"), 20));
    }
    return CVarGetInteger(CVAR_SETTING("InterpolationFPS"), 20);
}

extern "C" void OTRMessage_Init();
extern "C" void AudioMgr_CreateNextAudioBuffer(s16* samples, u32 num_samples);
extern "C" void AudioPlayer_Play(const uint8_t* buf, uint32_t len);
int AudioPlayer_Buffered(void);
extern "C" int AudioPlayer_GetDesiredBuffered(void);
std::unordered_map<std::string, ExtensionEntry> ExtensionCache;

void OTRAudio_Thread() {
#define SAMPLES_HIGH 560
#define SAMPLES_MID 544
#define SAMPLES_LOW 528
#define AUDIO_FRAMES_PER_UPDATE (R_UPDATE_RATE > 0 ? R_UPDATE_RATE : 1)
#define NUM_AUDIO_CHANNELS 2

    // The sequencer advances a fixed slice of musical time per engine update
    // (tempoInternalToExternal in audio_heap.c assumes 60 updates/sec), so with
    // production paced by backend buffer fill the sample count must average
    // exactly 32000/60 = 533.33 per update or tempo drifts.
    // Two thirds 528 one third 544 gives 533.33.
    int32_t sample_debt_thirds = 0;

    // Single producer routine used by both wake-driven and pre-buffer loops.
    // Picks per-iteration sample count itself, then produces and plays it.
    //
    // SoH-3DS (gated): produce ONE engine update (528/544 frames, ~16.5 ms)
    // per call instead of AUDIO_FRAMES_PER_UPDATE bundled updates (~49.5 ms).
    // With a 2048-frame reservoir, a 3-update batch means the producer guard
    // only admits a refill once the queue is below 416 frames (13 ms) - and
    // synthesizing the next 49.5 ms batch takes ~22 ms at stock 3DS speed, so
    // the queue ran dry mid-synthesis on most refills (measured 65% gap rate;
    // synthesis itself is only ~45% of real time). Single updates raise the
    // refill floor to 47 ms with the same reservoir and latency. The
    // sequencer's tempo debt is tracked per update, so musical time is
    // unchanged; the pre-buffer loop below still tops the queue up fully.
    // Desktop keeps the upstream bundled cadence.
#ifdef __3DS__
#define UPDATES_PER_BATCH 1
#else
#define UPDATES_PER_BATCH AUDIO_FRAMES_PER_UPDATE
#endif
    auto produce_next_batch = [&]() {
        u32 num_audio_samples = sample_debt_thirds > 0 ? SAMPLES_MID : SAMPLES_LOW;
        sample_debt_thirds += (1600 - 3 * (int32_t)num_audio_samples) * UPDATES_PER_BATCH;

        const u32 total_frames = num_audio_samples * UPDATES_PER_BATCH;
        const u32 total_samples = total_frames * NUM_AUDIO_CHANNELS;

        // 3 is the maximum authentic frame divisor.
        // SoH-3DS: was `static thread_local`. thread_local placed a 6.6 KiB
        // copy in EVERY thread's TLS, which libctru lays out in the same
        // guard-free heap block as the thread's stack, immediately above the
        // stack top - hardware dumps 12/17 carry garbage sp values pointing
        // into exactly that region of the audio thread's block. NOT proven as
        // the corruption source (3DS writes at most 1088 of 3360 slots per
        // call), but one thread owns this routine, so a plain static is
        // correct anyway: .bss placement, far from every stack, and 6.6 KiB
        // less TLS per thread. Doubles as an experiment - if the poisoned
        // frame crashes move or stop, this neighborhood was implicated.
        static s16 audio_buffer[SAMPLES_HIGH * NUM_AUDIO_CHANNELS * 3];
        // Tripwire for the unproven half: nothing may size a batch beyond the
        // buffer. Compile-time for the constants, runtime for the debt logic.
        static_assert(SAMPLES_HIGH >= SAMPLES_MID && SAMPLES_HIGH >= SAMPLES_LOW, "batch sizing");
        if (total_samples > sizeof(audio_buffer) / sizeof(audio_buffer[0])) {
            fprintf(stderr, "audio: BATCH OVERRUN %lu samples > buffer, clamping\n", (unsigned long)total_samples);
            return;
        }

#ifdef __3DS__
        // SoH-3DS: temporary diagnostics - measure raw synthesis cost against
        // real time. Each batch is total_frames/32000 seconds of audio; if
        // synthesis ticks exceed that, no pacing fix can prevent gaps.
        extern int64_t svcGetSystemTick(void) __asm__("svcGetSystemTick");
        const uint64_t synthStart = (uint64_t)svcGetSystemTick();
#endif
        for (int i = 0; i < UPDATES_PER_BATCH; i++) {
            AudioMgr_CreateNextAudioBuffer(audio_buffer + i * (num_audio_samples * NUM_AUDIO_CHANNELS),
                                           num_audio_samples);
        }
#ifdef __3DS__
        {
            static uint64_t sSynthTicks = 0;
            static uint64_t sAudioFrames = 0;
            static uint32_t sBatches = 0;
            sSynthTicks += (uint64_t)svcGetSystemTick() - synthStart;
            sAudioFrames += total_frames;
            if (++sBatches % 256 == 0) {
                // 268 ticks/us; batch real-time budget is frames/32 us.
                const uint64_t synthUs = sSynthTicks / 268u;
                const uint64_t budgetUs = sAudioFrames * 1000u / 32u;
                // SPDLOG so this reaches sdmc:/logs/SoH-3DS.log on hardware,
                // where svcOutputDebugString output is invisible.
                SPDLOG_INFO("SoH-3DS audio: synth {}ms for {}ms of audio ({}%)", (unsigned long)(synthUs / 1000u),
                            (unsigned long)(budgetUs / 1000u),
                            (unsigned long)(budgetUs > 0 ? synthUs * 100u / budgetUs : 0));
                sSynthTicks = 0;
                sAudioFrames = 0;
            }
        }
#endif

        AudioPlayer_Play(reinterpret_cast<u8*>(audio_buffer), total_samples * sizeof(int16_t));
    };

    // Self-pump cadence. The gfx thread wakes us once per rendered frame
    // (Graph_ProcessGfxCommands sets audio.processing), but a single long
    // frame leave us asleep while the backend's queue drains to silence.
    // So we also wake on a short timeout, independent of the gfx frame rate.
    // Doing so is in fact closer to the console, where the audio task ran
    // off the scheduler rather than gated on rendering..
    constexpr auto kSelfPumpInterval = std::chrono::milliseconds(5);

    // The self-pump timeout must wait that the game has reached its render
    // loop, to avoid accessing uninitialized variables.
    bool primed = false;

    while (audio.running) {
        {
            std::unique_lock<std::mutex> Lock(audio.mutex);
            if (!primed) {
                // Pre-init: block until the gfx thread drives the first buffer
                // (engine guaranteed ready by then), exactly as before.
                while (!audio.processing && audio.running) {
                    audio.cv_to_thread.wait(Lock);
                }
                primed = true;
            } else if (!audio.processing && audio.running) {
                // Primed: wait for the next gfx wake, but no longer than
                // kSelfPumpInterval so a stalled gfx thread can't starve the
                // backend queue. A pending wake falls straight through.
                audio.cv_to_thread.wait_for(Lock, kSelfPumpInterval);
            }

            if (!audio.running) {
                break;
            }
        }

        {
            std::unique_lock<std::mutex> Lock(audio.mutex);

            // Producer guard (banteg/Shipwright#6594): skip advancing the audio
            // engine if the backend ring cannot accept the largest next burst.
            // Generating PCM that DoPlay() would refuse creates a discontinuity
            // audible as a click. The pre-buffer loop below will catch up once
            // the backend drains enough.
            if (AudioPlayer_Buffered() + SAMPLES_MID * UPDATES_PER_BATCH > AudioPlayer_GetDesiredBuffered()) {
                audio.processing = false;
            } else {
                produce_next_batch();
                audio.processing = false;
            }
        }

        // Pre-buffer: fill the reservoir while the backend can accept more,
        // without waiting for the next frame signal. This absorbs load spikes.
        // Safe for BGM — the N64 sequencer advances independently of gameplay.
        // The producer guard (same as above) prevents advancing the audio engine
        // when the backend ring is already at capacity.
        while (audio.running && AudioPlayer_Buffered() < AudioPlayer_GetDesiredBuffered()) {
            if (AudioPlayer_Buffered() + SAMPLES_MID * UPDATES_PER_BATCH > AudioPlayer_GetDesiredBuffered()) {
                break;
            }
            produce_next_batch();
        }
    }
}
// Scoped to OTRAudio_Thread; keep the port switch from leaking into the rest
// of the translation unit.
#undef UPDATES_PER_BATCH

#ifdef __3DS__
// SoH-3DS: run the audio thread off core 0, following mario-kart-64-3ds
// (platform/3ds/source/audio_runtime_3ds.cpp). As a std::thread it lands on
// core 0 and competes with the game loop and renderer, so synthesis starves
// during heavy frames - audible as stutter - and the only mitigation was a
// deep (high-latency) backend buffer. Core 2 exists on New 3DS; on Old 3DS
// core 1 becomes usable once APT grants the app a share of the syscore.
// <3ds.h> conflicts with libultra's typedefs, so declare the few calls used.
extern "C" {
typedef struct Thread_tag* Soh3dsThreadHandle;
Soh3dsThreadHandle threadCreate(void (*entry)(void*), void* arg, size_t stackSize, int32_t prio, int32_t coreId,
                                bool detached);
int32_t threadJoin(Soh3dsThreadHandle thread, uint64_t timeoutNs);
void threadFree(Soh3dsThreadHandle thread);
int32_t APT_SetAppCpuTimeLimit(uint32_t percent);
int32_t APT_CheckNew3DS(bool* isNew);
}
static Soh3dsThreadHandle sSoh3dsAudioThread = nullptr;
static void Soh3dsAudioThreadEntry(void*) {
    OTRAudio_Thread();
}
#endif

void OTRAudio_Init() {
    // Precache all our samples, sequences, etc...
    ResourceMgr_LoadDirectory("audio");

    if (!audio.running) {
        audio.running = true;
#ifdef __3DS__
        constexpr size_t kAudioStackSize = 128 * 1024;
        constexpr int32_t kAudioPriority = 0x18; // higher than the 0x30 main thread
        bool isNewModel = false;
        APT_CheckNew3DS(&isNewModel);
        if (isNewModel) {
            sSoh3dsAudioThread = threadCreate(Soh3dsAudioThreadEntry, nullptr, kAudioStackSize, kAudioPriority, 2, false);
        }
        if (sSoh3dsAudioThread == nullptr) {
            for (uint32_t limit : { 80u, 70u, 50u, 30u }) {
                if (APT_SetAppCpuTimeLimit(limit) < 0) {
                    continue;
                }
                sSoh3dsAudioThread = threadCreate(Soh3dsAudioThreadEntry, nullptr, kAudioStackSize, kAudioPriority, 1, false);
                break;
            }
        }
        if (sSoh3dsAudioThread != nullptr) {
            fprintf(stderr, "soh-3ds audio: synthesis thread on core %d\n", isNewModel ? 2 : 1);
        } else {
            // ponytail: core-0 std::thread fallback keeps audio functional on
            // whatever refuses the app a second core.
            fprintf(stderr, "soh-3ds audio: no spare core, synthesis on core 0\n");
            audio.thread = std::thread(OTRAudio_Thread);
        }
#else
        audio.thread = std::thread(OTRAudio_Thread);
#endif
    }
}

// C->C++ Bridge
extern "C" char** sequenceMap;
extern "C" size_t sequenceMapSize;

extern "C" char** fontMap;
extern "C" size_t fontMapSize;

extern "C" void OTRAudio_Exit() {
    // Tell the audio thread to stop
    {
        std::unique_lock<std::mutex> Lock(audio.mutex);
        audio.running = false;
    }
    audio.cv_to_thread.notify_all();

    // Wait until the audio thread quit
#ifdef __3DS__
    if (sSoh3dsAudioThread != nullptr) {
        threadJoin(sSoh3dsAudioThread, UINT64_MAX);
        threadFree(sSoh3dsAudioThread);
        sSoh3dsAudioThread = nullptr;
    } else {
        audio.thread.join();
    }
#else
    audio.thread.join();
#endif
#if 0
    for (size_t i = 0; i < sequenceMapSize; i++) {
        free(sequenceMap[i]);
    }
    free(sequenceMap);

    for (size_t i = 0; i < fontMapSize; i++) {
        free(fontMap[i]);
    }
    free(fontMap);
    free(gAudioContext.seqLoadStatus);
    free(gAudioContext.fontLoadStatus);
#endif
}

void VanillaItemTable_Init() {
    static GetItemEntry getItemTable[] = {
        // clang-format off
        GET_ITEM(ITEM_BOMBS_5,          OBJECT_GI_BOMB_1,        GID_BOMB,             0x32, 0x59, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BOMBS_5),
        GET_ITEM(ITEM_NUTS_5,           OBJECT_GI_NUTS,          GID_NUTS,             0x34, 0x0C, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_NUTS_5),
        GET_ITEM(ITEM_BOMBCHU,          OBJECT_GI_BOMB_2,        GID_BOMBCHU,          0x33, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOMBCHUS_10),
        GET_ITEM(ITEM_BOW,              OBJECT_GI_BOW,           GID_BOW,              0x31, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOW),
        GET_ITEM(ITEM_SLINGSHOT,        OBJECT_GI_PACHINKO,      GID_SLINGSHOT,        0x30, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SLINGSHOT),
        GET_ITEM(ITEM_BOOMERANG,        OBJECT_GI_BOOMERANG,     GID_BOOMERANG,        0x35, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOOMERANG),
        GET_ITEM(ITEM_STICK,            OBJECT_GI_STICK,         GID_STICK,            0x37, 0x0D, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_STICKS_1),
        GET_ITEM(ITEM_HOOKSHOT,         OBJECT_GI_HOOKSHOT,      GID_HOOKSHOT,         0x36, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_HOOKSHOT),
        GET_ITEM(ITEM_LONGSHOT,         OBJECT_GI_HOOKSHOT,      GID_LONGSHOT,         0x4F, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_LONGSHOT),
        GET_ITEM(ITEM_LENS,             OBJECT_GI_GLASSES,       GID_LENS,             0x39, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_LENS),
        GET_ITEM(ITEM_LETTER_ZELDA,     OBJECT_GI_LETTER,        GID_LETTER_ZELDA,     0x69, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_LETTER_ZELDA),
        GET_ITEM(ITEM_OCARINA_TIME,     OBJECT_GI_OCARINA,       GID_OCARINA_TIME,     0x3A, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_OCARINA_OOT),
        GET_ITEM(ITEM_HAMMER,           OBJECT_GI_HAMMER,        GID_HAMMER,           0x38, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_HAMMER),
        GET_ITEM(ITEM_COJIRO,           OBJECT_GI_NIWATORI,      GID_COJIRO,           0x02, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_COJIRO),
        GET_ITEM(ITEM_BOTTLE,           OBJECT_GI_BOTTLE,        GID_BOTTLE,           0x42, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOTTLE),
        GET_ITEM(ITEM_POTION_RED,       OBJECT_GI_LIQUID,        GID_POTION_RED,       0x43, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_POTION_RED),
        GET_ITEM(ITEM_POTION_GREEN,     OBJECT_GI_LIQUID,        GID_POTION_GREEN,     0x44, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_POTION_GREEN),
        GET_ITEM(ITEM_POTION_BLUE,      OBJECT_GI_LIQUID,        GID_POTION_BLUE,      0x45, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_POTION_BLUE),
        GET_ITEM(ITEM_FAIRY,            OBJECT_GI_BOTTLE,        GID_BOTTLE,           0x46, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_FAIRY),
        GET_ITEM(ITEM_MILK_BOTTLE,      OBJECT_GI_MILK,          GID_MILK,             0x98, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MILK_BOTTLE),
        GET_ITEM(ITEM_LETTER_RUTO,      OBJECT_GI_BOTTLE_LETTER, GID_LETTER_RUTO,      0x99, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_LETTER_RUTO),
        GET_ITEM(ITEM_BEAN,             OBJECT_GI_BEAN,          GID_BEAN,             0x48, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BEAN),
        GET_ITEM(ITEM_MASK_SKULL,       OBJECT_GI_SKJ_MASK,      GID_MASK_SKULL,       0x10, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_SKULL),
        GET_ITEM(ITEM_MASK_SPOOKY,      OBJECT_GI_REDEAD_MASK,   GID_MASK_SPOOKY,      0x11, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_SPOOKY),
        GET_ITEM(ITEM_CHICKEN,          OBJECT_GI_NIWATORI,      GID_CHICKEN,          0x48, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_CHICKEN),
        GET_ITEM(ITEM_MASK_KEATON,      OBJECT_GI_KI_TAN_MASK,   GID_MASK_KEATON,      0x12, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_KEATON),
        GET_ITEM(ITEM_MASK_BUNNY,       OBJECT_GI_RABIT_MASK,    GID_MASK_BUNNY,       0x13, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_BUNNY),
        GET_ITEM(ITEM_MASK_TRUTH,       OBJECT_GI_TRUTH_MASK,    GID_MASK_TRUTH,       0x17, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_TRUTH),
        GET_ITEM(ITEM_POCKET_EGG,       OBJECT_GI_EGG,           GID_EGG,              0x01, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_POCKET_EGG),
        GET_ITEM(ITEM_POCKET_CUCCO,     OBJECT_GI_NIWATORI,      GID_CHICKEN,          0x48, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_POCKET_CUCCO),
        GET_ITEM(ITEM_ODD_MUSHROOM,     OBJECT_GI_MUSHROOM,      GID_ODD_MUSHROOM,     0x03, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_ODD_MUSHROOM),
        GET_ITEM(ITEM_ODD_POTION,       OBJECT_GI_POWDER,        GID_ODD_POTION,       0x04, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_ODD_POTION),
        GET_ITEM(ITEM_SAW,              OBJECT_GI_SAW,           GID_SAW,              0x05, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SAW),
        GET_ITEM(ITEM_SWORD_BROKEN,     OBJECT_GI_BROKENSWORD,   GID_SWORD_BROKEN,     0x08, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SWORD_BROKEN),
        GET_ITEM(ITEM_PRESCRIPTION,     OBJECT_GI_PRESCRIPTION,  GID_PRESCRIPTION,     0x09, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_PRESCRIPTION),
        GET_ITEM(ITEM_FROG,             OBJECT_GI_FROG,          GID_FROG,             0x0D, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_FROG),
        GET_ITEM(ITEM_EYEDROPS,         OBJECT_GI_EYE_LOTION,    GID_EYEDROPS,         0x0E, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_EYEDROPS),
        GET_ITEM(ITEM_CLAIM_CHECK,      OBJECT_GI_TICKETSTONE,   GID_CLAIM_CHECK,      0x0A, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_CLAIM_CHECK),
        GET_ITEM(ITEM_SWORD_KOKIRI,     OBJECT_GI_SWORD_1,       GID_SWORD_KOKIRI,     0xA4, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SWORD_KOKIRI),
        GET_ITEM(ITEM_SWORD_BGS,        OBJECT_GI_LONGSWORD,     GID_SWORD_BGS,        0x4B, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SWORD_KNIFE),
        GET_ITEM(ITEM_SHIELD_DEKU,      OBJECT_GI_SHIELD_1,      GID_SHIELD_DEKU,      0x4C, 0xA0, CHEST_ANIM_SHORT, ITEM_CATEGORY_LESSER,          MOD_NONE, GI_SHIELD_DEKU),
        GET_ITEM(ITEM_SHIELD_HYLIAN,    OBJECT_GI_SHIELD_2,      GID_SHIELD_HYLIAN,    0x4D, 0xA0, CHEST_ANIM_SHORT, ITEM_CATEGORY_LESSER,          MOD_NONE, GI_SHIELD_HYLIAN),
        GET_ITEM(ITEM_SHIELD_MIRROR,    OBJECT_GI_SHIELD_3,      GID_SHIELD_MIRROR,    0x4E, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SHIELD_MIRROR),
        GET_ITEM(ITEM_TUNIC_GORON,      OBJECT_GI_CLOTHES,       GID_TUNIC_GORON,      0x50, 0xA0, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_TUNIC_GORON),
        GET_ITEM(ITEM_TUNIC_ZORA,       OBJECT_GI_CLOTHES,       GID_TUNIC_ZORA,       0x51, 0xA0, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_TUNIC_ZORA),
        GET_ITEM(ITEM_BOOTS_IRON,       OBJECT_GI_BOOTS_2,       GID_BOOTS_IRON,       0x53, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOOTS_IRON),
        GET_ITEM(ITEM_BOOTS_HOVER,      OBJECT_GI_HOVERBOOTS,    GID_BOOTS_HOVER,      0x54, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOOTS_HOVER),
        GET_ITEM(ITEM_QUIVER_40,        OBJECT_GI_ARROWCASE,     GID_QUIVER_40,        0x56, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_QUIVER_40),
        GET_ITEM(ITEM_QUIVER_50,        OBJECT_GI_ARROWCASE,     GID_QUIVER_50,        0x57, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_QUIVER_50),
        GET_ITEM(ITEM_BOMB_BAG_20,      OBJECT_GI_BOMBPOUCH,     GID_BOMB_BAG_20,      0x58, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOMB_BAG_20),
        GET_ITEM(ITEM_BOMB_BAG_30,      OBJECT_GI_BOMBPOUCH,     GID_BOMB_BAG_30,      0x59, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_BOMB_BAG_30),
        GET_ITEM(ITEM_BOMB_BAG_40,      OBJECT_GI_BOMBPOUCH,     GID_BOMB_BAG_40,      0x5A, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_BOMB_BAG_40),
        GET_ITEM(ITEM_GAUNTLETS_SILVER, OBJECT_GI_GLOVES,        GID_GAUNTLETS_SILVER, 0x5B, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_GAUNTLETS_SILVER),
        GET_ITEM(ITEM_GAUNTLETS_GOLD,   OBJECT_GI_GLOVES,        GID_GAUNTLETS_GOLD,   0x5C, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_GAUNTLETS_GOLD),
        GET_ITEM(ITEM_SCALE_SILVER,     OBJECT_GI_SCALE,         GID_SCALE_SILVER,     0xCD, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SCALE_SILVER),
        GET_ITEM(ITEM_SCALE_GOLDEN,     OBJECT_GI_SCALE,         GID_SCALE_GOLDEN,     0xCE, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SCALE_GOLDEN),
        GET_ITEM(ITEM_STONE_OF_AGONY,   OBJECT_GI_MAP,           GID_STONE_OF_AGONY,   0x68, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_STONE_OF_AGONY),
        GET_ITEM(ITEM_GERUDO_CARD,      OBJECT_GI_GERUDO,        GID_GERUDO_CARD,      0x7B, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_GERUDO_CARD),
        GET_ITEM(ITEM_OCARINA_FAIRY,    OBJECT_GI_OCARINA_0,     GID_OCARINA_FAIRY,    0x4A, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_OCARINA_FAIRY),
        GET_ITEM(ITEM_SEEDS,            OBJECT_GI_SEED,          GID_SEEDS,            0xDC, 0x50, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_SEEDS_5),
        GET_ITEM(ITEM_HEART_CONTAINER,  OBJECT_GI_HEARTS,        GID_HEART_CONTAINER,  0xC6, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_HEALTH,          MOD_NONE, GI_HEART_CONTAINER),
        GET_ITEM(ITEM_HEART_PIECE_2,    OBJECT_GI_HEARTS,        GID_HEART_PIECE,      0xC2, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_HEALTH,          MOD_NONE, GI_HEART_PIECE),
        GET_ITEM(ITEM_KEY_BOSS,         OBJECT_GI_BOSSKEY,       GID_KEY_BOSS,         0xC7, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_BOSS_KEY,        MOD_NONE, GI_KEY_BOSS),
        GET_ITEM(ITEM_COMPASS,          OBJECT_GI_COMPASS,       GID_COMPASS,          0x67, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_COMPASS),
        GET_ITEM(ITEM_DUNGEON_MAP,      OBJECT_GI_MAP,           GID_DUNGEON_MAP,      0x66, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_MAP),
        GET_ITEM(ITEM_KEY_SMALL,        OBJECT_GI_KEY,           GID_KEY_SMALL,        0x60, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_SMALL_KEY,       MOD_NONE, GI_KEY_SMALL),
        GET_ITEM(ITEM_MAGIC_SMALL,      OBJECT_GI_MAGICPOT,      GID_MAGIC_SMALL,      0x52, 0x6F, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_MAGIC_SMALL),
        GET_ITEM(ITEM_MAGIC_LARGE,      OBJECT_GI_MAGICPOT,      GID_MAGIC_LARGE,      0x52, 0x6E, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_MAGIC_LARGE),
        GET_ITEM(ITEM_WALLET_ADULT,     OBJECT_GI_PURSE,         GID_WALLET_ADULT,     0x5E, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_WALLET_ADULT),
        GET_ITEM(ITEM_WALLET_GIANT,     OBJECT_GI_PURSE,         GID_WALLET_GIANT,     0x5F, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_WALLET_GIANT),
        GET_ITEM(ITEM_WEIRD_EGG,        OBJECT_GI_EGG,           GID_EGG,              0x9A, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_WEIRD_EGG),
        GET_ITEM(ITEM_HEART,            OBJECT_GI_HEART,         GID_HEART,            0x55, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_HEART),
        GET_ITEM(ITEM_ARROWS_SMALL,     OBJECT_GI_ARROW,         GID_ARROWS_SMALL,     0xE6, 0x48, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_ARROWS_SMALL),
        GET_ITEM(ITEM_ARROWS_MEDIUM,    OBJECT_GI_ARROW,         GID_ARROWS_MEDIUM,    0xE6, 0x49, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_ARROWS_MEDIUM),
        GET_ITEM(ITEM_ARROWS_LARGE,     OBJECT_GI_ARROW,         GID_ARROWS_LARGE,     0xE6, 0x4A, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_ARROWS_LARGE),
        GET_ITEM(ITEM_RUPEE_GREEN,      OBJECT_GI_RUPY,          GID_RUPEE_GREEN,      0x6F, 0x00, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_GREEN),
        GET_ITEM(ITEM_RUPEE_BLUE,       OBJECT_GI_RUPY,          GID_RUPEE_BLUE,       0xCC, 0x01, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_BLUE),
        GET_ITEM(ITEM_RUPEE_RED,        OBJECT_GI_RUPY,          GID_RUPEE_RED,        0xF0, 0x02, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_RED),
        GET_ITEM(ITEM_HEART_CONTAINER,  OBJECT_GI_HEARTS,        GID_HEART_CONTAINER,  0xC6, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_HEALTH,          MOD_NONE, GI_HEART_CONTAINER_2),
        GET_ITEM(ITEM_MILK,             OBJECT_GI_MILK,          GID_MILK,             0x98, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_MILK),
        GET_ITEM(ITEM_MASK_GORON,       OBJECT_GI_GOLONMASK,     GID_MASK_GORON,       0x14, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_GORON),
        GET_ITEM(ITEM_MASK_ZORA,        OBJECT_GI_ZORAMASK,      GID_MASK_ZORA,        0x15, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_ZORA),
        GET_ITEM(ITEM_MASK_GERUDO,      OBJECT_GI_GERUDOMASK,    GID_MASK_GERUDO,      0x16, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_MASK_GERUDO),
        GET_ITEM(ITEM_BRACELET,         OBJECT_GI_BRACELET,      GID_BRACELET,         0x79, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BRACELET),
        GET_ITEM(ITEM_RUPEE_PURPLE,     OBJECT_GI_RUPY,          GID_RUPEE_PURPLE,     0xF1, 0x14, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_PURPLE),
        GET_ITEM(ITEM_RUPEE_GOLD,       OBJECT_GI_RUPY,          GID_RUPEE_GOLD,       0xF2, 0x13, CHEST_ANIM_SHORT, ITEM_CATEGORY_LESSER,          MOD_NONE, GI_RUPEE_GOLD),
        GET_ITEM(ITEM_SWORD_BGS,        OBJECT_GI_LONGSWORD,     GID_SWORD_BGS,        0x0C, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_SWORD_BGS),
        GET_ITEM(ITEM_ARROW_FIRE,       OBJECT_GI_M_ARROW,       GID_ARROW_FIRE,       0x70, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_ARROW_FIRE),
        GET_ITEM(ITEM_ARROW_ICE,        OBJECT_GI_M_ARROW,       GID_ARROW_ICE,        0x71, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_ARROW_ICE),
        GET_ITEM(ITEM_ARROW_LIGHT,      OBJECT_GI_M_ARROW,       GID_ARROW_LIGHT,      0x72, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_ARROW_LIGHT),
        GET_ITEM(ITEM_SKULL_TOKEN,      OBJECT_GI_SUTARU,        GID_SKULL_TOKEN,      0xB4, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_SKULLTULA_TOKEN, MOD_NONE, GI_SKULL_TOKEN),
        GET_ITEM(ITEM_DINS_FIRE,        OBJECT_GI_GODDESS,       GID_DINS_FIRE,        0xAD, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_DINS_FIRE),
        GET_ITEM(ITEM_FARORES_WIND,     OBJECT_GI_GODDESS,       GID_FARORES_WIND,     0xAE, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_FARORES_WIND),
        GET_ITEM(ITEM_NAYRUS_LOVE,      OBJECT_GI_GODDESS,       GID_NAYRUS_LOVE,      0xAF, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_NAYRUS_LOVE),
        GET_ITEM(ITEM_BULLET_BAG_30,    OBJECT_GI_DEKUPOUCH,     GID_BULLET_BAG,       0x07, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_BULLET_BAG_30),
        GET_ITEM(ITEM_BULLET_BAG_40,    OBJECT_GI_DEKUPOUCH,     GID_BULLET_BAG,       0x07, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_BULLET_BAG_40),
        GET_ITEM(ITEM_STICKS_5,         OBJECT_GI_STICK,         GID_STICK,            0x37, 0x0D, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_STICKS_5),
        GET_ITEM(ITEM_STICKS_10,        OBJECT_GI_STICK,         GID_STICK,            0x37, 0x0D, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_STICKS_10),
        GET_ITEM(ITEM_NUTS_5,           OBJECT_GI_NUTS,          GID_NUTS,             0x34, 0x0C, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_NUTS_5_2),
        GET_ITEM(ITEM_NUTS_10,          OBJECT_GI_NUTS,          GID_NUTS,             0x34, 0x0C, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_NUTS_10),
        GET_ITEM(ITEM_BOMB,             OBJECT_GI_BOMB_1,        GID_BOMB,             0x32, 0x59, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BOMBS_1),
        GET_ITEM(ITEM_BOMBS_10,         OBJECT_GI_BOMB_1,        GID_BOMB,             0x32, 0x59, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BOMBS_10),
        GET_ITEM(ITEM_BOMBS_20,         OBJECT_GI_BOMB_1,        GID_BOMB,             0x32, 0x59, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BOMBS_20),
        GET_ITEM(ITEM_BOMBS_30,         OBJECT_GI_BOMB_1,        GID_BOMB,             0x32, 0x59, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BOMBS_30),
        GET_ITEM(ITEM_SEEDS_30,         OBJECT_GI_SEED,          GID_SEEDS,            0xDC, 0x50, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_SEEDS_30),
        GET_ITEM(ITEM_BOMBCHUS_5,       OBJECT_GI_BOMB_2,        GID_BOMBCHU,          0x33, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOMBCHUS_5),
        GET_ITEM(ITEM_BOMBCHUS_20,      OBJECT_GI_BOMB_2,        GID_BOMBCHU,          0x33, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_MAJOR,           MOD_NONE, GI_BOMBCHUS_20),
        GET_ITEM(ITEM_FISH,             OBJECT_GI_FISH,          GID_FISH,             0x47, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_FISH),
        GET_ITEM(ITEM_BUG,              OBJECT_GI_INSECT,        GID_BUG,              0x7A, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BUGS),
        GET_ITEM(ITEM_BLUE_FIRE,        OBJECT_GI_FIRE,          GID_BLUE_FIRE,        0x5D, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BLUE_FIRE),
        GET_ITEM(ITEM_POE,              OBJECT_GI_GHOST,         GID_POE,              0x97, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_POE),
        GET_ITEM(ITEM_BIG_POE,          OBJECT_GI_GHOST,         GID_BIG_POE,          0xF9, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_JUNK,            MOD_NONE, GI_BIG_POE),
        GET_ITEM(ITEM_KEY_SMALL,        OBJECT_GI_KEY,           GID_KEY_SMALL,        0xF3, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_SMALL_KEY,       MOD_NONE, GI_DOOR_KEY),
        GET_ITEM(ITEM_RUPEE_GREEN,      OBJECT_GI_RUPY,          GID_RUPEE_GREEN,      0xF4, 0x00, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_GREEN_LOSE),
        GET_ITEM(ITEM_RUPEE_BLUE,       OBJECT_GI_RUPY,          GID_RUPEE_BLUE,       0xF5, 0x01, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_BLUE_LOSE),
        GET_ITEM(ITEM_RUPEE_RED,        OBJECT_GI_RUPY,          GID_RUPEE_RED,        0xF6, 0x02, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_RED_LOSE),
        GET_ITEM(ITEM_RUPEE_PURPLE,     OBJECT_GI_RUPY,          GID_RUPEE_PURPLE,     0xF7, 0x14, CHEST_ANIM_SHORT, ITEM_CATEGORY_JUNK,            MOD_NONE, GI_RUPEE_PURPLE_LOSE),
        GET_ITEM(ITEM_HEART_PIECE_2,    OBJECT_GI_HEARTS,        GID_HEART_PIECE,      0xFA, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_HEALTH,          MOD_NONE, GI_HEART_PIECE_WIN),
        GET_ITEM(ITEM_STICK_UPGRADE_20, OBJECT_GI_STICK,         GID_STICK,            0x90, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_LESSER,          MOD_NONE, GI_STICK_UPGRADE_20),
        GET_ITEM(ITEM_STICK_UPGRADE_30, OBJECT_GI_STICK,         GID_STICK,            0x91, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_LESSER,          MOD_NONE, GI_STICK_UPGRADE_30),
        GET_ITEM(ITEM_NUT_UPGRADE_30,   OBJECT_GI_NUTS,          GID_NUTS,             0xA7, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_LESSER,          MOD_NONE, GI_NUT_UPGRADE_30),
        GET_ITEM(ITEM_NUT_UPGRADE_40,   OBJECT_GI_NUTS,          GID_NUTS,             0xA8, 0x80, CHEST_ANIM_SHORT, ITEM_CATEGORY_LESSER,          MOD_NONE, GI_NUT_UPGRADE_40),
        GET_ITEM(ITEM_BULLET_BAG_50,    OBJECT_GI_DEKUPOUCH,     GID_BULLET_BAG_50,    0x6C, 0x80, CHEST_ANIM_LONG,  ITEM_CATEGORY_LESSER,          MOD_NONE, GI_BULLET_BAG_50),
        GET_ITEM_NONE,
        GET_ITEM_NONE,
        GET_ITEM_NONE // GI_MAX - if you need to add to this table insert it before this entry.
        // clang-format on
    };
    ItemTableManager::Instance->AddItemTable(MOD_NONE);
    for (uint8_t i = 0; i < ARRAY_COUNT(getItemTable); i++) {
        // The vanilla item table array started with ITEM_BOMBS_5,
        // but the GetItemID enum started with GI_NONE. Then everywhere
        // that table was accessed used `GetItemID - 1`. This allows the
        // "first" item of the new map to start at 1, syncing it up with
        // the GetItemID values and removing the need for the `- 1`
        ItemTableManager::Instance->AddItemEntry(MOD_NONE, i + 1, getItemTable[i]);
    }
}

std::unordered_map<ItemID, GetItemID> ItemIDtoGetItemIDMap{
    { ITEM_ARROWS_LARGE, GI_ARROWS_LARGE },
    { ITEM_ARROWS_MEDIUM, GI_ARROWS_MEDIUM },
    { ITEM_ARROWS_SMALL, GI_ARROWS_SMALL },
    { ITEM_ARROW_FIRE, GI_ARROW_FIRE },
    { ITEM_ARROW_ICE, GI_ARROW_ICE },
    { ITEM_ARROW_LIGHT, GI_ARROW_LIGHT },
    { ITEM_BEAN, GI_BEAN },
    { ITEM_BIG_POE, GI_BIG_POE },
    { ITEM_BLUE_FIRE, GI_BLUE_FIRE },
    { ITEM_BOMB, GI_BOMBS_1 },
    { ITEM_BOMBCHU, GI_BOMBCHUS_10 },
    { ITEM_BOMBCHUS_20, GI_BOMBCHUS_20 },
    { ITEM_BOMBCHUS_5, GI_BOMBCHUS_5 },
    { ITEM_BOMBS_10, GI_BOMBS_10 },
    { ITEM_BOMBS_20, GI_BOMBS_20 },
    { ITEM_BOMBS_30, GI_BOMBS_30 },
    { ITEM_BOMBS_5, GI_BOMBS_5 },
    { ITEM_BOMB_BAG_20, GI_BOMB_BAG_20 },
    { ITEM_BOMB_BAG_30, GI_BOMB_BAG_30 },
    { ITEM_BOMB_BAG_40, GI_BOMB_BAG_40 },
    { ITEM_BOOMERANG, GI_BOOMERANG },
    { ITEM_BOOTS_HOVER, GI_BOOTS_HOVER },
    { ITEM_BOOTS_IRON, GI_BOOTS_IRON },
    { ITEM_BOTTLE, GI_BOTTLE },
    { ITEM_BOW, GI_BOW },
    { ITEM_BRACELET, GI_BRACELET },
    { ITEM_BUG, GI_BUGS },
    { ITEM_BULLET_BAG_30, GI_BULLET_BAG_30 },
    { ITEM_BULLET_BAG_40, GI_BULLET_BAG_40 },
    { ITEM_BULLET_BAG_50, GI_BULLET_BAG_50 },
    { ITEM_CHICKEN, GI_CHICKEN },
    { ITEM_CLAIM_CHECK, GI_CLAIM_CHECK },
    { ITEM_COJIRO, GI_COJIRO },
    { ITEM_COMPASS, GI_COMPASS },
    { ITEM_DINS_FIRE, GI_DINS_FIRE },
    { ITEM_DUNGEON_MAP, GI_MAP },
    { ITEM_EYEDROPS, GI_EYEDROPS },
    { ITEM_FAIRY, GI_FAIRY },
    { ITEM_FARORES_WIND, GI_FARORES_WIND },
    { ITEM_FISH, GI_FISH },
    { ITEM_FROG, GI_FROG },
    { ITEM_GAUNTLETS_GOLD, GI_GAUNTLETS_GOLD },
    { ITEM_GAUNTLETS_SILVER, GI_GAUNTLETS_SILVER },
    { ITEM_GERUDO_CARD, GI_GERUDO_CARD },
    { ITEM_HAMMER, GI_HAMMER },
    { ITEM_HEART, GI_HEART },
    { ITEM_HEART_CONTAINER, GI_HEART_CONTAINER },
    { ITEM_HEART_CONTAINER, GI_HEART_CONTAINER_2 },
    { ITEM_HEART_PIECE_2, GI_HEART_PIECE },
    { ITEM_HEART_PIECE_2, GI_HEART_PIECE_WIN },
    { ITEM_HOOKSHOT, GI_HOOKSHOT },
    { ITEM_KEY_BOSS, GI_KEY_BOSS },
    { ITEM_KEY_SMALL, GI_DOOR_KEY },
    { ITEM_KEY_SMALL, GI_KEY_SMALL },
    { ITEM_LENS, GI_LENS },
    { ITEM_LETTER_RUTO, GI_LETTER_RUTO },
    { ITEM_LETTER_ZELDA, GI_LETTER_ZELDA },
    { ITEM_LONGSHOT, GI_LONGSHOT },
    { ITEM_MAGIC_LARGE, GI_MAGIC_LARGE },
    { ITEM_MAGIC_SMALL, GI_MAGIC_SMALL },
    { ITEM_MASK_BUNNY, GI_MASK_BUNNY },
    { ITEM_MASK_GERUDO, GI_MASK_GERUDO },
    { ITEM_MASK_GORON, GI_MASK_GORON },
    { ITEM_MASK_KEATON, GI_MASK_KEATON },
    { ITEM_MASK_SKULL, GI_MASK_SKULL },
    { ITEM_MASK_SPOOKY, GI_MASK_SPOOKY },
    { ITEM_MASK_TRUTH, GI_MASK_TRUTH },
    { ITEM_MASK_ZORA, GI_MASK_ZORA },
    { ITEM_MILK, GI_MILK },
    { ITEM_MILK_BOTTLE, GI_MILK_BOTTLE },
    { ITEM_NAYRUS_LOVE, GI_NAYRUS_LOVE },
    { ITEM_NUT, GI_NUTS_5 },
    { ITEM_NUTS_10, GI_NUTS_10 },
    { ITEM_NUTS_5, GI_NUTS_5 },
    { ITEM_NUTS_5, GI_NUTS_5_2 },
    { ITEM_NUT_UPGRADE_30, GI_NUT_UPGRADE_30 },
    { ITEM_NUT_UPGRADE_40, GI_NUT_UPGRADE_40 },
    { ITEM_OCARINA_FAIRY, GI_OCARINA_FAIRY },
    { ITEM_OCARINA_TIME, GI_OCARINA_OOT },
    { ITEM_ODD_MUSHROOM, GI_ODD_MUSHROOM },
    { ITEM_ODD_POTION, GI_ODD_POTION },
    { ITEM_POCKET_CUCCO, GI_POCKET_CUCCO },
    { ITEM_POCKET_EGG, GI_POCKET_EGG },
    { ITEM_POE, GI_POE },
    { ITEM_POTION_BLUE, GI_POTION_BLUE },
    { ITEM_POTION_GREEN, GI_POTION_GREEN },
    { ITEM_POTION_RED, GI_POTION_RED },
    { ITEM_PRESCRIPTION, GI_PRESCRIPTION },
    { ITEM_QUIVER_40, GI_QUIVER_40 },
    { ITEM_QUIVER_50, GI_QUIVER_50 },
    { ITEM_RUPEE_BLUE, GI_RUPEE_BLUE },
    { ITEM_RUPEE_BLUE, GI_RUPEE_BLUE_LOSE },
    { ITEM_RUPEE_GOLD, GI_RUPEE_GOLD },
    { ITEM_RUPEE_GREEN, GI_RUPEE_GREEN },
    { ITEM_RUPEE_GREEN, GI_RUPEE_GREEN_LOSE },
    { ITEM_RUPEE_PURPLE, GI_RUPEE_PURPLE },
    { ITEM_RUPEE_PURPLE, GI_RUPEE_PURPLE_LOSE },
    { ITEM_RUPEE_RED, GI_RUPEE_RED },
    { ITEM_RUPEE_RED, GI_RUPEE_RED_LOSE },
    { ITEM_SAW, GI_SAW },
    { ITEM_SCALE_GOLDEN, GI_SCALE_GOLDEN },
    { ITEM_SCALE_SILVER, GI_SCALE_SILVER },
    { ITEM_SEEDS, GI_SEEDS_5 },
    { ITEM_SEEDS_30, GI_SEEDS_30 },
    { ITEM_SHIELD_DEKU, GI_SHIELD_DEKU },
    { ITEM_SHIELD_HYLIAN, GI_SHIELD_HYLIAN },
    { ITEM_SHIELD_MIRROR, GI_SHIELD_MIRROR },
    { ITEM_SKULL_TOKEN, GI_SKULL_TOKEN },
    { ITEM_SLINGSHOT, GI_SLINGSHOT },
    { ITEM_STICK, GI_STICKS_1 },
    { ITEM_STICKS_10, GI_STICKS_10 },
    { ITEM_STICKS_5, GI_STICKS_5 },
    { ITEM_STICK_UPGRADE_20, GI_STICK_UPGRADE_20 },
    { ITEM_STICK_UPGRADE_30, GI_STICK_UPGRADE_30 },
    { ITEM_STONE_OF_AGONY, GI_STONE_OF_AGONY },
    { ITEM_SWORD_BGS, GI_SWORD_BGS },
    { ITEM_SWORD_BGS, GI_SWORD_KNIFE },
    { ITEM_SWORD_BROKEN, GI_SWORD_BROKEN },
    { ITEM_SWORD_KOKIRI, GI_SWORD_KOKIRI },
    { ITEM_TUNIC_GORON, GI_TUNIC_GORON },
    { ITEM_TUNIC_ZORA, GI_TUNIC_ZORA },
    { ITEM_WALLET_ADULT, GI_WALLET_ADULT },
    { ITEM_WALLET_GIANT, GI_WALLET_GIANT },
    { ITEM_WEIRD_EGG, GI_WEIRD_EGG },
};

extern "C" GetItemID RetrieveGetItemIDFromItemID(ItemID itemID) {
    if (ItemIDtoGetItemIDMap.contains(itemID)) {
        return ItemIDtoGetItemIDMap.at(itemID);
    }
    return GI_MAX;
}

std::unordered_map<ItemID, RandomizerGet> ItemIDtoRandomizerGetMap{
    { ITEM_SONG_MINUET, RG_MINUET_OF_FOREST },
    { ITEM_SONG_BOLERO, RG_BOLERO_OF_FIRE },
    { ITEM_SONG_SERENADE, RG_SERENADE_OF_WATER },
    { ITEM_SONG_REQUIEM, RG_REQUIEM_OF_SPIRIT },
    { ITEM_SONG_NOCTURNE, RG_NOCTURNE_OF_SHADOW },
    { ITEM_SONG_PRELUDE, RG_PRELUDE_OF_LIGHT },
    { ITEM_SONG_LULLABY, RG_ZELDAS_LULLABY },
    { ITEM_SONG_EPONA, RG_EPONAS_SONG },
    { ITEM_SONG_SARIA, RG_SARIAS_SONG },
    { ITEM_SONG_SUN, RG_SUNS_SONG },
    { ITEM_SONG_TIME, RG_SONG_OF_TIME },
    { ITEM_SONG_STORMS, RG_SONG_OF_STORMS },
    { ITEM_MEDALLION_FOREST, RG_FOREST_MEDALLION },
    { ITEM_MEDALLION_FIRE, RG_FIRE_MEDALLION },
    { ITEM_MEDALLION_WATER, RG_WATER_MEDALLION },
    { ITEM_MEDALLION_SPIRIT, RG_SPIRIT_MEDALLION },
    { ITEM_MEDALLION_SHADOW, RG_SHADOW_MEDALLION },
    { ITEM_MEDALLION_LIGHT, RG_LIGHT_MEDALLION },
    { ITEM_KOKIRI_EMERALD, RG_KOKIRI_EMERALD },
    { ITEM_GORON_RUBY, RG_GORON_RUBY },
    { ITEM_ZORA_SAPPHIRE, RG_ZORA_SAPPHIRE },
    { ITEM_SWORD_MASTER, RG_MASTER_SWORD },
};

extern "C" RandomizerGet RetrieveRandomizerGetFromItemID(ItemID itemID) {
    if (ItemIDtoRandomizerGetMap.contains(itemID)) {
        return ItemIDtoRandomizerGetMap.at(itemID);
    }
    return RG_MAX;
}

extern "C" void OTRExtScanner() {
    // SoH-3DS: bind by reference. ListFiles() already materialises all ~39k
    // archive paths into a fresh vector<string>; dereferencing into `auto lst`
    // copied every one of them again, for roughly 2.5 MB of pure duplicate on a
    // heap that has ~13 MB left at this point. The shared_ptr keeps it alive for
    // the loop below.
    auto lstPtr = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->ListFiles();
    const auto& lst = *lstPtr;
#ifdef __3DS__
    {
        struct mallinfo mi = mallinfo();
        fprintf(stderr, "soh-3ds ext: %u entries, used=%uKiB free=%uKiB\n", (unsigned)lst.size(),
                (unsigned)(mi.uordblks / 1024), (unsigned)(mi.fordblks / 1024));
        fflush(stderr);
    }
#endif

    ExtensionCache.reserve(lst.size());
    for (const auto& rPath : lst) {
        // SoH-3DS: only the final extension is wanted, so take it directly
        // instead of splitting the whole path into a vector<string> per entry
        // (39k transient vectors, each with its own allocations).
        const auto dot = rPath.find_last_of('.');
        if (dot == std::string::npos) {
            continue;
        }
        const std::string ext = rPath.substr(dot + 1);
        std::string nPath = rPath.substr(0, dot);
        replace(nPath.begin(), nPath.end(), '\\', '/');

        ExtensionCache[nPath] = { rPath, ext };
    }
#ifdef __3DS__
    {
        struct mallinfo mi = mallinfo();
        fprintf(stderr, "soh-3ds ext: cache built (%u), used=%uKiB free=%uKiB\n", (unsigned)ExtensionCache.size(),
                (unsigned)(mi.uordblks / 1024), (unsigned)(mi.fordblks / 1024));
        fflush(stderr);
    }
#endif
}

// Read the port version from an OTR file
OTRVersion ReadPortVersionFromOTR(std::string otrPath) {
    OTRVersion version = {};

    // Use a temporary archive instance to load the otr and read the version file
    auto archive = std::make_shared<Ship::O2rArchive>(otrPath);
    if (archive->Open()) {
        auto t = archive->LoadFile("portVersion");
        if (t != nullptr && t->IsLoaded) {
            auto stream = std::make_shared<Ship::MemoryStream>(t->Buffer->data(), t->Buffer->size());
            auto reader = std::make_shared<Ship::BinaryReader>(stream);
            Ship::Endianness endianness = (Ship::Endianness)reader->ReadUByte();
            reader->SetEndianness(endianness);
            version.major = reader->ReadUInt16();
            version.minor = reader->ReadUInt16();
            version.patch = reader->ReadUInt16();
        }
    }

    return version;
}

// Checks the program version stored in the otr and compares the major value to soh
// For Windows/Mac/Linux if the version doesn't match, offer to
OTRVersion DetectOTRVersion(std::string fileName, bool isMQ) {
    bool isOtrOld = false;
    std::string otrPath = Ship::Context::LocateFileAcrossAppDirs(fileName, appShortName);

    // Doesn't exist so nothing to do here
    if (!std::filesystem::exists(otrPath)) {
        return { INT16_MAX, INT16_MAX, INT16_MAX };
    }

    return ReadPortVersionFromOTR(otrPath);
}

extern "C" void Messagebox_ShowErrorBox(char* title, char* body) {
    Extractor::ShowErrorBox(title, body);
}

bool VerifyArchiveVersion(OTRVersion version) {
    return version.major != INT16_MAX && version.major != gBuildVersionMajor;
}

extern "C" void InitOTR(int argc, char* argv[]) {
    SOH3DS_INIT_TRACE("new OTRGlobals");
    OTRGlobals::Instance = new OTRGlobals();
    SOH3DS_INIT_TRACE("RunExtract");
    OTRGlobals::Instance->RunExtract(argc, argv);

    SOH3DS_INIT_TRACE("OTRGlobals::Initialize");
    OTRGlobals::Instance->Initialize();
    SOH3DS_INIT_TRACE("Initialize done");
    SOH3DS_INIT_TRACE("InitOTR: CustomMessageManager");
    CustomMessageManager::Instance = new CustomMessageManager();
    SOH3DS_INIT_TRACE("InitOTR: ItemTableManager");
    ItemTableManager::Instance = new ItemTableManager();
    SOH3DS_INIT_TRACE("InitOTR: GameInteractor");
    GameInteractor::Instance = new GameInteractor();
    SOH3DS_INIT_TRACE("InitOTR: SaveManager");
    SaveManager::Instance = new SaveManager();

    SOH3DS_INIT_TRACE("InitOTR: GetConfig");
    std::shared_ptr<Ship::Config> conf = OTRGlobals::Instance->context->GetConfig();
    conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion1Updater>());
    conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion2Updater>());
    conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion3Updater>());
    conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion4Updater>());
    conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion5Updater>());
    conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion6Updater>());
    conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion7Updater>());
    SOH3DS_INIT_TRACE("InitOTR: RunVersionUpdates");
    conf->RunVersionUpdates();

    SOH3DS_INIT_TRACE("SetupGuiElements");
    SohGui::SetupGuiElements();
#ifdef __3DS__
    // SoH-3DS: AddMenuElements builds the full port-menu widget tree - the
    // single largest InitOTR cost (6.4 s of a 21 s boot, measured) - for an
    // ImGui menu that never draws on this port. The SohMenu object itself is
    // still created (SetupMenu) since UpdateAudioBackendObjects needs it.
    // Audited before gating: AddMenuElements additionally runs every
    // MenuInit::GetInitFuncs() callback; all ten registrants
    // (ResolutionEditor, Anchor menu, four rando trackers, mod_menu,
    // CosmeticsEditor, Mapper, SohInputEditorWindow) are Register*Widgets
    // functions - widget construction only. Functional game/randomizer/network
    // init uses the separate RegisterShipInitFunc registry, which is unaffected.
    SOH3DS_INIT_TRACE("SetupMenuElements skipped (no ImGui)");
#else
    SohGui::SetupMenuElements();
#endif

    SOH3DS_INIT_TRACE("AudioCollection");
    AudioCollection::Instance = new AudioCollection();
    ActorDB::Instance = new ActorDB();
#ifdef __APPLE__
    SpeechSynthesizer::Instance = new DarwinSpeechSynthesizer();
#elif defined(_WIN32)
    SpeechSynthesizer::Instance = new SAPISpeechSynthesizer();
#elif ESPEAK
    SpeechSynthesizer::Instance = new ESpeakSpeechSynthesizer();
#else
    SpeechSynthesizer::Instance = new SpeechLogger();
#endif
    SOH3DS_INIT_TRACE("SpeechSynthesizer::Init");
    SpeechSynthesizer::Instance->Init();

    CrowdControl::Instance = new CrowdControl();
    Sail::Instance = new Sail();
    Anchor::Instance = new Anchor();

    SOH3DS_INIT_TRACE("OTRMessage_Init");
    OTRMessage_Init();
    SOH3DS_INIT_TRACE("OTRAudio_Init");
    OTRAudio_Init();
    SOH3DS_INIT_TRACE("OTRExtScanner");
    OTRExtScanner();
    SOH3DS_INIT_TRACE("VanillaItemTable_Init");
    VanillaItemTable_Init();
    SOH3DS_INIT_TRACE("DebugConsole_Init");
    DebugConsole_Init();

    // #region SOH [Randomizer] TODO: Remove these and refactor spoiler file handling for randomizer
    CVarClear(CVAR_GENERAL("RandomizerNewFileDropped"));
    CVarClear(CVAR_GENERAL("RandomizerDroppedFile"));
    // #endregion

    Ship::Context::GetRawInstance()->GetFileDropMgr()->RegisterDropHandler(SoH_HandleConfigDrop);

    SOH3DS_INIT_TRACE("RegisterImGuiItemIcons");
    RegisterImGuiItemIcons();

    time_t now = time(NULL);
    tm* tm_now = localtime(&now);
    if (tm_now->tm_mon == 11 && tm_now->tm_mday >= 24 && tm_now->tm_mday <= 25) {
        CVarRegisterInteger(CVAR_GENERAL("LetItSnow"), 1);
    } else {
        CVarClear(CVAR_GENERAL("LetItSnow"));
    }

    srand(static_cast<unsigned int>(now));
    SOH3DS_INIT_TRACE("SDLNet_Init");
    SDLNet_Init();
    if (CVarGetInteger(CVAR_REMOTE_CROWD_CONTROL("Enabled"), 0)) {
        CrowdControl::Instance->Enable();
    }
    if (CVarGetInteger(CVAR_REMOTE_SAIL("Enabled"), 0)) {
        Sail::Instance->Enable();
    }
    if (CVarGetInteger(CVAR_REMOTE_ANCHOR("Enabled"), 0)) {
        Anchor::Instance->Enable();
    }
    ShipInit::InitAll();
    Rando::StaticData::InitHashMaps();
    OTRGlobals::Instance->gRandoContext->AddExcludedOptions();
}

extern "C" void SaveManager_ThreadPoolWait() {
    SaveManager::Instance->ThreadPoolWait();
}

extern "C" void DeinitOTR() {
    SaveManager_ThreadPoolWait();
    OTRAudio_Exit();
    if (CVarGetInteger(CVAR_REMOTE_CROWD_CONTROL("Enabled"), 0)) {
        CrowdControl::Instance->Disable();
    }
    if (CVarGetInteger(CVAR_REMOTE_SAIL("Enabled"), 0)) {
        Sail::Instance->Disable();
    }
    if (CVarGetInteger(CVAR_REMOTE_ANCHOR("Enabled"), 0)) {
        Anchor::Instance->Disable();
    }
    SDLNet_Quit();

    // Destroying gui here because we have shared ptrs to LUS objects which output to SPDLOG which is destroyed before
    // these shared ptrs.
    SohGui::Destroy();
    sohFast3dWindow = nullptr;

    OTRGlobals::Instance->context = nullptr;
}

#ifdef _WIN32
extern "C" uint64_t GetFrequency() {
    LARGE_INTEGER nFreq;

    QueryPerformanceFrequency(&nFreq);

    return nFreq.QuadPart;
}

extern "C" uint64_t GetPerfCounter() {
    LARGE_INTEGER ticks;
    QueryPerformanceCounter(&ticks);

    return ticks.QuadPart;
}
#else
extern "C" uint64_t GetFrequency() {
    return 1000; // sec -> ms
}

extern "C" uint64_t GetPerfCounter() {
    struct timespec monotime;
    clock_gettime(CLOCK_MONOTONIC, &monotime);

    uint64_t remainingMs = (monotime.tv_nsec / 1000000);

    // in milliseconds
    return monotime.tv_sec * 1000 + remainingMs;
}
#endif

extern "C" uint64_t GetUnixTimestamp() {
    auto time = std::chrono::system_clock::now();
    auto since_epoch = time.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch);
    return (uint64_t)millis.count();
}

extern "C" void Graph_StartFrame() {
    auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
#ifndef __WIIU__
    using Ship::KbScancode;
    int32_t dwScancode = OTRGlobals::Instance->context->GetWindow()->GetLastScancode();
    OTRGlobals::Instance->context->GetWindow()->SetLastScancode(-1);

    switch (dwScancode) {
        case KbScancode::LUS_KB_F1: {
            std::shared_ptr<SohModalWindow> modal =
                static_pointer_cast<SohModalWindow>(gui->GetGuiWindow("Modal Window"));
            if (modal->IsPopupOpen("Menu Moved")) {
                modal->DismissPopup();
            } else {
                modal->RegisterPopup("Menu Moved",
                                     "The menubar, accessed by hitting F1, no longer exists.\nThe new menu can be "
                                     "accessed by hitting the Esc button instead.",
                                     "OK");
            }
            break;
        }
        case KbScancode::LUS_KB_F5: {
            if (CVarGetInteger(CVAR_CHEAT("SaveStatesEnabled"), 0) == 0) {
                gui->GetGameOverlay()->TextDrawNotification(6.0f, true, "Save states not enabled. Check Cheats Menu.");
                return;
            }
            const unsigned int slot = OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot();
            const SaveStateReturn stateReturn =
                OTRGlobals::Instance->gSaveStateMgr->AddRequest({ slot, RequestType::SAVE });

            switch (stateReturn) {
                case SaveStateReturn::SUCCESS:
                    SPDLOG_INFO("[SOH] Saved state to slot {}", slot);
                    break;
                case SaveStateReturn::FAIL_WRONG_GAMESTATE:
                    SPDLOG_ERROR("[SOH] Can not save a state outside of \"GamePlay\"");
                    break;
                    [[unlikely]] default : break;
            }
            break;
        }
        case KbScancode::LUS_KB_F6: {
            if (CVarGetInteger(CVAR_CHEAT("SaveStatesEnabled"), 0) == 0) {
                gui->GetGameOverlay()->TextDrawNotification(6.0f, true, "Save states not enabled. Check Cheats Menu.");
                return;
            }
            unsigned int slot = OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot();
            slot++;
            if (slot > 5) {
                slot = 0;
            }
            OTRGlobals::Instance->gSaveStateMgr->SetCurrentSlot(slot);
            SPDLOG_INFO("Set SaveState slot to {}.", slot);
            break;
        }
        case KbScancode::LUS_KB_F7: {
            if (CVarGetInteger(CVAR_CHEAT("SaveStatesEnabled"), 0) == 0) {
                gui->GetGameOverlay()->TextDrawNotification(6.0f, true, "Save states not enabled. Check Cheats Menu.");
                return;
            }
            const unsigned int slot = OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot();
            const SaveStateReturn stateReturn =
                OTRGlobals::Instance->gSaveStateMgr->AddRequest({ slot, RequestType::LOAD });

            switch (stateReturn) {
                case SaveStateReturn::SUCCESS:
                    SPDLOG_INFO("[SOH] Loaded state from slot {}", slot);
                    break;
                case SaveStateReturn::FAIL_INVALID_SLOT:
                    SPDLOG_ERROR("[SOH] Invalid State Slot Number {}", slot);
                    break;
                case SaveStateReturn::FAIL_STATE_EMPTY:
                    SPDLOG_ERROR("[SOH] State Slot {} is empty", slot);
                    break;
                case SaveStateReturn::FAIL_WRONG_GAMESTATE:
                    SPDLOG_ERROR("[SOH] Can not load a state outside of \"GamePlay\"");
                    break;
                    [[unlikely]] default : break;
            }

            break;
        }
#if defined(_WIN32) || defined(__APPLE__)
        case KbScancode::LUS_KB_F9: {
            // Toggle TTS
            CVarSetInteger(CVAR_SETTING("A11yTTS"), !CVarGetInteger(CVAR_SETTING("A11yTTS"), 0));
            break;
        }
#endif
        case KbScancode::LUS_KB_TAB: {
            if (CVarGetInteger(CVAR_SETTING("Mods.AlternateAssetsHotkey"), 1)) {
                CVarSetInteger(CVAR_SETTING("AltAssets"), !CVarGetInteger(CVAR_SETTING("AltAssets"), 1));
            }
            break;
        }
    }
#endif
}

// Interpolated frames of a tick are evenly spaced numerators time+step, time+2*step, ... over denom.
void RunCommands(Gfx* Commands, int time, int step, int denom, int count) {
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(OTRGlobals::Instance->context->GetWindow());

    if (wnd == nullptr) {
        return;
    }

    // Process window events for resize, mouse, keyboard events
    wnd->HandleEvents();

    auto intp = wnd->GetInterpreterWeak().lock().get();
    intp->mInterpolationIndex = 0;

    UIWidgets::Colors themeColor =
        static_cast<UIWidgets::Colors>(CVarGetInteger(CVAR_SETTING("Menu.Theme"), UIWidgets::Colors::LightBlue));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, UIWidgets::ColorValues.at(themeColor));
    for (int i = 0; i < count; i++) {
        time += step;
#ifdef __3DS__
        // SoH-3DS: the renderer paces frames on an absolute schedule; while it
        // is behind, draw only the last frame of this tick so the game keeps
        // its 20 Hz instead of paying a second render it cannot afford. The
        // last frame is never skipped - a tick must present something and
        // the intermediate ones are the only thing interpolation adds.
        if (i + 1 < count && Soh3dsFrameBehind()) {
            Soh3dsFrameDropped();
            intp->mInterpolationIndex++;
            continue;
        }
#endif
        std::unordered_map<Mtx*, MtxF> mtx_replacements =
            (time == denom) ? std::unordered_map<Mtx*, MtxF>() : FrameInterpolation_Interpolate((float)time / denom);
        intp->mInterpolationT = (float)time / denom;
        wnd->DrawAndRunGraphicsCommands(Commands, mtx_replacements);
        intp->mInterpolationIndex++;
    }
    ImGui::PopStyleColor();
}

// C->C++ Bridge
#ifdef __3DS__
// SoH-3DS: game ticks, read by the renderer heartbeat as tps - the number
// that says whether the game runs at speed (20 in play, 60 in transitions),
// which presented fps cannot show once interpolated frames are being dropped.
static uint32_t sGameTicks = 0;
extern "C" uint32_t Soh3dsGameTicks(void) {
    return sGameTicks;
}
#endif

extern "C" void Graph_ProcessGfxCommands(Gfx* commands) {
#ifdef __3DS__
    ++sGameTicks;
#endif
    {
        std::unique_lock<std::mutex> Lock(audio.mutex);
        audio.processing = true;
    }

    audio.cv_to_thread.notify_one();
    int target_fps = OTRGlobals::Instance->GetInterpolationFPS();
    static int last_fps;
    static int last_update_rate;
    static int time;
    int fps = target_fps;
    int original_fps = 60 / R_UPDATE_RATE;
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());

    if (target_fps == 20 || original_fps > target_fps) {
        fps = original_fps;
    }

    if (last_fps != fps || last_update_rate != R_UPDATE_RATE) {
        time = 0;
    }

    // time_base = fps * original_fps (one second)
    int next_original_frame = fps;

    int start_time = time;
    int count = 0;
    while (time + original_fps <= next_original_frame) {
        time += original_fps;
        count++;
    }

    time -= fps;

    if (wnd != nullptr) {
        wnd->SetTargetFps(fps);
    }

    int step = original_fps;
    // When the gfx debugger is active, only run with the final mtx
    if (GfxDebuggerIsDebugging()) {
        start_time = next_original_frame;
        step = 0;
        count = 1;
    }

    RunCommands(commands, start_time, step, next_original_frame, count);

    last_fps = fps;
    last_update_rate = R_UPDATE_RATE;

    bool curAltAssets = CVarGetInteger(CVAR_SETTING("AltAssets"), 1);
    if (prevAltAssets != curAltAssets) {
        prevAltAssets = curAltAssets;
        Ship::Context::GetRawInstance()->GetResourceManager()->SetAltAssetsEnabled(curAltAssets);
        gfx_texture_cache_clear();
        SOH::SkeletonPatcher::UpdateSkeletons();
        GameInteractor::Instance->ExecuteHooks<GameInteractor::OnAssetAltChange>();
    }

    // OTRTODO: FIGURE OUT END FRAME POINT
    /* if (OTRGlobals::Instance->context->lastScancode != -1)
         OTRGlobals::Instance->context->lastScancode = -1;*/
}

extern "C" void OTRGetPixelDepthPrepare(float x, float y) {
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    if (wnd == nullptr) {
        return;
    }

    wnd->GetPixelDepthPrepare(x, y);
}

extern "C" uint16_t OTRGetPixelDepth(float x, float y) {
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    if (wnd == nullptr) {
        return 0;
    }

    return wnd->GetPixelDepth(x, y);
}

extern "C" Sprite* GetSeedTexture(uint8_t index) {
    return OTRGlobals::Instance->gRandoContext->GetSeedTexture(index);
}

extern "C" uint8_t GetSeedIconIndex(uint8_t index) {
    return OTRGlobals::Instance->gRandoContext->hashIconIndexes[index];
}

std::map<std::string, SoundFontSample*> cachedCustomSFs;

ImFont* OTRGlobals::CreateFontWithSize(float size, std::string fontPath, bool isJapaneseFont) {
    auto mImGuiIo = &ImGui::GetIO();
    ImFont* font;
    if (fontPath == "") {
        ImFontConfig fontCfg = ImFontConfig();
        fontCfg.OversampleH = fontCfg.OversampleV = 1;
        fontCfg.PixelSnapH = true;
        fontCfg.SizePixels = size;
        font = mImGuiIo->Fonts->AddFontDefault(&fontCfg);
    } else {
        auto initData = std::make_shared<Ship::ResourceInitData>();
        initData->Format = RESOURCE_FORMAT_BINARY;
        initData->Type = static_cast<uint32_t>(RESOURCE_TYPE_FONT);
        initData->ResourceVersion = 0;
        initData->Path = fontPath;
        std::shared_ptr<Ship::Font> fontData = std::static_pointer_cast<Ship::Font>(
            Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(fontPath, false, initData));
        ImFontConfig fontConf;
        fontConf.FontDataOwnedByAtlas = false;
        const ImWchar* glyph_ranges = isJapaneseFont ? mImGuiIo->Fonts->GetGlyphRangesJapanese() : nullptr;
#ifdef __3DS__
        // SoH-3DS: the top screen is 400x240, so nothing above ~14px is legible
        // anyway, and the atlas has to fit in a 36 MB app heap next to the game.
        //
        // Three costs are cut here:
        //  - ImGui defaults to 3x3 oversampling, which rasterises 9x the area.
        //  - The Japanese font merges GetGlyphRangesJapanese() (~3000 CJK glyphs);
        //    at 24px that alone is a multi-megabyte atlas and minutes of ARM11
        //    rasterisation. Latin-only keeps the ImFont* valid for every caller.
        //  - Sizes are clamped so the 20px and 24px variants stop duplicating
        //    large atlases the screen cannot show.
        fontConf.OversampleH = fontConf.OversampleV = 1;
        fontConf.PixelSnapH = true;
        glyph_ranges = nullptr;
        size = std::min(size, 14.0f);
#endif
        font = mImGuiIo->Fonts->AddFontFromMemoryTTF(fontData->Data, static_cast<int>(fontData->DataSize), size,
                                                     &fontConf, glyph_ranges);
    }
    // FontAwesome fonts need to have their sizes reduced by 2.0f/3.0f in order to align correctly
    float iconFontSize = size * 2.0f / 3.0f;
    static const ImWchar sIconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    ImFontConfig iconsConfig;
    iconsConfig.MergeMode = true;
    iconsConfig.PixelSnapH = true;
    iconsConfig.GlyphMinAdvanceX = iconFontSize;
    mImGuiIo->Fonts->AddFontFromMemoryCompressedBase85TTF(fontawesome_compressed_data_base85, iconFontSize,
                                                          &iconsConfig, sIconsRanges);
    return font;
}

std::filesystem::path GetSaveFile(std::shared_ptr<Ship::Config> Conf) {
    const std::string fileName =
        Conf->GetString("Game.SaveName", Ship::Context::GetPathRelativeToAppDirectory("oot_save.sav"));
    std::filesystem::path saveFile = std::filesystem::absolute(fileName);

    if (!exists(saveFile.parent_path())) {
        create_directories(saveFile.parent_path());
    }

    return saveFile;
}

std::filesystem::path GetSaveFile() {
    const std::shared_ptr<Ship::Config> pConf = OTRGlobals::Instance->context->GetConfig();

    return GetSaveFile(pConf);
}

extern "C" void Ctx_ReadSaveFile(uintptr_t addr, void* dramAddr, size_t size) {
    SaveManager::ReadSaveFile(GetSaveFile(), addr, dramAddr, size);
}

extern "C" void Ctx_WriteSaveFile(uintptr_t addr, void* dramAddr, size_t size) {
    SaveManager::WriteSaveFile(GetSaveFile(), addr, dramAddr, size);
}

std::wstring StringToU16(const std::string& s) {
    std::vector<unsigned long> result;
    size_t i = 0;

    while (i < s.size()) {
        unsigned long uni = '\1'; // skipped unless a valid encoding is matched below
        size_t nbytes = 0;
        bool error = false;
        unsigned char c = s[i++];
        if (c < 0x80) { // ascii
            uni = c;
            nbytes = 0;
        } else if (c == GFXP_HIRAGANA_CHAR) { // Start Hiragana Mode
            uni = c;
            nbytes = 0;
        } else if (c == GFXP_KATAKANA_CHAR) { // Start Katakana Mode
            uni = c;
            nbytes = 0;
        } else if (c <= 0xBF) { // Invalid Characters (Skipped)
            nbytes = 0;
            uni = '\1';
        } else if (c <= 0xDF) {
            uni = c & 0x1F;
            nbytes = 1;
        } else if (c <= 0xEF) {
            uni = c & 0x0F;
            nbytes = 2;
        } else if (c <= 0xF7) {
            uni = c & 0x07;
            nbytes = 3;
        }
        for (size_t j = 0; j < nbytes; ++j) {
            unsigned char c = s[i++];
            uni <<= 6;
            uni += c & 0x3F;
        }
        if (uni != '\1')
            result.push_back(uni);
    }
    std::wstring utf16;
    for (size_t i = 0; i < result.size(); ++i) {
        unsigned long uni = result[i];
        if (uni <= 0xFFFF) {
            utf16 += (wchar_t)uni;
        } else {
            uni -= 0x10000;
            utf16 += (wchar_t)((uni >> 10) + 0xD800);
            utf16 += (wchar_t)((uni & 0x3FF) + 0xDC00);
        }
    }
    return utf16;
}

extern "C" void OTRGfxPrint(const char* str, void* printer, void (*printImpl)(void*, char)) {
    const std::vector<uint32_t> hira1 = {
        u'を', u'ぁ', u'ぃ', u'ぅ', u'ぇ', u'ぉ', u'ゃ', u'ゅ', u'ょ', u'っ', u'-',  u'あ', u'い',
        u'う', u'え', u'お', u'か', u'き', u'く', u'け', u'こ', u'さ', u'し', u'す', u'せ', u'そ',
    };

    const std::vector<uint32_t> hira2 = {
        u'た', u'ち', u'つ', u'て', u'と', u'な', u'に', u'ぬ', u'ね', u'の', u'は', u'ひ', u'ふ', u'へ', u'ほ', u'ま',
        u'み', u'む', u'め', u'も', u'や', u'ゆ', u'よ', u'ら', u'り', u'る', u'れ', u'ろ', u'わ', u'ん', u'゛', u'゜',
    };

    const std::vector<uint32_t> kata1 = {
        u'ヲ', u'ァ', u'ィ', u'ゥ', u'ェ', u'ォ', u'ャ', u'ュ', u'ョ', u'ッ', u'ー',
    };

    const std::vector<uint32_t> kata2 = {
        u'ア', u'イ', u'ウ', u'エ', u'オ', u'カ', u'キ', u'ク', u'ケ', u'コ', u'サ', u'シ', u'ス', u'セ', u'ソ',
        u'タ', u'チ', u'ツ', u'テ', u'ト', u'ナ', u'ニ', u'ヌ', u'ネ', u'ノ', u'ハ', u'ヒ', u'フ', u'ヘ', u'ホ',
        u'マ', u'ミ', u'ム', u'メ', u'モ', u'ヤ', u'ユ', u'ヨ', u'ラ', u'リ', u'ル', u'レ', u'ロ', u'ワ', u'ン',
    };

    std::wstring wstr = StringToU16(str);
    bool hiraganaMode = false;

    for (const auto& c : wstr) {
        if (c < 0x80) {
            printImpl(printer, static_cast<char>(c));
        } else if (c == GFXP_HIRAGANA_CHAR) {
            hiraganaMode = true;
        } else if (c == GFXP_KATAKANA_CHAR) {
            hiraganaMode = false;
        } else if (c >= u'｡' && c <= u'ﾟ') { // katakana (hankaku)
            if (hiraganaMode && c >= u'ｦ' && c <= u'ｿ') {
                printImpl(printer, c - 0xFEC0 - 0x20); // Hiragana Mode, Block 1
            } else if (hiraganaMode && c >= u'ﾀ' && c <= u'ﾝ') {
                printImpl(printer, c - 0xFEC0 + 0x20); // Hiragana Mode, Block 2
            } else {
                printImpl(printer, c - 0xFEC0);
            }
        } else if (c == u'　') { // zenkaku space
            printImpl(printer, u' ');
        } else {
            auto it = std::find(hira1.begin(), hira1.end(), c);
            if (it != hira1.end()) { // hiragana block 1
                printImpl(printer, static_cast<char>(0x86 + std::distance(hira1.begin(), it)));
            }

            auto it2 = std::find(hira2.begin(), hira2.end(), c);
            if (it2 != hira2.end()) { // hiragana block 2
                printImpl(printer, static_cast<char>(0xe0 + std::distance(hira2.begin(), it2)));
            }

            auto it3 = std::find(kata1.begin(), kata1.end(), c);
            if (it3 != kata1.end()) { // katakana zenkaku block 1
                printImpl(printer, static_cast<char>(0xa6 + std::distance(kata1.begin(), it3)));
            }

            auto it4 = std::find(kata2.begin(), kata2.end(), c);
            if (it4 != kata2.end()) { // katakana zenkaku block 2
                printImpl(printer, static_cast<char>(0xb1 + std::distance(kata2.begin(), it4)));
            }
        }
    }
}

Color_RGB8 GetColorForControllerLED() {
    auto brightness = CVarGetFloat(CVAR_SETTING("LEDBrightness"), 1.0f) / 1.0f;
    Color_RGB8 color = { 0, 0, 0 };
    if (brightness > 0.0f) {
        LEDColorSource source =
            static_cast<LEDColorSource>(CVarGetInteger(CVAR_SETTING("LEDColorSource"), LED_SOURCE_TUNIC_ORIGINAL));
        bool criticalOverride = CVarGetInteger(CVAR_SETTING("LEDCriticalOverride"), 1);
        if (gPlayState && (source == LED_SOURCE_TUNIC_ORIGINAL || source == LED_SOURCE_TUNIC_COSMETICS)) {
            switch (CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC)) {
                case EQUIP_VALUE_TUNIC_KOKIRI:
                    color = source == LED_SOURCE_TUNIC_COSMETICS
                                ? CVarGetColor24(CVAR_COSMETIC("Link.KokiriTunic.Value"), kokiriColor)
                                : kokiriColor;
                    break;
                case EQUIP_VALUE_TUNIC_GORON:
                    color = source == LED_SOURCE_TUNIC_COSMETICS
                                ? CVarGetColor24(CVAR_COSMETIC("Link.GoronTunic.Value"), goronColor)
                                : goronColor;
                    break;
                case EQUIP_VALUE_TUNIC_ZORA:
                    color = source == LED_SOURCE_TUNIC_COSMETICS
                                ? CVarGetColor24(CVAR_COSMETIC("Link.ZoraTunic.Value"), zoraColor)
                                : zoraColor;
                    break;
            }
        }
        if (gPlayState && (source == LED_SOURCE_NAVI_ORIGINAL || source == LED_SOURCE_NAVI_COSMETICS)) {
            Actor* arrowPointedActor = gPlayState->actorCtx.targetCtx.arrowPointedActor;
            if (arrowPointedActor) {
                ActorCategory category = (ActorCategory)arrowPointedActor->category;
                switch (category) {
                    case ACTORCAT_PLAYER:
                        if (source == LED_SOURCE_NAVI_COSMETICS &&
                            CVarGetInteger(CVAR_COSMETIC("Navi.IdlePrimary.Changed"), 0)) {
                            color = CVarGetColor24(CVAR_COSMETIC("Navi.IdlePrimary.Value"), defaultIdleColor.inner);
                            break;
                        }
                        color = LEDColorDefaultNaviColorList[category].inner;
                        break;
                    case ACTORCAT_NPC:
                        if (source == LED_SOURCE_NAVI_COSMETICS &&
                            CVarGetInteger(CVAR_COSMETIC("Navi.NPCPrimary.Changed"), 0)) {
                            color = CVarGetColor24(CVAR_COSMETIC("Navi.NPCPrimary.Value"), defaultNPCColor.inner);
                            break;
                        }
                        color = LEDColorDefaultNaviColorList[category].inner;
                        break;
                    case ACTORCAT_ENEMY:
                    case ACTORCAT_BOSS:
                        if (source == LED_SOURCE_NAVI_COSMETICS &&
                            CVarGetInteger(CVAR_COSMETIC("Navi.EnemyPrimary.Changed"), 0)) {
                            color = CVarGetColor24(CVAR_COSMETIC("Navi.EnemyPrimary.Value"), defaultEnemyColor.inner);
                            break;
                        }
                        color = LEDColorDefaultNaviColorList[category].inner;
                        break;
                    default:
                        if (source == LED_SOURCE_NAVI_COSMETICS &&
                            CVarGetInteger(CVAR_COSMETIC("Navi.PropsPrimary.Changed"), 0)) {
                            color = CVarGetColor24(CVAR_COSMETIC("Navi.PropsPrimary.Value"), defaultPropsColor.inner);
                            break;
                        }
                        color = LEDColorDefaultNaviColorList[category].inner;
                }
            } else { // No target actor.
                if (source == LED_SOURCE_NAVI_COSMETICS &&
                    CVarGetInteger(CVAR_COSMETIC("Navi.IdlePrimary.Changed"), 0)) {
                    color = CVarGetColor24(CVAR_COSMETIC("Navi.IdlePrimary.Value"), defaultIdleColor.inner);
                } else {
                    color = LEDColorDefaultNaviColorList[ACTORCAT_PLAYER].inner;
                }
            }
        }
        if (source == LED_SOURCE_CUSTOM) {
            color = CVarGetColor24(CVAR_SETTING("LEDPort1Color"), { 255, 255, 255 });
        }
        if (gPlayState && (criticalOverride || source == LED_SOURCE_HEALTH)) {
            if (HealthMeter_IsCritical()) {
                color = { 0xFF, 0, 0 };
            } else if (gSaveContext.healthCapacity != 0 && source == LED_SOURCE_HEALTH) {
                if (gSaveContext.health / (float)gSaveContext.healthCapacity <= 0.4f) {
                    color = { 0xFF, 0xFF, 0 };
                } else {
                    color = { 0, 0xFF, 0 };
                }
            }
        }
        color.r = static_cast<u8>(color.r * brightness);
        color.g = static_cast<u8>(color.g * brightness);
        color.b = static_cast<u8>(color.b * brightness);
    }

    return color;
}

extern "C" void OTRControllerCallback(uint8_t rumble) {
    // We call this every tick, SDL accounts for this use and prevents driver spam
    // https://github.com/libsdl-org/SDL/blob/f17058b562c8a1090c0c996b42982721ace90903/src/joystick/SDL_joystick.c#L1114-L1144
    Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0)->GetLED()->SetLEDColor(
        GetColorForControllerLED());

    static std::shared_ptr<SohInputEditorWindow> controllerConfigWindow = nullptr;
    if (controllerConfigWindow == nullptr) {
        auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
        controllerConfigWindow =
            std::dynamic_pointer_cast<SohInputEditorWindow>(gui->GetGuiWindow("Controller Configuration"));
    } else if (controllerConfigWindow->TestingRumble()) {
        return;
    }

    if (rumble) {
        Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0)->GetRumble()->StartRumble();
    } else {
        Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0)->GetRumble()->StopRumble();
    }
}

#ifdef __3DS__
// SoH-3DS dual screen: set while game code draws into the bottom-screen
// framebuffer (320x240, exactly N64 4:3). The HUD anchors to the screen
// edges through OTRGetAspectRatio(); with the top screen's 5:3 the minimap
// would land 40 px past the bottom screen's right edge.
extern "C" int gSoh3dsBottomPass = 0;
#endif

extern "C" float OTRGetAspectRatio() {
#ifdef __3DS__
    if (gSoh3dsBottomPass) {
        return 4.0f / 3.0f;
    }
#endif
    return Ship::Context::GetRawInstance()->GetWindow()->GetAspectRatio();
}

extern "C" float OTRGetDimensionFromLeftEdge(float v) {
    return (SCREEN_WIDTH / 2 - SCREEN_HEIGHT / 2 * OTRGetAspectRatio() + (v));
}

extern "C" float OTRGetDimensionFromRightEdge(float v) {
    return (SCREEN_WIDTH / 2 + SCREEN_HEIGHT / 2 * OTRGetAspectRatio() - (SCREEN_WIDTH - v));
}

// Gets the width of the current render target area
extern "C" uint32_t OTRGetGameRenderWidth() {
    auto fastWnd = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    auto intP = fastWnd->GetInterpreterWeak().lock();

    if (!intP) {
        assert(false && "Lost reference to Fast::Interpreter");
        return 320;
    }

    uint32_t height, width;
    intP->GetCurDimensions(&width, &height);

    return width;
}

// Gets the height of the current render target area
extern "C" uint32_t OTRGetGameRenderHeight() {
    auto fastWnd = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    auto intP = fastWnd->GetInterpreterWeak().lock();

    if (!intP) {
        assert(false && "Lost reference to Fast::Interpreter");
        return 240;
    }

    uint32_t height, width;
    intP->GetCurDimensions(&width, &height);

    return height;
}

f32 floorf(f32 x); // RANDOTODO False positive error "allowing all exceptions is incompatible with previous function"
f32 ceilf(f32 x);  // This gets annoying

extern "C" int16_t OTRGetRectDimensionFromLeftEdge(float v) {
    return ((int)floorf(OTRGetDimensionFromLeftEdge(v)));
}

extern "C" int16_t OTRGetRectDimensionFromRightEdge(float v) {
    return ((int)ceilf(OTRGetDimensionFromRightEdge(v)));
}

int AudioPlayer_Buffered(void) {
    return AudioPlayerBuffered();
}

extern "C" int AudioPlayer_GetDesiredBuffered(void) {
    return AudioPlayerGetDesiredBuffered();
}

extern "C" void AudioPlayer_Play(const uint8_t* buf, uint32_t len) {
    AudioPlayerPlayFrame(buf, len);
}

extern "C" int Controller_ShouldRumble(size_t slot) {
    // don't rumble if we don't have rumble mappings
    if (Ship::Context::GetRawInstance()
            ->GetControlDeck()
            ->GetControllerByPort(static_cast<uint8_t>(slot))
            ->GetRumble()
            ->GetAllRumbleMappings()
            .empty()) {
        return 0;
    }

    // don't rumble if we don't have connected gamepads
    if (Ship::Context::GetRawInstance()
            ->GetControlDeck()
            ->GetConnectedPhysicalDeviceManager()
            ->GetConnectedSDLGamepadsForPort(static_cast<s32>(slot))
            .empty()) {
        return 0;
    }

    // rumble
    return 1;
}

extern "C" size_t GetEquipNowMessage(char* buffer, char* src, const size_t maxBufferSize) {
    CustomMessage customMessage("\x04\x1A\x08"
                                "Would you like to equip it now?"
                                "\x09&&"
                                "\x1B%g"
                                "Yes"
                                "&"
                                "No"
                                "%w\x02",
                                "\x04\x1A\x08"
                                "M"
                                "\x9A"
                                "chtest Du es jetzt ausr\x9Esten?"
                                "\x09&&"
                                "\x1B%g"
                                "Ja!"
                                "&"
                                "Nein!"
                                "%w\x02",
                                "\x04\x1A\x08"
                                "D\x96sirez-vous l'\x96quiper maintenant?"
                                "\x09&&"
                                "\x1B%g"
                                "Oui"
                                "&"
                                "Non"
                                "%w\x02");
    customMessage.Format();

    std::string postfix = customMessage.GetForCurrentLanguage();
    std::string str;
    std::string FixedBaseStr(src);
    size_t RemoveControlChar = FixedBaseStr.find_first_of("\x02");

    if (RemoveControlChar != std::string::npos) {
        FixedBaseStr = FixedBaseStr.substr(0, RemoveControlChar);
    }
    str = FixedBaseStr + postfix;

    if (!str.empty()) {
        memset(buffer, 0, maxBufferSize);
        const size_t copiedCharLen = std::min<size_t>(maxBufferSize - 1, str.length());
        memcpy(buffer, str.c_str(), copiedCharLen);
        return copiedCharLen;
    }
    return 0;
}

extern "C" void Randomizer_ParseSpoiler(const char* fileLoc) {
    Randomizer_ImportSpoilerFile(fileLoc);
}

extern "C" u32 SpoilerFileExists(const char* spoilerFileName) {
    return OTRGlobals::Instance->gRandomizer->SpoilerFileExists(spoilerFileName);
}

extern "C" u8 Randomizer_GetSettingValue(RandomizerSettingKey randoSettingKey) {
    return OTRGlobals::Instance->gRandoContext->GetOption(randoSettingKey).Get();
}

extern "C" RandomizerCheck Randomizer_GetCheckFromActor(s16 actorId, s16 sceneNum, s16 actorParams) {
    return OTRGlobals::Instance->gRandomizer->GetCheckFromActor(actorId, sceneNum, actorParams);
}

extern "C" ShopItemIdentity Randomizer_IdentifyShopItem(s32 sceneNum, u8 slotIndex) {
    return OTRGlobals::Instance->gRandomizer->IdentifyShopItem(sceneNum, slotIndex);
}

extern "C" GetItemEntry ItemTable_Retrieve(int16_t getItemID) {
    // A negative getItemId makes the vanilla lookup `sGetItemTable[getItemId - 1]` read out of
    // bounds below the table (Get Item Manipulation); reproduce the console result of that read.
    if (getItemID < 0) {
        return Gim_RetrieveOobGetItemEntry(getItemID);
    }
    GetItemEntry giEntry = ItemTableManager::Instance->RetrieveItemEntry(MOD_NONE, getItemID);
    return giEntry;
}

extern "C" GetItemEntry ItemTable_RetrieveEntry(s16 tableID, s16 getItemID) {
    if (tableID == MOD_RANDOMIZER) {
        return Rando::StaticData::RetrieveItem(static_cast<RandomizerGet>(getItemID)).GetGIEntry_Copy();
    }
    return ItemTableManager::Instance->RetrieveItemEntry(tableID, getItemID);
}

extern "C" GetItemEntry Randomizer_GetItemFromKnownCheck(RandomizerCheck randomizerCheck, GetItemID ogId) {
    return OTRGlobals::Instance->gRandomizer->GetItemFromKnownCheck(randomizerCheck, ogId);
}

extern "C" GetItemEntry Randomizer_GetItemFromKnownCheckWithoutObtainabilityCheck(RandomizerCheck randomizerCheck,
                                                                                  GetItemID ogId) {
    return OTRGlobals::Instance->gRandomizer->GetItemFromKnownCheck(randomizerCheck, ogId, false);
}

extern "C" ItemObtainability Randomizer_GetItemObtainabilityFromRandomizerCheck(RandomizerCheck randomizerCheck) {
    return OTRGlobals::Instance->gRandomizer->GetItemObtainabilityFromRandomizerCheck(randomizerCheck);
}

extern "C" bool Randomizer_IsCheckShuffled(RandomizerCheck rc) {
    return CheckTracker::IsCheckShuffled(rc);
}

extern "C" GetItemEntry GetItemMystery() {
    return GET_ITEM_MYSTERY;
}

extern "C" uint8_t Randomizer_IsSeedGenerated() {
    return OTRGlobals::Instance->gRandoContext->IsSeedGenerated() ? 1 : 0;
}

extern "C" uint8_t Randomizer_IsSpoilerLoaded() {
    return OTRGlobals::Instance->gRandoContext->IsSpoilerLoaded() ? 1 : 0;
}

extern "C" void Randomizer_SetSpoilerLoaded(bool spoilerLoaded) {
    OTRGlobals::Instance->gRandoContext->SetSpoilerLoaded(spoilerLoaded);
}

extern "C" uint8_t Randomizer_GenerateRandomizer() {
    return GenerateRandomizer() ? 1 : 0;
}

extern "C" uint8_t Randomizer_GenerateFromSeed(const char* seed) {
    if (seed == nullptr) {
        return 0;
    }
    return GenerateRandomizer(seed) ? 1 : 0;
}

extern "C" uint8_t Randomizer_ImportSpoilerFile(const char* fileLoc) {
    if (fileLoc == nullptr || IsRandoGenerating() || !OTRGlobals::Instance->gRandomizer->SpoilerFileExists(fileLoc)) {
        return 0;
    }
    const auto previous = OTRGlobals::Instance->gRandoContext;
    std::shared_ptr<Rando::Context> candidate;
    try {
        candidate = Rando::Context::CreateDetachedInstance();
        // AddExcludedOptions also initializes this Context's location list.
        // Its Settings registry updates are idempotent, so a detached import
        // gets the same per-seed data as the boot Context without duplicates.
        candidate->AddExcludedOptions();
        Rando::Context::SetInstance(candidate);
        Rando::Settings::GetInstance()->AssignContext(candidate);
        candidate->ParseSpoiler(fileLoc);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to stage randomizer seed {}: {}", fileLoc, e.what());
    } catch (...) {
        SPDLOG_ERROR("Failed to stage randomizer seed {}", fileLoc);
    }
    if (candidate == nullptr || !candidate->IsSpoilerLoaded()) {
        // Parsing can unshuffle the global entrance graph before a nested JSON
        // conversion throws. Restore both singleton consumers and the live
        // seed's entrance overrides before returning the error.
        Rando::Context::SetInstance(previous);
        // Rebind Settings before rebuilding the graph. RegionTable_Init may
        // throw while allocating the table; leaving Settings on the rejected
        // candidate would keep that partial seed live after this function.
        Rando::Settings::GetInstance()->AssignContext(previous);
        try {
            // ParseJson rebuilds areaTable and its raw ctx/logic bindings for
            // the candidate. Rebuild from the restored singleton before the
            // candidate is released, then restore the old seed's overrides.
            RegionTable_Init();
            previous->GetEntranceShuffler()->ApplyEntranceOverrides();
            SetAreas();
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Failed to restore randomizer state after rejecting {}: {}", fileLoc, e.what());
        } catch (...) {
            SPDLOG_ERROR("Failed to restore randomizer state after rejecting {}", fileLoc);
        }
        if (candidate != nullptr) {
            candidate->GetLogic()->SetContext(nullptr);
        }
        return 0;
    }
    OTRGlobals::Instance->gRandoContext = candidate;
    // Context and Logic own each other. Retiring a live seed without breaking
    // that back-reference would permanently leak the full old seed context;
    // likewise the failed-candidate path above must break its cycle.
    previous->GetLogic()->SetContext(nullptr);
    CVarSetString(CVAR_GENERAL("SpoilerLog"), fileLoc);
    CVarSave();
    return 1;
}

extern "C" uint8_t Randomizer_ApplyPreset3DS(uint8_t presetIndex) {
    static const char* presetNames[] = {
        "Rando Seed Settings - Beginner",
        "Rando Seed Settings - Standard",
        "Rando Seed Settings - Advanced",
        "Rando Seed Settings - Reset to Default",
    };
    if (IsRandoGenerating() || presetIndex >= sizeof(presetNames) / sizeof(presetNames[0])) {
        return 0;
    }
    // The 3DS boot omits ImGui menu construction, which normally performs
    // this load. Reuse the same preset registry and apply path on demand.
    LoadPresets();
    if (!HasPreset(presetNames[presetIndex], PRESET_SECTION_RANDOMIZER)) {
        SPDLOG_ERROR("Bundled 3DS randomizer preset is unavailable: {}", presetNames[presetIndex]);
        return 0;
    }
    applyPreset(presetNames[presetIndex], { PRESET_SECTION_RANDOMIZER });
    CVarSave();
    return 1;
}

extern "C" bool Randomizer_IsGenerating() {
    return IsRandoGenerating();
}

extern "C" void Randomizer_WaitForGeneration() {
    WaitForRandoGeneration();
}

extern "C" void Randomizer_ShowRandomizerMenu() {
    SohGui::ShowRandomizerSettingsMenu();
}

extern "C" void EntranceTracker_SetCurrentGrottoID(s16 entranceIndex) {
    EntranceTracker::SetCurrentGrottoIDForTracker(entranceIndex);
}

extern "C" void EntranceTracker_SetLastEntranceOverride(s16 entranceIndex) {
    EntranceTracker::SetLastEntranceOverrideForTracker(entranceIndex);
}

extern "C" void Gfx_RegisterBlendedTexture(const char* name, u8* mask, u8* replacement) {
    if (auto intP = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow())
                        ->GetInterpreterWeak()
                        .lock()) {
        intP->RegisterBlendedTexture(name, mask, replacement);
    } else {
        assert(false && "Lost reference to Fast::Interpreter");
    }
}

extern "C" void Gfx_UnregisterBlendedTexture(const char* name) {
    if (auto intP = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow())
                        ->GetInterpreterWeak()
                        .lock()) {
        intP->UnregisterBlendedTexture(name);
    } else {
        assert(false && "Lost reference to Fast::Interpreter");
    }
}

extern "C" void Gfx_TextureCacheDelete(const uint8_t* texAddr) {
    char* imgName = (char*)texAddr;

    if (texAddr == nullptr) {
        return;
    }

    if (ResourceMgr_OTRSigCheck(imgName)) {
        texAddr = (const uint8_t*)ResourceMgr_GetResourceDataByNameHandlingMQ(imgName);
    }

    if (auto intP = dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow())
                        ->GetInterpreterWeak()
                        .lock()) {
        intP->TextureCacheDelete(texAddr);
    } else {
        assert(false && "Lost reference to Fast::Interpreter");
    }
}

bool SoH_HandleConfigDrop(char* filePath) {
    if (SohUtils::IsStringEmpty(filePath)) {
        return false;
    }
    try {
        auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
        std::ifstream configStream(filePath);
        if (!configStream) {
            return false;
        }

        nlohmann::json configJson;
        configStream >> configJson;

        if (!configJson.contains("CVars")) {
            return false;
        }

        CVarClearBlock(CVAR_PREFIX_ENHANCEMENT);
        CVarClearBlock(CVAR_PREFIX_CHEAT);
        CVarClearBlock(CVAR_PREFIX_RANDOMIZER_SETTING);
        CVarClearBlock(CVAR_PREFIX_RANDOMIZER_ENHANCEMENT);
        CVarClearBlock(CVAR_PREFIX_DEVELOPER_TOOLS);

        // Flatten everything under CVars into a single array
        auto cvars = configJson["CVars"].flatten();

        for (auto& [key, value] : cvars.items()) {
            // Replace slashes with dots in key, and remove leading dot
            std::string path = key;
            std::replace(path.begin(), path.end(), '/', '.');
            if (path[0] == '.') {
                path.erase(0, 1);
            }
            if (value.is_string()) {
                CVarSetString(path.c_str(), value.get<std::string>().c_str());
            } else if (value.is_number_integer()) {
                CVarSetInteger(path.c_str(), value.get<int>());
            } else if (value.is_number_float()) {
                CVarSetFloat(path.c_str(), value.get<float>());
            }
        }

        gui->GetGuiWindow("Console")->Hide();
        gui->GetGuiWindow("Actor Viewer")->Hide();
        gui->GetGuiWindow("Collision Viewer")->Hide();
        gui->GetGuiWindow("Save Editor")->Hide();
        gui->GetGuiWindow("Display List Viewer")->Hide();
        gui->GetGuiWindow("Stats")->Hide();
        std::dynamic_pointer_cast<Ship::ConsoleWindow>(gui->GetGuiWindow("Console"))->ClearBindings();

        Rando::Settings::GetInstance()->UpdateAllOptions();
        SohGui::MarkRandomizerMenusDirty();
        gui->SaveConsoleVariablesNextFrame();
        ShipInit::Init("*");

        uint32_t finalHash = SohUtils::Hash(configJson.dump());
        gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "Configuration Loaded. Hash: %d", finalHash);
        return true;
    } catch (std::exception& e) {
        SPDLOG_ERROR("Failed to load config file: {}", e.what());
        auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
        gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "Failed to load config file");
        return false;
    } catch (...) {
        SPDLOG_ERROR("Failed to load config file");
        auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
        gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "Failed to load config file");
        return false;
    }
    return false;
}
