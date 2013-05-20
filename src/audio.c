/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - audio.c                                         *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2012 Bobby Smiles                                       *
 *   Copyright (C) 2009 Richard Goedeken                                   *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdio.h>
#include <string.h>
#include "hle.h"
#include "m64p_types.h"

typedef void (*acmd_callback_t)(u32 inst1, u32 inst2);

/*
 * Audio flags
 */

#define A_INIT          0x01
#define A_CONTINUE      0x00
#define A_LOOP          0x02
#define A_OUT           0x02
#define A_LEFT          0x02
#define A_RIGHT         0x00
#define A_VOL           0x04
#define A_RATE          0x00
#define A_AUX           0x08
#define A_NOAUX         0x00
#define A_MAIN          0x00
#define A_MIX           0x10

// FIXME: this decomposition into 3 ABI is not accurate,
// there are a least 9 or 10 different ABI, each with one or a few revisions
// for a total of almost 16 differents audio ucode.
//
// ABI2 in fact is a mix of at least 7 differents ABI which are mostly compatible
// but not totally, that's why there is a isZeldaABI/isMKABI workaround.
//
static const acmd_callback_t ABI1[0x10];
static const acmd_callback_t ABI2[0x20];
static const acmd_callback_t ABI3[0x10];

/* local functions */
static void alist_process(const acmd_callback_t abi[], unsigned int abi_size)
{
    u32 inst1, inst2;
    unsigned int acmd;
    const OSTask_t * const task = get_task();

    const unsigned int *alist = (unsigned int*)(rsp.RDRAM + task->data_ptr);
    const unsigned int * const alist_end = alist + (task->data_size >> 2);

    while (alist != alist_end)
    {
        inst1 = *(alist++);
        inst2 = *(alist++);

        acmd = inst1 >> 24;

        if (acmd < abi_size)
        {
            (*abi[acmd])(inst1, inst2);
        }
        else
        {
            DebugMessage(M64MSG_WARNING, "Invalid ABI command %u", acmd);
        }
    }
}

/* global functions */
void alist_process_ABI1()
{
    alist_process(ABI1, 0x10);
}

void alist_process_ABI2()
{
    alist_process(ABI2, 0x20);
}

void alist_process_ABI3()
{
    alist_process(ABI3, 0x10);
}


/* audio ucode */
static unsigned align(unsigned x, unsigned m)
{
    --m;
    return (x + m) & (~m);
}

static s16 clamp_s16(s32 x)
{
    if (x > 32767) { x = 32767; } else if (x < -32768) { x = -32768; }
    return x;
}

static unsigned parse_flags(u32 w1)
{
    return (w1 >> 16) & 0xff;
}

static u32 parse_address(u32 w2)
{
    // ignore segments (always zero in practice)
    return (w2 & 0xffffff); // audio.segments[(w2 >> 24) & 0xff];
}

static u16 parse_lo(u32 x)
{
    return x & 0xffff;
}

static u16 parse_hi(u32 x)
{
    return (x >> 16) & 0xffff;
}


//#include "rsp.h"
//#define SAFE_MEMORY
/*
#ifndef SAFE_MEMORY
#   define wr8 (src , address);
#   define rd8 (dest, address);
#   define wr16 (src, address);
#   define rd16 (dest, address);
#   define wr32 (src, address);
#   define rd32 (dest, address);
#   define wr64 (src, address);
#   define rd64 (dest, address);
#   define dmamem (dest, src, size) memcpy (dest, src, size);
#   define clrmem (dest, size)      memset (dest, 0, size);
#else
    void wr8 (u8 src, void *address);
    void rd8 (u8 dest, void *address);
    void wr16 (u16 src, void *address);
    void rd16 (u16 dest, void *address);
    void wr32 (u16 src, void *address);
    void rd32 (u16 dest, void *address);
    void wr64 (u16 src, void *address);
    void rd64 (u16 dest, void *address);
    void dmamem (void *dest, void *src, int size);
    void clrmem (void *dest, int size);
#endif
*/
/******** DMEM Memory Map for ABI 1 ***************
Address/Range       Description
-------------       -------------------------------
0x000..0x2BF        UCodeData
    0x000-0x00F     Constants  - 0000 0001 0002 FFFF 0020 0800 7FFF 4000
    0x010-0x02F     Function Jump Table (16 Functions * 2 bytes each = 32) 0x20
    0x030-0x03F     Constants  - F000 0F00 00F0 000F 0001 0010 0100 1000
    0x040-0x03F     Used by the Envelope Mixer (But what for?)
    0x070-0x07F     Used by the Envelope Mixer (But what for?)
0x2C0..0x31F        <Unknown>
0x320..0x35F        Segments
0x360               Audio In Buffer (Location)
0x362               Audio Out Buffer (Location)
0x364               Audio Buffer Size (Location)
0x366               Initial Volume for Left Channel
0x368               Initial Volume for Right Channel
0x36A               Auxillary Buffer #1 (Location)
0x36C               Auxillary Buffer #2 (Location)
0x36E               Auxillary Buffer #3 (Location)
0x370               Loop Value (shared location)
0x370               Target Volume (Left)
0x372               Ramp?? (Left)
0x374               Rate?? (Left)
0x376               Target Volume (Right)
0x378               Ramp?? (Right)
0x37A               Rate?? (Right)
0x37C               Dry??
0x37E               Wet??
0x380..0x4BF        Alist data
0x4C0..0x4FF        ADPCM CodeBook
0x500..0x5BF        <Unknown>
0x5C0..0xF7F        Buffers...
0xF80..0xFFF        <Unknown>
***************************************************/
#ifdef USE_EXPANSION
    #define MEMMASK 0x7FFFFF
#else
    #define MEMMASK 0x3FFFFF
#endif

static void SPNOOP(u32 inst1, u32 inst2)
{
}

static struct audio_t
{
    // segments
    u32 segments[0x10]; // 0x320

    // main buffers
    u16 in;             // 0x0000(t8)
    u16 out;            // 0x0002(t8)
    u16 count;          // 0x0004(t8)

    // auxiliary buffers
    u16 aux_dry_left;   // 0x000a(t8)
    u16 aux_wet_right;  // 0x000c(t8)
    u16 aux_wet_left;   // 0x000e(t8)

    // loop
    u32 loop;           // 0x0010(t8)

    // envmixer gains
    s16 dry;            // 0x001c(t8)
    s16 wet;            // 0x001e(t8)

    // envmixer envelopes (0: left, 1: right)
    s16 env_vol[2];
    s16 env_target[2];
    s32 env_ramp[2];

    // adpcm
    u16 adpcm_table[0x80];
} audio;

u8 BufferSpace[0x10000];

static const u16 ResampleLUT [0x200] = {
    0x0C39, 0x66AD, 0x0D46, 0xFFDF, 0x0B39, 0x6696, 0x0E5F, 0xFFD8,
    0x0A44, 0x6669, 0x0F83, 0xFFD0, 0x095A, 0x6626, 0x10B4, 0xFFC8,
    0x087D, 0x65CD, 0x11F0, 0xFFBF, 0x07AB, 0x655E, 0x1338, 0xFFB6,
    0x06E4, 0x64D9, 0x148C, 0xFFAC, 0x0628, 0x643F, 0x15EB, 0xFFA1,
    0x0577, 0x638F, 0x1756, 0xFF96, 0x04D1, 0x62CB, 0x18CB, 0xFF8A,
    0x0435, 0x61F3, 0x1A4C, 0xFF7E, 0x03A4, 0x6106, 0x1BD7, 0xFF71,
    0x031C, 0x6007, 0x1D6C, 0xFF64, 0x029F, 0x5EF5, 0x1F0B, 0xFF56,
    0x022A, 0x5DD0, 0x20B3, 0xFF48, 0x01BE, 0x5C9A, 0x2264, 0xFF3A,
    0x015B, 0x5B53, 0x241E, 0xFF2C, 0x0101, 0x59FC, 0x25E0, 0xFF1E,
    0x00AE, 0x5896, 0x27A9, 0xFF10, 0x0063, 0x5720, 0x297A, 0xFF02,
    0x001F, 0x559D, 0x2B50, 0xFEF4, 0xFFE2, 0x540D, 0x2D2C, 0xFEE8,
    0xFFAC, 0x5270, 0x2F0D, 0xFEDB, 0xFF7C, 0x50C7, 0x30F3, 0xFED0,
    0xFF53, 0x4F14, 0x32DC, 0xFEC6, 0xFF2E, 0x4D57, 0x34C8, 0xFEBD,
    0xFF0F, 0x4B91, 0x36B6, 0xFEB6, 0xFEF5, 0x49C2, 0x38A5, 0xFEB0,
    0xFEDF, 0x47ED, 0x3A95, 0xFEAC, 0xFECE, 0x4611, 0x3C85, 0xFEAB,
    0xFEC0, 0x4430, 0x3E74, 0xFEAC, 0xFEB6, 0x424A, 0x4060, 0xFEAF,
    0xFEAF, 0x4060, 0x424A, 0xFEB6, 0xFEAC, 0x3E74, 0x4430, 0xFEC0,
    0xFEAB, 0x3C85, 0x4611, 0xFECE, 0xFEAC, 0x3A95, 0x47ED, 0xFEDF,
    0xFEB0, 0x38A5, 0x49C2, 0xFEF5, 0xFEB6, 0x36B6, 0x4B91, 0xFF0F,
    0xFEBD, 0x34C8, 0x4D57, 0xFF2E, 0xFEC6, 0x32DC, 0x4F14, 0xFF53,
    0xFED0, 0x30F3, 0x50C7, 0xFF7C, 0xFEDB, 0x2F0D, 0x5270, 0xFFAC,
    0xFEE8, 0x2D2C, 0x540D, 0xFFE2, 0xFEF4, 0x2B50, 0x559D, 0x001F,
    0xFF02, 0x297A, 0x5720, 0x0063, 0xFF10, 0x27A9, 0x5896, 0x00AE,
    0xFF1E, 0x25E0, 0x59FC, 0x0101, 0xFF2C, 0x241E, 0x5B53, 0x015B,
    0xFF3A, 0x2264, 0x5C9A, 0x01BE, 0xFF48, 0x20B3, 0x5DD0, 0x022A,
    0xFF56, 0x1F0B, 0x5EF5, 0x029F, 0xFF64, 0x1D6C, 0x6007, 0x031C,
    0xFF71, 0x1BD7, 0x6106, 0x03A4, 0xFF7E, 0x1A4C, 0x61F3, 0x0435,
    0xFF8A, 0x18CB, 0x62CB, 0x04D1, 0xFF96, 0x1756, 0x638F, 0x0577,
    0xFFA1, 0x15EB, 0x643F, 0x0628, 0xFFAC, 0x148C, 0x64D9, 0x06E4,
    0xFFB6, 0x1338, 0x655E, 0x07AB, 0xFFBF, 0x11F0, 0x65CD, 0x087D,
    0xFFC8, 0x10B4, 0x6626, 0x095A, 0xFFD0, 0x0F83, 0x6669, 0x0A44,
    0xFFD8, 0x0E5F, 0x6696, 0x0B39, 0xFFDF, 0x0D46, 0x66AD, 0x0C39
};

static void CLEARBUFF (u32 inst1, u32 inst2) {
    u16 addr = parse_lo(inst1) & ~3;
    u16 count = align(parse_lo(inst2), 4);
    
    memset(BufferSpace+addr, 0, count);
}

//FILE *dfile = fopen ("d:\\envmix.txt", "wt");

static void ENVMIXER (u32 inst1, u32 inst2) {
    int x,y;
    short state_buffer[40];
    //static int envmixcnt = 0;
    unsigned flags = parse_flags(inst1);
    u32 addy = parse_address(inst2);
    //static
// ********* Make sure these conditions are met... ***********
    /*if ((audio.in | audio.out | audio.aux_dry_left | audio.aux_wet_right | audio.aux_wet_left | audio.count) & 0x3) {
        MessageBox (NULL, "Unaligned EnvMixer... please report this to Azimer with the following information: RomTitle, Place in the rom it occurred, and any save state just before the error", "AudioHLE Error", MB_OK);
    }*/
// ------------------------------------------------------------
    short *inp=(short *)(BufferSpace+audio.in);
    short *out=(short *)(BufferSpace+audio.out);
    short *aux1=(short *)(BufferSpace+audio.aux_dry_left);
    short *aux2=(short *)(BufferSpace+audio.aux_wet_right);
    short *aux3=(short *)(BufferSpace+audio.aux_wet_left);
    s32 MainR;
    s32 MainL;
    s32 AuxR;
    s32 AuxL;
    int i1,o1,a1,a2=0,a3=0;
    unsigned short AuxIncRate=1;
    short zero[8];
    memset(zero,0,16);
    s32 LVol, RVol;
    s32 LAcc, RAcc;
    s32 LTrg, RTrg;
    s16 Wet, Dry;
    u32 ptr = 0;
    s32 RRamp, LRamp;
    s32 LAdderStart, RAdderStart, LAdderEnd, RAdderEnd;
    s32 oMainR, oMainL, oAuxR, oAuxL;

    //envmixcnt++;

    //fprintf (dfile, "\n----------------------------------------------------\n");
    if (flags & A_INIT) {
        LVol = ((audio.env_vol[0] * (s32)audio.env_ramp[0]));
        RVol = ((audio.env_vol[1] * (s32)audio.env_ramp[1]));
        Wet = audio.wet;
        Dry = audio.dry; // Save Wet/Dry values
        LTrg = (audio.env_target[0] << 16);
        RTrg = (audio.env_target[1] << 16); // Save Current Left/Right Targets
        LAdderStart = audio.env_vol[0] << 16;
        RAdderStart = audio.env_vol[1] << 16;
        LAdderEnd = LVol;
        RAdderEnd = RVol;
        LRamp = audio.env_ramp[0];
        RRamp = audio.env_ramp[1];
    } else {
        // Load LVol, RVol, LAcc, and RAcc (all 32bit)
        // Load Wet, Dry, LTrg, RTrg
        memcpy((u8 *)state_buffer, (rsp.RDRAM+addy), 80);
        Wet  = *(s16 *)(state_buffer +  0); // 0-1
        Dry  = *(s16 *)(state_buffer +  2); // 2-3
        LTrg = *(s32 *)(state_buffer +  4); // 4-5
        RTrg = *(s32 *)(state_buffer +  6); // 6-7
        LRamp= *(s32 *)(state_buffer +  8); // 8-9 (state_buffer is a 16bit pointer)
        RRamp= *(s32 *)(state_buffer + 10); // 10-11
        LAdderEnd = *(s32 *)(state_buffer + 12); // 12-13
        RAdderEnd = *(s32 *)(state_buffer + 14); // 14-15
        LAdderStart = *(s32 *)(state_buffer + 16); // 12-13
        RAdderStart = *(s32 *)(state_buffer + 18); // 14-15
    }

    if (!(flags & A_AUX)) {
        AuxIncRate=0;
        aux2=aux3=zero;
    }

    oMainL = (Dry * (LTrg>>16) + 0x4000) >> 15;
    oAuxL  = (Wet * (LTrg>>16) + 0x4000)  >> 15;
    oMainR = (Dry * (RTrg>>16) + 0x4000) >> 15;
    oAuxR  = (Wet * (RTrg>>16) + 0x4000)  >> 15;

    for (y = 0; y < audio.count; y += 0x10) {

        if (LAdderStart != LTrg) {
            LAcc = LAdderStart;
            LVol = (LAdderEnd - LAdderStart) >> 3;
            LAdderEnd   = (s32) (((s64)LAdderEnd * (s64)LRamp) >> 16);
            LAdderStart = (s32) (((s64)LAcc * (s64)LRamp) >> 16);
        } else {
            LAcc = LTrg;
            LVol = 0;
        }

        if (RAdderStart != RTrg) {
            RAcc = RAdderStart;
            RVol = (RAdderEnd - RAdderStart) >> 3;
            RAdderEnd   = (s32) (((s64)RAdderEnd * (s64)RRamp) >> 16);
            RAdderStart = (s32) (((s64)RAcc * (s64)RRamp) >> 16);
        } else {
            RAcc = RTrg;
            RVol = 0;
        }

    for (x = 0; x < 8; x++) {
        i1=(int)inp[ptr^S];
        o1=(int)out[ptr^S];
        a1=(int)aux1[ptr^S];
        if (AuxIncRate) {
            a2=(int)aux2[ptr^S];
            a3=(int)aux3[ptr^S];
        }
        // TODO: here...
        //LAcc = LTrg;
        //RAcc = RTrg;

        LAcc += LVol;
        RAcc += RVol;

        if (LVol <= 0) { // Decrementing
            if (LAcc < LTrg) {
                LAcc = LTrg;
                LAdderStart = LTrg;
                MainL = oMainL;
                AuxL  = oAuxL;
            } else {
                MainL = (Dry * ((s32)LAcc>>16) + 0x4000) >> 15;
                AuxL  = (Wet * ((s32)LAcc>>16) + 0x4000)  >> 15;
            }
        } else {
            if (LAcc > LTrg) {
                LAcc = LTrg;
                LAdderStart = LTrg;
                MainL = oMainL;
                AuxL  = oAuxL;
            } else {
                MainL = (Dry * ((s32)LAcc>>16) + 0x4000) >> 15;
                AuxL  = (Wet * ((s32)LAcc>>16) + 0x4000)  >> 15;
            }
        }

        if (RVol <= 0) { // Decrementing
            if (RAcc < RTrg) {
                RAcc = RTrg;
                RAdderStart = RTrg;
                MainR = oMainR;
                AuxR  = oAuxR;
            } else {
                MainR = (Dry * ((s32)RAcc>>16) + 0x4000) >> 15;
                AuxR  = (Wet * ((s32)RAcc>>16) + 0x4000)  >> 15;
            }
        } else {
            if (RAcc > RTrg) {
                RAcc = RTrg;
                RAdderStart = RTrg;
                MainR = oMainR;
                AuxR  = oAuxR;
            } else {
                MainR = (Dry * ((s32)RAcc>>16) + 0x4000) >> 15;
                AuxR  = (Wet * ((s32)RAcc>>16) + 0x4000)  >> 15;
            }
        }

        //fprintf (dfile, "%04X ", (LAcc>>16));

        /*MainL = (((s64)Dry*2 * (s64)(LAcc>>16)) + 0x8000) >> 16;
        MainR = (((s64)Dry*2 * (s64)(RAcc>>16)) + 0x8000) >> 16;
        AuxL  = (((s64)Wet*2 * (s64)(LAcc>>16)) + 0x8000) >> 16;
        AuxR  = (((s64)Wet*2 * (s64)(RAcc>>16)) + 0x8000) >> 16;*/
/*
        if (MainL>32767) MainL = 32767;
        else if (MainL<-32768) MainL = -32768;
        if (MainR>32767) MainR = 32767;
        else if (MainR<-32768) MainR = -32768;
        if (AuxL>32767) AuxL = 32767;
        else if (AuxL<-32768) AuxR = -32768;
        if (AuxR>32767) AuxR = 32767;
        else if (AuxR<-32768) AuxR = -32768;*/
        /*
        MainR = (Dry * RTrg + 0x10000) >> 15;
        MainL = (Dry * LTrg + 0x10000) >> 15;
        AuxR  = (Wet * RTrg + 0x8000)  >> 16;
        AuxL  = (Wet * LTrg + 0x8000)  >> 16;*/

        o1+=(/*(o1*0x7fff)+*/(i1*MainR)+0x4000)>>15;
        a1+=(/*(a1*0x7fff)+*/(i1*MainL)+0x4000)>>15;

/*      o1=((s64)(((s64)o1*0xfffe)+((s64)i1*MainR*2)+0x8000)>>16);

        a1=((s64)(((s64)a1*0xfffe)+((s64)i1*MainL*2)+0x8000)>>16);*/

        out[ptr^S]  = clamp_s16(o1);
        aux1[ptr^S] = clamp_s16(a1);
        if (AuxIncRate) {
            //a2=((s64)(((s64)a2*0xfffe)+((s64)i1*AuxR*2)+0x8000)>>16);

            //a3=((s64)(((s64)a3*0xfffe)+((s64)i1*AuxL*2)+0x8000)>>16);
            a2+=(/*(a2*0x7fff)+*/(i1*AuxR)+0x4000)>>15;
            a3+=(/*(a3*0x7fff)+*/(i1*AuxL)+0x4000)>>15;

            aux2[ptr^S] = clamp_s16(a2);
            aux3[ptr^S] = clamp_s16(a3);
        }
        ptr++;
    }
    }

    /*LAcc = LAdderEnd;
    RAcc = RAdderEnd;*/

    *(s16 *)(state_buffer +  0) = Wet; // 0-1
    *(s16 *)(state_buffer +  2) = Dry; // 2-3
    *(s32 *)(state_buffer +  4) = LTrg; // 4-5
    *(s32 *)(state_buffer +  6) = RTrg; // 6-7
    *(s32 *)(state_buffer +  8) = LRamp; // 8-9 (state_buffer is a 16bit pointer)
    *(s32 *)(state_buffer + 10) = RRamp; // 10-11
    *(s32 *)(state_buffer + 12) = LAdderEnd; // 12-13
    *(s32 *)(state_buffer + 14) = RAdderEnd; // 14-15
    *(s32 *)(state_buffer + 16) = LAdderStart; // 12-13
    *(s32 *)(state_buffer + 18) = RAdderStart; // 14-15
    memcpy(rsp.RDRAM+addy, (u8 *)state_buffer,80);
}

