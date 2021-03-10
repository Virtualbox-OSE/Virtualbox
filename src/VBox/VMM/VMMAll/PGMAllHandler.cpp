/* $Id: PGMAllHandler.cpp $ */
/** @file
 * PGM - Page Manager / Monitor, Access Handlers.
 */

/*
 * Copyright (C) 2006-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_PGM
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/dbgf.h>
#ifdef IN_RING0
# include <VBox/vmm/pdmdev.h>
#endif
#include "PGMInternal.h"
#include <VBox/vmm/vmcc.h>
#include "PGMInline.h"

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/string.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/vmm/selm.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int  pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs(PVMCC pVM, PPGMPHYSHANDLER pCur, PPGMRAMRANGE pRam);
static void pgmHandlerPhysicalDeregisterNotifyREMAndNEM(PVMCC pVM, PPGMPHYSHANDLER pCur, int fRestoreRAM);
static void pgmHandlerPhysicalResetRamFlags(PVMCC pVM, PPGMPHYSHANDLER pCur);


/**
 * Internal worker for releasing a physical handler type registration reference.
 *
 * @returns New reference count. UINT32_MAX if invalid input (asserted).
 * @param   pVM         The cross context VM structure.
 * @param   pType       Pointer to the type registration.
 */
DECLINLINE(uint32_t) pgmHandlerPhysicalTypeRelease(PVMCC pVM, PPGMPHYSHANDLERTYPEINT pType)
{
    AssertMsgReturn(pType->u32Magic == PGMPHYSHANDLERTYPEINT_MAGIC, ("%#x\n", pType->u32Magic), UINT32_MAX);
    uint32_t cRefs = ASMAtomicDecU32(&pType->cRefs);
    if (cRefs == 0)
    {
        pgmLock(pVM);
        pType->u32Magic = PGMPHYSHANDLERTYPEINT_MAGIC_DEAD;
        RTListOff32NodeRemove(&pType->ListNode);
        pgmUnlock(pVM);
        MMHyperFree(pVM, pType);
    }
    return cRefs;
}


/**
 * Internal worker for retaining a physical handler type registration reference.
 *
 * @returns New reference count. UINT32_MAX if invalid input (asserted).
 * @param   pVM         The cross context VM structure.
 * @param   pType       Pointer to the type registration.
 */
DECLINLINE(uint32_t) pgmHandlerPhysicalTypeRetain(PVM pVM, PPGMPHYSHANDLERTYPEINT pType)
{
    NOREF(pVM);
    AssertMsgReturn(pType->u32Magic == PGMPHYSHANDLERTYPEINT_MAGIC, ("%#x\n", pType->u32Magic), UINT32_MAX);
    uint32_t cRefs = ASMAtomicIncU32(&pType->cRefs);
    Assert(cRefs < _1M && cRefs > 0);
    return cRefs;
}


/**
 * Releases a reference to a physical handler type registration.
 *
 * @returns New reference count. UINT32_MAX if invalid input (asserted).
 * @param   pVM         The cross context VM structure.
 * @param   hType       The type regiration handle.
 */
VMMDECL(uint32_t) PGMHandlerPhysicalTypeRelease(PVMCC pVM, PGMPHYSHANDLERTYPE hType)
{
    if (hType != NIL_PGMPHYSHANDLERTYPE)
        return pgmHandlerPhysicalTypeRelease(pVM, PGMPHYSHANDLERTYPEINT_FROM_HANDLE(pVM, hType));
    return 0;
}


/**
 * Retains a reference to a physical handler type registration.
 *
 * @returns New reference count. UINT32_MAX if invalid input (asserted).
 * @param   pVM         The cross context VM structure.
 * @param   hType       The type regiration handle.
 */
VMMDECL(uint32_t) PGMHandlerPhysicalTypeRetain(PVM pVM, PGMPHYSHANDLERTYPE hType)
{
    return pgmHandlerPhysicalTypeRetain(pVM, PGMPHYSHANDLERTYPEINT_FROM_HANDLE(pVM, hType));
}


/**
 * Creates a physical access handler.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when successfully installed.
 * @retval  VINF_PGM_GCPHYS_ALIASED when the shadow PTs could be updated because
 *          the guest page aliased or/and mapped by multiple PTs. A CR3 sync has been
 *          flagged together with a pool clearing.
 * @retval  VERR_PGM_HANDLER_PHYSICAL_CONFLICT if the range conflicts with an existing
 *          one. A debug assertion is raised.
 *
 * @param   pVM             The cross context VM structure.
 * @param   hType           The handler type registration handle.
 * @param   pvUserR3        User argument to the R3 handler.
 * @param   pvUserR0        User argument to the R0 handler.
 * @param   pvUserRC        User argument to the RC handler. This can be a value
 *                          less that 0x10000 or a (non-null) pointer that is
 *                          automatically relocated.
 * @param   pszDesc         Description of this handler.  If NULL, the type
 *                          description will be used instead.
 * @param   ppPhysHandler   Where to return the access handler structure on
 *                          success.
 */
int pgmHandlerPhysicalExCreate(PVMCC pVM, PGMPHYSHANDLERTYPE hType, RTR3PTR pvUserR3, RTR0PTR pvUserR0, RTRCPTR pvUserRC,
                               R3PTRTYPE(const char *) pszDesc, PPGMPHYSHANDLER *ppPhysHandler)
{
    PPGMPHYSHANDLERTYPEINT pType = PGMPHYSHANDLERTYPEINT_FROM_HANDLE(pVM, hType);
    Log(("pgmHandlerPhysicalExCreate: pvUserR3=%RHv pvUserR0=%RHv pvUserGC=%RRv hType=%#x (%d, %s) pszDesc=%RHv:%s\n",
         pvUserR3, pvUserR0, pvUserRC, hType, pType->enmKind, R3STRING(pType->pszDesc), pszDesc, R3STRING(pszDesc)));

    /*
     * Validate input.
     */
    AssertPtr(ppPhysHandler);
    AssertReturn(pType->u32Magic == PGMPHYSHANDLERTYPEINT_MAGIC, VERR_INVALID_HANDLE);
    AssertMsgReturn(    (RTRCUINTPTR)pvUserRC < 0x10000
                    ||  MMHyperR3ToRC(pVM, MMHyperRCToR3(pVM, pvUserRC)) == pvUserRC,
                    ("Not RC pointer! pvUserRC=%RRv\n", pvUserRC),
                    VERR_INVALID_PARAMETER);
#if 0 /* No longer valid. */
    AssertMsgReturn(    (RTR0UINTPTR)pvUserR0 < 0x10000
                    ||  MMHyperR3ToR0(pVM, MMHyperR0ToR3(pVM, pvUserR0)) == pvUserR0,
                    ("Not R0 pointer! pvUserR0=%RHv\n", pvUserR0),
                    VERR_INVALID_PARAMETER);
#endif

    /*
     * Allocate and initialize the new entry.
     */
    PPGMPHYSHANDLER pNew;
    int rc = MMHyperAlloc(pVM, sizeof(*pNew), 0, MM_TAG_PGM_HANDLERS, (void **)&pNew);
    if (RT_SUCCESS(rc))
    {
        pNew->Core.Key      = NIL_RTGCPHYS;
        pNew->Core.KeyLast  = NIL_RTGCPHYS;
        pNew->cPages        = 0;
        pNew->cAliasedPages = 0;
        pNew->cTmpOffPages  = 0;
        pNew->pvUserR3      = pvUserR3;
        pNew->pvUserR0      = pvUserR0;
        pNew->hType         = hType;
        pNew->pszDesc       = pszDesc != NIL_RTR3PTR ? pszDesc : pType->pszDesc;
        pgmHandlerPhysicalTypeRetain(pVM, pType);
        *ppPhysHandler = pNew;
        return VINF_SUCCESS;
    }

    return rc;
}


/**
 * Duplicates a physical access handler.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when successfully installed.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pPhysHandlerSrc The source handler to duplicate
 * @param   ppPhysHandler   Where to return the access handler structure on
 *                          success.
 */
int pgmHandlerPhysicalExDup(PVMCC pVM, PPGMPHYSHANDLER pPhysHandlerSrc, PPGMPHYSHANDLER *ppPhysHandler)
{
    return pgmHandlerPhysicalExCreate(pVM,
                                      pPhysHandlerSrc->hType,
                                      pPhysHandlerSrc->pvUserR3,
                                      pPhysHandlerSrc->pvUserR0,
                                      NIL_RTR0PTR,
                                      pPhysHandlerSrc->pszDesc,
                                      ppPhysHandler);
}


/**
 * Register a access handler for a physical range.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when successfully installed.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pPhysHandler    The physical handler.
 * @param   GCPhys          Start physical address.
 * @param   GCPhysLast      Last physical address. (inclusive)
 */
