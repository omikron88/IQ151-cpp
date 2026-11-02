// SPDX-License-Identifier: GPL-3.0-or-later
// Port of Pio8255.java; Copyright (c) 2006 Roman Borik <pmd85emu@gmail.com>.
#pragma once
#include "bus.hpp"
#include <array>
namespace iq151 { class Pio8255 {

public:
    static const int PP_PortA   = 0;
    static const int PP_PortB   = 1;
    static const int PP_PortC   = 2;
    static const int PP_CWR     = 3;
    static const int PP_PortCH  = 4;
    static const int PP_PortCL  = 5;

private:
    static const int PP_Bit0   = 0;
    static const int PP_Bit1   = 1;
    static const int PP_Bit2   = 2;
    static const int PP_Bit3   = 3;
    static const int PP_Bit4   = 4;
    static const int PP_Bit5   = 5;
    static const int PP_Bit6   = 6;
    static const int PP_Bit7   = 7;

    static const int CWR_MASK  = 0x80;

    static const int GA_MODE   = 0x60;
    static const int GA_MODE0  = 0x00;
    static const int GA_MODE1  = 0x20;
    static const int GA_MODE2  = 0x40;
    static const int PORTA_DIR = 0x10;
    static const int PORTA_INP = 0x10;
    static const int PORTA_OUT = 0x00;
    static const int PORTCH_DIR= 0x08;
    static const int PORTCH_INP= 0x08;
    static const int PORTCH_OUT= 0x00;

    static const int GB_MODE   = 0x04;
    static const int GB_MODE0  = 0x00;
    static const int GB_MODE1  = 0x04;
    static const int PORTB_DIR = 0x02;
    static const int PORTB_INP = 0x02;
    static const int PORTB_OUT = 0x00;
    static const int PORTCL_DIR= 0x01;
    static const int PORTCL_INP= 0x01;
    static const int PORTCL_OUT= 0x00;

    static const int BASIC_CWR     =
        (CWR_MASK | GA_MODE0 | PORTA_INP | PORTCH_INP | GB_MODE0 | PORTB_INP | PORTCL_INP);

    static const int _STBA         = PP_Bit4;
    static const int _STBA_MASK    = (1 << _STBA);
    static const int IBFA          = PP_Bit5;
    static const int IBFA_MASK     = (1 << IBFA);
    static const int _OBFA         = PP_Bit7;
    static const int _OBFA_MASK    = (1 << _OBFA);
    static const int _ACKA         = PP_Bit6;
    static const int _ACKA_MASK    = (1 << _ACKA);
    static const int INTRA         = PP_Bit3;
    static const int INTRA_MASK    = (1 << INTRA);
    static const int INTEAIN       = PP_Bit4;
    static const int INTEAIN_MASK  = (1 << INTEAIN);
    static const int INTEAOUT      = PP_Bit6;
    static const int INTEAOUT_MASK = (1 << INTEAOUT);

    static const int _STBB         = PP_Bit2;
    static const int _STBB_MASK    = (1 << _STBB);
    static const int IBFB          = PP_Bit1;
    static const int IBFB_MASK     = (1 << IBFB);
    static const int _OBFB         = PP_Bit1;
    static const int _OBFB_MASK    = (1 << _OBFB);
    static const int _ACKB         = PP_Bit2;
    static const int _ACKB_MASK    = (1 << _ACKB);
    static const int INTRB         = PP_Bit0;
    static const int INTRB_MASK    = (1 << INTRB);
    static const int INTEB         = PP_Bit2;
    static const int INTEB_MASK    = (1 << INTEB);


    int CWR{};
    int InBufferA{};
    int InLatchA{};
    int OutLatchA{};
    int InBufferB{};
    int InLatchB{};
    int OutLatchB{};
    int InBufferC{};
    int OutLatchC{};


    bool InteAin{};
    bool InteAout{};
    bool InteB{};

    Pio8255Notify& on;


public:
    explicit Pio8255(Pio8255Notify& notify) : on(notify) {
    Reset();
}


    void Reset()
{

        CpuWrite(PP_CWR, BASIC_CWR);
        InLatchA = 0;
        InLatchB = 0;
}