static void RESAMPLE (u32 inst1, u32 inst2) {
    unsigned flags = parse_flags(inst1);
    unsigned int Pitch= parse_lo(inst1) << 1;
    u32 addy = parse_address(inst2);
    unsigned int Accum=0;
    unsigned int location;
    s16 *lut/*, *lut2*/;
    short *dst;
    s16 *src;
    dst=(short *)(BufferSpace);
    src=(s16 *)(BufferSpace);
    u32 srcPtr=(audio.in/2);
    u32 dstPtr=(audio.out/2);
    s32 temp;
    s32 accum;
    int count = align(audio.count, 16) >> 1;
    int i;

/*
    if (addy > (1024*1024*8))
        addy = (inst2 & 0xffffff);
*/
    srcPtr -= 4;

    if (flags & A_INIT) {
        for (i=0; i < 4; i++)
            src[(srcPtr+i)^S] = 0;//*(u16 *)(rsp.RDRAM+((addy+i)^2));
    } else {
        //memcpy (src+srcPtr, rsp.RDRAM+addy, 0x8);
        for (i=0; i < 4; i++)
            src[(srcPtr+i)^S] = ((u16 *)rsp.RDRAM)[((addy/2)+i)^S];
        Accum = *(u16 *)(rsp.RDRAM+addy+10);
    }

    for(i=0; i < count; i++)
    {
        //location = (((Accum * 0x40) >> 0x10) * 8);
       // location is the fractional position between two samples
        location = (Accum >> 0xa) * 4;
        lut = (s16*)ResampleLUT + location;

        // mov eax, dword ptr [src+srcPtr];
        // movsx edx, word ptr [lut];
        // shl edx, 1
        // imul edx
        // test eax, 08000h
        // setz ecx
        // shl ecx, 16
        // xor eax, 08000h
        // add eax, ecx
        // and edx, 0f000h

        // imul
        temp =  ((s32)*(s16*)(src+((srcPtr+0)^S))*((s32)((s16)lut[0])));
        accum = (s32)(temp >> 15);

        temp = ((s32)*(s16*)(src+((srcPtr+1)^S))*((s32)((s16)lut[1])));
        accum += (s32)(temp >> 15);

        temp = ((s32)*(s16*)(src+((srcPtr+2)^S))*((s32)((s16)lut[2])));
        accum += (s32)(temp >> 15);

        temp = ((s32)*(s16*)(src+((srcPtr+3)^S))*((s32)((s16)lut[3])));
        accum += (s32)(temp >> 15);

        dst[dstPtr^S] = clamp_s16(accum);
        dstPtr++;
        Accum += Pitch;
        srcPtr += (Accum>>16);
        Accum&=0xffff;
    }
    for (i=0; i < 4; i++)
        ((u16 *)rsp.RDRAM)[((addy/2)+i)^S] = src[(srcPtr+i)^S];
    //memcpy (RSWORK, src+srcPtr, 0x8);
    *(u16 *)(rsp.RDRAM+addy+10) = Accum;
}

static void SETVOL (u32 inst1, u32 inst2) {
// Might be better to unpack these depending on the flags...
    unsigned flags = parse_flags(inst1);
    s16 vol = (s16)parse_lo(inst1);
    //u16 voltarg =(u16)((inst2 >> 16)&0xffff);
    s16 volrate = (s16)parse_lo(inst2);

    if (flags & A_AUX) {
        audio.dry = vol;         // m_MainVol
        audio.wet = volrate;     // m_AuxVol
        return;
    }

    if (flags & A_VOL) { // Set the Source(start) Volumes
        if (flags & A_LEFT) {
            audio.env_vol[0] = vol;    // m_LeftVolume
        } else { // A_RIGHT
            audio.env_vol[1] = vol;   // m_RightVolume
        }
        return;
    }

//0x370             Loop Value (shared location)
//0x370             Target Volume (Left)
//u16 env_ramp[0]; // 0x0012(T8)
    if (flags & A_LEFT) { // Set the Ramping values Target, Ramp
        //loopval = (((u32)vol << 0x10) | (u32)voltarg);
        audio.env_target[0]  = (s16)inst1;      // m_LeftVol
        //env_ramp[0] = (s32)inst2;
        audio.env_ramp[0] = (s32)inst2;//(u16)(inst2) | (s32)(s16)(inst2 << 0x10);
        //fprintf (dfile, "Ramp Left: %f\n", (float)env_ramp[0]/65536.0);
        //fprintf (dfile, "Ramp Left: %08X\n", inst2);
        //env_ramp[0] = (s16)voltarg;  // m_LeftVolTarget
        //VolRate_Left = (s16)volrate;  // m_LeftVolRate
    } else { // A_RIGHT
        audio.env_target[1]  = (s16)inst1;     // m_RightVol
        //env_ramp[1] = (s32)inst2;
        audio.env_ramp[1] = (s32)inst2;//(u16)(inst2 >> 0x10) | (s32)(s16)(inst2 << 0x10);
        //fprintf (dfile, "Ramp Right: %f\n", (float)env_ramp[1]/65536.0);
        //fprintf (dfile, "Ramp Right: %08X\n", inst2);
        //env_ramp[1] = (s16)voltarg; // m_RightVolTarget
        //VolRate_Right = (s16)volrate; // m_RightVolRate
    }
}

static void UNKNOWN (u32 inst1, u32 inst2) {}

static void SETLOOP (u32 inst1, u32 inst2) {
    audio.loop = parse_address(inst2);
    //env_target[0]  = (s16)(loopval>>16);        // m_LeftVol
    //env_ramp[0] = (s16)(loopval);    // m_LeftVolTarget
}

static void ADPCM (u32 inst1, u32 inst2) { // Work in progress! :)
    unsigned flags = parse_flags(inst1);
    //unsigned short Gain=(u16)(inst1&0xffff);
    u32 Address = parse_address(inst2);
    unsigned short inPtr=0;
    //short *out=(s16 *)(testbuff+(audio.out>>2));
    short *out=(short *)(BufferSpace+audio.out);
    //unsigned char *in=(unsigned char *)(BufferSpace+audio.in);
    short count=(short)audio.count;
    unsigned char icode;
    unsigned char code;
    int vscale;
    unsigned short index;
    unsigned short j;
    int a[8];
    short *book1,*book2;
/*
    if (Address > (1024*1024*8))
        Address = (inst2 & 0xffffff);
*/
    memset(out,0,32);

    if (!(flags & A_INIT))
    {
        if (flags & A_LOOP) {
            memcpy(out,&rsp.RDRAM[audio.loop&MEMMASK],32);
        } else {
            memcpy(out,&rsp.RDRAM[Address],32);
        }
    }

    int l1=out[14^S];
    int l2=out[15^S];
    int inp1[8];
    int inp2[8];
    out+=16;
    while(count>0)
    {
                                                    // the first interation through, these values are
                                                    // either 0 in the case of A_INIT, from a special
                                                    // area of memory in the case of A_LOOP or just
                                                    // the values we calculated the last time

        code=BufferSpace[(audio.in+inPtr)^S8];
        index=code&0xf;
        index<<=4;                                  // index into the adpcm code table
        book1=(short *)&audio.adpcm_table[index];
        book2=book1+8;
        code>>=4;                                   // upper nibble is scale
        vscale=(0x8000>>((12-code)-1));         // very strange. 0x8000 would be .5 in 16:16 format
                                                    // so this appears to be a fractional scale based
                                                    // on the 12 based inverse of the scale value.  note
                                                    // that this could be negative, in which case we do
                                                    // not use the calculated vscale value... see the
                                                    // if(code>12) check below

        inPtr++;                                    // coded adpcm data lies next
        j=0;
        while(j<8)                                  // loop of 8, for 8 coded nibbles from 4 bytes
                                                    // which yields 8 short pcm values
        {
            icode=BufferSpace[(audio.in+inPtr)^S8];
            inPtr++;

            inp1[j]=(s16)((icode&0xf0)<<8);         // this will in effect be signed
            if(code<12)
                inp1[j]=((int)((int)inp1[j]*(int)vscale)>>16);
            /*else
                int catchme=1;*/
            j++;

            inp1[j]=(s16)((icode&0xf)<<12);
            if(code<12)
                inp1[j]=((int)((int)inp1[j]*(int)vscale)>>16);
            /*else
                int catchme=1;*/
            j++;
        }
        j=0;
        while(j<8)
        {
            icode=BufferSpace[(audio.in+inPtr)^S8];
            inPtr++;

            inp2[j]=(short)((icode&0xf0)<<8);           // this will in effect be signed
            if(code<12)
                inp2[j]=((int)((int)inp2[j]*(int)vscale)>>16);
            /*else
                int catchme=1;*/
            j++;

            inp2[j]=(short)((icode&0xf)<<12);
            if(code<12)
                inp2[j]=((int)((int)inp2[j]*(int)vscale)>>16);
            /*else
                int catchme=1;*/
            j++;
        }

        a[0]= (int)book1[0]*(int)l1;
        a[0]+=(int)book2[0]*(int)l2;
        a[0]+=(int)inp1[0]*(int)2048;

        a[1] =(int)book1[1]*(int)l1;
        a[1]+=(int)book2[1]*(int)l2;
        a[1]+=(int)book2[0]*inp1[0];
        a[1]+=(int)inp1[1]*(int)2048;

        a[2] =(int)book1[2]*(int)l1;
        a[2]+=(int)book2[2]*(int)l2;
        a[2]+=(int)book2[1]*inp1[0];
        a[2]+=(int)book2[0]*inp1[1];
        a[2]+=(int)inp1[2]*(int)2048;

        a[3] =(int)book1[3]*(int)l1;
        a[3]+=(int)book2[3]*(int)l2;
        a[3]+=(int)book2[2]*inp1[0];
        a[3]+=(int)book2[1]*inp1[1];
        a[3]+=(int)book2[0]*inp1[2];
        a[3]+=(int)inp1[3]*(int)2048;

        a[4] =(int)book1[4]*(int)l1;
        a[4]+=(int)book2[4]*(int)l2;
        a[4]+=(int)book2[3]*inp1[0];
        a[4]+=(int)book2[2]*inp1[1];
        a[4]+=(int)book2[1]*inp1[2];
        a[4]+=(int)book2[0]*inp1[3];
        a[4]+=(int)inp1[4]*(int)2048;

        a[5] =(int)book1[5]*(int)l1;
        a[5]+=(int)book2[5]*(int)l2;
        a[5]+=(int)book2[4]*inp1[0];
        a[5]+=(int)book2[3]*inp1[1];
        a[5]+=(int)book2[2]*inp1[2];
        a[5]+=(int)book2[1]*inp1[3];
        a[5]+=(int)book2[0]*inp1[4];
        a[5]+=(int)inp1[5]*(int)2048;

        a[6] =(int)book1[6]*(int)l1;
        a[6]+=(int)book2[6]*(int)l2;
        a[6]+=(int)book2[5]*inp1[0];
        a[6]+=(int)book2[4]*inp1[1];
        a[6]+=(int)book2[3]*inp1[2];
        a[6]+=(int)book2[2]*inp1[3];
        a[6]+=(int)book2[1]*inp1[4];
        a[6]+=(int)book2[0]*inp1[5];
        a[6]+=(int)inp1[6]*(int)2048;

        a[7] =(int)book1[7]*(int)l1;
        a[7]+=(int)book2[7]*(int)l2;
        a[7]+=(int)book2[6]*inp1[0];
        a[7]+=(int)book2[5]*inp1[1];
        a[7]+=(int)book2[4]*inp1[2];
        a[7]+=(int)book2[3]*inp1[3];
        a[7]+=(int)book2[2]*inp1[4];
        a[7]+=(int)book2[1]*inp1[5];
        a[7]+=(int)book2[0]*inp1[6];
        a[7]+=(int)inp1[7]*(int)2048;

        for(j=0;j<8;j++)
        {
            *(out++) = a[j^S] = clamp_s16(a[j^S] >> 11);
        }
        l1=a[6];
        l2=a[7];

        a[0]= (int)book1[0]*(int)l1;
        a[0]+=(int)book2[0]*(int)l2;
        a[0]+=(int)inp2[0]*(int)2048;

        a[1] =(int)book1[1]*(int)l1;
        a[1]+=(int)book2[1]*(int)l2;
        a[1]+=(int)book2[0]*inp2[0];
        a[1]+=(int)inp2[1]*(int)2048;

        a[2] =(int)book1[2]*(int)l1;
        a[2]+=(int)book2[2]*(int)l2;
        a[2]+=(int)book2[1]*inp2[0];
        a[2]+=(int)book2[0]*inp2[1];
        a[2]+=(int)inp2[2]*(int)2048;

        a[3] =(int)book1[3]*(int)l1;
        a[3]+=(int)book2[3]*(int)l2;
        a[3]+=(int)book2[2]*inp2[0];
        a[3]+=(int)book2[1]*inp2[1];
        a[3]+=(int)book2[0]*inp2[2];
        a[3]+=(int)inp2[3]*(int)2048;

        a[4] =(int)book1[4]*(int)l1;
        a[4]+=(int)book2[4]*(int)l2;
        a[4]+=(int)book2[3]*inp2[0];
        a[4]+=(int)book2[2]*inp2[1];
        a[4]+=(int)book2[1]*inp2[2];
        a[4]+=(int)book2[0]*inp2[3];
        a[4]+=(int)inp2[4]*(int)2048;

        a[5] =(int)book1[5]*(int)l1;
        a[5]+=(int)book2[5]*(int)l2;
        a[5]+=(int)book2[4]*inp2[0];
        a[5]+=(int)book2[3]*inp2[1];
        a[5]+=(int)book2[2]*inp2[2];
        a[5]+=(int)book2[1]*inp2[3];
        a[5]+=(int)book2[0]*inp2[4];
        a[5]+=(int)inp2[5]*(int)2048;

        a[6] =(int)book1[6]*(int)l1;
        a[6]+=(int)book2[6]*(int)l2;
        a[6]+=(int)book2[5]*inp2[0];
        a[6]+=(int)book2[4]*inp2[1];
        a[6]+=(int)book2[3]*inp2[2];
        a[6]+=(int)book2[2]*inp2[3];
        a[6]+=(int)book2[1]*inp2[4];
        a[6]+=(int)book2[0]*inp2[5];
        a[6]+=(int)inp2[6]*(int)2048;

        a[7] =(int)book1[7]*(int)l1;
        a[7]+=(int)book2[7]*(int)l2;
        a[7]+=(int)book2[6]*inp2[0];
        a[7]+=(int)book2[5]*inp2[1];
        a[7]+=(int)book2[4]*inp2[2];
        a[7]+=(int)book2[3]*inp2[3];
        a[7]+=(int)book2[2]*inp2[4];
        a[7]+=(int)book2[1]*inp2[5];
        a[7]+=(int)book2[0]*inp2[6];
        a[7]+=(int)inp2[7]*(int)2048;

        for(j=0;j<8;j++)
        {
            *(out++) = a[j^S] = clamp_s16(a[j^S] >> 11);
        }
        l1=a[6];
        l2=a[7];

        count-=32;
    }
    out-=16;
    memcpy(&rsp.RDRAM[Address],out,32);
}

static void LOADBUFF (u32 inst1, u32 inst2) { // memcpy causes static... endianess issue :(
    u32 v0;
    //u32 cnt;
    if (audio.count == 0)
        return;
    v0 = parse_address(inst2) & ~3;
    memcpy (BufferSpace+(audio.in&0xFFFC), rsp.RDRAM+v0, align(audio.count,4));
}

static void SAVEBUFF (u32 inst1, u32 inst2) { // memcpy causes static... endianess issue :(
    u32 v0;
    //u32 cnt;
    if (audio.count == 0)
        return;
    v0 = parse_address(inst2) & ~3;
    memcpy (rsp.RDRAM+v0, BufferSpace+(audio.out&0xFFFC), align(audio.count,4));
}

static void SETBUFF (u32 inst1, u32 inst2) { // Should work ;-)

    if (parse_flags(inst1) & A_AUX)
    {
        audio.aux_dry_left  = parse_lo(inst1);
        audio.aux_wet_right = parse_hi(inst2);
        audio.aux_wet_left  = parse_lo(inst2);
    }
    else
    {
        audio.in    = parse_lo(inst1);
        audio.out   = parse_hi(inst2);
        audio.count = parse_lo(inst2);
    }
}

static void DMEMMOVE (u32 inst1, u32 inst2) { // Doesn't sound just right?... will fix when HLE is ready - 03-11-01
    u32 v0, v1;
    u32 cnt;
    u32 count = parse_lo(inst2);
    if (count == 0)
        return;
    v0 = parse_lo(inst1);
    v1 = parse_hi(inst2);
    //assert ((v1 & 0x3) == 0);
    //assert ((v0 & 0x3) == 0);
    count = align(count, 4);
    //v0 = (v0) & 0xfffc;
    //v1 = (v1) & 0xfffc;

    //memcpy (BufferSpace+v1, BufferSpace+v0, count-1);
    for (cnt = 0; cnt < count; cnt++) {
        *(u8 *)(BufferSpace+((cnt+v1)^S8)) = *(u8 *)(BufferSpace+((cnt+v0)^S8));
    }
}

