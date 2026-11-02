// SPDX-License-Identifier: GPL-3.0-or-later
#include "iq151/machine.hpp"
#include "iq151/files.hpp"
#include "iq151/preferences.hpp"
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <memory>
#include <cstring>
using namespace iq151;
namespace {
int checks=0;
void require(bool condition,const std::string& message) {++checks;if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char* message){bool bad=false;try{f();}catch(const std::exception&){bad=true;}require(bad,message);}
uint64_t hash=0;
void byteHash(int b){hash=(hash^uint8_t(b))*UINT64_C(0x100000001b3);}
void wordHash(int w){byteHash(w);byteHash(w>>8);}
uint64_t imageHash(const Machine& m){hash=UINT64_C(0xcbf29ce484222325);for(uint32_t p:m.render())for(int i=0;i<4;++i)byteHash(p>>(8*i));return hash;}
struct Bus:MemIoOps,NotifyOps {
    Clock clock;std::array<uint8_t,65536> mem{};int output=0;
    int fetchOpcode(int a)override{clock.addTstates(4);return mem[a&65535];}
    int peek8(int a)override{clock.addTstates(3);return mem[a&65535];}
    int peek16(int a)override{clock.addTstates(6);return mem[a&65535]|(mem[(a+1)&65535]<<8);}
    void poke8(int a,int v)override{clock.addTstates(3);mem[a&65535]=uint8_t(v);}
    void poke16(int a,int v)override{clock.addTstates(6);mem[a&65535]=uint8_t(v);mem[(a+1)&65535]=uint8_t(v>>8);}
    int inPort(int p)override{clock.addTstates(4);return(p^0x5a)&255;}
    void outPort(int p,int v)override{clock.addTstates(4);output=((p&255)<<8)|(v&255);}
};
void cpuOracle() {
    std::ifstream oracle(IQ151_FIXTURES "/cpu-java.txt");require(bool(oracle),"CPU reference fixture missing");
    for(int op=0;op<256;++op) {
        hash=UINT64_C(0xcbf29ce484222325);
        for(int t=0;t<32;++t) {
            uint32_t random=0x12345678u+unsigned(op)*65537u+unsigned(t);
            const auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
            std::unique_ptr<Bus> b(new Bus);std::unique_ptr<I8085> cpu(new I8085(b->clock,*b,*b));auto& c=*cpu;
            for(auto& v:b->mem)v=uint8_t(next());
            c.setRegA(int(next()));c.setFlags(int(next()&255));c.setRegBC(int(next()));c.setRegDE(int(next()));c.setRegHL(int(next()));
            c.setRegSP(t<2?(t==0?0xffff:0):int(next()));c.setRegPC(0x2345);c.setMemPtr(int(next()));
            b->mem[0x2345]=uint8_t(op);c.executeOne();
            wordHash(c.getRegAF());byteHash(c.getFlags());wordHash(c.getRegBC());wordHash(c.getRegDE());wordHash(c.getRegHL());
            wordHash(c.getRegSP());wordHash(c.getRegPC());wordHash(c.getMemPtr());byteHash(c.getRegSim());byteHash(c.isPendingEI());byteHash(c.isHalted());
            wordHash(int(b->clock.tstates));wordHash(b->output);for(uint8_t v:b->mem)byteHash(v);
        }
        unsigned expectedOp;uint64_t expected;oracle>>std::hex>>expectedOp>>expected;
        std::ostringstream msg;msg<<"Java opcode "<<std::hex<<op<<": expected "<<expected<<", got "<<hash;
        require(bool(oracle)&&expectedOp==unsigned(op)&&hash==expected,msg.str());
    }
    std::cout<<"CPU: 8192 instruction cases match Java, including flags, timing and memory\n";
}
void preferences() {
    makeDirectory("preferences-test");
    Preferences p;p.dir="preferences-test";p.config.main=1;p.config.video=1;p.color=2;
    p.config.sdRoot="C:\\IQ 151 data\\SD-ROM";p.save();
    Preferences loaded;loaded.dir=p.dir;loaded.load();
    require(loaded.config.sdRoot==p.config.sdRoot&&loaded.config.main==1&&loaded.config.video==1&&loaded.color==2,"Preferences retain SD folder with spaces and Windows backslashes");
    require(loaded.sdRootFor("override root")=="override root","CLI SD folder overrides saved setting");
    require(loaded.sdRootFor("")==p.config.sdRoot,"Saved SD folder overrides default");
    std::string text="sdRoot /some folder/with spaces\r\nmodule 3\r\nvideo 1\r\ncolor 1\r\n";
    writeFile(p.dir+"/iq151.ini",reinterpret_cast<const uint8_t*>(text.data()),text.size());
    Preferences reordered;reordered.dir=p.dir;reordered.load();
    require(reordered.config.sdRoot=="/some folder/with spaces"&&reordered.config.main==3&&reordered.color==1,"Path line does not stop numeric preference parsing");
    text="module 1\nunknown ignored value\nvideo 1\n";
    writeFile(p.dir+"/iq151.ini",reinterpret_cast<const uint8_t*>(text.data()),text.size());
    Preferences legacy;legacy.dir=p.dir;legacy.load();
    require(legacy.config.main==1&&legacy.config.video==1&&legacy.sdRootFor("")==p.dir+"/SDRoot","Legacy configuration keeps default SD folder");
    auto original=readFile(p.dir+"/iq151.ini");p.config.sdRoot="bad\npath";
    rejects([&]{p.save();},"Multiline SD folder rejected");
    require(original==readFile(p.dir+"/iq151.ini"),"Invalid folder does not truncate preferences");
    std::remove((p.dir+"/iq151.ini").c_str());
    Config config;config.sdRoot="from Config";
    std::unique_ptr<Machine> configured(new Machine(config));
    require(configured->sd.root==config.sdRoot,"Machine constructor uses Config SD folder");
    std::unique_ptr<Machine> override(new Machine(config,"constructor override"));
    require(override->config.sdRoot=="constructor override"&&override->sd.root=="constructor override","Explicit constructor SD folder retains precedence");
}
void memoryAndInterrupts() {
    Config c;Memory m(c);m.setBootstrap(true);int boot=m.read(0);m.write(0,42);require(m.read(0)==boot,"bootstrap ROM is read-only");m.setBootstrap(false);require(m.read(0)==42,"bootstrap writes reach underlying RAM");
    int rom=m.read(0xf800);m.write(0xf800,rom^255);require(m.read(0xf800)==rom,"monitor ROM is read-only");
    m.write(0x8000,33);require(m.read(0x8000)==255,"unmapped 32 KB memory");
    m.write(0xec00,55);require(m.vram[0]==55,"Video32 mapping");
    c.video=1;c.mem64=true;c.main=3;m.reset(false);m.write(0xe800,91);m.write(0xec00,92);require(m.vram[0]==91&&m.vram[1024]==92,"Video64 mapping");
    for(int bank=0;bank<3;++bank){m.switchAmos(bank);const char* names[]={"Pascal.rom","Pascal1.rom","Assembler.rom"};require(m.read(0x8000)==asset(names[bank]).data[0],"AMOS ROM bank");}
    m.switchAmos(3);m.write(0x8000,71);require(m.read(0x8000)==71,"AMOS RAM bank");m.mountDisc();require(m.read(0xe000)==asset("Disc2.rom").data[0],"Disk2 ROM mount");
    Bus b;I8085 cpu(b.clock,b,b);Pic pic(cpu);
    pic.out(0,0x12);pic.out(1,0x34);pic.out(1,0);pic.irq(5);pic.irq(6);
    require(cpu.isActiveInt()&&pic.ack()==0xcd&&pic.ack()==0x3430,"8259 priority and CALL acknowledge");
    pic.out(0,0x20);require(pic.ack()==0xcd&&pic.ack()==0x3428,"8259 EOI enables lower priority request");
    cpu.reset();b.mem[0]=0xfb;b.mem[1]=0;cpu.setActiveInt(true);cpu.executeOne();require(cpu.isPendingEI(),"EI delayed");cpu.executeOne();require(!cpu.isPendingEI()&&cpu.getRegPC()==2,"EI executes following instruction first");
    cpu.reset();b.mem[0]=0x76;cpu.executeOne();require(cpu.isHalted()&&cpu.getRegPC()==0,"HLT pins PC");cpu.doTrap();cpu.executeOne();require(!cpu.isHalted()&&cpu.getRegPC()==0x24,"TRAP wakes HALT");
    Keyboard k;k.key(2,0,true);require(k.portA(0xfb)==254&&k.portB(0xfe)==0xfb,"bidirectional keyboard scan");k.reset();require(k.character('@'),"smart keyboard @");require(k.row[7]==253&&!k.shift,"host character maps to IQ matrix");
    Grafik g;g.d0=3;g.d1=9;g.write(0x81);require(g.pixel(24,246)&&g.pixel(31,246),"Grafik orientation");g.control(0);g.write(32);require(g.read()==0x83,"Grafik bit set");g.control(2);g.write(32);require(g.read()==0x81,"Grafik bit erase");
}
void bootAndVideo() {
    std::ifstream hashes(IQ151_FIXTURES "/video-java.txt");require(bool(hashes),"Java video fixture missing");
    for(int main=0;main<4;++main)for(int video=0;video<2;++video) {
        Config c;c.main=main;c.video=video;std::unique_ptr<Machine> p(new Machine(c));Machine& m=*p;
        for(int i=0;i<120;++i)m.frame();
        std::string fixture=std::string(IQ151_FIXTURES)+"/boot-"+char('0'+main)+"-"+char('0'+video)+".isn";
        auto expected=readFile(fixture);
        require(std::equal(expected.end()-67584,expected.end()-2048,m.memory.ram.begin()),"Boot RAM matches Java, module "+std::to_string(main)+" video "+std::to_string(video));
        require(std::equal(expected.end()-2048,expected.end(),m.memory.vram.begin()),"Boot VRAM matches Java");
        require(m.cpu.getRegPC()==int(expected[24]|(expected[25]<<8)),"Boot PC matches Java");
        std::string kind;int module,v;uint64_t h;hashes>>kind>>module>>v>>std::hex>>h>>std::dec;
        require(bool(hashes)&&kind=="boot"&&module==main&&v==video&&imageHash(m)==h,"Boot pixels match Java");
        m.saveSnapshot("roundtrip.isn");auto own=readFile("roundtrip.isn");
        std::fill(m.memory.ram.begin(),m.memory.ram.end(),0);m.loadSnapshot("roundtrip.isn");m.saveSnapshot("roundtrip-2.isn");
        require(own==readFile("roundtrip-2.isn"),"ISN round-trip is byte-exact");
        m.loadSnapshot(fixture);m.saveSnapshot("roundtrip-2.isn");require(readFile(fixture)==readFile("roundtrip-2.isn"),"Java ISN interoperability");
    }
    for(int video=0;video<2;++video)for(int wide=0;wide<2;++wide) {
        Config c;c.video=video;c.v64ena32=wide!=0;std::unique_ptr<Machine> p(new Machine(c));auto& m=*p;
        for(int i=0;i<2048;++i)m.memory.vram[i]=uint8_t(i*37+(i>>5));
        for(int i=0;i<2048;i+=31)m.memory.vram[i]=127;
        for(int x=0;x<64;++x)for(int y=0;y<256;++y)m.grafik.ram[x*256+y]=uint8_t(x*13+y*7);
        if(wide) { m.config.v64ena32=false;m.render();m.config.v64ena32=true; }
        std::string kind;int v,w;uint64_t h;hashes>>kind>>v>>w>>std::hex>>h>>std::dec;
        std::ostringstream msg;auto actual=imageHash(m);msg<<"Video pixels match Java "<<video<<" "<<wide<<": expected "<<std::hex<<h<<", got "<<actual;
        require(bool(hashes)&&kind=="video"&&v==video&&w==wide&&actual==h,msg.str());
    }
    std::remove("roundtrip.isn");std::remove("roundtrip-2.isn");
    std::cout<<"Boot/video: 8 ROM configurations, 4 graphics patterns and Java snapshots verified\n";
}
void filesAndMedia() {
    std::unique_ptr<Machine> p(new Machine);auto& m=*p;for(int i=0;i<120;++i)m.frame();
    m.paste("PRINT 123\n");for(int i=0;i<180;++i)m.frame();require(m.screenText().find("\n 123\n")!=std::string::npos,"BASIC executes PRINT via keyboard input");
    auto text=m.screenText();std::cout<<"BASIC input: "<<text.substr(0,140)<<'\n';
    m.saveSnapshot("test.isn");auto valid=readFile("test.isn"),bad=valid;bad.resize(57);writeFile("bad.isn",bad.data(),bad.size());auto ram=m.memory.ram;
    rejects([&]{m.loadSnapshot("bad.isn");},"truncated ISN rejected");require(m.memory.ram==ram,"failed snapshot leaves machine unchanged");
    bad=valid;bad[4]=0;writeFile("bad.isn",bad.data(),bad.size());rejects([&]{m.loadSnapshot("bad.isn");},"incompatible snapshot rejected");
    Config plain;plain.grafik=false;Machine noGraf(plain);noGraf.saveSnapshot("plain.isn");require(readFile("plain.isn").size()==67637,"ISN without Grafik size");noGraf.loadSnapshot("plain.isn");
    std::string hex=":0401000001020304F1\n:00000001FF\n";writeFile("test.hex",reinterpret_cast<const uint8_t*>(hex.data()),hex.size());m.loadHex("test.hex");require(m.memory.ram[0x100]==1&&m.memory.ram[0x103]==4,"Intel HEX load");
    hex[18]='0';writeFile("bad.hex",reinterpret_cast<const uint8_t*>(hex.data()),hex.size());rejects([&]{m.loadHex("bad.hex");},"HEX checksum checked");
    m.saveBinary("test.bin",0x100,0x103);m.loadBinary("test.bin",0x200);require(m.memory.ram[0x200]==1&&m.memory.ram[0x203]==4,"binary round-trip");rejects([&]{m.loadBinary("test.bin",65534);},"binary bounds checked");
    Tape out;out.save("test.csw");out.record=true;out.advance(2048000/10,true,false);out.advance(2048000/10,true,true);out.close();Tape in;in.open("test.csw");require(!in.advance(2048,true,false),"CSW first polarity");require(in.advance(2048000/10,true,false),"CSW transition");
    std::vector<uint8_t> wav(48,0);std::memcpy(wav.data(),"RIFF",4);wav[4]=40;std::memcpy(wav.data()+8,"WAVEfmt ",8);wav[16]=16;wav[20]=1;wav[22]=1;wav[24]=0x22;wav[25]=0x56;wav[28]=0x22;wav[29]=0x56;wav[32]=1;wav[34]=8;std::memcpy(wav.data()+36,"data",4);wav[40]=4;wav[44]=255;wav[45]=0;wav[46]=255;wav[47]=0;
    writeFile("test.wav",wav.data(),wav.size());in.open("test.wav");require(in.advance(94,true,false),"PCM WAV high sample");require(!in.advance(94,true,false),"PCM WAV low sample");
    std::vector<uint8_t> image(256256);for(std::size_t i=0;i<image.size();++i)image[i]=uint8_t(i);writeFile("test.iqd",image.data(),image.size());Floppy f;f.reset();f.disks[0].open("test.iqd",false);
    int read[]={6,1,0,0,1,0,1,0,128};for(int v:read)f.out(v);require(f.in(0xaa)==240,"8272 read execution phase");for(int i=0;i<128;++i)require(f.in(0xab)==i,"8272 sector contents");require(f.in(0xaa)==208,"8272 result phase");for(int i=0;i<7;++i)f.in(0xab);require(f.in(0xaa)==128,"8272 returns ready");
    read[0]=5;for(int v:read)f.out(v);require((f.in(0xab)&64)!=0&&f.in(0xab)==2,"8272 write-protect result");while(f.status&64)f.in(0xab);
    f.disks[0].writable=true;for(int v:read)f.out(v);for(int i=0;i<128;++i)f.out(0xa5);require(readFile("test.iqd")[0]==0xa5&&readFile("test.iqd")[128]==128,"8272 writes selected sector only");
    f.reset();read[0]=6;read[2]=255;for(int v:read)f.out(v);require((f.in(0xab)&64)!=0,"8272 invalid track bounded");
    for(const char* file:{"test.isn","bad.isn","plain.isn","test.hex","bad.hex","test.bin","test.csw","test.wav","test.iqd"})std::remove(file);
}
void soundIntegration() {
    Config c;c.audio=true;std::unique_ptr<Machine> p(new Machine(c));auto& m=*p;
    m.memory.setBootstrap(false);
    const uint8_t program[]={0x3e,8,0xd3,0x86,0x3e,0,0xd3,0x86,0xc3,0,0};
    std::copy(program,program+sizeof(program),m.memory.ram.begin());m.frame();
    require(m.audio.size()>870&&m.audio.size()<900,"Sound uses emulated clock at 44100 Hz");
    require(std::find(m.audio.begin(),m.audio.end(),6000)!=m.audio.end()&&std::find(m.audio.begin(),m.audio.end(),-6000)!=m.audio.end(),"PC3 produces both speaker levels");
}
void tapeIntegration() {
    std::unique_ptr<Machine> p(new Machine);auto& m=*p;
    for(int i=0;i<120;++i)m.frame();
    m.tape.open(IQ151_FIXTURES "/hello.bas.csw");m.paste("MLOAD\n");
    for(int i=0;i<700;++i)m.frame();
    require(!m.tapestart,"MLOAD completes and stops tape motor");
    m.paste("RUN\n");for(int i=0;i<300;++i)m.frame();
    require(m.screenText().find("\nTAPE OK\n")!=std::string::npos,"BASIC program loaded from CSW executes");
    std::cout<<"Tape: MLOAD from CSW and RUN verified\n";
}
void sdIntegration() {
    makeDirectory("sd-test");
    // Keep this root isolated so the manager's first item is always TEST.BAS.
    std::string basic="10 PRINT \"SD OK\"\r\n20 END\r\n";
    writeFile("sd-test/TEST.BAS",reinterpret_cast<const uint8_t*>(basic.data()),basic.size());
    Config c;c.sdrom=c.sdAutorun=true;c.sdRoot="sd-test";
    std::unique_ptr<Machine> p(new Machine(c));auto& m=*p;
    for(int i=0;i<600;++i)m.frame();
    require(m.screenText().find("IQ 151 SD-ROM Manager")!=std::string::npos,"SD-ROM bootstrap executes embedded loader");
    require(m.screenText().find("TEST.BAS")!=std::string::npos,"SD-ROM directory is transferred through 8255 handshake");
    m.keyboard.key(4,4,true);for(int i=0;i<8;++i)m.frame();m.keyboard.reset();
    for(int i=0;i<1000;++i)m.frame();
    require(m.screenText().find("10 PRINT")!=std::string::npos,"SD-ROM file transferred to BASIC");
    m.paste("RUN\n");for(int i=0;i<300;++i)m.frame();
    require(m.screenText().find("\nSD OK\n")!=std::string::npos,"Program loaded via SD-ROM executes");
    makeDirectory("sd-test second folder");
    std::string other="10 PRINT \"SECOND ROOT\"\r\n20 END\r\n";
    writeFile("sd-test second folder/SECOND.BAS",reinterpret_cast<const uint8_t*>(other.data()),other.size());
    m.config.sdRoot=absolutePath("sd-test second folder");m.reset();
    for(int i=0;i<600;++i)m.frame();
    require(m.sd.root==m.config.sdRoot&&m.screenText().find("SECOND.BAS")!=std::string::npos&&m.screenText().find("TEST.BAS")==std::string::npos,"Reset switches SD-ROM to configured folder");
    m.keyboard.key(4,4,true);for(int i=0;i<8;++i)m.frame();m.keyboard.reset();
    for(int i=0;i<1000;++i)m.frame();m.paste("RUN\n");for(int i=0;i<300;++i)m.frame();
    require(m.screenText().find("\nSECOND ROOT\n")!=std::string::npos,"BASIC loads and runs from newly configured SD folder");
    std::remove("sd-test second folder/SECOND.BAS");std::remove("sd-test second folder/__BLOADER.BAS");
    std::string target=absolutePath("sd-test/TEST.BAS");
#ifdef _WIN32
    require(sdPath(target.substr(0,3),target.substr(3))==target,"SD folder can be the Windows drive root");
#else
    require(sdPath("/",target)==target,"SD folder can be the filesystem root");
#endif
    // Simulate the 8255 mode-1 output protocol used by the Java ATmega save interrupt.
    SDRom sd("sd-test");sd.reset();
    sd.pio.CpuWrite(3,14);sd.control(14);sd.pio.CpuWrite(3,13);sd.control(13);
    std::string saved="10 PRINT 1\r\n\x1b\r\n",stream=saved+std::string(64,'\0');
    for(unsigned char v:stream){sd.pio.CpuWrite(1,v);sd.poll();sd.poll();}
    auto data=readFile("sd-test/save001.bas");
    require(std::string(data.begin(),data.end())==saved,"SD-ROM saves BASIC through handshake and detects terminator");
    rejects([&]{sdPath("sd-test","missing.bas");},"Missing SD file diagnosed");
    require(sdPath("sd-test","../../")==absolutePath("sd-test"),"SD paths stay rooted");
    for(const char* name:{"TEST.BAS","save001.bas","__BLOADER.BAS"})std::remove((std::string("sd-test/")+name).c_str());
    std::cout<<"SD-ROM: bootstrap, directory, BASIC load/run and BASIC save verified\n";
}

}
int main() {
    try {cpuOracle();preferences();memoryAndInterrupts();bootAndVideo();filesAndMedia();soundIntegration();tapeIntegration();sdIntegration();std::cout<<"PASS: "<<checks<<" checks\n";return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
