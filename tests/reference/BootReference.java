// SPDX-License-Identifier: GPL-3.0-or-later
// Run with the unmodified Java project on the classpath and an X display.
// Disables only wall-clock scheduling; ms20() and the entire hardware/renderer run unchanged.
import java.lang.reflect.*;
import java.util.*;
import java.io.*;
import machine.*;
import gui.*;
public class BootReference {
    static Object field(Object o,String name) throws Exception {Field f=o.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(o);}
    static void set(Object o,String name,Object v) throws Exception {Field f=o.getClass().getDeclaredField(name);f.setAccessible(true);f.set(o,v);}
    static long imageHash(Iq m) {
        long h=0xcbf29ce484222325L;
        for(int y=0;y<272;y++)for(int x=0;x<560;x++){int p=m.getImage().getRGB(x,y);for(int i=0;i<4;i++)h=(h^((p>>>(8*i))&255))*0x100000001b3L;}
        return h;
    }
    public static void main(String[] args)throws Exception {
        JIQ151 f=new JIQ151();Iq m=(Iq)field(f,"m");
        m.stopEmulation();((Timer)field(m,"tim")).cancel();
        set(m,"tim",new Timer(true){@Override public void scheduleAtFixedRate(TimerTask t,long delay,long period){}});
        for(int main=0;main<4;main++)for(int video=0;video<2;video++) {
            Config c=m.getConfig();c.setMain((byte)main);c.setVideo((byte)video);c.setMem64(false);c.setMonitor((byte)10);c.setGrafik(true);c.setSDRom(false);c.setDisc2(false);c.setAudio(false);
            utils.Config.mainmodule=main;utils.Config.sdrom=false;utils.Config.amosautorun=false;
            for(byte[] page:m.getVideoMemory())java.util.Arrays.fill(page,(byte)0);
            m.cpu.setRegAF(0);m.cpu.setFlags(0);m.cpu.setRegBC(0);m.cpu.setRegDE(0);m.cpu.setRegHL(0);m.cpu.setMemPtr(0);
            Grafik fresh=new Grafik();fresh.Init();set(m,"graf",fresh);
            m.Reset(true);for(int i=0;i<120;i++)m.ms20();
            if(!m.saveSnapshot(args[0]+"/boot-"+main+"-"+video+".isn"))throw new Exception(m.getLastSnapshotError());
            System.out.printf("boot %d %d %016x%n",main,video,imageHash(m));
        }
        Grafik g=(Grafik)field(m,"graf");
        for(int video=0;video<2;video++)for(int wide=0;wide<2;wide++) {
            m.getConfig().setVideo((byte)video);m.getConfig().V64ena32=wide!=0;
            byte[][] v=m.getVideoMemory();for(int i=0;i<2048;i++)v[i/1024][i%1024]=(byte)(i*37+(i>>5));
            // Force double-width delimiters at both odd and even positions, including line ends.
            for(int i=0;i<2048;i+=31)v[i/1024][i%1024]=127;
            for(int x=0;x<64;x++)for(int y=0;y<256;y++){g.D0=x;g.D1=y;g.BitAcces=false;g.wpD4(x*13+y*7);}
            m.redrawScreen();System.out.printf("video %d %d %016x%n",video,wide,imageHash(m));
        }
        System.exit(0);
    }
}
