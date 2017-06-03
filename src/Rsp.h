/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*   Mupen64plus-rsp-hle - Rsp.h                                           *
*   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
*   Copyright (C) 2014 Bobby Smiles                                       *
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


#pragma once

#include "Common.h"

#if defined(__cplusplus)
extern "C" {
#endif

	typedef struct
	{
		int hInst;
		int MemoryBswaped;    /* If this is set to TRUE, then the memory has been pre
							  bswap on a dword (32 bits) boundry */

		unsigned char * RDRAM;
		unsigned char * DMEM;
		unsigned char * IMEM;

		unsigned long * MI_INTR_REG;

		unsigned long * SP_MEM_ADDR_REG;
		unsigned long * SP_DRAM_ADDR_REG;
		unsigned long * SP_RD_LEN_REG;
		unsigned long * SP_WR_LEN_REG;
		unsigned long * SP_STATUS_REG;
		unsigned long * SP_DMA_FULL_REG;
		unsigned long * SP_DMA_BUSY_REG;
		unsigned long * SP_PC_REG;
		unsigned long * SP_SEMAPHORE_REG;


		unsigned long * DPC_START_REG;
		unsigned long * DPC_END_REG;
		unsigned long * DPC_CURRENT_REG;
		unsigned long * DPC_STATUS_REG;
		unsigned long * DPC_CLOCK_REG;
		unsigned long * DPC_BUFBUSY_REG;
		unsigned long * DPC_PIPEBUSY_REG;
		unsigned long * DPC_TMEM_REG;

		void(*CheckInterrupts)(void);
		void(*ProcessDList)(void);
		void(*ProcessAList)(void);
		void(*ProcessRdpList)(void);
		void(*ShowCFB)(void);

	} RSP_INFO;

	void  _RSP_M64p_CloseDLL(void);
	void  _RSP_M64p_DllAbout(int hParent);
	unsigned long _RSP_M64p_DoRspCycles(unsigned long Cycles);
	void  _RSP_M64p_GetDllInfo(PLUGIN_INFO * PluginInfo);
	void  _RSP_M64p_InitiateRSP(RSP_INFO Rsp_Info, unsigned long * CycleCount);
	void  _RSP_M64p_RomClosed(void);
	void  _RSP_M64p_DllConfig(int hWnd);

#if defined(__cplusplus)
}

#endif

