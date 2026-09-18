#include <cstdio>
#include <stdexcept>
#include "teakra/teakra.h"
#include "typed_firmware.h"
#include "chunk_runner.h"
#include "state_transaction.h"
extern "C" void adpcmReferenceAt(int16_t*,unsigned,unsigned,uint16_t,uint16_t,int16_t*,int16_t*,const int16_t*);
extern "C" void resampleReference(int16_t*,uint16_t,uint16_t,uint16_t,uint8_t,uint16_t,int16_t*);
extern "C" const int16_t* productionTable();
extern "C" void mappedFilterSetupReference(int16_t*,unsigned,const int16_t*);
extern "C" void mappedFilterProcessReference(int16_t*,unsigned,int16_t*,unsigned);
using namespace ResidentDsp;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Transport {
    Teakra::Teakra& dsp;bool event=false,failFlush=false,failStateRead=false;unsigned writes=0,failReadBank=0x6400;
    explicit Transport(Teakra::Teakra& d):dsp(d){dsp.SetRecvDataHandler(0,[this](){event=true;});}
    uint16_t read(unsigned a){return dsp.DataRead(a);}
    void write(unsigned a,uint16_t v){++writes;dsp.DataWrite(a,v);}
    bool flush(unsigned a,unsigned n){return !failFlush && a<=0x7420 && n<=0x7420-a;}
    bool invalidate(unsigned a,unsigned n){return !(failStateRead && a>=failReadBank) && a<=0x7420 && n<=0x7420-a;}
    Receive tryReceive(uint16_t& v){if(!event)return Receive::Pending;if(!dsp.RecvDataIsReady(0))return Receive::Error;v=dsp.RecvData(0);event=false;return Receive::Received;}
};
int main(){
    using Tx=StateTransaction;using Runner=ChunkRunner<Transport>;
    // Pointer identity, unchanged baseline, fixed bank capacity and alias refusal.
    {
        Tx tx;std::array<std::array<int16_t,16>,65> states{};unsigned slot=99,again=99;
        for(unsigned i=0;i<64;++i)require(tx.bind(Tx::Kind::Resample,states[i].data(),slot) && slot==i,"state capacity");
        require(tx.bind(Tx::Kind::Resample,states[0].data(),again) && again==0,"same state reuses slot");
        require(!tx.bind(Tx::Kind::Resample,states[64].data(),slot) && tx.resetBeforeSubmit(),"overflow before publication");
        require(tx.bind(Tx::Kind::Adpcm,states[0].data(),slot) && !tx.bind(Tx::Kind::Filter,states[0].data(),again),"cross bank alias rejected");
        require(tx.resetBeforeSubmit() && tx.bind(Tx::Kind::Adpcm,states[0].data(),slot) && !tx.loop(states[0].data(),again),"loop mutable alias rejected");
        require(tx.resetBeforeSubmit() && tx.bind(Tx::Kind::Adpcm,states[0].data(),slot),"baseline setup");states[0][0]=1;
        require(!tx.bind(Tx::Kind::Adpcm,states[0].data(),again),"producer mutation needs fallback");
        require(tx.resetBeforeSubmit() && tx.bind(Tx::Kind::Adpcm,states[0].data(),slot) && !tx.bind(Tx::Kind::Adpcm,states[0].data()+1,again),"partial state alias rejected");
    }
    for(unsigned isSave=0;isSave<2;++isSave){
        Tx tx;ChunkCapture c;std::array<int16_t,16> state{};unsigned slot;
        require(tx.bind(Tx::Kind::Filter,state.data(),slot),"alias state");
        require(isSave?c.save(0x3c0,state.data(),32):c.load(state.data(),0x3c0,32),"alias entry");
        require(c.seal() && !tx.seal(c) && tx.resetBeforeSubmit(),"host command/state dependency rejects before stage");
    }
    {
        Tx tx;std::array<int16_t,128> table{};unsigned slot;
        for(unsigned i=0;i<16;++i){table[0]=i;require(tx.book(table.data(),slot) && slot==i,"book snapshots");}
        require(tx.book(table.data(),slot) && slot==15,"book content dedup");table[0]=16;
        require(!tx.book(table.data(),slot),"book capacity");
    }
    auto firmware=makeTypedFirmware(true,true,true,true);
    // Multiple completed chunks retain real filter histories, while failures
    // after DSP mutation withhold BOTH final PCM and host persistent state.
    for(unsigned fault=0;fault<3;++fault){
        Teakra::Teakra dsp({});for(unsigned i=0;i<firmware.words.size();++i)dsp.ProgramWrite(i,firmware.words[i]);
        dsp.Run(700);require(dsp.RecvDataIsReady(2),"boot");dsp.RecvData(2);
        Transport io(dsp);Session<Transport> session(io);require(session.attach(0x4458),"attach");
        ChunkCapture capture;Runner runner(io,session,capture);Tx tx;
        std::array<int16_t,16> stateA{},stateB{},resample{},adpcm{},loop{};
        std::array<int16_t,128> table{};table[5]=1234;
        std::array<int16_t,8> coeff={1000,-3000,4000,12000,-10000,2000,500,700};
        for(unsigned chunk=0;chunk<(fault?1u:4u);++chunk){
            std::array<int16_t,32> input{},output{};output.fill(0x5555);
            for(unsigned i=0;i<input.size();++i)input[i]=int(i*713+chunk*311)-13000;
            auto oldA=stateA,oldB=stateB,expectedA=stateA,expectedB=stateB;auto oldOutput=output;
            unsigned a,b,r,d,l,book;
            require(tx.bind(Tx::Kind::Filter,stateA.data(),a) && tx.bind(Tx::Kind::Filter,stateB.data(),b) &&
                    tx.bind(Tx::Kind::Resample,resample.data(),r) && tx.bind(Tx::Kind::Adpcm,adpcm.data(),d) &&
                    tx.loop(loop.data(),l) && tx.book(table.data(),book),"bind all banks");
            Command command;MappedParameters p;
            require(capture.load(input.data(),0x3c0,64) && lowerMappedFilterSetup(7,64,coeff,command,p) && capture.append(command,&p),"setup");
            for(unsigned slot:{a,b,a})require(lowerMappedFilter(7,0x3c0,slot,chunk?0:1,command,p) && capture.append(command,&p),"state reuse within batch");
            require(capture.save(0x3c0,output.data(),64) && capture.seal() && tx.seal(capture),"seal");
            if(fault==1)io.failFlush=true;
            bool staged=tx.stage(io,session);
            if(fault==1){require(!staged && !tx.commit(capture) && !tx.resetBeforeSubmit() && stateA==oldA && stateB==oldB && output==oldOutput,"partial staging fault ownership");break;}
            require(staged && dsp.DataRead(AdpcmBookWordBase+5)==1234,"stage states and books");
            std::vector<int16_t> expected(0x7500);std::memcpy(expected.data()+AudioDmemWordBase,input.data(),64);
            mappedFilterSetupReference(expected.data(),64,coeff.data());
            for(auto* state:{&expectedA,&expectedB,&expectedA})mappedFilterProcessReference(expected.data(),chunk?0:1,state->data(),AudioDmemWordBase*2);
            require(runner.start(10000) && !tx.collect(io,session,capture) && !tx.commit(capture),"no premature collection");
            uint64_t tick=0;
            while(runner.phase()!=Runner::Phase::Collected && tick<1000){if(runner.phase()==Runner::Phase::Waiting)dsp.Run(5000);runner.step(++tick);require(runner.phase()!=Runner::Phase::Fault,"runner success");}
            require(runner.phase()==Runner::Phase::Collected && stateA==oldA && stateB==oldB && output==oldOutput,"execution remains private");
            if(fault==2){io.failStateRead=true;io.failReadBank=FilterStateWordBase;}
            bool collected=tx.collect(io,session,capture);
            if(fault==2){require(!collected && !tx.commit(capture) && stateA==oldA && stateB==oldB && output==oldOutput,"state readback fault withholds all outputs");break;}
            require(collected && stateA==oldA && output==oldOutput,"readback remains private");
            require(tx.commit(capture) && stateA==expectedA && stateB==expectedB && !std::memcmp(output.data(),expected.data()+AudioDmemWordBase,64),"atomic PCM and persistent state commit");
            require(!tx.commit(capture) && tx.nextChunk() && capture.nextChunk(),"exactly once and next chunk");
        }
        dsp.SendData(2,0x8000);dsp.Run(700);require(dsp.RecvDataIsReady(2) && dsp.RecvData(2)==0,"shutdown");
        session.resetAfterStop();capture.resetAfterStop();tx.resetAfterStop();require(runner.resetAfterStop(),"reset after actual stop");
    }
    {
        Teakra::Teakra dsp({});for(unsigned i=0;i<firmware.words.size();++i)dsp.ProgramWrite(i,firmware.words[i]);
        dsp.Run(700);require(dsp.RecvDataIsReady(2),"decoder boot");dsp.RecvData(2);
        for(unsigned i=0;i<256;++i)dsp.DataWrite(0x4000+i,uint16_t(productionTable()[i]));
        for(unsigned i=0;i<16;++i)dsp.DataWrite(0x2800+i,uint16_t(1u<<i));
        Transport io(dsp);Session<Transport> session(io);require(session.attach(0x4458),"decoder attach");
        ChunkCapture capture;Runner runner(io,session,capture);Tx tx;
        std::array<int16_t,16> adpcm{},resample{},loop{};resample[6]=123;resample[7]=-456;
        for(unsigned i=0;i<16;++i)loop[i]=int(i*23)-145;
        const auto originalLoop=loop;
        std::array<int16_t,128> table{};for(unsigned i=0;i<128;++i)table[i]=int(i*13)-800;
        for(unsigned chunk=0;chunk<4;++chunk){
            unsigned decoderFlags=chunk==0?1:chunk==2?2:0;
            unsigned d,r,l,b;require(tx.bind(Tx::Kind::Adpcm,adpcm.data(),d) && tx.bind(Tx::Kind::Resample,resample.data(),r) && tx.loop(loop.data(),l) && tx.book(table.data(),b),"decode bindings");
            std::array<uint8_t,18> compressed{};for(unsigned i=0;i<18;++i)compressed[i]=uint8_t(i*31+chunk*19);compressed[0]=0x21;compressed[9]=0x32;
            std::array<int16_t,8> output{};output.fill(9876);auto oldOutput=output;
            auto oldD=adpcm,oldR=resample,expectedD=adpcm,expectedR=resample;
            Command c;MappedParameters p;
            require(capture.load(compressed.data(),0xdc0,18) && lowerMappedAdpcm(7,false,64,0xdc0,0x3c0,decoderFlags,d,l,b,c,p) && capture.append(c,&p),"decode capture");
            require(lowerMappedResample(7,16,0x3e0,0x500,0x4000,chunk?0:1,r,c,p) && capture.append(c,&p) && capture.save(0x500,output.data(),16) && capture.seal() && tx.seal(capture) && tx.stage(io,session),"resample capture/stage");
            std::vector<int16_t> expected(0x7500);std::memcpy(reinterpret_cast<uint8_t*>(expected.data()+AudioDmemWordBase)+0xa00,compressed.data(),18);
            adpcmReferenceAt(expected.data(),64,decoderFlags,AudioDmemWordBase*2+0xa00,AudioDmemWordBase*2,expectedD.data(),loop.data(),table.data());
            resampleReference(expected.data(),AudioDmemWordBase*2+32,AudioDmemWordBase*2+320,16,chunk?0:1,0x4000,expectedR.data());
            require(runner.start(10000),"decode start");uint64_t tick=0;
            while(runner.phase()!=Runner::Phase::Collected && tick<1000){if(runner.phase()==Runner::Phase::Waiting)dsp.Run(5000);runner.step(++tick);require(runner.phase()!=Runner::Phase::Fault,"decode runner");}
            require(runner.phase()==Runner::Phase::Collected && tx.collect(io,session,capture) && adpcm==oldD && resample==oldR && output==oldOutput,"decoder state tentative");
            require(tx.commit(capture),"decoder commit");
            require(adpcm==expectedD,"decoder CPU state");require(resample==expectedR,"resample CPU state");
            require(!std::memcmp(output.data(),expected.data()+AudioDmemWordBase+160,16),"decoder/resample CPU PCM");
            require(loop==originalLoop,"loop host immutable");
            for(unsigned i=0;i<16;++i)require(dsp.DataRead(AdpcmLoopWordBase+l*16+i)==uint16_t(loop[i]),"loop DSP immutable");
            for(unsigned i=0;i<128;++i)require(dsp.DataRead(AdpcmBookWordBase+b*128+i)==uint16_t(table[i]),"book DSP immutable");
            require(tx.nextChunk() && capture.nextChunk(),"decoder next chunk");
        }
        dsp.SendData(2,0x8000);dsp.Run(700);require(dsp.RecvDataIsReady(2) && dsp.RecvData(2)==0,"decoder shutdown");
    }
    std::printf("PASS: state identity/capacity/alias guards, book snapshots, four filter and four ADPCM/resample persistent chunks versus production PCM/state, loop/book immutability, all resource banks, staging/readback failure withholding and stop recovery; %zu transaction bytes\n",sizeof(Tx));
}