int pgmHandlerPhysicalExRegister(PVMCC pVM, PPGMPHYSHANDLER pPhysHandler, RTGCPHYS GCPhys, RTGCPHYS GCPhysLast)
{
    /*
     * Validate input.
     */
    AssertPtr(pPhysHandler);
    PPGMPHYSHANDLERTYPEINT pType = PGMPHYSHANDLERTYPEINT_FROM_HANDLE(pVM, pPhysHandler->hType);
    Assert(pType->u32Magic == PGMPHYSHANDLERTYPEINT_MAGIC);
    Log(("pgmHandlerPhysicalExRegister: GCPhys=%RGp GCPhysLast=%RGp hType=%#x (%d, %s) pszDesc=%RHv:%s\n",
         GCPhys, GCPhysLast, pPhysHandler->hType, pType->enmKind, R3STRING(pType->pszDesc), pPhysHandler->pszDesc, R3STRING(pPhysHandler->pszDesc)));
    AssertReturn(pPhysHandler->Core.Key == NIL_RTGCPHYS, VERR_WRONG_ORDER);

    AssertMsgReturn(GCPhys < GCPhysLast, ("GCPhys >= GCPhysLast (%#x >= %#x)\n", GCPhys, GCPhysLast), VERR_INVALID_PARAMETER);
    switch (pType->enmKind)
    {
        case PGMPHYSHANDLERKIND_WRITE:
            break;
        case PGMPHYSHANDLERKIND_MMIO:
        case PGMPHYSHANDLERKIND_ALL:
            /* Simplification for PGMPhysRead, PGMR0Trap0eHandlerNPMisconfig and others: Full pages. */
            AssertMsgReturn(!(GCPhys & PAGE_OFFSET_MASK), ("%RGp\n", GCPhys), VERR_INVALID_PARAMETER);
            AssertMsgReturn((GCPhysLast & PAGE_OFFSET_MASK) == PAGE_OFFSET_MASK, ("%RGp\n", GCPhysLast), VERR_INVALID_PARAMETER);
            break;
        default:
            AssertMsgFailed(("Invalid input enmKind=%d!\n", pType->enmKind));
            return VERR_INVALID_PARAMETER;
    }

    /*
     * We require the range to be within registered ram.
     * There is no apparent need to support ranges which cover more than one ram range.
     */
    PPGMRAMRANGE pRam = pgmPhysGetRange(pVM, GCPhys);
    if (   !pRam
        || GCPhysLast > pRam->GCPhysLast)
    {
#ifdef IN_RING3
        DBGFR3Info(pVM->pUVM, "phys", NULL, NULL);
#endif
        AssertMsgFailed(("No RAM range for %RGp-%RGp\n", GCPhys, GCPhysLast));
        return VERR_PGM_HANDLER_PHYSICAL_NO_RAM_RANGE;
    }
    Assert(GCPhys >= pRam->GCPhys && GCPhys < pRam->GCPhysLast);
    Assert(GCPhysLast <= pRam->GCPhysLast && GCPhysLast >= pRam->GCPhys);

    /*
     * Try insert into list.
     */
    pPhysHandler->Core.Key     = GCPhys;
    pPhysHandler->Core.KeyLast = GCPhysLast;
    pPhysHandler->cPages       = (GCPhysLast - (GCPhys & X86_PTE_PAE_PG_MASK) + PAGE_SIZE) >> PAGE_SHIFT;

    pgmLock(pVM);
    if (RTAvlroGCPhysInsert(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, &pPhysHandler->Core))
    {
        int rc = pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs(pVM, pPhysHandler, pRam);
        if (rc == VINF_PGM_SYNC_CR3)
            rc = VINF_PGM_GCPHYS_ALIASED;

#if defined(IN_RING3) || defined(IN_RING0)
        NEMHCNotifyHandlerPhysicalRegister(pVM, pType->enmKind, GCPhys, GCPhysLast - GCPhys + 1);
#endif
        pgmUnlock(pVM);

        if (rc != VINF_SUCCESS)
            Log(("PGMHandlerPhysicalRegisterEx: returns %Rrc (%RGp-%RGp)\n", rc, GCPhys, GCPhysLast));
        return rc;
    }
    pgmUnlock(pVM);

    pPhysHandler->Core.Key     = NIL_RTGCPHYS;
    pPhysHandler->Core.KeyLast = NIL_RTGCPHYS;

#if defined(IN_RING3) && defined(VBOX_STRICT)
    DBGFR3Info(pVM->pUVM, "handlers", "phys nostats", NULL);
#endif
    AssertMsgFailed(("Conflict! GCPhys=%RGp GCPhysLast=%RGp pszDesc=%s/%s\n",
                     GCPhys, GCPhysLast, R3STRING(pPhysHandler->pszDesc), R3STRING(pType->pszDesc)));
    return VERR_PGM_HANDLER_PHYSICAL_CONFLICT;
}


/**
 * Register a access handler for a physical range.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when successfully installed.
 * @retval  VINF_PGM_GCPHYS_ALIASED when the shadow PTs could be updated because
 *          the guest page aliased or/and mapped by multiple PTs. A CR3 sync has been
 *          flagged together with a pool clearing.
 * @retval  VERR_PGM_HANDLER_PHYSICAL_CONFLICT if the range conflicts with an existing
 *          one. A debug assertion is raised.
 *
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys          Start physical address.
 * @param   GCPhysLast      Last physical address. (inclusive)
 * @param   hType           The handler type registration handle.
 * @param   pvUserR3        User argument to the R3 handler.
 * @param   pvUserR0        User argument to the R0 handler.
 * @param   pvUserRC        User argument to the RC handler. This can be a value
 *                          less that 0x10000 or a (non-null) pointer that is
 *                          automatically relocated.
 * @param   pszDesc         Description of this handler.  If NULL, the type
 *                          description will be used instead.
 */
VMMDECL(int) PGMHandlerPhysicalRegister(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysLast, PGMPHYSHANDLERTYPE hType,
                                        RTR3PTR pvUserR3, RTR0PTR pvUserR0, RTRCPTR pvUserRC, R3PTRTYPE(const char *) pszDesc)
{
#ifdef LOG_ENABLED
    PPGMPHYSHANDLERTYPEINT pType = PGMPHYSHANDLERTYPEINT_FROM_HANDLE(pVM, hType);
    Log(("PGMHandlerPhysicalRegister: GCPhys=%RGp GCPhysLast=%RGp pvUserR3=%RHv pvUserR0=%RHv pvUserGC=%RRv hType=%#x (%d, %s) pszDesc=%RHv:%s\n",
         GCPhys, GCPhysLast, pvUserR3, pvUserR0, pvUserRC, hType, pType->enmKind, R3STRING(pType->pszDesc), pszDesc, R3STRING(pszDesc)));
#endif

    PPGMPHYSHANDLER pNew;
    int rc = pgmHandlerPhysicalExCreate(pVM, hType, pvUserR3, pvUserR0, pvUserRC, pszDesc, &pNew);
    if (RT_SUCCESS(rc))
    {
        rc = pgmHandlerPhysicalExRegister(pVM, pNew, GCPhys, GCPhysLast);
        if (RT_SUCCESS(rc))
            return rc;
        pgmHandlerPhysicalExDestroy(pVM, pNew);
    }
    return rc;
}


/**
 * Sets ram range flags and attempts updating shadow PTs.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when shadow PTs was successfully updated.
 * @retval  VINF_PGM_SYNC_CR3 when the shadow PTs could be updated because
 *          the guest page aliased or/and mapped by multiple PTs. FFs set.
 * @param   pVM     The cross context VM structure.
 * @param   pCur    The physical handler.
 * @param   pRam    The RAM range.
 */
static int pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs(PVMCC pVM, PPGMPHYSHANDLER pCur, PPGMRAMRANGE pRam)
{
    /*
     * Iterate the guest ram pages updating the flags and flushing PT entries
     * mapping the page.
     */
    bool                    fFlushTLBs = false;
    int                     rc         = VINF_SUCCESS;
    PPGMPHYSHANDLERTYPEINT  pCurType   = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
    const unsigned          uState     = pCurType->uState;
    uint32_t                cPages     = pCur->cPages;
    uint32_t                i          = (pCur->Core.Key - pRam->GCPhys) >> PAGE_SHIFT;
    for (;;)
    {
        PPGMPAGE pPage = &pRam->aPages[i];
        AssertMsg(pCurType->enmKind != PGMPHYSHANDLERKIND_MMIO || PGM_PAGE_IS_MMIO(pPage),
                  ("%RGp %R[pgmpage]\n", pRam->GCPhys + (i << PAGE_SHIFT), pPage));

        /* Only do upgrades. */
        if (PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) < uState)
        {
            PGM_PAGE_SET_HNDL_PHYS_STATE(pPage, uState);

            const RTGCPHYS GCPhysPage = pRam->GCPhys + (i << PAGE_SHIFT);
            int rc2 = pgmPoolTrackUpdateGCPhys(pVM, GCPhysPage, pPage,
                                               false /* allow updates of PTEs (instead of flushing) */, &fFlushTLBs);
            if (rc2 != VINF_SUCCESS && rc == VINF_SUCCESS)
                rc = rc2;

            /* Tell NEM about the protection update. */
            if (VM_IS_NEM_ENABLED(pVM))
            {
                uint8_t     u2State = PGM_PAGE_GET_NEM_STATE(pPage);
                PGMPAGETYPE enmType = (PGMPAGETYPE)PGM_PAGE_GET_TYPE(pPage);
                NEMHCNotifyPhysPageProtChanged(pVM, GCPhysPage, PGM_PAGE_GET_HCPHYS(pPage),
                                               pgmPhysPageCalcNemProtection(pPage, enmType), enmType, &u2State);
                PGM_PAGE_SET_NEM_STATE(pPage, u2State);
            }
        }

        /* next */
        if (--cPages == 0)
            break;
        i++;
    }

    if (fFlushTLBs)
    {
        PGM_INVL_ALL_VCPU_TLBS(pVM);
        Log(("pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs: flushing guest TLBs; rc=%d\n", rc));
    }
    else
        Log(("pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs: doesn't flush guest TLBs. rc=%Rrc; sync flags=%x VMCPU_FF_PGM_SYNC_CR3=%d\n", rc, VMMGetCpu(pVM)->pgm.s.fSyncFlags, VMCPU_FF_IS_SET(VMMGetCpu(pVM), VMCPU_FF_PGM_SYNC_CR3)));

    return rc;
}


/**
 * Deregister a physical page access handler.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pPhysHandler    The handler to deregister (but not free).
 * @param   fRestoreAsRAM   How this will likely be restored, if we know (true,
 *                          false, or if we don't know -1).
 */
