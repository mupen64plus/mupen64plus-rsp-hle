/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - alist_naudio.c                                  *
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
#include "mp3.h"
#include "resample.h"

#define NAUDIO_SUBFRAME_SIZE    0x170  /* ie 184 samples */
#define NAUDIO_MAIN             0x4f0
#define NAUDIO_MAIN2            0x660
#define NAUDIO_DRY_LEFT         0x9d0
#define NAUDIO_DRY_RIGHT        0xb40
#define NAUDIO_WET_LEFT         0xcb0
#define NAUDIO_WET_RIGHT        0xe20

/* local variables */
static struct alist_t
{
    // envmixer gains
    int16_t dry;
    int16_t wet;

    // envmixer envelopes (0: left, 1: right)
    int16_t env_vol[2];
    int16_t env_target[2];
    int32_t env_rate[2];

    // dram address of adpcm frame before loop point
    uint32_t loop;

    // storage for adpcm codebooks and polef coefficients
    uint16_t table[16 * 8];
} l_alist;

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

static void NAUDIO_0000(uint32_t w1, uint32_t w2)
{
    /* ??? */
    UNKNOWN(w1, w2);
}

static void NAUDIO_02B0(uint32_t w1, uint32_t w2)
{
    /* ??? */
    /* UNKNOWN(w1, w2); commented to avoid constant spamming during gameplay */
}

static void SETVOL(uint32_t w1, uint32_t w2)
{
    uint8_t flags = alist_parse(w1, 16, 8);

    if (flags & 0x4)
    {
        if (flags & 0x2)
        {
            l_alist.env_vol[0]  = (int16_t)alist_parse(w1,  0, 16); // 0x50
            l_alist.dry         = (int16_t)alist_parse(w2, 16, 16); // 0x4c
            l_alist.wet         = (int16_t)alist_parse(w2,  0, 16); // 0x4e
        }
        else
        {
            l_alist.env_target[1] = (int16_t)alist_parse(w1, 0, 16); // 0x46
            l_alist.env_rate[1]   = (int32_t)w2;               // 0x48/0x4A
        }
    }
    else
    {
        l_alist.env_target[0] = (int16_t)alist_parse(w1, 0, 16); // 0x40
        l_alist.env_rate[0]   = (int32_t)w2;               // 0x42/0x44
    }
}

static void ENVMIXER(uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = alist_parse(w1, 16,  8);
    uint32_t address = alist_parse(w2,  0, 24);

    int y;
    int16_t state_buffer[40];

    int16_t *in = (int16_t*)(rsp.DMEM + NAUDIO_MAIN);
    int16_t *dl = (int16_t*)(rsp.DMEM + NAUDIO_DRY_LEFT);
    int16_t *dr = (int16_t*)(rsp.DMEM + NAUDIO_DRY_RIGHT);
    int16_t *wl = (int16_t*)(rsp.DMEM + NAUDIO_WET_LEFT);
    int16_t *wr = (int16_t*)(rsp.DMEM + NAUDIO_WET_RIGHT);

    int16_t envL, envR, value;

    /* 0 -> Left, 1->Right */
    struct ramp_t ramps[2];
    int16_t dry, wet;

    l_alist.env_vol[1] = (int16_t)alist_parse(w1, 0, 16);

    if (flags & A_INIT)
    {
        ramps[0].step   = l_alist.env_rate[0] >> 3;
        ramps[0].value  = (int32_t)l_alist.env_vol[0] << 16;
        ramps[0].target = (int32_t)l_alist.env_target[0] << 16;

        ramps[1].step   = l_alist.env_rate[1] >> 3;
        ramps[1].value  = (int32_t)l_alist.env_vol[1] << 16;
        ramps[1].target = (int32_t)l_alist.env_target[1] << 16;

        wet = (int16_t)l_alist.wet;
        dry = (int16_t)l_alist.dry;
    }
    else
    {
        memcpy((uint8_t *)state_buffer, rsp.RDRAM+address, 80);

        wet             = *(int16_t *)(state_buffer +  0); // 0-1
        dry             = *(int16_t *)(state_buffer +  2); // 2-3
        ramps[0].target = (int32_t)*(int16_t *)(state_buffer +  4) << 16; // 4-5
        ramps[1].target = (int32_t)*(int16_t *)(state_buffer +  6) << 16; // 6-7
        ramps[0].step   = *(int32_t *)(state_buffer +  8); // 8-9 (state_buffer is a 16bit pointer)
        ramps[1].step   = *(int32_t *)(state_buffer + 10); // 10-11
//        0   = *(int32_t *)(state_buffer + 12); // 12-13
//        0   = *(int32_t *)(state_buffer + 14); // 14-15
        ramps[0].value  = *(int32_t *)(state_buffer + 16); // 16-17
        ramps[1].value  = *(int32_t *)(state_buffer + 18); // 18-19
    }

    for (y = 0; y < (NAUDIO_SUBFRAME_SIZE/2); ++y)
    {
        ramp_next(&ramps[0]);
        ramp_next(&ramps[1]);

        value = in[y^S];
        envL = ramps[0].value >> 16;
        envR = ramps[1].value >> 16;

        sadd(&dl[y^S], dmul_round(value, dmul_round(dry, envL)));
        sadd(&dr[y^S], dmul_round(value, dmul_round(dry, envR)));
        sadd(&wl[y^S], dmul_round(value, dmul_round(wet, envL)));
        sadd(&wr[y^S], dmul_round(value, dmul_round(wet, envR)));
    }

    *(int16_t *)(state_buffer +  0) = wet; // 0-1
    *(int16_t *)(state_buffer +  2) = dry; // 2-3
    *(int16_t *)(state_buffer +  4) = ramps[0].target >> 16; // 4-5
    *(int16_t *)(state_buffer +  6) = ramps[1].target >> 16; // 6-7
    *(int32_t *)(state_buffer +  8) = ramps[0].step; // 8-9 (state_buffer is a 16bit pointer)
    *(int32_t *)(state_buffer + 10) = ramps[1].step; // 10-11
    *(int32_t *)(state_buffer + 12) = 0; // 12-13
    *(int32_t *)(state_buffer + 14) = 0; // 14-15
    *(int32_t *)(state_buffer + 16) = ramps[0].value; // 16-17
    *(int32_t *)(state_buffer + 18) = ramps[1].value; // 18-19
    memcpy(rsp.RDRAM+address, (uint8_t *)state_buffer,80);
}

