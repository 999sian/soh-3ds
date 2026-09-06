#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOH3DS_RANDO_MAX_FILES 64
#define SOH3DS_RANDO_PATH_MAX 512
#define SOH3DS_RANDO_NAME_MAX 96
#define SOH3DS_RANDO_VISIBLE_ROWS 7
#define SOH3DS_RANDO_SEED_MAX 64

enum Soh3dsRandoResult {
    SOH3DS_RANDO_OK = 1,
    SOH3DS_RANDO_CANCELLED = 0,
    SOH3DS_RANDO_BUSY = -1,
    SOH3DS_RANDO_INVALID = -2,
    SOH3DS_RANDO_WRONG_VERSION = -3,
    SOH3DS_RANDO_TOO_LARGE = -4,
    SOH3DS_RANDO_FAILED = -5,
};

enum Soh3dsRandoStage {
    SOH3DS_RANDO_STAGE_IDLE,
    SOH3DS_RANDO_STAGE_QUEUED,
    SOH3DS_RANDO_STAGE_SETTINGS,
    SOH3DS_RANDO_STAGE_PLACING_ITEMS,
    SOH3DS_RANDO_STAGE_WRITING_LOG,
    SOH3DS_RANDO_STAGE_COMPLETE,
    SOH3DS_RANDO_STAGE_FAILED,
};

typedef struct Soh3dsRandoSeedFile {
    char path[SOH3DS_RANDO_PATH_MAX];
    char name[SOH3DS_RANDO_NAME_MAX];
} Soh3dsRandoSeedFile;

typedef struct Soh3dsRandoMenuRow {
    char label[SOH3DS_RANDO_NAME_MAX];
    char value[SOH3DS_RANDO_NAME_MAX];
    int disabled;
} Soh3dsRandoMenuRow;

typedef struct Soh3dsRandoMenuView {
    char title[48];
    char status[128];
    Soh3dsRandoMenuRow rows[SOH3DS_RANDO_VISIBLE_ROWS];
    int rowCount;
    int selectedRow;
    int busy;
    int picker;
    int pageIndex;
    int pageCount;
} Soh3dsRandoMenuView;

typedef int (*Soh3dsRandoAction)(const char* value);

int Soh3dsRandoMenu_ListSeedFiles(const char* directory, Soh3dsRandoSeedFile* out, int capacity);
int Soh3dsRandoMenu_ValidateSeedFile(const char* path, const char* expectedVersion);
int Soh3dsRandoMenu_TryImport(const char* path, const char* expectedVersion, int busy, Soh3dsRandoAction importer);
int Soh3dsRandoMenu_TryStart(const char* seed, int busy, Soh3dsRandoAction starter);

void Soh3dsRandoProgress_Begin(void);
void Soh3dsRandoProgress_SetStage(int stage);
void Soh3dsRandoProgress_Finish(int success);
int Soh3dsRandoProgress_Stage(void);
const char* Soh3dsRandoProgress_StageName(void);
uint32_t Soh3dsRandoProgress_ElapsedSeconds(void);

void Soh3dsRandoMenu_Open(void);
void Soh3dsRandoMenu_Close(void);
int Soh3dsRandoMenu_IsOpen(void);
void Soh3dsRandoMenu_Update(int accept, int cancel, int up, int down, int left, int right);
void Soh3dsRandoMenu_GetView(Soh3dsRandoMenuView* out);
struct GameState;
void Soh3dsRandoMenu_Draw(struct GameState* state);

#ifdef __cplusplus
}
#endif
