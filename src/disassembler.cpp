// SPDX-License-Identifier: GPL-3.0-or-later
#include "iq151/machine.hpp"
#include <cstdio>
namespace iq151 {
std::string disassemble(const Memory& m,int address,int& length,bool z) {
    int op=m.read(address),n=m.read(address+1),nn=n|(m.read(address+2)<<8);length=1;
    const char* regs[]={"B","C","D","E","H","L",z?"(HL)":"M","A"};
    const char* pairs[]={z?"BC":"B",z?"DE":"D",z?"HL":"H","SP"};
    const char* conditions[]={"NZ","Z","NC","C","PO","PE","P","M"};
    const auto fmt=[](const char* f,const char* a,int v){char b[96];std::snprintf(b,sizeof(b),f,a,v);return std::string(b);};
    const auto imm=[&](const char* intel,const char* zilog){length=2;return fmt(z?zilog:intel,"",n);};
    const auto addr=[&](const char* f){length=3;return fmt(f,"",nn);};
    if(op>=0x40&&op<=0x7f) return op==0x76?(z?"HALT":"HLT"):std::string(z?"LD ":"MOV ")+regs[(op>>3)&7]+","+regs[op&7];
    if(op>=0x80&&op<=0xbf) {
        const char* alu[]={z?"ADD A,":"ADD ",z?"ADC A,":"ADC ","SUB ",z?"SBC A,":"SBB ",z?"AND ":"ANA ",z?"XOR ":"XRA ",z?"OR ":"ORA ",z?"CP ":"CMP "};
        return std::string(alu[(op>>3)&7])+regs[op&7];
    }
    if(op<0x40&&(op&7)==4) return std::string(z?"INC ":"INR ")+regs[(op>>3)&7];
    if(op<0x40&&(op&7)==5) return std::string(z?"DEC ":"DCR ")+regs[(op>>3)&7];
    if(op<0x40&&(op&7)==6) {length=2;return fmt((std::string(z?"LD ":"MVI ")+"%s,%02X").c_str(),regs[(op>>3)&7],n);}
    if(op<0x40&&(op&15)==1) {length=3;return fmt(z?"LD %s,%04X":"LXI %s,%04X",pairs[op>>4],nn);}
    if(op<0x40&&(op&15)==3) return std::string(z?"INC ":"INX ")+pairs[op>>4];
    if(op<0x40&&(op&15)==11) return std::string(z?"DEC ":"DCX ")+pairs[op>>4];
    if(op<0x40&&(op&15)==9) return std::string(z?"ADD HL,":"DAD ")+pairs[op>>4];
    if(op>=0xc0) {
        if((op&7)==0) return std::string(z?"RET ":"R")+conditions[(op>>3)&7];
        if((op&7)==2) {length=3;return fmt((std::string(z?"JP ":"J")+"%s,%04X").c_str(),conditions[(op>>3)&7],nn);}
        if((op&7)==4) {length=3;return fmt((std::string(z?"CALL ":"C")+"%s,%04X").c_str(),conditions[(op>>3)&7],nn);}
        if((op&7)==7) return fmt("RST %s%02X","",z?op&56:(op>>3)&7);
        if((op&15)==1||(op&15)==5) {const char* p=((op>>4)&3)==3?(z?"AF":"PSW"):pairs[(op>>4)&3];return std::string((op&4)?"PUSH ":"POP ")+p;}
        if((op&7)==6) {
            const char* ai[]={"ADI","ACI","SUI","SBI","ANI","XRI","ORI","CPI"};
            const char* az[]={"ADD A,","ADC A,","SUB ","SBC A,","AND ","XOR ","OR ","CP "};length=2;
            return fmt((std::string(z?az[(op>>3)&7]:std::string(ai[(op>>3)&7])+" ")+"%s%02X").c_str(),"",n);
        }
    }
    switch(op) {
    case 0x00:return "NOP";case 0x02:return z?"LD (BC),A":"STAX B";case 0x12:return z?"LD (DE),A":"STAX D";
    case 0x0a:return z?"LD A,(BC)":"LDAX B";case 0x1a:return z?"LD A,(DE)":"LDAX D";
    case 7:return z?"RLCA":"RLC";case 15:return z?"RRCA":"RRC";case 23:return z?"RLA":"RAL";case 31:return z?"RRA":"RAR";
    case 8:return "DSUB";case 16:return "ARHL";case 24:return "RDEL";case 32:return "RIM";case 40:return imm("LDHI %s%02X","LDHI %s%02X");case 48:return "SIM";case 56:return imm("LDSI %s%02X","LDSI %s%02X");
    case 0x22:return addr(z?"LD (%s%04X),HL":"SHLD %s%04X");case 0x2a:return addr(z?"LD HL,(%s%04X)":"LHLD %s%04X");
    case 0x32:return addr(z?"LD (%s%04X),A":"STA %s%04X");case 0x3a:return addr(z?"LD A,(%s%04X)":"LDA %s%04X");
    case 0x27:return "DAA";case 0x2f:return z?"CPL":"CMA";case 0x37:return z?"SCF":"STC";case 0x3f:return z?"CCF":"CMC";
    case 0xc3:return addr(z?"JP %s%04X":"JMP %s%04X");case 0xc9:return "RET";case 0xcd:return addr("CALL %s%04X");
    case 0xd3:return imm("OUT %s%02X","OUT (%s%02X),A");case 0xdb:return imm("IN %s%02X","IN A,(%s%02X)");
    case 0xe3:return z?"EX (SP),HL":"XTHL";case 0xe9:return z?"JP (HL)":"PCHL";case 0xeb:return z?"EX DE,HL":"XCHG";
    case 0xf3:return "DI";case 0xfb:return "EI";case 0xf9:return z?"LD SP,HL":"SPHL";
    case 0xcb:return "RSTV";case 0xd9:return "SHLX";case 0xdd:return addr("JNUI %s%04X");case 0xed:return "LHLX";case 0xfd:return addr("JUI %s%04X");
    default:return fmt("DB %s%02X","",op);
    }
}
}
