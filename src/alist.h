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

#include <stdbool.h>
#include <stdint.h>

/* types defintions */
typedef void (*acmd_callback_t)(uint32_t w1, uint32_t w2);

struct ramp_t
{
    int32_t value;
    int32_t step;
    int32_t target; // lower 16 bits should be null
};

/* some audio list flags */
#define A_INIT          0x01
#define A_LOOP          0x02
#define A_LEFT          0x02
#define A_VOL           0x04
#define A_AUX           0x08

unsigned align(unsigned x, unsigned m);
void dma_read_fast(uint16_t mem, uint32_t dram, uint16_t length);
void dma_write_fast(uint32_t dram, uint16_t mem, uint16_t length);
void dram_read_many_u16(uint16_t *dst, uint32_t address, size_t length);

uint32_t alist_parse(uint32_t value, unsigned offset, unsigned width);
void alist_process(const acmd_callback_t abi[], size_t n);
uint32_t alist_segments_load(uint32_t so, const uint32_t* const segments, size_t n);
void alist_segments_store(uint32_t so, uint32_t* const segments, size_t n);
void alist_dmemmove(uint16_t dmemo, uint16_t dmemi, uint16_t count);
void alist_mix(uint16_t dmemo, uint16_t dmemi, uint16_t count, int16_t gain);
void alist_interleave(uint16_t dmemo, uint16_t left, uint16_t right, uint16_t count);
void alist_polef(bool init, uint16_t gain, int16_t* table, uint32_t address, uint16_t dmemo, uint16_t dmemi, int count);

bool ramp_next(struct ramp_t *ramp);

#endif

