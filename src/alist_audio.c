/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - alist_audio.c                                   *
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

#include "arithmetic.h"
#include "alist.h"
#include "adpcm.h"
#include "resample.h"

#define N_SEGMENTS 16

/* local variables */
static struct alist_t
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
} l_alist;


/* local functions */
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

/* Audio commands */
static void SPNOOP(u32 w1, u32 w2)
{
}

static void SEGMENT(u32 w1, u32 w2)
{
    alist_segments_store(w2, l_alist.segments, N_SEGMENTS);
}

static void POLEF(u32 w1, u32 w2)
{
    if (l_alist.count == 0) { return; }

    u16 flags = alist_parse(w1, 16, 16);
    u16 gain  = alist_parse(w1,  0, 16);
    u32 address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    adpcm_polef(
            flags & A_INIT,
            gain,
            (s16*)l_alist.adpcm_codebook,
            address,
            l_alist.in,
            l_alist.out,
            align(l_alist.count, 16));
}

static void CLEARBUFF(u32 w1, u32 w2)
{
    u16 dmem  = alist_parse(w1, 0, 16);
    u16 count = alist_parse(w2, 0, 16);
   
    if (count == 0) { return; }

    memset(rsp.DMEM + (dmem & ~3), 0, align(count, 4));
}

static void ENVMIXER(u32 w1, u32 w2)
{
    u8  flags   = alist_parse(w1, 16,  8);
    u32 address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    int x,y;
    s16 state_buffer[40];

    s16 *in = (s16*)(rsp.DMEM + l_alist.in);
    s16 *dl = (s16*)(rsp.DMEM + l_alist.out);
    s16 *dr = (s16*)(rsp.DMEM + l_alist.aux_dry_right);
    s16 *wl = (s16*)(rsp.DMEM + l_alist.aux_wet_left);
    s16 *wr = (s16*)(rsp.DMEM + l_alist.aux_wet_right);

    unsigned flag_aux = ((flags & A_AUX) != 0);

    struct ramp_t ramps[2];
    s32 rates[2];
    s16 value, envL, envR;

    s16 dry, wet;
    u32 ptr = 0;
    s32 LAdderStart, RAdderStart, LAdderEnd, RAdderEnd;

    if (flags & A_INIT)
    {
        wet             = l_alist.wet;
        dry             = l_alist.dry;
        ramps[0].target = l_alist.env_target[0] << 16;
        ramps[1].target = l_alist.env_target[1] << 16;
        rates[0]        = l_alist.env_rate[0];
        rates[1]        = l_alist.env_rate[1];
        LAdderEnd       = l_alist.env_vol[0] * l_alist.env_rate[0];
        RAdderEnd       = l_alist.env_vol[1] * l_alist.env_rate[1];
        LAdderStart     = l_alist.env_vol[0] << 16;
        RAdderStart     = l_alist.env_vol[1] << 16;
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

    for (y = 0; y < l_alist.count; y += 0x10)
    {
        envmix_exp_next_ramp(&ramps[0], &LAdderStart, &LAdderEnd, rates[0]);
        envmix_exp_next_ramp(&ramps[1], &RAdderStart, &RAdderEnd, rates[1]);

        for (x = 0; x < 8; ++x)
        {
            if (ramp_next(&ramps[0])) { LAdderStart = ramps[0].value; }
            if (ramp_next(&ramps[1])) { RAdderStart = ramps[1].value; }

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
    u8  flags   = alist_parse(w1, 16,  8);
    u16 pitch   = alist_parse(w1,  0, 16);
    u32 address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    resample_buffer(
            flags & A_INIT,
            address,
            (u32)pitch << 1,
            l_alist.in >> 1,
            l_alist.out >> 1,
            align(l_alist.count, 16) >> 1);
}

static void SETVOL(u32 w1, u32 w2)
{
    u8 flags = alist_parse(w1, 16, 8);

    if (flags & A_AUX)
    {
        l_alist.dry = (s16)alist_parse(w1, 0, 16);
        l_alist.wet = (s16)alist_parse(w2, 0, 16);
    }
    else
    {
        unsigned lr = (flags & A_LEFT) ? 0 : 1;

        if (flags & A_VOL)
        {
            l_alist.env_vol[lr] = (s16)alist_parse(w1, 0, 16);
        }
        else
        {
            l_alist.env_target[lr] = (s16)alist_parse(w1, 0, 16);
            l_alist.env_rate[lr]   = (s32)w2;
        }
    }
}

static void SETLOOP(u32 w1, u32 w2)
{
    l_alist.adpcm_loop = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);
}

static void ADPCM(u32 w1, u32 w2)
{
    u8  flags   = alist_parse(w1, 16,  8);
    u32 address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);
   
    adpcm_decode(
            flags & A_INIT,
            flags & A_LOOP,
            0, // not supported in this ucode version
            (s16*)l_alist.adpcm_codebook,
            l_alist.adpcm_loop,
            address,
            l_alist.in,
            l_alist.out,
            align(l_alist.count, 32) >> 5);
}

static void LOADBUFF(u32 w1, u32 w2)
{
    if (l_alist.count == 0) { return; }

    u32 address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    dma_read_fast(l_alist.in & 0xff8, address & ~7, l_alist.count - 1);
}

static void SAVEBUFF(u32 w1, u32 w2)
{
    if (l_alist.count == 0) { return; }
    
    u32 address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    dma_write_fast(address & ~7, l_alist.out & 0xff8, l_alist.count - 1);
}

static void SETBUFF(u32 w1, u32 w2)
{
    u8 flags = alist_parse(w1, 16, 8);

    if (flags & A_AUX)
    {
        l_alist.aux_dry_right = alist_parse(w1,  0, 16);
        l_alist.aux_wet_left  = alist_parse(w2, 16, 16);
        l_alist.aux_wet_right = alist_parse(w2,  0, 16);
    }
    else
    {
        l_alist.in    = alist_parse(w1,  0, 16);
        l_alist.out   = alist_parse(w2, 16, 16);
        l_alist.count = alist_parse(w2,  0, 16);
    }
}

static void DMEMMOVE(u32 w1, u32 w2)
{
    u16 dmemi = alist_parse(w1,  0, 16);
    u16 dmemo = alist_parse(w2, 16, 16);
    u16 count = alist_parse(w2,  0, 16);

    if (count == 0) { return; }

    alist_dmemmove(
        dmemo,
        dmemi,
        align(count, 4));
}

static void LOADADPCM(u32 w1, u32 w2)
{
    u16 count   = alist_parse(w1, 0, 16);
    u32 address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    adpcm_load_codebook(
            l_alist.adpcm_codebook,
            address,
            count);
}

static void INTERLEAVE(u32 w1, u32 w2)
{
    if (l_alist.count == 0) { return; }

    u16 left  = alist_parse(w2, 16, 16);
    u16 right = alist_parse(w2,  0, 16);

    alist_interleave(
            l_alist.out,
            left,
            right,
            l_alist.count >> 1);
}

static void MIXER(u32 w1, u32 w2)
{
    if (l_alist.count == 0) { return; }

    u16 gain  = alist_parse(w1,  0, 16);
    u16 dmemi = alist_parse(w2, 16, 16);
    u16 dmemo = alist_parse(w2,  0, 16);

    alist_mix(
            dmemo,
            dmemi,
            l_alist.count >> 1,
            (s16)gain);
}

/* Audio Binary Interface tables */
static const acmd_callback_t ABI_AUDIO[0x10] =
{
    SPNOOP,     ADPCM,      CLEARBUFF,  ENVMIXER,
    LOADBUFF,   RESAMPLE,   SAVEBUFF,   SEGMENT,
    SETBUFF,    SETVOL,     DMEMMOVE,   LOADADPCM,
    MIXER,      INTERLEAVE, POLEF,      SETLOOP
};

static const acmd_callback_t ABI_AUDIO_GE[0x10] =
{
    SPNOOP,     ADPCM,      CLEARBUFF,  ENVMIXER,
    LOADBUFF,   RESAMPLE,   SAVEBUFF,   SEGMENT,
    SETBUFF,    SETVOL,     DMEMMOVE,   LOADADPCM,
    MIXER,      INTERLEAVE, POLEF,      SETLOOP
};

static const acmd_callback_t ABI_AUDIO_BC[0x10] =
{
    SPNOOP,     ADPCM,      CLEARBUFF,  ENVMIXER,
    LOADBUFF,   RESAMPLE,   SAVEBUFF,   SEGMENT,
    SETBUFF,    SETVOL,     DMEMMOVE,   LOADADPCM,
    MIXER,      INTERLEAVE, POLEF,      SETLOOP
};

/* global functions */
void alist_process_audio()
{
    memset(l_alist.segments, 0, sizeof(l_alist.segments[0])*N_SEGMENTS);
    alist_process(ABI_AUDIO, 0x10);
}

void alist_process_audio_ge()
{
    memset(l_alist.segments, 0, sizeof(l_alist.segments[0])*N_SEGMENTS);
    alist_process(ABI_AUDIO_GE, 0x10);
}

void alist_process_audio_bc()
{
    memset(l_alist.segments, 0, sizeof(l_alist.segments[0])*N_SEGMENTS);
    alist_process(ABI_AUDIO_BC, 0x10);
}

