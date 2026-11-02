// SPDX-License-Identifier: GPL-3.0-or-later
// Hardware mapping and video logic ported from omikron88/iq-151 (see NOTICE).
#include "iq151/machine.hpp"
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <iterator>
namespace iq151 {
std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path.c_str(),std::ios::binary);
    if(!f) throw std::runtime_error("Cannot open " + path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    if(f.bad()) throw std::runtime_error("Cannot read " + path);
    return data;
}
void writeFile(const std::string& path,const uint8_t* bytes,std::size_t size) {
    std::ofstream f(path.c_str(),std::ios::binary|std::ios::trunc);
    if(!f || !f.write(reinterpret_cast<const char*>(bytes),size) || !f.flush())
        throw std::runtime_error("Cannot write " + path);
}
std::array<uint8_t,10> Config::bytes() const {
    return {{uint8_t(main),uint8_t(monitor),uint8_t(video),uint8_t(mem64),uint8_t(grafik),
        uint8_t(sdrom),uint8_t(disc2),uint8_t(audio),uint8_t(smartkeyboard),uint8_t(v64ena32)}};
}
void Memory::map(int first,int count,const uint8_t* rom) {
    for(int p=0;p<count;++p) { reads[first+p]=rom+p*1024; writes[first+p]=sink.data(); }
}
void Memory::reset(bool dirty) {
    if(cfg.main<0||cfg.main>3||cfg.monitor<10||cfg.monitor>13||cfg.video<0||cfg.video>1)
        throw std::runtime_error("Invalid machine configuration");
    if(dirty) ram.fill(255);
    unmapped.fill(255); amosBank=-1; bootstrap=false; discMounted=false;
    for(int p=0;p<64;++p) {
        reads[p]=p<(cfg.mem64?64:32) ? ram.data()+p*1024 : unmapped.data();
        writes[p]=p<(cfg.mem64?64:32) ? ram.data()+p*1024 : sink.data();
    }
    const char* monitors[]={"Monitor-std.rom","Monitor-dis.rom","Monitor-cpm-Variel.rom","Monitor-cpm-030188.rom"};
    map(60,4,asset(monitors[cfg.monitor-10]).data);
    reads[59]=writes[59]=vram.data()+(cfg.video?1024:0);
    if(cfg.video) reads[58]=writes[58]=vram.data();
    if(cfg.main==1) map(50,8,asset("Basic6.rom").data);
    if(cfg.main==2) { map(50,8,asset("BasicG.rom").data); map(44,4,asset("BasicG2.rom").data); }
    if(cfg.main==3) map(32,16,asset("Assembler.rom").data);
}
void Memory::setBootstrap(bool on) {
    bootstrap=on;
    for(int p=0;p<2;++p) { reads[p]=on?reads[62+p]:ram.data()+p*1024; writes[p]=ram.data()+p*1024; }
}
void Memory::switchAmos(int bank) {
    amosBank=bank&3;
    const char* roms[]={"Pascal.rom","Pascal1.rom","Assembler.rom"};
    if(amosBank<3) map(32,16,asset(roms[amosBank]).data);
    else for(int p=32;p<48;++p) reads[p]=writes[p]=ram.data()+p*1024;
}
void Memory::mountDisc() { discMounted=true; map(56,2,asset("Disc2.rom").data); }
int Pic::highest(int b) const { for(int i=7;i>=0;--i) if(b&(1<<i)) return i; return -1; }
void Pic::reset() { s.fill(0); s[6]=2; s[9]=3; cpu.setActiveInt(false); }
void Pic::check() { if(highest(s[7]&~s[4])>highest(s[8])) { s[10]=1; cpu.setActiveInt(true); } }
void Pic::command(int v) {
    if(v&32) { int rq=(v&64)?(s[5]&7):highest(s[8]); if(rq>=0) s[8]&=~(1<<rq); check(); }
}
void Pic::irq(int level) { s[7]|=1<<level; check(); }
void Pic::out(int p,int v) {
    if(!p) {
        if(v&16) { s[0]=v; s[4]=0; if(!(v&1)) s[3]=0; s[6]=(s[6]&0xf8)|2; s[9]=0; }
        else if(v&8) s[6]=v;
        else { s[5]=v; command(v); }
    } else if(s[9]==0) { s[1]=v; s[9]=!(s[0]&2)?1:((s[0]&1)?2:3); }
    else if(s[9]==1) { s[2]=v; s[9]=(s[0]&1)?2:3; }
    else if(s[9]==2) { s[3]=v; s[9]=3; }
    else { s[4]=v; check(); }
}
int Pic::in(int p) {
    if(p) return s[4];
    if(s[6]&4) return s[8]?(highest(s[8])|128):0;
    return (s[6]&3)==3?s[8]:((s[6]&2)?s[7]:255);
}
int Pic::ack() {
    if(s[10]==1) {
        int rq=highest(s[7]&~s[4]); if(rq<0) return 0;
        s[8]|=1<<rq; s[7]&=~(1<<rq); s[10]=2; return 0xcd;
    }
    if(s[10]==2) {
        int rq=highest(s[8]); s[10]=0; cpu.setActiveInt(false);
        int a=(s[1]<<8)&0xff00;
        if(s[3]&2) command(32);
        return a|((s[0]&4)?((s[0]&0xe0)|(rq<<2)):((s[0]&0xc0)|(rq<<3)));
    }
    return 0;
}
void Grafik::control(int v) { bitAccess=!(v&1); show=(v&8)!=0; pen=(v&2)!=0; }
void Grafik::bsr(int v) {
    if(v&128) return;
    switch((v&15)>>1) { case 0:bitAccess=!(v&1);break;case 1:pen=(v&1)!=0;break;case 3:show=(v&1)!=0;break; }
}
void Grafik::write(int v) {
    uint8_t& a=ram[(d0&63)*256+(d1&255)];
    if(bitAccess) { int mask=1<<((v>>5)&7); a=uint8_t(pen?(a&~mask):(a|mask)); } else a=uint8_t(v);
}
int Keyboard::portA(int p) const { int k=255;for(int i=0;i<8;++i) if(!(p&(1<<i))) k&=row[i];return k; }
int Keyboard::portB(int p) const { int k=255;for(int i=0;i<8;++i) if((row[i]|p)!=255) k&=~(1<<i);return k; }
bool Keyboard::decode(char c,int& r,int& b,bool& shifted) {
    switch(c) {
#include "iq151/keymap.inc"
    case '\r': case '\n': r=4;b=4;shifted=false;return true;
    case '\b': r=5;b=6;shifted=false;return true;
    case '\t': r=6;b=6;shifted=false;return true;
    default:return false;
    }
}
bool Keyboard::character(char c) { int r,b;bool sh;if(!decode(c,r,b,sh)) return false; key(r,b,true);shift=sh;return true; }
Machine::Machine(const Config& c,const std::string& root)
    : config(c),cpu(clock,*this,*this),memory(config),pic(cpu),pio(*this),sd(root.empty()?(config.sdRoot.empty()?"SDRoot":config.sdRoot):root) { config.sdRoot=sd.root;reset(true); }
