/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - hvqm.c                                          *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2018 Gilles Siberlin                                    *
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
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "hle_external.h"
#include "hle_internal.h"
#include "memory.h"

/* Nest size  */
#define HVQM2_NESTSIZE_L 70	/* Number of elements on long side */
#define HVQM2_NESTSIZE_S 38	/* Number of elements on short side */
#define HVQM2_NESTSIZE (HVQM2_NESTSIZE_L * HVQM2_NESTSIZE_S)

struct HVQM2Block {
  uint8_t nbase;
  uint8_t dc;
  uint8_t dc_l;
  uint8_t dc_r;
  uint8_t dc_u;
  uint8_t dc_d;
};

struct HVQM2Basis {
  uint8_t sx;
  uint8_t sy;
  int16_t scale;
  uint16_t offset;
  uint16_t lineskip;
};

union HVQM2Info {
  struct HVQM2Block block;
  struct HVQM2Basis basis;
  uint8_t byte[8];
};

struct HVQM2Arg {
  uint32_t info;                //0x70
  uint32_t buf;                 //0x74
  uint16_t buf_width;           //0x78
  uint8_t chroma_step_h;        //0x7a
  uint8_t chroma_step_v;        //0x7b
  uint16_t hmcus;               //0x7c
  uint16_t vmcus;               //0x7e
  uint8_t alpha;                //0x80
  uint8_t nest[HVQM2_NESTSIZE]; //0x81
};

union VectorResult {
  int16_t vec[12][8];
  int16_t vec_half[24][4];
};

static struct HVQM2Arg arg;

int16_t constant[10][8] = {
{0x0100,0x0200,0x1000,0x2000,0x8000,0x0001,0x0002,0x0004}, //v2 -> 0
{0x0008,0x0020,0x0040,0x0080,0x0000,0x0000,0x0000,0x0000}, //v3 -> 1
{0x0071,0xFFEA,0xFFD2,0x005A,0x3FC0,0x3E00,0x0000,0x0000}, //v4 -> 2
{0x0002,0x0000,0xFFFF,0xFFFF,0x0002,0x0000,0xFFFF,0xFFFF}, //v5 -> 3
{0xFFFF,0xFFFF,0x0000,0x0002,0xFFFF,0xFFFF,0x0000,0x0002}, //v6 -> 4
{0x0002,0x0002,0x0002,0x0002,0x0000,0x0000,0x0000,0x0000}, //v7 -> 5
{0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF}, //v8 -> 6
{0x0000,0x0000,0x0000,0x0000,0x0002,0x0002,0x0002,0x0002}, //v9 -> 7
{0x0006,0x0008,0x0008,0x0006,0x0008,0x000A,0x000A,0x0008}, //v10 -> 8
{0x0008,0x000A,0x000A,0x0008,0x0006,0x0008,0x0008,0x0006}  //v11 -> 9
};

