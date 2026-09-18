#!/usr/bin/env python3
"""Exercise actual frame dispatch and texture endpoint handling for both runtime model paths."""
from pathlib import Path
import subprocess,tempfile,argparse
ROOT=Path(__file__).resolve().parents[1]
def extract(text,signature):
 a=text.index(signature);i=text.index('{',a);depth=1;j=i+1
 while depth:depth+=(text[j]=='{')-(text[j]=='}');j+=1
 return text[a:j]
s=(ROOT/'third_party/shipwright/soh/soh/OTRGlobals.cpp').read_text()
frames=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
int maps=0,interpolates=0,lookups=0,behind=0,drops=0,events=0,styles=0,draws=0,originalDraws=0,notifications=0,target=0;
namespace std {template<class K,class V>struct unordered_map{unordered_map(){++maps;}};template<class T,class U>U* dynamic_pointer_cast(U* p){return p;}}
struct Gfx{};struct Mtx{};struct MtxF{};
struct Interpreter{int mInterpolationIndex=0,mInterpolationIndexTarget=0;float mInterpolationT=1;};
struct Lock {Interpreter* value;Interpreter* get(){return value;}};
struct Weak {Interpreter* value;Lock lock(){return{value};}};
struct Window {Interpreter i;Weak GetInterpreterWeak(){return{&i};}void HandleEvents(){++events;}void SetTargetFps(int fps){target=fps;}void DrawAndRunGraphicsCommands(Gfx*,const std::unordered_map<Mtx*,MtxF>&){++draws;}void DrawAndRunGraphicsCommands(Gfx*){++draws;++originalDraws;}} window;
namespace Fast{using Fast3dWindow=Window;}
struct Resources{void SetAltAssetsEnabled(bool){}};
namespace Ship {struct Context {static Context* GetRawInstance(){static Context c;return &c;}Window* GetWindow(){return &window;}Resources* GetResourceManager(){static Resources r;return &r;}};}
bool gSoh3dsFrameInterpolationEnabled=false;
int wantedFps=20;struct OTRGlobals{Ship::Context* context=Ship::Context::GetRawInstance();int GetInterpolationFPS(){++lookups;return wantedFps;}static OTRGlobals* Instance;};OTRGlobals globals;OTRGlobals* OTRGlobals::Instance=&globals;
#define CVAR_SETTING(x) x
int CVarGetInteger(const char*,int fallback){return fallback;}
namespace UIWidgets{enum Colors{LightBlue};struct ColorLookup{int at(Colors){return0;}};ColorLookup ColorValues;}
#define return0 return 0
namespace ImGui{void PushStyleColor(int,int){++styles;}void PopStyleColor(){--styles;}}
constexpr int ImGuiCol_TitleBgActive=0;
std::unordered_map<Mtx*,MtxF> FrameInterpolation_Interpolate(float){++interpolates;return{};}
bool Soh3dsFrameBehind(){++behind;return false;}void Soh3dsFrameDropped(){++drops;}
int R_UPDATE_RATE=3;unsigned sGameTicks=0;struct Audio{void NotifyProcessing(){++notifications;}}audio;
bool debugging=false;bool GfxDebuggerIsDebugging(){return debugging;}
bool prevAltAssets=true;void gfx_texture_cache_clear(){}
namespace SOH{struct SkeletonPatcher{static void UpdateSkeletons(){}};}
struct GameInteractor{struct OnAssetAltChange{};static GameInteractor* Instance;template<class T>void ExecuteHooks(){}};GameInteractor interactor;GameInteractor* GameInteractor::Instance=&interactor;
'''.replace('return0;', 'return 0;')+extract(s,'void RunCommands(')+'\n'+extract(s,'extern "C" void Graph_ProcessGfxCommands(')+r'''
int main(){Gfx command;
 for(int rate: {1,2,3}){
  R_UPDATE_RATE=rate;
  for(int i=0;i<100;i++){debugging=i%2;int before=draws;Graph_ProcessGfxCommands(&command);assert(draws==before+1);assert(target==60/rate);}
 }
 assert(maps==0 && interpolates==0 && lookups==0 && behind==0 && drops==0);
 assert(originalDraws==300 && notifications==300 && events==300 && styles==0 && sGameTicks==300);
 puts("PASS: Old native20/30/60Hz ticks render once, zero interpolation scheduling/maps/calls");
 maps=interpolates=lookups=behind=drops=events=styles=draws=originalDraws=notifications=0;
 gSoh3dsFrameInterpolationEnabled=true;debugging=false;
 wantedFps=30;R_UPDATE_RATE=3;for(int i=0;i<20;i++)Graph_ProcessGfxCommands(&command);
 assert(draws==30 && originalDraws==0 && interpolates==20 && notifications==20 && styles==0 && target==30);
 puts("PASS: standard/New30FPS path still renders30 frames for20 game ticks");
}
'''
frames='#include <initializer_list>\n'+frames
s=(ROOT/'third_party/libultraship/src/fast/interpreter.cpp').read_text()
tile=r'''
#include <initializer_list>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
struct F3DGfx{struct{uint32_t w0,w1;}words;};
struct Tile{float uls,ult,lrs,lrt;};struct Rdp{Tile texture_tile[8];bool textures_changed[2]{};};
struct Interpreter{const void* mCurMtxReplacements=nullptr;float mInterpolationT=0.5;Rdp state{};Rdp* mRdp=&state;};Interpreter instance;Interpreter* sInterpreterRaw=&instance;
#define C1(shift,bits) ((cmd->words.w1>>(shift))&((1u<<(bits))-1))
'''+extract(s,'bool gfx_set_tile_size_lerp_handler_rdp(')+r'''
int main(){
 const float ends[3][8]={{0,1,2,3,4,5,6,7},{1e20f,-1e20f,1024,-1024,1,-1,0.25f,-0.25f},{-64,32,64,128,-63.75f,32.25f,64.5f,128.75f}};
 for(bool native: {true,false}){instance.mCurMtxReplacements=native?nullptr:&instance;
 for(auto&coords:ends){F3DGfx words[5]{};for(int i=0;i<8;i+=2){memcpy(&words[1+i/2].words.w0,&coords[i],4);memcpy(&words[1+i/2].words.w1,&coords[i+1],4);}auto* pc=words;
  assert(!gfx_set_tile_size_lerp_handler_rdp(&pc));assert(pc==words+4);
  auto*result=&instance.state.texture_tile[0].uls;
  for(int i=0;i<4;i++){
   if(native){assert(!memcmp(&result[i],&coords[i+4],4));}else{
   float expected=coords[i]+.5f*(coords[i+4]-coords[i]);assert(!memcmp(&result[i],&expected,4));}
  }
  assert(instance.state.textures_changed[0]&&instance.state.textures_changed[1]);
 }
 }
 puts("PASS: texture command consumption and exact endpoint/midpoint values");
}
'''
parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path);args=parser.parse_args()
with tempfile.TemporaryDirectory() as d:
 p=Path(d)
 for name,code in [('frames',frames),('tile',tile)]:
  source=p/(name+'.cpp');source.write_text(code)
  if args.output:args.output.mkdir(parents=True,exist_ok=True);(args.output/source.name).write_text(code)
  exe=p/name
  subprocess.run(['c++','-std=c++20','-O2','-ffp-contract=off','-D__3DS__',str(source),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