static void LOADADPCM (u32 inst1, u32 inst2) { // Loads an ADPCM table - Works 100% Now 03-13-01
    u32 x;
    u32 v0 = parse_address(inst2);
    u16 iter_max = parse_lo(inst1) >> 4;
/*  if (v0 > (1024*1024*8))
        v0 = (inst2 & 0xffffff);*/
    //memcpy (dmem+0x4c0, rsp.RDRAM+v0, inst1&0xffff); // Could prolly get away with not putting this in dmem
    //assert ((inst1&0xffff) <= 0x80);
    u16 *table = (u16 *)(rsp.RDRAM+v0);
    for (x = 0; x < iter_max; x++) {
        audio.adpcm_table[(0x0+(x<<3))^S] = table[0];
        audio.adpcm_table[(0x1+(x<<3))^S] = table[1];

        audio.adpcm_table[(0x2+(x<<3))^S] = table[2];
        audio.adpcm_table[(0x3+(x<<3))^S] = table[3];

        audio.adpcm_table[(0x4+(x<<3))^S] = table[4];
        audio.adpcm_table[(0x5+(x<<3))^S] = table[5];

        audio.adpcm_table[(0x6+(x<<3))^S] = table[6];
        audio.adpcm_table[(0x7+(x<<3))^S] = table[7];
        table += 8;
    }
}


static void INTERLEAVE (u32 inst1, u32 inst2) { // Works... - 3-11-01
    int x;
    u32 inL, inR;
    u16 *outbuff = (u16 *)(audio.out+BufferSpace);
    u16 *inSrcR;
    u16 *inSrcL;
    u16 Left, Right, Left2, Right2;

    inL = parse_lo(inst2);
    inR = parse_hi(inst2);

    inSrcR = (u16 *)(BufferSpace+inR);
    inSrcL = (u16 *)(BufferSpace+inL);

    for (x = 0; x < (audio.count/4); x++) {
        Left=*(inSrcL++);
        Right=*(inSrcR++);
        Left2=*(inSrcL++);
        Right2=*(inSrcR++);

#ifdef M64P_BIG_ENDIAN
        *(outbuff++)=Right;
        *(outbuff++)=Left;
        *(outbuff++)=Right2;
        *(outbuff++)=Left2;
#else
        *(outbuff++)=Right2;
        *(outbuff++)=Left2;
        *(outbuff++)=Right;
        *(outbuff++)=Left;
#endif
    }
}


static void MIXER (u32 inst1, u32 inst2) { // Fixed a sign issue... 03-14-01
    u32 dmemin  = parse_hi(inst2);
    u32 dmemout = parse_lo(inst2);
    s32 gain    = (s16)parse_lo(inst1);
    s32 temp;
    int x;

    if (audio.count == 0)
        return;

    for (x=0; x < audio.count; x+=2) { // I think I can do this a lot easier
        temp = (*(s16 *)(BufferSpace+dmemin+x) * gain) >> 15;
        temp += *(s16 *)(BufferSpace+dmemout+x);

        *(s16*)(BufferSpace+dmemout+x) = clamp_s16(temp);
    }
}

// TOP Performance Hogs:
//Command: ADPCM    - Calls:  48 - Total Time: 331226 - Avg Time:  6900.54 - Percent: 31.53%
//Command: ENVMIXER - Calls:  48 - Total Time: 408563 - Avg Time:  8511.73 - Percent: 38.90%
//Command: LOADBUFF - Calls:  56 - Total Time:  21551 - Avg Time:   384.84 - Percent:  2.05%
//Command: RESAMPLE - Calls:  48 - Total Time: 225922 - Avg Time:  4706.71 - Percent: 21.51%

//Command: ADPCM    - Calls:  48 - Total Time: 391600 - Avg Time:  8158.33 - Percent: 32.52%
//Command: ENVMIXER - Calls:  48 - Total Time: 444091 - Avg Time:  9251.90 - Percent: 36.88%
//Command: LOADBUFF - Calls:  58 - Total Time:  29945 - Avg Time:   516.29 - Percent:  2.49%
//Command: RESAMPLE - Calls:  48 - Total Time: 276354 - Avg Time:  5757.38 - Percent: 22.95%


static const acmd_callback_t ABI1[0x10] = { // TOP Performace Hogs: MIXER, RESAMPLE, ENVMIXER
    SPNOOP , ADPCM , CLEARBUFF, ENVMIXER  , LOADBUFF, RESAMPLE  , SAVEBUFF, UNKNOWN,
    SETBUFF, SETVOL, DMEMMOVE , LOADADPCM , MIXER   , INTERLEAVE, UNKNOWN , SETLOOP
};

/*  BACKUPS
void MIXER (u32 inst1, u32 inst2) { // Fixed a sign issue... 03-14-01
    u16 dmemin  = (u16)(inst2 >> 0x10);
    u16 dmemout = (u16)(inst2 & 0xFFFF);
    u16 gain    = (u16)(inst1 & 0xFFFF);
    u8  flags   = (u8)((inst1 >> 16) & 0xff);
    u64 temp;

    if (audio.count == 0)
        return;

    for (int x=0; x < audio.count; x+=2) { // I think I can do this a lot easier
        temp = (s64)(*(s16 *)(BufferSpace+dmemout+x)) * (s64)((s16)(0x7FFF)*2);

        if (temp & 0x8000)
            temp = (temp^0x8000) + 0x10000;
        else
            temp = (temp^0x8000);

        temp = (temp & 0xFFFFFFFFFFFF);

        temp += ((*(s16 *)(BufferSpace+dmemin+x) * (s64)((s16)gain*2))) & 0xFFFFFFFFFFFF;
            
        temp = (s32)(temp >> 16);
        if ((s32)temp > 32767) 
            temp = 32767;
        if ((s32)temp < -32768) 
            temp = -32768;

        *(u16 *)(BufferSpace+dmemout+x) = (u16)(temp & 0xFFFF);
    }
}
*/

/* naudio ucode */
static struct naudio_t
{
    // loop
    u32 loop;           // 0x0010(t8)

    // envmixer gains
    s16 dry;            // 0x001c(t8)
    s16 wet;            // 0x001e(t8)

    // envmixer envelopes (0: left, 1: right)
    s16 env_vol[2];
    s16 env_target[2];
    s32 env_ramp[2];

    // adpcm
    u16 adpcm_table[0x80];

    // TODO: add mp3 related variables
} naudio;

extern const u16 ResampleLUT [0x200];

extern u8 BufferSpace[0x10000];

/*
static void SETVOL3 (u32 inst1, u32 inst2) { // Swapped Rate_Left and Vol
    u8 Flags = (u8)(inst1 >> 0x10);
    if (Flags & 0x4) { // 288
        if (Flags & 0x2) { // 290
            naudio.env_target[0]  = *(s16*)&inst1;
            naudio.env_ramp[0] = *(s32*)&inst2;
        } else {
            naudio.env_target[1]  = *(s16*)&inst1;
            naudio.env_ramp[1] = *(s32*)&inst2;
        }
    } else {
        naudio.env_vol[0]    = *(s16*)&inst1;
        naudio.dry     = (s16)(*(s32*)&inst2 >> 0x10);
        naudio.wet     = *(s16*)&inst2;
    }
}
*/
static void SETVOL3 (u32 inst1, u32 inst2) {
    u8 Flags = (u8)(inst1 >> 0x10);
    if (Flags & 0x4) { // 288
        if (Flags & 0x2) { // 290
            naudio.env_vol[0]  = (s16)inst1; // 0x50
            naudio.dry   = (s16)(inst2 >> 0x10); // 0x4E
            naudio.wet   = (s16)inst2; // 0x4C
        } else {
            naudio.env_target[1]  = (s16)inst1; // 0x46
            //naudio.env_ramp[1] = (u16)(inst2 >> 0x10) | (s32)(s16)(inst2 << 0x10);
            naudio.env_ramp[1] = (s32)inst2; // 0x48/0x4A
        }
    } else {
        naudio.env_target[0]  = (s16)inst1; // 0x40
        naudio.env_ramp[0] = (s32)inst2; // 0x42/0x44
    }
}

static void ENVMIXER3 (u32 inst1, u32 inst2) {
    int y;
    short state_buffer[40];
    u8 flags = (u8)((inst1 >> 16) & 0xff);
    u32 addy = (inst2 & 0xFFFFFF);

    short *inp=(short *)(BufferSpace+0x4F0);
    short *out=(short *)(BufferSpace+0x9D0);
    short *aux1=(short *)(BufferSpace+0xB40);
    short *aux2=(short *)(BufferSpace+0xCB0);
    short *aux3=(short *)(BufferSpace+0xE20);
    s32 MainR;
    s32 MainL;
    s32 AuxR;
    s32 AuxL;
    int i1,o1,a1,a2,a3;
    //unsigned short AuxIncRate=1;
    short zero[8];
    memset(zero,0,16);

    s32 LAdder, LAcc, LVol;
    s32 RAdder, RAcc, RVol;
    s16 RSig, LSig; // Most significant part of the Ramp Value
    s16 Wet, Dry;
    s16 LTrg, RTrg;

    naudio.env_vol[1] = (s16)inst1;

    if (flags & A_INIT) {
        LAdder = naudio.env_ramp[0] / 8;
        LAcc  = 0;
        LVol  = naudio.env_vol[0];
        LSig = (s16)(naudio.env_ramp[0] >> 16);

        RAdder = naudio.env_ramp[1] / 8;
        RAcc  = 0;
        RVol  = naudio.env_vol[1];
        RSig = (s16)(naudio.env_ramp[1] >> 16);

        Wet = (s16)naudio.wet; Dry = (s16)naudio.dry; // Save Wet/Dry values
        LTrg = naudio.env_target[0]; RTrg = naudio.env_target[1]; // Save Current Left/Right Targets
    } else {
        memcpy((u8 *)state_buffer, rsp.RDRAM+addy, 80);
        Wet    = *(s16 *)(state_buffer +  0); // 0-1
        Dry    = *(s16 *)(state_buffer +  2); // 2-3
        LTrg   = *(s16 *)(state_buffer +  4); // 4-5
        RTrg   = *(s16 *)(state_buffer +  6); // 6-7
        LAdder = *(s32 *)(state_buffer +  8); // 8-9 (state_buffer is a 16bit pointer)
        RAdder = *(s32 *)(state_buffer + 10); // 10-11
        LAcc   = *(s32 *)(state_buffer + 12); // 12-13
        RAcc   = *(s32 *)(state_buffer + 14); // 14-15
        LVol   = *(s32 *)(state_buffer + 16); // 16-17
        RVol   = *(s32 *)(state_buffer + 18); // 18-19
        LSig   = *(s16 *)(state_buffer + 20); // 20-21
        RSig   = *(s16 *)(state_buffer + 22); // 22-23
        //u32 test  = *(s32 *)(state_buffer + 24); // 22-23
        //if (test != 0x13371337)
    }


    //if(!(flags&A_AUX)) {
    //  AuxIncRate=0;
    //  aux2=aux3=zero;
    //}

    for (y = 0; y < (0x170/2); y++) {

        // Left
        LAcc += LAdder;
        LVol += (LAcc >> 16);
        LAcc &= 0xFFFF;

        // Right
        RAcc += RAdder;
        RVol += (RAcc >> 16);
        RAcc &= 0xFFFF;
// ****************************************************************
        // Clamp Left
        if (LSig >= 0) { // VLT
            if (LVol > LTrg) {
                LVol = LTrg;
            }
        } else { // VGE
            if (LVol < LTrg) {
                LVol = LTrg;
            }
        }

        // Clamp Right
        if (RSig >= 0) { // VLT
            if (RVol > RTrg) {
                RVol = RTrg;
            }
        } else { // VGE
            if (RVol < RTrg) {
                RVol = RTrg;
            }
        }
// ****************************************************************
        MainL = ((Dry * LVol) + 0x4000) >> 15;
        MainR = ((Dry * RVol) + 0x4000) >> 15;

        o1 = out [y^S];
        a1 = aux1[y^S];
        i1 = inp [y^S];

        o1+=((i1*MainL)+0x4000)>>15;
        a1+=((i1*MainR)+0x4000)>>15;

// ****************************************************************

        if(o1>32767) o1=32767;
        else if(o1<-32768) o1=-32768;

        if(a1>32767) a1=32767;
        else if(a1<-32768) a1=-32768;

// ****************************************************************

        out[y^S]=o1;
        aux1[y^S]=a1;

// ****************************************************************
        //if (!(flags&A_AUX)) {
            a2 = aux2[y^S];
            a3 = aux3[y^S];

            AuxL  = ((Wet * LVol) + 0x4000) >> 15;
            AuxR  = ((Wet * RVol) + 0x4000) >> 15;

            a2+=((i1*AuxL)+0x4000)>>15;
            a3+=((i1*AuxR)+0x4000)>>15;
            
            if(a2>32767) a2=32767;
            else if(a2<-32768) a2=-32768;

            if(a3>32767) a3=32767;
            else if(a3<-32768) a3=-32768;

            aux2[y^S]=a2;
            aux3[y^S]=a3;
        }
    //}

    *(s16 *)(state_buffer +  0) = Wet; // 0-1
    *(s16 *)(state_buffer +  2) = Dry; // 2-3
    *(s16 *)(state_buffer +  4) = LTrg; // 4-5
    *(s16 *)(state_buffer +  6) = RTrg; // 6-7
    *(s32 *)(state_buffer +  8) = LAdder; // 8-9 (state_buffer is a 16bit pointer)
    *(s32 *)(state_buffer + 10) = RAdder; // 10-11
    *(s32 *)(state_buffer + 12) = LAcc; // 12-13
    *(s32 *)(state_buffer + 14) = RAcc; // 14-15
    *(s32 *)(state_buffer + 16) = LVol; // 16-17
    *(s32 *)(state_buffer + 18) = RVol; // 18-19
    *(s16 *)(state_buffer + 20) = LSig; // 20-21
    *(s16 *)(state_buffer + 22) = RSig; // 22-23
    //*(u32 *)(state_buffer + 24) = 0x13371337; // 22-23
    memcpy(rsp.RDRAM+addy, (u8 *)state_buffer,80);
}

static void CLEARBUFF3 (u32 inst1, u32 inst2) {
    u16 addr = (u16)(inst1 & 0xffff);
    u16 count = (u16)(inst2 & 0xffff);
    memset(BufferSpace+addr+0x4f0, 0, count);
}

static void MIXER3 (u32 inst1, u32 inst2) { // Needs accuracy verification...
    u16 dmemin  = (u16)(inst2 >> 0x10)  + 0x4f0;
    u16 dmemout = (u16)(inst2 & 0xFFFF) + 0x4f0;
    //u8  flags   = (u8)((inst1 >> 16) & 0xff);
    s32 gain    = (s16)(inst1 & 0xFFFF);
    s32 temp;
    int x;

    for (x=0; x < 0x170; x+=2) { // I think I can do this a lot easier
        temp = (*(s16 *)(BufferSpace+dmemin+x) * gain) >> 15;
        temp += *(s16 *)(BufferSpace+dmemout+x);
            
        if ((s32)temp > 32767) 
            temp = 32767;
        if ((s32)temp < -32768) 
            temp = -32768;

        *(u16 *)(BufferSpace+dmemout+x) = (u16)(temp & 0xFFFF);
    }
}

static void LOADBUFF3 (u32 inst1, u32 inst2) {
    u32 v0;
    u32 cnt = (((inst1 >> 0xC)+3)&0xFFC);
    v0 = (inst2 & 0xfffffc);
    u32 src = (inst1&0xffc)+0x4f0;
    memcpy (BufferSpace+src, rsp.RDRAM+v0, cnt);
}

static void SAVEBUFF3 (u32 inst1, u32 inst2) {
    u32 v0;
    u32 cnt = (((inst1 >> 0xC)+3)&0xFFC);
    v0 = (inst2 & 0xfffffc);
    u32 src = (inst1&0xffc)+0x4f0;
    memcpy (rsp.RDRAM+v0, BufferSpace+src, cnt);
}

static void LOADADPCM3 (u32 inst1, u32 inst2) { // Loads an ADPCM table - Works 100% Now 03-13-01
    u32 v0;
    u32 x;
    v0 = (inst2 & 0xffffff);
    //memcpy (dmem+0x3f0, rsp.RDRAM+v0, inst1&0xffff); 
    //assert ((inst1&0xffff) <= 0x80);
    u16 *table = (u16 *)(rsp.RDRAM+v0);
    for (x = 0; x < ((inst1&0xffff)>>0x4); x++) {
        naudio.adpcm_table[(0x0+(x<<3))^S] = table[0];
        naudio.adpcm_table[(0x1+(x<<3))^S] = table[1];

        naudio.adpcm_table[(0x2+(x<<3))^S] = table[2];
        naudio.adpcm_table[(0x3+(x<<3))^S] = table[3];

        naudio.adpcm_table[(0x4+(x<<3))^S] = table[4];
        naudio.adpcm_table[(0x5+(x<<3))^S] = table[5];

        naudio.adpcm_table[(0x6+(x<<3))^S] = table[6];
        naudio.adpcm_table[(0x7+(x<<3))^S] = table[7];
        table += 8;
    }
}

static void DMEMMOVE3 (u32 inst1, u32 inst2) { // Needs accuracy verification...
    u32 v0, v1;
    u32 cnt;
    v0 = (inst1 & 0xFFFF) + 0x4f0;
    v1 = (inst2 >> 0x10) + 0x4f0;
    u32 count = ((inst2+3) & 0xfffc);

    //memcpy (dmem+v1, dmem+v0, count-1);
    for (cnt = 0; cnt < count; cnt++) {
        *(u8 *)(BufferSpace+((cnt+v1)^S8)) = *(u8 *)(BufferSpace+((cnt+v0)^S8));
    }
}

static void SETLOOP3 (u32 inst1, u32 inst2) {
    naudio.loop = (inst2 & 0xffffff);
}

