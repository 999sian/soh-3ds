#pragma once
#ifdef __cplusplus
extern "C" {
#endif
struct PlayState;
void Soh3dsQuickItems_Update(struct PlayState* play);
int Soh3dsQuickItems_Slot(int shortcut);
int Soh3dsQuickItems_Item(int shortcut);
int Soh3dsQuickItems_CanUse(struct PlayState* play, int shortcut);
int Soh3dsQuickItems_Assign(struct PlayState* play, int shortcut, int slot);
/* Virtual button ids 8..31 encode the inventory slot and never index equips. */
int Soh3dsQuickItems_Button(struct PlayState* play, int pressed);
int Soh3dsQuickItems_ButtonItem(struct PlayState* play, int button);
int Soh3dsQuickItems_UpdateBottle(struct PlayState* play, int item, int button);
#ifdef __cplusplus
}
#endif
