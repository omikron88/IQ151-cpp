// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "cpu.hpp"
#include "pio.hpp"
#include <array>
#include <vector>
#include <string>
#include <functional>
#include <fstream>
#include <deque>
namespace iq151 {
struct Asset { const char* name; const unsigned char* data; std::size_t size; };
const Asset& asset(const std::string& name);
struct Config {
    int main = 2, monitor = 10, video = 0;
    bool mem64 = false, grafik = true, sdrom = false, disc2 = false;
    bool audio = false, smartkeyboard = false, v64ena32 = false;
    bool sdAutorun = false, felAutorun = false, amosAutorun = false;
    std::array<uint8_t,10> bytes() const;
};
class Memory {
    Config& cfg;
    std::array<const uint8_t*,64> reads;
    std::array<uint8_t*,64> writes;
    std::array<uint8_t,1024> unmapped{}, sink{};
    void map(int first, int count, const uint8_t* rom);
public:
    std::array<uint8_t,65536> ram{};
    std::array<uint8_t,2048> vram{};
    int amosBank = -1;
    bool bootstrap = false, discMounted = false;
    explicit Memory(Config& c) : cfg(c) { reset(true); }
    void reset(bool dirty);
    void setBootstrap(bool on);
    void switchAmos(int bank);
    void mountDisc();
    uint8_t read(int a) const { a &= 65535; return reads[a>>10][a&1023]; }
    void write(int a, int v) { a &= 65535; writes[a>>10][a&1023] = static_cast<uint8_t>(v); }
};
class Pic {
    I8085& cpu;
    int highest(int bits) const;
    void check();
    void command(int v);
public:
    // ICW1..4, OCW1..3, IRR, ISR, initialization state, acknowledge state.
    std::array<int,11> s{};
    explicit Pic(I8085& c) : cpu(c) { reset(); }
    void reset();
    void out(int port, int value);
    int in(int port);
    void irq(int level);
    int ack();
    void restore(const std::array<int,11>& state) { s=state; check(); }
};
struct Grafik {
    std::array<uint8_t,16384> ram{};
    int d0=0,d1=0,d2=0;
    bool enabled=true,show=true,bitAccess=false,pen=false;
    void control(int value);
    void bsr(int value);
    void write(int value);
    int read() const { return ram[(d0&63)*256+(d1&255)]; }
    bool pixel(int x,int y) const { return (ram[(x/8)*256+255-y] & (1<<(x&7))) != 0; }
};
struct Keyboard {
    std::array<int,8> row;
    bool shift=false,ctrl=false,fa=false,fb=false;
    Keyboard() { reset(); }
    void reset() { row.fill(255); shift=ctrl=fa=fb=false; }
    void key(int r,int b,bool down) { if(down) row[r]&=~(1<<b); else row[r]|=1<<b; }
    int portA(int port) const;
    int portB(int port) const;
    static bool decode(char c,int& r,int& b,bool& shifted);
    bool character(char c);
};
class Tape {
    std::vector<uint32_t> runs;
    std::vector<int16_t> samples;
    std::size_t pos=0;
    uint32_t left=0;
    bool level=false, initialLevel=false;
    uint64_t recordPhase=0;
    double samplePhase=0;
    int rate=22050;
    int minimum=0,maximum=0;
    std::deque<int> filter;
    std::ofstream output;
    uint32_t outLength=0,outCount=0;
    bool outLevel=false;
    void flushRun();
public:
    bool record=false, inverted=true, eof=false;
    std::string inputPath, outputPath;
    ~Tape();
    void open(const std::string& path);
    void rewind();
    bool isCsw() const { return !runs.empty(); }
    void save(const std::string& path);
    void close();
    bool advance(int states,bool running,bool signal);
};
struct Disk {
    std::string path;
    std::vector<uint8_t> data;
    bool writable=false;
    int trackSize=32*128;
    void open(const std::string& name,bool write);
    void flush();
};
class Floppy {
    std::array<int,9> cmd{};
    std::vector<uint8_t> transfer;
    std::deque<int> result;
    std::size_t pos=0,offset=0;
    int needed=0,lastStatus=32,lastCylinder=0,senseCount=1;
    bool reading=false,writing=false;
    void execute();
    void finish();
    int drive() const { return (cmd[1]&3)==1 ? 0 : 1; }
public:
    Disk disks[2];
    int status=128, activity=0;
    void reset();
    int in(int port);
    void out(int value);
};
class SDRom : public Pio8255Notify {
    std::vector<uint8_t> tx,rx,saved;
    std::size_t txPos=0;
    int command=0,pathLength=-1,bit2=1,bit5=0;
    bool transmitting=true,waiting=false,saving=false,saveAck=false,saveArmed=false;
    int saveNumber=1,discard=0;
    void inputMode();
    void outputMode();
    void receive(int value);
    void request();
    void finishSave();
public:
    Pio8255 pio;
    std::string root;
    bool activity=false;
    explicit SDRom(const std::string& path);
    void reset();
    void poll();
    void control(int value);
};
class Machine : public MemIoOps, public NotifyOps, public Pio8255Notify {
    int64_t nextHalf=1024;
    uint32_t audioPhase=0;
    std::deque<char> pasteQueue;
    int pasteDelay=0;
    bool autoPending=true, pasteDown=false, speaker=false;
    mutable std::vector<uint32_t> framebuffer = std::vector<uint32_t>(560*272,0xff000000);
    void peripherals(int states);
public:
    Config config;
    int port80=0;
    bool khz1=false,tapein=false,tapeo=false,tapeout=false,tapestart=false;
    Clock clock;
    I8085 cpu;
    Memory memory;
    Pic pic;
    Keyboard keyboard;
    Grafik grafik;
    Tape tape;
    Floppy floppy;
    Pio8255 pio;
    SDRom sd;
    bool paused=false;
    int memoryBreakpoint=-1;
    std::vector<int16_t> audio;
    explicit Machine(const Config& c=Config(),const std::string& sdRoot="SDRoot");
    void reset(bool dirty=false);
    void step();
    void frame();
    void paste(const std::string& text);
    void cancelPaste();
    std::string screenText() const;
    std::vector<uint32_t> render(uint32_t foreground=0xffffffff) const;
    void saveSnapshot(const std::string& path) const;
    void loadSnapshot(const std::string& path);
    void loadBinary(const std::string& path,int address);
    void saveBinary(const std::string& path,int first,int last) const;
    void loadHex(const std::string& path);
    int fetchOpcode(int address) override;
    int peek8(int address) override;
    int peek16(int address) override;
    void poke8(int address,int value) override;
    void poke16(int address,int value) override;
    int inPort(int port) override;
    void outPort(int port,int value) override;
    void OnCpuWriteC() override;
    void OnCpuReadA() override;
    void OnCpuReadB() override;
    void OnCpuReadCH() override;
};
std::vector<uint8_t> readFile(const std::string& path);
void writeFile(const std::string& path,const uint8_t* bytes,std::size_t size);
std::string disassemble(const Memory& mem,int address,int& length,bool z80=false);
}
