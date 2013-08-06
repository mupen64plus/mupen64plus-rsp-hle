/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - memory.c                                        *
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

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "hle.h"
#include "memory.h"

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

// address should be 2 byte aligned
// transfert should not exceed buffer size
void dram_load_many_u16(uint16_t *dst, uint32_t address, size_t count)
{
    size_t i;

    for(i = 0; i < count; ++i)
    {
        *(dst++) = *(uint16_t*)(rsp.RDRAM + (address^S16));
        address += 2;
    }
}

// address should be 2 byte aligned
// transfert should not exceed buffer size
void dram_store_many_u16(uint32_t address, const uint16_t *src, size_t count)
{
    size_t i;

    for(i = 0; i < count; ++i)
    {
        *(uint16_t*)(rsp.RDRAM + (address^S16)) = *(src++);
        address += 2;
    }
}

// address should be 4 byte aligned
// transfert should not exceed buffer size
void dram_load_many_u32(uint32_t *dst, uint32_t address, size_t count)
{
    memcpy(dst, rsp.RDRAM + address, count << 2);
}

// address should be 4 byte aligned
// transfert should not exceed buffer size
void dram_store_many_u32(uint32_t address, const uint32_t *src, size_t count)
{
    memcpy(rsp.RDRAM + address, src, count << 2);
}





// address should be 2 byte aligned
// transfert should not exceed buffer size
void mem_load_many_u16(uint16_t *dst, uint16_t address, size_t count)
{
    size_t i;

    for(i = 0; i < count; ++i)
    {
        *(dst++) = *(uint16_t*)(rsp.DMEM + (address^S16));
        address += 2;
    }
}

// address should be 2 byte aligned
// transfert should not exceed buffer size
void mem_store_many_u16(uint16_t address, const uint16_t *src, size_t count)
{
    size_t i;

    for(i = 0; i < count; ++i)
    {
        *(uint16_t*)(rsp.DMEM + (address^S16)) = *(src++);
        address += 2;
    }
}

