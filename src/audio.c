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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hle.h"
#include "m64p_types.h"

#include "adpcm.h"
#include "mp3.h"

/* types defintions */
typedef void (*acmd_callback_t)(u32 inst1, u32 inst2);

/* local variables */
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
    u16 adpcm_codebook[0x80];
} audio;

static struct naudio_t
{
    // loop
    u32 loop;

    // envmixer gains
    s16 dry;
    s16 wet;

    // envmixer envelopes (0: left, 1: right)
    s16 env_vol[2];
    s16 env_target[2];
    s32 env_ramp[2];

    // adpcm
    u16 adpcm_codebook[0x80];
} naudio;

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
    u16 adpcm_codebook[0x80];

    //envmixer2 related variables
    u32 t3;
    u32 s5;
    u32 s6;
    u16 env[8];
} audio2;

// FIXME: remove these flags
int isMKABI = 0;
int isZeldaABI = 0;

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

static const u16 RESAMPLE_LUT [0x200] =
{
    0x0c39, 0x66ad, 0x0d46, 0xffdf, 0x0b39, 0x6696, 0x0e5f, 0xffd8,
    0x0a44, 0x6669, 0x0f83, 0xffd0, 0x095a, 0x6626, 0x10b4, 0xffc8,
    0x087d, 0x65cd, 0x11f0, 0xffbf, 0x07ab, 0x655e, 0x1338, 0xffb6,
    0x06e4, 0x64d9, 0x148c, 0xffac, 0x0628, 0x643f, 0x15eb, 0xffa1,
    0x0577, 0x638f, 0x1756, 0xff96, 0x04d1, 0x62cb, 0x18cb, 0xff8a,
    0x0435, 0x61f3, 0x1a4c, 0xff7e, 0x03a4, 0x6106, 0x1bd7, 0xff71,
    0x031c, 0x6007, 0x1d6c, 0xff64, 0x029f, 0x5ef5, 0x1f0b, 0xff56,
    0x022a, 0x5dd0, 0x20b3, 0xff48, 0x01be, 0x5c9a, 0x2264, 0xff3a,
    0x015b, 0x5b53, 0x241e, 0xff2c, 0x0101, 0x59fc, 0x25e0, 0xff1e,
    0x00ae, 0x5896, 0x27a9, 0xff10, 0x0063, 0x5720, 0x297a, 0xff02,
    0x001f, 0x559d, 0x2b50, 0xfef4, 0xffe2, 0x540d, 0x2d2c, 0xfee8,
    0xffac, 0x5270, 0x2f0d, 0xfedb, 0xff7c, 0x50c7, 0x30f3, 0xfed0,
    0xff53, 0x4f14, 0x32dc, 0xfec6, 0xff2e, 0x4d57, 0x34c8, 0xfebd,
    0xff0f, 0x4b91, 0x36b6, 0xfeb6, 0xfef5, 0x49c2, 0x38a5, 0xfeb0,
    0xfedf, 0x47ed, 0x3a95, 0xfeac, 0xfece, 0x4611, 0x3c85, 0xfeab,
    0xfec0, 0x4430, 0x3e74, 0xfeac, 0xfeb6, 0x424a, 0x4060, 0xfeaf,
    0xfeaf, 0x4060, 0x424a, 0xfeb6, 0xfeac, 0x3e74, 0x4430, 0xfec0,
    0xfeab, 0x3c85, 0x4611, 0xfece, 0xfeac, 0x3a95, 0x47ed, 0xfedf,
    0xfeb0, 0x38a5, 0x49c2, 0xfef5, 0xfeb6, 0x36b6, 0x4b91, 0xff0f,
    0xfebd, 0x34c8, 0x4d57, 0xff2e, 0xfec6, 0x32dc, 0x4f14, 0xff53,
    0xfed0, 0x30f3, 0x50c7, 0xff7c, 0xfedb, 0x2f0d, 0x5270, 0xffac,
    0xfee8, 0x2d2c, 0x540d, 0xffe2, 0xfef4, 0x2b50, 0x559d, 0x001f,
    0xff02, 0x297a, 0x5720, 0x0063, 0xff10, 0x27a9, 0x5896, 0x00ae,
    0xff1e, 0x25e0, 0x59fc, 0x0101, 0xff2c, 0x241e, 0x5b53, 0x015b,
    0xff3a, 0x2264, 0x5c9a, 0x01be, 0xff48, 0x20b3, 0x5dd0, 0x022a,
    0xff56, 0x1f0b, 0x5ef5, 0x029f, 0xff64, 0x1d6c, 0x6007, 0x031c,
    0xff71, 0x1bd7, 0x6106, 0x03a4, 0xff7e, 0x1a4c, 0x61f3, 0x0435,
    0xff8a, 0x18cb, 0x62cb, 0x04d1, 0xff96, 0x1756, 0x638f, 0x0577,
    0xffa1, 0x15eb, 0x643f, 0x0628, 0xffac, 0x148c, 0x64d9, 0x06e4,
    0xffb6, 0x1338, 0x655e, 0x07ab, 0xffbf, 0x11f0, 0x65cd, 0x087d,
    0xffc8, 0x10b4, 0x6626, 0x095a, 0xffd0, 0x0f83, 0x6669, 0x0a44,
    0xffd8, 0x0e5f, 0x6696, 0x0b39, 0xffdf, 0x0d46, 0x66ad, 0x0c39
};

