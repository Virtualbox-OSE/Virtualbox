; $Id: RTStrMemFind32.asm $
;; @file
; IPRT - RTStrMemFind32 - AMD64 & X86.
;

;
; Copyright (C) 2019-2020 Oracle Corporation
;
; This file is part of VirtualBox Open Source Edition (OSE), as
; available from http://www.virtualbox.org. This file is free software;
; you can redistribute it and/or modify it under the terms of the GNU
; General Public License (GPL) as published by the Free Software
; Foundation, in version 2 as it comes in the "COPYING" file of the
; VirtualBox OSE distribution. VirtualBox OSE is distributed in the
; hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
;
; The contents of this file may alternatively be used under the terms
; of the Common Development and Distribution License Version 1.0
; (CDDL) only, as it comes in the "COPYING.CDDL" file of the
; VirtualBox OSE distribution, in which case the provisions of the
; CDDL are applicable instead of those of the GPL.
;
; You may elect to license modified versions of this file under the
; terms and conditions of either the GPL or the CDDL or both.
;

%include "iprt/asmdefs.mac"

BEGINCODE

;;
;
; This is just a 32-bit memchr.
;
; @param    pvHaystack      gcc: rdi  msc: ecx  x86:[esp+4]   wcall: eax
; @param    uNeedle         gcc: esi  msc: edx  x86:[esp+8]   wcall: edx
; @param    cbHaystack      gcc: rdx  msc: r8   x86:[esp+0ch] wcall: ebx
BEGINPROC_EXPORTED RTStrMemFind32
        cld
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
        mov     r9, rdi                 ; save rdi
        mov     eax, edx
        mov     rdi, rcx
        mov     rcx, r8
 %else
        mov     rcx, rdx
        mov     eax, esi
 %endif
%else
 %ifdef ASM_CALL32_WATCOM
        mov     ecx, ebx
        xchg    eax, edx
        xchg    edi, edx                ; load and save edi.
 %else
        mov     ecx, [esp + 0ch]
        mov     edx, edi                ; save edi
        mov     eax, [esp + 8]
        mov     edi, [esp + 4]
 %endif
%endif
        cmp     xCX, 4
        jb      .not_found

        ; do the search
        repne   scasd
        jne     .not_found

        ; found it
        lea     xAX, [xDI - 4]
%ifdef ASM_CALL64_MSC
        mov     rdi, r9
%endif
%ifdef RT_ARCH_X86
        mov     edi, edx
%endif
        ret

.not_found:
%ifdef ASM_CALL64_MSC
        mov     rdi, r9
%endif
%ifdef RT_ARCH_X86
        mov     edi, edx
%endif
        xor     eax, eax
        ret
ENDPROC RTStrMemFind32

