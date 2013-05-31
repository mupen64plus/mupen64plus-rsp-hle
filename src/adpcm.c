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

static s16 clamp_s16(s32 x)
{
    if (x > 32767) { x = 32767; } else if (x < -32768) { x = -32768; }
    return x;
}

/* types definition */
typedef u16 (*get_predicted_frame_t)(s16 *dst, u16 src, unsigned char scale);

/* local functions prototypes */
static unsigned int get_scale_shift(unsigned char scale, unsigned char range);
static s16 get_predicted_sample(u8 byte, u8 mask, unsigned lshift, unsigned rshift);
static u16 get_predicted_frame_4bits(s16 *dst, u16 src, unsigned char scale);
static u16 get_predicted_frame_2bits(s16 *dst, u16 src, unsigned char scale);
static s32 compute_residuals(unsigned index, const s16 *src, const s16 *book1, const s16 *book2, s16 l1, s16 l2);
static void decode_8_samples(s16 *dst, const s16 *src, const s16 *cb_entry);
static s16* decode_frames(get_predicted_frame_t get_predicted_frame, s16 *dst, u16 src, int count, s16 *codebook);

/* global functions */
void adpcm_decode(
        int init, int loop, int two_bits_per_sample,
        s16* codebook, u32 loop_address, u32 last_frame_address,
        u16 in, u16 out, int count)
{
    s16 *dst = (s16*)(rsp.DMEM + out);

    /* init/load last frame */
    if (init)
    {
        memset(dst, 0, 32);
    }
    else
    {
        void *src = (loop) ? &rsp.RDRAM[loop_address] : &rsp.RDRAM[last_frame_address];
        memcpy(dst, src, 32);
    }
    dst += 16;
   
    /* decode frames */
    dst = (two_bits_per_sample)
        ? decode_frames(get_predicted_frame_2bits, dst, in, count, codebook)
        : decode_frames(get_predicted_frame_4bits, dst, in, count, codebook);

    /* save last frame */
    dst -= 16;
    memcpy(&rsp.RDRAM[last_frame_address], dst, 32);
}

void adpcm_load_codebook(u16 *dst, u32 address, int count)
{
    unsigned int i;

    const s16 *src = (s16*)(rsp.RDRAM + address);
    count >>= 1;

    for (i = 0; i < count; ++i)
        dst[i^S] = *(src++);
}

/* local functions */
static unsigned int get_scale_shift(unsigned char scale, unsigned char range)
{
    return (scale < range) ? range - scale : 0;
}

static s16 get_predicted_sample(u8 byte, u8 mask, unsigned lshift, unsigned rshift)
{
    s16 sample = ((u16)byte & (u16)mask) << lshift;
    sample >>= rshift; /* signed */
    return sample;
}

static u16 get_predicted_frame_4bits(s16 *dst, u16 src, unsigned char scale)
{
    unsigned int i;
    u8 byte;
    
    unsigned int rshift = get_scale_shift(scale, 12);

    for(i = 0; i < 8; ++i)
    {
        byte = rsp.DMEM[(src++)^S8];

        *(dst++) = get_predicted_sample(byte, 0xf0,  8, rshift);
        *(dst++) = get_predicted_sample(byte, 0x0f, 12, rshift);
    }

    return src;
}

static u16 get_predicted_frame_2bits(s16 *dst, u16 src, unsigned char scale)
{
    unsigned int i;
    u8 byte;

    unsigned int rshift = get_scale_shift(scale, 14);

    for(i = 0; i < 4; ++i)
    {
        byte = rsp.DMEM[(src++)^S8];

        *(dst++) = get_predicted_sample(byte, 0xc0,  8, rshift);
        *(dst++) = get_predicted_sample(byte, 0x30, 10, rshift);
        *(dst++) = get_predicted_sample(byte, 0x0c, 12, rshift);
        *(dst++) = get_predicted_sample(byte, 0x03, 14, rshift);
    }

    return src;
}

static s32 compute_residuals(unsigned index, const s16 *src, const s16 *book1, const s16 *book2, s16 l1, s16 l2)
{
    unsigned i;
    s32 residuals;

    residuals =  book1[index]*l1 + book2[index]*l2;
    
    for(i = 0; i < index; ++i)
        residuals += book2[index - i - 1] * src[i];

    return residuals;
}

static void decode_8_samples(s16 *dst, const s16 *src, const s16 *cb_entry)
{
    const s16 * const book1 = cb_entry;
    const s16 * const book2 = cb_entry + 8;

    const s16 l1 = dst[-2 ^ S];
    const s16 l2 = dst[-1 ^ S];

    unsigned i;
    s32 accu;

    for(i = 0; i < 8; ++i)
    {
        accu = src[i] << 11;
        accu += compute_residuals(i, src, book1, book2, l1, l2);
        dst[i ^ S] = clamp_s16(accu >> 11);
    }
}

static s16* decode_frames(get_predicted_frame_t get_predicted_frame, s16 *dst, u16 src, int count, s16 *codebook)
{
    u8 predictor;
    s16 *cb_entry;
    unsigned char scale;
    s16 frame[16];

    while (count > 0)
    {
        predictor = rsp.DMEM[(src++)^S8];

        scale = (predictor & 0xf0) >> 4;
        cb_entry = codebook + ((predictor & 0xf) << 4);

        src = get_predicted_frame(frame, src, scale);

        decode_8_samples(dst, frame    , cb_entry); dst += 8; 
        decode_8_samples(dst, frame + 8, cb_entry); dst += 8; 
        
        --count;
    }

    return dst;
}