/* local functions */
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

static s32 dmul(s16 x, s16 y)
{
    return ((s32)x * (s32)y) >> 15;
}

static s32 dmul_round(s16 x, s16 y)
{
    return ((s32)x * (s32)y + 0x4000) >> 15;
}

static void sadd(s16 *x, s32 y)
{
    *x = clamp_s16(*x + y);
}


static unsigned parse_acmd(u32 w1)
{
    return (w1 >> 24) & 0xff;
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

/* caller is responsible to ensure that size and alignment constrains are met */
static void dma_read_fast(u16 mem, u32 dram, u16 length)
{
    // mem & dram should be 8 byte aligned
    // length should encode a linear transfer
    assert((mem & ~0x1ff8) == 0);
    assert((dram & ~0xfffff8) == 0);
    assert((length & ~0xfff) == 0);

    memcpy(rsp.DMEM + mem, rsp.RDRAM + dram, align(length+1, 8));
}

/* caller is responsible to ensure that size and alignment constrains are met */
static void dma_write_fast(u32 dram, u16 mem, u16 length)
{
    // mem & dram should be 8 byte aligned
    // length should encode a linear transfer
    assert((mem & ~0x1ff8) == 0);
    assert((dram & ~0xfffff8) == 0);
    assert((length & ~0xfff) == 0);

    memcpy(rsp.RDRAM + dram, rsp.DMEM + mem, align(length+1, 8));
}


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

        acmd = parse_acmd(inst1);

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

static void mix_buffers(u16 in, u16 out, int count, s16 gain)
{
    int i;

    s16 *src = (s16*)(rsp.DMEM+in);
    s16 *dst = (s16*)(rsp.DMEM+out);

    for (i = 0; i < count; ++i)
    {
        sadd(dst, dmul(*src, gain));
        ++src; ++dst;
    }
}

static void resample_buffer(
        int init,
        u32 state_address,
        unsigned int pitch,
        u16 in,
        u16 out,
        int count)
{
    unsigned int pitch_accu = 0;
    unsigned int location;
    s16 *lut;
    short *dst;
    s16 *src;
    dst=(short *)(rsp.DMEM);
    src=(s16 *)(rsp.DMEM);
    u32 srcPtr = in;
    u32 dstPtr = out;
    s32 accum;
    int i;

    srcPtr -= 4;

    if (init)
    {
        for (i=0; i < 4; i++)
            src[(srcPtr+i)^S] = 0;
        pitch_accu = 0;
    }
    else
    {
        for (i=0; i < 4; i++)
            src[(srcPtr+i)^S] = ((u16 *)rsp.RDRAM)[((state_address/2)+i)^S];
        pitch_accu = (unsigned int)(*(u16 *)(rsp.RDRAM+state_address+10));
    }

    for(i=0; i < count; i++)
    {
       // location is the fractional position between two samples
        location = (pitch_accu >> 10) << 2;
        lut = (s16*)RESAMPLE_LUT + location;

        accum  = dmul(src[(srcPtr+0)^S], lut[0]);
        accum += dmul(src[(srcPtr+1)^S], lut[1]);
        accum += dmul(src[(srcPtr+2)^S], lut[2]);
        accum += dmul(src[(srcPtr+3)^S], lut[3]);

        dst[dstPtr^S] = clamp_s16(accum);
        dstPtr++;
        pitch_accu += pitch;
        srcPtr += (pitch_accu>>16);
        pitch_accu &= 0xffff;
    }
    for (i=0; i < 4; i++)
        ((u16 *)rsp.RDRAM)[((state_address/2)+i)^S] = src[(srcPtr+i)^S];
    *(u16 *)(rsp.RDRAM+state_address+10) = pitch_accu;
}

static void interleave_buffers(u16 right, u16 left, u16 out, int count)
{
    int i;
    u16 Left, Right, Left2, Right2;

    u16 *srcR = (u16*)(rsp.DMEM + right);
    u16 *srcL = (u16*)(rsp.DMEM + left);
    u16 *dst = (u16*)(rsp.DMEM + out);

    for (i = 0; i < count; ++i)
    {
        Left=*(srcL++);
        Right=*(srcR++);
        Left2=*(srcL++);
        Right2=*(srcR++);

#ifdef M64P_BIG_ENDIAN
        *(dst++)=Right;
        *(dst++)=Left;
        *(dst++)=Right2;
        *(dst++)=Left2;
#else
        *(dst++)=Right2;
        *(dst++)=Left2;
        *(dst++)=Right;
        *(dst++)=Left;
#endif
    }
}

static void dmem_move(u16 dst, u16 src, int count)
{
    int i;

    for(i = 0; i < count; ++i)
    {
        *(u8*)(rsp.DMEM+(dst^S8)) = *(u8*)(rsp.DMEM+(src^S8));
        ++src;
        ++dst;
    }
}









/* Audio commands */
static void SPNOOP(u32 w1, u32 w2)
{
}

static void UNKNOWN(u32 w1, u32 w2)
{
    DebugMessage(M64MSG_WARNING,
            "Unknown audio command %d: %08x %08x",
            parse_acmd(w1), w1, w2);
}

static void SEGMENT(u32 inst1, u32 inst2)
{
    // ignored in practice
}

static void POLEF(u32 inst1, u32 inst2)
{
    // TODO
}

static void CLEARBUFF (u32 inst1, u32 inst2) {
    u16 addr = parse_lo(inst1) & ~3;
    u16 count = align(parse_lo(inst2), 4);
    
    memset(rsp.DMEM+addr, 0, count);
}

static void ENVMIXER (u32 inst1, u32 inst2) {
    int x,y;
    short state_buffer[40];
    unsigned flags = parse_flags(inst1);
    u32 addy = parse_address(inst2);
    short *inp=(short *)(rsp.DMEM+audio.in);
    short *out=(short *)(rsp.DMEM+audio.out);
    short *aux1=(short *)(rsp.DMEM+audio.aux_dry_left);
    short *aux2=(short *)(rsp.DMEM+audio.aux_wet_right);
    short *aux3=(short *)(rsp.DMEM+audio.aux_wet_left);
    s32 MainR;
    s32 MainL;
    s32 AuxR;
    s32 AuxL;
    s32 i1;
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

    oMainL = dmul_round(Dry, LTrg >> 16);
    oAuxL  = dmul_round(Wet, LTrg >> 16);
    oMainR = dmul_round(Dry, RTrg >> 16);
    oAuxR  = dmul_round(Wet, RTrg >> 16);

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
                MainL = dmul_round(Dry, LAcc >> 16);
                AuxL  = dmul_round(Wet, LAcc >> 16);
            }
        } else {
            if (LAcc > LTrg) {
                LAcc = LTrg;
                LAdderStart = LTrg;
                MainL = oMainL;
                AuxL  = oAuxL;
            } else {
                MainL = dmul_round(Dry, LAcc >> 16);
                AuxL  = dmul_round(Wet, LAcc >> 16);
            }
        }

        if (RVol <= 0) { // Decrementing
            if (RAcc < RTrg) {
                RAcc = RTrg;
                RAdderStart = RTrg;
                MainR = oMainR;
                AuxR  = oAuxR;
            } else {
                MainR = dmul_round(Dry, RAcc >> 16);
                AuxR  = dmul_round(Wet, RAcc >> 16);
            }
        } else {
            if (RAcc > RTrg) {
                RAcc = RTrg;
                RAdderStart = RTrg;
                MainR = oMainR;
                AuxR  = oAuxR;
            } else {
                MainR = dmul_round(Dry, RAcc >> 16);
                AuxR  = dmul_round(Wet, RAcc >> 16);
            }
        }

        i1 = (s32)inp[ptr^S];
        sadd(&out[ptr^S],  dmul_round(i1, MainR));
        sadd(&aux1[ptr^S], dmul_round(i1, MainL));

        if (AuxIncRate)
        {
            sadd(&aux2[ptr^S], dmul_round(i1, AuxR));
            sadd(&aux3[ptr^S], dmul_round(i1, AuxL));
        }
        ptr++;
    }
    }

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

