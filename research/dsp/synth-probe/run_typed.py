#!/usr/bin/env python3
"""Refresh production oracles and verify resident DSP kernels and dispatch."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'builds/dsp-synth-probe-20260909'
for script in ('run_resample.py', 'run_adpcm.py', 'run_envelope.py', 'run_filter.py', 'run_dmem.py', 'run_mapped_arithmetic.py', 'run_duplicate.py', 'run_s8.py', 'run_mapped_filter.py', 'run_scalar.py', 'run_payload_load.py', 'run_capture.py'):
    subprocess.run(['python3', str(HERE / script)], check=True)
subprocess.run(['python3', str(HERE / 'run_chunk_runner.py'), '--reuse-scalar'], check=True)
subprocess.run(['python3', str(HERE / 'run_state_transaction.py'), '--reuse-oracles'], check=True)
subprocess.run(['python3', str(HERE / 'run_cpu_replay.py')], check=True)
subprocess.run(['python3', str(HERE / 'run_chunk_frontend.py'), '--reuse-cpu'], check=True)
subprocess.run(['python3', str(HERE / 'run_mixer_hooks.py'), '--reuse-cpu'], check=True)
subprocess.run(['python3', str(HERE / 'run_mixer_owner.py'), '--reuse-hooks'], check=True)
subprocess.run(['python3', str(HERE / 'run_mapped_bootstrap.py')], check=True)
subprocess.run(['python3', str(HERE / 'run_native_transport.py')], check=True)
subprocess.run(['python3', str(HERE / 'run_mapped_component.py')], check=True)
subprocess.run(['python3', str(HERE / 'run_sdk_lifecycle.py')], check=True)
subprocess.run(['python3', str(HERE / 'run_producer_gate.py')], check=True)
subprocess.run(['python3', str(HERE / 'run_csnd_stream.py')], check=True)
subprocess.run(['python3', str(HERE / 'run_output_worker.py')], check=True)
transport_result=json.loads((OUT/'native-transport-validation.json').read_text())['result']
results = {'resident_transport_probe': transport_result.strip()}
for name in ('typed_firmware_probe', 'queue_probe', 'pipeline_probe', 'synth_chain_probe', 'multiblock_chain_probe', 'synth_chain_multiblock_probe', 'completion_event_probe', 'mapped_resample_probe', 'mapped_adpcm_probe', 'mapped_synthesis_probe', 'mapped_chunk_probe'):
    binary = OUT / name.replace('_', '-')
    subprocess.run([
        'c++', '-std=c++17', '-O2', '-fsanitize=undefined', '-fno-sanitize-recover=all',
        '-I' + str(ROOT / 'research/dsp/teakra/include'),
        '-I' + str(ROOT / 'research/dsp/teakra/src'),
        *(['-DSOH_DSP_MULTIBLOCK_TEST'] if name == 'synth_chain_multiblock_probe' else []),
        str(HERE / (('synth_chain_probe' if name == 'synth_chain_multiblock_probe' else name) + '.cpp')),
        str(OUT / 'resample_reference.o'), str(OUT / 'adpcm_reference.o'),
        str(OUT / ('mapped_arithmetic_reference.o' if name in ('mapped_synthesis_probe', 'mapped_chunk_probe') else 'envelope_reference.o')),
        str(OUT / 'filter_reference.o'),
        str(OUT / 'teakra-build/src/libteakra.a'), '-pthread', '-o', str(binary),
    ], check=True)
    command = [str(binary)] + ([str(OUT)] if name in ('pipeline_probe', 'synth_chain_probe', 'multiblock_chain_probe', 'completion_event_probe') else [])
    result = subprocess.run(command, capture_output=True, text=True)
    print(result.stdout, end='', flush=True)
    print(result.stderr, end='', flush=True)
    result.check_returncode()
    (OUT / (name + '-result.txt')).write_text(result.stdout)
    results[name] = result.stdout.strip()
paths = [*HERE.glob('*.h'), *HERE.glob('*.cpp'), HERE / 'transport_test/3ds.h', HERE / 'component_test/3ds.h', HERE / 'run_mapped_component.py', HERE / 'sdk_lifecycle_bridge.c', HERE / 'run_sdk_lifecycle.py', HERE / 'run_producer_gate.py', HERE / 'gate_test/3ds.h', HERE / 'prepare_sdk_lifecycle.py', ROOT / 'cmake/DspLifecycle3DS.cmake', HERE / 'csnd_test/3ds.h', HERE / 'output_worker_test/3ds.h', HERE / 'run_output_worker.py', HERE / 'run_csnd_stream.py', HERE / 'run_typed.py', HERE / 'run_duplicate.py', HERE / 'run_s8.py', HERE / 'run_mapped_filter.py', HERE / 'run_scalar.py', HERE / 'run_payload_load.py', HERE / 'run_capture.py', HERE / 'run_chunk_runner.py', HERE / 'run_state_transaction.py', HERE / 'run_cpu_replay.py', HERE / 'run_chunk_frontend.py', HERE / 'run_mixer_hooks.py', HERE / 'run_mixer_owner.py', HERE / 'build_mapped_firmware.py', HERE / 'run_mapped_bootstrap.py', HERE / 'run_native_transport.py', HERE / 'run_mapped_arithmetic.py',
         OUT / 'resample_reference.c', OUT / 'adpcm_reference.c',
         OUT / 'envelope_reference.c', OUT / 'filter_reference.c',
         OUT / 'duplicate_reference.c', OUT / 's8_reference.c', OUT / 'mapped_filter_reference.c', OUT / 'scalar_reference.c', OUT / 'mapped_arithmetic_reference.c',
         OUT / 'typed-firmware-words.bin', OUT / 'pipeline_fixture.h',
         OUT / 'extended-firmware-words.bin', OUT / 'chain_fixture.h',
         OUT / 'multiblock-firmware-words.bin', OUT / 'completion-firmware-words.bin']
(OUT / 'typed-validation.json').write_text(json.dumps({
    'results': results,
    'limitation': 'Production ADPCM oracle has unsanitized signed shifts; see adpcm-reference.json.',
    'sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
}, indent=2) + '\n')