static void ADPCM3 (u32 inst1, u32 inst2) { // Verified to be 100% Accurate...
    unsigned char Flags=(u8)(inst2>>0x1c)&0xff;
    //unsigned short Gain=(u16)(inst1&0xffff);
    unsigned int Address=(inst1 & 0xffffff);// + SEGMENTS[(inst2>>24)&0xf];
    unsigned short inPtr=(inst2>>12)&0xf;
    //short *out=(s16 *)(testbuff+(AudioOutBuffer>>2));
    short *out=(short *)(BufferSpace+(inst2&0xfff)+0x4f0);
    //unsigned char *in=(unsigned char *)(BufferSpace+((inst2>>12)&0xf)+0x4f0);
    short count=(short)((inst2 >> 16)&0xfff);
    unsigned char icode;
    unsigned char code;
    int vscale;
    unsigned short index;
    unsigned short j;
    int a[8];
    short *book1,*book2;

    memset(out,0,32);

    if(!(Flags&0x1))
    {
        if(Flags&0x2)
        {/*
            for(int i=0;i<16;i++)
            {
                out[i]=*(short *)&rsp.RDRAM[(naudio.loop+i*2)^2];
            }*/
            memcpy(out,&rsp.RDRAM[naudio.loop],32);
        }
        else
        {/*
            for(int i=0;i<16;i++)
            {
                out[i]=*(short *)&rsp.RDRAM[(Address+i*2)^2];
            }*/
            memcpy(out,&rsp.RDRAM[Address],32);
        }
    }

    int l1=out[14^S];
    int l2=out[15^S];
    int inp1[8];
    int inp2[8];
    out+=16;
    while(count>0)
    {
                                                    // the first interation through, these values are
                                                    // either 0 in the case of A_INIT, from a special
                                                    // area of memory in the case of A_LOOP or just
                                                    // the values we calculated the last time

        code=BufferSpace[(0x4f0+inPtr)^S8];
        index=code&0xf;
        index<<=4;                                  // index into the adpcm code table
        book1=(short *)&naudio.adpcm_table[index];
        book2=book1+8;
        code>>=4;                                   // upper nibble is scale
        vscale=(0x8000>>((12-code)-1));         // very strange. 0x8000 would be .5 in 16:16 format
                                                    // so this appears to be a fractional scale based
                                                    // on the 12 based inverse of the scale value.  note
                                                    // that this could be negative, in which case we do
                                                    // not use the calculated vscale value... see the 
                                                    // if(code>12) check below

        inPtr++;                                    // coded adpcm data lies next
        j=0;
        while(j<8)                                  // loop of 8, for 8 coded nibbles from 4 bytes
                                                    // which yields 8 short pcm values
        {
            icode=BufferSpace[(0x4f0+inPtr)^S8];
            inPtr++;

            inp1[j]=(s16)((icode&0xf0)<<8);         // this will in effect be signed
            if(code<12)
                inp1[j]=((int)((int)inp1[j]*(int)vscale)>>16);
            /*else
                int catchme=1;*/
            j++;

            inp1[j]=(s16)((icode&0xf)<<12);
            if(code<12)
                inp1[j]=((int)((int)inp1[j]*(int)vscale)>>16);
            /*else
                int catchme=1;*/
            j++;
        }
        j=0;
        while(j<8)
        {
            icode=BufferSpace[(0x4f0+inPtr)^S8];
            inPtr++;

            inp2[j]=(short)((icode&0xf0)<<8);           // this will in effect be signed
            if(code<12)
                inp2[j]=((int)((int)inp2[j]*(int)vscale)>>16);
            /*else
                int catchme=1;*/
            j++;

            inp2[j]=(short)((icode&0xf)<<12);
            if(code<12)
                inp2[j]=((int)((int)inp2[j]*(int)vscale)>>16);
            /*else
                int catchme=1;*/
            j++;
        }

        a[0]= (int)book1[0]*(int)l1;
        a[0]+=(int)book2[0]*(int)l2;
        a[0]+=(int)inp1[0]*(int)2048;

        a[1] =(int)book1[1]*(int)l1;
        a[1]+=(int)book2[1]*(int)l2;
        a[1]+=(int)book2[0]*inp1[0];
        a[1]+=(int)inp1[1]*(int)2048;

        a[2] =(int)book1[2]*(int)l1;
        a[2]+=(int)book2[2]*(int)l2;
        a[2]+=(int)book2[1]*inp1[0];
        a[2]+=(int)book2[0]*inp1[1];
        a[2]+=(int)inp1[2]*(int)2048;

        a[3] =(int)book1[3]*(int)l1;
        a[3]+=(int)book2[3]*(int)l2;
        a[3]+=(int)book2[2]*inp1[0];
        a[3]+=(int)book2[1]*inp1[1];
        a[3]+=(int)book2[0]*inp1[2];
        a[3]+=(int)inp1[3]*(int)2048;

        a[4] =(int)book1[4]*(int)l1;
        a[4]+=(int)book2[4]*(int)l2;
        a[4]+=(int)book2[3]*inp1[0];
        a[4]+=(int)book2[2]*inp1[1];
        a[4]+=(int)book2[1]*inp1[2];
        a[4]+=(int)book2[0]*inp1[3];
        a[4]+=(int)inp1[4]*(int)2048;

        a[5] =(int)book1[5]*(int)l1;
        a[5]+=(int)book2[5]*(int)l2;
        a[5]+=(int)book2[4]*inp1[0];
        a[5]+=(int)book2[3]*inp1[1];
        a[5]+=(int)book2[2]*inp1[2];
        a[5]+=(int)book2[1]*inp1[3];
        a[5]+=(int)book2[0]*inp1[4];
        a[5]+=(int)inp1[5]*(int)2048;

        a[6] =(int)book1[6]*(int)l1;
        a[6]+=(int)book2[6]*(int)l2;
        a[6]+=(int)book2[5]*inp1[0];
        a[6]+=(int)book2[4]*inp1[1];
        a[6]+=(int)book2[3]*inp1[2];
        a[6]+=(int)book2[2]*inp1[3];
        a[6]+=(int)book2[1]*inp1[4];
        a[6]+=(int)book2[0]*inp1[5];
        a[6]+=(int)inp1[6]*(int)2048;

        a[7] =(int)book1[7]*(int)l1;
        a[7]+=(int)book2[7]*(int)l2;
        a[7]+=(int)book2[6]*inp1[0];
        a[7]+=(int)book2[5]*inp1[1];
        a[7]+=(int)book2[4]*inp1[2];
        a[7]+=(int)book2[3]*inp1[3];
        a[7]+=(int)book2[2]*inp1[4];
        a[7]+=(int)book2[1]*inp1[5];
        a[7]+=(int)book2[0]*inp1[6];
        a[7]+=(int)inp1[7]*(int)2048;

        for(j=0;j<8;j++)
        {
            a[j^S]>>=11;
            if(a[j^S]>32767) a[j^S]=32767;
            else if(a[j^S]<-32768) a[j^S]=-32768;
            *(out++)=a[j^S];
            //*(out+j)=a[j^S];
        }
        //out += 0x10;
        l1=a[6];
        l2=a[7];

        a[0]= (int)book1[0]*(int)l1;
        a[0]+=(int)book2[0]*(int)l2;
        a[0]+=(int)inp2[0]*(int)2048;

        a[1] =(int)book1[1]*(int)l1;
        a[1]+=(int)book2[1]*(int)l2;
        a[1]+=(int)book2[0]*inp2[0];
        a[1]+=(int)inp2[1]*(int)2048;

        a[2] =(int)book1[2]*(int)l1;
        a[2]+=(int)book2[2]*(int)l2;
        a[2]+=(int)book2[1]*inp2[0];
        a[2]+=(int)book2[0]*inp2[1];
        a[2]+=(int)inp2[2]*(int)2048;

        a[3] =(int)book1[3]*(int)l1;
        a[3]+=(int)book2[3]*(int)l2;
        a[3]+=(int)book2[2]*inp2[0];
        a[3]+=(int)book2[1]*inp2[1];
        a[3]+=(int)book2[0]*inp2[2];
        a[3]+=(int)inp2[3]*(int)2048;

        a[4] =(int)book1[4]*(int)l1;
        a[4]+=(int)book2[4]*(int)l2;
        a[4]+=(int)book2[3]*inp2[0];
        a[4]+=(int)book2[2]*inp2[1];
        a[4]+=(int)book2[1]*inp2[2];
        a[4]+=(int)book2[0]*inp2[3];
        a[4]+=(int)inp2[4]*(int)2048;

        a[5] =(int)book1[5]*(int)l1;
        a[5]+=(int)book2[5]*(int)l2;
        a[5]+=(int)book2[4]*inp2[0];
        a[5]+=(int)book2[3]*inp2[1];
        a[5]+=(int)book2[2]*inp2[2];
        a[5]+=(int)book2[1]*inp2[3];
        a[5]+=(int)book2[0]*inp2[4];
        a[5]+=(int)inp2[5]*(int)2048;

        a[6] =(int)book1[6]*(int)l1;
        a[6]+=(int)book2[6]*(int)l2;
        a[6]+=(int)book2[5]*inp2[0];
        a[6]+=(int)book2[4]*inp2[1];
        a[6]+=(int)book2[3]*inp2[2];
        a[6]+=(int)book2[2]*inp2[3];
        a[6]+=(int)book2[1]*inp2[4];
        a[6]+=(int)book2[0]*inp2[5];
        a[6]+=(int)inp2[6]*(int)2048;

        a[7] =(int)book1[7]*(int)l1;
        a[7]+=(int)book2[7]*(int)l2;
        a[7]+=(int)book2[6]*inp2[0];
        a[7]+=(int)book2[5]*inp2[1];
        a[7]+=(int)book2[4]*inp2[2];
        a[7]+=(int)book2[3]*inp2[3];
        a[7]+=(int)book2[2]*inp2[4];
        a[7]+=(int)book2[1]*inp2[5];
        a[7]+=(int)book2[0]*inp2[6];
        a[7]+=(int)inp2[7]*(int)2048;

        for(j=0;j<8;j++)
        {
            a[j^S]>>=11;
            if(a[j^S]>32767) a[j^S]=32767;
            else if(a[j^S]<-32768) a[j^S]=-32768;
            *(out++)=a[j^S];
            //*(out+j+0x1f8)=a[j^S];
        }
        l1=a[6];
        l2=a[7];

        count-=32;
    }
    out-=16;
    memcpy(&rsp.RDRAM[Address],out,32);
}

static void RESAMPLE3 (u32 inst1, u32 inst2) {
    unsigned char Flags=(u8)((inst2>>0x1e));
    unsigned int Pitch=((inst2>>0xe)&0xffff)<<1;
    u32 addy = (inst1 & 0xffffff);
    unsigned int Accum=0;
    unsigned int location;
    s16 *lut;
    short *dst;
    s16 *src;
    dst=(short *)(BufferSpace);
    src=(s16 *)(BufferSpace);
    u32 srcPtr=((((inst2>>2)&0xfff)+0x4f0)/2);
    u32 dstPtr;//=(AudioOutBuffer/2);
    s32 temp;
    s32 accum;
    int i;

    //if (addy > (1024*1024*8))
    //  addy = (inst2 & 0xffffff);

    srcPtr -= 4;

    if (inst2 & 0x3) {
        dstPtr = 0x660/2;
    } else {
        dstPtr = 0x4f0/2;
    }

    if ((Flags & 0x1) == 0) {   
        for (i=0; i < 4; i++) //memcpy (src+srcPtr, rsp.RDRAM+addy, 0i8);
            src[(srcPtr+i)^S] = ((u16 *)rsp.RDRAM)[((addy/2)+i)^S];
        Accum = *(u16 *)(rsp.RDRAM+addy+10);
    } else {
        for (i=0; i < 4; i++)
            src[(srcPtr+i)^S] = 0;//*(u16 *)(rsp.RDRAM+((addy+i)^2));
    }

    for(i=0;i < 0x170/2;i++)    {
        location = (((Accum * 0x40) >> 0x10) * 8);
        //location = (Accum >> 0xa) << 0x3;
        lut = (s16 *)(((u8 *)ResampleLUT) + location);

        temp =  ((s32)*(s16*)(src+((srcPtr+0)^S))*((s32)((s16)lut[0])));
        accum = (s32)(temp >> 15);

        temp = ((s32)*(s16*)(src+((srcPtr+1)^S))*((s32)((s16)lut[1])));
        accum += (s32)(temp >> 15);

        temp = ((s32)*(s16*)(src+((srcPtr+2)^S))*((s32)((s16)lut[2])));
        accum += (s32)(temp >> 15);
        
        temp = ((s32)*(s16*)(src+((srcPtr+3)^S))*((s32)((s16)lut[3])));
        accum += (s32)(temp >> 15);
/*      temp =  ((s64)*(s16*)(src+((srcPtr+0)^S))*((s64)((s16)lut[0]<<1)));
        if (temp & 0x8000) temp = (temp^0x8000) + 0x10000;
        else temp = (temp^0x8000);
        temp = (s32)(temp >> 16);
        if ((s32)temp > 32767) temp = 32767;
        if ((s32)temp < -32768) temp = -32768;
        accum = (s32)(s16)temp;

        temp = ((s64)*(s16*)(src+((srcPtr+1)^S))*((s64)((s16)lut[1]<<1)));
        if (temp & 0x8000) temp = (temp^0x8000) + 0x10000;
        else temp = (temp^0x8000);
        temp = (s32)(temp >> 16);
        if ((s32)temp > 32767) temp = 32767;
        if ((s32)temp < -32768) temp = -32768;
        accum += (s32)(s16)temp;

        temp = ((s64)*(s16*)(src+((srcPtr+2)^S))*((s64)((s16)lut[2]<<1)));
        if (temp & 0x8000) temp = (temp^0x8000) + 0x10000;
        else temp = (temp^0x8000);
        temp = (s32)(temp >> 16);
        if ((s32)temp > 32767) temp = 32767;
        if ((s32)temp < -32768) temp = -32768;
        accum += (s32)(s16)temp;

        temp = ((s64)*(s16*)(src+((srcPtr+3)^S))*((s64)((s16)lut[3]<<1)));
        if (temp & 0x8000) temp = (temp^0x8000) + 0x10000;
        else temp = (temp^0x8000);
        temp = (s32)(temp >> 16);
        if ((s32)temp > 32767) temp = 32767;
        if ((s32)temp < -32768) temp = -32768;
        accum += (s32)(s16)temp;*/

        if (accum > 32767) accum = 32767;
        if (accum < -32768) accum = -32768;

        dst[dstPtr^S] = (accum);
        dstPtr++;
        Accum += Pitch;
        srcPtr += (Accum>>16);
        Accum&=0xffff;
    }
    for (i=0; i < 4; i++)
        ((u16 *)rsp.RDRAM)[((addy/2)+i)^S] = src[(srcPtr+i)^S];
    *(u16 *)(rsp.RDRAM+addy+10) = Accum;
}

static void INTERLEAVE3 (u32 inst1, u32 inst2) { // Needs accuracy verification...
    //u32 inL, inR;
    u16 *outbuff = (u16 *)(BufferSpace + 0x4f0);//(u16 *)(AudioOutBuffer+dmem);
    u16 *inSrcR;
    u16 *inSrcL;
    u16 Left, Right, Left2, Right2;
    int x;

    //inR = inst2 & 0xFFFF;
    //inL = (inst2 >> 16) & 0xFFFF;

    inSrcR = (u16 *)(BufferSpace+0xb40);
    inSrcL = (u16 *)(BufferSpace+0x9d0);

    for (x = 0; x < (0x170/4); x++) {
        Left=*(inSrcL++);
        Right=*(inSrcR++);
        Left2=*(inSrcL++);
        Right2=*(inSrcR++);

#ifdef M64P_BIG_ENDIAN
        *(outbuff++)=Right;
        *(outbuff++)=Left;
        *(outbuff++)=Right2;
        *(outbuff++)=Left2;
#else
        *(outbuff++)=Right2;
        *(outbuff++)=Left2;
        *(outbuff++)=Right;
        *(outbuff++)=Left;
#endif
/*
        Left=*(inSrcL++);
        Right=*(inSrcR++);
        *(outbuff++)=(u16)Left;
        Left >>= 16;
        *(outbuff++)=(u16)Right;
        Right >>= 16;
        *(outbuff++)=(u16)Left;
        *(outbuff++)=(u16)Right;*/
    }
}

static void WHATISTHIS (u32 inst1, u32 inst2) {
}

u32 setaddr;
static void MP3ADDY (u32 inst1, u32 inst2) {
        setaddr = (inst2 & 0xffffff);
}