static void RESAMPLE (u32 w1, u32 w2)
{
    resample_buffer(
            parse_flags(w1) & A_INIT,
            parse_address(w2),
            (unsigned int)(parse_lo(w1)) << 1,
            audio.in >> 1,
            audio.out >> 1,
            align(audio.count, 16) >> 1);
}

static void SETVOL (u32 inst1, u32 inst2) {
    unsigned flags = parse_flags(inst1);
    s16 vol = (s16)parse_lo(inst1);
    s16 volrate = (s16)parse_lo(inst2);

    if (flags & A_AUX) {
        audio.dry = vol;
        audio.wet = volrate;
        return;
    }

    if (flags & A_VOL) {
        if (flags & A_LEFT) {
            audio.env_vol[0] = vol;
        } else {
            audio.env_vol[1] = vol;
        }
        return;
    }

    if (flags & A_LEFT) {
        audio.env_target[0]  = (s16)inst1;
        audio.env_ramp[0] = (s32)inst2;
    } else { // A_RIGHT
        audio.env_target[1]  = (s16)inst1;
        audio.env_ramp[1] = (s32)inst2;
    }
}


static void SETLOOP (u32 inst1, u32 inst2)
{
    audio.loop = parse_address(inst2);
}

static void ADPCM (u32 w1, u32 w2)
{
    unsigned flags = parse_flags(w1);
   
    adpcm_decode(
            flags & A_INIT,
            flags & A_LOOP,
            0, // not supported in this version
            (s16*)audio.adpcm_codebook,
            audio.loop,
            parse_address(w2),
            audio.in,
            audio.out,
            align(audio.count, 32) >> 5);
}

