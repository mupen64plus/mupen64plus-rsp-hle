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
#include <string.h>
#include "hle.h"

#include "adpcm.h"
#include "mp3.h"
#include "resample.h"

#define N_SEGMENTS 16

#define NAUDIO_SUBFRAME_SIZE 0x170  /* ie 184 samples */
#define NAUDIO_MAIN     0x4f0
#define NAUDIO_MAIN2    0x660
#define NAUDIO_DRY_LEFT     0x9d0
#define NAUDIO_DRY_RIGHT    0xb40
#define NAUDIO_WET_LEFT     0xcb0
#define NAUDIO_WET_RIGHT    0xe20

/* types defintions */
typedef void (*acmd_callback_t)(u32 w1, u32 w2);

/* local variables */
static struct audio_t
{
    // segments
    u32 segments[N_SEGMENTS]; // 0x320

    // main buffers
    u16 in;             // 0x0000(t8)
    u16 out;            // 0x0002(t8)
    u16 count;          // 0x0004(t8)

    // auxiliary buffers
    u16 aux_dry_right;  // 0x000a(t8)
    u16 aux_wet_left;   // 0x000c(t8)
    u16 aux_wet_right;  // 0x000e(t8)

    // envmixer gains
    s16 dry;            // 0x001c(t8)
    s16 wet;            // 0x001e(t8)

    // envmixer envelopes (0: left, 1: right)
    s16 env_vol[2];
    s16 env_target[2];
    s32 env_rate[2];

    // adpcm
    u32 adpcm_loop;     // 0x0010(t8)
    u16 adpcm_codebook[0x80];
} l_audio;

static struct naudio_t
{
    // envmixer gains
    s16 dry;
    s16 wet;

    // envmixer envelopes (0: left, 1: right)
    s16 env_vol[2];
    s16 env_target[2];
    s32 env_rate[2];

    // adpcm
    u32 adpcm_loop;
    u16 adpcm_codebook[0x80];
} l_naudio;

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

    //envmixer2 envelopes (0: dry left, 1: dry right, 2: wet)
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

    //envmixer2 envelopes (0: dry left, 1: dry right, 2: wet)
    u16 env_value[3];
    u16 env_step[3];
} l_audio2;

struct ramp_t
{
    s32 value;
    s32 step;
    s32 target;
};

/* update ramp to its next value.
 * returns true if target has been reached, false otherwise */
static int ramp_next(struct ramp_t *ramp)
{
    ramp->value += ramp->step;

    return (ramp->step >= 0)
        ? (ramp->value > ramp->target)
        : (ramp->value < ramp->target);
}

static void envmix_exp_next_ramp(struct ramp_t *ramp, s32 *start, s32 *end, s32 rate)
{
    if (*start != ramp->target)
    {
        ramp->value = *start;
        ramp->step  = (*end - *start) >> 3;

        *start = (s32)(((s64)*start * (s64)rate) >> 16);
        *end   = (s32)(((s64)*end   * (s64)rate) >> 16);
    }
    else
    {
        ramp->value = ramp->target;
        ramp->step  = 0;
    }
}

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

/* local functions */
static void swap(s16 **a, s16 **b)
{
    s16* tmp = *b;
    *b = *a;
    *a = tmp;
}

static unsigned align(unsigned x, unsigned m)
{
    --m;
    return (x + m) & (~m);
}

static u32 parse(u32 value, unsigned offset, unsigned width)
{
    return (value >> offset) & ((1 << width) - 1);
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
    u32 w1, w2;
    unsigned int acmd;
    const OSTask_t * const task = get_task();

    const unsigned int *alist = (unsigned int*)(rsp.RDRAM + task->data_ptr);
    const unsigned int * const alist_end = alist + (task->data_size >> 2);

    while (alist != alist_end)
    {
        w1 = *(alist++);
        w2 = *(alist++);

        acmd = parse(w1, 24, 8);

        if (acmd < abi_size)
        {
            (*abi[acmd])(w1, w2);
        }
        else
        {
            DebugMessage(M64MSG_WARNING, "Invalid ABI command %u", acmd);
        }
    }
}


