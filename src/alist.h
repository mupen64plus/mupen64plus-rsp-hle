/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - alist.h                                         *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
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

#ifndef ALIST_H
#define ALIST_H

#include "hle.h"

/* types defintions */
typedef void (*acmd_callback_t)(u32 w1, u32 w2);

struct ramp_t
{
    s32 value;
    s32 step;
    s32 target; // lower 16 bits should be null
};

/* some audio list flags */
#define A_INIT          0x01
#define A_LOOP          0x02
#define A_LEFT          0x02
#define A_VOL           0x04
#define A_AUX           0x08

unsigned align(unsigned x, unsigned m);
void dma_read_fast(u16 mem, u32 dram, u16 length);
void dma_write_fast(u32 dram, u16 mem, u16 length);
void dram_read_many_u16(u16 *dst, u32 address, size_t length);

u32 alist_parse(u32 value, unsigned offset, unsigned width);
void alist_process(const acmd_callback_t abi[], size_t n);
u32 alist_segments_load(u32 so, const u32* const segments, size_t n);
void alist_segments_store(u32 so, u32* const segments, size_t n);
void alist_dmemmove(u16 dmemo, u16 dmemi, u16 count);
void alist_mix(u16 dmemo, u16 dmemi, u16 count, s16 gain);
void alist_interleave(u16 dmemo, u16 left, u16 right, u16 count);
void alist_polef(int init, u16 gain, s16* table, u32 address, u16 dmemo, u16 dmemi, int count);

int ramp_next(struct ramp_t *ramp);

#endif