static void LOADBUFF (u32 w1, u32 w2)
{
    if (audio.count == 0) { return; }
    dma_read_fast(audio.in & 0xff8, parse_address(w2) & ~7, audio.count - 1);
}

static void SAVEBUFF (u32 w1, u32 w2)
{
    if (audio.count == 0) { return; }
    dma_write_fast(parse_address(w2) & ~7, audio.out & 0xff8, audio.count - 1);
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

static void DMEMMOVE(u32 w1, u32 w2)
{
    int count = (int)parse_lo(w2);

    if (count == 0) { return; }

    dmem_move(
        parse_hi(w2),
        parse_lo(w1),
        align(count, 4));
}

static void LOADADPCM (u32 w1, u32 w2)
{
    adpcm_load_codebook(
            audio.adpcm_codebook,
            parse_address(w2 & 0xffffff),
            parse_lo(w1));
}


static void INTERLEAVE (u32 w1, u32 w2)
{
    interleave_buffers(
            parse_hi(w2),
            parse_lo(w2),
            audio.out,
            audio.count >> 2);
}


static void MIXER (u32 w1, u32 w2)
{
    mix_buffers(
            parse_hi(w2),
            parse_lo(w2),
            audio.count >> 1,
            (s16)parse_lo(w1));
}

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

    short *inp=(short *)(rsp.DMEM+0x4F0);
    short *out=(short *)(rsp.DMEM+0x9D0);
    short *aux1=(short *)(rsp.DMEM+0xB40);
    short *aux2=(short *)(rsp.DMEM+0xCB0);
    short *aux3=(short *)(rsp.DMEM+0xE20);
    s32 MainR;
    s32 MainL;
    s32 AuxR;
    s32 AuxL;
    s32 i1;

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
    }

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
        MainL = dmul_round(Dry, LVol);
        MainR = dmul_round(Dry, RVol);
        AuxL = dmul_round(Wet, LVol);
        AuxR = dmul_round(Wet, RVol);
        
        i1 = inp[y^S];
        sadd(&out[y^S],  dmul_round(i1, MainL));
        sadd(&aux1[y^S], dmul_round(i1, MainR));
        sadd(&aux2[y^S], dmul_round(i1, AuxL));
        sadd(&aux3[y^S], dmul_round(i1, AuxR));
    }

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
    memcpy(rsp.RDRAM+addy, (u8 *)state_buffer,80);
}