static const u16 DeWindowLUT [0x420] = {
    0x0000, 0xFFF3, 0x005D, 0xFF38, 0x037A, 0xF736, 0x0B37, 0xC00E,
    0x7FFF, 0x3FF2, 0x0B37, 0x08CA, 0x037A, 0x00C8, 0x005D, 0x000D,
    0x0000, 0xFFF3, 0x005D, 0xFF38, 0x037A, 0xF736, 0x0B37, 0xC00E,
    0x7FFF, 0x3FF2, 0x0B37, 0x08CA, 0x037A, 0x00C8, 0x005D, 0x000D,
    0x0000, 0xFFF2, 0x005F, 0xFF1D, 0x0369, 0xF697, 0x0A2A, 0xBCE7,
    0x7FEB, 0x3CCB, 0x0C2B, 0x082B, 0x0385, 0x00AF, 0x005B, 0x000B,
    0x0000, 0xFFF2, 0x005F, 0xFF1D, 0x0369, 0xF697, 0x0A2A, 0xBCE7,
    0x7FEB, 0x3CCB, 0x0C2B, 0x082B, 0x0385, 0x00AF, 0x005B, 0x000B,
    0x0000, 0xFFF1, 0x0061, 0xFF02, 0x0354, 0xF5F9, 0x0905, 0xB9C4,
    0x7FB0, 0x39A4, 0x0D08, 0x078C, 0x038C, 0x0098, 0x0058, 0x000A,
    0x0000, 0xFFF1, 0x0061, 0xFF02, 0x0354, 0xF5F9, 0x0905, 0xB9C4,
    0x7FB0, 0x39A4, 0x0D08, 0x078C, 0x038C, 0x0098, 0x0058, 0x000A,
    0x0000, 0xFFEF, 0x0062, 0xFEE6, 0x033B, 0xF55C, 0x07C8, 0xB6A4,
    0x7F4D, 0x367E, 0x0DCE, 0x06EE, 0x038F, 0x0080, 0x0056, 0x0009,
    0x0000, 0xFFEF, 0x0062, 0xFEE6, 0x033B, 0xF55C, 0x07C8, 0xB6A4,
    0x7F4D, 0x367E, 0x0DCE, 0x06EE, 0x038F, 0x0080, 0x0056, 0x0009,
    0x0000, 0xFFEE, 0x0063, 0xFECA, 0x031C, 0xF4C3, 0x0671, 0xB38C,
    0x7EC2, 0x335D, 0x0E7C, 0x0652, 0x038E, 0x006B, 0x0053, 0x0008,
    0x0000, 0xFFEE, 0x0063, 0xFECA, 0x031C, 0xF4C3, 0x0671, 0xB38C,
    0x7EC2, 0x335D, 0x0E7C, 0x0652, 0x038E, 0x006B, 0x0053, 0x0008,
    0x0000, 0xFFEC, 0x0064, 0xFEAC, 0x02F7, 0xF42C, 0x0502, 0xB07C,
    0x7E12, 0x3041, 0x0F14, 0x05B7, 0x038A, 0x0056, 0x0050, 0x0007,
    0x0000, 0xFFEC, 0x0064, 0xFEAC, 0x02F7, 0xF42C, 0x0502, 0xB07C,
    0x7E12, 0x3041, 0x0F14, 0x05B7, 0x038A, 0x0056, 0x0050, 0x0007,
    0x0000, 0xFFEB, 0x0064, 0xFE8E, 0x02CE, 0xF399, 0x037A, 0xAD75,
    0x7D3A, 0x2D2C, 0x0F97, 0x0520, 0x0382, 0x0043, 0x004D, 0x0007,
    0x0000, 0xFFEB, 0x0064, 0xFE8E, 0x02CE, 0xF399, 0x037A, 0xAD75,
    0x7D3A, 0x2D2C, 0x0F97, 0x0520, 0x0382, 0x0043, 0x004D, 0x0007,
    0xFFFF, 0xFFE9, 0x0063, 0xFE6F, 0x029E, 0xF30B, 0x01D8, 0xAA7B,
    0x7C3D, 0x2A1F, 0x1004, 0x048B, 0x0377, 0x0030, 0x004A, 0x0006,
    0xFFFF, 0xFFE9, 0x0063, 0xFE6F, 0x029E, 0xF30B, 0x01D8, 0xAA7B,
    0x7C3D, 0x2A1F, 0x1004, 0x048B, 0x0377, 0x0030, 0x004A, 0x0006,
    0xFFFF, 0xFFE7, 0x0062, 0xFE4F, 0x0269, 0xF282, 0x001F, 0xA78D,
    0x7B1A, 0x271C, 0x105D, 0x03F9, 0x036A, 0x001F, 0x0046, 0x0006,
    0xFFFF, 0xFFE7, 0x0062, 0xFE4F, 0x0269, 0xF282, 0x001F, 0xA78D,
    0x7B1A, 0x271C, 0x105D, 0x03F9, 0x036A, 0x001F, 0x0046, 0x0006,
    0xFFFF, 0xFFE4, 0x0061, 0xFE2F, 0x022F, 0xF1FF, 0xFE4C, 0xA4AF,
    0x79D3, 0x2425, 0x10A2, 0x036C, 0x0359, 0x0010, 0x0043, 0x0005,
    0xFFFF, 0xFFE4, 0x0061, 0xFE2F, 0x022F, 0xF1FF, 0xFE4C, 0xA4AF,
    0x79D3, 0x2425, 0x10A2, 0x036C, 0x0359, 0x0010, 0x0043, 0x0005,
    0xFFFF, 0xFFE2, 0x005E, 0xFE10, 0x01EE, 0xF184, 0xFC61, 0xA1E1,
    0x7869, 0x2139, 0x10D3, 0x02E3, 0x0346, 0x0001, 0x0040, 0x0004,
    0xFFFF, 0xFFE2, 0x005E, 0xFE10, 0x01EE, 0xF184, 0xFC61, 0xA1E1,
    0x7869, 0x2139, 0x10D3, 0x02E3, 0x0346, 0x0001, 0x0040, 0x0004,
    0xFFFF, 0xFFE0, 0x005B, 0xFDF0, 0x01A8, 0xF111, 0xFA5F, 0x9F27,
    0x76DB, 0x1E5C, 0x10F2, 0x025E, 0x0331, 0xFFF3, 0x003D, 0x0004,
    0xFFFF, 0xFFE0, 0x005B, 0xFDF0, 0x01A8, 0xF111, 0xFA5F, 0x9F27,
    0x76DB, 0x1E5C, 0x10F2, 0x025E, 0x0331, 0xFFF3, 0x003D, 0x0004,
    0xFFFF, 0xFFDE, 0x0057, 0xFDD0, 0x015B, 0xF0A7, 0xF845, 0x9C80,
    0x752C, 0x1B8E, 0x1100, 0x01DE, 0x0319, 0xFFE7, 0x003A, 0x0003,
    0xFFFF, 0xFFDE, 0x0057, 0xFDD0, 0x015B, 0xF0A7, 0xF845, 0x9C80,
    0x752C, 0x1B8E, 0x1100, 0x01DE, 0x0319, 0xFFE7, 0x003A, 0x0003,
    0xFFFE, 0xFFDB, 0x0053, 0xFDB0, 0x0108, 0xF046, 0xF613, 0x99EE,
    0x735C, 0x18D1, 0x10FD, 0x0163, 0x0300, 0xFFDC, 0x0037, 0x0003,
    0xFFFE, 0xFFDB, 0x0053, 0xFDB0, 0x0108, 0xF046, 0xF613, 0x99EE,
    0x735C, 0x18D1, 0x10FD, 0x0163, 0x0300, 0xFFDC, 0x0037, 0x0003,
    0xFFFE, 0xFFD8, 0x004D, 0xFD90, 0x00B0, 0xEFF0, 0xF3CC, 0x9775,
    0x716C, 0x1624, 0x10EA, 0x00EE, 0x02E5, 0xFFD2, 0x0033, 0x0003,
    0xFFFE, 0xFFD8, 0x004D, 0xFD90, 0x00B0, 0xEFF0, 0xF3CC, 0x9775,
    0x716C, 0x1624, 0x10EA, 0x00EE, 0x02E5, 0xFFD2, 0x0033, 0x0003,
    0xFFFE, 0xFFD6, 0x0047, 0xFD72, 0x0051, 0xEFA6, 0xF16F, 0x9514,
    0x6F5E, 0x138A, 0x10C8, 0x007E, 0x02CA, 0xFFC9, 0x0030, 0x0003,
    0xFFFE, 0xFFD6, 0x0047, 0xFD72, 0x0051, 0xEFA6, 0xF16F, 0x9514,
    0x6F5E, 0x138A, 0x10C8, 0x007E, 0x02CA, 0xFFC9, 0x0030, 0x0003,
    0xFFFE, 0xFFD3, 0x0040, 0xFD54, 0xFFEC, 0xEF68, 0xEEFC, 0x92CD,
    0x6D33, 0x1104, 0x1098, 0x0014, 0x02AC, 0xFFC0, 0x002D, 0x0002,
    0xFFFE, 0xFFD3, 0x0040, 0xFD54, 0xFFEC, 0xEF68, 0xEEFC, 0x92CD,
    0x6D33, 0x1104, 0x1098, 0x0014, 0x02AC, 0xFFC0, 0x002D, 0x0002,
    0x0030, 0xFFC9, 0x02CA, 0x007E, 0x10C8, 0x138A, 0x6F5E, 0x9514,
    0xF16F, 0xEFA6, 0x0051, 0xFD72, 0x0047, 0xFFD6, 0xFFFE, 0x0003,
    0x0030, 0xFFC9, 0x02CA, 0x007E, 0x10C8, 0x138A, 0x6F5E, 0x9514,
    0xF16F, 0xEFA6, 0x0051, 0xFD72, 0x0047, 0xFFD6, 0xFFFE, 0x0003,
    0x0033, 0xFFD2, 0x02E5, 0x00EE, 0x10EA, 0x1624, 0x716C, 0x9775,
    0xF3CC, 0xEFF0, 0x00B0, 0xFD90, 0x004D, 0xFFD8, 0xFFFE, 0x0003,
    0x0033, 0xFFD2, 0x02E5, 0x00EE, 0x10EA, 0x1624, 0x716C, 0x9775,
    0xF3CC, 0xEFF0, 0x00B0, 0xFD90, 0x004D, 0xFFD8, 0xFFFE, 0x0003,
    0x0037, 0xFFDC, 0x0300, 0x0163, 0x10FD, 0x18D1, 0x735C, 0x99EE,
    0xF613, 0xF046, 0x0108, 0xFDB0, 0x0053, 0xFFDB, 0xFFFE, 0x0003,
    0x0037, 0xFFDC, 0x0300, 0x0163, 0x10FD, 0x18D1, 0x735C, 0x99EE,
    0xF613, 0xF046, 0x0108, 0xFDB0, 0x0053, 0xFFDB, 0xFFFE, 0x0003,
    0x003A, 0xFFE7, 0x0319, 0x01DE, 0x1100, 0x1B8E, 0x752C, 0x9C80,
    0xF845, 0xF0A7, 0x015B, 0xFDD0, 0x0057, 0xFFDE, 0xFFFF, 0x0003,
    0x003A, 0xFFE7, 0x0319, 0x01DE, 0x1100, 0x1B8E, 0x752C, 0x9C80,
    0xF845, 0xF0A7, 0x015B, 0xFDD0, 0x0057, 0xFFDE, 0xFFFF, 0x0004,
    0x003D, 0xFFF3, 0x0331, 0x025E, 0x10F2, 0x1E5C, 0x76DB, 0x9F27,
    0xFA5F, 0xF111, 0x01A8, 0xFDF0, 0x005B, 0xFFE0, 0xFFFF, 0x0004,
    0x003D, 0xFFF3, 0x0331, 0x025E, 0x10F2, 0x1E5C, 0x76DB, 0x9F27,
    0xFA5F, 0xF111, 0x01A8, 0xFDF0, 0x005B, 0xFFE0, 0xFFFF, 0x0004,
    0x0040, 0x0001, 0x0346, 0x02E3, 0x10D3, 0x2139, 0x7869, 0xA1E1,
    0xFC61, 0xF184, 0x01EE, 0xFE10, 0x005E, 0xFFE2, 0xFFFF, 0x0004,
    0x0040, 0x0001, 0x0346, 0x02E3, 0x10D3, 0x2139, 0x7869, 0xA1E1,
    0xFC61, 0xF184, 0x01EE, 0xFE10, 0x005E, 0xFFE2, 0xFFFF, 0x0005,
    0x0043, 0x0010, 0x0359, 0x036C, 0x10A2, 0x2425, 0x79D3, 0xA4AF,
    0xFE4C, 0xF1FF, 0x022F, 0xFE2F, 0x0061, 0xFFE4, 0xFFFF, 0x0005,
    0x0043, 0x0010, 0x0359, 0x036C, 0x10A2, 0x2425, 0x79D3, 0xA4AF,
    0xFE4C, 0xF1FF, 0x022F, 0xFE2F, 0x0061, 0xFFE4, 0xFFFF, 0x0006,
    0x0046, 0x001F, 0x036A, 0x03F9, 0x105D, 0x271C, 0x7B1A, 0xA78D,
    0x001F, 0xF282, 0x0269, 0xFE4F, 0x0062, 0xFFE7, 0xFFFF, 0x0006,
    0x0046, 0x001F, 0x036A, 0x03F9, 0x105D, 0x271C, 0x7B1A, 0xA78D,
    0x001F, 0xF282, 0x0269, 0xFE4F, 0x0062, 0xFFE7, 0xFFFF, 0x0006,
    0x004A, 0x0030, 0x0377, 0x048B, 0x1004, 0x2A1F, 0x7C3D, 0xAA7B,
    0x01D8, 0xF30B, 0x029E, 0xFE6F, 0x0063, 0xFFE9, 0xFFFF, 0x0006,
    0x004A, 0x0030, 0x0377, 0x048B, 0x1004, 0x2A1F, 0x7C3D, 0xAA7B,
    0x01D8, 0xF30B, 0x029E, 0xFE6F, 0x0063, 0xFFE9, 0xFFFF, 0x0007,
    0x004D, 0x0043, 0x0382, 0x0520, 0x0F97, 0x2D2C, 0x7D3A, 0xAD75,
    0x037A, 0xF399, 0x02CE, 0xFE8E, 0x0064, 0xFFEB, 0x0000, 0x0007,
    0x004D, 0x0043, 0x0382, 0x0520, 0x0F97, 0x2D2C, 0x7D3A, 0xAD75,
    0x037A, 0xF399, 0x02CE, 0xFE8E, 0x0064, 0xFFEB, 0x0000, 0x0007,
    0x0050, 0x0056, 0x038A, 0x05B7, 0x0F14, 0x3041, 0x7E12, 0xB07C,
    0x0502, 0xF42C, 0x02F7, 0xFEAC, 0x0064, 0xFFEC, 0x0000, 0x0007,
    0x0050, 0x0056, 0x038A, 0x05B7, 0x0F14, 0x3041, 0x7E12, 0xB07C,
    0x0502, 0xF42C, 0x02F7, 0xFEAC, 0x0064, 0xFFEC, 0x0000, 0x0008,
    0x0053, 0x006B, 0x038E, 0x0652, 0x0E7C, 0x335D, 0x7EC2, 0xB38C,
    0x0671, 0xF4C3, 0x031C, 0xFECA, 0x0063, 0xFFEE, 0x0000, 0x0008,
    0x0053, 0x006B, 0x038E, 0x0652, 0x0E7C, 0x335D, 0x7EC2, 0xB38C,
    0x0671, 0xF4C3, 0x031C, 0xFECA, 0x0063, 0xFFEE, 0x0000, 0x0009,
    0x0056, 0x0080, 0x038F, 0x06EE, 0x0DCE, 0x367E, 0x7F4D, 0xB6A4,
    0x07C8, 0xF55C, 0x033B, 0xFEE6, 0x0062, 0xFFEF, 0x0000, 0x0009,
    0x0056, 0x0080, 0x038F, 0x06EE, 0x0DCE, 0x367E, 0x7F4D, 0xB6A4,
    0x07C8, 0xF55C, 0x033B, 0xFEE6, 0x0062, 0xFFEF, 0x0000, 0x000A,
    0x0058, 0x0098, 0x038C, 0x078C, 0x0D08, 0x39A4, 0x7FB0, 0xB9C4,
    0x0905, 0xF5F9, 0x0354, 0xFF02, 0x0061, 0xFFF1, 0x0000, 0x000A,
    0x0058, 0x0098, 0x038C, 0x078C, 0x0D08, 0x39A4, 0x7FB0, 0xB9C4,
    0x0905, 0xF5F9, 0x0354, 0xFF02, 0x0061, 0xFFF1, 0x0000, 0x000B,
    0x005B, 0x00AF, 0x0385, 0x082B, 0x0C2B, 0x3CCB, 0x7FEB, 0xBCE7,
    0x0A2A, 0xF697, 0x0369, 0xFF1D, 0x005F, 0xFFF2, 0x0000, 0x000B,
    0x005B, 0x00AF, 0x0385, 0x082B, 0x0C2B, 0x3CCB, 0x7FEB, 0xBCE7,
    0x0A2A, 0xF697, 0x0369, 0xFF1D, 0x005F, 0xFFF2, 0x0000, 0x000D,
    0x005D, 0x00C8, 0x037A, 0x08CA, 0x0B37, 0x3FF2, 0x7FFF, 0xC00E,
    0x0B37, 0xF736, 0x037A, 0xFF38, 0x005D, 0xFFF3, 0x0000, 0x000D,
    0x005D, 0x00C8, 0x037A, 0x08CA, 0x0B37, 0x3FF2, 0x7FFF, 0xC00E,
    0x0B37, 0xF736, 0x037A, 0xFF38, 0x005D, 0xFFF3, 0x0000, 0x0000
};

//static u16 myVector[32][8];

static u8 mp3data[0x1000];

static s32 v[32];

static void MP3AB0 () {
    // Part 2 - 100% Accurate
    const u16 LUT2[8] = { 0xFEC4, 0xF4FA, 0xC5E4, 0xE1C4, 
                          0x1916, 0x4A50, 0xA268, 0x78AE };
    const u16 LUT3[4] = { 0xFB14, 0xD4DC, 0x31F2, 0x8E3A };
    int i;

    for (i = 0; i < 8; i++) {
        v[16+i] = v[0+i] + v[8+i];
        v[24+i] = ((v[0+i] - v[8+i]) * LUT2[i]) >> 0x10;
    }

    // Part 3: 4-wide butterflies

    for (i=0; i < 4; i++) {
        v[0+i]  = v[16+i] + v[20+i];
        v[4+i]  = ((v[16+i] - v[20+i]) * LUT3[i]) >> 0x10;

        v[8+i]  = v[24+i] + v[28+i];
        v[12+i] = ((v[24+i] - v[28+i]) * LUT3[i]) >> 0x10;
    }
                
    // Part 4: 2-wide butterflies - 100% Accurate

    for (i = 0; i < 16; i+=4) {
        v[16+i] = v[0+i] + v[2+i];
        v[18+i] = ((v[0+i] - v[2+i]) * 0xEC84) >> 0x10;

        v[17+i] = v[1+i] + v[3+i];
        v[19+i] = ((v[1+i] - v[3+i]) * 0x61F8) >> 0x10;
    }
}

static void InnerLoop ();

static u32 inPtr, outPtr;

static u32 t6;// = 0x08A0; // I think these are temporary storage buffers
static u32 t5;// = 0x0AC0;
static u32 t4;// = (inst1 & 0x1E);

static void MP3 (u32 inst1, u32 inst2) {
    // Initialization Code
    u32 readPtr; // s5
    u32 writePtr; // s6
    //u32 Count = 0x0480; // s4
    u32 tmp;
    //u32 inPtr, outPtr;
    int cnt, cnt2;

    t6 = 0x08A0; // I think these are temporary storage buffers
    t5 = 0x0AC0;
    t4 = (inst1 & 0x1E);

    writePtr = inst2 & 0xFFFFFF;
    readPtr  = writePtr;
    memcpy (mp3data+0xCE8, rsp.RDRAM+readPtr, 8); // Just do that for efficiency... may remove and use directly later anyway
    readPtr += 8; // This must be a header byte or whatnot

    for (cnt = 0; cnt < 0x480; cnt += 0x180) {
        memcpy (mp3data+0xCF0, rsp.RDRAM+readPtr, 0x180); // DMA: 0xCF0 <- RDRAM[s5] : 0x180
        inPtr  = 0xCF0; // s7
        outPtr = 0xE70; // s3
// --------------- Inner Loop Start --------------------
        for (cnt2 = 0; cnt2 < 0x180; cnt2 += 0x40) {
            t6 &= 0xFFE0;
            t5 &= 0xFFE0;
            t6 |= t4;
            t5 |= t4;
            InnerLoop ();
            t4 = (t4-2)&0x1E;
            tmp = t6;
            t6 = t5;
            t5 = tmp;
            //outPtr += 0x40;
            inPtr += 0x40;
        }
// --------------- Inner Loop End --------------------
        memcpy (rsp.RDRAM+writePtr, mp3data+0xe70, 0x180);
        writePtr += 0x180;
        readPtr  += 0x180;
    }
}