    std::array<int,5> getState()
{
        std::array<int,5> res{};

        res[0] = CWR;
	res[1] = OutLatchC;
	res[2] = OutLatchB;
	res[3] = OutLatchA;
	res[4] = (int) (
		((InteAin == true) ? 1 : 0) |
		((InteAout == true) ? 2 : 0) |
		((InteB == true) ? 4 : 0)
		);

	return res;
}

    void setState(const std::array<int,5>& state)
{
	if (true) {
		Reset();
		CpuWrite(PP_CWR, state[0]);
		CpuWrite(PP_PortC, state[1]);
		CpuWrite(PP_PortB, state[2]);
		CpuWrite(PP_PortA, state[3]);
		if ((state[4] & 1) != 0)
			CpuWrite(PP_CWR, 9);
		if ((state[4] & 2) != 0)
			CpuWrite(PP_CWR, 13);
		if ((state[4] & 4) != 0)
			CpuWrite(PP_CWR, 5);
	}
}

private:
    void NotifyOnWritePortC(int oldVal, int newVal)
{
	int val{};

	val = oldVal ^ newVal;
	if (val!=0)
		on.OnCpuWriteC();
	else {
		if ((val & 0xF0) !=0)
			on.OnCpuWriteCH();
		if ((val & 0x0F) !=0)
			on.OnCpuWriteCL();
	}
}

public:
    void CpuWrite(int dest, int val)
{
	int oldVal{};
	int mode{};

	switch (dest) {
		case PP_PortA :
			if ((CWR & GA_MODE) == GA_MODE0) {

				OutLatchA = val;
				on.OnCpuWriteA();
			}
			else {
				OutLatchA = val;
				oldVal = OutLatchC;
				OutLatchC &= (int)(~(INTRA_MASK | _OBFA_MASK));

				NotifyOnWritePortC(oldVal, OutLatchC);
			}
			break;

		case PP_PortB :
			if ((CWR & GB_MODE) == GB_MODE0) {

				OutLatchB = val;
				on.OnCpuWriteB();
			}
			else {
				OutLatchB = val;
				oldVal = OutLatchC;
				OutLatchC &= ~(INTRB_MASK | _OBFB_MASK);

				NotifyOnWritePortC(oldVal, OutLatchC);
			}
			break;

		case PP_PortC :

			oldVal = OutLatchC;

			if ((CWR & (GB_MODE | PORTCL_DIR)) == (GB_MODE0 | PORTCL_OUT)
					&& (CWR & (GA_MODE | PORTCH_DIR)) == (GA_MODE0 | PORTCH_OUT))
				OutLatchC = val;
			else if ((CWR & (GB_MODE | PORTCL_DIR)) == (GB_MODE0 | PORTCL_OUT))
				OutLatchC = (int)((OutLatchC & 0xF0) | (val & 0x0F));
			else if ((CWR & (GA_MODE | PORTCH_DIR)) == (GA_MODE0 | PORTCH_OUT))
				OutLatchC = (int)((OutLatchC & 0x0F) | (val & 0xF0));

			NotifyOnWritePortC(oldVal, OutLatchC);
			break;

		case PP_CWR :
			if ((val & CWR_MASK) !=0) {

				CWR = val;
				if ((CWR & GA_MODE) == GA_MODE)
					CWR &= ~GA_MODE1;

				InBufferA = 0xFF;

				OutLatchA = 0;

				InBufferB = 0xFF;
				if ((CWR & GB_MODE) != GB_MODE0)
					InLatchB = 0;
				OutLatchB = 0;

				InBufferC = 0;
				if ((CWR & GB_MODE) == GB_MODE0)
					InBufferC |= 0x07;
				if ((CWR & GA_MODE) == GA_MODE0)
					InBufferC |= 0xF8;

				OutLatchC = 0;
				if ((CWR & GB_MODE) != GB_MODE0)
					OutLatchC |= _OBFB_MASK;
				if ((CWR & GA_MODE) != GA_MODE0)
					OutLatchC |= _OBFA_MASK;

				InteAin = false;
				InteAout = false;
				InteB = false;

				on.OnCpuWriteCWR(CWR);

				NotifyOnWritePortC((int)(~OutLatchC), OutLatchC);
			}
			else {

				bool inte = false;
				val &= 0x0F;

				mode = (int)(CWR & (GA_MODE | PORTA_DIR));
				oldVal = OutLatchC;

				if ((mode == (GA_MODE1 | PORTA_INP) || (CWR & GA_MODE) == GA_MODE2) && (val & 0x0E) == 8) {
					InteAin = ((val & 1)!=0);
					inte = true;

				}
				else if ((mode == (GA_MODE1 | PORTA_OUT) || (CWR & GA_MODE) == GA_MODE2) && (val & 0x0E) == 12) {
					InteAout = ((val & 1)!=0);
					inte = true;

				}

				if (inte == true) {
					OutLatchC &= ~INTRA_MASK;
					if ((InteAin == true && (InBufferC & _STBA_MASK) == _STBA_MASK
							 && (OutLatchC & IBFA_MASK) == IBFA_MASK)
							|| (InteAout == true && (InBufferC & _ACKA_MASK) == _ACKA_MASK
									&& (OutLatchC & _OBFA_MASK) == _OBFA_MASK)) {
						OutLatchC |= INTRA_MASK;
					}

				}

				if ((CWR & GB_MODE) == GB_MODE1 && (val & 0x0E) == 4) {
					InteB = ((val & 1)!=0);
					OutLatchC &= ~INTRB_MASK;
					if (InteB == true) {
						if (((CWR & PORTB_INP) == PORTB_INP
								 && (InBufferC & _STBB_MASK) == _STBB_MASK
								 && (OutLatchC & IBFB_MASK) == IBFB_MASK)
								|| ((CWR & PORTB_OUT) == PORTB_OUT
										&& (InBufferC & _ACKB_MASK) == _ACKB_MASK
										&& (OutLatchC & _OBFB_MASK) == _OBFB_MASK)) {
							OutLatchC |= INTRB_MASK;
						}
					}
					inte = true;
				}

				if (inte == false) {
					if (((CWR & (GB_MODE | PORTCL_DIR)) == (GB_MODE0 | PORTCL_OUT)
						 && (CWR & (GA_MODE | PORTCH_DIR)) == (GA_MODE0 | PORTCH_OUT))
						|| ((CWR & (GB_MODE | PORTCL_DIR)) == (GB_MODE0 | PORTCL_OUT)
								&& (CWR & (GA_MODE | PORTCH_DIR)) == (GA_MODE0 | PORTCH_INP)
								&& (val >> 1) < 4)
						|| ((CWR & (GB_MODE | PORTCL_DIR)) == (GB_MODE0 | PORTCL_INP)
								&& (CWR & (GA_MODE | PORTCH_DIR)) == (GA_MODE0 | PORTCH_OUT)
								&& (val >> 1) > 3)
						|| ((CWR & (GB_MODE | PORTCL_DIR)) == (GB_MODE0 | PORTCL_OUT)
								&& (val >> 1) < 3)
						|| ((CWR & (GA_MODE | PORTCH_DIR)) == (GA_MODE0 | PORTCH_OUT)
								&& (val >> 1) > 2))
					{
						if ((val & 1)!=0)
							OutLatchC |= (int)((1 << (val >> 1)));
						else
							OutLatchC &= (int)(~(1 << (val >> 1)));
					}
				}

				NotifyOnWritePortC(oldVal, OutLatchC);
			}
			break;

		default :

			break;
	}
}