/* segment / offset related functions */
static u32 segoffset_load(u32 so, const u32* const segments, size_t n)
{
    u8 segment = parse(so, 24,  8);
    u32 offset = parse(so,  0, 24);

    if (segment < n)
    {
        return segments[segment] + offset;
    }
    else
    {
        DebugMessage(M64MSG_WARNING, "Invalid segment %u", segment);
        return offset;
    }
}

static void segoffset_store(u32 so, u32* const segments, size_t n)
{
    u8 segment = parse(so, 24,  8);
    u32 offset = parse(so,  0, 24);

    if (segment < n)
    {
        segments[segment] = offset;
    }
    else
    {
        DebugMessage(M64MSG_WARNING, "Invalid segment %u", segment);
    }
}

static void dmem_move(u16 dmemo, u16 dmemi, u16 count)
{
    while (count != 0)
    {
        rsp.DMEM[(dmemo++)^S8] = rsp.DMEM[(dmemi++)^S8];
        --count;
    }
}

static void mix_buffers(u16 dmemo, u16 dmemi, u16 count, s16 gain)
{
    s16 *dst = (s16*)(rsp.DMEM + dmemo);
    s16 *src = (s16*)(rsp.DMEM + dmemi);

    while (count != 0)
    {
        sadd(dst++, dmul(*src++, gain));
        --count;
    }
}