static void InnerLoop () {
                // Part 1: 100% Accurate

                int i;
                v[0] = *(s16 *)(mp3data+inPtr+(0x00^S16)); v[31] = *(s16 *)(mp3data+inPtr+(0x3E^S16)); v[0] += v[31];
                v[1] = *(s16 *)(mp3data+inPtr+(0x02^S16)); v[30] = *(s16 *)(mp3data+inPtr+(0x3C^S16)); v[1] += v[30];
                v[2] = *(s16 *)(mp3data+inPtr+(0x06^S16)); v[28] = *(s16 *)(mp3data+inPtr+(0x38^S16)); v[2] += v[28];
                v[3] = *(s16 *)(mp3data+inPtr+(0x04^S16)); v[29] = *(s16 *)(mp3data+inPtr+(0x3A^S16)); v[3] += v[29];

                v[4] = *(s16 *)(mp3data+inPtr+(0x0E^S16)); v[24] = *(s16 *)(mp3data+inPtr+(0x30^S16)); v[4] += v[24];
                v[5] = *(s16 *)(mp3data+inPtr+(0x0C^S16)); v[25] = *(s16 *)(mp3data+inPtr+(0x32^S16)); v[5] += v[25];
                v[6] = *(s16 *)(mp3data+inPtr+(0x08^S16)); v[27] = *(s16 *)(mp3data+inPtr+(0x36^S16)); v[6] += v[27];
                v[7] = *(s16 *)(mp3data+inPtr+(0x0A^S16)); v[26] = *(s16 *)(mp3data+inPtr+(0x34^S16)); v[7] += v[26];

                v[8] = *(s16 *)(mp3data+inPtr+(0x1E^S16)); v[16] = *(s16 *)(mp3data+inPtr+(0x20^S16)); v[8] += v[16];
                v[9] = *(s16 *)(mp3data+inPtr+(0x1C^S16)); v[17] = *(s16 *)(mp3data+inPtr+(0x22^S16)); v[9] += v[17];
                v[10]= *(s16 *)(mp3data+inPtr+(0x18^S16)); v[19] = *(s16 *)(mp3data+inPtr+(0x26^S16)); v[10]+= v[19];
                v[11]= *(s16 *)(mp3data+inPtr+(0x1A^S16)); v[18] = *(s16 *)(mp3data+inPtr+(0x24^S16)); v[11]+= v[18];

                v[12]= *(s16 *)(mp3data+inPtr+(0x10^S16)); v[23] = *(s16 *)(mp3data+inPtr+(0x2E^S16)); v[12]+= v[23];
                v[13]= *(s16 *)(mp3data+inPtr+(0x12^S16)); v[22] = *(s16 *)(mp3data+inPtr+(0x2C^S16)); v[13]+= v[22];
                v[14]= *(s16 *)(mp3data+inPtr+(0x16^S16)); v[20] = *(s16 *)(mp3data+inPtr+(0x28^S16)); v[14]+= v[20];
                v[15]= *(s16 *)(mp3data+inPtr+(0x14^S16)); v[21] = *(s16 *)(mp3data+inPtr+(0x2A^S16)); v[15]+= v[21];

                // Part 2-4

                MP3AB0 ();

                // Part 5 - 1-Wide Butterflies - 100% Accurate but need SSVs!!!

                u32 t0 = t6 + 0x100;
                u32 t1 = t6 + 0x200;
                u32 t2 = t5 + 0x100;
                u32 t3 = t5 + 0x200;
                /*RSP_GPR[0x8].W = t0;
                RSP_GPR[0x9].W = t1;
                RSP_GPR[0xA].W = t2;
                RSP_GPR[0xB].W = t3;

                RSP_Vect[0].DW[1] = 0xB504A57E00016A09;
                RSP_Vect[0].DW[0] = 0x0002D4130005A827;
*/

                // 0x13A8
                v[1] = 0;
                v[11] = ((v[16] - v[17]) * 0xB504) >> 0x10;

                v[16] = -v[16] -v[17];
                v[2] = v[18] + v[19];
                // ** Store v[11] -> (T6 + 0)**
                *(s16 *)(mp3data+((t6+(short)0x0))) = (short)v[11];
                
                
                v[11] = -v[11];
                // ** Store v[16] -> (T3 + 0)**
                *(s16 *)(mp3data+((t3+(short)0x0))) = (short)v[16];
                // ** Store v[11] -> (T5 + 0)**
                *(s16 *)(mp3data+((t5+(short)0x0))) = (short)v[11];
                // 0x13E8 - Verified....
                v[2] = -v[2];
                // ** Store v[2] -> (T2 + 0)**
                *(s16 *)(mp3data+((t2+(short)0x0))) = (short)v[2];
                v[3]  = (((v[18] - v[19]) * 0x16A09) >> 0x10) + v[2];
                // ** Store v[3] -> (T0 + 0)**
                *(s16 *)(mp3data+((t0+(short)0x0))) = (short)v[3];
                // 0x1400 - Verified
                v[4] = -v[20] -v[21];
                v[6] = v[22] + v[23];
                v[5] = ((v[20] - v[21]) * 0x16A09) >> 0x10;
                // ** Store v[4] -> (T3 + 0xFF80)
                *(s16 *)(mp3data+((t3+(short)0xFF80))) = (short)v[4];
                v[7] = ((v[22] - v[23]) * 0x2D413) >> 0x10;
                v[5] = v[5] - v[4];
                v[7] = v[7] - v[5];
                v[6] = v[6] + v[6];
                v[5] = v[5] - v[6];
                v[4] = -v[4] - v[6];
                // *** Store v[7] -> (T1 + 0xFF80)
                *(s16 *)(mp3data+((t1+(short)0xFF80))) = (short)v[7];
                // *** Store v[4] -> (T2 + 0xFF80)
                *(s16 *)(mp3data+((t2+(short)0xFF80))) = (short)v[4];
                // *** Store v[5] -> (T0 + 0xFF80)
                *(s16 *)(mp3data+((t0+(short)0xFF80))) = (short)v[5];
                v[8] = v[24] + v[25];


                v[9] = ((v[24] - v[25]) * 0x16A09) >> 0x10;
                v[2] = v[8] + v[9];
                v[11] = ((v[26] - v[27]) * 0x2D413) >> 0x10;
                v[13] = ((v[28] - v[29]) * 0x2D413) >> 0x10;

                v[10] = v[26] + v[27]; v[10] = v[10] + v[10];
                v[12] = v[28] + v[29]; v[12] = v[12] + v[12];
                v[14] = v[30] + v[31];
                v[3] = v[8] + v[10];
                v[14] = v[14] + v[14];
                v[13] = (v[13] - v[2]) + v[12];
                v[15] = (((v[30] - v[31]) * 0x5A827) >> 0x10) - (v[11] + v[2]);
                v[14] = -(v[14] + v[14]) + v[3];
                v[17] = v[13] - v[10];
                v[9] = v[9] + v[14];
                // ** Store v[9] -> (T6 + 0x40)
                *(s16 *)(mp3data+((t6+(short)0x40))) = (short)v[9];
                v[11] = v[11] - v[13];
                // ** Store v[17] -> (T0 + 0xFFC0)
                *(s16 *)(mp3data+((t0+(short)0xFFC0))) = (short)v[17];
                v[12] = v[8] - v[12];
                // ** Store v[11] -> (T0 + 0x40)
                *(s16 *)(mp3data+((t0+(short)0x40))) = (short)v[11];
                v[8] = -v[8];
                // ** Store v[15] -> (T1 + 0xFFC0)
                *(s16 *)(mp3data+((t1+(short)0xFFC0))) = (short)v[15];
                v[10] = -v[10] -v[12];
                // ** Store v[12] -> (T2 + 0x40)
                *(s16 *)(mp3data+((t2+(short)0x40))) = (short)v[12];
                // ** Store v[8] -> (T3 + 0xFFC0)
                *(s16 *)(mp3data+((t3+(short)0xFFC0))) = (short)v[8];
                // ** Store v[14] -> (T5 + 0x40)
                *(s16 *)(mp3data+((t5+(short)0x40))) = (short)v[14];
                // ** Store v[10] -> (T2 + 0xFFC0)
                *(s16 *)(mp3data+((t2+(short)0xFFC0))) = (short)v[10];
                // 0x14FC - Verified...

                // Part 6 - 100% Accurate

                v[0] = *(s16 *)(mp3data+inPtr+(0x00^S16)); v[31] = *(s16 *)(mp3data+inPtr+(0x3E^S16)); v[0] -= v[31];
                v[1] = *(s16 *)(mp3data+inPtr+(0x02^S16)); v[30] = *(s16 *)(mp3data+inPtr+(0x3C^S16)); v[1] -= v[30];
                v[2] = *(s16 *)(mp3data+inPtr+(0x06^S16)); v[28] = *(s16 *)(mp3data+inPtr+(0x38^S16)); v[2] -= v[28];
                v[3] = *(s16 *)(mp3data+inPtr+(0x04^S16)); v[29] = *(s16 *)(mp3data+inPtr+(0x3A^S16)); v[3] -= v[29];

                v[4] = *(s16 *)(mp3data+inPtr+(0x0E^S16)); v[24] = *(s16 *)(mp3data+inPtr+(0x30^S16)); v[4] -= v[24];
                v[5] = *(s16 *)(mp3data+inPtr+(0x0C^S16)); v[25] = *(s16 *)(mp3data+inPtr+(0x32^S16)); v[5] -= v[25];
                v[6] = *(s16 *)(mp3data+inPtr+(0x08^S16)); v[27] = *(s16 *)(mp3data+inPtr+(0x36^S16)); v[6] -= v[27];
                v[7] = *(s16 *)(mp3data+inPtr+(0x0A^S16)); v[26] = *(s16 *)(mp3data+inPtr+(0x34^S16)); v[7] -= v[26];

                v[8] = *(s16 *)(mp3data+inPtr+(0x1E^S16)); v[16] = *(s16 *)(mp3data+inPtr+(0x20^S16)); v[8] -= v[16];
                v[9] = *(s16 *)(mp3data+inPtr+(0x1C^S16)); v[17] = *(s16 *)(mp3data+inPtr+(0x22^S16)); v[9] -= v[17];
                v[10]= *(s16 *)(mp3data+inPtr+(0x18^S16)); v[19] = *(s16 *)(mp3data+inPtr+(0x26^S16)); v[10]-= v[19];
                v[11]= *(s16 *)(mp3data+inPtr+(0x1A^S16)); v[18] = *(s16 *)(mp3data+inPtr+(0x24^S16)); v[11]-= v[18];

                v[12]= *(s16 *)(mp3data+inPtr+(0x10^S16)); v[23] = *(s16 *)(mp3data+inPtr+(0x2E^S16)); v[12]-= v[23];
                v[13]= *(s16 *)(mp3data+inPtr+(0x12^S16)); v[22] = *(s16 *)(mp3data+inPtr+(0x2C^S16)); v[13]-= v[22];
                v[14]= *(s16 *)(mp3data+inPtr+(0x16^S16)); v[20] = *(s16 *)(mp3data+inPtr+(0x28^S16)); v[14]-= v[20];
                v[15]= *(s16 *)(mp3data+inPtr+(0x14^S16)); v[21] = *(s16 *)(mp3data+inPtr+(0x2A^S16)); v[15]-= v[21];

                //0, 1, 3, 2, 7, 6, 4, 5, 7, 6, 4, 5, 0, 1, 3, 2
                const u16 LUT6[16] = { 0xFFB2, 0xFD3A, 0xF10A, 0xF854,
                                       0xBDAE, 0xCDA0, 0xE76C, 0xDB94,
                                       0x1920, 0x4B20, 0xAC7C, 0x7C68,
                                       0xABEC, 0x9880, 0xDAE8, 0x839C };
                for (i = 0; i < 16; i++) {
                    v[0+i] = (v[0+i] * LUT6[i]) >> 0x10;
                }
                v[0] = v[0] + v[0]; v[1] = v[1] + v[1];
                v[2] = v[2] + v[2]; v[3] = v[3] + v[3]; v[4] = v[4] + v[4];
                v[5] = v[5] + v[5]; v[6] = v[6] + v[6]; v[7] = v[7] + v[7];
                v[12] = v[12] + v[12]; v[13] = v[13] + v[13]; v[15] = v[15] + v[15];
                
                MP3AB0 ();

                // Part 7: - 100% Accurate + SSV - Unoptimized

                v[0] = ( v[17] + v[16] ) >> 1;
                v[1] = ((v[17] * (int)((short)0xA57E * 2)) + (v[16] * 0xB504)) >> 0x10;
                v[2] = -v[18] -v[19];
                v[3] = ((v[18] - v[19]) * 0x16A09) >> 0x10;
                v[4] = v[20] + v[21] + v[0];
                v[5] = (((v[20] - v[21]) * 0x16A09) >> 0x10) + v[1];
                v[6] = (((v[22] + v[23]) << 1) + v[0]) - v[2];
                v[7] = (((v[22] - v[23]) * 0x2D413) >> 0x10) + v[0] + v[1] + v[3];
                // 0x16A8
                // Save v[0] -> (T3 + 0xFFE0)
                *(s16 *)(mp3data+((t3+(short)0xFFE0))) = (short)-v[0];
                v[8] = v[24] + v[25];
                v[9] = ((v[24] - v[25]) * 0x16A09) >> 0x10;
                v[10] = ((v[26] + v[27]) << 1) + v[8];
                v[11] = (((v[26] - v[27]) * 0x2D413) >> 0x10) + v[8] + v[9];
                v[12] = v[4] - ((v[28] + v[29]) << 1);
                // ** Store v12 -> (T2 + 0x20)
                *(s16 *)(mp3data+((t2+(short)0x20))) = (short)v[12];
                v[13] = (((v[28] - v[29]) * 0x2D413) >> 0x10) - v[12] - v[5];
                v[14] = v[30] + v[31];
                v[14] = v[14] + v[14];
                v[14] = v[14] + v[14];
                v[14] = v[6] - v[14];
                v[15] = (((v[30] - v[31]) * 0x5A827) >> 0x10) - v[7];
                // Store v14 -> (T5 + 0x20)
                *(s16 *)(mp3data+((t5+(short)0x20))) = (short)v[14];
                v[14] = v[14] + v[1];
                // Store v[14] -> (T6 + 0x20)
                *(s16 *)(mp3data+((t6+(short)0x20))) = (short)v[14];
                // Store v[15] -> (T1 + 0xFFE0)
                *(s16 *)(mp3data+((t1+(short)0xFFE0))) = (short)v[15];
                v[9] = v[9] + v[10];
                v[1] = v[1] + v[6];
                v[6] = v[10] - v[6];
                v[1] = v[9] - v[1];
                // Store v[6] -> (T5 + 0x60)
                *(s16 *)(mp3data+((t5+(short)0x60))) = (short)v[6];
                v[10] = v[10] + v[2];
                v[10] = v[4] - v[10];
                // Store v[10] -> (T2 + 0xFFA0)
                *(s16 *)(mp3data+((t2+(short)0xFFA0))) = (short)v[10];
                v[12] = v[2] - v[12];
                // Store v[12] -> (T2 + 0xFFE0)
                *(s16 *)(mp3data+((t2+(short)0xFFE0))) = (short)v[12];
                v[5] = v[4] + v[5];
                v[4] = v[8] - v[4];
                // Store v[4] -> (T2 + 0x60)
                *(s16 *)(mp3data+((t2+(short)0x60))) = (short)v[4];
                v[0] = v[0] - v[8];
                // Store v[0] -> (T3 + 0xFFA0)
                *(s16 *)(mp3data+((t3+(short)0xFFA0))) = (short)v[0];
                v[7] = v[7] - v[11];
                // Store v[7] -> (T1 + 0xFFA0)
                *(s16 *)(mp3data+((t1+(short)0xFFA0))) = (short)v[7];
                v[11] = v[11] - v[3];
                // Store v[1] -> (T6 + 0x60)
                *(s16 *)(mp3data+((t6+(short)0x60))) = (short)v[1];
                v[11] = v[11] - v[5];
                // Store v[11] -> (T0 + 0x60)
                *(s16 *)(mp3data+((t0+(short)0x60))) = (short)v[11];
                v[3] = v[3] - v[13];
                // Store v[3] -> (T0 + 0x20)
                *(s16 *)(mp3data+((t0+(short)0x20))) = (short)v[3];
                v[13] = v[13] + v[2];
                // Store v[13] -> (T0 + 0xFFE0)
                *(s16 *)(mp3data+((t0+(short)0xFFE0))) = (short)v[13];
                //v[2] = ;
                v[2] = (v[5] - v[2]) - v[9];
                // Store v[2] -> (T0 + 0xFFA0)
                *(s16 *)(mp3data+((t0+(short)0xFFA0))) = (short)v[2];
                // 0x7A8 - Verified...

                // Step 8 - Dewindowing
    
                //u64 *DW = (u64 *)&DeWindowLUT[0x10-(t4>>1)];
                u32 offset = 0x10-(t4>>1);

                u32 addptr = t6 & 0xFFE0;
                offset = 0x10-(t4>>1);

                s32 v2=0, v4=0, v6=0, v8=0;
                //s32 z2=0, z4=0, z6=0, z8=0;

                offset = 0x10-(t4>>1);// + x*0x40;
                int x;
                for (x = 0; x < 8; x++) {
                    v2 = v4 = v6 = v8 = 0;

                    //addptr = t1;
                
                    for (i = 7; i >= 0; i--) {
                        v2 += ((int)*(s16 *)(mp3data+(addptr)+0x00) * (short)DeWindowLUT[offset+0x00] + 0x4000) >> 0xF;
                        v4 += ((int)*(s16 *)(mp3data+(addptr)+0x10) * (short)DeWindowLUT[offset+0x08] + 0x4000) >> 0xF;
                        v6 += ((int)*(s16 *)(mp3data+(addptr)+0x20) * (short)DeWindowLUT[offset+0x20] + 0x4000) >> 0xF;
                        v8 += ((int)*(s16 *)(mp3data+(addptr)+0x30) * (short)DeWindowLUT[offset+0x28] + 0x4000) >> 0xF;
                        addptr+=2; offset++;
                    }
                    s32 v0  = v2 + v4;
                    s32 v18 = v6 + v8;
                    //Clamp(v0);
                    //Clamp(v18);
                    // clamp???
                    *(s16 *)(mp3data+(outPtr^S16)) = v0;
                    *(s16 *)(mp3data+((outPtr+2)^S16)) = v18;
                    outPtr+=4;
                    addptr += 0x30;
                    offset += 0x38;
                }

                offset = 0x10-(t4>>1) + 8*0x40;
                v2 = v4 = 0;
                for (i = 0; i < 4; i++) {
                    v2 += ((int)*(s16 *)(mp3data+(addptr)+0x00) * (short)DeWindowLUT[offset+0x00] + 0x4000) >> 0xF;
                    v2 += ((int)*(s16 *)(mp3data+(addptr)+0x10) * (short)DeWindowLUT[offset+0x08] + 0x4000) >> 0xF;
                    addptr+=2; offset++;
                    v4 += ((int)*(s16 *)(mp3data+(addptr)+0x00) * (short)DeWindowLUT[offset+0x00] + 0x4000) >> 0xF;
                    v4 += ((int)*(s16 *)(mp3data+(addptr)+0x10) * (short)DeWindowLUT[offset+0x08] + 0x4000) >> 0xF;
                    addptr+=2; offset++;
                }
                s32 mult6 = *(s32 *)(mp3data+0xCE8);
                s32 mult4 = *(s32 *)(mp3data+0xCEC);
                if (t4 & 0x2) {
                    v2 = (v2 * *(u32 *)(mp3data+0xCE8)) >> 0x10;
                    *(s16 *)(mp3data+(outPtr^S16)) = v2;
                } else {
                    v4 = (v4 * *(u32 *)(mp3data+0xCE8)) >> 0x10;
                    *(s16 *)(mp3data+(outPtr^S16)) = v4;
                    mult4 = *(u32 *)(mp3data+0xCE8);
                }
                addptr -= 0x50;

                for (x = 0; x < 8; x++) {
                    v2 = v4 = v6 = v8 = 0;

                    offset = (0x22F-(t4>>1) + x*0x40);
                
                    for (i = 0; i < 4; i++) {
                        v2 += ((int)*(s16 *)(mp3data+(addptr    )+0x20) * (short)DeWindowLUT[offset+0x00] + 0x4000) >> 0xF;
                        v2 -= ((int)*(s16 *)(mp3data+((addptr+2))+0x20) * (short)DeWindowLUT[offset+0x01] + 0x4000) >> 0xF;
                        v4 += ((int)*(s16 *)(mp3data+(addptr    )+0x30) * (short)DeWindowLUT[offset+0x08] + 0x4000) >> 0xF;
                        v4 -= ((int)*(s16 *)(mp3data+((addptr+2))+0x30) * (short)DeWindowLUT[offset+0x09] + 0x4000) >> 0xF;
                        v6 += ((int)*(s16 *)(mp3data+(addptr    )+0x00) * (short)DeWindowLUT[offset+0x20] + 0x4000) >> 0xF;
                        v6 -= ((int)*(s16 *)(mp3data+((addptr+2))+0x00) * (short)DeWindowLUT[offset+0x21] + 0x4000) >> 0xF;
                        v8 += ((int)*(s16 *)(mp3data+(addptr    )+0x10) * (short)DeWindowLUT[offset+0x28] + 0x4000) >> 0xF;
                        v8 -= ((int)*(s16 *)(mp3data+((addptr+2))+0x10) * (short)DeWindowLUT[offset+0x29] + 0x4000) >> 0xF;
                        addptr+=4; offset+=2;
                    }
                    s32 v0  = v2 + v4;
                    s32 v18 = v6 + v8;
                    //Clamp(v0);
                    //Clamp(v18);
                    // clamp???
                    *(s16 *)(mp3data+((outPtr+2)^S16)) = v0;
                    *(s16 *)(mp3data+((outPtr+4)^S16)) = v18;
                    outPtr+=4;
                    addptr -= 0x50;
                }

                int tmp = outPtr;
                s32 hi0 = mult6;
                s32 hi1 = mult4;
                s32 v;

                hi0 = (int)hi0 >> 0x10;
                hi1 = (int)hi1 >> 0x10;
                for (i = 0; i < 8; i++) {
                    // v0
                    v = (*(s16 *)(mp3data+((tmp-0x40)^S16)) * hi0);
                    if (v > 32767) v = 32767; else if (v < -32767) v = -32767;
                    *(s16 *)((u8 *)mp3data+((tmp-0x40)^S16)) = (s16)v;
                    // v17
                    v = (*(s16 *)(mp3data+((tmp-0x30)^S16)) * hi0);
                    if (v > 32767) v = 32767; else if (v < -32767) v = -32767;
                    *(s16 *)((u8 *)mp3data+((tmp-0x30)^S16)) = v;
                    // v2
                    v = (*(s16 *)(mp3data+((tmp-0x1E)^S16)) * hi1);
                    if (v > 32767) v = 32767; else if (v < -32767) v = -32767;
                    *(s16 *)((u8 *)mp3data+((tmp-0x1E)^S16)) = v;
                    // v4
                    v = (*(s16 *)(mp3data+((tmp-0xE)^S16)) * hi1);
                    if (v > 32767) v = 32767; else if (v < -32767) v = -32767;
                    *(s16 *)((u8 *)mp3data+((tmp-0xE)^S16)) = v;
                    tmp += 2;
                }
}

