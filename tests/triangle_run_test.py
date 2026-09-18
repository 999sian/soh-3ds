#!/usr/bin/env python3
"""Exercise actual dispatch/run decoder and triangle output across state boundaries.

Breaks caught: carrying reuse across state/ucode/debug/flush boundaries, skipping
commands, wrong triangle decoding, and never eliminating redundant key capture.
The existing triangle-pair fixture supplies real clip/cull/key/emission code;
backend/asset import endpoints alone are substituted.
"""
import os
import hashlib
from pathlib import Path
import subprocess
import tempfile
import triangle_pair_test as pair

source = pair.source
reference_path = pair.ROOT / "tests/fixtures/triangle_run_before.cpp"
reference_bytes = reference_path.read_bytes()
assert hashlib.sha256(reference_bytes).hexdigest() == "2e34aa99c68939831315393ee111a4790b2c3b48fd878299c7a5442b8687814c", "Immutable dispatcher oracle changed"
reference = reference_bytes.decode()
assert 'static bool gfx_run_triangles(' in source, 'triangle-run fast path is missing'
fn = pair.function
# The command loop moved from Interpreter::Run into Interpreter::RunInternal;
# Run is now a thin wrapper. The asserted text below is unchanged - only the
# function being extracted was stale. These two assertions pin source text and
# are fragile for that reason; the load-bearing coverage is the oracle
# comparison below, which compiles the real dispatcher against a hash-pinned
# reference. They are kept because they guard one invariant the oracle does not
# reach: reuse must never be carried across a debugger boundary.
run_body = fn(source, 'void Interpreter::RunInternal(')
assert 'const bool debugging = dbg->IsDebugging();' in run_body
assert '#ifdef USE_GBI_TRACE\n        gfx_step();\n#else\n        gfx_step(!debugging);\n#endif' in run_body
extra_handlers = ['gfx_tri1_otr_handler_f3dex2', 'gfx_tri1_handler_f3dex2',
                  'gfx_tri1_handler_f3dex', 'gfx_tri1_handler_f3d']

PRE = r'''
using GfxOpcodeHandlerFunc = bool (*)(F3DGfx**);
using UcodeHandlers = unsigned;
static unsigned ucode_handler_index = 0;
static constexpr int8_t F3DEX2_G_LOAD_UCODE = int8_t(0xdd);
static constexpr int8_t OTR_G_VTX_OTR_FILEPATH=0x24, OTR_G_SETTIMG_OTR_FILEPATH=0x25,
    OTR_G_DL_OTR_FILEPATH=0x27, OTR_G_PUSHCD=0x28, OTR_G_MTX_OTR_FILEPATH=0x29;
struct ExecStack { F3DGfx* current{}; F3DGfx*& currCmd(){return current;} };
static ExecStack g_exec_stack;
static unsigned errors=0;
#define SPDLOG_CRITICAL(...) (++errors)
static void gfx_set_ucode_handler(UcodeHandlers ucode) { ucode_handler_index=ucode; }
'''
TABLES = r'''
static bool change_handler(F3DGfx** cmd) {
    sInterpreterRaw->rdp.combine_mode ^= (*cmd)->words.w1;
    return false;
}
static bool early_handler(F3DGfx** cmd) { ++*cmd; return true; }
struct HandlerTable {
    std::array<GfxOpcodeHandlerFunc,256> handlers{};
    bool contains(int8_t op) const { return handlers[uint8_t(op)]!=nullptr; }
    std::pair<const char*,GfxOpcodeHandlerFunc> at(int8_t op) const { return {"test",handlers[uint8_t(op)]}; }
};
static HandlerTable otrHandlers, rdpHandlers, table0, table1;
static std::array<const HandlerTable*,2> ucode_handlers{&table0,&table1};
static void tables() {
    // Actual production handlers, bound at test opcodes to cover dispatch paths.
    table0.handlers[5]=gfx_tri1_handler_f3dex2;
    table0.handlers[6]=gfx_tri2_handler_f3dex;
    table0.handlers[7]=gfx_quad_handler_f3dex2;
    table0.handlers[8]=gfx_tri1_handler_f3dex;
    table0.handlers[9]=gfx_tri1_handler_f3d;
    table0.handlers[10]=gfx_quad_handler_f3dex;
    table0.handlers[0x11]=early_handler;
    table1=table0;
    table1.handlers[5]=change_handler; // Same numeric opcode, different microcode meaning.
    otrHandlers.handlers[0x26]=gfx_tri1_otr_handler_f3dex2;
    otrHandlers.handlers[0x24]=change_handler;
    rdpHandlers.handlers[0x12]=change_handler;
}
'''