    int CpuRead(int src)
{
    int ret_val = 0;
    int oldVal{};
    int mode{};

    switch (src) {
        case PP_PortA :
            mode = (int)(CWR & (GA_MODE | PORTA_DIR));


            if (mode == (GA_MODE0 | PORTA_INP))
                on.OnCpuReadA();

            if (mode == (GA_MODE0 | PORTA_INP))
                ret_val = InBufferA;
            if (mode == (GA_MODE0 | PORTA_OUT))

                ret_val = OutLatchA & InBufferA;
            if (mode == (GA_MODE1 | PORTA_OUT))
                ret_val = OutLatchA;
            if (mode == (GA_MODE1 | PORTA_INP)
             || (CWR & GA_MODE) == GA_MODE2) {
                ret_val = InLatchA;

                oldVal = OutLatchC;
                OutLatchC &= ~(INTRA_MASK | IBFA_MASK);
                NotifyOnWritePortC(oldVal, OutLatchC);
            }
            break;

        case PP_PortB :
            mode = (int)(CWR & (GB_MODE | PORTB_DIR));


            if (mode == (GB_MODE0 | PORTB_INP))
                on.OnCpuReadB();

            if (mode == (GB_MODE0 | PORTB_OUT))
                ret_val = OutLatchB;
            if (mode == (GB_MODE0 | PORTB_INP))
                ret_val = InBufferB;
            if (mode == (GB_MODE1 | PORTB_OUT))
                ret_val = OutLatchB;
            if (mode == (GB_MODE1 | PORTB_INP)) {
                ret_val = InLatchB;

                oldVal = OutLatchC;
                OutLatchC &= ~(INTRB_MASK | IBFB_MASK);
                NotifyOnWritePortC(oldVal, OutLatchC);
            }
            break;

        case PP_PortC :
            ret_val = 0;

            if ((CWR & (GA_MODE | PORTCH_DIR)) == (GA_MODE0 | PORTCH_INP))
                on.OnCpuReadCH();

            if ((CWR & (GB_MODE | PORTCL_DIR)) == (GB_MODE0 | PORTCL_INP))
                on.OnCpuReadCL();

            if ((CWR & (GB_MODE | PORTCL_DIR)) == (GB_MODE0 | PORTCL_INP)
            || (CWR & (GA_MODE | PORTCH_DIR)) == (GA_MODE0 | PORTCH_INP))
                on.OnCpuReadC();

            if ((CWR & (GB_MODE | PORTCL_DIR)) == (GB_MODE0 | PORTCL_OUT))
                ret_val |= (int)(OutLatchC & 0x0F);
            if ((CWR & (GB_MODE | PORTCL_DIR)) == (GB_MODE0 | PORTCL_INP))
                ret_val |= (int)(InBufferC & 0x0F);
            if ((CWR & (GA_MODE | PORTCH_DIR)) == (GA_MODE0 | PORTCH_OUT))
                ret_val |= (int)(OutLatchC & 0xF0);
            if ((CWR & (GA_MODE | PORTCH_DIR)) == (GA_MODE0 | PORTCH_INP))
                ret_val |= (int)(InBufferC & 0xF0);

            switch (CWR & GA_MODE) {
                case GA_MODE1 :
                    if ((CWR & PORTA_DIR) == PORTA_INP) {
                        ret_val &= 0xC7;
                        if (InteAin == true)
                            ret_val |= INTEAIN_MASK;
                        ret_val |= (int)(OutLatchC & IBFA_MASK);
                    }
                    else {
                        ret_val &= 0x37;
                        if (InteAout == true)
                            ret_val |= INTEAOUT_MASK;
                        ret_val |= (int)(OutLatchC & _OBFA_MASK);
                    }
                    ret_val |= (int)(OutLatchC & INTRA_MASK);
                    break;

                case GA_MODE2 :
                    ret_val &= 0x07;
                    if (InteAin == true)
                        ret_val |= INTEAIN_MASK;
                    ret_val |= (int)(OutLatchC & IBFA_MASK);
                    if (InteAout == true)
                        ret_val |= INTEAOUT_MASK;
                    ret_val |= (int)(OutLatchC & _OBFA_MASK);
                    ret_val |= (int)(OutLatchC & INTRA_MASK);
                    break;
            }

            if ((CWR & GB_MODE) == GB_MODE1) {
                ret_val &= 0xF8;
                if (InteB == true)
                    ret_val |= INTEB_MASK;
                ret_val |= (int)(OutLatchC & INTRB_MASK);
                if ((CWR & PORTB_DIR) == PORTB_INP)
                    ret_val |= (int)(OutLatchC & IBFB_MASK);
                else
                    ret_val |= (int)(OutLatchC & _OBFB_MASK);
                if ((CWR & GA_MODE) == GA_MODE0) {
                    ret_val &= 0xF7;
                    if ((CWR & PORTCH_DIR) == PORTCH_INP)
                        ret_val |= (int)(InBufferC & 0x08);
                    else
                        ret_val |= (int)(OutLatchC & 0x08);
                }
            }
            break;

        case PP_CWR :
            ret_val = CWR;
            break;

        default :

            ret_val = 0;
            break;
    }
    return ret_val;
}

