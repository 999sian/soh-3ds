#pragma once
#include "RandomizerMenu3DS.h"
#ifdef __cplusplus
extern "C" {
#endif
void Soh3dsRandoSettings_Refresh(void);
int Soh3dsRandoSettings_Randomize(void);
int Soh3dsRandoSettings_Reset(void);
int Soh3dsRandoSettings_GroupCount(void);
const char* Soh3dsRandoSettings_GroupName(int group);
int Soh3dsRandoSettings_Count(int group);
void Soh3dsRandoSettings_Read(int group, int row, Soh3dsRandoMenuRow* out);
const char* Soh3dsRandoSettings_Description(int group, int row);
int Soh3dsRandoSettings_Adjust(int group, int row, int direction);
#ifdef __cplusplus
}
#endif
