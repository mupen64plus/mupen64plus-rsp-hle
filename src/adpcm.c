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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hle.h"
#include "arithmetic.h"

/* types definition */
typedef unsigned int (*get_predicted_frame_t)(int16_t *dst, uint16_t dmemi, unsigned char scale);

/* local functions prototypes */
static unsigned int get_scale_shift(unsigned char scale, unsigned char range);
static int16_t get_predicted_sample(uint8_t byte, uint8_t mask, unsigned lshift, unsigned rshift);
static unsigned int get_predicted_frame_4bits(int16_t *dst, uint16_t dmemi, unsigned char scale);
static unsigned int get_predicted_frame_2bits(int16_t *dst, uint16_t dmemi, unsigned char scale);
static void decode_8_samples(int16_t *dst, const int16_t *src,
        const int16_t *cb_entry, const int16_t *last_samples);
static void decode_frames(get_predicted_frame_t get_predicted_frame,
        const int16_t *codebook, int16_t *last_frame,
        uint16_t dmemo, uint16_t dmemi, uint16_t size);


static void dram_load_many_u16(uint16_t *dst, uint32_t address, size_t count)
{
    size_t i;

    for(i = 0; i < count; ++i)
    {
        *(dst++) = *(uint16_t*)(rsp.RDRAM + (address^S16));
        address += 2;
    }
}

static void dram_store_many_u16(uint32_t address, const uint16_t *src, size_t count)
{
    size_t i;

    for(i = 0; i < count; ++i)
    {
        *(uint16_t*)(rsp.RDRAM + (address^S16)) = *(src++);
        address += 2;
    }
}

static void mem_store_many_u16(uint16_t address, const uint16_t *src, size_t count)
{
    size_t i;

    for(i = 0; i < count; ++i)
    {
        *(uint16_t*)(rsp.DMEM + (address^S16)) = *(src++);
        address += 2;
    }
}


/* global functions */
void adpcm_decode(
        bool init, bool loop, bool two_bits_per_sample,
        const int16_t* codebook, uint32_t loop_address, uint32_t last_frame_address,
        uint16_t dmemo, uint16_t dmemi, uint16_t size)
{
    int16_t last_frame[16];

    if (init)
    {
        memset(last_frame, 0, 16*sizeof(last_frame[0]));
    }
    else
    {
        dram_load_many_u16((uint16_t*)last_frame, (loop) ? loop_address : last_frame_address, 16);
    }

    mem_store_many_u16(dmemo, (uint16_t*)last_frame, 16);
   
    decode_frames(
            (two_bits_per_sample) ? get_predicted_frame_2bits : get_predicted_frame_4bits,
            codebook, last_frame,
            dmemo + 32, dmemi, size);

    dram_store_many_u16(last_frame_address, (uint16_t*)last_frame, 16);
}

/* local functions */
static unsigned int get_scale_shift(unsigned char scale, unsigned char range)
{
    return (scale < range) ? range - scale : 0;
}

static int16_t get_predicted_sample(uint8_t byte, uint8_t mask, unsigned lshift, unsigned rshift)
{
    int16_t sample = ((uint16_t)byte & (uint16_t)mask) << lshift;
    sample >>= rshift; /* signed */
    return sample;
}

static unsigned int get_predicted_frame_4bits(int16_t *dst, uint16_t dmemi, unsigned char scale)
{
    unsigned int i;
    uint8_t byte;
    
    unsigned int rshift = get_scale_shift(scale, 12);

    for(i = 0; i < 8; ++i)
    {
        byte = rsp.DMEM[(dmemi++)^S8];

        *(dst++) = get_predicted_sample(byte, 0xf0,  8, rshift);
        *(dst++) = get_predicted_sample(byte, 0x0f, 12, rshift);
    }

    return 8;
}

static unsigned int get_predicted_frame_2bits(int16_t *dst, uint16_t dmemi, unsigned char scale)
{
    unsigned int i;
    uint8_t byte;

    unsigned int rshift = get_scale_shift(scale, 14);

    for(i = 0; i < 4; ++i)
    {
        byte = rsp.DMEM[(dmemi++)^S8];

        *(dst++) = get_predicted_sample(byte, 0xc0,  8, rshift);
        *(dst++) = get_predicted_sample(byte, 0x30, 10, rshift);
        *(dst++) = get_predicted_sample(byte, 0x0c, 12, rshift);
        *(dst++) = get_predicted_sample(byte, 0x03, 14, rshift);
    }

    return 4;
}

static void decode_8_samples(int16_t *dst, const int16_t *src,
        const int16_t *cb_entry, const int16_t *last_samples)
{
    const int16_t * const book1 = cb_entry;
    const int16_t * const book2 = cb_entry + 8;

    const int16_t l1 = last_samples[0];
    const int16_t l2 = last_samples[1];

    size_t i;
    int32_t accu;

    for(i = 0; i < 8; ++i)
    {
        accu = src[i] << 11;
        accu += book1[i]*l1 + book2[i]*l2 + rdot(i, book2, src);
        dst[i] = clamp_s16(accu >> 11);
    }
}

static void decode_frames(get_predicted_frame_t get_predicted_frame,
        const int16_t *codebook, int16_t *last_frame,
        uint16_t dmemo, uint16_t dmemi, uint16_t size)
{
    uint8_t predictor;
    const int16_t *cb_entry;
    unsigned char scale;
    int16_t frame[16];
    uint16_t i;

    for(i = 0; i < size; i += 32)
    {
        predictor = rsp.DMEM[(dmemi++)^S8];

        scale = (predictor & 0xf0) >> 4;
        cb_entry = codebook + ((predictor & 0xf) << 4);

        dmemi += get_predicted_frame(frame, dmemi, scale);

        decode_8_samples(last_frame    , frame    , cb_entry, last_frame + 14);
        decode_8_samples(last_frame + 8, frame + 8, cb_entry, last_frame + 6 );

        mem_store_many_u16(dmemo, (uint16_t*)last_frame, 16);
        dmemo += 32;
    }
}

