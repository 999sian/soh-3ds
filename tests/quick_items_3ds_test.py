#!/usr/bin/env python3
"""Exercise production shortcut capture across hold, drag, pause and scene changes."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / 'third_party/shipwright/soh/soh/Enhancements/dualscreen3ds'
SOURCE = r'''
#include <assert.h>
#include <stdio.h>
#include "QuickItemsPolicy3DS.h"
int main(void) {
    Soh3dsQuickCapture s = {0};
    Soh3dsQuickCapture_Update(&s, 0, -1, 1, 10);
    assert(s.active == -1 && !s.pressed);
    Soh3dsQuickCapture_Update(&s, 1, 1, 1, 10);
    assert(s.active == -1); // reject one-sample noise
    Soh3dsQuickCapture_Update(&s, 1, 1, 1, 10);
    assert(s.active == 1 && s.pressed);
    Soh3dsQuickCapture_Update(&s, 1, 1, 1, 10);
    assert(s.active == 1 && !s.pressed); // hold does not retrigger
    Soh3dsQuickCapture_Update(&s, 1, 2, 1, 10);
    assert(s.active == -1); // sliding releases, never switches items
    Soh3dsQuickCapture_Update(&s, 1, 1, 1, 10);
    assert(s.active == -1); // release required to rearm
    Soh3dsQuickCapture_Update(&s, 0, -1, 1, 10);
    Soh3dsQuickCapture_Update(&s, 1, 0, 1, 10);
    Soh3dsQuickCapture_Update(&s, 1, 0, 1, 10);
    assert(s.active == 0 && s.pressed);
    Soh3dsQuickCapture_Update(&s, 1, 0, 0, 10);
    assert(s.active == -1);
    Soh3dsQuickCapture_Update(&s, 1, 0, 1, 10);
    assert(s.active == -1); // pause/resume cannot leave a held item stuck
    Soh3dsQuickCapture_Update(&s, 0, -1, 1, 10);
    Soh3dsQuickCapture_Update(&s, 1, 2, 1, 10);
    Soh3dsQuickCapture_Update(&s, 1, 2, 1, 10);
    assert(s.active == 2);
    Soh3dsQuickCapture_Update(&s, 1, 2, 1, 11);
    assert(s.active == -1); // new scene requires fresh contact
    Soh3dsQuickCapture_Update(&s, 0, -1, 1, 11);
    Soh3dsQuickCapture_Update(&s, 1, -1, 1, 11);
    Soh3dsQuickCapture_Update(&s, 1, 0, 1, 11);
    assert(s.active == -1); // entering a button from another UI region is inert
    assert(Soh3dsQuickTarget(10, 202) == 0);
    assert(Soh3dsQuickTarget(112, 202) == 1);
    assert(Soh3dsQuickTarget(214, 202) == 2);
    assert(Soh3dsQuickTarget(110, 215) == -1);
    assert(Soh3dsQuickTarget(319, 239) == -1);
    assert(Soh3dsQuickTarget(10, 201) == -1);
    assert(Soh3dsQuickTarget(-1, 210) == -1);
    puts("quick item capture: press/hold/release, noise, drag, bounds, pause and scene changes pass");
}
'''
with tempfile.TemporaryDirectory(prefix='soh-quick-items-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(SOURCE)
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(UI), str(path / 'test.c'),
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)

# Compile the production game bridge against narrow engine doubles. Item/slot
# constants come from the real engine header; arrays are deliberately exact-size
# so ASan detects accidentally treating virtual buttons as equipment indexes.
production = (UI / 'QuickItems3DS.c').read_text()
production = '\n'.join(line for line in production.splitlines() if not line.startswith('#include'))
game = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include "z64item.h"
#include "QuickItems3DS.h"
#include "QuickItemsPolicy3DS.h"
using u32 = uint32_t;
#define ARRAY_COUNT(x) (sizeof(x)/sizeof((x)[0]))
#define CVAR_ENHANCEMENT(x) "gEnhancements." x
#define CHECK_AGE_REQ_SLOT(x) (allowedAge[x])
#define BUTTON_STATUS_INDEX(x) ((x) > 3 ? (x)+1 : (x))
#define GET_PLAYER(p) (&(p)->player)
enum { MSGMODE_NONE=0, GAMEOVER_INACTIVE=0, GAMEMODE_NORMAL=0, TRANS_TRIGGER_OFF=0, TRANS_MODE_OFF=0,
       SCENE_FISHING_POND=10, SCENE_TREASURE_BOX_SHOP=11, SOH3DS_TAB_MAP=2, SOH3DS_TAB_GEAR=1,
       BTN_ENABLED=0, BTN_START=0x1000, PAUSE_ITEM=0,
       PLAYER_STATE1_ON_HORSE=1, PLAYER_STATE1_CLIMBING_LADDER=2, PLAYER_STATE1_CARRYING_ACTOR=4,
       PLAYER_STATE1_IN_CUTSCENE=8, PLAYER_STATE2_CRAWLING=1,
       VB_EQUIP_ITEM_TO_C_BUTTON=100, VB_EMPTY_BOTTLE_TO_HALF_MILK=101, VB_UPDATE_BOTTLE_ITEM=102 };
struct Player { unsigned stateFlags1=0,stateFlags2=0; int currentMask=0,itemAction=0,heldItemAction=0,heldItemButton=0; };
struct InterfaceContext { struct { int bottles=0,tradeItems=0,ocarina=0,hookshot=0,farores=0,dinsNayrus=0,all=0; } restrictions; };
struct PlayState {
 struct { int state=0,debugState=0; int cursorItem[4]{}; } pauseCtx;
 struct { int msgMode=0; } msgCtx;
 struct { int state=0; } gameOverCtx;
 struct { u32 frames=0; struct { struct {unsigned button=0;} press,cur; } input[1]; } state;
 int transitionTrigger=0,transitionMode=0,shootingGalleryStatus=0,bombchuBowlingStatus=0,sceneNum=0;
 InterfaceContext interfaceCtx; Player player;
};
struct Save {
 int health=16,gameMode=0,minigameState=0,eventInf[1]{},linkAge=0,fileNum=0;
 struct { uint8_t items[24]; } inventory;
 struct { uint8_t buttonItems[8],cButtonSlots[7]; } equips;
 uint8_t buttonStatus[9]{};
} gSaveContext;
bool allowedAge[24]; bool cutscene=false,pacifist=false,equipAllowed=true; int hazard=0,tab=2;
int touchHeld=0,touchX=0,touchY=0,saves=0,iconLoads=0;
std::map<std::string,int> vars;
int CVarGetInteger(const char* k,int d){auto p=vars.find(k);return p==vars.end()?d:p->second;}
void CVarSetInteger(const char*k,int v){vars[k]=v;} void CVarSave(){++saves;}
int Play_InCsMode(PlayState*){return cutscene;} int GameInteractor_PacifistModeActive(){return pacifist;}
int Player_GetEnvironmentalHazard(PlayState*){return hazard;}
int GameInteractor_Should(int hook,int fallback,...){assert(hook!=VB_EQUIP_ITEM_TO_C_BUTTON);return fallback;}
void Interface_LoadItemIcon1(PlayState*,int button){assert(button>0 && button<8);++iconLoads;}
int Soh3dsBottomScreen_Tab(){return tab;}
extern "C" int Soh3dsTouchRead(int*x,int*y){*x=touchX;*y=touchY;return touchHeld;}
""" + production + r"""
int main(){
 PlayState p;
 memset(gSaveContext.inventory.items,ITEM_NONE,24);
 memset(gSaveContext.equips.buttonItems,ITEM_NONE,8);
 memset(gSaveContext.equips.cButtonSlots,SLOT_NONE,7);
 for(bool &a:allowedAge)a=true;
 gSaveContext.inventory.items[SLOT_OCARINA]=ITEM_OCARINA_FAIRY;
 gSaveContext.inventory.items[SLOT_BOW]=ITEM_BOW;
 gSaveContext.inventory.items[SLOT_ARROW_FIRE]=ITEM_ARROW_FIRE;
 auto equips=gSaveContext.equips;
 assert(!Soh3dsQuickItems_Assign(&p,0,SLOT_BOW));
 assert(!Soh3dsQuickItems_Assign(&p,1,24));
 assert(!Soh3dsQuickItems_Assign(&p,1,-1));
 allowedAge[SLOT_BOW]=false;assert(!Soh3dsQuickItems_Assign(&p,1,SLOT_BOW));
 allowedAge[SLOT_BOW]=true;equipAllowed=false;
 assert(Soh3dsQuickItems_Assign(&p,1,SLOT_BOW));assert(saves==1); // equip hook must not run
 equipAllowed=true;
 assert(memcmp(&equips,&gSaveContext.equips,sizeof(equips))==0);
 assert(Soh3dsQuickItems_Item(1)==ITEM_BOW);
 vars["gEnhancements.DualScreen.QuickSlot2"]=999;assert(Soh3dsQuickItems_Item(2)==ITEM_NONE);
 assert(Soh3dsQuickItems_Assign(&p,2,SLOT_ARROW_FIRE));
 assert(Soh3dsQuickItems_Item(2)==ITEM_BOW_ARROW_FIRE);
 gSaveContext.inventory.items[SLOT_ARROW_ICE]=ITEM_ARROW_ICE;
 assert(Soh3dsQuickItems_Assign(&p,2,SLOT_ARROW_ICE));
 assert(Soh3dsQuickItems_Item(2)==ITEM_BOW_ARROW_ICE);
 gSaveContext.inventory.items[SLOT_ARROW_LIGHT]=ITEM_ARROW_LIGHT;
 assert(Soh3dsQuickItems_Assign(&p,2,SLOT_ARROW_LIGHT));
 assert(Soh3dsQuickItems_Item(2)==ITEM_BOW_ARROW_LIGHT);
 assert(Soh3dsQuickItems_CanUse(&p,0));
 p.interfaceCtx.restrictions.ocarina=1;assert(!Soh3dsQuickItems_CanUse(&p,0));
 p.interfaceCtx.restrictions.ocarina=0;p.pauseCtx.state=1;assert(!Soh3dsQuickItems_CanUse(&p,1));
 p.pauseCtx.state=0;cutscene=true;assert(!Soh3dsQuickItems_CanUse(&p,1));cutscene=false;
 p.transitionTrigger=1;assert(!Soh3dsQuickItems_CanUse(&p,1));p.transitionTrigger=0;
 pacifist=true;assert(!Soh3dsQuickItems_CanUse(&p,1));pacifist=false;
 hazard=3;assert(!Soh3dsQuickItems_CanUse(&p,1));hazard=0;
 p.interfaceCtx.restrictions.all=1;assert(!Soh3dsQuickItems_CanUse(&p,1));p.interfaceCtx.restrictions.all=0;
 assert(Soh3dsQuickItems_ButtonItem(&p,-1)==ITEM_NONE);
 assert(Soh3dsQuickItems_ButtonItem(&p,32)==ITEM_NONE);
 Soh3dsQuickItems_Update(&p);
 touchHeld=1;touchX=120;touchY=220;++p.state.frames;Soh3dsQuickItems_Update(&p);
 assert(Soh3dsQuickItems_Button(&p,1)==-1);
 ++p.state.frames;Soh3dsQuickItems_Update(&p);
 int button=Soh3dsQuickItems_Button(&p,1);assert(button==8+SLOT_BOW);
 assert(Soh3dsQuickItems_ButtonItem(&p,button)==ITEM_BOW);
 ++p.state.frames;Soh3dsQuickItems_Update(&p);assert(Soh3dsQuickItems_Button(&p,1)==-1);
 assert(Soh3dsQuickItems_Button(&p,0)==button);
 touchHeld=0;++p.state.frames;Soh3dsQuickItems_Update(&p);assert(Soh3dsQuickItems_Button(&p,0)==-1);
 // Updating an un-equipped bottle must never write past actual equipped slots.
 gSaveContext.inventory.items[SLOT_BOTTLE_1]=ITEM_MILK_BOTTLE;
 gSaveContext.inventory.items[SLOT_BOTTLE_2]=ITEM_POTION_RED;
 assert(Soh3dsQuickItems_UpdateBottle(&p,ITEM_BOTTLE,8+SLOT_BOTTLE_1));
 assert(gSaveContext.inventory.items[SLOT_BOTTLE_1]==ITEM_MILK_HALF);
 assert(gSaveContext.inventory.items[SLOT_BOTTLE_2]==ITEM_POTION_RED);
 assert(memcmp(&equips,&gSaveContext.equips,sizeof(equips))==0);assert(iconLoads==0);
 gSaveContext.equips.cButtonSlots[1]=SLOT_BOTTLE_1;gSaveContext.equips.buttonItems[2]=ITEM_MILK_HALF;
 assert(Soh3dsQuickItems_UpdateBottle(&p,ITEM_BOTTLE,8+SLOT_BOTTLE_1));
 assert(gSaveContext.inventory.items[SLOT_BOTTLE_1]==ITEM_BOTTLE);
 assert(gSaveContext.equips.buttonItems[2]==ITEM_BOTTLE && iconLoads==1);
 assert(!Soh3dsQuickItems_UpdateBottle(&p,ITEM_BOTTLE,2));
 assert(Soh3dsQuickItems_UpdateBottle(&p,ITEM_BOTTLE,255));
 puts("quick item game bridge: assignments, restrictions, virtual buttons and bottle updates pass");
}
"""
with tempfile.TemporaryDirectory(prefix='soh-quick-game-') as directory:
    path = Path(directory)

    player_source = (ROOT / 'third_party/shipwright/soh/src/overlays/actors/ovl_player_actor/z_player.c').read_text()
    def function(signature):
        start = player_source.index(signature)
        opening = player_source.index('{', start)
        depth, end = 1, opening + 1
        while depth:
            depth += (player_source[end] == '{') - (player_source[end] == '}')
            end += 1
        return re.sub(r'\bthis\b', 'self', player_source[start:end])
    player_prefix = r"""
using s32=int; using u16=uint16_t;
enum { BTN_B=0x4000, BTN_CLEFT=2, BTN_CDOWN=4, BTN_CRIGHT=1,
 BTN_DUP=0x800, BTN_DDOWN=0x400, BTN_DLEFT=0x200, BTN_DRIGHT=0x100,
 PLAYER_MASK_NONE=0, PLAYER_IA_MASK_KEATON=50, PLAYER_IA_FISHING_POLE=2,
 VB_PLAYER_UNEQUIP_MASK_WITHOUT_BUTTON=200, VB_PUTAWAY_BECAUSE_DISABLED_ITEM_BUTTONS=201,
 VB_OVERRIDE_BUTTON_ITEM_USED=202, VB_CHANGE_HELD_ITEM_AND_USE_ITEM=203 };
#define CHECK_BTN_ALL(b,m) (((b)&(m))==(m))
#define B_BTN_ITEM gSaveContext.equips.buttonItems[0]
#define C_BTN_ITEM(n) (gSaveContext.buttonStatus[(n)+1]!=255?gSaveContext.equips.buttonItems[(n)+1]:ITEM_NONE)
#define DPAD_ITEM(n) (gSaveContext.buttonStatus[(n)+5]!=255?gSaveContext.equips.buttonItems[(n)+4]:ITEM_NONE)
u16 sItemButtons[]={BTN_B,BTN_CLEFT,BTN_CDOWN,BTN_CRIGHT,BTN_DUP,BTN_DDOWN,BTN_DLEFT,BTN_DRIGHT};
decltype(PlayState{}.state.input[0])* unusedInputType; // replaced below: remove reference from decltype
bool sHeldItemButtonIsHeldDown=false;
int useCount=0,lastUsed=-1;
int Player_ItemToItemAction(int item){return item<ITEM_NONE_FE?item+10:0;}
int func_8008F128(Player*){return 0;}
void Player_UseItem(PlayState*,Player* player,int item){++useCount;lastUsed=item;player->heldItemAction=player->itemAction=Player_ItemToItemAction(item);}
"""
    player_prefix = player_prefix.replace('decltype(PlayState{}.state.input[0])* unusedInputType; // replaced below: remove reference from decltype',
                                         'std::remove_reference_t<decltype(PlayState{}.state.input[0])>* sControlInput;')
    player_code = '\n'.join(function(sig) for sig in ['s32 Player_ItemIsInUse(', 's32 Player_ItemIsItemAction(',
                                                     's32 Player_GetItemOnButton(', 'void Player_ProcessItemButtons('])
    player_tests = r"""
 // Execute the production player scanner: no-input sentinel may NOT alias a quick button.
 sControlInput=&p.state.input[0];p.player={};p.state.input[0]={};useCount=0;
 Player_ProcessItemButtons(&p.player,&p);assert(useCount==0);
 touchHeld=1;touchX=120;touchY=220;
 ++p.state.frames;Soh3dsQuickItems_Update(&p);++p.state.frames;Soh3dsQuickItems_Update(&p);
 Player_ProcessItemButtons(&p.player,&p);assert(useCount==1&&lastUsed==ITEM_BOW);
 assert(p.player.heldItemButton==8+SLOT_BOW);
 ++p.state.frames;Soh3dsQuickItems_Update(&p);sHeldItemButtonIsHeldDown=false;
 Player_ProcessItemButtons(&p.player,&p);assert(useCount==1&&sHeldItemButtonIsHeldDown);
 touchHeld=0;++p.state.frames;Soh3dsQuickItems_Update(&p);sHeldItemButtonIsHeldDown=false;
 Player_ProcessItemButtons(&p.player,&p);assert(useCount==1&&!sHeldItemButtonIsHeldDown);
 // Real C-button input wins over a simultaneous touch press.
 gSaveContext.equips.buttonItems[1]=ITEM_NUT;
 touchHeld=1;++p.state.frames;Soh3dsQuickItems_Update(&p);++p.state.frames;Soh3dsQuickItems_Update(&p);
 p.state.input[0].press.button=BTN_CLEFT;
 Player_ProcessItemButtons(&p.player,&p);assert(useCount==2&&lastUsed==ITEM_NUT&&p.player.heldItemButton==1);
 p.state.input[0].press.button=0;touchHeld=0;++p.state.frames;Soh3dsQuickItems_Update(&p);
 // Both slots route their current item through the same player API, including bottles.
 p.player={};gSaveContext.inventory.items[SLOT_BOTTLE_1]=ITEM_POTION_RED;
 assert(Soh3dsQuickItems_Assign(&p,2,SLOT_BOTTLE_1));
 touchHeld=1;touchX=220;++p.state.frames;Soh3dsQuickItems_Update(&p);++p.state.frames;Soh3dsQuickItems_Update(&p);
 Player_ProcessItemButtons(&p.player,&p);assert(lastUsed==ITEM_POTION_RED&&p.player.heldItemButton==8+SLOT_BOTTLE_1);
 touchHeld=0;++p.state.frames;Soh3dsQuickItems_Update(&p);p.player={};
 assert(Soh3dsQuickItems_Assign(&p,2,SLOT_ARROW_LIGHT));
 touchHeld=1;++p.state.frames;Soh3dsQuickItems_Update(&p);++p.state.frames;Soh3dsQuickItems_Update(&p);
 Player_ProcessItemButtons(&p.player,&p);assert(lastUsed==ITEM_BOW_ARROW_LIGHT);
 """
    game = game.replace('int main(){', player_prefix + player_code + '\nint main(){')
    game = game.replace(' puts("quick item game bridge:', player_tests + '\n puts("quick item game bridge:')
    (path / 'test.cpp').write_text(game)

    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-D__3DS__', '-O1',
                    '-fsanitize=address,undefined', '-I', str(UI), '-I', str(ROOT / 'third_party/shipwright/soh/include'),
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