//TOREMOVE
uint8_t data[] = {
0x00,0x02,0x00,0x01,0x00,0x20,0x00,0x10,0x01,0x00,0x00,0x80,0x04,0x00,0x02,0x00,
0x20,0x00,0x08,0x00,0x80,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0xea,0xff,0x71,0x00,0x5a,0x00,0xd2,0xff,0x00,0x3e,0xc0,0x3f,0x00,0x00,0x00,0x00,
0xff,0xff,0x00,0x02,0xff,0xff,0x00,0x02,0x02,0x00,0xff,0xff,0x02,0x00,0xff,0xff,
0x02,0x02,0x02,0x02,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
0x00,0x00,0x00,0x00,0x02,0x02,0x02,0x02,0x06,0x08,0x08,0x06,0x08,0x0a,0x0a,0x08,
0x08,0x0a,0x0a,0x08,0x06,0x08,0x08,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static int process_info(struct hle_t* hle, uint8_t * base, int16_t * out)
{
  union HVQM2Info info;
  int16_t * v1 = out; //v12
  int16_t * v2 = out + 8; //v13
  uint8_t nbase = *base;

  dram_load_u8(hle, info.byte, arg.info, 8);
  arg.info += 8;

  *base = info.block.nbase & 0x7;

  if((info.block.nbase & nbase) != 0)
    return 0;

  if(info.block.nbase == 0)
  {
    //LABEL8
    for(int i = 0; i < 8; i++)
    {
      v1[i] =  constant[8][i] * info.block.dc;
      v1[i] += constant[3][i] * info.block.dc_l;
      v1[i] += constant[4][i] * info.block.dc_r;
      v1[i] += constant[5][i] * info.block.dc_u;
      v1[i] += constant[6][i] * info.block.dc_d;

      v2[i] =  constant[9][i] * info.block.dc;
      v2[i] += constant[3][i] * info.block.dc_l;
      v2[i] += constant[4][i] * info.block.dc_r;
      v2[i] += constant[6][i] * info.block.dc_u;
      v2[i] += constant[7][i] * info.block.dc_d;

      v1[i] += constant[0][7];
      v2[i] += constant[0][7];

      v1[i] = (v1[i] * (uint16_t)constant[0][3]) >> 16;
      v2[i] = (v2[i] * (uint16_t)constant[0][3]) >> 16;
    }
  }
  else if((info.block.nbase & 0xf) == 0)
  {
    //LABEL7
    union HVQM2Info info1;
    union HVQM2Info info2;

    dram_load_u8(hle, info1.byte, arg.info, 8);
    arg.info += 8;

    dram_load_u8(hle, info2.byte, arg.info, 8);
    arg.info += 8;

    for(int i = 0; i < 8; i++)
    {
      v1[i] = info1.byte[i] << 7;
      v2[i] = info2.byte[i] << 7;
      v1[i] = (v1[i] * (uint16_t)constant[0][1]) >> 16;
      v2[i] = (v2[i] * (uint16_t)constant[0][1]) >> 16;
    }
  }
  else if(*base == 0)
  {
    //LABEL6

    //TODO
    //uint8_t dc = info.block.dc; //v16[1]
    //dram_load_u8(hle, info.byte, arg.info, 8);
    //arg.info += 8;
    assert(0);
  }
  else
  {
    //LABEL5
    union HVQM2Info info1;

    for(; *base != 0; (*base)--)
    {
      dram_load_u8(hle, info1.byte, arg.info, 8);
      arg.info += 8;

      //TODO
      //SUBROUTINE3 (HVQM2Basis)

      info.block.nbase &= 8;
    }

    assert(info.block.nbase == 0);

    //if(info.block.nbase != 0)
    //  LABEL6
  }

  return 1;
}

void hvqm2_decode_sp1_task(struct hle_t* hle)
{
  uint32_t uc_data_ptr = *dmem_u32(hle, TASK_UCODE_DATA);
  uint32_t data_ptr = *dmem_u32(hle, TASK_DATA_PTR);

  (void)uc_data_ptr;
  assert(memcmp(data, dram_u32(hle, uc_data_ptr), sizeof(data)) == 0);
  assert((*dmem_u32(hle, TASK_FLAGS) & 0x1) == 0);

  /* Fill HVQM2Arg struct */
  dram_load_u32(hle, &arg.info, data_ptr, 1); //r13
  data_ptr += 4;
  dram_load_u32(hle, &arg.buf, data_ptr, 1);  //r14
  data_ptr += 4;
  dram_load_u16(hle, &arg.buf_width, data_ptr, 1);  //r18
  data_ptr += 2;
  dram_load_u8(hle, &arg.chroma_step_h, data_ptr, 1);
  data_ptr += 1;
  dram_load_u8(hle, &arg.chroma_step_v, data_ptr, 1); //r24 & r23
  data_ptr += 1;
  dram_load_u16(hle, &arg.hmcus, data_ptr, 1);  //r16
  data_ptr += 2;
  dram_load_u16(hle, &arg.vmcus, data_ptr, 1);  //r17
  data_ptr += 2;
  dram_load_u8(hle, &arg.alpha, data_ptr, 1);
  data_ptr += 1;
  dram_load_u8(hle, arg.nest, data_ptr, HVQM2_NESTSIZE);

  int16_t alpha = ((int16_t)arg.alpha * (uint16_t)constant[0][1]) >> 16;  //v20

  //int length = 0x10;
  //int count = arg.chroma_step_v << 2;
  int skip = arg.buf_width << 1;

  if((arg.chroma_step_v-1) != 0)
  {
    assert(arg.chroma_step_v == 2);
    arg.buf_width <<= 3;
    arg.buf_width += arg.buf_width;
  }

  assert((*hle->sp_status & 0x80) == 0);  //SP_STATUS_YIELD

  for(int i = arg.vmcus; i != 0; i--)
  {
    int j;
    uint32_t out;

    for(j = arg.hmcus, out = arg.buf; j != 0; j--, out += 0x10)
    {
      union VectorResult result;
      uint8_t base = 0x80;  //r9
      uint32_t index = 12; //r20

      if((arg.chroma_step_v - 1) != 0)
      {
        index = 8;
        if(process_info(hle, &base, result.vec[4]) == 0)
          continue;
        if(process_info(hle, &base, result.vec[8]) == 0)
          continue;
      }

      if(process_info(hle, &base, result.vec[6]) == 0)
        continue;
      if(process_info(hle, &base, result.vec[10]) == 0)
        continue;
      if(process_info(hle, &base, result.vec[2]) == 0)
        continue;
      if(process_info(hle, &base, result.vec[0]) == 0)
        continue;

      uint32_t out_buf = out;
      for(int k = 0; k < 4; k++)
      {
        int l;
        int16_t v26[8], v27[8], v28[8];

        for(l = 0; l < 8; l++)
        {
          int16_t v24 = result.vec_half[k+4][l >> 1] - constant[1][3];
          int16_t v25 = result.vec_half[k][l >> 1] - constant[1][3];
          v28[l] = v24 * constant[2][0];
          v27[l] = v24 * constant[2][1];
          v27[l] += v25 * constant[2][2];
          v26[l] = v25 * constant[2][3];
        }

        for(l = arg.chroma_step_v; l != 0; l--)
        {
          int16_t v23[8], v29[8], v30[8], v31[8];

          memcpy(&v23[0], result.vec_half[index], 4 * sizeof(int16_t));
          memcpy(&v23[4], result.vec_half[index+8], 4 * sizeof(int16_t));
          index++;

          for(int m = 0; m < 8; m++)
          {
            v23[m] *= constant[1][2];
            v23[m] += constant[1][1];
            v29[m] = v23[m] + v26[m];
            v30[m] = v23[m] + v27[m];
            v31[m] = v23[m] + v28[m];
            v29[m] = (v29[m] > 0) ? v29[m] : 0;
            v30[m] = (v30[m] > 0) ? v30[m] : 0;
            v31[m] = (v31[m] > 0) ? v31[m] : 0;
            v29[m] = (v29[m] < constant[2][4]) ? v29[m] : constant[2][4];
            v30[m] = (v30[m] < constant[2][4]) ? v30[m] : constant[2][4];
            v31[m] = (v31[m] < constant[2][4]) ? v31[m] : constant[2][4];
            v29[m] &= constant[2][5];
            v30[m] &= constant[2][5];
            v31[m] &= constant[2][5];
            v29[m] = (uint16_t)v29[m] * constant[0][7];
            v30[m] = ((uint16_t)v30[m] * (uint16_t)constant[0][3]) >> 16;
            v31[m] = ((uint16_t)v31[m] * (uint16_t)constant[0][0]) >> 16;
            v29[m] |= alpha;
            v29[m] |= v30[m];
            v29[m] |= v31[m];
          }

          dram_store_u16(hle, (uint16_t*)v29, out_buf, 8);
          out_buf += skip;
        }
      }
    }
    arg.buf += arg.buf_width;
  }

  rsp_break(hle, SP_STATUS_TASKDONE);
}
