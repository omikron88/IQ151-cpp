// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace iq151 {
struct Clock {
    int64_t tstates = 0;
    int64_t getTstates() const { return tstates; }
    void addTstates(int n) { tstates += n; }
    void reset() { tstates = 0; }
};
struct MemIoOps {
    virtual ~MemIoOps() {}
    virtual int fetchOpcode(int) = 0;
    virtual int peek8(int) = 0;
    virtual int peek16(int) = 0;
    virtual void poke8(int, int) = 0;
    virtual void poke16(int, int) = 0;
    virtual int inPort(int) = 0;
    virtual void outPort(int, int) = 0;
    virtual bool inSerial() { return false; }
    virtual void outSerial(bool) {}
};
struct NotifyOps {
    virtual ~NotifyOps() {}
    virtual int atAddress(int, int op) { return op; }
    virtual void execDone() {}
};
struct Pio8255Notify {
    virtual ~Pio8255Notify() {}
    virtual void OnCpuWriteA() {}
    virtual void OnCpuWriteB() {}
    virtual void OnCpuWriteC() {}
    virtual void OnCpuWriteCL() {}
    virtual void OnCpuWriteCH() {}
    virtual void OnCpuWriteCWR(int) {}
    virtual void OnCpuReadA() {}
    virtual void OnCpuReadB() {}
    virtual void OnCpuReadC() {}
    virtual void OnCpuReadCL() {}
    virtual void OnCpuReadCH() {}
};
}