static void CLEARBUFF3 (u32 inst1, u32 inst2) {
    u16 addr = (u16)(inst1 & 0xffff);
    u16 count = (u16)(inst2 & 0xffff);
    memset(rsp.DMEM+addr+0x4f0, 0, count);
}

static void MIXER3 (u32 w1, u32 w2)
{
    mix_buffers(
            parse_hi(w2) + 0x4f0,
            parse_lo(w2) + 0x4f0,
            0x170 >> 1,
            (s16)parse_lo(w1));
}

static void LOADBUFF3 (u32 w1, u32 w2)
 {
    u16 length = (w1 >> 12) & 0xfff;
    if (length == 0) { return; }

    dma_read_fast((w1 + 0x4f0) & 0xff8, (w2 & 0xfffff8), length - 1);
}

static void SAVEBUFF3 (u32 w1, u32 w2)
{
    u16 length = (w1 >> 12) & 0xfff;
    if (length == 0) { return; }

    dma_write_fast((w2 & 0xfffff8), (w1 + 0x4f0) & 0xff8, length - 1);
}

static void LOADADPCM3 (u32 w1, u32 w2)
{
    adpcm_load_codebook(
            naudio.adpcm_codebook,
            w2 & 0xffffff,
            w1 & 0xffff);
}

static void DMEMMOVE3 (u32 w1, u32 w2)
{
    dmem_move(
            0x4f0 + parse_hi(w2),
            0x4f0 + parse_lo(w1),
            align(parse_lo(w2), 4));
}

static void SETLOOP3 (u32 inst1, u32 inst2) {
    naudio.loop = (inst2 & 0xffffff);
}

static void ADPCM3 (u32 w1, u32 w2)
{
    unsigned flags = (w2 >> 28) & 0x0f;

    adpcm_decode(
            flags & A_INIT,
            flags & A_LOOP,
            0, // not supported in this version
            (s16*)naudio.adpcm_codebook,
            naudio.loop,
            w1 & 0xffffff,
            0x4f0 + ((w2 >> 12) & 0xf),
            0x4f0 + (w2 & 0xfff),
            align((w2 >> 16) & 0xfff, 32) >> 5);
}

static void RESAMPLE3 (u32 w1, u32 w2)
{
    resample_buffer(
            ((w2 >> 30) & 0x3) & A_INIT,
            w1 & 0xffffff,
            ((w2 >> 14) & 0xffff) << 1,
            (((w2 >> 2) & 0xfff) + 0x4f0) >> 1,
            ((w2 & 0x3) ? 0x660 : 0x4f0) >> 1,
            0x170 >> 1);
}

static void INTERLEAVE3 (u32 w1, u32 w2)
{
    interleave_buffers(
            0xb40,
            0x9d0,
            0x4f0,
            0x170 >> 2);
}

static void MP3ADDY (u32 w1, u32 w2)
{
    /* do nothing ? */
}

static void MP3 (u32 w1, u32 w2)
{
    mp3_decode(
            w2 & 0xffffff,
            (w1 >> 1) & 0x0f);
}