void Machine::reset(bool dirty) {
    if(config.main<0||config.main>3||config.monitor<10||config.monitor>13||config.video<0||config.video>1)
        throw std::runtime_error("Invalid machine configuration");
    memory.reset(dirty); memory.setBootstrap(true); clock.reset(); nextHalf=1024;
    pic.reset(); cpu.reset(); pio.Reset(); keyboard.reset();
    port80=0;khz1=tapein=tapeo=tapeout=tapestart=false;
    grafik=Grafik(); floppy.reset();
    sd.root=config.sdRoot.empty()?"SDRoot":config.sdRoot;
    if(config.sdrom) sd.reset();
    cancelPaste(); audio.clear();audioPhase=0;autoPending=true;
}
int Machine::fetchOpcode(int a) { clock.addTstates(4);return cpu.isIntAck()?pic.ack():memory.read(a); }
int Machine::peek8(int a) { clock.addTstates(3);return memory.read(a); }
int Machine::peek16(int a) {
    clock.addTstates(6);
    if(cpu.isIntAck()) { cpu.setRegPC(cpu.getRegPC()-2);return pic.ack(); }
    return memory.read(a)|(memory.read(a+1)<<8);
}
void Machine::poke8(int a,int v) { clock.addTstates(3);memory.write(a,v);if((a&65535)==memoryBreakpoint) paused=true; }
void Machine::poke16(int a,int v) { poke8(a,v);poke8(a+1,v>>8); }
int Machine::inPort(int p) {
    clock.addTstates(4);p&=255;
    if(p>=0x84&&p<=0x87) return pio.CpuRead(p-0x84);
    if(p==0x88||p==0x89) return pic.in(p&1);
    if(p==0xaa||p==0xab) return config.disc2?floppy.in(p):255;
    if(p==0xac) return config.disc2&&memory.discMounted?1:0;
    if(p==0xd4) return config.grafik?grafik.read():255;
    if(p>=0xf8&&p<=0xfb&&config.sdrom) { sd.poll();return sd.pio.CpuRead(p-0xf8); }
    if(p>=0xfc) return config.video?254:255;
    return 255;
}
void Machine::outPort(int p,int v) {
    clock.addTstates(4);p&=255;v&=255;
    if(p==0x80) { port80=v; memory.setBootstrap(!(v&1)); }
    if(p>=0x84&&p<=0x87) {
        // The Java sound path consumes BSR writes to PC3 without changing the 8255 latch.
        int corrected=v&0x8f;
        if(p!=0x87||(corrected!=6&&corrected!=7)) pio.CpuWrite(p-0x84,v);
        if(p==0x86) speaker=(v&8)!=0;
        if(p==0x87&&(corrected==6||corrected==7)) speaker=(v&1)!=0;
    }
    if(p==0x88||p==0x89) pic.out(p&1,v);
    if(p==0xab&&config.disc2) floppy.out(v);
    if(p==0xac&&config.disc2&&(v&1)) memory.mountDisc();
    if(p>=0xec&&p<=0xef) memory.switchAmos(v&3);
    if(config.grafik) switch(p) {
        case 0xd0:grafik.d0=v;break;case 0xd1:grafik.d1=v;break;
        case 0xd2:grafik.control(v);break;case 0xd3:grafik.bsr(v);break;case 0xd4:grafik.write(v);break;
    }
    if(p>=0xf8&&p<=0xfb&&config.sdrom) {
        sd.pio.CpuWrite(p-0xf8,v);if(p==0xfb) sd.control(v);sd.poll();
    }
    if(p>=0xfc&&config.video) config.v64ena32=(v&1)!=0;
}
void Machine::OnCpuWriteC() {
    tapeo=pio.PeripheralReadBit(2,0);tapestart=pio.PeripheralReadBit(2,1);
}
void Machine::OnCpuReadA() { pio.PeripheralWriteByte(0,keyboard.portA(pio.PeripheralReadByte(1))); }
void Machine::OnCpuReadB() { pio.PeripheralWriteByte(1,keyboard.portB(pio.PeripheralReadByte(0))); }
void Machine::OnCpuReadCH() {
    int v=tapestart?((tapein?128:0)|(khz1?32:0)):
        ((!keyboard.fb?128:0)|(!keyboard.fa?64:0)|(!keyboard.ctrl?32:0)|(!keyboard.shift?16:0));
    for(int b=4;b<8;++b) pio.PeripheralChangeBit(2,b,(v&(1<<b))!=0);
}
void Machine::peripherals(int states) {
    while(clock.tstates>=nextHalf) { nextHalf+=1024;khz1=!khz1;if(khz1) tapeout=tapeo; }
    bool sample=tape.advance(states,tapestart,tapeout^khz1);
    if(tapestart&&!tape.record&&!tape.inputPath.empty()) // CswFile.java toggles before its first run; retain its polarity convention.
        tapein=sample^tape.inverted^tape.isCsw();
    if(config.audio) {
        audioPhase+=uint32_t(states)*44100u;
        while(audioPhase>=2048000u) { audioPhase-=2048000u;audio.push_back(speaker?6000:-6000); }
    }
    if(config.sdrom) sd.poll();
}
void Machine::step() {
    if(autoPending) {
        int pc=cpu.getRegPC();
        if(config.sdrom&&config.sdAutorun&&pc==(config.main==1?0xccd4:(config.main==2?0xcd79:0xf1ce))) {
            memory.write(3,85);int sp=cpu.getRegSP()-2;cpu.setRegSP(sp);memory.write(sp,0);memory.write(sp+1,0);cpu.setRegPC(0xf3ba);autoPending=false;
        }
        if(pc==0xf1e3) {
            if(config.main==3&&config.amosAutorun) {
                cpu.setRegHL(0x8000);int ptr=memory.read(0x2a)|(memory.read(0x2b)<<8);
                memory.write(ptr,0x32);memory.write(ptr+1,0xf4);cpu.setRegPC(0xf41b);autoPending=false;
            } else if(config.monitor==13&&config.felAutorun) {cpu.setRegPC(0xfff8);autoPending=false;}
        }
    }
    int64_t before=clock.tstates;cpu.executeOne();peripherals(int(clock.tstates-before));
}
void Machine::frame() {
    if(paused) return;
    pic.irq(6);
    // Preserve Java's per-half-period instruction overshoot, not just a rounded frame budget.
    for(int h=0;h<40&&!paused;++h) {
        int64_t limit=clock.tstates+1024;nextHalf=limit;
        while(clock.tstates<limit&&!paused) {
            if(cpu.isBreakpoint(cpu.getRegPC())) { paused=true;break; }
            step();
        }
    }
    if(!pasteQueue.empty()&&++pasteDelay>=3) {
        keyboard.reset();pasteDelay=0;
        if(pasteDown) {if(pasteQueue.front()=='\n')pasteDelay=-42;pasteQueue.pop_front();pasteDown=false;}
        else {keyboard.character(pasteQueue.front());pasteDown=true;}
    }
}
void Machine::paste(const std::string& text) { cancelPaste();for(char c:text) if(c!='\r') {if(c>='a'&&c<='z')c=char(c-'a'+'A');else if(c>='A'&&c<='Z')c=char(c-'A'+'a');pasteQueue.push_back(c);} }
void Machine::cancelPaste() { if(!pasteQueue.empty()||pasteDown) keyboard.reset();pasteQueue.clear();pasteDelay=0;pasteDown=false; }
std::string Machine::screenText() const {
    std::string out;int columns=config.video?64:32;
    for(int y=0;y<32;++y) {
        std::string line;
        for(int x=0;x<columns;++x) {int c=memory.vram[y*columns+x]&127;line+=char(c>=32&&c<127?c:' ');}
        while(!line.empty()&&line.back()==' ') line.pop_back();
        out+=line+'\n';
    }
    return out;
}
std::vector<uint32_t> Machine::render(uint32_t fg) const {
    // Java retains pixels in cells skipped by Video64 double-width control codes.
    auto& pixels=framebuffer;
    const uint8_t* chars=asset("chars.bin").data;
    const auto glyph=[&](int address,int c,bool inverse,bool doubled) {
        int columns=config.video?64:32;
        int x0=20+(address%columns)*(config.video?8:16),y0=8+(address/columns)*8;
        int scale=doubled?2:(config.video?1:2);
        for(int y=0;y<8;++y) for(int x=0;x<8*scale;++x) {
            int px=x0+x,py=y0+y;
            bool lit=((chars[(c&127)*8+y]>>(7-x/scale))&1)!=inverse;
            if(config.grafik&&grafik.enabled&&grafik.show&&px<532) lit|=grafik.pixel(px-20,py-8);
            if(px<552) pixels[py*560+px]=lit?fg:0xff000000;
        }
    };
    if(!config.video) for(int a=0;a<1024;++a) glyph(a,memory.vram[a]&127,(memory.vram[a]&128)!=0,false);
    else {
        bool doubled=false,second=false;
        for(int a=0;a<2048;++a) {
            if(!(a&63)) doubled=false;
            int raw=memory.vram[a],c=raw&127;bool inv=(raw&128)!=0;
            if(!doubled) glyph(a,(c==127||c==124)?32:c,inv,false);
            else if(!second) {
                int prev=memory.vram[a-1];if((prev&127)==127) prev=raw;
                inv=(prev&128)!=0;prev&=127;
                if(prev==124||prev==13) {doubled=false;glyph(a,32,inv,false);}
                else {glyph(a,prev,inv,true);second=true;}
            } else second=false;
            if(c==127) {doubled=config.v64ena32;if((a&1)==1) {second=true;glyph(a,32,inv,true);}}
        }
    }
    return pixels;
}
void Machine::loadBinary(const std::string& p,int a) {
    std::vector<uint8_t> d=readFile(p);
    if(a<0||a>65535||d.size()>65536u-unsigned(a)) throw std::runtime_error("Binary exceeds address space");
    std::copy(d.begin(),d.end(),memory.ram.begin()+a);
}
void Machine::saveBinary(const std::string& p,int first,int last) const {
    if(first<0||last>65535||first>last) throw std::runtime_error("Invalid memory range");
    std::vector<uint8_t> d;for(int a=first;a<=last;++a) d.push_back(memory.read(a));writeFile(p,d.data(),d.size());
}
void Machine::loadHex(const std::string& path) {
    std::ifstream f(path.c_str());if(!f) throw std::runtime_error("Cannot open "+path);
    std::string line;std::vector<std::pair<int,int> > bytes;bool end=false;
    while(std::getline(f,line)) {
        if(!line.empty()&&line.back()=='\r') line.pop_back();
        if(line.empty()) continue;
        if(line[0]!=':'||line.size()<11||(line.size()&1)==0) throw std::runtime_error("Invalid Intel HEX record");
        std::vector<int> b;
        for(std::size_t i=1;i<line.size();i+=2) {
            std::string s=line.substr(i,2);if(s.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos) throw std::runtime_error("Invalid HEX digit");
            b.push_back(std::stoi(s,nullptr,16));
        }
        int sum=0;for(int v:b) sum+=v;
        if((sum&255)||b.size()!=std::size_t(b[0]+5)) throw std::runtime_error("Invalid HEX checksum or length");
        if(b[3]==1) {end=true;break;}
        if(b[3]!=0) throw std::runtime_error("Only 16-bit Intel HEX is supported");
        int a=(b[1]<<8)|b[2];if(a+b[0]>65536) throw std::runtime_error("HEX exceeds address space");
        for(int i=0;i<b[0];++i) bytes.push_back(std::make_pair(a+i,b[i+4]));
    }
    if(!end) throw std::runtime_error("Missing Intel HEX end record");
    for(const auto& b:bytes) memory.ram[b.first]=uint8_t(b.second);
}
}