int pgmHandlerPhysicalExDeregister(PVMCC pVM, PPGMPHYSHANDLER pPhysHandler, int fRestoreAsRAM)
{
    LogFlow(("pgmHandlerPhysicalExDeregister: Removing Range %RGp-%RGp %s fRestoreAsRAM=%d\n",
             pPhysHandler->Core.Key, pPhysHandler->Core.KeyLast, R3STRING(pPhysHandler->pszDesc), fRestoreAsRAM));
    AssertReturn(pPhysHandler->Core.Key != NIL_RTGCPHYS, VERR_PGM_HANDLER_NOT_FOUND);

    /*
     * Remove the handler from the tree.
     */
    pgmLock(pVM);
    PPGMPHYSHANDLER pRemoved = (PPGMPHYSHANDLER)RTAvlroGCPhysRemove(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers,
                                                                    pPhysHandler->Core.Key);
    if (pRemoved == pPhysHandler)
    {
        /*
         * Clear the page bits, notify the REM about this change and clear
         * the cache.
         */
        pgmHandlerPhysicalResetRamFlags(pVM, pPhysHandler);
        pgmHandlerPhysicalDeregisterNotifyREMAndNEM(pVM, pPhysHandler, fRestoreAsRAM);
        pVM->pgm.s.pLastPhysHandlerR0 = 0;
        pVM->pgm.s.pLastPhysHandlerR3 = 0;

        pPhysHandler->Core.Key     = NIL_RTGCPHYS;
        pPhysHandler->Core.KeyLast = NIL_RTGCPHYS;

        pgmUnlock(pVM);

        return VINF_SUCCESS;
    }

    /*
     * Both of the failure conditions here are considered internal processing
     * errors because they can only be caused by race conditions or corruption.
     * If we ever need to handle concurrent deregistration, we have to move
     * the NIL_RTGCPHYS check inside the PGM lock.
     */
    if (pRemoved)
        RTAvlroGCPhysInsert(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, &pRemoved->Core);

    pgmUnlock(pVM);

    if (!pRemoved)
        AssertMsgFailed(("Didn't find range starting at %RGp in the tree!\n", pPhysHandler->Core.Key));
    else
        AssertMsgFailed(("Found different handle at %RGp in the tree: got %p insteaded of %p\n",
                         pPhysHandler->Core.Key, pRemoved, pPhysHandler));
    return VERR_PGM_HANDLER_IPE_1;
}


/**
 * Destroys (frees) a physical handler.
 *
 * The caller must deregister it before destroying it!
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pHandler    The handler to free.  NULL if ignored.
 */
int pgmHandlerPhysicalExDestroy(PVMCC pVM, PPGMPHYSHANDLER pHandler)
{
    if (pHandler)
    {
        AssertPtr(pHandler);
        AssertReturn(pHandler->Core.Key == NIL_RTGCPHYS, VERR_WRONG_ORDER);
        PGMHandlerPhysicalTypeRelease(pVM, pHandler->hType);
        MMHyperFree(pVM, pHandler);
    }
    return VINF_SUCCESS;
}


/**
 * Deregister a physical page access handler.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      Start physical address.
 */
VMMDECL(int)  PGMHandlerPhysicalDeregister(PVMCC pVM, RTGCPHYS GCPhys)
{
    /*
     * Find the handler.
     */
    pgmLock(pVM);
    PPGMPHYSHANDLER pRemoved = (PPGMPHYSHANDLER)RTAvlroGCPhysRemove(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys);
    if (pRemoved)
    {
        LogFlow(("PGMHandlerPhysicalDeregister: Removing Range %RGp-%RGp %s\n",
                 pRemoved->Core.Key, pRemoved->Core.KeyLast, R3STRING(pRemoved->pszDesc)));

        /*
         * Clear the page bits, notify the REM about this change and clear
         * the cache.
         */
        pgmHandlerPhysicalResetRamFlags(pVM, pRemoved);
        pgmHandlerPhysicalDeregisterNotifyREMAndNEM(pVM, pRemoved, -1);
        pVM->pgm.s.pLastPhysHandlerR0 = 0;
        pVM->pgm.s.pLastPhysHandlerR3 = 0;

        pgmUnlock(pVM);

        pRemoved->Core.Key = NIL_RTGCPHYS;
        pgmHandlerPhysicalExDestroy(pVM, pRemoved);
        return VINF_SUCCESS;
    }

    pgmUnlock(pVM);

    AssertMsgFailed(("Didn't find range starting at %RGp\n", GCPhys));
    return VERR_PGM_HANDLER_NOT_FOUND;
}


/**
 * Shared code with modify.
 */
static void pgmHandlerPhysicalDeregisterNotifyREMAndNEM(PVMCC pVM, PPGMPHYSHANDLER pCur, int fRestoreAsRAM)
{
    PPGMPHYSHANDLERTYPEINT  pCurType    = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
    RTGCPHYS                GCPhysStart = pCur->Core.Key;
    RTGCPHYS                GCPhysLast  = pCur->Core.KeyLast;

    /*
     * Page align the range.
     *
     * Since we've reset (recalculated) the physical handler state of all pages
     * we can make use of the page states to figure out whether a page should be
     * included in the REM notification or not.
     */
    if (   (pCur->Core.Key           & PAGE_OFFSET_MASK)
        || ((pCur->Core.KeyLast + 1) & PAGE_OFFSET_MASK))
    {
        Assert(pCurType->enmKind != PGMPHYSHANDLERKIND_MMIO);

        if (GCPhysStart & PAGE_OFFSET_MASK)
        {
            PPGMPAGE pPage = pgmPhysGetPage(pVM, GCPhysStart);
            if (    pPage
                &&  PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) != PGM_PAGE_HNDL_PHYS_STATE_NONE)
            {
                RTGCPHYS GCPhys = (GCPhysStart + (PAGE_SIZE - 1)) & X86_PTE_PAE_PG_MASK;
                if (    GCPhys > GCPhysLast
                    ||  GCPhys < GCPhysStart)
                    return;
                GCPhysStart = GCPhys;
            }
            else
                GCPhysStart &= X86_PTE_PAE_PG_MASK;
            Assert(!pPage || PGM_PAGE_GET_TYPE(pPage) != PGMPAGETYPE_MMIO); /* these are page aligned atm! */
        }

        if (GCPhysLast & PAGE_OFFSET_MASK)
        {
            PPGMPAGE pPage = pgmPhysGetPage(pVM, GCPhysLast);
            if (    pPage
                &&  PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) != PGM_PAGE_HNDL_PHYS_STATE_NONE)
            {
                RTGCPHYS GCPhys = (GCPhysLast & X86_PTE_PAE_PG_MASK) - 1;
                if (    GCPhys < GCPhysStart
                    ||  GCPhys > GCPhysLast)
                    return;
                GCPhysLast = GCPhys;
            }
            else
                GCPhysLast |= PAGE_OFFSET_MASK;
            Assert(!pPage || PGM_PAGE_GET_TYPE(pPage) != PGMPAGETYPE_MMIO); /* these are page aligned atm! */
        }
    }

    /*
     * Tell REM and NEM.
     */
    const bool fRestoreAsRAM2 = pCurType->pfnHandlerR3
                             && pCurType->enmKind != PGMPHYSHANDLERKIND_MMIO; /** @todo this isn't entirely correct. */
    /** @todo do we need this notification? */
    NEMHCNotifyHandlerPhysicalDeregister(pVM, pCurType->enmKind, GCPhysStart, GCPhysLast - GCPhysStart + 1,
                                         fRestoreAsRAM, fRestoreAsRAM2);
}


/**
 * pgmHandlerPhysicalResetRamFlags helper that checks for other handlers on
 * edge pages.
 */
DECLINLINE(void) pgmHandlerPhysicalRecalcPageState(PVMCC pVM, RTGCPHYS GCPhys, bool fAbove, PPGMRAMRANGE *ppRamHint)
{
    /*
     * Look for other handlers.
     */
    unsigned uState = PGM_PAGE_HNDL_PHYS_STATE_NONE;
    for (;;)
    {
        PPGMPHYSHANDLER pCur = (PPGMPHYSHANDLER)RTAvlroGCPhysGetBestFit(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys, fAbove);
        if (   !pCur
            || ((fAbove ? pCur->Core.Key : pCur->Core.KeyLast) >> PAGE_SHIFT) != (GCPhys >> PAGE_SHIFT))
            break;
        PPGMPHYSHANDLERTYPEINT pCurType = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
        uState = RT_MAX(uState, pCurType->uState);

        /* next? */
        RTGCPHYS GCPhysNext = fAbove
                            ? pCur->Core.KeyLast + 1
                            : pCur->Core.Key - 1;
        if ((GCPhysNext >> PAGE_SHIFT) != (GCPhys >> PAGE_SHIFT))
            break;
        GCPhys = GCPhysNext;
    }

    /*
     * Update if we found something that is a higher priority
     * state than the current.
     */
    if (uState != PGM_PAGE_HNDL_PHYS_STATE_NONE)
    {
        PPGMPAGE pPage;
        int rc = pgmPhysGetPageWithHintEx(pVM, GCPhys, &pPage, ppRamHint);
        if (    RT_SUCCESS(rc)
            &&  PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) < uState)
        {
            /* This should normally not be necessary. */
            PGM_PAGE_SET_HNDL_PHYS_STATE(pPage, uState);
            bool fFlushTLBs ;
            rc = pgmPoolTrackUpdateGCPhys(pVM, GCPhys, pPage, false /*fFlushPTEs*/, &fFlushTLBs);
            if (RT_SUCCESS(rc) && fFlushTLBs)
                PGM_INVL_ALL_VCPU_TLBS(pVM);
            else
                AssertRC(rc);

            /* Tell NEM about the protection update. */
            if (VM_IS_NEM_ENABLED(pVM))
            {
                uint8_t     u2State = PGM_PAGE_GET_NEM_STATE(pPage);
                PGMPAGETYPE enmType = (PGMPAGETYPE)PGM_PAGE_GET_TYPE(pPage);
                NEMHCNotifyPhysPageProtChanged(pVM, GCPhys, PGM_PAGE_GET_HCPHYS(pPage),
                                               pgmPhysPageCalcNemProtection(pPage, enmType), enmType, &u2State);
                PGM_PAGE_SET_NEM_STATE(pPage, u2State);
            }
        }
        else
            AssertRC(rc);
    }
}


