/*
 * Performance Monitor
 *
 * Copyright 2007 Hans Leidekker
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

#ifndef _WINPERF_
#define _WINPERF_

#define PERF_SIZE_DWORD         0x00000000
#define PERF_SIZE_LARGE         0x00000100
#define PERF_SIZE_ZERO          0x00000200
#define PERF_SIZE_VARIABLE_LEN  0x00000300

#define PERF_TYPE_NUMBER        0x00000000
#define PERF_TYPE_COUNTER       0x00000400
#define PERF_TYPE_TEXT          0x00000800
#define PERF_TYPE_ZERO          0x00000C00

#define PERF_NUMBER_HEX         0x00000000
#define PERF_NUMBER_DECIMAL     0x00010000
#define PERF_NUMBER_DEC_1000    0x00020000

#define PERF_COUNTER_VALUE      0x00000000
#define PERF_COUNTER_RATE       0x00010000
#define PERF_COUNTER_FRACTION   0x00020000
#define PERF_COUNTER_BASE       0x00030000
#define PERF_COUNTER_ELAPSED    0x00040000
#define PERF_COUNTER_QUEUELEN   0x00050000
#define PERF_COUNTER_HISTOGRAM  0x00060000
#define PERF_COUNTER_PRECISION  0x00070000

#define PERF_TEXT_UNICODE       0x00000000
#define PERF_TEXT_ASCII         0x00010000

#define PERF_TIMER_TICK         0x00000000
#define PERF_TIMER_100NS        0x00100000
#define PERF_OBJECT_TIMER       0x00200000

#define PERF_DELTA_COUNTER      0x00400000
#define PERF_DELTA_BASE         0x00800000
#define PERF_INVERSE_COUNTER    0x01000000
#define PERF_MULTI_COUNTER      0x02000000

#define PERF_DISPLAY_NO_SUFFIX  0x00000000
#define PERF_DISPLAY_PER_SEC    0x10000000
#define PERF_DISPLAY_PERCENT    0x20000000
#define PERF_DISPLAY_SECONDS    0x30000000
#define PERF_DISPLAY_NOSHOW     0x40000000

#define PERF_DETAIL_NOVICE      100
#define PERF_DETAIL_ADVANCED    200
#define PERF_DETAIL_EXPERT      300
#define PERF_DETAIL_WIZARD      400

#endif /* _WINPERF_ */
