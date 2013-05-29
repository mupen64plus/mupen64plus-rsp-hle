/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - adpcm.c                                         *
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

#include <string.h>

#include "hle.h"

static unsigned int get_vscale(unsigned char nibble, unsigned char range);
static s16 unpack_sample(u8 byte, u8 mask, unsigned shift, unsigned vscale);
static u16 unpack_16_samples_4bits(s16 *samples, u16 src,  unsigned char nibble);
static u16 unpack_16_samples_2bits(s16 *samples, u16 src, unsigned char nibble);
static void decode_8_samples(s16 *dst, const s16 *src, const s16 * codebook);

static s16 clamp_s16(s32 x)
{
    if (x > 32767) { x = 32767; } else if (x < -32768) { x = -32768; }
    return x;
}

/* global functions */
void adpcm_decode(
        int init,
        int loop,
        int better_compression, // some ucodes encodes 4 samples per byte instead of 2 per byte
        s16* codebook,
        u32 loop_address,
        u32 state_address,
        u16 in,
        u16 out,
        int count)
{
    unsigned char byte;
    s16 *code;
    s16 samples[16];
    
    s16 *dst = (s16*)(rsp.DMEM + out);
  
    if (init)
    {
        memset(dst, 0, 32);
    }
    else
    {
        void *src = (loop) ? &rsp.RDRAM[loop_address] : &rsp.RDRAM[state_address];
        memcpy(dst, src, 32);
    }

    dst += 16;
    while(count>0)
    {
        byte = rsp.DMEM[in^S8]; ++in;
        code = codebook + ((byte & 0xf) << 4);
        byte >>= 4;

        in = (better_compression) 
            ? unpack_16_samples_2bits(samples, in, byte)
            : unpack_16_samples_4bits(samples, in, byte);

        decode_8_samples(dst, samples    , code); dst += 8; 
        decode_8_samples(dst, samples + 8, code); dst += 8; 

        count -= 32;
    }
    dst -= 16;
    memcpy(&rsp.RDRAM[state_address], dst, 32);
}

void adpcm_load_codebook(u16 *dst, u32 address, int count)
{
    int i;
    u16 *table = (u16 *)(rsp.RDRAM+address);

    count >>= 4;

    for (i = 0; i < count; ++i)
    {
        dst[(0x0+(i<<3))^S] = table[0];
        dst[(0x1+(i<<3))^S] = table[1];
        dst[(0x2+(i<<3))^S] = table[2];
        dst[(0x3+(i<<3))^S] = table[3];
        dst[(0x4+(i<<3))^S] = table[4];
        dst[(0x5+(i<<3))^S] = table[5];
        dst[(0x6+(i<<3))^S] = table[6];
        dst[(0x7+(i<<3))^S] = table[7];
        table += 8;
    }
}


/* local functions */
static unsigned int get_vscale(unsigned char nibble, unsigned char range)
{
    return (nibble < range) ? range - nibble : 0;
}

static s16 unpack_sample(u8 byte, u8 mask, unsigned shift, unsigned vscale)
{
    s16 sample = ((u16)byte & (u16)mask) << shift;
    sample >>= vscale; /* signed */
    return sample;
}

static u16 unpack_16_samples_4bits(s16 *samples, u16 src,  unsigned char nibble)
{
    unsigned int i;
    u8 byte;
    
    unsigned int vscale = get_vscale(nibble, 12);

    for(i = 0; i < 8; ++i)
    {
        byte = rsp.DMEM[(src++)^S8];

        *(samples++) = unpack_sample(byte, 0xf0,  8, vscale);
        *(samples++) = unpack_sample(byte, 0x0f, 12, vscale);
    }

    return src;
}

static u16 unpack_16_samples_2bits(s16 *samples, u16 src, unsigned char nibble)
{
    unsigned int i;
    u8 byte;

    unsigned int vscale = get_vscale(nibble, 14);

    for(i = 0; i < 4; ++i)
    {
        byte = rsp.DMEM[(src++)^S8];

        *(samples++) = unpack_sample(byte, 0xc0,  8, vscale);
        *(samples++) = unpack_sample(byte, 0x30, 10, vscale);
        *(samples++) = unpack_sample(byte, 0x0c, 12, vscale);
        *(samples++) = unpack_sample(byte, 0x03, 14, vscale);
    }

    return src;
}