/**
 * Resets an aliased page.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pPage           The page.
 * @param   GCPhysPage      The page address in case it comes in handy.
 * @param   fDoAccounting   Whether to perform accounting.  (Only set during
 *                          reset where pgmR3PhysRamReset doesn't have the
 *                          handler structure handy.)
 */
void pgmHandlerPhysicalResetAliasedPage(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhysPage, bool fDoAccounting)
{
    Assert(   PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO2_ALIAS_MMIO
           || PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_SPECIAL_ALIAS_MMIO);
    Assert(PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) == PGM_PAGE_HNDL_PHYS_STATE_DISABLED);
    RTHCPHYS const HCPhysPrev = PGM_PAGE_GET_HCPHYS(pPage);

    /*
     * Flush any shadow page table references *first*.
     */
    bool fFlushTLBs = false;
    int rc = pgmPoolTrackUpdateGCPhys(pVM, GCPhysPage, pPage, true /*fFlushPTEs*/, &fFlushTLBs);
    AssertLogRelRCReturnVoid(rc);
    HMFlushTlbOnAllVCpus(pVM);

    /*
     * Make it an MMIO/Zero page.
     */
    PGM_PAGE_SET_HCPHYS(pVM, pPage, pVM->pgm.s.HCPhysZeroPg);
    PGM_PAGE_SET_TYPE(pVM, pPage, PGMPAGETYPE_MMIO);
    PGM_PAGE_SET_STATE(pVM, pPage, PGM_PAGE_STATE_ZERO);
    PGM_PAGE_SET_PAGEID(pVM, pPage, NIL_GMM_PAGEID);
    PGM_PAGE_SET_HNDL_PHYS_STATE(pPage, PGM_PAGE_HNDL_PHYS_STATE_ALL);

    /* Flush its TLB entry. */
    pgmPhysInvalidatePageMapTLBEntry(pVM, GCPhysPage);

    /*
     * Do accounting for pgmR3PhysRamReset.
     */
    if (fDoAccounting)
    {
        PPGMPHYSHANDLER pHandler = pgmHandlerPhysicalLookup(pVM, GCPhysPage);
        if (RT_LIKELY(pHandler))
        {
            Assert(pHandler->cAliasedPages > 0);
            pHandler->cAliasedPages--;
        }
        else
            AssertFailed();
    }

    /*
     * Tell NEM about the protection change.
     */
    if (VM_IS_NEM_ENABLED(pVM))
    {
        uint8_t u2State = PGM_PAGE_GET_NEM_STATE(pPage);
        NEMHCNotifyPhysPageChanged(pVM, GCPhysPage, HCPhysPrev, pVM->pgm.s.HCPhysZeroPg,
                                   NEM_PAGE_PROT_NONE, PGMPAGETYPE_MMIO, &u2State);
        PGM_PAGE_SET_NEM_STATE(pPage, u2State);
    }
}


/**
 * Resets ram range flags.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when shadow PTs was successfully updated.
 * @param   pVM     The cross context VM structure.
 * @param   pCur    The physical handler.
 *
 * @remark  We don't start messing with the shadow page tables, as we've
 *          already got code in Trap0e which deals with out of sync handler
 *          flags (originally conceived for global pages).
 */
static void pgmHandlerPhysicalResetRamFlags(PVMCC pVM, PPGMPHYSHANDLER pCur)
{
    /*
     * Iterate the guest ram pages updating the state.
     */
    RTUINT          cPages   = pCur->cPages;
    RTGCPHYS        GCPhys   = pCur->Core.Key;
    PPGMRAMRANGE    pRamHint = NULL;
    for (;;)
    {
        PPGMPAGE pPage;
        int rc = pgmPhysGetPageWithHintEx(pVM, GCPhys, &pPage, &pRamHint);
        if (RT_SUCCESS(rc))
        {
            /* Reset aliased MMIO pages to MMIO, since this aliasing is our business.
               (We don't flip MMIO to RAM though, that's PGMPhys.cpp's job.)  */
            bool fNemNotifiedAlready = false;
            if (   PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO2_ALIAS_MMIO
                || PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_SPECIAL_ALIAS_MMIO)
            {
                Assert(pCur->cAliasedPages > 0);
                pgmHandlerPhysicalResetAliasedPage(pVM, pPage, GCPhys, false /*fDoAccounting*/);
                pCur->cAliasedPages--;
                fNemNotifiedAlready = true;
            }
#ifdef VBOX_STRICT
            PPGMPHYSHANDLERTYPEINT pCurType = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
            AssertMsg(pCurType->enmKind != PGMPHYSHANDLERKIND_MMIO || PGM_PAGE_IS_MMIO(pPage), ("%RGp %R[pgmpage]\n", GCPhys, pPage));
#endif
            PGM_PAGE_SET_HNDL_PHYS_STATE(pPage, PGM_PAGE_HNDL_PHYS_STATE_NONE);

            /* Tell NEM about the protection change. */
            if (VM_IS_NEM_ENABLED(pVM) && !fNemNotifiedAlready)
            {
                uint8_t     u2State = PGM_PAGE_GET_NEM_STATE(pPage);
                PGMPAGETYPE enmType = (PGMPAGETYPE)PGM_PAGE_GET_TYPE(pPage);
                NEMHCNotifyPhysPageProtChanged(pVM, GCPhys, PGM_PAGE_GET_HCPHYS(pPage),
                                               pgmPhysPageCalcNemProtection(pPage, enmType), enmType, &u2State);
                PGM_PAGE_SET_NEM_STATE(pPage, u2State);
            }
        }
        else
            AssertRC(rc);

        /* next */
        if (--cPages == 0)
            break;
        GCPhys += PAGE_SIZE;
    }

    pCur->cAliasedPages = 0;
    pCur->cTmpOffPages  = 0;

    /*
     * Check for partial start and end pages.
     */
    if (pCur->Core.Key & PAGE_OFFSET_MASK)
        pgmHandlerPhysicalRecalcPageState(pVM, pCur->Core.Key - 1, false /* fAbove */, &pRamHint);
    if ((pCur->Core.KeyLast & PAGE_OFFSET_MASK) != PAGE_OFFSET_MASK)
        pgmHandlerPhysicalRecalcPageState(pVM, pCur->Core.KeyLast + 1, true /* fAbove */, &pRamHint);
}


/**
 * Modify a physical page access handler.
 *
 * Modification can only be done to the range it self, not the type or anything else.
 *
 * @returns VBox status code.
 *          For all return codes other than VERR_PGM_HANDLER_NOT_FOUND and VINF_SUCCESS the range is deregistered
 *          and a new registration must be performed!
 * @param   pVM             The cross context VM structure.
 * @param   GCPhysCurrent   Current location.
 * @param   GCPhys          New location.
 * @param   GCPhysLast      New last location.
 */
VMMDECL(int) PGMHandlerPhysicalModify(PVMCC pVM, RTGCPHYS GCPhysCurrent, RTGCPHYS GCPhys, RTGCPHYS GCPhysLast)
{
    /*
     * Remove it.
     */
    int rc;
    pgmLock(pVM);
    PPGMPHYSHANDLER pCur = (PPGMPHYSHANDLER)RTAvlroGCPhysRemove(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhysCurrent);
    if (pCur)
    {
        /*
         * Clear the ram flags. (We're gonna move or free it!)
         */
        pgmHandlerPhysicalResetRamFlags(pVM, pCur);
        PPGMPHYSHANDLERTYPEINT const pCurType      = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
        bool const                   fRestoreAsRAM = pCurType->pfnHandlerR3 /** @todo this isn't entirely correct. */
                                                  && pCurType->enmKind != PGMPHYSHANDLERKIND_MMIO;

        /*
         * Validate the new range, modify and reinsert.
         */
        if (GCPhysLast >= GCPhys)
        {
            /*
             * We require the range to be within registered ram.
             * There is no apparent need to support ranges which cover more than one ram range.
             */
            PPGMRAMRANGE pRam = pgmPhysGetRange(pVM, GCPhys);
            if (   pRam
                && GCPhys <= pRam->GCPhysLast
                && GCPhysLast >= pRam->GCPhys)
            {
                pCur->Core.Key      = GCPhys;
                pCur->Core.KeyLast  = GCPhysLast;
                pCur->cPages        = (GCPhysLast - (GCPhys & X86_PTE_PAE_PG_MASK) + 1) >> PAGE_SHIFT;

                if (RTAvlroGCPhysInsert(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, &pCur->Core))
                {
                    RTGCPHYS            const cb            = GCPhysLast - GCPhys + 1;
                    PGMPHYSHANDLERKIND  const enmKind       = pCurType->enmKind;

                    /*
                     * Set ram flags, flush shadow PT entries and finally tell REM about this.
                     */
                    rc = pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs(pVM, pCur, pRam);

                    /** @todo NEM: not sure we need this notification... */
                    NEMHCNotifyHandlerPhysicalModify(pVM, enmKind, GCPhysCurrent, GCPhys, cb, fRestoreAsRAM);

                    pgmUnlock(pVM);

                    PGM_INVL_ALL_VCPU_TLBS(pVM);
                    Log(("PGMHandlerPhysicalModify: GCPhysCurrent=%RGp -> GCPhys=%RGp GCPhysLast=%RGp\n",
                         GCPhysCurrent, GCPhys, GCPhysLast));
                    return VINF_SUCCESS;
                }

                AssertMsgFailed(("Conflict! GCPhys=%RGp GCPhysLast=%RGp\n", GCPhys, GCPhysLast));
                rc = VERR_PGM_HANDLER_PHYSICAL_CONFLICT;
            }
            else
            {
                AssertMsgFailed(("No RAM range for %RGp-%RGp\n", GCPhys, GCPhysLast));
                rc = VERR_PGM_HANDLER_PHYSICAL_NO_RAM_RANGE;
            }
        }
        else
        {
            AssertMsgFailed(("Invalid range %RGp-%RGp\n", GCPhys, GCPhysLast));
            rc = VERR_INVALID_PARAMETER;
        }

        /*
         * Invalid new location, flush the cache and free it.
         * We've only gotta notify REM and free the memory.
         */
        pgmHandlerPhysicalDeregisterNotifyREMAndNEM(pVM, pCur, -1);
        pVM->pgm.s.pLastPhysHandlerR0 = 0;
        pVM->pgm.s.pLastPhysHandlerR3 = 0;
        PGMHandlerPhysicalTypeRelease(pVM, pCur->hType);
        MMHyperFree(pVM, pCur);
    }
    else
    {
        AssertMsgFailed(("Didn't find range starting at %RGp\n", GCPhysCurrent));
        rc = VERR_PGM_HANDLER_NOT_FOUND;
    }

    pgmUnlock(pVM);
    return rc;
}


