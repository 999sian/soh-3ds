#!/usr/bin/env python3
"""Exercise production sequence steps with the null sample from ARM11 dump 16."""
from pathlib import Path
import subprocess, tempfile, re
root = Path(__file__).resolve().parents[1]
source = (root/'third_party/shipwright/soh/src/code/audio_seqplayer.c').read_text()
def function(name):
    start = re.search(r'^(?:void|s32) ' + name + r'\([^;{}]*\) \{', source, re.M).start()
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int32_t s32; typedef uint8_t u8; typedef uint16_t u16; typedef float f32;
#define PORTAMENTO_MODE(x) ((x).mode & ~0x80)
#define PORTAMENTO_IS_SPECIAL(x) ((x).mode & 0x80)
enum { PORTAMENTO_MODE_1=1, PORTAMENTO_MODE_2, PORTAMENTO_MODE_3, PORTAMENTO_MODE_4, PORTAMENTO_MODE_5 };
enum { CODEC_S16_INMEMORY=2, MEDIUM_RAM=0 };
typedef struct { int loopEnd; } Loop;
typedef struct { int codec, medium; Loop* loop; } Sample;
typedef struct { Sample* sample; float tuning; } SoundFontSound;
typedef struct { SoundFontSound sound; } Instrument;
typedef struct { SoundFontSound sound; void* envelope; int releaseRate, pan; } Drum;
typedef struct { int transposition, tempo; } SequencePlayer;
typedef struct { int hasInstrument, instOrWave, transposition, fontId; Instrument* instrument; SequencePlayer* seqPlayer; } SequenceChannel;
typedef struct { int mode, speed, cur; float extent; } Portamento;
typedef struct SequenceLayer SequenceLayer;
typedef struct { struct { SequenceLayer *parentLayer, *wantedParentLayer; } playbackState; } Note;
struct SequenceLayer {
 int enabled, delay, gateDelay, stopSomething, continuousNotes, bit1, bit3;
 int instOrWave, semitone, transposition, ignoreDrumPan, pan, delay2, portamentoTargetNote, portamentoTime;
 int notePropertiesNeedInit;
 float freqScale, unk_34;
 SoundFontSound* sound; Note* note; SequenceChannel* channel; Instrument* instrument;
 Portamento portamento; struct { void* envelope; int releaseRate; } adsr;
};
struct { struct {SoundFontSound sound;} synthesisReverbs[64]; int tempoInternalToExternal; struct {int updatesPerFrame;} audioBufferParameters; float unk_2870; } gAudioContext;
float gNoteFrequencies[128];
static int allocations, decays, synthetic, vibratos, portamentos;
static Note note; static Drum drum; static SoundFontSound sfx;
void Audio_SeqLayerNoteDecay(SequenceLayer* l){ ++decays; }
Note* Audio_AllocNote(SequenceLayer* l){ ++allocations; note.playbackState.parentLayer=l; return &note; }
void Audio_InitSyntheticWave(Note* n, SequenceLayer* l){ ++synthetic; }
void Audio_NoteVibratoInit(Note* n){ ++vibratos; }
void Audio_NotePortamentoInit(Note* n){ ++portamentos; }
Drum* Audio_GetDrum(int a,int b){return &drum;}
SoundFontSound* Audio_GetSfx(int a,int b){return &sfx;}
SoundFontSound* Audio_InstrumentGetSound(Instrument* i,int s){return &i->sound;}
void AudioSeq_SeqLayerProcessScriptStep1(SequenceLayer* l){}
s32 AudioSeq_SeqLayerProcessScriptStep2(SequenceLayer* l){return 60;}
s32 AudioSeq_SeqLayerProcessScriptStep3(SequenceLayer* l,s32 c){return c;}
'''
code += '\n'.join(function(n) for n in ('AudioSeq_SeqLayerProcessScriptStep5', 'AudioSeq_SeqLayerProcessScriptStep4', 'AudioSeq_SeqLayerProcessScript'))
code += r'''
int main(void){
 Loop loop={100}; Sample sample={0,0,&loop}; Instrument inst={{&sample,1}};
 SequencePlayer player={0,1}; SequenceChannel channel={1,2,0,0,&inst,&player};
 SequenceLayer initial={.enabled=1,.instOrWave=2,.instrument=&inst,.channel=&channel,.unk_34=1,.delay=1};
 for(int i=0;i<128;i++)gNoteFrequencies[i]=1;
 gAudioContext.unk_2870=1;
 // Exact failing pointer chain: sound exists, sample is null.
 SequenceLayer l=initial; inst.sound.sample=NULL; l.sound=&inst.sound;
 assert(AudioSeq_SeqLayerProcessScriptStep5(&l,true)==-1);
 assert(l.stopSomething && allocations==0);
 // Step 4 must reject before zero-delay loop dereference, for every sound kind.
 for(int kind=0;kind<3;kind++) {
  l=initial;l.instOrWave=kind;l.delay=0;
  assert(AudioSeq_SeqLayerProcessScriptStep4(&l,60)==-1);
  assert(l.stopSomething && l.delay2==0);
 }
 // The caller decays an existing continuous note after rejecting the sample.
 l=initial;l.continuousNotes=1;l.note=&note;decays=0;
 AudioSeq_SeqLayerProcessScript(&l);
 assert(l.stopSomething && decays==1 && allocations==0);
 // Valid sampled sounds still allocate and derive their automatic duration.
 inst.sound.sample=&sample;l=initial;l.delay=0;
 assert(AudioSeq_SeqLayerProcessScriptStep4(&l,60)==false);
 assert(l.delay==101 && !l.stopSomething);
 assert(AudioSeq_SeqLayerProcessScriptStep5(&l,false)==0);
 assert(allocations==1 && vibratos==1 && portamentos==1);
 // A null *sound* is an intentional synthetic wave, including continuous reuse.
 l=initial;l.instrument=NULL;l.continuousNotes=1;l.bit3=1;l.note=&note;note.playbackState.parentLayer=&l;
 assert(AudioSeq_SeqLayerProcessScriptStep4(&l,60)==true && l.sound==NULL);
 assert(AudioSeq_SeqLayerProcessScriptStep5(&l,true)==0 && synthetic==1);
 // Retain the existing unsupported in-memory PCM guard.
 l=initial;l.sound=&inst.sound;sample.codec=2;sample.medium=1;
 assert(AudioSeq_SeqLayerProcessScriptStep5(&l,true)==-1 && l.stopSomething);
}
'''
with tempfile.TemporaryDirectory(prefix='soh-missing-sample-') as d:
    p=Path(d);(p/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS: missing sample, zero-delay, decay, valid audio and synthetic notes')
