#!/usr/bin/env python3
"""Isolated LLE smoke run; emulator timings are not hardware benchmarks."""
from pathlib import Path
import argparse,datetime,os,subprocess,time,json
root=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('model',choices=['old','new'],default='old',nargs='?')
parser.add_argument('--typed',action='store_true')
parser.add_argument('--extended',action='store_true')
parser.add_argument('--multiblock',action='store_true')
parser.add_argument('--events',action='store_true')
parser.add_argument('--mapped',action='store_true')
parser.add_argument('--stream',action='store_true')
parser.add_argument('--lifecycle',action='store_true')
parser.add_argument('--stream-trace',action='store_true')
parser.add_argument('--stream-io-trace',action='store_true')
parser.add_argument('--stream-mailbox',action='store_true')
args=parser.parse_args();model=args.model
if args.stream_mailbox:args.stream_io_trace=True
if args.stream_io_trace:args.stream_trace=True
if args.stream_trace:args.stream=True
if args.events:args.multiblock=True
if args.multiblock:args.extended=True
if args.extended:args.typed=True
emulator=os.environ.get('AZAHAR')
if not emulator:
    emulator=next((str(p) for p in (Path('/tmp/azahar.AppImage'),Path('/var/tmp/azahar-2126.0.AppImage')) if p.is_file()),None)
if not emulator:raise RuntimeError('Set AZAHAR to an installed emulator executable')
version='v14' if args.stream_mailbox else 'v13' if args.stream_io_trace else 'v12' if args.stream_trace else 'v11' if args.lifecycle else 'v10' if args.stream else 'v9' if args.mapped else 'v8' if args.events else 'v7' if args.multiblock else 'v6' if args.extended else 'v5' if args.typed else 'v4'
stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
out=root/('builds/dsp-synth-probe-20260909/emulator-'+version+'-'+model+'-'+stamp)
out.mkdir(parents=True,exist_ok=True)
cfg=out/'config/azahar-emu';cfg.mkdir(parents=True,exist_ok=True)
s=(root/'builds/old3ds-no-interpolation-20260909T011639Z/emulator/late-policy-old/emulator-config.ini').read_text()
s=s.replace('audio_emulation=0','audio_emulation=1').replace('audio_emulation\\default=true','audio_emulation\\default=false')
s=s.replace('use_gdbstub=true','use_gdbstub=false')
s=s.replace('is_new_3ds=false','is_new_3ds='+str(model=='new').lower()).replace('is_new_3ds\\default=true','is_new_3ds\\default=false')
if os.environ.get('DSP_TRACE'):
    import re
    s=re.sub(r'^log_filter=.*$', 'log_filter=*:Info Service.DSP:Trace Kernel.SVC:Debug', s, flags=re.MULTILINE)
