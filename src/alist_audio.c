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
    // segments
    uint32_t segments[N_SEGMENTS]; // 0x320

    // main buffers
    uint16_t in;             // 0x0000(t8)
    uint16_t out;            // 0x0002(t8)
    uint16_t count;          // 0x0004(t8)

    // auxiliary buffers
    uint16_t aux_dry_right;  // 0x000a(t8)
    uint16_t aux_wet_left;   // 0x000c(t8)
    uint16_t aux_wet_right;  // 0x000e(t8)

    // envmixer gains
    int16_t dry;            // 0x001c(t8)
    int16_t wet;            // 0x001e(t8)

    // envmixer envelopes (0: left, 1: right)
    int16_t env_vol[2];
    int16_t env_target[2];
    int32_t env_rate[2];

    // dram address of adpcm frame before loop point
    uint32_t loop;     // 0x0010(t8)

    // storage for adpcm codebooks and polef coefficients
    uint16_t table[0x80];
} l_alist;


/* local functions */
static void envmix_exp_next_ramp(struct ramp_t *ramp, int32_t *start, int32_t *end, int32_t rate)
{
    if (*start != ramp->target)
    {
        ramp->value = *start;
        ramp->step  = (*end - *start) >> 3;

        *start = (int32_t)(((int64_t)*start * (int64_t)rate) >> 16);
        *end   = (int32_t)(((int64_t)*end   * (int64_t)rate) >> 16);
    }
    else
    {
        ramp->value = ramp->target;
        ramp->step  = 0;
    }
}

static void clear_segments()
{
    memset(l_alist.segments, 0, sizeof(l_alist.segments[0])*N_SEGMENTS);
}


/* Audio commands */
static void SPNOOP(uint32_t w1, uint32_t w2)
{
}

static void SEGMENT(uint32_t w1, uint32_t w2)
{
    alist_segments_store(w2, l_alist.segments, N_SEGMENTS);
}

static void POLEF(uint32_t w1, uint32_t w2)
{
    if (l_alist.count == 0) { return; }

    uint16_t flags   = alist_parse(w1, 16, 16);
    uint16_t gain    = alist_parse(w1,  0, 16);
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

static void CLEARBUFF(uint32_t w1, uint32_t w2)
{
    uint16_t dmem  = alist_parse(w1, 0, 16);
    uint16_t count = alist_parse(w2, 0, 16);
   
    if (count == 0) { return; }

    memset(rsp.DMEM + (dmem & ~3), 0, align(count, 4));
}

static void ENVMIXER(uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = alist_parse(w1, 16,  8);
    uint32_t address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    int x,y;
    int16_t state_buffer[40];

    int16_t *in = (int16_t*)(rsp.DMEM + l_alist.in);
    int16_t *dl = (int16_t*)(rsp.DMEM + l_alist.out);
    int16_t *dr = (int16_t*)(rsp.DMEM + l_alist.aux_dry_right);
    int16_t *wl = (int16_t*)(rsp.DMEM + l_alist.aux_wet_left);
    int16_t *wr = (int16_t*)(rsp.DMEM + l_alist.aux_wet_right);

    unsigned flag_aux = ((flags & A_AUX) != 0);

    struct ramp_t ramps[2];
    int32_t rates[2];
    int16_t value, envL, envR;

    int16_t dry, wet;
    uint32_t ptr = 0;
    int32_t LAdderStart, RAdderStart, LAdderEnd, RAdderEnd;

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
        memcpy((uint8_t *)state_buffer, (rsp.RDRAM+address), 80);

        wet             = *(int16_t*)(state_buffer + 0);
        dry             = *(int16_t*)(state_buffer + 2);
        ramps[0].target = *(int32_t*)(state_buffer + 4);
        ramps[1].target = *(int32_t*)(state_buffer + 6);
        rates[0]        = *(int32_t*)(state_buffer + 8);
        rates[1]        = *(int32_t*)(state_buffer + 10);
        LAdderEnd       = *(int32_t*)(state_buffer + 12);
        RAdderEnd       = *(int32_t*)(state_buffer + 14);
        LAdderStart     = *(int32_t*)(state_buffer + 16);
        RAdderStart     = *(int32_t*)(state_buffer + 18);
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

    *(int16_t *)(state_buffer +  0) = wet;
    *(int16_t *)(state_buffer +  2) = dry;
    *(int32_t *)(state_buffer +  4) = ramps[0].target;
    *(int32_t *)(state_buffer +  6) = ramps[1].target;
    *(int32_t *)(state_buffer +  8) = rates[0];
    *(int32_t *)(state_buffer + 10) = rates[1];
    *(int32_t *)(state_buffer + 12) = LAdderEnd;
    *(int32_t *)(state_buffer + 14) = RAdderEnd;
    *(int32_t *)(state_buffer + 16) = LAdderStart;
    *(int32_t *)(state_buffer + 18) = RAdderStart;
    memcpy(rsp.RDRAM+address, (uint8_t*)state_buffer, 80);
}

static void RESAMPLE(uint32_t w1, uint32_t w2)
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

static void SETVOL(uint32_t w1, uint32_t w2)
{
    uint8_t flags = alist_parse(w1, 16, 8);

    if (flags & A_AUX)
    {
        l_alist.dry = (int16_t)alist_parse(w1, 0, 16);
        l_alist.wet = (int16_t)alist_parse(w2, 0, 16);
    }
    else
    {
        unsigned lr = (flags & A_LEFT) ? 0 : 1;

        if (flags & A_VOL)
        {
            l_alist.env_vol[lr] = (int16_t)alist_parse(w1, 0, 16);
        }
        else
        {
            l_alist.env_target[lr] = (int16_t)alist_parse(w1, 0, 16);
            l_alist.env_rate[lr]   = (int32_t)w2;
        }
    }
}

static void SETLOOP(uint32_t w1, uint32_t w2)
{
    l_alist.loop = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);
}