/**
 * Changes the user callback arguments associated with a physical access handler.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys          Start physical address of the handler.
 * @param   pvUserR3        User argument to the R3 handler.
 * @param   pvUserR0        User argument to the R0 handler.
 */
VMMDECL(int) PGMHandlerPhysicalChangeUserArgs(PVMCC pVM, RTGCPHYS GCPhys, RTR3PTR pvUserR3, RTR0PTR pvUserR0)
{
    /*
     * Find the handler.
     */
    int rc = VINF_SUCCESS;
    pgmLock(pVM);
    PPGMPHYSHANDLER pCur = (PPGMPHYSHANDLER)RTAvlroGCPhysGet(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys);
    if (pCur)
    {
        /*
         * Change arguments.
         */
        pCur->pvUserR3 = pvUserR3;
        pCur->pvUserR0 = pvUserR0;
    }
    else
    {
        AssertMsgFailed(("Didn't find range starting at %RGp\n", GCPhys));
        rc = VERR_PGM_HANDLER_NOT_FOUND;
    }

    pgmUnlock(pVM);
    return rc;
}


/**
 * Splits a physical access handler in two.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys          Start physical address of the handler.
 * @param   GCPhysSplit     The split address.
 */
VMMDECL(int) PGMHandlerPhysicalSplit(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysSplit)
{
    AssertReturn(GCPhys < GCPhysSplit, VERR_INVALID_PARAMETER);

    /*
     * Do the allocation without owning the lock.
     */
    PPGMPHYSHANDLER pNew;
    int rc = MMHyperAlloc(pVM, sizeof(*pNew), 0, MM_TAG_PGM_HANDLERS, (void **)&pNew);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Get the handler.
     */
    pgmLock(pVM);
    PPGMPHYSHANDLER pCur = (PPGMPHYSHANDLER)RTAvlroGCPhysGet(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys);
    if (RT_LIKELY(pCur))
    {
        if (RT_LIKELY(GCPhysSplit <= pCur->Core.KeyLast))
        {
            /*
             * Create new handler node for the 2nd half.
             */
            *pNew = *pCur;
            pNew->Core.Key      = GCPhysSplit;
            pNew->cPages        = (pNew->Core.KeyLast - (pNew->Core.Key & X86_PTE_PAE_PG_MASK) + PAGE_SIZE) >> PAGE_SHIFT;

            pCur->Core.KeyLast  = GCPhysSplit - 1;
            pCur->cPages        = (pCur->Core.KeyLast - (pCur->Core.Key & X86_PTE_PAE_PG_MASK) + PAGE_SIZE) >> PAGE_SHIFT;

            if (RT_LIKELY(RTAvlroGCPhysInsert(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, &pNew->Core)))
            {
                LogFlow(("PGMHandlerPhysicalSplit: %RGp-%RGp and %RGp-%RGp\n",
                         pCur->Core.Key, pCur->Core.KeyLast, pNew->Core.Key, pNew->Core.KeyLast));
                pgmUnlock(pVM);
                return VINF_SUCCESS;
            }
            AssertMsgFailed(("whu?\n"));
            rc = VERR_PGM_PHYS_HANDLER_IPE;
        }
        else
        {
            AssertMsgFailed(("outside range: %RGp-%RGp split %RGp\n", pCur->Core.Key, pCur->Core.KeyLast, GCPhysSplit));
            rc = VERR_INVALID_PARAMETER;
        }
    }
    else
    {
        AssertMsgFailed(("Didn't find range starting at %RGp\n", GCPhys));
        rc = VERR_PGM_HANDLER_NOT_FOUND;
    }
    pgmUnlock(pVM);
    MMHyperFree(pVM, pNew);
    return rc;
}


/**
 * Joins up two adjacent physical access handlers which has the same callbacks.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys1         Start physical address of the first handler.
 * @param   GCPhys2         Start physical address of the second handler.
 */
VMMDECL(int) PGMHandlerPhysicalJoin(PVMCC pVM, RTGCPHYS GCPhys1, RTGCPHYS GCPhys2)
{
    /*
     * Get the handlers.
     */
    int rc;
    pgmLock(pVM);
    PPGMPHYSHANDLER pCur1 = (PPGMPHYSHANDLER)RTAvlroGCPhysGet(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys1);
    if (RT_LIKELY(pCur1))
    {
        PPGMPHYSHANDLER pCur2 = (PPGMPHYSHANDLER)RTAvlroGCPhysGet(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys2);
        if (RT_LIKELY(pCur2))
        {
            /*
             * Make sure that they are adjacent, and that they've got the same callbacks.
             */
            if (RT_LIKELY(pCur1->Core.KeyLast + 1 == pCur2->Core.Key))
            {
                if (RT_LIKELY(pCur1->hType == pCur2->hType))
                {
                    PPGMPHYSHANDLER pCur3 = (PPGMPHYSHANDLER)RTAvlroGCPhysRemove(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys2);
                    if (RT_LIKELY(pCur3 == pCur2))
                    {
                        pCur1->Core.KeyLast  = pCur2->Core.KeyLast;
                        pCur1->cPages        = (pCur1->Core.KeyLast - (pCur1->Core.Key & X86_PTE_PAE_PG_MASK) + PAGE_SIZE) >> PAGE_SHIFT;
                        LogFlow(("PGMHandlerPhysicalJoin: %RGp-%RGp %RGp-%RGp\n",
                                 pCur1->Core.Key, pCur1->Core.KeyLast, pCur2->Core.Key, pCur2->Core.KeyLast));
                        pVM->pgm.s.pLastPhysHandlerR0 = 0;
                        pVM->pgm.s.pLastPhysHandlerR3 = 0;
                        PGMHandlerPhysicalTypeRelease(pVM, pCur2->hType);
                        MMHyperFree(pVM, pCur2);
                        pgmUnlock(pVM);
                        return VINF_SUCCESS;
                    }

                    Assert(pCur3 == pCur2);
                    rc = VERR_PGM_PHYS_HANDLER_IPE;
                }
                else
                {
                    AssertMsgFailed(("mismatching handlers\n"));
                    rc = VERR_ACCESS_DENIED;
                }
            }
            else
            {
                AssertMsgFailed(("not adjacent: %RGp-%RGp %RGp-%RGp\n",
                                 pCur1->Core.Key, pCur1->Core.KeyLast, pCur2->Core.Key, pCur2->Core.KeyLast));
                rc = VERR_INVALID_PARAMETER;
            }
        }
        else
        {
            AssertMsgFailed(("Didn't find range starting at %RGp\n", GCPhys2));
            rc = VERR_PGM_HANDLER_NOT_FOUND;
        }
    }
    else
    {
        AssertMsgFailed(("Didn't find range starting at %RGp\n", GCPhys1));
        rc = VERR_PGM_HANDLER_NOT_FOUND;
    }
    pgmUnlock(pVM);
    return rc;

}


/**
 * Resets any modifications to individual pages in a physical page access
 * handler region.
 *
 * This is used in pair with PGMHandlerPhysicalPageTempOff(),
 * PGMHandlerPhysicalPageAliasMmio2() or PGMHandlerPhysicalPageAliasHC().
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      The start address of the handler regions, i.e. what you
 *                      passed to PGMR3HandlerPhysicalRegister(),
 *                      PGMHandlerPhysicalRegisterEx() or
 *                      PGMHandlerPhysicalModify().
 */
