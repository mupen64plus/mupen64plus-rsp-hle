/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - alist_nead.c                                    *
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
#include <string.h>
#include "hle.h"

#include "alist.h"
#include "adpcm.h"
#include "resample.h"

#define N_SEGMENTS 16

/* local variables */
static struct audio_mk_t
{
    // segments
    u32 segments[N_SEGMENTS]; // 0x320

    // main buffers
    u16 in;             // 0x0000(t8)
    u16 out;            // 0x0002(t8)
    u16 count;          // 0x0004(t8)

    // adpcm
    u32 adpcm_loop;     // 0x0010(t8)
    u16 adpcm_codebook[0x80];

    // envmixer2 envelopes (0: dry left, 1: dry right, 2: wet)
    u16 env_value[3];
    u16 env_step[3];
} l_audio_mk;

static struct audio2_t
{
    // main buffers
    u16 in;             // 0x0000(t8)
    u16 out;            // 0x0002(t8)
    u16 count;          // 0x0004(t8)

    // adpcm
    u32 adpcm_loop;     // 0x0010(t8)
    u16 adpcm_codebook[0x80];

    // envmixer2 envelopes (0: dry left, 1: dry right, 2: wet)
    u16 env_value[3];
    u16 env_step[3];
} l_audio2;

/* local functions */
static void swap(s16 **a, s16 **b)
{
    s16* tmp = *b;
    *b = *a;
    *a = tmp;
}

static void envmixer2(
        u16* const env_value,
        u16* const env_step,
        const s16* const xor_masks,
        unsigned swap_wet_LR,
        u16 dmemi,
        u16 dmem_dry_left,
        u16 dmem_dry_right,
        u16 dmem_wet_left,
        u16 dmem_wet_right,
        s32 count)
{
    unsigned i;
    s16 vec9, vec10;

    s16 *in = (s16*)(rsp.DMEM + dmemi);
    s16 *dl = (s16*)(rsp.DMEM + dmem_dry_left);
    s16 *dr = (s16*)(rsp.DMEM + dmem_dry_right);
    s16 *wl = (s16*)(rsp.DMEM + dmem_wet_left);
    s16 *wr = (s16*)(rsp.DMEM + dmem_wet_right);

    if (swap_wet_LR)
    {
        swap(&wl, &wr);
    }

    while (count > 0)
    {
        for (i = 0; i < 8; ++i)
        {
            vec9  = (s16)(((s32)in[i^S] * (u32)env_value[0]) >> 16) ^ xor_masks[0];
            vec10 = (s16)(((s32)in[i^S] * (u32)env_value[1]) >> 16) ^ xor_masks[1];

            sadd(&dl[i^S], vec9);
            sadd(&dr[i^S], vec10);

            vec9  = (s16)(((s32)vec9  * (u32)env_value[2]) >> 16) ^ xor_masks[2];
            vec10 = (s16)(((s32)vec10 * (u32)env_value[2]) >> 16) ^ xor_masks[3];

            sadd(&wl[i^S], vec9);
            sadd(&wr[i^S], vec10);
        }

        dl += 8; dr += 8;
        wl += 8; wr += 8;
        in += 8; count -= 8;
        env_value[0] += env_step[0];
        env_value[1] += env_step[1];
        env_value[2] += env_step[2];
    }
}



/* Audio commands */
static void SPNOOP(u32 w1, u32 w2)
{
}

static void UNKNOWN(u32 w1, u32 w2)
{
    u8 acmd = parse(w1, 24, 8);

    DebugMessage(M64MSG_WARNING,
            "Unknown audio command %d: %08x %08x",
            acmd, w1, w2);
}

static void SEGMENT_MK(u32 w1, u32 w2)
{
    segoffset_store(w2, l_audio_mk.segments, N_SEGMENTS);
}

static void LOADADPCM_MK(u32 w1, u32 w2)
{
    u16 count   = parse(w1, 0, 16);
    u32 address = segoffset_load(w2, l_audio_mk.segments, N_SEGMENTS);

    adpcm_load_codebook(
            l_audio_mk.adpcm_codebook,
            address,
            count);
}

static void SETLOOP_MK(u32 w1, u32 w2)
{
    u32 address = segoffset_load(w2, l_audio_mk.segments, N_SEGMENTS);

    l_audio_mk.adpcm_loop = address;
}

static void SETBUFF_MK(u32 w1, u32 w2)
{
    l_audio_mk.in    = parse(w1,  0, 16);
    l_audio_mk.out   = parse(w2, 16, 16);
    l_audio_mk.count = parse(w2,  0, 16);
}

