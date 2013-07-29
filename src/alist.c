/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - alist.c                                         *
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

#include "alist.h"
#include "arithmetic.h"

/* update ramp to its next value.
 * returns true if target has been reached, false otherwise */
bool ramp_next(struct ramp_t *ramp)
{
    int64_t accu     = (int64_t)ramp->value + (int64_t)ramp->step;
    int64_t accu_int = accu & ~0xffff; // lower bits are discarded for the comparison to target

    bool target_reached = (ramp->step >= 0)
        ? (accu_int > ramp->target)
        : (accu_int < ramp->target);

    ramp->value = (target_reached)
        ? ramp->target | (accu & 0xffff) // but restored even if target is reached
        : (int32_t)accu;

    return target_reached;
}

unsigned align(unsigned x, unsigned m)
{
    --m;
    return (x + m) & (~m);
}

/* caller is responsible to ensure that size and alignment constrains are met */
void dma_read_fast(uint16_t mem, uint32_t dram, uint16_t length)
{
    // mem & dram should be 8 byte aligned
    // length should encode a linear transfer
    assert((mem & ~0x1ff8) == 0);
    assert((dram & ~0xfffff8) == 0);
    assert((length & ~0xfff) == 0);

    memcpy(rsp.DMEM + mem, rsp.RDRAM + dram, align(length+1, 8));
}

/* caller is responsible to ensure that size and alignment constrains are met */
void dma_write_fast(uint32_t dram, uint16_t mem, uint16_t length)
{
    // mem & dram should be 8 byte aligned
    // length should encode a linear transfer
    assert((mem & ~0x1ff8) == 0);
    assert((dram & ~0xfffff8) == 0);
    assert((length & ~0xfff) == 0);

    memcpy(rsp.RDRAM + dram, rsp.DMEM + mem, align(length+1, 8));
}

void dram_read_many_u16(uint16_t *dst, uint32_t address, size_t length)
{
    length >>= 1;

    while (length != 0)
    {
        *dst++ = *(uint16_t*)(rsp.RDRAM + (address^S16));
        address += 2;
        --length;
    }
}


uint32_t alist_parse(uint32_t value, unsigned offset, unsigned width)
{
    return (value >> offset) & ((1 << width) - 1);
}

void alist_process(const acmd_callback_t abi[], size_t n)
{
    uint32_t w1, w2;
    unsigned char acmd;
    const OSTask_t * const task = get_task();

    const uint32_t * alist = (uint32_t*)(rsp.RDRAM + task->data_ptr);
    const uint32_t * const alist_end = alist + (task->data_size >> 2);

    while (alist != alist_end)
    {
        w1 = *(alist++);
        w2 = *(alist++);

        acmd = alist_parse(w1, 24, 7);

        if (acmd < n)
        {
            (*abi[acmd])(w1, w2);
        }
        else
        {
            DebugMessage(M64MSG_WARNING, "Invalid ABI command %u", acmd);
        }
    }
}

uint32_t alist_segments_load(uint32_t so, const uint32_t* const segments, size_t n)
{
    unsigned char segment = alist_parse(so, 24,  6);
    uint32_t offset = alist_parse(so,  0, 24);

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

void alist_segments_store(uint32_t so, uint32_t* const segments, size_t n)
{
    unsigned char segment = alist_parse(so, 24,  6);
    uint32_t offset = alist_parse(so,  0, 24);

    if (segment < n)
    {
        segments[segment] = offset;
    }
    else
    {
        DebugMessage(M64MSG_WARNING, "Invalid segment %u", segment);
    }
}

void alist_dmemmove(uint16_t dmemo, uint16_t dmemi, uint16_t count)
{
    while (count != 0)
    {
        rsp.DMEM[(dmemo++)^S8] = rsp.DMEM[(dmemi++)^S8];
        --count;
    }
}

void alist_mix(uint16_t dmemo, uint16_t dmemi, uint16_t count, int16_t gain)
{
    int16_t *dst = (int16_t*)(rsp.DMEM + dmemo);
    int16_t *src = (int16_t*)(rsp.DMEM + dmemi);

    while (count != 0)
    {
        sadd(dst++, dmul_round(*src++, gain));
        --count;
    }
}

void alist_interleave(uint16_t dmemo, uint16_t left, uint16_t right, uint16_t count)
{
    uint16_t l1, l2, r1, r2;

    count >>= 1;

    uint16_t *dst  = (uint16_t*)(rsp.DMEM + dmemo);
    uint16_t *srcL = (uint16_t*)(rsp.DMEM + left);
    uint16_t *srcR = (uint16_t*)(rsp.DMEM + right);

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

void alist_polef(
        bool init, uint16_t gain, int16_t* table, uint32_t address,
        uint16_t dmemo, uint16_t dmemi, int count)
{
    int16_t *dst = (int16_t*)(rsp.DMEM + dmemo);

    const int16_t * const h1 = table;
          int16_t * const h2 = table + 8;

    int16_t l1, l2;
    unsigned i;
    int32_t accu;
    int16_t h2_before[8];
    int16_t frame[8]; /* buffer for samples being processed
                     (needed because processing is usually done inplace [dmemi == dmemo]) */

    if (init)
    {
        /* FIXME: original ucode doesn't do it that way */
        l1 = 0;
        l2 = 0;
    }
    else
    {
        /* only the last 2 samples are needed */
        l1 = *(int16_t*)(rsp.RDRAM + ((address + 4) ^ S16));
        l2 = *(int16_t*)(rsp.RDRAM + ((address + 6) ^ S16));
    }

    for(i = 0; i < 8; ++i)
    {
        h2_before[i] = h2[i];
        h2[i] = (int16_t)(((int32_t)h2[i] * gain) >> 14);
    }

    do
    {
        for(i = 0; i < 8; ++i)
        {
            frame[i] = *(int16_t*)(rsp.DMEM + (dmemi^S16));
            dmemi += 2;
        }

        for(i = 0; i < 8; ++i)
        {
            accu = frame[i] * gain;
            accu += h1[i]*l1 + h2_before[i]*l2;
            accu += rdot(i, h2, frame);
            dst[i ^ S] = clamp_s16(accu >> 14);
        }

        l1 = dst[6^S];
        l2 = dst[7^S];

        dst += 8;
        count -= 0x10;
    } while (count > 0);

    /* save last 4 samples */
    memcpy(rsp.RDRAM + address, dst - 4, 8);
}