(cfg/'qt-config.ini').write_text(s)
display=':94' if model=='new' else ':93'
env=os.environ.copy();env.update(DISPLAY=display,XDG_CONFIG_HOME=str(out/'config'),XDG_DATA_HOME=str(out/'data'),QT_QPA_PLATFORM='xcb')
x=subprocess.Popen(['/var/tmp/soh-visual-xvfb/usr/bin/Xvfb',display,'-screen','0','1000x900x24'],stdout=open(out/'xvfb.log','w'),stderr=subprocess.STDOUT)
a=None
try:
    time.sleep(1)
    binary=root/'builds/dsp-synth-probe-20260909'
    if args.typed:binary=binary/('events-v8' if args.events else 'multiblock-v7' if args.multiblock else 'extended-v6' if args.extended else 'typed-v5')
    if args.mapped:binary=root/'builds/dsp-synth-probe-20260909/mapped-v9'
    if args.stream:binary=root/('builds/dsp-synth-probe-20260909/stream-v14' if args.stream_mailbox else 'builds/dsp-synth-probe-20260909/stream-v13' if args.stream_io_trace else 'builds/dsp-synth-probe-20260909/stream-v12' if args.stream_trace else 'builds/dsp-synth-probe-20260909/stream-v10')
    if args.lifecycle:binary=root/'builds/dsp-synth-probe-20260909/lifecycle-v11'
    a=subprocess.Popen([emulator,str(binary/('soh-dsp-lifecycle-probe.cxi' if args.lifecycle else 'soh-dsp-stream-probe.cxi' if args.stream else 'soh-dsp-mapped-probe.cxi' if args.mapped else 'soh-dsp-synth-probe.cxi'))],env=env,stdout=open(out/'stdout.log','w'),stderr=subprocess.STDOUT)
    results=out/'data/azahar-emu/sdmc/3ds/soh'/('dsp-lifecycle-probe.txt' if args.lifecycle else 'dsp-stream-probe.txt' if args.stream else 'dsp-mapped-probe.txt' if args.mapped else 'dsp-event-output-probe.txt' if args.events else 'dsp-chain-output-probe.txt' if args.multiblock else 'dsp-chain-probe.txt' if args.extended else 'dsp-pipeline-probe.txt' if args.typed else 'dsp-synth-probe.csv')
    deadline=time.monotonic()+150
    finished=False
    while time.monotonic()<deadline:
        time.sleep(1)
        if results.exists() and ('finished=' if (args.typed or args.mapped or args.stream or args.lifecycle) else '# finished') in results.read_text():
            finished=True;break
        if a.poll() is not None:break
    verification={'finished':finished,'model':model,'result':results.read_text() if results.exists() else None}
    expectedCsndLimitation=False
    if args.multiblock:
        outputFile=results.parent/'dsp-output-probe.txt'
        outputText=outputFile.read_text() if outputFile.exists() else ''
        # Azahar's CSND UpdateState source hardcodes inactive; this is a known
        # unsupported observation, never an audio-output pass.
        expectedCsndLimitation=finished and verification['result'].count('frames=256 result=PASS')==6 and 'initially_active=0' in outputText and 'result=FAIL' in outputText
        verification.update(output_result=outputText,expected_csnd_limitation=expectedCsndLimitation)
    if args.stream:
        result=verification['result'] or ''
        expectedCsndLimitation=finished and 'stream_prefill=PASS blocks=8' in result and 'stream_start=FAIL rc=00000000 activity_known=1 active_mask=0' in result and 'stream_stop=PASS dsp_stop=PASS retained=0' in result and 'finished=FAIL' in result
        verification.update(expected_csnd_limitation=expectedCsndLimitation,streaming_verified=False,scope='Native DSP prefill correctness and teardown; Azahar inactive CSND cannot validate streaming playback.')
    if args.mapped:
        verification['scope']='Native mapped CPU/PCM/state correctness; no audio output or physical performance validation.'
        verification['two_boots_pass']=finished and verification['result'].count('result=PASS')==34 and verification['result'].count('recovery=1 result=PASS')==2
    if args.lifecycle:
        result=verification['result'] or ''
        verification['scope']='Native SDK entry routing, explicit sleep/wake/cancel with DSP correctness; not physical APT events, producer exclusion or playback/performance validation.'
        verification['lifecycle_pass']=finished and result.count('recovery=0 result=PASS')==32 and result.count('recovery=1 result=PASS')==2 and result.count('lifecycle boot=')==4 and 'lifecycle_sleeps=4 wakes=2 cancels=2' in result and 'finished=PASS' in result
    (out/'verification.json').write_text(json.dumps(verification,indent=2))
    if args.lifecycle and not verification['lifecycle_pass']:
        raise RuntimeError('Lifecycle smoke failed; inspect '+str(out))
    if args.mapped and not verification['two_boots_pass']:
        raise RuntimeError('Mapped two-boot smoke failed; inspect '+str(out))
    subprocess.run(['import','-window','root',str(out/'screen.png')],env=env,check=True)
    print('Capture:',out/'screen.png',flush=True)
    if (args.typed or args.mapped or args.stream or args.lifecycle) and not expectedCsndLimitation and (not finished or 'finished=PASS' not in results.read_text()):
        raise RuntimeError('Typed pipeline smoke failed; inspect '+str(out))
finally:
    for p in (a,x):
        if p:
            p.terminate()
            try:p.wait(timeout=5)
            except subprocess.TimeoutExpired:p.kill();p.wait()