static void LOADADPCM2 (u32 w1, u32 w2)
{
    adpcm_load_codebook(
            audio2.adpcm_codebook,
            w2 & 0xffffff,
            w1 & 0xffff);
}

static void SETLOOP2 (u32 inst1, u32 inst2) {
    audio2.loop = inst2 & 0xffffff; // No segment?
}

static void SETBUFF2 (u32 inst1, u32 inst2) {
    audio2.in    = (u16)(inst1);            // 0x00
    audio2.out   = (u16)((inst2 >> 0x10)); // 0x02
    audio2.count = (u16)(inst2);            // 0x04
}

static void ADPCM2 (u32 w1, u32 w2)
{
    unsigned flags = parse_flags(w1);

    adpcm_decode(
            flags & A_INIT,
            flags & A_LOOP,
            flags & 0x4,
            (s16*)audio2.adpcm_codebook,
            audio2.loop,
            parse_address(w2),
            audio2.in,
            audio2.out,
            align(audio2.count, 32) >> 5);
}

static void CLEARBUFF2 (u32 inst1, u32 inst2) {
    u16 addr = (u16)(inst1 & 0xffff);
    u16 count = (u16)(inst2 & 0xffff);
    if (count > 0)
        memset(rsp.DMEM+addr, 0, count);
}

static void LOADBUFF2 (u32 w1, u32 w2)
{
    dma_read_fast(w1 & 0xff8, parse_address(w2) & ~7, ((w1 >> 12) & 0xff0) - 1);
}

static void SAVEBUFF2 (u32 w1, u32 w2)
{
    dma_write_fast(parse_address(w2) & ~7, w1 & 0xff8, ((w1 >> 12) & 0xff0) - 1);
}


static void MIXER2 (u32 w1, u32 w2)
{
    mix_buffers(
            parse_hi(w2),
            parse_lo(w2),
            ((w1 >> 12) & 0xff0) >> 1,
            (s16)parse_lo(w1));
}

static void RESAMPLE2 (u32 w1, u32 w2)
{
    resample_buffer(
            parse_flags(w1) & A_INIT,
            parse_address(w2),
            (unsigned int)(parse_lo(w1)) << 1,
            audio2.in >> 1,
            audio2.out >> 1,
            align(audio2.count, 16) >> 1);
}

static void DMEMMOVE2 (u32 w1, u32 w2)
{
    int count = (int)parse_lo(w2);

    if (count == 0) { return; }

    dmem_move(
        parse_hi(w2),
        parse_lo(w1),
        align(count, 4));
}

static void ENVSETUP1 (u32 inst1, u32 inst2) {
    u32 tmp;

    audio2.t3 = inst1 & 0xFFFF;
    tmp = (inst1 >> 0x8) & 0xFF00;
    audio2.env[4] = (u16)tmp;
    tmp += audio2.t3;
    audio2.env[5] = (u16)tmp;
    audio2.s5 = inst2 >> 0x10;
    audio2.s6 = inst2 & 0xFFFF;
}

static void ENVSETUP2 (u32 inst1, u32 inst2) {
    u32 tmp;

    tmp = (inst2 >> 0x10);
    audio2.env[0] = (u16)tmp;
    tmp += audio2.s5;
    audio2.env[1] = (u16)tmp;
    tmp = inst2 & 0xffff;
    audio2.env[2] = (u16)tmp;
    tmp += audio2.s6;
    audio2.env[3] = (u16)tmp;
}