static void interleave_buffers(u16 dmemo, u16 left, u16 right, u16 count)
{
    u16 l1, l2, r1, r2;

    count >>= 1;

    u16 *dst  = (u16*)(rsp.DMEM + dmemo);
    u16 *srcL = (u16*)(rsp.DMEM + left);
    u16 *srcR = (u16*)(rsp.DMEM + right);

    while (count != 0)
    {
        l1 = *(srcL++);
        l2 = *(srcL++);
        r1 = *(srcR++);
        r2 = *(srcR++);

#ifdef M64P_BIG_ENDIAN
        *(dst++) = l1;
        *(dst++) = r1;
        *(dst++) = l2;
        *(dst++) = r2;
#else
        *(dst++) = r2;
        *(dst++) = l2;
        *(dst++) = r1;
        *(dst++) = l1;
#endif
        --count;
    }
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

static void SEGMENT(u32 w1, u32 w2)
{
    segoffset_store(w2, l_audio.segments, N_SEGMENTS);
}

static void POLEF(u32 w1, u32 w2)
{
    // TODO
}

static void CLEARBUFF(u32 w1, u32 w2)
{
    u16 dmem  = parse(w1, 0, 16);
    u16 count = parse(w2, 0, 16);
    
    memset(rsp.DMEM + (dmem & ~3), 0, align(count, 4));
}

static void ENVMIXER(u32 w1, u32 w2)
{
    u8  flags   = parse(w1, 16,  8);
    u32 address = segoffset_load(w2, l_audio.segments, N_SEGMENTS);

    int x,y;
    s16 state_buffer[40];

    s16 *in = (s16*)(rsp.DMEM + l_audio.in);
    s16 *dl = (s16*)(rsp.DMEM + l_audio.out);
    s16 *dr = (s16*)(rsp.DMEM + l_audio.aux_dry_right);
    s16 *wl = (s16*)(rsp.DMEM + l_audio.aux_wet_left);
    s16 *wr = (s16*)(rsp.DMEM + l_audio.aux_wet_right);

    unsigned flag_aux = ((flags & A_AUX) != 0);

    struct ramp_t ramps[2];
    s32 rates[2];
    s16 value, envL, envR;

    s16 dry, wet;
    u32 ptr = 0;
    s32 LAdderStart, RAdderStart, LAdderEnd, RAdderEnd;

    if (flags & A_INIT)
    {
        wet             = l_audio.wet;
        dry             = l_audio.dry;
        ramps[0].target = l_audio.env_target[0] << 16;
        ramps[1].target = l_audio.env_target[1] << 16;
        rates[0]        = l_audio.env_rate[0];
        rates[1]        = l_audio.env_rate[1];
        LAdderEnd       = l_audio.env_vol[0] * l_audio.env_rate[0];
        RAdderEnd       = l_audio.env_vol[1] * l_audio.env_rate[1];
        LAdderStart     = l_audio.env_vol[0] << 16;
        RAdderStart     = l_audio.env_vol[1] << 16;
    }
    else
    {
        memcpy((u8 *)state_buffer, (rsp.RDRAM+address), 80);

        wet             = *(s16*)(state_buffer + 0);
        dry             = *(s16*)(state_buffer + 2);
        ramps[0].target = *(s32*)(state_buffer + 4);
        ramps[1].target = *(s32*)(state_buffer + 6);
        rates[0]        = *(s32*)(state_buffer + 8);
        rates[1]        = *(s32*)(state_buffer + 10);
        LAdderEnd       = *(s32*)(state_buffer + 12);
        RAdderEnd       = *(s32*)(state_buffer + 14);
        LAdderStart     = *(s32*)(state_buffer + 16);
        RAdderStart     = *(s32*)(state_buffer + 18);
    }

    for (y = 0; y < l_audio.count; y += 0x10)
    {
        envmix_exp_next_ramp(&ramps[0], &LAdderStart, &LAdderEnd, rates[0]);
        envmix_exp_next_ramp(&ramps[1], &RAdderStart, &RAdderEnd, rates[1]);

        for (x = 0; x < 8; ++x)
        {
            if (ramp_next(&ramps[0]))
            {
                ramps[0].value = ramps[0].target;
                LAdderStart = ramps[0].target;
            }
            if (ramp_next(&ramps[1]))
            {
                ramps[1].value = ramps[1].target;
                RAdderStart = ramps[1].target;
            }

            value = in[ptr^S];
            envL = ramps[0].value >> 16;
            envR = ramps[1].value >> 16;

            sadd(&dl[ptr^S], dmul_round(value, dmul_round(dry, envL)));
            sadd(&dr[ptr^S], dmul_round(value, dmul_round(dry, envR)));
            if (flag_aux)
            {
                sadd(&wl[ptr^S], dmul_round(value, dmul_round(wet, envL)));
                sadd(&wr[ptr^S], dmul_round(value, dmul_round(wet, envR)));
            }
            ++ptr;
        }
    }

    *(s16 *)(state_buffer +  0) = wet;
    *(s16 *)(state_buffer +  2) = dry;
    *(s32 *)(state_buffer +  4) = ramps[0].target;
    *(s32 *)(state_buffer +  6) = ramps[1].target;
    *(s32 *)(state_buffer +  8) = rates[0];
    *(s32 *)(state_buffer + 10) = rates[1];
    *(s32 *)(state_buffer + 12) = LAdderEnd;
    *(s32 *)(state_buffer + 14) = RAdderEnd;
    *(s32 *)(state_buffer + 16) = LAdderStart;
    *(s32 *)(state_buffer + 18) = RAdderStart;
    memcpy(rsp.RDRAM+address, (u8*)state_buffer, 80);
}

static void RESAMPLE(u32 w1, u32 w2)
{
    u8  flags   = parse(w1, 16,  8);
    u16 pitch   = parse(w1,  0, 16);
    u32 address = segoffset_load(w2, l_audio.segments, N_SEGMENTS);

    resample_buffer(
            flags & A_INIT,
            address,
            (u32)pitch << 1,
            l_audio.in >> 1,
            l_audio.out >> 1,
            align(l_audio.count, 16) >> 1);
}

static void SETVOL(u32 w1, u32 w2)
{
    u8 flags = parse(w1, 16, 8);

    if (flags & A_AUX)
    {
        l_audio.dry = (s16)parse(w1, 0, 16);
        l_audio.wet = (s16)parse(w2, 0, 16);
        return;
    }

    if (flags & A_VOL)
    {
        if (flags & A_LEFT)
        {
            l_audio.env_vol[0] = (s16)parse(w1, 0, 16);
        }
        else
        {
            l_audio.env_vol[1] = (s16)parse(w1, 0, 16);
        }
        return;
    }

    if (flags & A_LEFT)
    {
        l_audio.env_target[0] = (s16)parse(w1, 0, 16);
        l_audio.env_rate[0]   = (s32)w2;
    }
    else
    { // A_RIGHT
        l_audio.env_target[1] = (s16)parse(w1, 0, 16);
        l_audio.env_rate[1]   = (s32)w2;
    }
}

static void SETLOOP(u32 w1, u32 w2)
{
    l_audio.adpcm_loop = segoffset_load(w2, l_audio.segments, N_SEGMENTS);
}

static void ADPCM(u32 w1, u32 w2)
{
    u8  flags   = parse(w1, 16,  8);
    u32 address = segoffset_load(w2, l_audio.segments, N_SEGMENTS);
   
    adpcm_decode(
            flags & A_INIT,
            flags & A_LOOP,
            0, // not supported in this ucode version
            (s16*)l_audio.adpcm_codebook,
            l_audio.adpcm_loop,
            address,
            l_audio.in,
            l_audio.out,
            align(l_audio.count, 32) >> 5);
}

static void LOADBUFF(u32 w1, u32 w2)
{
    if (l_audio.count == 0) { return; }

    u32 address = segoffset_load(w2, l_audio.segments, N_SEGMENTS);

    dma_read_fast(l_audio.in & 0xff8, address & ~7, l_audio.count - 1);
}

static void SAVEBUFF(u32 w1, u32 w2)
{
    if (l_audio.count == 0) { return; }
    
    u32 address = segoffset_load(w2, l_audio.segments, N_SEGMENTS);

    dma_write_fast(address & ~7, l_audio.out & 0xff8, l_audio.count - 1);
}

static void SETBUFF(u32 w1, u32 w2)
{
    u8 flags = parse(w1, 16, 8);

    if (flags & A_AUX)
    {
        l_audio.aux_dry_right = parse(w1,  0, 16);
        l_audio.aux_wet_left  = parse(w2, 16, 16);
        l_audio.aux_wet_right = parse(w2,  0, 16);
    }
    else
    {
        l_audio.in    = parse(w1,  0, 16);
        l_audio.out   = parse(w2, 16, 16);
        l_audio.count = parse(w2,  0, 16);
    }
}

static void DMEMMOVE(u32 w1, u32 w2)
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

static void LOADADPCM(u32 w1, u32 w2)
{
    u16 count   = parse(w1, 0, 16);
    u32 address = segoffset_load(w2, l_audio.segments, N_SEGMENTS);

    adpcm_load_codebook(
            l_audio.adpcm_codebook,
            address,
            count);
}

static void INTERLEAVE(u32 w1, u32 w2)
{
    u16 left  = parse(w2, 16, 16);
    u16 right = parse(w2,  0, 16);

    interleave_buffers(
            l_audio.out,
            left,
            right,
            l_audio.count >> 1);
}

static void MIXER(u32 w1, u32 w2)
{
    u16 gain  = parse(w1,  0, 16);
    u16 dmemi = parse(w2, 16, 16);
    u16 dmemo = parse(w2,  0, 16);

    mix_buffers(
            dmemo,
            dmemi,
            l_audio.count >> 1,
            (s16)gain);
}






static void SETVOL3(u32 w1, u32 w2)
{
    u8 flags = parse(w1, 16, 8);

    if (flags & 0x4)
    {
        if (flags & 0x2)
        {
            l_naudio.env_vol[0]  = (s16)parse(w1,  0, 16); // 0x50
            l_naudio.dry         = (s16)parse(w2, 16, 16); // 0x4c
            l_naudio.wet         = (s16)parse(w2,  0, 16); // 0x4e
        }
        else
        {
            l_naudio.env_target[1] = (s16)parse(w1, 0, 16); // 0x46
            l_naudio.env_rate[1]   = (s32)w2;               // 0x48/0x4A
        }
    }
    else
    {
        l_naudio.env_target[0] = (s16)parse(w1, 0, 16); // 0x40
        l_naudio.env_rate[0]   = (s32)w2;               // 0x42/0x44
    }
}

static void ENVMIXER3(u32 w1, u32 w2)
{
    u8  flags   = parse(w1, 16,  8);
    u32 address = parse(w2,  0, 24);

    int y;
    s16 state_buffer[40];

    s16 *in = (s16*)(rsp.DMEM + NAUDIO_MAIN);
    s16 *dl = (s16*)(rsp.DMEM + NAUDIO_DRY_LEFT);
    s16 *dr = (s16*)(rsp.DMEM + NAUDIO_DRY_RIGHT);
    s16 *wl = (s16*)(rsp.DMEM + NAUDIO_WET_LEFT);
    s16 *wr = (s16*)(rsp.DMEM + NAUDIO_WET_RIGHT);

    s16 envL, envR, value;

    /* 0 -> Left, 1->Right */
    struct ramp_t ramps[2];
    s16 dry, wet;

    l_naudio.env_vol[1] = (s16)w1;

    if (flags & A_INIT)
    {
        ramps[0].step   = l_naudio.env_rate[0] >> 3;
        ramps[0].value  = (s32)l_naudio.env_vol[0] << 16;
        ramps[0].target = (s32)l_naudio.env_target[0] << 16;

        ramps[1].step   = l_naudio.env_rate[1] >> 3;
        ramps[1].value  = (s32)l_naudio.env_vol[1] << 16;
        ramps[1].target = (s32)l_naudio.env_target[1] << 16;

        wet = (s16)l_naudio.wet;
        dry = (s16)l_naudio.dry;
    }
    else
    {
        memcpy((u8 *)state_buffer, rsp.RDRAM+address, 80);

        wet             = *(s16 *)(state_buffer +  0); // 0-1
        dry             = *(s16 *)(state_buffer +  2); // 2-3
        ramps[0].target = (s32)*(s16 *)(state_buffer +  4) << 16; // 4-5
        ramps[1].target = (s32)*(s16 *)(state_buffer +  6) << 16; // 6-7
        ramps[0].step   = *(s32 *)(state_buffer +  8); // 8-9 (state_buffer is a 16bit pointer)
        ramps[1].step   = *(s32 *)(state_buffer + 10); // 10-11
//        0   = *(s32 *)(state_buffer + 12); // 12-13
//        0   = *(s32 *)(state_buffer + 14); // 14-15
        ramps[0].value  = *(s32 *)(state_buffer + 16); // 16-17
        ramps[1].value  = *(s32 *)(state_buffer + 18); // 18-19
    }

    for (y = 0; y < (NAUDIO_SUBFRAME_SIZE/2); ++y)
    {
        if (ramp_next(&ramps[0])) { ramps[0].value = ramps[0].target; }
        if (ramp_next(&ramps[1])) { ramps[1].value = ramps[1].target; }

        value = in[y^S];
        envL = ramps[0].value >> 16;
        envR = ramps[1].value >> 16;

        sadd(&dl[y^S], dmul_round(value, dmul_round(dry, envL)));
        sadd(&dr[y^S], dmul_round(value, dmul_round(dry, envR)));
        sadd(&wl[y^S], dmul_round(value, dmul_round(wet, envL)));
        sadd(&wr[y^S], dmul_round(value, dmul_round(wet, envR)));
    }

    *(s16 *)(state_buffer +  0) = wet; // 0-1
    *(s16 *)(state_buffer +  2) = dry; // 2-3
    *(s16 *)(state_buffer +  4) = ramps[0].target >> 16; // 4-5
    *(s16 *)(state_buffer +  6) = ramps[1].target >> 16; // 6-7
    *(s32 *)(state_buffer +  8) = ramps[0].step; // 8-9 (state_buffer is a 16bit pointer)
    *(s32 *)(state_buffer + 10) = ramps[1].step; // 10-11
    *(s32 *)(state_buffer + 12) = 0; // 12-13
    *(s32 *)(state_buffer + 14) = 0; // 14-15
    *(s32 *)(state_buffer + 16) = ramps[0].value; // 16-17
    *(s32 *)(state_buffer + 18) = ramps[1].value; // 18-19
    memcpy(rsp.RDRAM+address, (u8 *)state_buffer,80);
}

static void CLEARBUFF3(u32 w1, u32 w2)
{
    u16 dmem  = parse(w1, 0, 16);
    u16 count = parse(w2, 0, 16);

    memset(rsp.DMEM + NAUDIO_MAIN + dmem, 0, count);
}

static void MIXER3(u32 w1, u32 w2)
{
    u16 gain  = parse(w1,  0, 16);
    u16 dmemi = parse(w2, 16, 16);
    u16 dmemo = parse(w2,  0, 16);

    mix_buffers(
            NAUDIO_MAIN + dmemo,
            NAUDIO_MAIN + dmemi,
            NAUDIO_SUBFRAME_SIZE >> 1,
            (s16)gain);
}

static void LOADBUFF3(u32 w1, u32 w2)
{
    u16 length  = parse(w1, 12, 12);
    u16 dmem    = parse(w1,  0, 12);
    u32 address = parse(w2,  0, 24);

    if (length == 0) { return; }

    dma_read_fast((NAUDIO_MAIN + dmem) & 0xff8, address & ~7, length - 1);
}

static void SAVEBUFF3(u32 w1, u32 w2)
{
    u16 length  = parse(w1, 12, 12);
    u16 dmem    = parse(w1,  0, 12);
    u32 address = parse(w2,  0, 24);

    if (length == 0) { return; }

    dma_write_fast(address & ~7, (NAUDIO_MAIN + dmem) & 0xff8, length - 1);
}

static void LOADADPCM3(u32 w1, u32 w2)
{
    u16 count   = parse(w1, 0, 16);
    u32 address = parse(w2, 0, 24);

    adpcm_load_codebook(
            l_naudio.adpcm_codebook,
            address,
            count);
}

static void DMEMMOVE3(u32 w1, u32 w2)
{
    u16 dmemi = parse(w1,  0, 16);
    u16 dmemo = parse(w2, 16, 16);
    u16 count = parse(w2,  0, 16);

    dmem_move(
            NAUDIO_MAIN + dmemo,
            NAUDIO_MAIN + dmemi,
            align(count, 4));
}

static void SETLOOP3(u32 w1, u32 w2)
{
    u32 address = parse(w2, 0, 24);

    l_naudio.adpcm_loop = address;
}

static void ADPCM3(u32 w1, u32 w2)
{
    u32 address = parse(w1,  0, 24);
    u8  flags   = parse(w2, 28,  4);
    u16 count   = parse(w2, 16, 12);
    u16 dmemi   = parse(w2, 12,  4);
    u16 dmemo   = parse(w2,  0, 12);

    adpcm_decode(
            flags & A_INIT,
            flags & A_LOOP,
            0, // not supported in this ucode version
            (s16*)l_naudio.adpcm_codebook,
            l_naudio.adpcm_loop,
            address,
            NAUDIO_MAIN + dmemi,
            NAUDIO_MAIN + dmemo,
            align(count, 32) >> 5);
}

static void RESAMPLE3(u32 w1, u32 w2)
{
    u32 address = parse(w1,  0, 24);
    u8  flags   = parse(w2, 30,  2);
    u16 pitch   = parse(w2, 14, 16);
    u16 dmemi   = parse(w2,  2, 12);
    u16 dmemo   = parse(w2,  0,  2);

    resample_buffer(
            flags & A_INIT,
            address,
            (u32)pitch << 1,
            (dmemi + NAUDIO_MAIN) >> 1,
            ((dmemo) ? NAUDIO_MAIN2 : NAUDIO_MAIN) >> 1,
            NAUDIO_SUBFRAME_SIZE >> 1);
}

static void INTERLEAVE3(u32 w1, u32 w2)
{
    interleave_buffers(
            NAUDIO_MAIN,
            NAUDIO_DRY_LEFT,
            NAUDIO_DRY_RIGHT,
            NAUDIO_SUBFRAME_SIZE >> 1);
}

static void MP3ADDY(u32 w1, u32 w2)
{
    /* do nothing ? */
}

static void MP3(u32 w1, u32 w2)
{
    u8  index   = parse(w1,  1,  4);
    u32 address = parse(w2,  0, 24);

    mp3_decode(address, index);
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

static void INTERLEAVE2(u32 w1, u32 w2)
{
    u16 count = parse(w1, 16,  8) << 4;
    u16 left  = parse(w2, 16, 16);
    u16 right = parse(w2,  0, 16);
    
    u16 out;
    if (count == 0)
    {
        out = l_audio2.out;
        count = l_audio2.count;
    }
    else
    {
        out = parse(w1, 0, 16);
    }

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
    // TODO
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
static const acmd_callback_t ABI1[0x10] =
{
    SPNOOP,     ADPCM,      CLEARBUFF,  ENVMIXER,
    LOADBUFF,   RESAMPLE,   SAVEBUFF,   SEGMENT,
    SETBUFF,    SETVOL,     DMEMMOVE,   LOADADPCM,
    MIXER,      INTERLEAVE, POLEF,      SETLOOP
};

static const acmd_callback_t ABI3[0x10] = 
{
    UNKNOWN,    ADPCM3,         CLEARBUFF3, ENVMIXER3,
    LOADBUFF3,  RESAMPLE3,      SAVEBUFF3,  MP3,
    MP3ADDY,    SETVOL3,        DMEMMOVE3,  LOADADPCM3,
    MIXER3,     INTERLEAVE3,    UNKNOWN,    SETLOOP3
};

static const acmd_callback_t ABI_MK[0x20] =
{
    SPNOOP,     ADPCM_MK,       CLEARBUFF2,     SPNOOP,
    SPNOOP,     RESAMPLE_MK,    SPNOOP,         SEGMENT_MK,
    SETBUFF_MK, SPNOOP,         DMEMMOVE2,      LOADADPCM_MK,
    MIXER2,     INTERLEAVE_MK,  POLEF2,         SETLOOP_MK,
    COPYBLOCKS2,INTERL2,        ENVSETUP1_MK,   ENVMIXER_MK,
    LOADBUFF_MK,SAVEBUFF_MK,    ENVSETUP2_MK,   SPNOOP,
    SPNOOP,     SPNOOP,         SPNOOP,         SPNOOP,
    SPNOOP,     SPNOOP,         SPNOOP,         SPNOOP
};

static const acmd_callback_t ABI_SF[0x20] =
{
    SPNOOP,     ADPCM2,         CLEARBUFF2, SPNOOP,
    ADDMIXER,   RESAMPLE2,      UNKNOWN,    SPNOOP,
    SETBUFF2,   SPNOOP,         DMEMMOVE2,  LOADADPCM2,
    MIXER2,     INTERLEAVE2,    POLEF2,     SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,  ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,  SPNOOP,
    HILOGAIN,   UNKNOWN,        DUPLICATE2, SPNOOP,
    SPNOOP,     SPNOOP,         SPNOOP,     SPNOOP
};

static const acmd_callback_t ABI_SFJ[0x20] =
{
    SPNOOP,     ADPCM2,         CLEARBUFF2, SPNOOP,
    ADDMIXER,   RESAMPLE2,      UNKNOWN,    SPNOOP,
    SETBUFF2,   SPNOOP,         DMEMMOVE2,  LOADADPCM2,
    MIXER2,     INTERLEAVE2,    POLEF2,     SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,  ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,  UNKNOWN,
    HILOGAIN,   UNKNOWN,        DUPLICATE2, SPNOOP,
    SPNOOP,     SPNOOP,         SPNOOP,     SPNOOP
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
    SPNOOP,     ADPCM2,         CLEARBUFF2, UNKNOWN,
    ADDMIXER,   RESAMPLE2,      UNKNOWN,    SPNOOP,
    SETBUFF2,   SPNOOP,         DMEMMOVE2,  LOADADPCM2,
    MIXER2,     INTERLEAVE2,    SPNOOP,     SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,  ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,  UNKNOWN,
    HILOGAIN,   UNKNOWN,        DUPLICATE2, FILTER2,
    SPNOOP,     SPNOOP,         SPNOOP,     SPNOOP
};

static const acmd_callback_t ABI_YS[0x18] =
{
    UNKNOWN,    ADPCM2,         CLEARBUFF2, UNKNOWN,
    ADDMIXER,   RESAMPLE2,      UNKNOWN,    FILTER2,
    SETBUFF2,   DUPLICATE2,     DMEMMOVE2,  LOADADPCM2,
    MIXER2,     INTERLEAVE2,    HILOGAIN,   SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,  ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,  UNKNOWN
};

static const acmd_callback_t ABI_1080[0x18] =
{
    UNKNOWN,    ADPCM2,         CLEARBUFF2, UNKNOWN,
    ADDMIXER,   RESAMPLE2,      UNKNOWN,    FILTER2,
    SETBUFF2,   DUPLICATE2,     DMEMMOVE2,  LOADADPCM2,
    MIXER2,     INTERLEAVE2,    HILOGAIN,   SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,  ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,  UNKNOWN
};

static const acmd_callback_t ABI_OOT[0x18] =
{
    UNKNOWN,    ADPCM2,         CLEARBUFF2, UNKNOWN,
    ADDMIXER,   RESAMPLE2,      UNKNOWN,    FILTER2,
    SETBUFF2,   DUPLICATE2,     DMEMMOVE2,  LOADADPCM2,
    MIXER2,     INTERLEAVE2,    HILOGAIN,   SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,  ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,  UNKNOWN
};

static const acmd_callback_t ABI_MM[0x18] =
{
    UNKNOWN,    ADPCM2,         CLEARBUFF2, SPNOOP,
    ADDMIXER,   RESAMPLE2,      UNKNOWN,    FILTER2,
    SETBUFF2,   DUPLICATE2,     DMEMMOVE2,  LOADADPCM2,
    MIXER2,     INTERLEAVE2,    HILOGAIN,   SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,  ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,  UNKNOWN
};

static const acmd_callback_t ABI_MMB[0x18] =
{
    SPNOOP,     ADPCM2,         CLEARBUFF2, SPNOOP,
    ADDMIXER,   RESAMPLE2,      UNKNOWN,    FILTER2,
    SETBUFF2,   DUPLICATE2,     DMEMMOVE2,  LOADADPCM2,
    MIXER2,     INTERLEAVE2,    HILOGAIN,   SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,  ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,  UNKNOWN
};


static const acmd_callback_t ABI_AC[0x18] =
{
    UNKNOWN,    ADPCM2,         CLEARBUFF2, SPNOOP,
    ADDMIXER,   RESAMPLE2,      UNKNOWN,    FILTER2,
    SETBUFF2,   DUPLICATE2,     DMEMMOVE2,  LOADADPCM2,
    MIXER2,     INTERLEAVE2,    HILOGAIN,   SETLOOP2,
    COPYBLOCKS2,INTERL2,        ENVSETUP1,  ENVMIXER2,
    LOADBUFF2,  SAVEBUFF2,      ENVSETUP2,  UNKNOWN
};

/* global functions */
void alist_process_ABI1()
{
    memset(l_audio.segments, 0, sizeof(l_audio.segments[0])*N_SEGMENTS);
    alist_process(ABI1, 0x10);
}

void alist_process_ABI3()
{
    alist_process(ABI3, 0x10);
}

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