VMMDECL(int) PGMHandlerPhysicalReset(PVMCC pVM, RTGCPHYS GCPhys)
{
    LogFlow(("PGMHandlerPhysicalReset GCPhys=%RGp\n", GCPhys));
    pgmLock(pVM);

    /*
     * Find the handler.
     */
    int rc;
    PPGMPHYSHANDLER pCur = (PPGMPHYSHANDLER)RTAvlroGCPhysGet(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys);
    if (RT_LIKELY(pCur))
    {
        /*
         * Validate kind.
         */
        PPGMPHYSHANDLERTYPEINT pCurType = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
        switch (pCurType->enmKind)
        {
            case PGMPHYSHANDLERKIND_WRITE:
            case PGMPHYSHANDLERKIND_ALL:
            case PGMPHYSHANDLERKIND_MMIO: /* NOTE: Only use when clearing MMIO ranges with aliased MMIO2 pages! */
            {
                STAM_COUNTER_INC(&pVM->pgm.s.CTX_SUFF(pStats)->CTX_MID_Z(Stat,PhysHandlerReset)); /** @todo move out of switch */
                PPGMRAMRANGE pRam = pgmPhysGetRange(pVM, GCPhys);
                Assert(pRam);
                Assert(pRam->GCPhys     <= pCur->Core.Key);
                Assert(pRam->GCPhysLast >= pCur->Core.KeyLast);

                if (pCurType->enmKind == PGMPHYSHANDLERKIND_MMIO)
                {
                    /*
                     * Reset all the PGMPAGETYPE_MMIO2_ALIAS_MMIO pages first and that's it.
                     * This could probably be optimized a bit wrt to flushing, but I'm too lazy
                     * to do that now...
                     */
                    if (pCur->cAliasedPages)
                    {
                        PPGMPAGE    pPage = &pRam->aPages[(pCur->Core.Key - pRam->GCPhys) >> PAGE_SHIFT];
                        uint32_t    cLeft = pCur->cPages;
                        while (cLeft-- > 0)
                        {
                            if (   PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO2_ALIAS_MMIO
                                || PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_SPECIAL_ALIAS_MMIO)
                            {
                                Assert(pCur->cAliasedPages > 0);
                                pgmHandlerPhysicalResetAliasedPage(pVM, pPage, pRam->GCPhys + ((RTGCPHYS)cLeft << PAGE_SHIFT),
                                                                   false /*fDoAccounting*/);
                                --pCur->cAliasedPages;
#ifndef VBOX_STRICT
                                if (pCur->cAliasedPages == 0)
                                    break;
#endif
                            }
                            Assert(PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO);
                            pPage++;
                        }
                        Assert(pCur->cAliasedPages == 0);
                    }
                }
                else if (pCur->cTmpOffPages > 0)
                {
                    /*
                     * Set the flags and flush shadow PT entries.
                     */
                    rc = pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs(pVM, pCur, pRam);
                }

                pCur->cAliasedPages = 0;
                pCur->cTmpOffPages  = 0;

                rc = VINF_SUCCESS;
                break;
            }

            /*
             * Invalid.
             */
            default:
                AssertMsgFailed(("Invalid type %d! Corruption!\n",  pCurType->enmKind));
                rc = VERR_PGM_PHYS_HANDLER_IPE;
                break;
        }
    }
    else
    {
        AssertMsgFailed(("Didn't find MMIO Range starting at %#x\n", GCPhys));
        rc = VERR_PGM_HANDLER_NOT_FOUND;
    }

    pgmUnlock(pVM);
    return rc;
}


/**
 * Temporarily turns off the access monitoring of a page within a monitored
 * physical write/all page access handler region.
 *
 * Use this when no further \#PFs are required for that page. Be aware that
 * a page directory sync might reset the flags, and turn on access monitoring
 * for the page.
 *
 * The caller must do required page table modifications.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   GCPhys              The start address of the access handler. This
 *                              must be a fully page aligned range or we risk
 *                              messing up other handlers installed for the
 *                              start and end pages.
 * @param   GCPhysPage          The physical address of the page to turn off
 *                              access monitoring for.
 */
VMMDECL(int)  PGMHandlerPhysicalPageTempOff(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage)
{
    LogFlow(("PGMHandlerPhysicalPageTempOff GCPhysPage=%RGp\n", GCPhysPage));

    pgmLock(pVM);
    /*
     * Validate the range.
     */
    PPGMPHYSHANDLER pCur = (PPGMPHYSHANDLER)RTAvlroGCPhysGet(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys);
    if (RT_LIKELY(pCur))
    {
        if (RT_LIKELY(    GCPhysPage >= pCur->Core.Key
                      &&  GCPhysPage <= pCur->Core.KeyLast))
        {
            Assert(!(pCur->Core.Key & PAGE_OFFSET_MASK));
            Assert((pCur->Core.KeyLast & PAGE_OFFSET_MASK) == PAGE_OFFSET_MASK);

            PPGMPHYSHANDLERTYPEINT pCurType = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
            AssertReturnStmt(   pCurType->enmKind == PGMPHYSHANDLERKIND_WRITE
                             || pCurType->enmKind == PGMPHYSHANDLERKIND_ALL,
                             pgmUnlock(pVM), VERR_ACCESS_DENIED);

            /*
             * Change the page status.
             */
            PPGMPAGE pPage;
            int rc = pgmPhysGetPageEx(pVM, GCPhysPage, &pPage);
            AssertReturnStmt(RT_SUCCESS_NP(rc), pgmUnlock(pVM), rc);
            if (PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) != PGM_PAGE_HNDL_PHYS_STATE_DISABLED)
            {
                PGM_PAGE_SET_HNDL_PHYS_STATE(pPage, PGM_PAGE_HNDL_PHYS_STATE_DISABLED);
                pCur->cTmpOffPages++;

                /* Tell NEM about the protection change (VGA is using this to track dirty pages). */
                if (VM_IS_NEM_ENABLED(pVM))
                {
                    uint8_t     u2State = PGM_PAGE_GET_NEM_STATE(pPage);
                    PGMPAGETYPE enmType = (PGMPAGETYPE)PGM_PAGE_GET_TYPE(pPage);
                    NEMHCNotifyPhysPageProtChanged(pVM, GCPhysPage, PGM_PAGE_GET_HCPHYS(pPage),
                                                   pgmPhysPageCalcNemProtection(pPage, enmType), enmType, &u2State);
                    PGM_PAGE_SET_NEM_STATE(pPage, u2State);
                }
            }
            pgmUnlock(pVM);
            return VINF_SUCCESS;
        }
        pgmUnlock(pVM);
        AssertMsgFailed(("The page %#x is outside the range %#x-%#x\n",
                         GCPhysPage, pCur->Core.Key, pCur->Core.KeyLast));
        return VERR_INVALID_PARAMETER;
    }
    pgmUnlock(pVM);
    AssertMsgFailed(("Specified physical handler start address %#x is invalid.\n", GCPhys));
    return VERR_PGM_HANDLER_NOT_FOUND;
}


/**
 * Resolves an MMIO2 page.
 *
 * Caller as taken the PGM lock.
 *
 * @returns Pointer to the page if valid, NULL otherwise
 * @param   pVM             The cross context VM structure.
 * @param   pDevIns         The device owning it.
 * @param   hMmio2          The MMIO2 region.
 * @param   offMmio2Page    The offset into the region.
 */
static PPGMPAGE pgmPhysResolveMmio2PageLocked(PVMCC pVM, PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2, RTGCPHYS offMmio2Page)
{
    /* Only works if the handle is in the handle table! */
    AssertReturn(hMmio2 != 0, NULL);
    hMmio2--;

    /* Must check the first one for PGMREGMMIO2RANGE_F_FIRST_CHUNK. */
    AssertReturn(hMmio2 < RT_ELEMENTS(pVM->pgm.s.apMmio2RangesR3), NULL);
    PPGMREGMMIO2RANGE pCur = pVM->pgm.s.CTX_SUFF(apMmio2Ranges)[hMmio2];
    AssertReturn(pCur, NULL);
    AssertReturn(pCur->fFlags & PGMREGMMIO2RANGE_F_FIRST_CHUNK, NULL);

    /* Loop thru the sub-ranges till we find the one covering offMmio2. */
    for (;;)
    {
        AssertReturn(pCur->fFlags & PGMREGMMIO2RANGE_F_MMIO2, NULL);
#ifdef IN_RING3
        AssertReturn(pCur->pDevInsR3 == pDevIns, NULL);
#else
        AssertReturn(pCur->pDevInsR3 == pDevIns->pDevInsForR3, NULL);
#endif

        /* Does it match the offset? */
        if (offMmio2Page < pCur->cbReal)
            return &pCur->RamRange.aPages[offMmio2Page >> PAGE_SHIFT];

        /* Advance if we can. */
        AssertReturn(!(pCur->fFlags & PGMREGMMIO2RANGE_F_LAST_CHUNK), NULL);
        offMmio2Page -= pCur->cbReal;
        hMmio2++;
        AssertReturn(hMmio2 < RT_ELEMENTS(pVM->pgm.s.apMmio2RangesR3), NULL);
        pCur = pVM->pgm.s.CTX_SUFF(apMmio2Ranges)[hMmio2];
        AssertReturn(pCur, NULL);
    }
}


/**
 * Replaces an MMIO page with an MMIO2 page.
 *
 * This is a worker for IOMMMIOMapMMIO2Page that works in a similar way to
 * PGMHandlerPhysicalPageTempOff but for an MMIO page. Since an MMIO page has no
 * backing, the caller must provide a replacement page. For various reasons the
 * replacement page must be an MMIO2 page.
 *
 * The caller must do required page table modifications. You can get away
 * without making any modifications since it's an MMIO page, the cost is an extra
 * \#PF which will the resync the page.
 *
 * Call PGMHandlerPhysicalReset() to restore the MMIO page.
 *
 * The caller may still get handler callback even after this call and must be
 * able to deal correctly with such calls. The reason for these callbacks are
 * either that we're executing in the recompiler (which doesn't know about this
 * arrangement) or that we've been restored from saved state (where we won't
 * save the change).
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   GCPhys              The start address of the access handler. This
 *                              must be a fully page aligned range or we risk
 *                              messing up other handlers installed for the
 *                              start and end pages.
 * @param   GCPhysPage          The physical address of the page to turn off
 *                              access monitoring for and replace with the MMIO2
 *                              page.
 * @param   pDevIns             The device instance owning @a hMmio2.
 * @param   hMmio2              Handle to the MMIO2 region containing the page
 *                              to remap in the the MMIO page at @a GCPhys.
 * @param   offMmio2PageRemap   The offset into @a hMmio2 of the MMIO2 page that
 *                              should serve as backing memory.
 *
 * @remark  May cause a page pool flush if used on a page that is already
 *          aliased.
 *
 * @note    This trick does only work reliably if the two pages are never ever
 *          mapped in the same page table. If they are the page pool code will
 *          be confused should either of them be flushed. See the special case
 *          of zero page aliasing mentioned in #3170.
 *
 */