static void CLEARBUFF(uint32_t w1, uint32_t w2)
{
    uint16_t dmem  = alist_parse(w1, 0, 16);
    uint16_t count = alist_parse(w2, 0, 16);

    memset(rsp.DMEM + NAUDIO_MAIN + dmem, 0, count);
}

static void MIXER(uint32_t w1, uint32_t w2)
{
    uint16_t gain  = alist_parse(w1,  0, 16);
    uint16_t dmemi = alist_parse(w2, 16, 16);
    uint16_t dmemo = alist_parse(w2,  0, 16);

    alist_mix(
            NAUDIO_MAIN + dmemo,
            NAUDIO_MAIN + dmemi,
            NAUDIO_SUBFRAME_SIZE >> 1,
            (int16_t)gain);
}

static void LOADBUFF(uint32_t w1, uint32_t w2)
{
    uint16_t length  = alist_parse(w1, 12, 12);
    uint16_t dmem    = alist_parse(w1,  0, 12);
    uint32_t address = alist_parse(w2,  0, 24);

    if (length == 0) { return; }

    dma_read_fast((NAUDIO_MAIN + dmem) & 0xff8, address & ~7, length - 1);
}

static void SAVEBUFF(uint32_t w1, uint32_t w2)
{
    uint16_t length  = alist_parse(w1, 12, 12);
    uint16_t dmem    = alist_parse(w1,  0, 12);
    uint32_t address = alist_parse(w2,  0, 24);

    if (length == 0) { return; }

    dma_write_fast(address & ~7, (NAUDIO_MAIN + dmem) & 0xff8, length - 1);
}

static void LOADADPCM(uint32_t w1, uint32_t w2)
{
    uint16_t length  = alist_parse(w1, 0, 16);
    uint32_t address = alist_parse(w2, 0, 24);

    dram_read_many_u16(l_alist.table, address, length);
}

static void DMEMMOVE(uint32_t w1, uint32_t w2)
{
    uint16_t dmemi = alist_parse(w1,  0, 16);
    uint16_t dmemo = alist_parse(w2, 16, 16);
    uint16_t count = alist_parse(w2,  0, 16);

    alist_dmemmove(
            NAUDIO_MAIN + dmemo,
            NAUDIO_MAIN + dmemi,
            align(count, 4));
}

static void SETLOOP(uint32_t w1, uint32_t w2)
{
    l_alist.loop = alist_parse(w2, 0, 24);
}

static void NAUDIO_14(uint32_t w1, uint32_t w2)
{
    if (l_alist.table[0] == 0 && l_alist.table[1] == 0)
    {
        uint8_t  flags       = alist_parse(w1, 16,  8);
        uint16_t gain        = alist_parse(w1,  0, 16);
        uint8_t  select_main = alist_parse(w2, 24,  8);
        uint32_t address     = alist_parse(w2,  0, 24);

        uint16_t dmem = (select_main == 0) ? NAUDIO_MAIN : NAUDIO_MAIN2;

        alist_polef(
            flags & A_INIT,
            gain,
            (int16_t*)l_alist.table,
            address,
            dmem,
            dmem,
            NAUDIO_SUBFRAME_SIZE);
    }
    else
    {
        DebugMessage(M64MSG_WARNING, "NAUDIO_14: non null codebook[0-3] case not implemented !");
    }
}

