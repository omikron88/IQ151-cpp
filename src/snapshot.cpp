// SPDX-License-Identifier: GPL-3.0-or-later
#include "iq151/machine.hpp"
#include <algorithm>
#include <stdexcept>
namespace iq151 {
void Machine::saveSnapshot(const std::string& path) const {
    // Getters preserve the Java emulator's public CPU representation.
    I8085& c=const_cast<I8085&>(cpu);Pio8255& p=const_cast<Pio8255&>(pio);
    std::vector<uint8_t> d={'I','S','N',1};
    const auto byte=[&](int v){d.push_back(uint8_t(v));};
    const auto word=[&](int v){byte(v);byte(v>>8);};
    auto cfg=config.bytes();d.insert(d.end(),cfg.begin(),cfg.end());
    int af=c.getRegAF();byte(af>>8);byte(af);byte(c.getRegB());byte(c.getRegC());
    byte(c.getRegD());byte(c.getRegE());byte(c.getRegH());byte(c.getRegL());
    word(c.getRegSP());word(c.getRegPC());word(c.getMemPtr());byte(c.getRegSim());
    byte((c.isPendingEI()?1:0)|(c.isActiveInt()?2:0)|(c.isHalted()?4:0));
    byte(port80);byte(memory.bootstrap);byte(memory.amosBank);byte(memory.discMounted);
    byte((khz1?1:0)|(tapein?2:0)|(tapeo?4:0)|(tapeout?8:0)|(tapestart?16:0)|(tape.inverted?32:0));byte(0);
    for(int v:p.getState()) byte(v);
    for(int v:pic.s) byte(v);
    byte(config.grafik);
    if(config.grafik) {
        byte(grafik.d0);byte(grafik.d1);byte(grafik.d2);
        byte((grafik.enabled?1:0)|(grafik.show?2:0)|(grafik.bitAccess?4:0)|(grafik.pen?8:0));
        d.insert(d.end(),grafik.ram.begin(),grafik.ram.end());
    }
    d.insert(d.end(),memory.ram.begin(),memory.ram.end());d.insert(d.end(),memory.vram.begin(),memory.vram.end());
    writeFile(path,d.data(),d.size());
}
void Machine::loadSnapshot(const std::string& path) {
    std::vector<uint8_t> d=readFile(path);
    if(d.size()<53||d[0]!='I'||d[1]!='S'||d[2]!='N'||d[3]!=1) throw std::runtime_error("Not an ISN v1 snapshot");
    auto cfg=config.bytes();
    for(int i=0;i<10;++i) if(i!=8&&cfg[i]!=d[4+i]) throw std::runtime_error("Snapshot modules differ: select the same machine settings first");
    if(d.size()!=std::size_t(config.grafik?84025:67637)||d[52]!=int(config.grafik)||
        d[50]>3||d[51]>2||(d[32]!=255&&d[32]>3)||d[31]>1||d[33]>1)
        throw std::runtime_error("Truncated or invalid snapshot");
    // Validate the complete stream before changing any machine state.
    std::size_t n=14;
    const auto byte=[&](){return int(d[n++]);};
    const auto word=[&](){int lo=byte();return lo|(byte()<<8);};
    memory.reset(false);cpu.reset();
    int a=byte(),f=byte();cpu.setRegAF((a<<8)|f);
    cpu.setRegB(byte());cpu.setRegC(byte());cpu.setRegD(byte());cpu.setRegE(byte());cpu.setRegH(byte());cpu.setRegL(byte());
    cpu.setRegSP(word());cpu.setRegPC(word());cpu.setMemPtr(word());cpu.setRegSim(byte());int flags=byte();
    cpu.setPendingEI(flags&1);cpu.setHalted(flags&4);
    port80=byte();bool boot=byte()!=0;int bank=byte();bool mounted=byte()!=0;int tf=byte();byte();
    std::array<int,5> ps;for(int& v:ps) v=byte();pio.setState(ps);
    std::array<int,11> is;for(int& v:is) v=byte();pic.restore(is);cpu.setActiveInt(flags&2);
    memory.setBootstrap(boot);if(bank!=255) memory.switchAmos(bank);if(mounted) memory.mountDisc();
    khz1=tf&1;tapein=tf&2;tapeo=tf&4;tapeout=tf&8;tapestart=tf&16;tape.inverted=tf&32;
    byte();
    if(config.grafik) {
        grafik.d0=byte();grafik.d1=byte();grafik.d2=byte();int gf=byte();
        grafik.enabled=gf&1;grafik.show=gf&2;grafik.bitAccess=gf&4;grafik.pen=gf&8;
        std::copy(d.begin()+n,d.begin()+n+16384,grafik.ram.begin());n+=16384;
    }
    std::copy(d.begin()+n,d.begin()+n+65536,memory.ram.begin());n+=65536;
    std::copy(d.begin()+n,d.end(),memory.vram.begin());
    cancelPaste();keyboard.reset();audio.clear();audioPhase=0;nextHalf=clock.tstates+1024;autoPending=false;
}
}