VMMDECL(int)  PGMHandlerPhysicalPageAliasMmio2(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage,
                                               PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2, RTGCPHYS offMmio2PageRemap)
{
    pgmLock(pVM);

    /*
     * Resolve the MMIO2 reference.
     */
    PPGMPAGE pPageRemap = pgmPhysResolveMmio2PageLocked(pVM, pDevIns, hMmio2, offMmio2PageRemap);
    if (RT_LIKELY(pPageRemap))
        AssertMsgReturnStmt(PGM_PAGE_GET_TYPE(pPageRemap) == PGMPAGETYPE_MMIO2,
                            ("hMmio2=%RU64 offMmio2PageRemap=%RGp %R[pgmpage]\n", hMmio2, offMmio2PageRemap, pPageRemap),
                            pgmUnlock(pVM), VERR_PGM_PHYS_NOT_MMIO2);
    else
    {
        pgmUnlock(pVM);
        return VERR_OUT_OF_RANGE;
    }

    /*
     * Lookup and validate the range.
     */
    PPGMPHYSHANDLER pCur = (PPGMPHYSHANDLER)RTAvlroGCPhysGet(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys);
    if (RT_LIKELY(pCur))
    {
        if (RT_LIKELY(   GCPhysPage >= pCur->Core.Key
                      && GCPhysPage <= pCur->Core.KeyLast))
        {
            PPGMPHYSHANDLERTYPEINT pCurType = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
            AssertReturnStmt(pCurType->enmKind == PGMPHYSHANDLERKIND_MMIO, pgmUnlock(pVM), VERR_ACCESS_DENIED);
            AssertReturnStmt(!(pCur->Core.Key & PAGE_OFFSET_MASK), pgmUnlock(pVM), VERR_INVALID_PARAMETER);
            AssertReturnStmt((pCur->Core.KeyLast & PAGE_OFFSET_MASK) == PAGE_OFFSET_MASK, pgmUnlock(pVM), VERR_INVALID_PARAMETER);

            /*
             * Validate the page.
             */
            PPGMPAGE pPage;
            int rc = pgmPhysGetPageEx(pVM, GCPhysPage, &pPage);
            AssertReturnStmt(RT_SUCCESS_NP(rc), pgmUnlock(pVM), rc);
            if (PGM_PAGE_GET_TYPE(pPage) != PGMPAGETYPE_MMIO)
            {
                AssertMsgReturn(PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO2_ALIAS_MMIO,
                                ("GCPhysPage=%RGp %R[pgmpage]\n", GCPhysPage, pPage),
                                VERR_PGM_PHYS_NOT_MMIO2);
                if (PGM_PAGE_GET_HCPHYS(pPage) == PGM_PAGE_GET_HCPHYS(pPageRemap))
                {
                    pgmUnlock(pVM);
                    return VINF_PGM_HANDLER_ALREADY_ALIASED;
                }

                /*
                 * The page is already mapped as some other page, reset it
                 * to an MMIO/ZERO page before doing the new mapping.
                 */
                Log(("PGMHandlerPhysicalPageAliasMmio2: GCPhysPage=%RGp (%R[pgmpage]; %RHp -> %RHp\n",
                     GCPhysPage, pPage, PGM_PAGE_GET_HCPHYS(pPage), PGM_PAGE_GET_HCPHYS(pPageRemap)));
                pgmHandlerPhysicalResetAliasedPage(pVM, pPage, GCPhysPage, false /*fDoAccounting*/);
                pCur->cAliasedPages--;
            }
            Assert(PGM_PAGE_IS_ZERO(pPage));

            /*
             * Do the actual remapping here.
             * This page now serves as an alias for the backing memory specified.
             */
            LogFlow(("PGMHandlerPhysicalPageAliasMmio2: %RGp (%R[pgmpage]) alias for %RU64/%RGp (%R[pgmpage])\n",
                     GCPhysPage, pPage, hMmio2, offMmio2PageRemap, pPageRemap ));
            PGM_PAGE_SET_HCPHYS(pVM, pPage, PGM_PAGE_GET_HCPHYS(pPageRemap));
            PGM_PAGE_SET_TYPE(pVM, pPage, PGMPAGETYPE_MMIO2_ALIAS_MMIO);
            PGM_PAGE_SET_STATE(pVM, pPage, PGM_PAGE_STATE_ALLOCATED);
            PGM_PAGE_SET_PAGEID(pVM, pPage, PGM_PAGE_GET_PAGEID(pPageRemap));
            PGM_PAGE_SET_HNDL_PHYS_STATE(pPage, PGM_PAGE_HNDL_PHYS_STATE_DISABLED);
            pCur->cAliasedPages++;
            Assert(pCur->cAliasedPages <= pCur->cPages);

            /* Flush its TLB entry. */
            pgmPhysInvalidatePageMapTLBEntry(pVM, GCPhysPage);

            /* Tell NEM about the backing and protection change. */
            if (VM_IS_NEM_ENABLED(pVM))
            {
                uint8_t u2State = PGM_PAGE_GET_NEM_STATE(pPage);
                NEMHCNotifyPhysPageChanged(pVM, GCPhysPage, pVM->pgm.s.HCPhysZeroPg, PGM_PAGE_GET_HCPHYS(pPage),
                                           pgmPhysPageCalcNemProtection(pPage, PGMPAGETYPE_MMIO2_ALIAS_MMIO),
                                           PGMPAGETYPE_MMIO2_ALIAS_MMIO, &u2State);
                PGM_PAGE_SET_NEM_STATE(pPage, u2State);
            }
            LogFlow(("PGMHandlerPhysicalPageAliasMmio2: => %R[pgmpage]\n", pPage));
            pgmUnlock(pVM);
            return VINF_SUCCESS;
        }

        pgmUnlock(pVM);
        AssertMsgFailed(("The page %#x is outside the range %#x-%#x\n",
                         GCPhysPage, pCur->Core.Key, pCur->Core.KeyLast));
        return VERR_INVALID_PARAMETER;
    }

    pgmUnlock(pVM);
    AssertMsgFailed(("Specified physical handler start address %#x is invalid.\n", GCPhys));
    return VERR_PGM_HANDLER_NOT_FOUND;
}


/**
 * Replaces an MMIO page with an arbitrary HC page in the shadow page tables.
 *
 * This differs from PGMHandlerPhysicalPageAliasMmio2 in that the page doesn't
 * need to be a known MMIO2 page and that only shadow paging may access the
 * page. The latter distinction is important because the only use for this
 * feature is for mapping the special APIC access page that VT-x uses to detect
 * APIC MMIO operations, the page is shared between all guest CPUs and actually
 * not written to. At least at the moment.
 *
 * The caller must do required page table modifications. You can get away
 * without making any modifications since it's an MMIO page, the cost is an extra
 * \#PF which will the resync the page.
 *
 * Call PGMHandlerPhysicalReset() to restore the MMIO page.
 *
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   GCPhys              The start address of the access handler. This
 *                              must be a fully page aligned range or we risk
 *                              messing up other handlers installed for the
 *                              start and end pages.
 * @param   GCPhysPage          The physical address of the page to turn off
 *                              access monitoring for.
 * @param   HCPhysPageRemap     The physical address of the HC page that
 *                              serves as backing memory.
 *
 * @remark  May cause a page pool flush if used on a page that is already
 *          aliased.
 */