static void POLEF_MK(u32 w1, u32 w2)
{
    if (l_audio_mk.count == 0) { return; }

    u16 flags = parse(w1, 16, 16);
    u16 gain  = parse(w1,  0, 16);
    u32 address = segoffset_load(w2, l_audio_mk.segments, N_SEGMENTS);

    adpcm_polef(
            flags & A_INIT,
            gain,
            (s16*)l_audio_mk.adpcm_codebook,
            address,
            l_audio_mk.in,
            l_audio_mk.out,
            align(l_audio_mk.count, 16));
}

static void ADPCM_MK(u32 w1, u32 w2)
{
    u8  flags   = parse(w1, 16,  8);
    u32 address = segoffset_load(w2, l_audio_mk.segments, N_SEGMENTS);

    adpcm_decode(
            flags & A_INIT,
            flags & A_LOOP,
            0, // not supported in this ucode version
            (s16*)l_audio_mk.adpcm_codebook,
            l_audio_mk.adpcm_loop,
            address,
            l_audio_mk.in,
            l_audio_mk.out,
            align(l_audio_mk.count, 32) >> 5);
}

static void LOADBUFF_MK(u32 w1, u32 w2)
{
    u16 count   = parse(w1, 12, 12);
    u16 dmem    = parse(w1,  0, 12);
    u32 address = segoffset_load(w2, l_audio_mk.segments, N_SEGMENTS);

    dma_read_fast(dmem & ~7, address & ~7, (count & 0xff0) - 1);
}

static void SAVEBUFF_MK(u32 w1, u32 w2)
{
    u16 count   = parse(w1, 12, 12);
    u16 dmem    = parse(w1,  0, 12);
    u32 address = segoffset_load(w2, l_audio_mk.segments, N_SEGMENTS);

    dma_write_fast(address & ~7, dmem & ~7, (count & 0xff0) - 1);
}

static void RESAMPLE_MK(u32 w1, u32 w2)
{
    u8  flags   = parse(w1, 16,  8);
    u16 pitch   = parse(w1,  0, 16);
    u32 address = segoffset_load(w2, l_audio_mk.segments, N_SEGMENTS);

    resample_buffer(
            flags & A_INIT,
            address,
            (u32)pitch << 1,
            l_audio_mk.in >> 1,
            l_audio_mk.out >> 1,
            align(l_audio_mk.count, 16) >> 1);
}

static void ENVSETUP1_MK(u32 w1, u32 w2)
{
    l_audio_mk.env_value[2] = parse(w1, 16,  8) << 8;
    l_audio_mk.env_step[2]  = 0; // not supported in this ucode version
    l_audio_mk.env_step[0]  = parse(w2, 16, 16);
    l_audio_mk.env_step[1]  = parse(w2,  0, 16);
}

static void ENVSETUP2_MK(u32 w1, u32 w2)
{
    l_audio_mk.env_value[0] = parse(w2, 16, 16);
    l_audio_mk.env_value[1] = parse(w2,  0, 16);
}

static void ENVMIXER_MK(u32 w1, u32 w2)
{
    s16 xor_masks[4];

    u16 dmemi = parse(w1, 16, 8) << 4;
    s32 count = (s32)parse(w1, 8, 8);
    xor_masks[2] = 0;
    xor_masks[3] = 0;
    xor_masks[0] = 0 - (s16)parse(w1, 1, 1);
    xor_masks[1] = 0 - (s16)parse(w1, 0, 1);
    u16 dmem_dry_left  = parse(w2, 24, 8) << 4;
    u16 dmem_dry_right = parse(w2, 16, 8) << 4;
    u16 dmem_wet_left  = parse(w2,  8, 8) << 4;
    u16 dmem_wet_right = parse(w2,  0, 8) << 4;
    
    envmixer2(
        l_audio_mk.env_value,
        l_audio_mk.env_step,
        xor_masks,
        0,
        dmemi,
        dmem_dry_left,
        dmem_dry_right,
        dmem_wet_left,
        dmem_wet_right,
        count);
}

static void INTERLEAVE_MK(u32 w1, u32 w2)
{
    if (l_audio_mk.count == 0) { return; }

    u16 left  = parse(w2, 16, 16);
    u16 right = parse(w2,  0, 16);
    
    interleave_buffers(
            l_audio_mk.out,
            left,
            right,
            l_audio_mk.count >> 1);
}










