// SPDX-License-Identifier: GPL-3.0-or-later
// Event-driven implementation of SDRom.java's ATmega/8255 protocol.
#include "iq151/machine.hpp"
#include "iq151/files.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace iq151 {
SDRom::SDRom(const std::string& path) : pio(*this),root(path) {}
void SDRom::outputMode() {pio.CpuWrite(3,180);pio.CpuWrite(3,9);pio.CpuWrite(3,5);pio.PeripheralChangeBit(2,4,true);}
void SDRom::inputMode() {
    transmitting=false;waiting=false;bit2=1;bit5=0;
    pio.PeripheralChangeBit(2,2,true); // receiver ready (toggle handshake)
}
void SDRom::reset() {
    makeDirectory(root);
    bool loader=false;
    for(const auto& e:directory(root)) if(e.name=="__BLOADER.BAS") loader=true;
    if(!loader) {const auto a=asset("__BLOADER.BAS");writeFile(root+"/__BLOADER.BAS",a.data,a.size);}
    tx=readFile(root+"/__BLOADER.BAS");txPos=0;rx.clear();saved.clear();command=0;pathLength=-1;
    saving=saveAck=false;discard=0;transmitting=true;waiting=false;activity=false;outputMode();
}
void SDRom::receive(int v) {
    if(!command) { if(v==130||v==131) {command=v;pathLength=-1;rx.clear();} return; }
    if(pathLength<0) {pathLength=v;if(v>128) {command=0;pathLength=-1;}else if(!v) request();return;}
    rx.push_back(uint8_t(v));if(int(rx.size())==pathLength) request();
}
void SDRom::request() {
    std::string name(rx.begin(),rx.end());tx.clear();txPos=0;
    if(command==130) {
        std::string target=sdPath(root,name);
        if(target!=absolutePath(root)) {tx.push_back('/');tx.push_back('.');tx.push_back('.');tx.push_back(0);}
        for(const auto& e:directory(target)) {
            if(e.name==".."||e.name=="__BLOADER.BAS") continue;
            if(e.directory) tx.push_back('/');
            tx.insert(tx.end(),e.name.begin(),e.name.end());tx.push_back(0);
        }
        tx.push_back(13);
    } else {
        tx=readFile(sdPath(root,name));
        // Monitor stops consuming at the EOF Intel HEX record; BASIC stops at ESC.
        std::string text(tx.begin(),tx.end());std::size_t end=text.rfind(":00000001");
        if(end!=std::string::npos) tx.resize(end+8);
        else {
            auto esc=std::find(tx.begin(),tx.end(),uint8_t(27));
            if(esc!=tx.end()) tx.erase(esc+1,tx.end());else tx.push_back(27);
        }
    }
    command=0;pathLength=-1;rx.clear();transmitting=true;waiting=false;outputMode();
}
void SDRom::control(int v) {
    // The CPU mode-set resets the input latch: re-present any queued byte afterwards.
    if(v==180&&transmitting) {if(waiting&&txPos) --txPos;waiting=false;}
    if(v==129&&!saving) {txPos=tx.size();inputMode();}

    if(v==14) saveArmed=true;
    else if(v==13&&saveArmed) {
        saveArmed=false;saving=true;saved.clear();discard=0;saveAck=false;
        outputMode();pio.PeripheralChangeBit(2,2,true);
    } else saveArmed=false;
}
void SDRom::finishSave() {
    std::string suffix=".hex";
    if(saved.size()>=3&&saved[saved.size()-3]==27) suffix=".bas";
    std::string path;
    for(;;) {
        std::ostringstream os;os<<root<<"/save"<<std::setfill('0')<<std::setw(3)<<saveNumber++<<suffix;path=os.str();
        std::ifstream f(path.c_str(),std::ios::binary);if(!f) break;
    }
    writeFile(path,saved.data(),saved.size());saving=false;activity=false;
}
void SDRom::poll() {
    activity=saving||transmitting;
    if(saving) {
        bool strobe=pio.PeripheralReadBit(2,1);
        if(!saveAck&&!strobe) {
            int v=pio.PeripheralReadByte(1);saveAck=true;pio.PeripheralChangeBit(2,2,false);
            if(discard) {if(!--discard) finishSave();return;}
            if(v) saved.push_back(uint8_t(v));
            std::size_t n=saved.size();
            if(n>=3&&saved[n-3]==27&&saved[n-2]==13&&saved[n-1]==10) discard=64;
            if(n>=13&&saved[n-13]==':'&&saved[n-12]=='0'&&saved[n-11]=='0'&&saved[n-6]=='0'&&saved[n-5]=='1'&&saved[n-2]==13&&saved[n-1]==10) finishSave();
        } else if(saveAck&&strobe) {saveAck=false;pio.PeripheralChangeBit(2,2,true);}
        return;
    }
    if(transmitting) {
        if(waiting&&pio.PeripheralReadBit(2,5)) return;
        waiting=false;
        if(txPos<tx.size()) {
            pio.PeripheralChangeBit(2,4,true);pio.PeripheralWriteByte(0,tx[txPos++]);pio.PeripheralChangeBit(2,4,false);pio.PeripheralChangeBit(2,4,true);waiting=true;
        } else inputMode();
        return;
    }
    bool ack=pio.PeripheralReadBit(2,5);
    if(ack!=(bit5!=0)) {
        bit5^=1;int v=pio.PeripheralReadByte(0);receive(v);
        if(!transmitting) {pio.PeripheralChangeBit(2,2,bit2==0);bit2^=1;}
    }
}
}