VMMDECL(int)  PGMHandlerPhysicalPageAliasHC(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage, RTHCPHYS HCPhysPageRemap)
{
///    Assert(!IOMIsLockOwner(pVM)); /* We mustn't own any other locks when calling this */
    pgmLock(pVM);

    /*
     * Lookup and validate the range.
     */
    PPGMPHYSHANDLER pCur = (PPGMPHYSHANDLER)RTAvlroGCPhysGet(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys);
    if (RT_LIKELY(pCur))
    {
        if (RT_LIKELY(    GCPhysPage >= pCur->Core.Key
                      &&  GCPhysPage <= pCur->Core.KeyLast))
        {
            PPGMPHYSHANDLERTYPEINT pCurType = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
            AssertReturnStmt(pCurType->enmKind == PGMPHYSHANDLERKIND_MMIO, pgmUnlock(pVM), VERR_ACCESS_DENIED);
            AssertReturnStmt(!(pCur->Core.Key & PAGE_OFFSET_MASK), pgmUnlock(pVM), VERR_INVALID_PARAMETER);
            AssertReturnStmt((pCur->Core.KeyLast & PAGE_OFFSET_MASK) == PAGE_OFFSET_MASK, pgmUnlock(pVM), VERR_INVALID_PARAMETER);

            /*
             * Get and validate the pages.
             */
            PPGMPAGE pPage;
            int rc = pgmPhysGetPageEx(pVM, GCPhysPage, &pPage);
            AssertReturnStmt(RT_SUCCESS_NP(rc), pgmUnlock(pVM), rc);
            if (PGM_PAGE_GET_TYPE(pPage) != PGMPAGETYPE_MMIO)
            {
                pgmUnlock(pVM);
                AssertMsgReturn(PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_SPECIAL_ALIAS_MMIO,
                                ("GCPhysPage=%RGp %R[pgmpage]\n", GCPhysPage, pPage),
                                VERR_PGM_PHYS_NOT_MMIO2);
                return VINF_PGM_HANDLER_ALREADY_ALIASED;
            }
            Assert(PGM_PAGE_IS_ZERO(pPage));

            /*
             * Do the actual remapping here.
             * This page now serves as an alias for the backing memory
             * specified as far as shadow paging is concerned.
             */
            LogFlow(("PGMHandlerPhysicalPageAliasHC: %RGp (%R[pgmpage]) alias for %RHp\n",
                     GCPhysPage, pPage, HCPhysPageRemap));
            PGM_PAGE_SET_HCPHYS(pVM, pPage, HCPhysPageRemap);
            PGM_PAGE_SET_TYPE(pVM, pPage, PGMPAGETYPE_SPECIAL_ALIAS_MMIO);
            PGM_PAGE_SET_STATE(pVM, pPage, PGM_PAGE_STATE_ALLOCATED);
            PGM_PAGE_SET_PAGEID(pVM, pPage, NIL_GMM_PAGEID);
            PGM_PAGE_SET_HNDL_PHYS_STATE(pPage, PGM_PAGE_HNDL_PHYS_STATE_DISABLED);
            pCur->cAliasedPages++;
            Assert(pCur->cAliasedPages <= pCur->cPages);

            /* Flush its TLB entry. */
            pgmPhysInvalidatePageMapTLBEntry(pVM, GCPhysPage);

            /* Tell NEM about the backing and protection change. */
            if (VM_IS_NEM_ENABLED(pVM))
            {
                uint8_t u2State = PGM_PAGE_GET_NEM_STATE(pPage);
                NEMHCNotifyPhysPageChanged(pVM, GCPhysPage, pVM->pgm.s.HCPhysZeroPg, PGM_PAGE_GET_HCPHYS(pPage),
                                           pgmPhysPageCalcNemProtection(pPage, PGMPAGETYPE_SPECIAL_ALIAS_MMIO),
                                           PGMPAGETYPE_SPECIAL_ALIAS_MMIO, &u2State);
                PGM_PAGE_SET_NEM_STATE(pPage, u2State);
            }
            LogFlow(("PGMHandlerPhysicalPageAliasHC: => %R[pgmpage]\n", pPage));
            pgmUnlock(pVM);
            return VINF_SUCCESS;
        }
        pgmUnlock(pVM);
        AssertMsgFailed(("The page %#x is outside the range %#x-%#x\n",
                         GCPhysPage, pCur->Core.Key, pCur->Core.KeyLast));
        return VERR_INVALID_PARAMETER;
    }
    pgmUnlock(pVM);

    AssertMsgFailed(("Specified physical handler start address %#x is invalid.\n", GCPhys));
    return VERR_PGM_HANDLER_NOT_FOUND;
}


/**
 * Checks if a physical range is handled
 *
 * @returns boolean
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      Start physical address earlier passed to PGMR3HandlerPhysicalRegister().
 * @remarks Caller must take the PGM lock...
 * @thread  EMT.
 */
VMMDECL(bool) PGMHandlerPhysicalIsRegistered(PVMCC pVM, RTGCPHYS GCPhys)
{
    /*
     * Find the handler.
     */
    pgmLock(pVM);
    PPGMPHYSHANDLER pCur = pgmHandlerPhysicalLookup(pVM, GCPhys);
    if (pCur)
    {
#ifdef VBOX_STRICT
        Assert(GCPhys >= pCur->Core.Key && GCPhys <= pCur->Core.KeyLast);
        PPGMPHYSHANDLERTYPEINT pCurType = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
        Assert(   pCurType->enmKind == PGMPHYSHANDLERKIND_WRITE
               || pCurType->enmKind == PGMPHYSHANDLERKIND_ALL
               || pCurType->enmKind == PGMPHYSHANDLERKIND_MMIO);
#endif
        pgmUnlock(pVM);
        return true;
    }
    pgmUnlock(pVM);
    return false;
}


/**
 * Checks if it's an disabled all access handler or write access handler at the
 * given address.
 *
 * @returns true if it's an all access handler, false if it's a write access
 *          handler.
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      The address of the page with a disabled handler.
 *
 * @remarks The caller, PGMR3PhysTlbGCPhys2Ptr, must hold the PGM lock.
 */
bool pgmHandlerPhysicalIsAll(PVMCC pVM, RTGCPHYS GCPhys)
{
    pgmLock(pVM);
    PPGMPHYSHANDLER pCur = pgmHandlerPhysicalLookup(pVM, GCPhys);
    if (!pCur)
    {
        pgmUnlock(pVM);
        AssertFailed();
        return true;
    }
    PPGMPHYSHANDLERTYPEINT pCurType = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
    Assert(   pCurType->enmKind == PGMPHYSHANDLERKIND_WRITE
           || pCurType->enmKind == PGMPHYSHANDLERKIND_ALL
           || pCurType->enmKind == PGMPHYSHANDLERKIND_MMIO); /* sanity */
    /* Only whole pages can be disabled. */
    Assert(   pCur->Core.Key     <= (GCPhys & ~(RTGCPHYS)PAGE_OFFSET_MASK)
           && pCur->Core.KeyLast >= (GCPhys | PAGE_OFFSET_MASK));

    bool bRet = pCurType->enmKind != PGMPHYSHANDLERKIND_WRITE;
    pgmUnlock(pVM);
    return bRet;
}

#ifdef VBOX_STRICT

/**
 * State structure used by the PGMAssertHandlerAndFlagsInSync() function
 * and its AVL enumerators.
 */
typedef struct PGMAHAFIS
{
    /** The current physical address. */
    RTGCPHYS    GCPhys;
    /** Number of errors. */
    unsigned    cErrors;
    /** Pointer to the VM. */
    PVM         pVM;
} PGMAHAFIS, *PPGMAHAFIS;


/**
 * Asserts that the handlers+guest-page-tables == ramrange-flags and
 * that the physical addresses associated with virtual handlers are correct.
 *
 * @returns Number of mismatches.
 * @param   pVM     The cross context VM structure.
 */
VMMDECL(unsigned) PGMAssertHandlerAndFlagsInSync(PVM pVM)
{
    PPGM        pPGM = &pVM->pgm.s;
    PGMAHAFIS   State;
    State.GCPhys  = 0;
    State.cErrors = 0;
    State.pVM     = pVM;

    PGM_LOCK_ASSERT_OWNER(pVM);

    /*
     * Check the RAM flags against the handlers.
     */
    for (PPGMRAMRANGE pRam = pPGM->CTX_SUFF(pRamRangesX); pRam; pRam = pRam->CTX_SUFF(pNext))
    {
        const uint32_t cPages = pRam->cb >> PAGE_SHIFT;
        for (uint32_t iPage = 0; iPage < cPages; iPage++)
        {
            PGMPAGE const *pPage = &pRam->aPages[iPage];
            if (PGM_PAGE_HAS_ANY_HANDLERS(pPage))
            {
                State.GCPhys = pRam->GCPhys + (iPage << PAGE_SHIFT);

                /*
                 * Physical first - calculate the state based on the handlers
                 *                  active on the page, then compare.
                 */
                if (PGM_PAGE_HAS_ANY_PHYSICAL_HANDLERS(pPage))
                {
                    /* the first */
                    PPGMPHYSHANDLER pPhys = (PPGMPHYSHANDLER)RTAvlroGCPhysRangeGet(&pPGM->CTX_SUFF(pTrees)->PhysHandlers, State.GCPhys);
                    if (!pPhys)
                    {
                        pPhys = (PPGMPHYSHANDLER)RTAvlroGCPhysGetBestFit(&pPGM->CTX_SUFF(pTrees)->PhysHandlers, State.GCPhys, true);
                        if (    pPhys
                            &&  pPhys->Core.Key > (State.GCPhys + PAGE_SIZE - 1))
                            pPhys = NULL;
                        Assert(!pPhys || pPhys->Core.Key >= State.GCPhys);
                    }
                    if (pPhys)
                    {
                        PPGMPHYSHANDLERTYPEINT pPhysType = (PPGMPHYSHANDLERTYPEINT)MMHyperHeapOffsetToPtr(pVM, pPhys->hType);
                        unsigned uState = pPhysType->uState;

                        /* more? */
                        while (pPhys->Core.KeyLast < (State.GCPhys | PAGE_OFFSET_MASK))
                        {
                            PPGMPHYSHANDLER pPhys2 = (PPGMPHYSHANDLER)RTAvlroGCPhysGetBestFit(&pPGM->CTX_SUFF(pTrees)->PhysHandlers,
                                                                                              pPhys->Core.KeyLast + 1, true);
                            if (    !pPhys2
                                ||  pPhys2->Core.Key > (State.GCPhys | PAGE_OFFSET_MASK))
                                break;
                            PPGMPHYSHANDLERTYPEINT pPhysType2 = (PPGMPHYSHANDLERTYPEINT)MMHyperHeapOffsetToPtr(pVM, pPhys2->hType);
                            uState = RT_MAX(uState, pPhysType2->uState);
                            pPhys = pPhys2;
                        }

                        /* compare.*/
                        if (    PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) != uState
                            &&  PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) != PGM_PAGE_HNDL_PHYS_STATE_DISABLED)
                        {
                            AssertMsgFailed(("ram range vs phys handler flags mismatch. GCPhys=%RGp state=%d expected=%d %s\n",
                                             State.GCPhys, PGM_PAGE_GET_HNDL_PHYS_STATE(pPage), uState, pPhysType->pszDesc));
                            State.cErrors++;
                        }
                    }
                    else
                    {
                        AssertMsgFailed(("ram range vs phys handler mismatch. no handler for GCPhys=%RGp\n", State.GCPhys));
                        State.cErrors++;
                    }
                }
            }
        } /* foreach page in ram range. */
    } /* foreach ram range. */

    /*
     * Do the reverse check for physical handlers.
     */
    /** @todo */

    return State.cErrors;
}

#endif /* VBOX_STRICT */

