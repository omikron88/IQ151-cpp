// Test oracle for the original Java I8085, NOT used by the C++ emulator.
// SPDX-License-Identifier: GPL-3.0-or-later
import machine.*;
public class Reference implements MemIoOps, NotifyOps {
    Clock clock=new Clock();
    byte[] mem=new byte[65536];
    int output=0;
    static int random=0;
    static int next() {random^=random<<13;random^=random>>>17;random^=random<<5;return random;}
    public int fetchOpcode(int a){clock.addTstates(4);return mem[a&65535]&255;}
    public int peek8(int a){clock.addTstates(3);return mem[a&65535]&255;}
    public int peek16(int a){clock.addTstates(6);return (mem[a&65535]&255)|((mem[(a+1)&65535]&255)<<8);}
    public void poke8(int a,int v){clock.addTstates(3);mem[a&65535]=(byte)v;}
    public void poke16(int a,int v){clock.addTstates(6);mem[a&65535]=(byte)v;mem[(a+1)&65535]=(byte)(v>>>8);}
    public int inPort(int p){clock.addTstates(4);return (p^0x5a)&255;}
    public void outPort(int p,int v){clock.addTstates(4);output=((p&255)<<8)|(v&255);}
    public boolean inSerial(){return false;}
    public void outSerial(boolean b){}
    public int atAddress(int a,int op){return op;}
    public void execDone(){}
    static long hash=0;
    static void byteHash(int b){hash=(hash^(b&255))*0x100000001b3L;}
    static void wordHash(int w){byteHash(w);byteHash(w>>>8);}
    public static void main(String[] args) {
        for(int op=0;op<256;op++) {
            hash=0xcbf29ce484222325L;
            for(int test=0;test<32;test++) {
                random=0x12345678+op*65537+test;
                Reference bus=new Reference();I8085 c=new I8085(bus.clock,bus,bus);
                for(int a=0;a<65536;a++)bus.mem[a]=(byte)next();
                c.setRegA(next());c.setFlags(next()&255);c.setRegBC(next());c.setRegDE(next());c.setRegHL(next());
                c.setRegSP(test<2?(test==0?0xffff:0):next());c.setRegPC(0x2345);c.setMemPtr(next());
                bus.mem[0x2345]=(byte)op;c.executeOne();
                wordHash(c.getRegAF());byteHash(c.getFlags());wordHash(c.getRegBC());wordHash(c.getRegDE());wordHash(c.getRegHL());
                wordHash(c.getRegSP());wordHash(c.getRegPC());wordHash(c.getMemPtr());byteHash(c.getRegSim());byteHash(c.isPendingEI()?1:0);byteHash(c.isHalted()?1:0);
                wordHash((int)bus.clock.getTstates());wordHash(bus.output);
                for(byte b:bus.mem)byteHash(b);
            }
            System.out.printf("%02x %016x%n",op,hash);
        }
    }
}
