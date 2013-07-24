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
#include <string.h>
#include "hle.h"

#include "alist.h"

/* update ramp to its next value.
 * returns true if target has been reached, false otherwise */
int ramp_next(struct ramp_t *ramp)
{
    s64 accu     = (s64)ramp->value + (s64)ramp->step;
    s64 accu_int = accu & ~0xffff; // lower bits are discarded for the comparison to target

    int target_reached = (ramp->step >= 0)
        ? (accu_int > ramp->target)
        : (accu_int < ramp->target);

    ramp->value = (target_reached)
        ? ramp->target | (accu & 0xffff) // but restored even if target is reached
        : (s32)accu;

    return target_reached;
}

/* local functions */
unsigned align(unsigned x, unsigned m)
{
    --m;
    return (x + m) & (~m);
}

s16 clamp_s16(s32 x)
{
    if (x > 32767) { x = 32767; } else if (x < -32768) { x = -32768; }
    return x;
}

s32 dmul_round(s16 x, s16 y)
{
    return ((s32)x * (s32)y + 0x4000) >> 15;
}

void sadd(s16 *x, s32 y)
{
    *x = clamp_s16(*x + y);
}

/* caller is responsible to ensure that size and alignment constrains are met */
void dma_read_fast(u16 mem, u32 dram, u16 length)
{
    // mem & dram should be 8 byte aligned
    // length should encode a linear transfer
    assert((mem & ~0x1ff8) == 0);
    assert((dram & ~0xfffff8) == 0);
    assert((length & ~0xfff) == 0);

    memcpy(rsp.DMEM + mem, rsp.RDRAM + dram, align(length+1, 8));
}

/* caller is responsible to ensure that size and alignment constrains are met */
void dma_write_fast(u32 dram, u16 mem, u16 length)
{
    // mem & dram should be 8 byte aligned
    // length should encode a linear transfer
    assert((mem & ~0x1ff8) == 0);
    assert((dram & ~0xfffff8) == 0);
    assert((length & ~0xfff) == 0);

    memcpy(rsp.RDRAM + dram, rsp.DMEM + mem, align(length+1, 8));
}


u32 alist_parse(u32 value, unsigned offset, unsigned width)
{
    return (value >> offset) & ((1 << width) - 1);
}

void alist_process(const acmd_callback_t abi[], size_t n)
{
    u32 w1, w2;
    u8 acmd;
    const OSTask_t * const task = get_task();

    const unsigned int *alist = (unsigned int*)(rsp.RDRAM + task->data_ptr);
    const unsigned int * const alist_end = alist + (task->data_size >> 2);

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

u32 alist_segments_load(u32 so, const u32* const segments, size_t n)
{
    u8 segment = alist_parse(so, 24,  6);
    u32 offset = alist_parse(so,  0, 24);

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

void alist_segments_store(u32 so, u32* const segments, size_t n)
{
    u8 segment = alist_parse(so, 24,  6);
    u32 offset = alist_parse(so,  0, 24);

    if (segment < n)
    {
        segments[segment] = offset;
    }
    else
    {
        DebugMessage(M64MSG_WARNING, "Invalid segment %u", segment);
    }
}

void alist_dmemmove(u16 dmemo, u16 dmemi, u16 count)
{
    while (count != 0)
    {
        rsp.DMEM[(dmemo++)^S8] = rsp.DMEM[(dmemi++)^S8];
        --count;
    }
}

void alist_mix(u16 dmemo, u16 dmemi, u16 count, s16 gain)
{
    s16 *dst = (s16*)(rsp.DMEM + dmemo);
    s16 *src = (s16*)(rsp.DMEM + dmemi);

    while (count != 0)
    {
        sadd(dst++, dmul_round(*src++, gain));
        --count;
    }
}

void alist_interleave(u16 dmemo, u16 left, u16 right, u16 count)
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