static void decode_8_samples(s16 *dst, const s16 *src, const s16 * codebook)
{
    const s16 * const book1 = codebook;
    const s16 * const book2 = codebook + 8;

    const s16 l1 = dst[-2 ^ S];
    const s16 l2 = dst[-1 ^ S];

    s32 accu;

    accu  = (s32)book1[0]*(s32)l1;
    accu += (s32)book2[0]*(s32)l2;
    accu += (s32)src[0] << 11;
    dst[0 ^ S] = clamp_s16(accu >> 11);

    accu  = (s32)book1[1]*(s32)l1;
    accu += (s32)book2[1]*(s32)l2;
    accu += (s32)book2[0]*(s32)src[0];
    accu += (s32)src[1] << 11;
    dst[1 ^ S] = clamp_s16(accu >> 11);

    accu  = (s32)book1[2]*(s32)l1;
    accu += (s32)book2[2]*(s32)l2;
    accu += (s32)book2[1]*(s32)src[0];
    accu += (s32)book2[0]*(s32)src[1];
    accu += (s32)src[2] << 11;
    dst[2 ^ S] = clamp_s16(accu >> 11);

    accu  = (s32)book1[3]*(s32)l1;
    accu += (s32)book2[3]*(s32)l2;
    accu += (s32)book2[2]*(s32)src[0];
    accu += (s32)book2[1]*(s32)src[1];
    accu += (s32)book2[0]*(s32)src[2];
    accu += (s32)src[3] << 11;
    dst[3 ^ S] = clamp_s16(accu >> 11);

    accu  = (s32)book1[4]*(s32)l1;
    accu += (s32)book2[4]*(s32)l2;
    accu += (s32)book2[3]*(s32)src[0];
    accu += (s32)book2[2]*(s32)src[1];
    accu += (s32)book2[1]*(s32)src[2];
    accu += (s32)book2[0]*(s32)src[3];
    accu += (s32)src[4] << 11;
    dst[4 ^ S] = clamp_s16(accu >> 11);

    accu  = (s32)book1[5]*(s32)l1;
    accu += (s32)book2[5]*(s32)l2;
    accu += (s32)book2[4]*(s32)src[0];
    accu += (s32)book2[3]*(s32)src[1];
    accu += (s32)book2[2]*(s32)src[2];
    accu += (s32)book2[1]*(s32)src[3];
    accu += (s32)book2[0]*(s32)src[4];
    accu += (s32)src[5] << 11;
    dst[5 ^ S] = clamp_s16(accu >> 11);

    accu  = (s32)book1[6]*(s32)l1;
    accu += (s32)book2[6]*(s32)l2;
    accu += (s32)book2[5]*(s32)src[0];
    accu += (s32)book2[4]*(s32)src[1];
    accu += (s32)book2[3]*(s32)src[2];
    accu += (s32)book2[2]*(s32)src[3];
    accu += (s32)book2[1]*(s32)src[4];
    accu += (s32)book2[0]*(s32)src[5];
    accu += (s32)src[6] << 11;
    dst[6 ^ S] = clamp_s16(accu >> 11);

    accu  = (s32)book1[7]*(s32)l1;
    accu += (s32)book2[7]*(s32)l2;
    accu += (s32)book2[6]*(s32)src[0];
    accu += (s32)book2[5]*(s32)src[1];
    accu += (s32)book2[4]*(s32)src[2];
    accu += (s32)book2[3]*(s32)src[3];
    accu += (s32)book2[2]*(s32)src[4];
    accu += (s32)book2[1]*(s32)src[5];
    accu += (s32)book2[0]*(s32)src[6];
    accu += (s32)src[7] << 11;
    dst[7 ^ S] = clamp_s16(accu >> 11);
}

