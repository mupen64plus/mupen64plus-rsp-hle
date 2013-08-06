/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - memory.h                                        *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2013 Bobby Smiles                                       *
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

#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

#ifdef M64P_BIG_ENDIAN
#define S 0
#define S16 0
#define S8 0
#else
#define S 1
#define S16 2
#define S8 3
#endif

unsigned align(unsigned x, unsigned m);

void dma_read_fast(uint16_t mem, uint32_t dram, uint16_t length);
void dma_write_fast(uint32_t dram, uint16_t mem, uint16_t length);

void dram_load_many_u16(uint16_t *dst, uint32_t address, size_t count);
void dram_store_many_u16(uint32_t address, const uint16_t *src, size_t count);

void mem_load_many_u16(uint16_t *dst, uint16_t address, size_t count);
void mem_store_many_u16(uint16_t address, const uint16_t *src, size_t count);

void dram_load_many_u32(uint32_t *dst, uint32_t address, size_t count);
void dram_store_many_u32(uint32_t address, const uint32_t *src, size_t count);


#endif