    void PeripheralWriteByte(int dest, int val)
{
	int oldVal{};

	switch (dest) {
		case PP_PortA :
			InBufferA = val;
			break;

		case PP_PortB :
			InBufferB = val;
			break;

		case PP_PortC :
			oldVal = OutLatchC;
			if ((CWR & GB_MODE) == GB_MODE1) {

				if ((CWR & PORTB_DIR) == PORTB_INP) {
					if ((InBufferC & _STBB_MASK) == _STBB_MASK && (val & _STBB_MASK) == 0) {

						InLatchB = InBufferB;
						OutLatchC |= IBFB_MASK;
						OutLatchC &= ~INTRB_MASK;
					}
					else if ((InBufferC & _STBB_MASK) == 0 && (val & _STBB_MASK) == _STBB_MASK) {

						if (InteB == true && (OutLatchC & IBFB_MASK) == IBFB_MASK)
							OutLatchC |= INTRB_MASK;
						else
							OutLatchC &= ~INTRB_MASK;
					}
				}
				else {
					if ((InBufferC & _ACKB_MASK) == _ACKB_MASK && (val & _ACKB_MASK) == 0) {

						OutLatchC |= _OBFB_MASK;
						OutLatchC &= ~INTRB_MASK;
					}
					else if ((InBufferC & _ACKB_MASK) == 0 && (val & _ACKB_MASK) == _ACKB_MASK) {

						if (InteB == true && (OutLatchC & _OBFB_MASK) == _OBFB_MASK)
							OutLatchC |= INTRB_MASK;
						else
							OutLatchC &= ~INTRB_MASK;
					}
				}
			}

			if ((CWR & GA_MODE) != GA_MODE0) {

				if ((CWR & GA_MODE) == GA_MODE2
						|| (CWR & (GA_MODE | PORTA_DIR)) == (GA_MODE1 | PORTA_INP)) {
					if ((InBufferC & _STBA_MASK) == _STBA_MASK && (val & _STBA_MASK) == 0) {

						InLatchA = InBufferA;
						OutLatchC |= IBFA_MASK;
						OutLatchC &= ~INTRA_MASK;
					}
					else if ((InBufferC & _STBA_MASK) == 0 && (val & _STBA_MASK) == _STBA_MASK) {

						if (InteAin == true && (OutLatchC & IBFA_MASK) == IBFA_MASK)
							OutLatchC |= INTRA_MASK;
						else
							OutLatchC &= ~INTRA_MASK;
					}
				}

				if ((CWR & GA_MODE) == GA_MODE2
						|| (CWR & (GA_MODE | PORTA_DIR)) == (GA_MODE1 | PORTA_OUT)) {
					if ((InBufferC & _ACKA_MASK) == _ACKA_MASK && (val & _ACKA_MASK) == 0) {

						OutLatchC |= _OBFA_MASK;
						OutLatchC &= ~INTRA_MASK;
					}
					else if ((InBufferC & _ACKA_MASK) == 0 && (val & _ACKA_MASK) == _ACKA_MASK) {

						if (InteAout == true && (OutLatchC & _OBFA_MASK) == _OBFA_MASK)
							OutLatchC |= INTRA_MASK;
						else
							OutLatchC &= ~INTRA_MASK;
					}
				}
			}

			InBufferC = val;
			NotifyOnWritePortC(oldVal, OutLatchC);
			break;

		default :

                break;
	}
}

