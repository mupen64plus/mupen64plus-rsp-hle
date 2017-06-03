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
#include <stdio.h>

#include "common.h"
#include "hle.h"
#include "hle_internal.h"

#ifndef _PJ64_SPEC
#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_common.h"
#include "m64p_plugin.h"
#include "m64p_types.h"
#else
#include "Rsp.h"
#if defined(_WIN32)
#define EXPORT      __declspec(dllexport)
#define CALL        __cdecl
#else
#define EXPORT      __attribute__((visibility("default")))
#define CALL
#endif
#endif

#define RSP_HLE_VERSION        0x020500
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
#ifndef _PJ64_SPEC
static int l_PluginInit = 0;
#endif

/* local function */
static void DebugMessage(int level, const char *message, va_list args)
{
    char msgbuf[1024];

    if (l_DebugCallback == NULL)
        return;

    vsprintf(msgbuf, message, args);

    (*l_DebugCallback)(l_DebugCallContext, level, msgbuf);
}

/* Global functions needed by HLE core */
void HleVerboseMessage(void* UNUSED(user_defined), const char *message, ...)
{
    va_list args;
    va_start(args, message);
    //DebugMessage(M64MSG_VERBOSE, message, args);
    va_end(args);
}

void HleErrorMessage(void* UNUSED(user_defined), const char *message, ...)
{
    va_list args;
    va_start(args, message);
    //DebugMessage(M64MSG_ERROR, message, args);
    va_end(args);
}

void HleWarnMessage(void* UNUSED(user_defined), const char *message, ...)
{
    va_list args;
    va_start(args, message);
    //DebugMessage(M64MSG_WARNING, message, args);
    va_end(args);
}

void HleCheckInterrupts(void* UNUSED(user_defined))
{
    if (l_CheckInterrupts == NULL)
        return;

    (*l_CheckInterrupts)();
}

void HleProcessDlistList(void* UNUSED(user_defined))
{
    if (l_ProcessDlistList == NULL)
        return;

    (*l_ProcessDlistList)();
}

void HleProcessAlistList(void* UNUSED(user_defined))
{
    if (l_ProcessAlistList == NULL)
        return;

    (*l_ProcessAlistList)();
}

void HleProcessRdpList(void* UNUSED(user_defined))
{
    if (l_ProcessRdpList == NULL)
        return;

    (*l_ProcessRdpList)();
}

void HleShowCFB(void* UNUSED(user_defined))
{
    if (l_ShowCFB == NULL)
        return;

    (*l_ShowCFB)();
}


/* DLL-exported functions */
#ifndef _PJ64_SPEC
EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle UNUSED(CoreLibHandle), void *Context,
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
#endif

EXPORT uint32_t CALL DoRspCycles(uint32_t Cycles)
{
    hle_execute(&g_hle);
    return Cycles;
}

EXPORT void CALL InitiateRSP(RSP_INFO Rsp_Info, unsigned int* UNUSED(CycleCount))
{
    hle_init(&g_hle,
             Rsp_Info.RDRAM,
             Rsp_Info.DMEM,
             Rsp_Info.IMEM,
             Rsp_Info.MI_INTR_REG,
             Rsp_Info.SP_MEM_ADDR_REG,
             Rsp_Info.SP_DRAM_ADDR_REG,
             Rsp_Info.SP_RD_LEN_REG,
             Rsp_Info.SP_WR_LEN_REG,
             Rsp_Info.SP_STATUS_REG,
             Rsp_Info.SP_DMA_FULL_REG,
             Rsp_Info.SP_DMA_BUSY_REG,
             Rsp_Info.SP_PC_REG,
             Rsp_Info.SP_SEMAPHORE_REG,
             Rsp_Info.DPC_START_REG,
             Rsp_Info.DPC_END_REG,
             Rsp_Info.DPC_CURRENT_REG,
             Rsp_Info.DPC_STATUS_REG,
             Rsp_Info.DPC_CLOCK_REG,
             Rsp_Info.DPC_BUFBUSY_REG,
             Rsp_Info.DPC_PIPEBUSY_REG,
             Rsp_Info.DPC_TMEM_REG,
             NULL);

    l_CheckInterrupts = Rsp_Info.CheckInterrupts;
#ifdef _PJ64_SPEC
    l_ProcessDlistList = Rsp_Info.ProcessDList;
    l_ProcessAlistList = Rsp_Info.ProcessAList;
#else
    l_ProcessDlistList = Rsp_Info.ProcessDlistList;
    l_ProcessAlistList = Rsp_Info.ProcessAlistList;
#endif
    l_ProcessRdpList = Rsp_Info.ProcessRdpList;
    l_ShowCFB = Rsp_Info.ShowCFB;
}

#ifdef _PJ64_SPEC
EXPORT void CALL CloseDLL(void)
{
    /* do nothing */
}
EXPORT void CALL GetDllInfo(PLUGIN_INFO * PluginInfo)
{
    PluginInfo->Version = 0x0101;
	PluginInfo->Type = PLUGIN_TYPE_RSP;
	//strcpy(PluginInfo->Name, "Mupen64Plus HLE RSP Plugin");
	PluginInfo->NormalMemory = 1;
	PluginInfo->MemoryBswaped = 1;
}
EXPORT void CALL DllConfig(int hWnd)
{
    /* do nothing */
}
#endif

EXPORT void CALL RomClosed(void)
{
    /* do nothing */
}