static void DISABLE (u32 inst1, u32 inst2) {
    //MessageBox (NULL, "Help", "ABI 3 Command 0", MB_OK);
    //ChangeABI (5);
}


static const acmd_callback_t ABI3[0x10] = {
    DISABLE , ADPCM3 , CLEARBUFF3,  ENVMIXER3  , LOADBUFF3, RESAMPLE3  , SAVEBUFF3, MP3,
    MP3ADDY, SETVOL3, DMEMMOVE3 , LOADADPCM3 , MIXER3   , INTERLEAVE3, WHATISTHIS   , SETLOOP3
};


/* others audio ucodes */

static struct audio2_t
{
    // segments
    u32 segments[0x10]; // 0x320

    // main buffers
    u16 in;             // 0x0000(t8)
    u16 out;            // 0x0002(t8)
    u16 count;          // 0x0004(t8)

    // loop
    u32 loop;           // 0x0010(t8)

    // adpcm
    u16 adpcm_table[0x80];

    // TODO: add envsetup state values here
} audio2;

int isMKABI = 0;
int isZeldaABI = 0;

void init_ucode2() { isMKABI = isZeldaABI = 0; }

static void LOADADPCM2 (u32 inst1, u32 inst2) { // Loads an ADPCM table - Works 100% Now 03-13-01
    u32 v0;
    u32 x;
    v0 = (inst2 & 0xffffff);// + SEGMENTS[(inst2>>24)&0xf];
    u16 *table = (u16 *)(rsp.RDRAM+v0); // Zelda2 Specific...

    for (x = 0; x < ((inst1&0xffff)>>0x4); x++) {
        audio2.adpcm_table[(0x0+(x<<3))^S] = table[0];
        audio2.adpcm_table[(0x1+(x<<3))^S] = table[1];

        audio2.adpcm_table[(0x2+(x<<3))^S] = table[2];
        audio2.adpcm_table[(0x3+(x<<3))^S] = table[3];

        audio2.adpcm_table[(0x4+(x<<3))^S] = table[4];
        audio2.adpcm_table[(0x5+(x<<3))^S] = table[5];

        audio2.adpcm_table[(0x6+(x<<3))^S] = table[6];
        audio2.adpcm_table[(0x7+(x<<3))^S] = table[7];
        table += 8;
    }
}

static void SETLOOP2 (u32 inst1, u32 inst2) {
    audio2.loop = inst2 & 0xffffff; // No segment?
}

static void SETBUFF2 (u32 inst1, u32 inst2) {
    audio2.in    = (u16)(inst1);            // 0x00
    audio2.out   = (u16)((inst2 >> 0x10)); // 0x02
    audio2.count = (u16)(inst2);            // 0x04
}

static void ADPCM2 (u32 inst1, u32 inst2) { // Verified to be 100% Accurate...
    unsigned char Flags=(u8)(inst1>>16)&0xff;
    //unsigned short Gain=(u16)(inst1&0xffff);
    unsigned int Address=(inst2 & 0xffffff);// + SEGMENTS[(inst2>>24)&0xf];
    unsigned short inPtr=0;
    //short *out=(s16 *)(testbuff+(audio2.out>>2));
    short *out=(short *)(BufferSpace+audio2.out);
    //unsigned char *in=(unsigned char *)(BufferSpace+audio2.in);
    short count=(short)audio2.count;
    unsigned char icode;
    unsigned char code;
    int vscale;
    unsigned short index;
    unsigned short j;
    int a[8];
    short *book1,*book2;

    u8 srange;
    u8 mask1;
    u8 mask2;
    u8 shifter;

    memset(out,0,32);

    if (Flags & 0x4) { // Tricky lil Zelda MM and ABI2!!! hahaha I know your secrets! :DDD
        srange = 0xE;
        mask1 = 0xC0;
        mask2 = 0x30;
        shifter = 10;
    } else {
        srange = 0xC;
        mask1 = 0xf0;
        mask2 = 0x0f;
        shifter = 12;
    }

    if(!(Flags&0x1))
    {
        if(Flags&0x2)
        {/*
            for(int i=0;i<16;i++)
            {
                out[i]=*(short *)&rsp.RDRAM[(audio2.loop+i*2)^2];
            }*/
            memcpy(out,&rsp.RDRAM[audio2.loop],32);
        }
        else
        {/*
            for(int i=0;i<16;i++)
            {
                out[i]=*(short *)&rsp.RDRAM[(Address+i*2)^2];
            }*/
            memcpy(out,&rsp.RDRAM[Address],32);
        }
    }

    int l1=out[14^S];
    int l2=out[15^S];
    int inp1[8];
    int inp2[8];
    out+=16;
    while(count>0) {
        code=BufferSpace[(audio2.in+inPtr)^S8];
        index=code&0xf;
        index<<=4;
        book1=(short *)&audio2.adpcm_table[index];
        book2=book1+8;
        code>>=4;
        vscale=(0x8000>>((srange-code)-1));
        
        inPtr++;
        j=0;

        while(j<8) {
            icode=BufferSpace[(audio2.in+inPtr)^S8];
            inPtr++;

            inp1[j]=(s16)((icode&mask1) << 8);          // this will in effect be signed
            if(code<srange) inp1[j]=((int)((int)inp1[j]*(int)vscale)>>16);
            //else int catchme=1;
            j++;

            inp1[j]=(s16)((icode&mask2)<<shifter);
            if(code<srange) inp1[j]=((int)((int)inp1[j]*(int)vscale)>>16);
            //else int catchme=1;
            j++;

            if (Flags & 4) {
                inp1[j]=(s16)((icode&0xC) << 12);           // this will in effect be signed
                if(code < 0xE) inp1[j]=((int)((int)inp1[j]*(int)vscale)>>16);
                //else int catchme=1;
                j++;

                inp1[j]=(s16)((icode&0x3) << 14);
                if(code < 0xE) inp1[j]=((int)((int)inp1[j]*(int)vscale)>>16);
                //else int catchme=1;
                j++;
            } // end flags
        } // end while



        j=0;
        while(j<8) {
            icode=BufferSpace[(audio2.in+inPtr)^S8];
            inPtr++;

            inp2[j]=(s16)((icode&mask1) << 8);
            if(code<srange) inp2[j]=((int)((int)inp2[j]*(int)vscale)>>16);
            //else int catchme=1;
            j++;

            inp2[j]=(s16)((icode&mask2)<<shifter);
            if(code<srange) inp2[j]=((int)((int)inp2[j]*(int)vscale)>>16);
            //else int catchme=1;
            j++;

            if (Flags & 4) {
                inp2[j]=(s16)((icode&0xC) << 12);
                if(code < 0xE) inp2[j]=((int)((int)inp2[j]*(int)vscale)>>16);
                //else int catchme=1;
                j++;

                inp2[j]=(s16)((icode&0x3) << 14);
                if(code < 0xE) inp2[j]=((int)((int)inp2[j]*(int)vscale)>>16);
                //else int catchme=1;
                j++;
            } // end flags
        }

        a[0]= (int)book1[0]*(int)l1;
        a[0]+=(int)book2[0]*(int)l2;
        a[0]+=(int)inp1[0]*(int)2048;

        a[1] =(int)book1[1]*(int)l1;
        a[1]+=(int)book2[1]*(int)l2;
        a[1]+=(int)book2[0]*inp1[0];
        a[1]+=(int)inp1[1]*(int)2048;

        a[2] =(int)book1[2]*(int)l1;
        a[2]+=(int)book2[2]*(int)l2;
        a[2]+=(int)book2[1]*inp1[0];
        a[2]+=(int)book2[0]*inp1[1];
        a[2]+=(int)inp1[2]*(int)2048;

        a[3] =(int)book1[3]*(int)l1;
        a[3]+=(int)book2[3]*(int)l2;
        a[3]+=(int)book2[2]*inp1[0];
        a[3]+=(int)book2[1]*inp1[1];
        a[3]+=(int)book2[0]*inp1[2];
        a[3]+=(int)inp1[3]*(int)2048;

        a[4] =(int)book1[4]*(int)l1;
        a[4]+=(int)book2[4]*(int)l2;
        a[4]+=(int)book2[3]*inp1[0];
        a[4]+=(int)book2[2]*inp1[1];
        a[4]+=(int)book2[1]*inp1[2];
        a[4]+=(int)book2[0]*inp1[3];
        a[4]+=(int)inp1[4]*(int)2048;

        a[5] =(int)book1[5]*(int)l1;
        a[5]+=(int)book2[5]*(int)l2;
        a[5]+=(int)book2[4]*inp1[0];
        a[5]+=(int)book2[3]*inp1[1];
        a[5]+=(int)book2[2]*inp1[2];
        a[5]+=(int)book2[1]*inp1[3];
        a[5]+=(int)book2[0]*inp1[4];
        a[5]+=(int)inp1[5]*(int)2048;

        a[6] =(int)book1[6]*(int)l1;
        a[6]+=(int)book2[6]*(int)l2;
        a[6]+=(int)book2[5]*inp1[0];
        a[6]+=(int)book2[4]*inp1[1];
        a[6]+=(int)book2[3]*inp1[2];
        a[6]+=(int)book2[2]*inp1[3];
        a[6]+=(int)book2[1]*inp1[4];
        a[6]+=(int)book2[0]*inp1[5];
        a[6]+=(int)inp1[6]*(int)2048;

        a[7] =(int)book1[7]*(int)l1;
        a[7]+=(int)book2[7]*(int)l2;
        a[7]+=(int)book2[6]*inp1[0];
        a[7]+=(int)book2[5]*inp1[1];
        a[7]+=(int)book2[4]*inp1[2];
        a[7]+=(int)book2[3]*inp1[3];
        a[7]+=(int)book2[2]*inp1[4];
        a[7]+=(int)book2[1]*inp1[5];
        a[7]+=(int)book2[0]*inp1[6];
        a[7]+=(int)inp1[7]*(int)2048;

        for(j=0;j<8;j++)
        {
            a[j^S]>>=11;
            if(a[j^S]>32767) a[j^S]=32767;
            else if(a[j^S]<-32768) a[j^S]=-32768;
            *(out++)=a[j^S];
        }
        l1=a[6];
        l2=a[7];

        a[0]= (int)book1[0]*(int)l1;
        a[0]+=(int)book2[0]*(int)l2;
        a[0]+=(int)inp2[0]*(int)2048;

        a[1] =(int)book1[1]*(int)l1;
        a[1]+=(int)book2[1]*(int)l2;
        a[1]+=(int)book2[0]*inp2[0];
        a[1]+=(int)inp2[1]*(int)2048;

        a[2] =(int)book1[2]*(int)l1;
        a[2]+=(int)book2[2]*(int)l2;
        a[2]+=(int)book2[1]*inp2[0];
        a[2]+=(int)book2[0]*inp2[1];
        a[2]+=(int)inp2[2]*(int)2048;

        a[3] =(int)book1[3]*(int)l1;
        a[3]+=(int)book2[3]*(int)l2;
        a[3]+=(int)book2[2]*inp2[0];
        a[3]+=(int)book2[1]*inp2[1];
        a[3]+=(int)book2[0]*inp2[2];
        a[3]+=(int)inp2[3]*(int)2048;

        a[4] =(int)book1[4]*(int)l1;
        a[4]+=(int)book2[4]*(int)l2;
        a[4]+=(int)book2[3]*inp2[0];
        a[4]+=(int)book2[2]*inp2[1];
        a[4]+=(int)book2[1]*inp2[2];
        a[4]+=(int)book2[0]*inp2[3];
        a[4]+=(int)inp2[4]*(int)2048;

        a[5] =(int)book1[5]*(int)l1;
        a[5]+=(int)book2[5]*(int)l2;
        a[5]+=(int)book2[4]*inp2[0];
        a[5]+=(int)book2[3]*inp2[1];
        a[5]+=(int)book2[2]*inp2[2];
        a[5]+=(int)book2[1]*inp2[3];
        a[5]+=(int)book2[0]*inp2[4];
        a[5]+=(int)inp2[5]*(int)2048;

        a[6] =(int)book1[6]*(int)l1;
        a[6]+=(int)book2[6]*(int)l2;
        a[6]+=(int)book2[5]*inp2[0];
        a[6]+=(int)book2[4]*inp2[1];
        a[6]+=(int)book2[3]*inp2[2];
        a[6]+=(int)book2[2]*inp2[3];
        a[6]+=(int)book2[1]*inp2[4];
        a[6]+=(int)book2[0]*inp2[5];
        a[6]+=(int)inp2[6]*(int)2048;

        a[7] =(int)book1[7]*(int)l1;
        a[7]+=(int)book2[7]*(int)l2;
        a[7]+=(int)book2[6]*inp2[0];
        a[7]+=(int)book2[5]*inp2[1];
        a[7]+=(int)book2[4]*inp2[2];
        a[7]+=(int)book2[3]*inp2[3];
        a[7]+=(int)book2[2]*inp2[4];
        a[7]+=(int)book2[1]*inp2[5];
        a[7]+=(int)book2[0]*inp2[6];
        a[7]+=(int)inp2[7]*(int)2048;

        for(j=0;j<8;j++)
        {
            a[j^S]>>=11;
            if(a[j^S]>32767) a[j^S]=32767;
            else if(a[j^S]<-32768) a[j^S]=-32768;
            *(out++)=a[j^S];
        }
        l1=a[6];
        l2=a[7];

        count-=32;
    }
    out-=16;
    memcpy(&rsp.RDRAM[Address],out,32);
}

static void CLEARBUFF2 (u32 inst1, u32 inst2) {
    u16 addr = (u16)(inst1 & 0xffff);
    u16 count = (u16)(inst2 & 0xffff);
    if (count > 0)
        memset(BufferSpace+addr, 0, count);
}

static void LOADBUFF2 (u32 inst1, u32 inst2) { // Needs accuracy verification...
    u32 v0;
    u32 cnt = (((inst1 >> 0xC)+3)&0xFFC);
    v0 = (inst2 & 0xfffffc);// + SEGMENTS[(inst2>>24)&0xf];
    memcpy (BufferSpace+(inst1&0xfffc), rsp.RDRAM+v0, (cnt+3)&0xFFFC);
}

static void SAVEBUFF2 (u32 inst1, u32 inst2) { // Needs accuracy verification...
    u32 v0;
    u32 cnt = (((inst1 >> 0xC)+3)&0xFFC);
    v0 = (inst2 & 0xfffffc);// + SEGMENTS[(inst2>>24)&0xf];
    memcpy (rsp.RDRAM+v0, BufferSpace+(inst1&0xfffc), (cnt+3)&0xFFFC);
}


static void MIXER2 (u32 inst1, u32 inst2) { // Needs accuracy verification...
    u16 dmemin  = (u16)(inst2 >> 0x10);
    u16 dmemout = (u16)(inst2 & 0xFFFF);
    u32 count   = ((inst1 >> 12) & 0xFF0);
    s32 gain    = (s16)(inst1 & 0xFFFF);
    s32 temp;
    u32 x;

    for (x=0; x < count; x+=2) { // I think I can do this a lot easier 

        temp = (*(s16 *)(BufferSpace+dmemin+x) * gain) >> 15;
        temp += *(s16 *)(BufferSpace+dmemout+x);
            
        if ((s32)temp > 32767) 
            temp = 32767;
        if ((s32)temp < -32768) 
            temp = -32768;

        *(u16 *)(BufferSpace+dmemout+x) = (u16)(temp & 0xFFFF);
    }
}


static void RESAMPLE2 (u32 inst1, u32 inst2) {
    unsigned char Flags=(u8)((inst1>>16)&0xff);
    unsigned int Pitch=((inst1&0xffff))<<1;
    u32 addy = (inst2 & 0xffffff);// + SEGMENTS[(inst2>>24)&0xf];
    unsigned int Accum=0;
    unsigned int location;
    s16 *lut;
    short *dst;
    s16 *src;
    dst=(short *)(BufferSpace);
    src=(s16 *)(BufferSpace);
    u32 srcPtr=(audio2.in/2);
    u32 dstPtr=(audio2.out/2);
    s32 temp;
    s32 accum;
    int i;

    if (addy > (1024*1024*8))
        addy = (inst2 & 0xffffff);

    srcPtr -= 4;

    if ((Flags & 0x1) == 0) {   
        for (i=0; i < 4; i++) //memcpy (src+srcPtr, rsp.RDRAM+addy, 0x8);
            src[(srcPtr+i)^S] = ((u16 *)rsp.RDRAM)[((addy/2)+i)^S];
        Accum = *(u16 *)(rsp.RDRAM+addy+10);
    } else {
        for (i=0; i < 4; i++)
            src[(srcPtr+i)^S] = 0;//*(u16 *)(rsp.RDRAM+((addy+i)^2));
    }

    for(i=0;i < ((audio2.count+0xf)&0xFFF0)/2;i++)    {
        location = (((Accum * 0x40) >> 0x10) * 8);
        //location = (Accum >> 0xa) << 0x3;
        lut = (s16 *)(((u8 *)ResampleLUT) + location);

        temp =  ((s32)*(s16*)(src+((srcPtr+0)^S))*((s32)((s16)lut[0])));
        accum = (s32)(temp >> 15);

        temp = ((s32)*(s16*)(src+((srcPtr+1)^S))*((s32)((s16)lut[1])));
        accum += (s32)(temp >> 15);

        temp = ((s32)*(s16*)(src+((srcPtr+2)^S))*((s32)((s16)lut[2])));
        accum += (s32)(temp >> 15);
        
        temp = ((s32)*(s16*)(src+((srcPtr+3)^S))*((s32)((s16)lut[3])));
        accum += (s32)(temp >> 15);

        if (accum > 32767) accum = 32767;
        if (accum < -32768) accum = -32768;

        dst[dstPtr^S] = (s16)(accum);
        dstPtr++;
        Accum += Pitch;
        srcPtr += (Accum>>16);
        Accum&=0xffff;
    }
    for (i=0; i < 4; i++)
        ((u16 *)rsp.RDRAM)[((addy/2)+i)^S] = src[(srcPtr+i)^S];
    *(u16 *)(rsp.RDRAM+addy+10) = (u16)Accum;
    //memcpy (RSWORK, src+srcPtr, 0x8);
}

static void DMEMMOVE2 (u32 inst1, u32 inst2) { // Needs accuracy verification...
    u32 v0, v1;
    u32 cnt;
    if ((inst2 & 0xffff)==0)
        return;
    v0 = (inst1 & 0xFFFF);
    v1 = (inst2 >> 0x10);
    //assert ((v1 & 0x3) == 0);
    //assert ((v0 & 0x3) == 0);
    u32 count = ((inst2+3) & 0xfffc);
    //v0 = (v0) & 0xfffc;
    //v1 = (v1) & 0xfffc;

    //memcpy (dmem+v1, dmem+v0, count-1);
    for (cnt = 0; cnt < count; cnt++) {
        *(u8 *)(BufferSpace+((cnt+v1)^S8)) = *(u8 *)(BufferSpace+((cnt+v0)^S8));
    }
}

static u32 t3, s5, s6;
static u16 env[8];

