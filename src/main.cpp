// SPDX-License-Identifier: GPL-3.0-or-later
#include "iq151/app.hpp"
#include <iostream>
#include <stdexcept>
#include <fstream>
namespace iq151 {
void screenshot(const Machine& m,const std::string& path) {
    auto pixels=m.render();std::ofstream f(path.c_str(),std::ios::binary);
    if(!f) throw std::runtime_error("Cannot create "+path);
    f<<"P6\n560 544\n255\n";
    for(int y=0;y<544;++y) for(int x=0;x<560;++x) {uint32_t c=pixels[(y/2)*560+x];f.put(char(c>>16));f.put(char(c>>8));f.put(char(c));}
    if(!f) throw std::runtime_error("Cannot write "+path);
}
}
int main(int argc,char** argv) {
    using namespace iq151;
    try {
        Options o;
        const auto number=[](const std::string& s){std::size_t n;int v=std::stoi(s,&n,0);if(n!=s.size()) throw std::runtime_error("Invalid number: "+s);return v;};
        for(int i=1;i<argc;++i) {
            std::string a=argv[i];
            if(a=="--module"||a=="--monitor"||a=="--video"||a=="--ram"||a=="--no-grafik"||a=="--audio"||a=="--smart-keyboard"||a=="--sdrom"||a=="--sd-autorun"||a=="--amos-autorun"||a=="--fel-autorun")o.configExplicit=true;
            const auto value=[&](){if(i+1>=argc) throw std::runtime_error("Missing value for "+a);return std::string(argv[++i]);};
            if(a=="--help"||a=="-h") {
                std::cout<<"IQ-151 C++11 / SDL2\nUsage: iq151 [options]\n"
                    "  --headless --frames N       Run deterministic emulation without SDL\n"
                    "  --module none|basic6|basicg|amos   --monitor std|dis|kom|fel\n"
                    "  --video 32|64 --ram 32|64  --no-grafik --audio --smart-keyboard\n"
                    "  --load FILE --address 0xNNNN --pc 0xNNNN  (binary or Intel HEX)\n"
                    "  --snapshot FILE.isn --save-state FILE.isn\n"
                    "  --tape FILE.csw|FILE.wav --disk-a FILE.IQD --disk-b FILE.IQD\n"
                    "  --sd-root DIR --sdrom --sd-autorun --amos-autorun --fel-autorun\n"
                    "  --screenshot FILE.ppm --expect TEXT --portable --ui-smoke --window-smoke\n";return 0;
            }
            if(a=="--headless") o.headless=true;
            else if(a=="--ui-smoke") o.smoke=true;
            else if(a=="--window-smoke") o.smoke=o.windowSmoke=true;
            else if(a=="--portable") o.portable=true;
            else if(a=="--frames") {o.frames=number(value());if(o.frames<1||o.frames>10000000) throw std::runtime_error("Invalid frame count");}
            else if(a=="--module") {std::string v=value();if(v=="none") o.config.main=0;else if(v=="basic6") o.config.main=1;else if(v=="basicg") o.config.main=2;else if(v=="amos") o.config.main=3;else throw std::runtime_error("Unknown module");}
            else if(a=="--monitor") {std::string v=value();if(v=="std") o.config.monitor=10;else if(v=="dis") o.config.monitor=11;else if(v=="kom") o.config.monitor=12;else if(v=="fel") o.config.monitor=13;else throw std::runtime_error("Unknown monitor");}
            else if(a=="--video") {int v=number(value());if(v!=32&&v!=64) throw std::runtime_error("Video must be 32 or 64");o.config.video=v==64;}
            else if(a=="--ram") {int v=number(value());if(v!=32&&v!=64) throw std::runtime_error("RAM must be 32 or 64");o.config.mem64=v==64;}
            else if(a=="--no-grafik") o.config.grafik=false;
            else if(a=="--audio") o.config.audio=true;
            else if(a=="--smart-keyboard") o.config.smartkeyboard=true;
            else if(a=="--sdrom") o.config.sdrom=true;
            else if(a=="--sd-autorun") o.config.sdAutorun=o.config.sdrom=true;
            else if(a=="--amos-autorun") o.config.amosAutorun=true;
            else if(a=="--fel-autorun") o.config.felAutorun=true;
            else if(a=="--sd-root") o.sdRoot=value();
            else if(a=="--load") o.input=value();
            else if(a=="--address") o.address=number(value());
            else if(a=="--pc") {o.pc=number(value());if(o.pc<0||o.pc>65535) throw std::runtime_error("PC outside address space");}
            else if(a=="--snapshot") o.snapshot=value();
            else if(a=="--save-state") o.state=value();
            else if(a=="--tape") o.tape=value();
            else if(a=="--disk-a") {o.diskA=value();o.config.disc2=true;}
            else if(a=="--disk-b") {o.diskB=value();o.config.disc2=true;}
            else if(a=="--expect") o.expect=value();
            else if(a=="--screenshot") o.screenshot=value();
            else throw std::runtime_error("Unknown option: "+a);
        }
        if(!o.headless) {
#ifdef IQ151_GUI
            return frontend(o);
#else
            throw std::runtime_error("Built without SDL frontend; use --headless");
#endif
        }
        Machine m(o.config,o.sdRoot);
        if(!o.snapshot.empty()) m.loadSnapshot(o.snapshot);
        if(!o.input.empty()) {if(o.input.size()>=4&&o.input.substr(o.input.size()-4)==".hex") m.loadHex(o.input);else m.loadBinary(o.input,o.address);}
        if(!o.tape.empty()) m.tape.open(o.tape);
        if(!o.diskA.empty()) m.floppy.disks[0].open(o.diskA,false);
        if(!o.diskB.empty()) m.floppy.disks[1].open(o.diskB,false);
        if(o.pc>=0) {m.memory.setBootstrap(false);m.cpu.setRegPC(o.pc);}
        for(int f=0;f<o.frames;++f) m.frame();
        std::string text=m.screenText();std::cout<<text;
        std::cerr<<"PC="<<std::hex<<m.cpu.getRegPC()<<std::dec<<" T="<<m.clock.tstates<<" frames="<<o.frames<<'\n';
        if(!o.screenshot.empty()) screenshot(m,o.screenshot);
        if(!o.state.empty()) m.saveSnapshot(o.state);
        if(!o.expect.empty()&&text.find(o.expect)==std::string::npos) throw std::runtime_error("Expected screen text not found: "+o.expect);
        return 0;
    } catch(const std::exception& e) {std::cerr<<"iq151: "<<e.what()<<'\n';return 1;}
}