static void LOADADPCM2(u32 w1, u32 w2)
{
    u16 count   = parse(w1, 0, 16);
    u32 address = parse(w2, 0, 24);

    adpcm_load_codebook(
            l_audio2.adpcm_codebook,
            address,
            count);
}

static void SETLOOP2(u32 w1, u32 w2)
{
    u32 address = parse(w2, 0, 24);

    l_audio2.adpcm_loop = address;
}

static void SETBUFF2(u32 w1, u32 w2)
{
    l_audio2.in    = parse(w1,  0, 16);
    l_audio2.out   = parse(w2, 16, 16);
    l_audio2.count = parse(w2,  0, 16);
}

static void ADPCM2(u32 w1, u32 w2)
{
    u8  flags   = parse(w1, 16,  8);
    u32 address = parse(w2,  0, 24);

    adpcm_decode(
            flags & A_INIT,
            flags & A_LOOP,
            flags & 0x4,
            (s16*)l_audio2.adpcm_codebook,
            l_audio2.adpcm_loop,
            address,
            l_audio2.in,
            l_audio2.out,
            align(l_audio2.count, 32) >> 5);
}

static void CLEARBUFF2(u32 w1, u32 w2)
{
    u16 dmem  = parse(w1, 0, 16);
    u16 count = parse(w2, 0, 16);
    
    if (count > 0)
        memset(rsp.DMEM + dmem, 0, count);
}

static void LOADBUFF2(u32 w1, u32 w2)
{
    u16 count   = parse(w1, 12, 12);
    u16 dmem    = parse(w1,  0, 12);
    u32 address = parse(w2,  0, 24);

    dma_read_fast(dmem & ~7, address & ~7, (count & 0xff0) - 1);
}

static void SAVEBUFF2(u32 w1, u32 w2)
{
    u16 count   = parse(w1, 12, 12);
    u16 dmem    = parse(w1,  0, 12);
    u32 address = parse(w2,  0, 24);

    dma_write_fast(address & ~7, dmem & ~7, (count & 0xff0) - 1);
}


static void MIXER2(u32 w1, u32 w2)
{
    u16 count = parse(w1, 12, 12);
    u16 gain  = parse(w1,  0, 16);
    u16 dmemi = parse(w2, 16, 16);
    u16 dmemo = parse(w2,  0, 16);

    mix_buffers(
            dmemo,
            dmemi,
            (count & ~0xf) >> 1,
            (s16)gain);
}

static void RESAMPLE2(u32 w1, u32 w2)
{
    u8  flags   = parse(w1, 16,  8);
    u16 pitch   = parse(w1,  0, 16);
    u32 address = parse(w2,  0, 24);

    resample_buffer(
            flags & A_INIT,
            address,
            (u32)pitch << 1,
            l_audio2.in >> 1,
            l_audio2.out >> 1,
            align(l_audio2.count, 16) >> 1);
}

static void RESAMPLE2_ZOH(u32 w1, u32 w2)
{
    u32 pitch = parse(w1, 0, 16) << 1;
    u32 pitch_accu = parse(w2, 0, 16);

    resample_zoh(
        pitch_accu,
        pitch,
        l_audio2.in >> 1,
        l_audio2.out >> 1,
        align(l_audio2.count,8) >> 1);
}

static void DMEMMOVE2(u32 w1, u32 w2)
{
    u16 dmemi = parse(w1,  0, 16);
    u16 dmemo = parse(w2, 16, 16);
    u16 count = parse(w2,  0, 16);

    if (count == 0) { return; }

    dmem_move(
        dmemo,
        dmemi,
        align(count, 4));
}

static void ENVSETUP1(u32 w1, u32 w2)
{
    l_audio2.env_value[2] = parse(w1, 16,  8) << 8;
    l_audio2.env_step[2]  = parse(w1,  0, 16);
    l_audio2.env_step[0]  = parse(w2, 16, 16);
    l_audio2.env_step[1]  = parse(w2,  0, 16);
}

static void ENVSETUP2(u32 w1, u32 w2)
{
    l_audio2.env_value[0] = parse(w2, 16, 16);
    l_audio2.env_value[1] = parse(w2,  0, 16);
}

