/* $Id: SUPR3HardenedMain-darwin.cpp $ */
/** @file
 * VirtualBox Support Library - Hardened main(), posix bits.
 */

/*
 * Copyright (C) 2017-2020 Oracle Corporation
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
#include <VBox/err.h>
#include <VBox/sup.h>

#include <iprt/path.h>
#include <iprt/string.h>

#include <dlfcn.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/sysctl.h> /* sysctlbyname() */
#include <stdio.h>
#include <stdint.h>
#include <mach-o/dyld.h>

#include "SUPLibInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Interpose table entry.
 */
typedef struct DYLDINTERPOSE
{
    /** The symbol address to replace with. */
    const void *pvReplacement;
    /** The replaced symbol address. */
    const void *pvReplacee;
} DYLDINTERPOSE;
/** Pointer to an interposer table entry. */
typedef DYLDINTERPOSE *PDYLDINTERPOSE;
/** Pointer to a const interposer table entry. */
typedef const DYLDINTERPOSE *PCDYLDINTERPOSE;

/** @sa dyld_dynamic_interpose(). */
typedef const mach_header * FNDYLDDYNAMICINTERPOSE(const struct mach_header* mh, PCDYLDINTERPOSE paSym, size_t cSyms);
typedef FNDYLDDYNAMICINTERPOSE *PFNDYLDDYNAMICINTERPOSE;

/** @sa dlopen(). */
typedef void *FNDLOPEN(const char *path, int mode);
typedef FNDLOPEN *PFNDLOPEN;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

extern "C" void _dyld_register_func_for_add_image(void (*func)(const struct mach_header* mh, intptr_t vmaddr_slide));

static void * supR3HardenedDarwinDlopenInterpose(const char *path, int mode);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Flag whether macOS 11.x (BigSur) was detected. */
static bool                    g_fMacOs11 = false;
/** Resolved dyld_dynamic_interpose() value. */
static PFNDYLDDYNAMICINTERPOSE g_pfnDyldDynamicInterpose = NULL;
/** Pointer to the real dlopen() function used from the interposer when verification succeeded. */
static PFNDLOPEN               g_pfnDlopenReal = NULL;
/**
 * The interposer table.
 */
static const DYLDINTERPOSE     g_aInterposers[] =
{
    { (const void *)(uintptr_t)&supR3HardenedDarwinDlopenInterpose, (const void *)(uintptr_t)&dlopen }
};


/**
 * dlopen() interposer which verifies that the path to be loaded meets the criteria for hardened builds.
 *
 * @sa dlopen() man page.
 */
static void * supR3HardenedDarwinDlopenInterpose(const char *path, int mode)
{
    /*
     * Giving NULL as the filename indicates opening the main program which is fine
     * We are already loaded and executing after all.
     *
     * Filenames without any path component (whether absolute or relative) are allowed
     * unconditionally too as the loader will only search the default paths configured by root.
     */
    if (   path
        && strchr(path, '/') != NULL)
    {
        int rc = VINF_SUCCESS;

        /*
         * Starting with macOS 11.0 (BigSur) system provided libraries
         * under /System/Libraries are not stored on the filesystem anymore
         * but in a dynamic linker cache. The integrity of the linker cache
         * is maintained by the system and dyld. Our verification code fails because
         * it can't find the file.
         * The obvious solution is to exclude paths starting with /System/Libraries
         * when we run on BigSur. Other paths are still subject to verification.
         */
        if (   !g_fMacOs11
            || strncmp(path, RT_STR_TUPLE("/System/Library")))
            rc = supR3HardenedVerifyFileFollowSymlinks(path, RTHCUINTPTR_MAX, true /* fMaybe3rdParty */,
                                                       NULL /* pErrInfo */);
        if (RT_FAILURE(rc))
            return NULL;
    }

    return g_pfnDlopenReal(path, mode);
}


/**
 * Callback to get notified of new images being loaded to be able to apply our dlopn() interposer.
 *
 * @returns nothing.
 * @param   mh              Pointer to the mach header of the loaded image.
 * @param   vmaddr_slide    The slide value for ASLR.
 */