static void ADPCM(uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = alist_parse(w1, 16,  8);
    uint32_t address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);
   
    adpcm_decode(
            flags & A_INIT,
            flags & A_LOOP,
            false, // not supported in this ucode version
            (int16_t*)l_alist.table,
            l_alist.loop,
            address,
            l_alist.in,
            l_alist.out,
            align(l_alist.count, 32) >> 5);
}

static void LOADBUFF(uint32_t w1, uint32_t w2)
{
    if (l_alist.count == 0) { return; }

    uint32_t address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    dma_read_fast(l_alist.in & 0xff8, address & ~7, l_alist.count - 1);
}

static void SAVEBUFF(uint32_t w1, uint32_t w2)
{
    if (l_alist.count == 0) { return; }
    
    uint32_t address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    dma_write_fast(address & ~7, l_alist.out & 0xff8, l_alist.count - 1);
}

static void SETBUFF(uint32_t w1, uint32_t w2)
{
    uint8_t flags = alist_parse(w1, 16, 8);

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

static void LOADADPCM(uint32_t w1, uint32_t w2)
{
    uint16_t length  = alist_parse(w1, 0, 16);
    uint32_t address = alist_segments_load(w2, l_alist.segments, N_SEGMENTS);

    dram_read_many_u16(l_alist.table, address, length);
}

static void INTERLEAVE(uint32_t w1, uint32_t w2)
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

static void MIXER(uint32_t w1, uint32_t w2)
{
    if (l_alist.count == 0) { return; }

    uint16_t gain  = alist_parse(w1,  0, 16);
    uint16_t dmemi = alist_parse(w2, 16, 16);
    uint16_t dmemo = alist_parse(w2,  0, 16);

    alist_mix(
            dmemo,
            dmemi,
            l_alist.count >> 1,
            (int16_t)gain);
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
    clear_segments();
    alist_process(ABI_AUDIO, 0x10);
}

void alist_process_audio_ge()
{
    clear_segments();
    alist_process(ABI_AUDIO_GE, 0x10);
}

void alist_process_audio_bc()
{
    clear_segments();
    alist_process(ABI_AUDIO_BC, 0x10);
}