def implementation(namespace):
    text = reference if namespace == "Original" else source
    code = pair.implementation(text, namespace, True)
    # Append within the existing fixture's namespace.
    return code + '\nnamespace '+namespace+' {\n'+PRE + '\n'.join(
        fn(text, 'bool '+name+'(') for name in extra_handlers) + '\n'+TABLES+'\n'+ \
        (fn(source, 'static bool gfx_run_triangles(') if namespace != 'Original' else '')+'\n'+fn(text, 'static void gfx_step(').replace('static void gfx_step()', 'static void gfx_step(bool = false)')+'\n}\n'

TEST = r'''
static F3DGfx command(unsigned op,unsigned a=0,unsigned b=1,unsigned c=2,unsigned d=3) {
    uint32_t w0=op<<24,w1=0;
    if(op==0x26){w0|=a;w1=(b<<16)|c;}
    else if(op==9){w1=(a*10<<16)|(b*10<<8)|(c*10);}
    else if(op==10){w1=(d*2<<24)|(a*2<<16)|(b*2<<8)|(c*2);}
    else {w0|=(a<<17)|(b<<9)|(c<<1);w1=(b<<17)|(d<<9)|(c<<1);}
    return {{w0,w1}};
}
static void equal(Original::Interpreter& a,Candidate::Interpreter& b) {
    assert(a.backend.calls==b.backend.calls);
    assert(a.state()==b.state());
    assert(a.counts.triangle==b.counts.triangle);
    assert(a.counts.emit==b.counts.emit);
    assert(a.counts.derive==b.counts.derive);
}
static void run(Original::Interpreter& a,Candidate::Interpreter& b,
                std::vector<F3DGfx> commands, bool allow=true) {
    auto other=commands;
    Original::sInterpreterRaw=&a; Candidate::sInterpreterRaw=&b;
    Original::g_exec_stack.current=commands.data(); Candidate::g_exec_stack.current=other.data();
    Original::ucode_handler_index=Candidate::ucode_handler_index=0;
    Original::errors=Candidate::errors=0;
    while(Original::g_exec_stack.current<commands.data()+commands.size()-1){
        activeCounts=&a.counts; Original::gfx_step(false);
    }
    while(Candidate::g_exec_stack.current<other.data()+other.size()-1){
        activeCounts=&b.counts; Candidate::gfx_step(allow);
    }
    assert(Original::g_exec_stack.current-commands.data()==Candidate::g_exec_stack.current-other.data());
    assert(Original::ucode_handler_index==Candidate::ucode_handler_index);
    assert(Original::errors==Candidate::errors);
    equal(a,b);
}
int main(){
    Original::tables(); Candidate::tables();
    unsigned cases=0;
    for(unsigned opcode:{5u,6u,7u,8u,9u,10u,0x26u}){
        for(unsigned scenario=0;scenario<12;++scenario){
            Original::Interpreter a; Candidate::Interpreter b;
            auto setup=[&](auto& i){
                i.seed();
                if(scenario==1)i.mTriState.valid=false;
                if(scenario==2){i.importPending=true;i.rdp.textures_changed[0]=true;}
                if(scenario==3){i.mBufVboNumTris=MAX_TRI_BUFFER-1;i.mBufVboLen=12;i.mutateAtFlush=true;}
                if(scenario==4){i.rdp.texture_tile[0].uls=std::numeric_limits<float>::quiet_NaN();}
                if(scenario==5){for(unsigned n:{0u,1u,2u})i.rsp.loaded_vertices[n].clip_rej=1;}
                if(scenario==6)i.rsp.geometry_mode=CULL_BOTH;
                if(scenario==7){i.rdp.viewport_or_scissor_changed=true;i.rdp.viewport.x=7;i.rdp.scissor.y=9;}
                if(scenario==8){i.mBufVboNumTris=MAX_TRI_BUFFER-2;i.mBufVboLen=12;i.mutateAtFlush=true;i.mutateToNan=true;}
                if(scenario==9){i.rdp.loaded_texture[0].raw_tex_metadata.h_byte_scale=std::numeric_limits<float>::quiet_NaN();i.seed();}
            };
            setup(a);setup(b);
            std::vector<F3DGfx> cmds;
            for(unsigned n=0;n<360;++n){
                cmds.push_back(command(opcode));
                if(scenario==10 && n%9==0)cmds.push_back({{0x12000000,0x123}});
                if(scenario==11 && n%13==0){
                    cmds.push_back({{0xdd000001,0}}); // microcode change
                    cmds.push_back(command(5));
                    cmds.push_back({{0xdd000000,0}});
                    cmds.push_back({{0x24000000,0}}); // guarded bad filepath command
                    cmds.push_back({{0x11000000,0}}); // handler-owned advancement
                    cmds.push_back({{0x13000000,0}}); // unknown opcode
                }
            }
            cmds.push_back({{0xff000000,0}}); // Valid readable nontriangle sentinel.
            run(a,b,cmds);
#if SOH3DS_TRIANGLE_RUN_REUSE
            if(scenario==0){
                assert(b.counts.key<a.counts.key/8);
                std::printf("opcode=%u commands=360 key-captures=%u -> %u\n",opcode,a.counts.key,b.counts.key);
            }
#else
            assert(a.counts.key==b.counts.key);
#endif
            ++cases;
        }
    }
    // Debug stepping and separate invocation after direct public state edits.
    Original::Interpreter a;Candidate::Interpreter b;a.seed();b.seed();
    std::vector<F3DGfx> cmds(40,command(6));cmds.push_back({{0xff000000,0}});
    run(a,b,cmds,false);assert(a.counts.key==b.counts.key);
    a.rdp.combine_mode^=0xabcd;b.rdp.combine_mode^=0xabcd;
    run(a,b,cmds);equal(a,b);
    // Mixed command stream: runs terminate at every opcode/state/ucode change.
    uint32_t rng=0x854567u;
    cmds.clear();
    const unsigned ops[]={5,6,7,8,9,10,0x26};
    for(unsigned n=0;n<5000;++n){
        rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
        if((rng&7)==0)cmds.push_back({{0x12000000,rng}});
        cmds.push_back(command(ops[(rng>>5)%7],(rng>>8)%20,(rng>>13)%20,(rng>>18)%20,(rng>>23)%20));
    }
    cmds.push_back({{0xff000000,0}});
    run(a,b,cmds);equal(a,b);
    std::printf("PASS: run=%d %u long-run/flush/state/ucode cases; exact streams and dispatch; debug fallback\n",SOH3DS_TRIANGLE_RUN_REUSE,cases);
}
'''

with tempfile.TemporaryDirectory(prefix='soh-triangle-run-') as directory:
    p=Path(directory)
    code=pair.PREAMBLE.replace('TRI_CAPACITY','256')+implementation('Original')+implementation('Candidate')+TEST
    cpp=p/'test.cpp';cpp.write_text(code)
    for enabled in (0,1):
        exe=p/f'test-{enabled}'
        flags=['-std=c++20','-O2','-fno-fast-math','-ffp-contract=off','-g']
        if os.getenv('SANITIZE'):
            flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
        subprocess.run([os.getenv('CXX','g++'),*flags,'-DSOH3DS_TRIANGLE_PAIR_REUSE=1',
                        f'-DSOH3DS_TRIANGLE_RUN_REUSE={enabled}',str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