static void ADPCM(uint32_t w1, uint32_t w2)
{
    uint32_t address = alist_parse(w1,  0, 24);
    uint8_t  flags   = alist_parse(w2, 28,  4);
    uint16_t count   = alist_parse(w2, 16, 12);
    uint16_t dmemi   = alist_parse(w2, 12,  4);
    uint16_t dmemo   = alist_parse(w2,  0, 12);

    adpcm_decode(
            flags & A_INIT,
            flags & A_LOOP,
            false, // not supported in this ucode version
            (int16_t*)l_alist.table,
            l_alist.loop,
            address,
            NAUDIO_MAIN + dmemo,
            NAUDIO_MAIN + dmemi,
            count);
}

static void RESAMPLE(uint32_t w1, uint32_t w2)
{
    uint32_t address = alist_parse(w1,  0, 24);
    uint8_t  flags   = alist_parse(w2, 30,  2);
    uint16_t pitch   = alist_parse(w2, 14, 16);
    uint16_t dmemi   = alist_parse(w2,  2, 12);
    uint16_t dmemo   = alist_parse(w2,  0,  2);

    resample_buffer(
            flags & A_INIT,
            address,
            (uint32_t)pitch << 1,
            (dmemi + NAUDIO_MAIN) >> 1,
            ((dmemo) ? NAUDIO_MAIN2 : NAUDIO_MAIN) >> 1,
            NAUDIO_SUBFRAME_SIZE >> 1);
}

static void INTERLEAVE(uint32_t w1, uint32_t w2)
{
    alist_interleave(
            NAUDIO_MAIN,
            NAUDIO_DRY_LEFT,
            NAUDIO_DRY_RIGHT,
            NAUDIO_SUBFRAME_SIZE >> 1);
}

static void MP3ADDY(uint32_t w1, uint32_t w2)
{
    /* do nothing ? */
}

static void MP3(uint32_t w1, uint32_t w2)
{
    uint8_t  index   = alist_parse(w1,  1,  4);
    uint32_t address = alist_parse(w2,  0, 24);

    mp3_decode(address, index);
}

/* Audio Binary Interface tables */
static const acmd_callback_t ABI_NAUDIO[0x10] = 
{
    SPNOOP,         ADPCM,          CLEARBUFF,      ENVMIXER,
    LOADBUFF,       RESAMPLE,       SAVEBUFF,       NAUDIO_0000,
    NAUDIO_0000,    SETVOL,         DMEMMOVE,       LOADADPCM,
    MIXER,          INTERLEAVE,     NAUDIO_02B0,    SETLOOP
};

static const acmd_callback_t ABI_NAUDIO_BK[0x10] = 
{
    SPNOOP,         ADPCM,          CLEARBUFF,      ENVMIXER,
    LOADBUFF,       RESAMPLE,       SAVEBUFF,       NAUDIO_0000,
    NAUDIO_0000,    SETVOL,         DMEMMOVE,       LOADADPCM,
    MIXER,          INTERLEAVE,     NAUDIO_02B0,    SETLOOP
};

static const acmd_callback_t ABI_NAUDIO_DK[0x10] = 
{
    SPNOOP,         ADPCM,          CLEARBUFF,      ENVMIXER,
    LOADBUFF,       RESAMPLE,       SAVEBUFF,       MIXER,
    MIXER,          SETVOL,         DMEMMOVE,       LOADADPCM,
    MIXER,          INTERLEAVE,     NAUDIO_02B0,    SETLOOP
};

static const acmd_callback_t ABI_NAUDIO_MP3[0x10] = 
{
    UNKNOWN,        ADPCM,          CLEARBUFF,      ENVMIXER,
    LOADBUFF,       RESAMPLE,       SAVEBUFF,       MP3,
    MP3ADDY,        SETVOL,         DMEMMOVE,       LOADADPCM,
    MIXER,          INTERLEAVE,     NAUDIO_14,      SETLOOP
};

static const acmd_callback_t ABI_NAUDIO_CBFD[0x10] = 
{
    UNKNOWN,        ADPCM,          CLEARBUFF,      ENVMIXER,
    LOADBUFF,       RESAMPLE,       SAVEBUFF,       MP3,
    MP3ADDY,        SETVOL,         DMEMMOVE,       LOADADPCM,
    MIXER,          INTERLEAVE,     NAUDIO_14,      SETLOOP
};

/* global functions */
void alist_process_naudio()
{
    alist_process(ABI_NAUDIO, 0x10);
}

void alist_process_naudio_bk()
{
    alist_process(ABI_NAUDIO_BK, 0x10);
}

void alist_process_naudio_dk()
{
    alist_process(ABI_NAUDIO_DK, 0x10);
}

void alist_process_naudio_mp3()
{
    alist_process(ABI_NAUDIO_MP3, 0x10);
}

void alist_process_naudio_cbfd()
{
    alist_process(ABI_NAUDIO_CBFD, 0x10);
}