static DECLCALLBACK(void) supR3HardenedDarwinAddImage(const struct mach_header* mh, intptr_t vmaddr_slide)
{
    RT_NOREF(vmaddr_slide);

    g_pfnDyldDynamicInterpose((const struct mach_header*)mh, &g_aInterposers[0], RT_ELEMENTS(g_aInterposers));
}


/**
 * Hardening initialization for macOS hosts.
 *
 * @returns nothing.
 *
 * @note Doesn't return on error.
 */
DECLHIDDEN(void) supR3HardenedDarwinInit(void)
{
    /*
     * Check whether we are running on macOS BigSur by checking kern.osproductversion
     * available since some point in 2018.
     */
    char szVers[256]; RT_ZERO(szVers);
    size_t cbVers = sizeof(szVers);
    int rc = sysctlbyname("kern.osproductversion", &szVers[0], &cbVers, NULL, 0);
    if (   !rc
        && !memcmp(&szVers[0], RT_STR_TUPLE("10.16")))
        g_fMacOs11 = true;

    /* Saved to call real dlopen() later on, as we will interpose dlopen() from the main binary in the next step as well. */
    g_pfnDlopenReal = (PFNDLOPEN)dlsym(RTLD_DEFAULT, "dlopen");
    g_pfnDyldDynamicInterpose = (PFNDYLDDYNAMICINTERPOSE)dlsym(RTLD_DEFAULT, "dyld_dynamic_interpose");
    if (!g_pfnDyldDynamicInterpose)
        supR3HardenedFatalMsg("supR3HardenedDarwinInit", kSupInitOp_Integrity, VERR_SYMBOL_NOT_FOUND,
                              "Failed to find dyld_dynamic_interpose()");

    /*
     * The following will causes our add image notification to be called for all images loaded so far.
     * The callback will set up the interposer.
     */
    _dyld_register_func_for_add_image(supR3HardenedDarwinAddImage);
}



/*
 * assert.cpp
 *
 * ASSUMES working DECLHIDDEN or there will be symbol confusion!
 */

RTDATADECL(char)                     g_szRTAssertMsg1[1024];
RTDATADECL(char)                     g_szRTAssertMsg2[4096];
RTDATADECL(const char * volatile)    g_pszRTAssertExpr;
RTDATADECL(const char * volatile)    g_pszRTAssertFile;
RTDATADECL(uint32_t volatile)        g_u32RTAssertLine;
RTDATADECL(const char * volatile)    g_pszRTAssertFunction;

RTDECL(bool) RTAssertMayPanic(void)
{
    return true;
}


RTDECL(void) RTAssertMsg1(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    /*
     * Fill in the globals.
     */
    g_pszRTAssertExpr       = pszExpr;
    g_pszRTAssertFile       = pszFile;
    g_pszRTAssertFunction   = pszFunction;
    g_u32RTAssertLine       = uLine;
    snprintf(g_szRTAssertMsg1, sizeof(g_szRTAssertMsg1),
             "\n!!Assertion Failed!!\n"
             "Expression: %s\n"
             "Location  : %s(%u) %s\n",
             pszExpr, pszFile, uLine, pszFunction);
}


RTDECL(void) RTAssertMsg2V(const char *pszFormat, va_list va)
{
    vsnprintf(g_szRTAssertMsg2, sizeof(g_szRTAssertMsg2), pszFormat, va);
    if (g_enmSupR3HardenedMainState < SUPR3HARDENEDMAINSTATE_CALLED_TRUSTED_MAIN)
        supR3HardenedFatalMsg(g_pszRTAssertExpr, kSupInitOp_Misc, VERR_INTERNAL_ERROR,
                              "%s%s", g_szRTAssertMsg1,  g_szRTAssertMsg2);
    else
        supR3HardenedError(VERR_INTERNAL_ERROR, false/*fFatal*/, "%s%s", g_szRTAssertMsg1,  g_szRTAssertMsg2);
}