static void ENVMIXER2 (u32 inst1, u32 inst2) {

    s16 *bufft6, *bufft7, *buffs0, *buffs1;
    s16 *buffs3;
    s32 count;
    u32 adder;

    s16 vec9, vec10;

    s16 v2[8];

    buffs3 = (s16 *)(rsp.DMEM + ((inst1 >> 0x0c)&0x0ff0));
    bufft6 = (s16 *)(rsp.DMEM + ((inst2 >> 0x14)&0x0ff0));
    bufft7 = (s16 *)(rsp.DMEM + ((inst2 >> 0x0c)&0x0ff0));
    buffs0 = (s16 *)(rsp.DMEM + ((inst2 >> 0x04)&0x0ff0));
    buffs1 = (s16 *)(rsp.DMEM + ((inst2 << 0x04)&0x0ff0));


    v2[0] = 0 - (s16)((inst1 & 0x2) >> 1);
    v2[1] = 0 - (s16)((inst1 & 0x1));
    v2[2] = 0 - (s16)((inst1 & 0x8) >> 1);
    v2[3] = 0 - (s16)((inst1 & 0x4) >> 1);

    count = (inst1 >> 8) & 0xff;

    if (!isMKABI) {
        audio2.s5 *= 2; audio2.s6 *= 2; audio2.t3 *= 2;
        adder = 0x10;
    } else {
        inst1 = 0;
        adder = 0x8;
        audio2.t3 = 0;
    }


    while (count > 0) {
        int x;
        for (x=0; x < 0x8; x++) {
            vec9  = (s16)(((s32)buffs3[x^S] * (u32)audio2.env[0]) >> 0x10) ^ v2[0];
            vec10 = (s16)(((s32)buffs3[x^S] * (u32)audio2.env[2]) >> 0x10) ^ v2[1];

            sadd(&bufft6[x^S], vec9);
            sadd(&bufft7[x^S], vec10);

            vec9  = (s16)(((s32)vec9  * (u32)audio2.env[4]) >> 0x10) ^ v2[2];
            vec10 = (s16)(((s32)vec10 * (u32)audio2.env[4]) >> 0x10) ^ v2[3];
            if (inst1 & 0x10)
            {
                sadd(&buffs0[x^S], vec10);
                sadd(&buffs1[x^S], vec9);
            }
            else
            {
                sadd(&buffs0[x^S], vec9);
                sadd(&buffs1[x^S], vec10);
            }
        }

        if (!isMKABI)
        for (x=0x8; x < 0x10; x++) {
            vec9  = (s16)(((s32)buffs3[x^S] * (u32)audio2.env[1]) >> 0x10) ^ v2[0];
            vec10 = (s16)(((s32)buffs3[x^S] * (u32)audio2.env[3]) >> 0x10) ^ v2[1];
            
            sadd(&bufft6[x^S], vec9);
            sadd(&bufft7[x^S], vec10);

            vec9  = (s16)(((s32)vec9  * (u32)audio2.env[5]) >> 0x10) ^ v2[2];
            vec10 = (s16)(((s32)vec10 * (u32)audio2.env[5]) >> 0x10) ^ v2[3];
            if (inst1 & 0x10)
            {
                sadd(&buffs0[x^S], vec10);
                sadd(&buffs1[x^S], vec9);
            }
            else
            {
                sadd(&buffs0[x^S], vec9);
                sadd(&buffs1[x^S], vec10);
            }
        }

        bufft6 += adder; bufft7 += adder;
        buffs0 += adder; buffs1 += adder;
        buffs3 += adder; count  -= adder;
        audio2.env[0] += (u16)audio2.s5; audio2.env[1] += (u16)audio2.s5;
        audio2.env[2] += (u16)audio2.s6; audio2.env[3] += (u16)audio2.s6;
        audio2.env[4] += (u16)audio2.t3; audio2.env[5] += (u16)audio2.t3;
    }
}

static void DUPLICATE2(u32 inst1, u32 inst2) {
    unsigned short Count = (inst1 >> 16) & 0xff;
    unsigned short In  = inst1&0xffff;
    unsigned short Out = (inst2>>16);

    unsigned short buff[64];
    
    memcpy(buff,rsp.DMEM+In,128);

    while(Count) {
        memcpy(rsp.DMEM+Out,buff,128);
        Out+=128;
        Count--;
    }
}

static void INTERL2 (u32 inst1, u32 inst2) {
    short Count = inst1 & 0xffff;
    unsigned short  Out   = inst2 & 0xffff;
    unsigned short In     = (inst2 >> 16);

    unsigned char *src,*dst;
    src=(unsigned char *)(rsp.DMEM);//[In];
    dst=(unsigned char *)(rsp.DMEM);//[Out];
    while(Count) {
        *(short *)(dst+(Out^S8)) = *(short *)(src+(In^S8));
        Out += 2;
        In  += 4;
        Count--;
    }
}

