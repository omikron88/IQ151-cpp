// SPDX-License-Identifier: GPL-3.0-or-later
#include "iq151/machine.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <limits>
namespace iq151 {
namespace {
uint32_t le(const std::vector<uint8_t>& b,std::size_t p,int count) {
    if(p>b.size()||b.size()-p<std::size_t(count)) throw std::runtime_error("Truncated media file");
    uint32_t r=0;for(int i=0;i<count;++i) r|=uint32_t(b[p+i])<<(i*8);return r;
}
void put32(std::ostream& f,uint32_t v) {for(int i=0;i<4;++i) f.put(char(v>>(i*8)));}
}
Tape::~Tape() { try { close(); } catch(...) {} }
void Tape::open(const std::string& path) {
    auto b=readFile(path);
    std::vector<uint32_t> newRuns;std::vector<int16_t> newSamples;int newRate=0;bool initial=false;
    if(b.size()>=52&&std::memcmp(b.data(),"Compressed Square Wave\x1a",23)==0) {
        if(b[23]!=2||b[33]!=1) throw std::runtime_error("Supported CSW: v2, RLE compression (1)");
        newRate=int(le(b,25,4));initial=(b[34]&1)!=0;std::size_t p=52+b[35];
        if(p>b.size()) throw std::runtime_error("Truncated CSW header");
        while(p<b.size()) {uint32_t n=b[p++];if(!n) {n=le(b,p,4);p+=4;}if(!n) throw std::runtime_error("Zero CSW pulse");newRuns.push_back(n);}
    } else if(b.size()>=12&&std::memcmp(b.data(),"RIFF",4)==0&&std::memcmp(b.data()+8,"WAVE",4)==0) {
        int bits=0,channels=0,format=0,alignment=0;std::size_t data=0,size=0;
        for(std::size_t p=12;p+8<=b.size();) {
            uint32_t len=le(b,p+4,4);std::size_t q=p+8;
            if(len>b.size()-q) throw std::runtime_error("Truncated WAV chunk");
            if(std::memcmp(b.data()+p,"fmt ",4)==0) {
                if(len<16) throw std::runtime_error("Invalid WAV format");
                format=int(le(b,q,2));channels=int(le(b,q+2,2));newRate=int(le(b,q+4,4));alignment=int(le(b,q+12,2));bits=int(le(b,q+14,2));
            }
            if(std::memcmp(b.data()+p,"data",4)==0) {data=q;size=len;}
            p=q+len+(len&1);
        }
        if(format!=1||(bits!=8&&bits!=16)||channels<1||channels>8||alignment!=channels*(bits/8)||!data||size%alignment)
            throw std::runtime_error("Supported WAV: PCM 8/16 bit, 1-8 channels");
        for(std::size_t p=data;p<data+size;p+=alignment) {
            int sample=0;
            for(int c=0;c<channels;++c) sample+=bits==8?(int(b[p+c])-128)*256:int(int16_t(le(b,p+c*2,2)));
            newSamples.push_back(int16_t(sample/channels));
        }
    } else throw std::runtime_error("Not a CSW or PCM WAV tape");
    if(newRate<1000||newRate>384000) throw std::runtime_error("Invalid tape sample rate");
    runs.swap(newRuns);samples.swap(newSamples);rate=newRate;inputPath=path;initialLevel=initial;rewind();
}
void Tape::rewind() {
    pos=0;left=0;level=initialLevel;samplePhase=0;eof=false;filter.assign(256,0);
}
void Tape::flushRun() {
    if(!outLength) return;
    if(outLength>255) {output.put(0);put32(output,outLength);} else output.put(char(outLength));
    ++outCount;outLength=0;
}
void Tape::save(const std::string& path) {
    close();output.open(path.c_str(),std::ios::binary|std::ios::trunc);
    if(!output) throw std::runtime_error("Cannot create "+path);
    output.write("Compressed Square Wave\x1a\x02\0",25);put32(output,22050);put32(output,0);
    output.put(1);output.put(0);output.put(0);output.write("IQ-151 C++11\0\0\0\0\0",16);
    outLength=outCount=0;outLevel=false;outputPath=path;recordPhase=0;
}
void Tape::close() {
    if(output.is_open()) {
        flushRun();output.seekp(29);put32(output,outCount);output.flush();bool fail=!output;output.close();
        if(fail) throw std::runtime_error("Cannot finish tape recording");
    }
}
bool Tape::advance(int states,bool running,bool signal) {
    if(!running) return level;
    if(record) {
        if(output.is_open()) {
            recordPhase+=uint64_t(states)*22050;
            while(recordPhase>=2048000) {
                recordPhase-=2048000;
                if(outLength&&signal!=outLevel) flushRun();
                if(!outLength&&!outCount) {outLevel=signal;auto p=output.tellp();output.seekp(34);output.put(signal?1:0);output.seekp(p);}
                outLevel=signal;++outLength;
                if(outLength==std::numeric_limits<uint32_t>::max()) flushRun();
            }
        }
        return level;
    }
    samplePhase+=double(states)*rate;
    while(samplePhase>=2048000) {
        samplePhase-=2048000;
        if(!runs.empty()) {
            if(!left) {
                if(pos==runs.size()) {eof=true;break;}
                if(pos) level=!level;
                left=runs[pos++];
            }
            --left;
        } else if(!samples.empty()) {
            if(pos==samples.size()) {eof=true;break;}
            int s=samples[pos++];filter.pop_front();filter.push_back(s);
            auto mm=std::minmax_element(filter.begin(),filter.end());
            minimum=std::min(0,*mm.first);maximum=std::max(0,*mm.second);level=s>(minimum+maximum)/2;
        }
    }
    return level;
}
void Disk::open(const std::string& name,bool write) {
    auto d=readFile(name);
    // IQD is a raw 77-track, 26 x 128-byte single-sided image.
    if(d.size()!=256256) throw std::runtime_error("IQD image must have 256256 bytes (77 x 26 x 128)");
    path=name;data.swap(d);writable=write;trackSize=3328;
}
void Disk::flush() {if(writable&&!path.empty()) writeFile(path,data.data(),data.size());}
void Floppy::reset() {cmd.fill(0);transfer.clear();result.clear();pos=0;needed=0;lastStatus=32;lastCylinder=0;senseCount=1;status=128;reading=writing=false;activity=0;}
void Floppy::finish() {
    if(writing) {
        Disk& d=disks[drive()];std::copy(transfer.begin(),transfer.end(),d.data.begin()+offset);d.flush();
    }
    result={cmd[1]&7,0,0,cmd[2],cmd[3],cmd[6],cmd[5]};reading=writing=false;status=208;activity=0;
}
int Floppy::in(int p) {
    if(p==0xaa) return status;
    if(reading) {int v=transfer[pos++];if(pos==transfer.size()) finish();return v;}
    if(result.empty()) return 255;
    int v=result.front();result.pop_front();if(result.empty()) status=128;return v;
}
void Floppy::out(int v) {
    if(status&64) return;
    if(writing) {transfer[pos++]=uint8_t(v);if(pos==transfer.size()) finish();return;}
    if(!needed) {
        cmd.fill(0);cmd[0]=v&15;pos=1;
        switch(cmd[0]) {case 3:case 15:needed=3;break;case 5:case 6:needed=9;break;case 7:needed=2;break;default:needed=1;break;}
    } else cmd[pos++]=v;
    if(int(pos)>=needed) {needed=0;execute();}
}
void Floppy::execute() {
    result.clear();pos=0;
    switch(cmd[0]) {
    case 3:status=128;senseCount=1;break;
    case 7:lastStatus=0;lastCylinder=0;senseCount=2;status=128;break;
    case 15:lastStatus=32+(cmd[1]&3);lastCylinder=cmd[2];senseCount=2;status=128;break;
    case 8:result.push_back(senseCount==1?128:lastStatus);if(senseCount==2) result.push_back(lastCylinder);senseCount=1;status=192;break;
    case 5:case 6: {
        Disk& d=disks[drive()];int size=cmd[5]?128<<std::min(cmd[5],7):cmd[8];
        int count=cmd[6]-cmd[4]+1;
        offset=std::size_t(cmd[2])*d.trackSize+std::size_t(std::max(0,cmd[4]-1))*128;
        std::size_t length=std::size_t(std::max(0,count))*size;
        if(d.data.empty()||cmd[4]<1||cmd[4]>26||cmd[6]>26||cmd[5]!=0||size!=128||!length||offset>d.data.size()||length>d.data.size()-offset||
            (cmd[0]==5&&!d.writable)) {
            result={int(0x40|(cmd[1]&7)),cmd[0]==5&&!d.writable?2:4,0,cmd[2],cmd[3],cmd[4],cmd[5]};status=208;return;
        }
        transfer.assign(d.data.begin()+offset,d.data.begin()+offset+length);
        reading=cmd[0]==6;writing=!reading;status=reading?240:176;activity=drive()+1;break;
    }
    default:result.push_back(128);status=192;break;
    }
}
}