static void ENVSETUP1 (u32 inst1, u32 inst2) {
    u32 tmp;

    //fprintf (dfile, "ENVSETUP1: inst1 = %08X, inst2 = %08X\n", inst1, inst2);
    t3 = inst1 & 0xFFFF;
    tmp = (inst1 >> 0x8) & 0xFF00;
    env[4] = (u16)tmp;
    tmp += t3;
    env[5] = (u16)tmp;
    s5 = inst2 >> 0x10;
    s6 = inst2 & 0xFFFF;
    //fprintf (dfile, " t3 = %X / s5 = %X / s6 = %X / env[4] = %X / env[5] = %X\n", t3, s5, s6, env[4], env[5]);
}

static void ENVSETUP2 (u32 inst1, u32 inst2) {
    u32 tmp;

    //fprintf (dfile, "ENVSETUP2: inst1 = %08X, inst2 = %08X\n", inst1, inst2);
    tmp = (inst2 >> 0x10);
    env[0] = (u16)tmp;
    tmp += s5;
    env[1] = (u16)tmp;
    tmp = inst2 & 0xffff;
    env[2] = (u16)tmp;
    tmp += s6;
    env[3] = (u16)tmp;
    //fprintf (dfile, " env[0] = %X / env[1] = %X / env[2] = %X / env[3] = %X\n", env[0], env[1], env[2], env[3]);
}

static void ENVMIXER2 (u32 inst1, u32 inst2) {
    //fprintf (dfile, "ENVMIXER: inst1 = %08X, inst2 = %08X\n", inst1, inst2);

    s16 *bufft6, *bufft7, *buffs0, *buffs1;
    s16 *buffs3;
    s32 count;
    u32 adder;

    s16 vec9, vec10;

    s16 v2[8];

    buffs3 = (s16 *)(BufferSpace + ((inst1 >> 0x0c)&0x0ff0));
    bufft6 = (s16 *)(BufferSpace + ((inst2 >> 0x14)&0x0ff0));
    bufft7 = (s16 *)(BufferSpace + ((inst2 >> 0x0c)&0x0ff0));
    buffs0 = (s16 *)(BufferSpace + ((inst2 >> 0x04)&0x0ff0));
    buffs1 = (s16 *)(BufferSpace + ((inst2 << 0x04)&0x0ff0));


    v2[0] = 0 - (s16)((inst1 & 0x2) >> 1);
    v2[1] = 0 - (s16)((inst1 & 0x1));
    v2[2] = 0 - (s16)((inst1 & 0x8) >> 1);
    v2[3] = 0 - (s16)((inst1 & 0x4) >> 1);

    count = (inst1 >> 8) & 0xff;

    if (!isMKABI) {
        s5 *= 2; s6 *= 2; t3 *= 2;
        adder = 0x10;
    } else {
        inst1 = 0;
        adder = 0x8;
        t3 = 0;
    }


    while (count > 0) {
        int temp, x;
        for (x=0; x < 0x8; x++) {
            vec9  = (s16)(((s32)buffs3[x^S] * (u32)env[0]) >> 0x10) ^ v2[0];
            vec10 = (s16)(((s32)buffs3[x^S] * (u32)env[2]) >> 0x10) ^ v2[1];
            temp = bufft6[x^S] + vec9;
            if (temp > 32767)  temp = 32767; if (temp < -32768) temp = -32768;
            bufft6[x^S] = temp;
            temp = bufft7[x^S] + vec10;
            if (temp > 32767)  temp = 32767; if (temp < -32768) temp = -32768;
            bufft7[x^S] = temp;
            vec9  = (s16)(((s32)vec9  * (u32)env[4]) >> 0x10) ^ v2[2];
            vec10 = (s16)(((s32)vec10 * (u32)env[4]) >> 0x10) ^ v2[3];
            if (inst1 & 0x10) {
                temp = buffs0[x^S] + vec10;
                if (temp > 32767)  temp = 32767; if (temp < -32768) temp = -32768;
                buffs0[x^S] = temp;
                temp = buffs1[x^S] + vec9;
                if (temp > 32767)  temp = 32767; if (temp < -32768) temp = -32768;
                buffs1[x^S] = temp;
            } else {
                temp = buffs0[x^S] + vec9;
                if (temp > 32767)  temp = 32767; if (temp < -32768) temp = -32768;
                buffs0[x^S] = temp;
                temp = buffs1[x^S] + vec10;
                if (temp > 32767)  temp = 32767; if (temp < -32768) temp = -32768;
                buffs1[x^S] = temp;
            }
        }

        if (!isMKABI)
        for (x=0x8; x < 0x10; x++) {
            vec9  = (s16)(((s32)buffs3[x^S] * (u32)env[1]) >> 0x10) ^ v2[0];
            vec10 = (s16)(((s32)buffs3[x^S] * (u32)env[3]) >> 0x10) ^ v2[1];
            temp = bufft6[x^S] + vec9;
            if (temp > 32767)  temp = 32767; if (temp < -32768) temp = -32768;
            bufft6[x^S] = temp;
           temp = bufft7[x^S] + vec10;
            if (temp > 32767)  temp = 32767; if (temp < -32768) temp = -32768;
            bufft7[x^S] = temp;
            vec9  = (s16)(((s32)vec9  * (u32)env[5]) >> 0x10) ^ v2[2];
            vec10 = (s16)(((s32)vec10 * (u32)env[5]) >> 0x10) ^ v2[3];
            if (inst1 & 0x10) {
                temp = buffs0[x^S] + vec10;
                if (temp > 32767)  temp = 32767; if (temp < -32768) temp = -32768;
                buffs0[x^S] = temp;
                temp = buffs1[x^S] + vec9;
                if (temp > 32767)  temp = 32767; if (temp < -32768) temp = -32768;
                buffs1[x^S] = temp;
            } else {
                temp = buffs0[x^S] + vec9;
                if (temp > 32767)  temp = 32767; if (temp < -32768) temp = -32768;
                buffs0[x^S] = temp;
                temp = buffs1[x^S] + vec10;
                if (temp > 32767)  temp = 32767; if (temp < -32768) temp = -32768;
                buffs1[x^S] = temp;
            }
        }
        bufft6 += adder; bufft7 += adder;
        buffs0 += adder; buffs1 += adder;
        buffs3 += adder; count  -= adder;
        env[0] += (u16)s5; env[1] += (u16)s5;
        env[2] += (u16)s6; env[3] += (u16)s6;
        env[4] += (u16)t3; env[5] += (u16)t3;
    }
}

static void DUPLICATE2(u32 inst1, u32 inst2) {
    unsigned short Count = (inst1 >> 16) & 0xff;
    unsigned short In  = inst1&0xffff;
    unsigned short Out = (inst2>>16);

    unsigned short buff[64];
    
    memcpy(buff,BufferSpace+In,128);

    while(Count) {
        memcpy(BufferSpace+Out,buff,128);
        Out+=128;
        Count--;
    }
}
/*
static void INTERL2 (u32 inst1, u32 inst2) { // Make your own...
    short Count = inst1 & 0xffff;
    unsigned short  Out   = inst2 & 0xffff;
    unsigned short In     = (inst2 >> 16);

    short *src,*dst,tmp;
    src=(short *)&BufferSpace[In];
    dst=(short *)&BufferSpace[Out];
    while(Count)
    {
        *(dst++)=*(src++);
        src++;
        *(dst++)=*(src++);
        src++;
        *(dst++)=*(src++);
        src++;
        *(dst++)=*(src++);
        src++;
        *(dst++)=*(src++);
        src++;
        *(dst++)=*(src++);
        src++;
        *(dst++)=*(src++);
        src++;
        *(dst++)=*(src++);
        src++;
        Count-=8;
    }
}
*/

static void INTERL2 (u32 inst1, u32 inst2) {
    short Count = inst1 & 0xffff;
    unsigned short  Out   = inst2 & 0xffff;
    unsigned short In     = (inst2 >> 16);

    unsigned char *src,*dst/*,tmp*/;
    src=(unsigned char *)(BufferSpace);//[In];
    dst=(unsigned char *)(BufferSpace);//[Out];
    while(Count) {
        *(short *)(dst+(Out^S8)) = *(short *)(src+(In^S8));
        Out += 2;
        In  += 4;
        Count--;
    }
}

static void INTERLEAVE2 (u32 inst1, u32 inst2) { // Needs accuracy verification...
    u32 inL, inR;
    u16 *outbuff;
    u16 *inSrcR;
    u16 *inSrcL;
    u16 Left, Right, Left2, Right2;
    u32 count;
    u32 x;

    count   = ((inst1 >> 12) & 0xFF0);
    if (count == 0) {
        outbuff = (u16 *)(audio2.out+BufferSpace);
        count = audio2.count;
    } else {
        outbuff = (u16 *)((inst1&0xFFFF)+BufferSpace);
    }

    inR = inst2 & 0xFFFF;
    inL = (inst2 >> 16) & 0xFFFF;

    inSrcR = (u16 *)(BufferSpace+inR);
    inSrcL = (u16 *)(BufferSpace+inL);

    for (x = 0; x < (count/4); x++) {
        Left=*(inSrcL++);
        Right=*(inSrcR++);
        Left2=*(inSrcL++);
        Right2=*(inSrcR++);

#ifdef M64P_BIG_ENDIAN
        *(outbuff++)=Right;
        *(outbuff++)=Left;
        *(outbuff++)=Right2;
        *(outbuff++)=Left2;
#else
        *(outbuff++)=Right2;
        *(outbuff++)=Left2;
        *(outbuff++)=Right;
        *(outbuff++)=Left;
#endif
    }
}

static void ADDMIXER (u32 inst1, u32 inst2) {
    short Count   = (inst1 >> 12) & 0x00ff0;
    u16 InBuffer  = (inst2 >> 16);
    u16 OutBuffer = inst2 & 0xffff;
    int cntr;

    s16 *inp, *outp;
    s32 temp;
    inp  = (s16 *)(BufferSpace + InBuffer);
    outp = (s16 *)(BufferSpace + OutBuffer);
    for (cntr = 0; cntr < Count; cntr+=2) {
        temp = *outp + *inp;
        if (temp > 32767)  temp = 32767; if (temp < -32768) temp = -32768;
        *(outp++) = temp;
        inp++;
    }
}

static void HILOGAIN (u32 inst1, u32 inst2) {
    u16 cnt = inst1 & 0xffff;
    u16 out = (inst2 >> 16) & 0xffff;
    s16 hi  = (s16)((inst1 >> 4) & 0xf000);
    u16 lo  = (inst1 >> 20) & 0xf;
    s16 *src;

    src = (s16 *)(BufferSpace+out);
    s32 tmp, val;

    while(cnt) {
        val = (s32)*src;
        //tmp = ((val * (s32)hi) + ((u64)(val * lo) << 16) >> 16);
        tmp = ((val * (s32)hi) >> 16) + (u32)(val * lo);
        if ((s32)tmp > 32767) tmp = 32767;
        else if ((s32)tmp < -32768) tmp = -32768;
        *src = tmp;
        src++;
        cnt -= 2;
    }
}

static void FILTER2 (u32 inst1, u32 inst2) {
            static int cnt = 0;
            static s16 *lutt6;
            static s16 *lutt5;
            u8 *save = (rsp.RDRAM+(inst2&0xFFFFFF));
            u8 t4 = (u8)((inst1 >> 0x10) & 0xFF);
            int x;

            if (t4 > 1) { // Then set the cnt variable
                cnt = (inst1 & 0xFFFF);
                lutt6 = (s16 *)save;
//              memcpy (dmem+0xFE0, rsp.RDRAM+(inst2&0xFFFFFF), 0x10);
                return;
            }

            if (t4 == 0) {
//              memcpy (dmem+0xFB0, rsp.RDRAM+(inst2&0xFFFFFF), 0x20);
                lutt5 = (short *)(save+0x10);
            }

            lutt5 = (short *)(save+0x10);

//          lutt5 = (short *)(dmem + 0xFC0);
//          lutt6 = (short *)(dmem + 0xFE0);
            for (x = 0; x < 8; x++) {
                s32 a;
                a = (lutt5[x] + lutt6[x]) >> 1;
                lutt5[x] = lutt6[x] = (short)a;
            }
            short *inp1, *inp2; 
            s32 out1[8];
            s16 outbuff[0x3c0], *outp;
            u32 inPtr = (u32)(inst1&0xffff);
            inp1 = (short *)(save);
            outp = outbuff;
            inp2 = (short *)(BufferSpace+inPtr);
            for (x = 0; x < cnt; x+=0x10) {
                out1[1] =  inp1[0]*lutt6[6];
                out1[1] += inp1[3]*lutt6[7];
                out1[1] += inp1[2]*lutt6[4];
                out1[1] += inp1[5]*lutt6[5];
                out1[1] += inp1[4]*lutt6[2];
                out1[1] += inp1[7]*lutt6[3];
                out1[1] += inp1[6]*lutt6[0];
                out1[1] += inp2[1]*lutt6[1]; // 1

                out1[0] =  inp1[3]*lutt6[6];
                out1[0] += inp1[2]*lutt6[7];
                out1[0] += inp1[5]*lutt6[4];
                out1[0] += inp1[4]*lutt6[5];
                out1[0] += inp1[7]*lutt6[2];
                out1[0] += inp1[6]*lutt6[3];
                out1[0] += inp2[1]*lutt6[0];
                out1[0] += inp2[0]*lutt6[1];

                out1[3] =  inp1[2]*lutt6[6];
                out1[3] += inp1[5]*lutt6[7];
                out1[3] += inp1[4]*lutt6[4];
                out1[3] += inp1[7]*lutt6[5];
                out1[3] += inp1[6]*lutt6[2];
                out1[3] += inp2[1]*lutt6[3];
                out1[3] += inp2[0]*lutt6[0];
                out1[3] += inp2[3]*lutt6[1];

                out1[2] =  inp1[5]*lutt6[6];
                out1[2] += inp1[4]*lutt6[7];
                out1[2] += inp1[7]*lutt6[4];
                out1[2] += inp1[6]*lutt6[5];
                out1[2] += inp2[1]*lutt6[2];
                out1[2] += inp2[0]*lutt6[3];
                out1[2] += inp2[3]*lutt6[0];
                out1[2] += inp2[2]*lutt6[1];

                out1[5] =  inp1[4]*lutt6[6];
                out1[5] += inp1[7]*lutt6[7];
                out1[5] += inp1[6]*lutt6[4];
                out1[5] += inp2[1]*lutt6[5];
                out1[5] += inp2[0]*lutt6[2];
                out1[5] += inp2[3]*lutt6[3];
                out1[5] += inp2[2]*lutt6[0];
                out1[5] += inp2[5]*lutt6[1];

                out1[4] =  inp1[7]*lutt6[6];
                out1[4] += inp1[6]*lutt6[7];
                out1[4] += inp2[1]*lutt6[4];
                out1[4] += inp2[0]*lutt6[5];
                out1[4] += inp2[3]*lutt6[2];
                out1[4] += inp2[2]*lutt6[3];
                out1[4] += inp2[5]*lutt6[0];
                out1[4] += inp2[4]*lutt6[1];

                out1[7] =  inp1[6]*lutt6[6];
                out1[7] += inp2[1]*lutt6[7];
                out1[7] += inp2[0]*lutt6[4];
                out1[7] += inp2[3]*lutt6[5];
                out1[7] += inp2[2]*lutt6[2];
                out1[7] += inp2[5]*lutt6[3];
                out1[7] += inp2[4]*lutt6[0];
                out1[7] += inp2[7]*lutt6[1];

                out1[6] =  inp2[1]*lutt6[6];
                out1[6] += inp2[0]*lutt6[7];
                out1[6] += inp2[3]*lutt6[4];
                out1[6] += inp2[2]*lutt6[5];
                out1[6] += inp2[5]*lutt6[2];
                out1[6] += inp2[4]*lutt6[3];
                out1[6] += inp2[7]*lutt6[0];
                out1[6] += inp2[6]*lutt6[1];
                outp[1] = /*CLAMP*/((out1[1]+0x4000) >> 0xF);
                outp[0] = /*CLAMP*/((out1[0]+0x4000) >> 0xF);
                outp[3] = /*CLAMP*/((out1[3]+0x4000) >> 0xF);
                outp[2] = /*CLAMP*/((out1[2]+0x4000) >> 0xF);
                outp[5] = /*CLAMP*/((out1[5]+0x4000) >> 0xF);
                outp[4] = /*CLAMP*/((out1[4]+0x4000) >> 0xF);
                outp[7] = /*CLAMP*/((out1[7]+0x4000) >> 0xF);
                outp[6] = /*CLAMP*/((out1[6]+0x4000) >> 0xF);
                inp1 = inp2;
                inp2 += 8;
                outp += 8;
            }
//          memcpy (rsp.RDRAM+(inst2&0xFFFFFF), dmem+0xFB0, 0x20);
            memcpy (save, inp2-8, 0x10);
            memcpy (BufferSpace+(inst1&0xffff), outbuff, cnt);
}

static void SEGMENT2 (u32 inst1, u32 inst2) {
    if (isZeldaABI) {
        FILTER2 (inst1, inst2);
        return;
    }
    if ((inst1 & 0xffffff) == 0) {
        isMKABI = 1;
        //SEGMENTS[(inst2>>24)&0xf] = (inst2 & 0xffffff);
    } else {
        isMKABI = 0;
        isZeldaABI = 1;
        FILTER2 (inst1, inst2);
    }
}

/*
void (*ABI2[0x20])(void) = {
    SPNOOP, ADPCM2, CLEARBUFF2, SPNOOP, SPNOOP, RESAMPLE2, SPNOOP, SEGMENT2,
    SETBUFF2, SPNOOP, DMEMMOVE2, LOADADPCM2, MIXER2, INTERLEAVE2, HILOGAIN, SETLOOP2,
    SPNOOP, INTERL2, ENVSETUP1, ENVMIXER2, LOADBUFF2, SAVEBUFF2, ENVSETUP2, SPNOOP,
    SPNOOP, SPNOOP, SPNOOP, SPNOOP, SPNOOP, SPNOOP, SPNOOP, SPNOOP
};*/

static const acmd_callback_t ABI2[0x20] = {
    SPNOOP , ADPCM2, CLEARBUFF2, UNKNOWN, ADDMIXER, RESAMPLE2, UNKNOWN, SEGMENT2,
    SETBUFF2 , DUPLICATE2, DMEMMOVE2, LOADADPCM2, MIXER2, INTERLEAVE2, HILOGAIN, SETLOOP2,
    SPNOOP, INTERL2 , ENVSETUP1, ENVMIXER2, LOADBUFF2, SAVEBUFF2, ENVSETUP2, SPNOOP,
    HILOGAIN , SPNOOP, DUPLICATE2 , UNKNOWN    , SPNOOP  , SPNOOP    , SPNOOP  , SPNOOP
};
/*
void (*ABI2[0x20])(void) = {
    SPNOOP , ADPCM2, CLEARBUFF2, SPNOOP, SPNOOP, RESAMPLE2  , SPNOOP  , SEGMENT2,
    SETBUFF2 , DUPLICATE2, DMEMMOVE2, LOADADPCM2, MIXER2, INTERLEAVE2, SPNOOP, SETLOOP2,
    SPNOOP, INTERL2 , ENVSETUP1, ENVMIXER2, LOADBUFF2, SAVEBUFF2, ENVSETUP2, SPNOOP,
    SPNOOP , SPNOOP, SPNOOP , SPNOOP    , SPNOOP  , SPNOOP    , SPNOOP  , SPNOOP
};*/
/* NOTES:

  FILTER/SEGMENT - Still needs to be finished up... add FILTER?
  UNKNOWWN #27   - Is this worth doing?  Looks like a pain in the ass just for WaveRace64
*/