static void INTERLEAVE2 (u32 w1, u32 w2)
{
    u16 out;
    int count = ((w1 >> 12) & 0xff0);
    if (count == 0)
    {
        out = audio2.out;
        count = audio2.count;
    }
    else
    {
        out = parse_lo(w1);
    }

    // TODO: verify L/R order ?
    interleave_buffers(
            parse_lo(w2),
            parse_hi(w2),
            out,
            count >> 2);
}

static void ADDMIXER (u32 inst1, u32 inst2) {
    short Count   = (inst1 >> 12) & 0x00ff0;
    u16 InBuffer  = (inst2 >> 16);
    u16 OutBuffer = inst2 & 0xffff;
    int cntr;

    s16 *inp  = (s16 *)(rsp.DMEM + InBuffer);
    s16 *outp = (s16 *)(rsp.DMEM + OutBuffer);
    for (cntr = 0; cntr < Count; cntr+=2)
        sadd(outp++, *(inp++));
}

static void HILOGAIN (u32 inst1, u32 inst2) {
    u16 cnt = inst1 & 0xffff;
    u16 out = (inst2 >> 16) & 0xffff;
    s16 hi  = (s16)((inst1 >> 4) & 0xf000);
    u16 lo  = (inst1 >> 20) & 0xf;
    s16 *src;

    src = (s16 *)(rsp.DMEM+out);
    s32 tmp, val;

    while(cnt) {
        val = (s32)*src;
        tmp = ((val * (s32)hi) >> 16) + (u32)(val * lo);
        *src = clamp_s16(tmp);
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
                return;
            }

            if (t4 == 0) {
                lutt5 = (short *)(save+0x10);
            }

            lutt5 = (short *)(save+0x10);

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
            inp2 = (short *)(rsp.DMEM+inPtr);
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
            memcpy (rsp.DMEM+(inst1&0xffff), outbuff, cnt);
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






/* Audio Binary Interface tables */
static const acmd_callback_t ABI1[0x10] =
{
    SPNOOP,     ADPCM,      CLEARBUFF,  ENVMIXER,
    LOADBUFF,   RESAMPLE,   SAVEBUFF,   SEGMENT,
    SETBUFF,    SETVOL,     DMEMMOVE,   LOADADPCM,
    MIXER,      INTERLEAVE, POLEF,      SETLOOP
};

// FIXME: ABI2 in fact is a mix of at least 7 differents ABI which are mostly compatible
// but not totally, that's why there is a isZeldaABI/isMKABI workaround.
static const acmd_callback_t ABI2[0x20] =
{
    SPNOOP,     ADPCM2,         CLEARBUFF2, UNKNOWN,
    ADDMIXER,   RESAMPLE2,      UNKNOWN,    SEGMENT2,
    SETBUFF2,   DUPLICATE2,     DMEMMOVE2,  LOADADPCM2,
    MIXER2,     INTERLEAVE2,    HILOGAIN,   SETLOOP2,
    SPNOOP,     INTERL2,        ENVSETUP1,  ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,  SPNOOP,
    HILOGAIN,   SPNOOP,         DUPLICATE2, UNKNOWN,
    SPNOOP,     SPNOOP,         SPNOOP,     SPNOOP
};

static const acmd_callback_t ABI3[0x10] = 
{
    UNKNOWN,    ADPCM3,         CLEARBUFF3, ENVMIXER3,
    LOADBUFF3,  RESAMPLE3,      SAVEBUFF3,  MP3,
    MP3ADDY,    SETVOL3,        DMEMMOVE3,  LOADADPCM3,
    MIXER3,     INTERLEAVE3,    UNKNOWN,    SETLOOP3
};


/* global functions */
void alist_process_ABI1()
{
    alist_process(ABI1, 0x10);
}

// FIXME: get rid of that function
void init_ucode2() { isMKABI = isZeldaABI = 0; }

void alist_process_ABI2()
{
    alist_process(ABI2, 0x20);
}

void alist_process_ABI3()
{
    alist_process(ABI3, 0x10);
}

