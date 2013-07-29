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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "hle.h"

#include "arithmetic.h"
#include "alist.h"
#include "adpcm.h"
#include "resample.h"

#define N_SEGMENTS 16

/* local variables */
static struct alist_t
{
    // segments (only used by MK version)
    uint32_t segments[N_SEGMENTS]; // 0x320

    // main buffers
    uint16_t in;             // 0x0000(t8)
    uint16_t out;            // 0x0002(t8)
    uint16_t count;          // 0x0004(t8)

    // dram address of adpcm frame before loop point
    uint32_t loop;     // 0x0010(t8)

    // storage for adpcm codebooks and polef coefficients
    uint16_t table[0x80];

    // envmixer envelopes (0: dry left, 1: dry right, 2: wet)
    uint16_t env_value[3];
    uint16_t env_step[3];
} l_alist;

/* local functions */
static void swap(int16_t **a, int16_t **b)
{
    int16_t* tmp = *b;
    *b = *a;
    *a = tmp;
}

static void clear_segments()
{
    memset(l_alist.segments, 0, sizeof(l_alist.segments[0])*N_SEGMENTS);
}

static void envmixer(
        uint16_t* const env_value,
        uint16_t* const env_step,
        const int16_t* const xor_masks,
        bool swap_wet_LR,
        uint16_t dmemi,
        uint16_t dmem_dry_left,
        uint16_t dmem_dry_right,
        uint16_t dmem_wet_left,
        uint16_t dmem_wet_right,
        int32_t count)
{
    unsigned i;
    int16_t vec9, vec10;

    int16_t *in = (int16_t*)(rsp.DMEM + dmemi);
    int16_t *dl = (int16_t*)(rsp.DMEM + dmem_dry_left);
    int16_t *dr = (int16_t*)(rsp.DMEM + dmem_dry_right);
    int16_t *wl = (int16_t*)(rsp.DMEM + dmem_wet_left);
    int16_t *wr = (int16_t*)(rsp.DMEM + dmem_wet_right);

    if (swap_wet_LR)
    {
        swap(&wl, &wr);
    }

    while (count > 0)
    {
        for (i = 0; i < 8; ++i)
        {
            vec9  = (int16_t)(((int32_t)in[i^S] * (uint32_t)env_value[0]) >> 16) ^ xor_masks[0];
            vec10 = (int16_t)(((int32_t)in[i^S] * (uint32_t)env_value[1]) >> 16) ^ xor_masks[1];

            sadd(&dl[i^S], vec9);
            sadd(&dr[i^S], vec10);

            vec9  = (int16_t)(((int32_t)vec9  * (uint32_t)env_value[2]) >> 16) ^ xor_masks[2];
            vec10 = (int16_t)(((int32_t)vec10 * (uint32_t)env_value[2]) >> 16) ^ xor_masks[3];

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
static void SPNOOP(uint32_t w1, uint32_t w2)
{
}

static void UNKNOWN(uint32_t w1, uint32_t w2)
{
    uint8_t acmd = alist_parse(w1, 24, 8);

    DebugMessage(M64MSG_WARNING,
            "Unknown audio command %d: %08x %08x",
            acmd, w1, w2);
}

static void SEGMENT(uint32_t w1, uint32_t w2)
{
    alist_segments_store(w2, l_alist.segments, N_SEGMENTS);
}

static void LOADADPCM_seg(uint32_t w1, uint32_t w2)
{
    uint16_t length  = alist_parse(w1, 0, 16);
    uint32_t address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    dram_read_many_u16(l_alist.table, address, length);
}

static void LOADADPCM_flat(uint32_t w1, uint32_t w2)
{
    uint16_t length  = alist_parse(w1, 0, 16);
    uint32_t address = alist_parse(w2, 0, 24);

    dram_read_many_u16(l_alist.table, address, length);
}

static void SETLOOP_seg(uint32_t w1, uint32_t w2)
{
    l_alist.loop = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);
}

static void SETLOOP_flat(uint32_t w1, uint32_t w2)
{
    l_alist.loop = alist_parse(w2, 0, 24);
}

static void SETBUFF(uint32_t w1, uint32_t w2)
{
    l_alist.in    = alist_parse(w1,  0, 16);
    l_alist.out   = alist_parse(w2, 16, 16);
    l_alist.count = alist_parse(w2,  0, 16);
}

static void POLEF_seg(uint32_t w1, uint32_t w2)
{
    if (l_alist.count == 0) { return; }

    uint16_t flags = alist_parse(w1, 16, 16);
    uint16_t gain  = alist_parse(w1,  0, 16);
    uint32_t address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    alist_polef(
            flags & A_INIT,
            gain,
            (int16_t*)l_alist.table,
            address,
            l_alist.out,
            l_alist.in,
            align(l_alist.count, 16));
}

static void POLEF_flat(uint32_t w1, uint32_t w2)
{
    if (l_alist.count == 0) { return; }

    uint16_t flags   = alist_parse(w1, 16, 16);
    uint16_t gain    = alist_parse(w1,  0, 16);
    uint32_t address = alist_parse(w2,  0, 24);

    alist_polef(
            flags & A_INIT,
            gain,
            (int16_t*)l_alist.table,
            address,
            l_alist.out,
            l_alist.in,
            align(l_alist.count, 16));
}

static void ADPCM_MK(uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = alist_parse(w1, 16,  8);
    uint32_t address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    adpcm_decode(
            flags & A_INIT,
            flags & A_LOOP,
            0, // not supported in this ucode version
            (int16_t*)l_alist.table,
            l_alist.loop,
            address,
            l_alist.in,
            l_alist.out,
            align(l_alist.count, 32) >> 5);
}

static void ADPCM_NEAD(uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = alist_parse(w1, 16,  8);
    uint32_t address = alist_parse(w2,  0, 24);

    adpcm_decode(
            flags & A_INIT,
            flags & A_LOOP,
            flags & 0x4,
            (int16_t*)l_alist.table,
            l_alist.loop,
            address,
            l_alist.in,
            l_alist.out,
            align(l_alist.count, 32) >> 5);
}

static void LOADBUFF_seg(uint32_t w1, uint32_t w2)
{
    uint16_t count   = alist_parse(w1, 12, 12);
    uint16_t dmem    = alist_parse(w1,  0, 12);
    uint32_t address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    dma_read_fast(dmem & ~7, address & ~7, (count & 0xff0) - 1);
}

static void LOADBUFF_flat(uint32_t w1, uint32_t w2)
{
    uint16_t count   = alist_parse(w1, 12, 12);
    uint16_t dmem    = alist_parse(w1,  0, 12);
    uint32_t address = alist_parse(w2,  0, 24);

    dma_read_fast(dmem & ~7, address & ~7, (count & 0xff0) - 1);
}

static void SAVEBUFF_seg(uint32_t w1, uint32_t w2)
{
    uint16_t count   = alist_parse(w1, 12, 12);
    uint16_t dmem    = alist_parse(w1,  0, 12);
    uint32_t address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    dma_write_fast(address & ~7, dmem & ~7, (count & 0xff0) - 1);
}

static void SAVEBUFF_flat(uint32_t w1, uint32_t w2)
{
    uint16_t count   = alist_parse(w1, 12, 12);
    uint16_t dmem    = alist_parse(w1,  0, 12);
    uint32_t address = alist_parse(w2,  0, 24);

    dma_write_fast(address & ~7, dmem & ~7, (count & 0xff0) - 1);
}

static void RESAMPLE_seg(uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = alist_parse(w1, 16,  8);
    uint16_t pitch   = alist_parse(w1,  0, 16);
    uint32_t address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    resample_buffer(
            flags & A_INIT,
            address,
            (uint32_t)pitch << 1,
            l_alist.in >> 1,
            l_alist.out >> 1,
            align(l_alist.count, 16) >> 1);
}

static void RESAMPLE_flat(uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = alist_parse(w1, 16,  8);
    uint16_t pitch   = alist_parse(w1,  0, 16);
    uint32_t address = alist_parse(w2,  0, 24);

    resample_buffer(
            flags & A_INIT,
            address,
            (uint32_t)pitch << 1,
            l_alist.in >> 1,
            l_alist.out >> 1,
            align(l_alist.count, 16) >> 1);
}

static void ENVSETUP1_MK(uint32_t w1, uint32_t w2)
{
    l_alist.env_value[2] = alist_parse(w1, 16,  8) << 8;
    l_alist.env_step[2]  = 0; // not supported in this ucode version
    l_alist.env_step[0]  = alist_parse(w2, 16, 16);
    l_alist.env_step[1]  = alist_parse(w2,  0, 16);
}

static void ENVSETUP1_NEAD(uint32_t w1, uint32_t w2)
{
    l_alist.env_value[2] = alist_parse(w1, 16,  8) << 8;
    l_alist.env_step[2]  = alist_parse(w1,  0, 16);
    l_alist.env_step[0]  = alist_parse(w2, 16, 16);
    l_alist.env_step[1]  = alist_parse(w2,  0, 16);
}

static void ENVSETUP2(uint32_t w1, uint32_t w2)
{
    l_alist.env_value[0] = alist_parse(w2, 16, 16);
    l_alist.env_value[1] = alist_parse(w2,  0, 16);
}

static void ENVMIXER_MK(uint32_t w1, uint32_t w2)
{
    int16_t xor_masks[4];

    uint16_t dmemi = alist_parse(w1, 16, 8) << 4;
    int32_t count = (int32_t)alist_parse(w1, 8, 8);
    xor_masks[2] = 0;
    xor_masks[3] = 0;
    xor_masks[0] = 0 - (int16_t)alist_parse(w1, 1, 1);
    xor_masks[1] = 0 - (int16_t)alist_parse(w1, 0, 1);
    uint16_t dmem_dry_left  = alist_parse(w2, 24, 8) << 4;
    uint16_t dmem_dry_right = alist_parse(w2, 16, 8) << 4;
    uint16_t dmem_wet_left  = alist_parse(w2,  8, 8) << 4;
    uint16_t dmem_wet_right = alist_parse(w2,  0, 8) << 4;
    
    envmixer(
        l_alist.env_value,
        l_alist.env_step,
        xor_masks,
        0,
        dmemi,
        dmem_dry_left,
        dmem_dry_right,
        dmem_wet_left,
        dmem_wet_right,
        count);
}

static void ENVMIXER_NEAD(uint32_t w1, uint32_t w2)
{
    int16_t xor_masks[4];

    uint16_t dmemi = alist_parse(w1, 16, 8) << 4;
    int32_t count = (int32_t)alist_parse(w1, 8, 8);
    unsigned swap_wet_LR = alist_parse(w1, 4, 1);
    xor_masks[2] = 0 - (int16_t)(alist_parse(w1, 3, 1) << 2);
    xor_masks[3] = 0 - (int16_t)(alist_parse(w1, 2, 1) << 1);
    xor_masks[0] = 0 - (int16_t)alist_parse(w1, 1, 1);
    xor_masks[1] = 0 - (int16_t)alist_parse(w1, 0, 1);
    uint16_t dmem_dry_left  = alist_parse(w2, 24, 8) << 4;
    uint16_t dmem_dry_right = alist_parse(w2, 16, 8) << 4;
    uint16_t dmem_wet_left  = alist_parse(w2,  8, 8) << 4;
    uint16_t dmem_wet_right = alist_parse(w2,  0, 8) << 4;
    
    envmixer(
        l_alist.env_value,
        l_alist.env_step,
        xor_masks,
        swap_wet_LR,
        dmemi,
        dmem_dry_left,
        dmem_dry_right,
        dmem_wet_left,
        dmem_wet_right,
        count);
}

static void INTERLEAVE_MK(uint32_t w1, uint32_t w2)
{
    if (l_alist.count == 0) { return; }

    uint16_t left  = alist_parse(w2, 16, 16);
    uint16_t right = alist_parse(w2,  0, 16);
    
    alist_interleave(
            l_alist.out,
            left,
            right,
            l_alist.count >> 1);
}

static void INTERLEAVE_NEAD(uint32_t w1, uint32_t w2)
{
    uint16_t count = alist_parse(w1, 16,  8) << 4;
    uint16_t out   = alist_parse(w1, 0, 16);
    uint16_t left  = alist_parse(w2, 16, 16);
    uint16_t right = alist_parse(w2,  0, 16);
    
    alist_interleave(
            out,
            left,
            right,
            count >> 1);
}


static void CLEARBUFF(uint32_t w1, uint32_t w2)
{
    uint16_t dmem  = alist_parse(w1, 0, 16);
    uint16_t count = alist_parse(w2, 0, 16);
    
    if (count > 0)
        memset(rsp.DMEM + dmem, 0, count);
}


static void MIXER(uint32_t w1, uint32_t w2)
{
    uint16_t count = alist_parse(w1, 12, 12);
    uint16_t gain  = alist_parse(w1,  0, 16);
    uint16_t dmemi = alist_parse(w2, 16, 16);
    uint16_t dmemo = alist_parse(w2,  0, 16);

    alist_mix(
            dmemo,
            dmemi,
            (count & ~0xf) >> 1,
            (int16_t)gain);
}

static void RESAMPLE_ZOH(uint32_t w1, uint32_t w2)
{
    uint32_t pitch      = alist_parse(w1, 0, 16) << 1;
    uint32_t pitch_accu = alist_parse(w2, 0, 16);

    resample_zoh(
        pitch_accu,
        pitch,
        l_alist.in >> 1,
        l_alist.out >> 1,
        align(l_alist.count,8) >> 1);
}

static void DMEMMOVE(uint32_t w1, uint32_t w2)
{
    uint16_t dmemi = alist_parse(w1,  0, 16);
    uint16_t dmemo = alist_parse(w2, 16, 16);
    uint16_t count = alist_parse(w2,  0, 16);

    if (count == 0) { return; }

    alist_dmemmove(
        dmemo,
        dmemi,
        align(count, 4));
}

static void DUPLICATE(uint32_t w1, uint32_t w2)
{
    uint16_t count = alist_parse(w1, 16,  8);
    uint16_t dmemi = alist_parse(w1,  0, 16);
    uint16_t dmemo = alist_parse(w2, 16, 16);

    unsigned short buff[64];
    
    memcpy(buff, rsp.DMEM + dmemi, 128);

    while (count != 0)
    {
        memcpy(rsp.DMEM + dmemo, buff, 128);
        dmemo += 128;
        --count;
    }
}

static void INTERL(uint32_t w1, uint32_t w2)
{
    uint16_t count = alist_parse(w1,  0, 16);
    uint16_t dmemi = alist_parse(w2, 16, 16);
    uint16_t dmemo = alist_parse(w2,  0, 16);

    while (count != 0)
    {
        *(uint16_t*)(rsp.DMEM + (dmemo ^ S8)) = *(uint16_t*)(rsp.DMEM + (dmemi ^ S8));

        dmemo += 2;
        dmemi += 4;
        --count;
    }
}

static void ADDMIXER(uint32_t w1, uint32_t w2)
{
    uint16_t count = alist_parse(w1, 16,  8) << 4;
    uint16_t dmemi = alist_parse(w2, 16, 16);
    uint16_t dmemo = alist_parse(w2,  0, 16);

    const int16_t *src = (int16_t *)(rsp.DMEM + dmemi);
    int16_t *dst       = (int16_t *)(rsp.DMEM + dmemo);

    while (count != 0)
    {
        sadd(dst++, *(src++));
        count -= 2;
    }
}

static void HILOGAIN(uint32_t w1, uint32_t w2)
{
    uint16_t count = alist_parse(w1,  0, 16);
    uint8_t  gain  = alist_parse(w1, 16,  8);  /* Q4.4 */
    uint16_t dmem  = alist_parse(w2, 16, 16);

    int16_t *ptr = (int16_t*)(rsp.DMEM + dmem);

    while (count != 0)
    {
        *ptr = clamp_s16(((int32_t)(*ptr) * gain) >> 4);

        ++ptr;
        count -= 2;
    }
}

static void FILTER(uint32_t w1, uint32_t w2)
{
    uint8_t  t4      = alist_parse(w1, 16,  8);
    uint16_t lw1     = alist_parse(w1,  0, 16);
    uint32_t address = alist_parse(w2,  0, 24);


    static int cnt = 0;
    static int16_t *lutt6;
    static int16_t *lutt5;

    int x;
    uint8_t *save = rsp.RDRAM + address;

    if (t4 > 1)
    { // Then set the cnt variable
        cnt = lw1;
        lutt6 = (int16_t *)save;
        return;
    }

    if (t4 == 0)
    {
        lutt5 = (int16_t*)(save+0x10);
    }

    lutt5 = (int16_t*)(save+0x10);

    for (x = 0; x < 8; x++)
    {
        int32_t a;
        a = (lutt5[x] + lutt6[x]) >> 1;
        lutt5[x] = lutt6[x] = (int16_t)a;
    }

    short *inp1, *inp2; 
    int32_t out1[8];
    int16_t outbuff[0x3c0], *outp;
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

static void COPYBLOCKS(uint32_t w1, uint32_t w2)
{
    uint8_t  count      = alist_parse(w1, 16,  8);
    uint16_t dmemi      = alist_parse(w1,  0, 16);
    uint16_t dmemo      = alist_parse(w2, 16, 16);
    uint16_t block_size = alist_parse(w2,  0, 16);

    assert((dmemi & 0x3) == 0);
    assert((dmemo & 0x3) == 0);

    uint16_t t4;
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
    SPNOOP,         ADPCM_MK,           CLEARBUFF,          SPNOOP,
    SPNOOP,         RESAMPLE_seg,       SPNOOP,             SEGMENT,
    SETBUFF,        SPNOOP,             DMEMMOVE,           LOADADPCM_seg,
    MIXER,          INTERLEAVE_MK,      POLEF_seg,          SETLOOP_seg,
    COPYBLOCKS,     INTERL,             ENVSETUP1_MK,       ENVMIXER_MK,
    LOADBUFF_seg,   SAVEBUFF_seg,       ENVSETUP2,          SPNOOP,
    SPNOOP,         SPNOOP,             SPNOOP,             SPNOOP,
    SPNOOP,         SPNOOP,             SPNOOP,             SPNOOP
};

static const acmd_callback_t ABI_SF[0x20] =
{
    SPNOOP,         ADPCM_NEAD,         CLEARBUFF,          SPNOOP,
    ADDMIXER,       RESAMPLE_flat,      RESAMPLE_ZOH,       SPNOOP,
    SETBUFF,        SPNOOP,             DMEMMOVE,           LOADADPCM_flat,
    MIXER,          INTERLEAVE_MK,      POLEF_flat,         SETLOOP_flat,
    COPYBLOCKS,     INTERL,             ENVSETUP1_NEAD,     ENVMIXER_NEAD,
    LOADBUFF_flat,  SAVEBUFF_flat,      ENVSETUP2,          SPNOOP,
    HILOGAIN,       UNKNOWN,            DUPLICATE,          SPNOOP,
    SPNOOP,         SPNOOP,             SPNOOP,             SPNOOP
};

static const acmd_callback_t ABI_SFJ[0x20] =
{
    SPNOOP,         ADPCM_NEAD,         CLEARBUFF,          SPNOOP,
    ADDMIXER,       RESAMPLE_flat,      RESAMPLE_ZOH,       SPNOOP,
    SETBUFF,        SPNOOP,             DMEMMOVE,           LOADADPCM_flat,
    MIXER,          INTERLEAVE_MK,      POLEF_flat,         SETLOOP_flat,
    COPYBLOCKS,     INTERL,             ENVSETUP1_NEAD,     ENVMIXER_NEAD,
    LOADBUFF_flat,  SAVEBUFF_flat,      ENVSETUP2,          UNKNOWN,
    HILOGAIN,       UNKNOWN,            DUPLICATE,          SPNOOP,
    SPNOOP,         SPNOOP,             SPNOOP,             SPNOOP
};

static const acmd_callback_t ABI_FZ[0x20] =
{
    UNKNOWN,        ADPCM_NEAD,         CLEARBUFF,          SPNOOP,
    ADDMIXER,       RESAMPLE_flat,      SPNOOP,             SPNOOP,
    SETBUFF,        SPNOOP,             DMEMMOVE,           LOADADPCM_flat,
    MIXER,          INTERLEAVE_NEAD,    SPNOOP,             SETLOOP_flat,
    COPYBLOCKS,     INTERL,             ENVSETUP1_NEAD,     ENVMIXER_NEAD,
    LOADBUFF_flat,  SAVEBUFF_flat,      ENVSETUP2,          UNKNOWN,
    SPNOOP,         UNKNOWN,            DUPLICATE,          SPNOOP,
    SPNOOP,         SPNOOP,             SPNOOP,             SPNOOP
};

static const acmd_callback_t ABI_WRJB[0x20] =
{
    SPNOOP,         ADPCM_NEAD,         CLEARBUFF,          UNKNOWN,
    ADDMIXER,       RESAMPLE_flat,      RESAMPLE_ZOH,       SPNOOP,
    SETBUFF,        SPNOOP,             DMEMMOVE,           LOADADPCM_flat,
    MIXER,          INTERLEAVE_NEAD,    SPNOOP,             SETLOOP_flat,
    COPYBLOCKS,     INTERL,             ENVSETUP1_NEAD,     ENVMIXER_NEAD,
    LOADBUFF_flat,  SAVEBUFF_flat,      ENVSETUP2,          UNKNOWN,
    HILOGAIN,       UNKNOWN,            DUPLICATE,          FILTER,
    SPNOOP,         SPNOOP,             SPNOOP,             SPNOOP
};

static const acmd_callback_t ABI_YS[0x18] =
{
    UNKNOWN,        ADPCM_NEAD,         CLEARBUFF,          UNKNOWN,
    ADDMIXER,       RESAMPLE_flat,      RESAMPLE_ZOH,       FILTER,
    SETBUFF,        DUPLICATE,          DMEMMOVE,           LOADADPCM_flat,
    MIXER,          INTERLEAVE_NEAD,    HILOGAIN,           SETLOOP_flat,
    COPYBLOCKS,     INTERL,             ENVSETUP1_NEAD,     ENVMIXER_NEAD,
    LOADBUFF_flat,  SAVEBUFF_flat,      ENVSETUP2,          UNKNOWN
};

static const acmd_callback_t ABI_1080[0x18] =
{
    UNKNOWN,        ADPCM_NEAD,         CLEARBUFF,          UNKNOWN,
    ADDMIXER,       RESAMPLE_flat,      RESAMPLE_ZOH,       FILTER,
    SETBUFF,        DUPLICATE,          DMEMMOVE,           LOADADPCM_flat,
    MIXER,          INTERLEAVE_NEAD,    HILOGAIN,           SETLOOP_flat,
    COPYBLOCKS,     INTERL,             ENVSETUP1_NEAD,     ENVMIXER_NEAD,
    LOADBUFF_flat,  SAVEBUFF_flat,      ENVSETUP2,          UNKNOWN
};

static const acmd_callback_t ABI_OOT[0x18] =
{
    UNKNOWN,        ADPCM_NEAD,         CLEARBUFF,          UNKNOWN,
    ADDMIXER,       RESAMPLE_flat,      RESAMPLE_ZOH,       FILTER,
    SETBUFF,        DUPLICATE,          DMEMMOVE,           LOADADPCM_flat,
    MIXER,          INTERLEAVE_NEAD,    HILOGAIN,           SETLOOP_flat,
    COPYBLOCKS,     INTERL,             ENVSETUP1_NEAD,     ENVMIXER_NEAD,
    LOADBUFF_flat,  SAVEBUFF_flat,      ENVSETUP2,          UNKNOWN
};

static const acmd_callback_t ABI_MM[0x18] =
{
    UNKNOWN,        ADPCM_NEAD,         CLEARBUFF,          SPNOOP,
    ADDMIXER,       RESAMPLE_flat,      RESAMPLE_ZOH,       FILTER,
    SETBUFF,        DUPLICATE,          DMEMMOVE,           LOADADPCM_flat,
    MIXER,          INTERLEAVE_NEAD,    HILOGAIN,           SETLOOP_flat,
    COPYBLOCKS,     INTERL,             ENVSETUP1_NEAD,     ENVMIXER_NEAD,
    LOADBUFF_flat,  SAVEBUFF_flat,      ENVSETUP2,          UNKNOWN
};

static const acmd_callback_t ABI_MMB[0x18] =
{
    SPNOOP,         ADPCM_NEAD,         CLEARBUFF,          SPNOOP,
    ADDMIXER,       RESAMPLE_flat,      RESAMPLE_ZOH,       FILTER,
    SETBUFF,        DUPLICATE,          DMEMMOVE,           LOADADPCM_flat,
    MIXER,          INTERLEAVE_NEAD,    HILOGAIN,           SETLOOP_flat,
    COPYBLOCKS,     INTERL,             ENVSETUP1_NEAD,     ENVMIXER_NEAD,
    LOADBUFF_flat,  SAVEBUFF_flat,      ENVSETUP2,          UNKNOWN
};


static const acmd_callback_t ABI_AC[0x18] =
{
    UNKNOWN,        ADPCM_NEAD,         CLEARBUFF,          SPNOOP,
    ADDMIXER,       RESAMPLE_flat,      RESAMPLE_ZOH,       FILTER,
    SETBUFF,        DUPLICATE,          DMEMMOVE,           LOADADPCM_flat,
    MIXER,          INTERLEAVE_NEAD,    HILOGAIN,           SETLOOP_flat,
    COPYBLOCKS,     INTERL,             ENVSETUP1_NEAD,     ENVMIXER_NEAD,
    LOADBUFF_flat,  SAVEBUFF_flat,      ENVSETUP2,          UNKNOWN
};

/* global functions */
void alist_process_mk()
{
    clear_segments();
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