static void ENVMIXER2(u32 w1, u32 w2)
{
    s16 xor_masks[4];

    u16 dmemi = parse(w1, 16, 8) << 4;
    s32 count = (s32)parse(w1, 8, 8);
    unsigned swap_wet_LR = parse(w1, 4, 1);
    xor_masks[2] = 0 - (s16)(parse(w1, 3, 1) << 2);
    xor_masks[3] = 0 - (s16)(parse(w1, 2, 1) << 1);
    xor_masks[0] = 0 - (s16)parse(w1, 1, 1);
    xor_masks[1] = 0 - (s16)parse(w1, 0, 1);
    u16 dmem_dry_left  = parse(w2, 24, 8) << 4;
    u16 dmem_dry_right = parse(w2, 16, 8) << 4;
    u16 dmem_wet_left  = parse(w2,  8, 8) << 4;
    u16 dmem_wet_right = parse(w2,  0, 8) << 4;
    
    envmixer2(
        l_audio2.env_value,
        l_audio2.env_step,
        xor_masks,
        swap_wet_LR,
        dmemi,
        dmem_dry_left,
        dmem_dry_right,
        dmem_wet_left,
        dmem_wet_right,
        count);
}

static void DUPLICATE2(u32 w1, u32 w2)
{
    u16 count = parse(w1, 16,  8);
    u16 dmemi = parse(w1,  0, 16);
    u16 dmemo = parse(w2, 16, 16);

    unsigned short buff[64];
    
    memcpy(buff, rsp.DMEM + dmemi, 128);

    while (count != 0)
    {
        memcpy(rsp.DMEM + dmemo, buff, 128);
        dmemo += 128;
        --count;
    }
}

static void INTERL2(u32 w1, u32 w2)
{
    u16 count = parse(w1,  0, 16);
    u16 dmemi = parse(w2, 16, 16);
    u16 dmemo = parse(w2,  0, 16);

    while (count != 0)
    {
        *(u16*)(rsp.DMEM + (dmemo ^ S8)) = *(u16*)(rsp.DMEM + (dmemi ^ S8));

        dmemo += 2;
        dmemi += 4;
        --count;
    }
}

static void INTERLEAVE2_SF(u32 w1, u32 w2)
{
    if (l_audio2.count == 0) { return; }

    u16 left  = parse(w2, 16, 16);
    u16 right = parse(w2,  0, 16);

    interleave_buffers(
            l_audio2.out,
            left,
            right,
            l_audio2.count >> 1);
}

static void INTERLEAVE2(u32 w1, u32 w2)
{
    u16 count = parse(w1, 16,  8) << 4;
    u16 out   = parse(w1, 0, 16);
    u16 left  = parse(w2, 16, 16);
    u16 right = parse(w2,  0, 16);
    
    interleave_buffers(
            out,
            left,
            right,
            count >> 1);
}

static void ADDMIXER(u32 w1, u32 w2)
{
    u16 count = parse(w1, 16,  8) << 4;
    u16 dmemi = parse(w2, 16, 16);
    u16 dmemo = parse(w2,  0, 16);

    const s16 *src = (s16 *)(rsp.DMEM + dmemi);
    s16 *dst       = (s16 *)(rsp.DMEM + dmemo);

    while (count != 0)
    {
        sadd(dst++, *(src++));
        count -= 2;
    }
}

static void HILOGAIN(u32 w1, u32 w2)
{
    u16 count = parse(w1,  0, 16);
    u8  gain  = parse(w1, 16,  8);  /* Q4.4 */
    u16 dmem  = parse(w2, 16, 16);

    s16 *ptr = (s16*)(rsp.DMEM + dmem);

    while (count != 0)
    {
        *ptr = clamp_s16(((s32)(*ptr) * gain) >> 4);

        ++ptr;
        count -= 2;
    }
}

