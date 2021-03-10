/* $Id: RTSystemFirmware-win.cpp $ */
/** @file
 * IPRT - System firmware information, Win32.
 */

/*
 * Copyright (C) 2019-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/system.h>

#include <iprt/nt/nt-and-windows.h>
#include <WinSDKVer.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/ldr.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#include "internal-r3-win.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#if _WIN32_MAXVER < 0x0602 /* Windows 7 or older, supply missing GetFirmwareType bits. */
typedef enum _FIRMWARE_TYPE
{
    FirmwareTypeUnknown,
    FirmwareTypeBios,
    FirmwareTypeUefi,
    FirmwareTypeMax
} FIRMWARE_TYPE;
typedef FIRMWARE_TYPE *PFIRMWARE_TYPE;
WINBASEAPI BOOL WINAPI GetFirmwareType(PFIRMWARE_TYPE);
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Defines the UEFI Globals UUID. */
#define VBOX_UEFI_UUID_GLOBALS L"{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}"
/** Defines an UEFI dummy UUID, see MSDN docs of the API. */
#define VBOX_UEFI_UUID_DUMMY   L"{00000000-0000-0000-0000-000000000000}"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static volatile bool                              g_fResolvedApis = false;
static decltype(GetFirmwareType)                 *g_pfnGetFirmwareType;
static decltype(GetFirmwareEnvironmentVariableW) *g_pfnGetFirmwareEnvironmentVariableW;


static void rtSystemFirmwareResolveApis(void)
{
    FARPROC pfnTmp1 = GetProcAddress(g_hModKernel32, "GetFirmwareType");
    FARPROC pfnTmp2 = GetProcAddress(g_hModKernel32, "GetFirmwareEnvironmentVariableW");
    ASMCompilerBarrier(); /* paranoia^2 */

    g_pfnGetFirmwareType                 = (decltype(GetFirmwareType) *)pfnTmp1;
    g_pfnGetFirmwareEnvironmentVariableW = (decltype(GetFirmwareEnvironmentVariableW) *)pfnTmp2;
    ASMAtomicWriteBool(&g_fResolvedApis, true);
}


static int rtSystemFirmwareGetPrivileges(LPCTSTR pcszPrivilege)
{
    HANDLE hToken;
    BOOL fRc = OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken);
    if (!fRc)
        return RTErrConvertFromWin32(GetLastError());

    int rc = VINF_SUCCESS;

    TOKEN_PRIVILEGES tokenPriv;
    fRc = LookupPrivilegeValue(NULL, pcszPrivilege, &tokenPriv.Privileges[0].Luid);
    if (fRc)
    {
        tokenPriv.PrivilegeCount = 1;
        tokenPriv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        fRc = AdjustTokenPrivileges(hToken, FALSE, &tokenPriv, 0, (PTOKEN_PRIVILEGES)NULL, 0);
        if (!fRc)
            rc = RTErrConvertFromWin32(GetLastError());
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());

    CloseHandle(hToken);

    return rc;
}


RTDECL(int) RTSystemQueryFirmwareType(PRTSYSFWTYPE penmFirmwareType)
{
    AssertPtrReturn(penmFirmwareType, VERR_INVALID_POINTER);

    if (!g_fResolvedApis)
        rtSystemFirmwareResolveApis();

    *penmFirmwareType = RTSYSFWTYPE_INVALID;
    int rc = VERR_NOT_SUPPORTED;

    /* GetFirmwareType is Windows 8 and later. */
    if (g_pfnGetFirmwareType)
    {
        FIRMWARE_TYPE enmWinFwType;
        if (g_pfnGetFirmwareType(&enmWinFwType))
        {
            switch (enmWinFwType)
            {
                case FirmwareTypeBios:
                    *penmFirmwareType = RTSYSFWTYPE_BIOS;
                    break;
                case FirmwareTypeUefi:
                    *penmFirmwareType = RTSYSFWTYPE_UEFI;
                    break;
                default:
                    *penmFirmwareType = RTSYSFWTYPE_UNKNOWN;
                    AssertMsgFailed(("%d\n", enmWinFwType));
                    break;
            }
            rc = VINF_SUCCESS;
        }
        else
            rc = RTErrConvertFromWin32(GetLastError());
    }
    /* GetFirmwareEnvironmentVariableW is XP and later. */
    else if (g_pfnGetFirmwareEnvironmentVariableW)
    {
        rtSystemFirmwareGetPrivileges(SE_SYSTEM_ENVIRONMENT_NAME);

        /* On a non-UEFI system (or such a system in legacy boot mode), we will get
           back ERROR_INVALID_FUNCTION when querying any firmware variable.  While on a
           UEFI system we'll typically get ERROR_ACCESS_DENIED or similar as the dummy
           is a non-exising dummy namespace.  See the API docs. */
        SetLastError(0);
        uint8_t abWhatever[64];
        DWORD cbRet = g_pfnGetFirmwareEnvironmentVariableW(L"", VBOX_UEFI_UUID_DUMMY, abWhatever, sizeof(abWhatever));
        DWORD dwErr = GetLastError();
        *penmFirmwareType = cbRet != 0 || dwErr != ERROR_INVALID_FUNCTION ? RTSYSFWTYPE_UEFI : RTSYSFWTYPE_BIOS;
        rc = VINF_SUCCESS;
    }
    return rc;
}


RTDECL(int) RTSystemQueryFirmwareBoolean(RTSYSFWBOOL enmBoolean, bool *pfValue)
{
    *pfValue = false;

    /*
     * Translate the enmBoolean to a name:
     */
    const wchar_t *pwszName = NULL;
    switch (enmBoolean)
    {
        case RTSYSFWBOOL_SECURE_BOOT:
            pwszName = L"SecureBoot";
            break;

        default:
            AssertReturn(enmBoolean > RTSYSFWBOOL_INVALID && enmBoolean < RTSYSFWBOOL_END, VERR_INVALID_PARAMETER);
            return VERR_SYS_UNSUPPORTED_FIRMWARE_PROPERTY;
    }

    /*
     * Do the query.
     * Note! This will typically fail with access denied unless we're in an elevated process.
     */
    if (!g_pfnGetFirmwareEnvironmentVariableW)
        return VERR_NOT_SUPPORTED;
    rtSystemFirmwareGetPrivileges(SE_SYSTEM_ENVIRONMENT_NAME);

    uint8_t bValue = 0;
    DWORD cbRet = g_pfnGetFirmwareEnvironmentVariableW(pwszName, VBOX_UEFI_UUID_GLOBALS, &bValue, sizeof(bValue));
    *pfValue = cbRet != 0 && bValue != 0;
    if (cbRet != 0)
        return VINF_SUCCESS;
    DWORD dwErr = GetLastError();
    if (   dwErr == ERROR_INVALID_FUNCTION
        || dwErr == ERROR_ENVVAR_NOT_FOUND)
        return VINF_SUCCESS;
    return RTErrConvertFromWin32(dwErr);
}