    void PeripheralChangeBit(int dest, int bit, bool state)
{
	int val{};

	switch (dest) {
		case PP_PortA :
			InBufferA &= (int)(~(1 << bit));
			InBufferA |= (int)((state == true) ? (1 << bit) : 0);
			break;

		case PP_PortB :
			InBufferB &= (int)(~(1 << bit));
			InBufferB |= (int)((state == true) ? (1 << bit) : 0);
			break;

		case PP_PortC :
			if ((CWR & GB_MODE) == GB_MODE1 && (bit == _STBB || bit == _ACKB)) {
				if (bit == _STBB)
					val = (int)((InBufferC & ~_STBB_MASK) | ((state == true) ? _STBB_MASK : 0));
				else
					val = (int)((InBufferC & ~_ACKB_MASK) | ((state == true) ? _ACKB_MASK : 0));
				PeripheralWriteByte(dest, val);
			}
			else if ((CWR & GA_MODE) != GA_MODE0 && (bit == _STBA || bit == _ACKA)) {
				if (bit == _STBA)
					val = (int)((InBufferC & ~_STBA_MASK) | ((state == true) ? _STBA_MASK : 0));
				else
					val = (int)((InBufferC & ~_ACKA_MASK) | ((state == true) ? _ACKA_MASK : 0));
				PeripheralWriteByte(dest, val);
			}
			else {
				InBufferC &= (int)(~(1 << bit));
				InBufferC |= (int)((state == true) ? (1 << bit) : 0);
			}
			break;

		default :

			break;
	}
}