static void FILTER2(u32 w1, u32 w2)
{
    u8  t4      = parse(w1, 16,  8);
    u16 lw1     = parse(w1,  0, 16);
    u32 address = parse(w2,  0, 24);


    static int cnt = 0;
    static s16 *lutt6;
    static s16 *lutt5;

    int x;
    u8 *save = rsp.RDRAM + address;

    if (t4 > 1)
    { // Then set the cnt variable
        cnt = lw1;
        lutt6 = (s16 *)save;
        return;
    }

    if (t4 == 0)
    {
        lutt5 = (s16*)(save+0x10);
    }

    lutt5 = (s16*)(save+0x10);

    for (x = 0; x < 8; x++)
    {
        s32 a;
        a = (lutt5[x] + lutt6[x]) >> 1;
        lutt5[x] = lutt6[x] = (s16)a;
    }

    short *inp1, *inp2; 
    s32 out1[8];
    s16 outbuff[0x3c0], *outp;
    inp1 = (short *)(save);
    outp = outbuff;
    inp2 = (short *)(rsp.DMEM+lw1);
    for (x = 0; x < cnt; x+=0x10)
    {
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
//          memcpy (rsp.RDRAM+(w2&0xFFFFFF), dmem+0xFB0, 0x20);
    memcpy (save, inp2-8, 0x10);
    memcpy (rsp.DMEM + lw1, outbuff, cnt);
}

static void POLEF2(u32 w1, u32 w2)
{
    if (l_audio2.count == 0) { return; }

    u16 flags   = parse(w1, 16, 16);
    u16 gain    = parse(w1,  0, 16);
    u32 address = parse(w2,  0, 24);

    adpcm_polef(
            flags & A_INIT,
            gain,
            (s16*)l_audio2.adpcm_codebook,
            address,
            l_audio2.in,
            l_audio2.out,
            align(l_audio2.count, 16));
}

static void COPYBLOCKS2(u32 w1, u32 w2)
{
    u8  count      = parse(w1, 16,  8);
    u16 dmemi      = parse(w1,  0, 16);
    u16 dmemo      = parse(w2, 16, 16);
    u16 block_size = parse(w2,  0, 16);

    assert((dmemi & 0x3) == 0);
    assert((dmemo & 0x3) == 0);

    u16 t4;
    do
    {
        --count;
        t4 = block_size;

        do
        {
            memcpy(rsp.DMEM + dmemo, rsp.DMEM + dmemi, 0x20);
            t4 -= 0x20;
            dmemi += 0x20;
            dmemo += 0x20;
        } while(t4 > 0);

    } while(count > 0);
}


/* Audio Binary Interface tables */
static const acmd_callback_t ABI_MK[0x20] =
{
    SPNOOP,     ADPCM_MK,       CLEARBUFF2,     SPNOOP,
    SPNOOP,     RESAMPLE_MK,    SPNOOP,         SEGMENT_MK,
    SETBUFF_MK, SPNOOP,         DMEMMOVE2,      LOADADPCM_MK,
    MIXER2,     INTERLEAVE_MK,  POLEF_MK,       SETLOOP_MK,
    COPYBLOCKS2,INTERL2,        ENVSETUP1_MK,   ENVMIXER_MK,
    LOADBUFF_MK,SAVEBUFF_MK,    ENVSETUP2_MK,   SPNOOP,
    SPNOOP,     SPNOOP,         SPNOOP,         SPNOOP,
    SPNOOP,     SPNOOP,         SPNOOP,         SPNOOP
};

static const acmd_callback_t ABI_SF[0x20] =
{
    SPNOOP,     ADPCM2,         CLEARBUFF2,     SPNOOP,
    ADDMIXER,   RESAMPLE2,      RESAMPLE2_ZOH,  SPNOOP,
    SETBUFF2,   SPNOOP,         DMEMMOVE2,      LOADADPCM2,
    MIXER2,     INTERLEAVE2_SF, POLEF2,         SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,      ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,      SPNOOP,
    HILOGAIN,   UNKNOWN,        DUPLICATE2,     SPNOOP,
    SPNOOP,     SPNOOP,         SPNOOP,         SPNOOP
};

static const acmd_callback_t ABI_SFJ[0x20] =
{
    SPNOOP,     ADPCM2,         CLEARBUFF2,     SPNOOP,
    ADDMIXER,   RESAMPLE2,      RESAMPLE2_ZOH,  SPNOOP,
    SETBUFF2,   SPNOOP,         DMEMMOVE2,      LOADADPCM2,
    MIXER2,     INTERLEAVE2_SF, POLEF2,         SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,      ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,      UNKNOWN,
    HILOGAIN,   UNKNOWN,        DUPLICATE2,     SPNOOP,
    SPNOOP,     SPNOOP,         SPNOOP,         SPNOOP
};

static const acmd_callback_t ABI_FZ[0x20] =
{
    UNKNOWN,    ADPCM2,         CLEARBUFF2, SPNOOP,
    ADDMIXER,   RESAMPLE2,      SPNOOP,     SPNOOP,
    SETBUFF2,   SPNOOP,         DMEMMOVE2,  LOADADPCM2,
    MIXER2,     INTERLEAVE2,    SPNOOP,     SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,  ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,  UNKNOWN,
    SPNOOP,     UNKNOWN,        DUPLICATE2, SPNOOP,
    SPNOOP,     SPNOOP,         SPNOOP,     SPNOOP
};

static const acmd_callback_t ABI_WRJB[0x20] =
{
    SPNOOP,     ADPCM2,         CLEARBUFF2,     UNKNOWN,
    ADDMIXER,   RESAMPLE2,      RESAMPLE2_ZOH,  SPNOOP,
    SETBUFF2,   SPNOOP,         DMEMMOVE2,      LOADADPCM2,
    MIXER2,     INTERLEAVE2,    SPNOOP,         SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,      ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,      UNKNOWN,
    HILOGAIN,   UNKNOWN,        DUPLICATE2,     FILTER2,
    SPNOOP,     SPNOOP,         SPNOOP,         SPNOOP
};

static const acmd_callback_t ABI_YS[0x18] =
{
    UNKNOWN,    ADPCM2,         CLEARBUFF2,     UNKNOWN,
    ADDMIXER,   RESAMPLE2,      RESAMPLE2_ZOH,  FILTER2,
    SETBUFF2,   DUPLICATE2,     DMEMMOVE2,      LOADADPCM2,
    MIXER2,     INTERLEAVE2,    HILOGAIN,       SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,      ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,      UNKNOWN
};

static const acmd_callback_t ABI_1080[0x18] =
{
    UNKNOWN,    ADPCM2,         CLEARBUFF2,     UNKNOWN,
    ADDMIXER,   RESAMPLE2,      RESAMPLE2_ZOH,  FILTER2,
    SETBUFF2,   DUPLICATE2,     DMEMMOVE2,      LOADADPCM2,
    MIXER2,     INTERLEAVE2,    HILOGAIN,       SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,      ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,      UNKNOWN
};

static const acmd_callback_t ABI_OOT[0x18] =
{
    UNKNOWN,    ADPCM2,         CLEARBUFF2,     UNKNOWN,
    ADDMIXER,   RESAMPLE2,      RESAMPLE2_ZOH,  FILTER2,
    SETBUFF2,   DUPLICATE2,     DMEMMOVE2,      LOADADPCM2,
    MIXER2,     INTERLEAVE2,    HILOGAIN,       SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,      ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,      UNKNOWN
};

static const acmd_callback_t ABI_MM[0x18] =
{
    UNKNOWN,    ADPCM2,         CLEARBUFF2,     SPNOOP,
    ADDMIXER,   RESAMPLE2,      RESAMPLE2_ZOH,  FILTER2,
    SETBUFF2,   DUPLICATE2,     DMEMMOVE2,      LOADADPCM2,
    MIXER2,     INTERLEAVE2,    HILOGAIN,       SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,      ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,      UNKNOWN
};

static const acmd_callback_t ABI_MMB[0x18] =
{
    SPNOOP,     ADPCM2,         CLEARBUFF2,     SPNOOP,
    ADDMIXER,   RESAMPLE2,      RESAMPLE2_ZOH,  FILTER2,
    SETBUFF2,   DUPLICATE2,     DMEMMOVE2,      LOADADPCM2,
    MIXER2,     INTERLEAVE2,    HILOGAIN,       SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,      ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,      UNKNOWN
};


static const acmd_callback_t ABI_AC[0x18] =
{
    UNKNOWN,    ADPCM2,         CLEARBUFF2,     SPNOOP,
    ADDMIXER,   RESAMPLE2,      RESAMPLE2_ZOH,  FILTER2,
    SETBUFF2,   DUPLICATE2,     DMEMMOVE2,      LOADADPCM2,
    MIXER2,     INTERLEAVE2,    HILOGAIN,       SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,      ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,      UNKNOWN
};

/* global functions */
void alist_process_mk()
{
    memset(l_audio_mk.segments, 0, sizeof(l_audio_mk.segments[0])*N_SEGMENTS);
    alist_process(ABI_MK, 0x20);
}

void alist_process_sfj()
{
    alist_process(ABI_SFJ, 0x20);
}

void alist_process_sf()
{
    alist_process(ABI_SF, 0x20);
}

void alist_process_fz()
{
    alist_process(ABI_FZ, 0x20);
}

void alist_process_wrjb()
{
    alist_process(ABI_WRJB, 0x20);
}

void alist_process_ys()
{
    alist_process(ABI_YS, 0x18);
}

void alist_process_1080()
{
    alist_process(ABI_1080, 0x18);
}

void alist_process_oot()
{
    alist_process(ABI_OOT, 0x18);
}

void alist_process_mm()
{
    alist_process(ABI_MM, 0x18);
}

void alist_process_mmb()
{
    alist_process(ABI_MMB, 0x18);
}

void alist_process_ac()
{
    alist_process(ABI_AC, 0x18);
}

