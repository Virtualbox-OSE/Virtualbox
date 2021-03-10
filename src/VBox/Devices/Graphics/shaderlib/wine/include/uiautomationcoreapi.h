/*
 * Copyright 2012 Jacek Caban for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
 * Oracle LGPL Disclaimer: For the avoidance of doubt, except that if any license choice
 * other than GPL or LGPL is available it will apply instead, Oracle elects to use only
 * the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where
 * a choice of LGPL license versions is made available with the language indicating
 * that LGPLv2 or any later version may be used, or where a choice of which version
 * of the LGPL is applied is otherwise unspecified.
 */

#ifndef _INC_UIAUTOMATIONCOREAPI
#define _INC_UIAUTOMATIONCOREAPI

#ifdef __cplusplus
extern "C" {
#endif

#define UIA_E_ELEMENTNOTENABLED       0x80040200
#define UIA_E_ELEMENTNOTAVAILABLE     0x80040201
#define UIA_E_NOCLICKABLEPOINT        0x80040202
#define UIA_E_PROXYASSEMBLYNOTLOADED  0x80040203
#define UIA_E_NOTSUPPORTED            0x80040204
#define UIA_E_INVALIDOPERATION        0x80131509
#define UIA_E_TIMEOUT                 0x80131505

#define UiaAppendRuntimeId  3
#define UiaRootObjectId     -25

DECLARE_HANDLE(HUIANODE);
DECLARE_HANDLE(HUIAPATTERNOBJECT);
DECLARE_HANDLE(HUIATEXTRANGE);
DECLARE_HANDLE(HUIAEVENT);

BOOL WINAPI UiaPatternRelease(HUIAPATTERNOBJECT hobj);
BOOL WINAPI UiaTextRangeRelease(HUIATEXTRANGE hobj);

#ifdef __cplusplus
}
#endif

#endif /* _INC_UIAUTOMATIONCOREAPI */