    int PeripheralReadByte(int src)
{
	int ret{};

	switch (src) {
		case PP_PortA :
			if ((CWR & GA_MODE) == GA_MODE2) {
				if ((InBufferC & _ACKA_MASK) == 0)
					ret = OutLatchA;
				else if ((InBufferC & _STBA_MASK) == 0)
					ret = InBufferA;
				else
					ret = 0xFF;
			}
			else {
				if ((CWR & PORTA_DIR) == PORTA_OUT)
					ret = OutLatchA;
				else
					ret = InBufferA;
			}
			break;

		case PP_PortB :
			if ((CWR & PORTB_DIR) == PORTB_OUT)
				ret = OutLatchB;
			else
				ret = InBufferB;
			break;

		case PP_PortC :
			ret = 0;
			if ((CWR & (GB_MODE | PORTCL_DIR)) == (GB_MODE0 | PORTCL_OUT))
				ret |= (int)(OutLatchC & 0x07);
			else if ((CWR & (GB_MODE | PORTCL_DIR)) == (GB_MODE0 | PORTCL_INP))
				ret |= (int)(InBufferC & 0x07);
			else if ((CWR & GB_MODE) == GB_MODE1) {
				ret |= (int)(OutLatchC & 0x03);
				ret |= (int)(InBufferC & 0x04);
			}

			if ((CWR & (GA_MODE | PORTCH_DIR)) == (GA_MODE0 | PORTCH_OUT))
				ret |= (int)(OutLatchC & 0xF0);
			else if ((CWR & (GA_MODE | PORTCH_DIR)) == (GA_MODE0 | PORTCH_INP))
				ret |= (int)(InBufferC & 0xF0);
			else if ((CWR & (GA_MODE | PORTA_DIR)) == (GA_MODE1 | PORTA_OUT)) {
				ret |= (int)(OutLatchC & 0x88);
				ret |= (int)(InBufferC & 0x70);
			}
			else if ((CWR & (GA_MODE | PORTA_DIR)) == (GA_MODE1 | PORTA_INP)) {
				ret |= (int)(OutLatchC & 0x28);
				ret |= (int)(InBufferC & 0xD0);
			}
			else if ((CWR & GA_MODE) == GA_MODE2) {
				ret |= (int)(OutLatchC & 0xA8);
				ret |= (int)(InBufferC & 0x50);
			}

			if ((CWR & GA_MODE) == GA_MODE0) {
				if ((CWR & (GB_MODE | PORTCL_DIR)) == (GB_MODE0 | PORTCL_OUT))
					ret |= (int)(OutLatchC & 0x08);
				else if ((CWR & (GB_MODE | PORTCL_DIR)) == (GB_MODE0 | PORTCL_INP))
					ret |= (int)(InBufferC & 0x08);
			}
			break;

		default :

			ret = 0xFF;
			break;
	}

	return ret;
}

    bool PeripheralReadBit(int src, int bit)
{
	bool ret{};

	switch (src) {
		case PP_PortA :
			ret = ((PeripheralReadByte(PP_PortA) & (1 << bit))!=0);
			break;

		case PP_PortB :
			ret = ((PeripheralReadByte(PP_PortB) & (1 << bit))!=0);
			break;

		case PP_PortC :
			ret = ((PeripheralReadByte(PP_PortC) & (1 << bit))!=0);
			break;

		default :

			ret = false;
			break;
	}

	return ret;
}


}; }
