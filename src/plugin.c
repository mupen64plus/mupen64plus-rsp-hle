/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - plugin.c                                        *
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

#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_types.h"
#include "m64p_common.h"
#include "m64p_plugin.h"
#include "main.h"

#define RSP_HLE_VERSION        0x020000
#define RSP_PLUGIN_API_VERSION 0x020000

/* local variables */
static struct hle_t g_hle;
static void (*l_CheckInterrupts)(void) = NULL;
static void (*l_ProcessDlistList)(void) = NULL;
static void (*l_ProcessAlistList)(void) = NULL;
static void (*l_ProcessRdpList)(void) = NULL;
static void (*l_ShowCFB)(void) = NULL;
static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;
static int l_PluginInit = 0;

/* Global functions */
void DebugMessage(int level, const char *message, ...)
{
    char msgbuf[1024];
    va_list args;

    if (l_DebugCallback == NULL)
        return;

    va_start(args, message);
    vsprintf(msgbuf, message, args);

    (*l_DebugCallback)(l_DebugCallContext, level, msgbuf);

    va_end(args);
}

void CheckInterrupts(void)
{
    if (l_CheckInterrupts == NULL)
        return;

    (*l_CheckInterrupts)();
}

void ProcessDlistList(void)
{
    if (l_ProcessDlistList == NULL)
        return;

    (*l_ProcessDlistList)();
}

void ProcessAlistList(void)
{
    if (l_ProcessAlistList == NULL)
        return;

    (*l_ProcessAlistList)();
}

void ProcessRdpList(void)
{
    if (l_ProcessRdpList == NULL)
        return;

    (*l_ProcessRdpList)();
}

void ShowCFB(void)
{
    if (l_ShowCFB == NULL)
        return;

    (*l_ShowCFB)();
}


/* DLL-exported functions */
EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                     void (*DebugCallback)(void *, int, const char *))
{
    if (l_PluginInit)
        return M64ERR_ALREADY_INIT;

    /* first thing is to set the callback function for debug info */
    l_DebugCallback = DebugCallback;
    l_DebugCallContext = Context;

    /* this plugin doesn't use any Core library functions (ex for Configuration), so no need to keep the CoreLibHandle */

    l_PluginInit = 1;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (!l_PluginInit)
        return M64ERR_NOT_INIT;

    /* reset some local variable */
    l_DebugCallback = NULL;
    l_DebugCallContext = NULL;

    l_PluginInit = 0;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    /* set version info */
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_RSP;

    if (PluginVersion != NULL)
        *PluginVersion = RSP_HLE_VERSION;

    if (APIVersion != NULL)
        *APIVersion = RSP_PLUGIN_API_VERSION;

    if (PluginNamePtr != NULL)
        *PluginNamePtr = "Hacktarux/Azimer High-Level Emulation RSP Plugin";

    if (Capabilities != NULL)
        *Capabilities = 0;

    return M64ERR_SUCCESS;
}

EXPORT unsigned int CALL DoRspCycles(unsigned int Cycles)
{
    hle_execute(&g_hle);
    return Cycles;
}

EXPORT void CALL InitiateRSP(RSP_INFO Rsp_Info, unsigned int *CycleCount)
{
    g_hle.dram = Rsp_Info.RDRAM;
    g_hle.dmem = Rsp_Info.DMEM;
    g_hle.imem = Rsp_Info.IMEM;

    g_hle.mi_intr = Rsp_Info.MI_INTR_REG;

    g_hle.sp_mem_addr = Rsp_Info.SP_MEM_ADDR_REG;
    g_hle.sp_dram_addr = Rsp_Info.SP_DRAM_ADDR_REG;
    g_hle.sp_rd_length = Rsp_Info.SP_RD_LEN_REG;
    g_hle.sp_wr_length = Rsp_Info.SP_WR_LEN_REG;
    g_hle.sp_status = Rsp_Info.SP_STATUS_REG;
    g_hle.sp_dma_full = Rsp_Info.SP_DMA_FULL_REG;
    g_hle.sp_dma_busy = Rsp_Info.SP_DMA_BUSY_REG;
    g_hle.sp_pc = Rsp_Info.SP_PC_REG;
    g_hle.sp_semaphore = Rsp_Info.SP_SEMAPHORE_REG;

    g_hle.dpc_start = Rsp_Info.DPC_START_REG;
    g_hle.dpc_end = Rsp_Info.DPC_END_REG;
    g_hle.dpc_current = Rsp_Info.DPC_CURRENT_REG;
    g_hle.dpc_status = Rsp_Info.DPC_STATUS_REG;
    g_hle.dpc_clock = Rsp_Info.DPC_CLOCK_REG;
    g_hle.dpc_bufbusy = Rsp_Info.DPC_BUFBUSY_REG;
    g_hle.dpc_pipebusy = Rsp_Info.DPC_PIPEBUSY_REG;
    g_hle.dpc_tmem = Rsp_Info.DPC_TMEM_REG;

    l_CheckInterrupts = Rsp_Info.CheckInterrupts;
    l_ProcessDlistList = Rsp_Info.ProcessDlistList;
    l_ProcessAlistList = Rsp_Info.ProcessAlistList;
    l_ProcessRdpList = Rsp_Info.ProcessRdpList;
    l_ShowCFB = Rsp_Info.ShowCFB;
}

EXPORT void CALL RomClosed(void)
{
    memset(g_hle.dmem, 0, 0x1000);
    memset(g_hle.imem, 0, 0x1000);
}
