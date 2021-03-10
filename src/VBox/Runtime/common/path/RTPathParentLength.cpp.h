/* $Id: RTPathParentLength.cpp.h $ */
/** @file
 * IPRT - RTPathParentLength - Code Template.
 *
 * This file included multiple times with different path style macros.
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

#include "rtpath-root-length-template.cpp.h"

/**
 * @copydoc RTPathParentLengthEx
 */
static size_t RTPATH_STYLE_FN(rtPathParentLength)(const char *pszPath, uint32_t fFlags)
{
    /*
     * Determin the length of the root component so we can make sure
     * we don't try ascend higher than it.
     */
    size_t const cchRoot = RTPATH_STYLE_FN(rtPathRootLengthEx)(pszPath, fFlags);

    /*
     * Rewind to the start of the final component.
     */
    size_t cch = strlen(pszPath);

    /* Trailing slashes: */
    while (cch > cchRoot && RTPATH_IS_SLASH(pszPath[cch - 1]))
        cch--;

    /* The component: */
    while (cch > cchRoot && !RTPATH_IS_SEP(pszPath[cch - 1]))
        cch--;

    /* Done! */
    return cch;
}

