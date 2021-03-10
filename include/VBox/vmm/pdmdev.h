/** @file
 * PDM - Pluggable Device Manager, Devices.
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

#ifndef VBOX_INCLUDED_vmm_pdmdev_h
#define VBOX_INCLUDED_vmm_pdmdev_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/pdmcritsect.h>
#include <VBox/vmm/pdmqueue.h>
#include <VBox/vmm/pdmtask.h>
#ifdef IN_RING3
# include <VBox/vmm/pdmthread.h>
#endif
#include <VBox/vmm/pdmifs.h>
#include <VBox/vmm/pdmins.h>
#include <VBox/vmm/pdmcommon.h>
#include <VBox/vmm/pdmpcidev.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/tm.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/err.h>  /* VINF_EM_DBG_STOP, also 120+ source files expecting this. */
#include <iprt/stdarg.h>
#include <iprt/list.h>


RT_C_DECLS_BEGIN

/** @defgroup grp_pdm_device    The PDM Devices API
 * @ingroup grp_pdm
 * @{
 */

/**
 * Construct a device instance for a VM.
 *
 * @returns VBox status.
 * @param   pDevIns     The device instance data. If the registration structure
 *                      is needed, it can be accessed thru  pDevIns->pReg.
 * @param   iInstance   Instance number. Use this to figure out which registers
 *                      and such to use. The instance number is also found in
 *                      pDevIns->iInstance, but since it's likely to be
 *                      frequently used PDM passes it as parameter.
 * @param   pCfg        Configuration node handle for the driver.  This is
 *                      expected to be in high demand in the constructor and is
 *                      therefore passed as an argument.  When using it at other
 *                      times, it can be found in pDevIns->pCfg.
 */
typedef DECLCALLBACK(int)   FNPDMDEVCONSTRUCT(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg);
/** Pointer to a FNPDMDEVCONSTRUCT() function. */
typedef FNPDMDEVCONSTRUCT *PFNPDMDEVCONSTRUCT;

/**
 * Destruct a device instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @returns VBox status.
 * @param   pDevIns     The device instance data.
 *
 * @remarks The device critical section is not entered.  The routine may delete
 *          the critical section, so the caller cannot exit it.
 */
typedef DECLCALLBACK(int)   FNPDMDEVDESTRUCT(PPDMDEVINS pDevIns);
/** Pointer to a FNPDMDEVDESTRUCT() function. */
typedef FNPDMDEVDESTRUCT *PFNPDMDEVDESTRUCT;

/**
 * Device relocation callback.
 *
 * This is called when the instance data has been relocated in raw-mode context
 * (RC).  It is also called when the RC hypervisor selects changes.  The device
 * must fixup all necessary pointers and re-query all interfaces to other RC
 * devices and drivers.
 *
 * Before the RC code is executed the first time, this function will be called
 * with a 0 delta so RC pointer calculations can be one in one place.
 *
 * @param   pDevIns     Pointer to the device instance.
 * @param   offDelta    The relocation delta relative to the old location.
 *
 * @remarks A relocation CANNOT fail.
 *
 * @remarks The device critical section is not entered.  The relocations should
 *          not normally require any locking.
 */
typedef DECLCALLBACK(void) FNPDMDEVRELOCATE(PPDMDEVINS pDevIns, RTGCINTPTR offDelta);
/** Pointer to a FNPDMDEVRELOCATE() function. */
typedef FNPDMDEVRELOCATE *PFNPDMDEVRELOCATE;

/**
 * Power On notification.
 *
 * @returns VBox status.
 * @param   pDevIns     The device instance data.
 *
 * @remarks Caller enters the device critical section.
 */
typedef DECLCALLBACK(void)   FNPDMDEVPOWERON(PPDMDEVINS pDevIns);
/** Pointer to a FNPDMDEVPOWERON() function. */
typedef FNPDMDEVPOWERON *PFNPDMDEVPOWERON;

/**
 * Reset notification.
 *
 * @returns VBox status.
 * @param   pDevIns     The device instance data.
 *
 * @remarks Caller enters the device critical section.
 */
typedef DECLCALLBACK(void)  FNPDMDEVRESET(PPDMDEVINS pDevIns);
/** Pointer to a FNPDMDEVRESET() function. */
typedef FNPDMDEVRESET *PFNPDMDEVRESET;

/**
 * Soft reset notification.
 *
 * This is mainly for emulating the 286 style protected mode exits, in which
 * most devices should remain in their current state.
 *
 * @returns VBox status.
 * @param   pDevIns     The device instance data.
 * @param   fFlags      PDMVMRESET_F_XXX (only bits relevant to soft resets).
 *
 * @remarks Caller enters the device critical section.
 */
typedef DECLCALLBACK(void)  FNPDMDEVSOFTRESET(PPDMDEVINS pDevIns, uint32_t fFlags);
/** Pointer to a FNPDMDEVSOFTRESET() function. */
typedef FNPDMDEVSOFTRESET *PFNPDMDEVSOFTRESET;

/** @name PDMVMRESET_F_XXX - VM reset flags.
 * These flags are used both for FNPDMDEVSOFTRESET and for hardware signalling
 * reset via PDMDevHlpVMReset.
 * @{ */
/** Unknown reason. */
#define PDMVMRESET_F_UNKNOWN            UINT32_C(0x00000000)
/** GIM triggered reset. */
#define PDMVMRESET_F_GIM                UINT32_C(0x00000001)
/** The last source always causing hard resets. */
#define PDMVMRESET_F_LAST_ALWAYS_HARD   PDMVMRESET_F_GIM
/** ACPI triggered reset. */
#define PDMVMRESET_F_ACPI               UINT32_C(0x0000000c)
/** PS/2 system port A (92h) reset. */
#define PDMVMRESET_F_PORT_A             UINT32_C(0x0000000d)
/** Keyboard reset. */
#define PDMVMRESET_F_KBD                UINT32_C(0x0000000e)
/** Tripple fault. */
#define PDMVMRESET_F_TRIPLE_FAULT       UINT32_C(0x0000000f)
/** Reset source mask. */
#define PDMVMRESET_F_SRC_MASK           UINT32_C(0x0000000f)
/** @} */

/**
 * Suspend notification.
 *
 * @returns VBox status.
 * @param   pDevIns     The device instance data.
 * @thread  EMT(0)
 *
 * @remarks Caller enters the device critical section.
 */
typedef DECLCALLBACK(void)  FNPDMDEVSUSPEND(PPDMDEVINS pDevIns);
/** Pointer to a FNPDMDEVSUSPEND() function. */
typedef FNPDMDEVSUSPEND *PFNPDMDEVSUSPEND;

/**
 * Resume notification.
 *
 * @returns VBox status.
 * @param   pDevIns     The device instance data.
 *
 * @remarks Caller enters the device critical section.
 */
typedef DECLCALLBACK(void)  FNPDMDEVRESUME(PPDMDEVINS pDevIns);
/** Pointer to a FNPDMDEVRESUME() function. */
typedef FNPDMDEVRESUME *PFNPDMDEVRESUME;

/**
 * Power Off notification.
 *
 * This is always called when VMR3PowerOff is called.
 * There will be no callback when hot plugging devices.
 *
 * @param   pDevIns     The device instance data.
 * @thread  EMT(0)
 *
 * @remarks Caller enters the device critical section.
 */
typedef DECLCALLBACK(void)   FNPDMDEVPOWEROFF(PPDMDEVINS pDevIns);
/** Pointer to a FNPDMDEVPOWEROFF() function. */
typedef FNPDMDEVPOWEROFF *PFNPDMDEVPOWEROFF;

/**
 * Attach command.
 *
 * This is called to let the device attach to a driver for a specified LUN
 * at runtime. This is not called during VM construction, the device
 * constructor has to attach to all the available drivers.
 *
 * This is like plugging in the keyboard or mouse after turning on the PC.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   iLUN        The logical unit which is being attached.
 * @param   fFlags      Flags, combination of the PDM_TACH_FLAGS_* \#defines.
 *
 * @remarks Caller enters the device critical section.
 */
typedef DECLCALLBACK(int)  FNPDMDEVATTACH(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags);
/** Pointer to a FNPDMDEVATTACH() function. */
typedef FNPDMDEVATTACH *PFNPDMDEVATTACH;

/**
 * Detach notification.
 *
 * This is called when a driver is detaching itself from a LUN of the device.
 * The device should adjust its state to reflect this.
 *
 * This is like unplugging the network cable to use it for the laptop or
 * something while the PC is still running.
 *
 * @param   pDevIns     The device instance.
 * @param   iLUN        The logical unit which is being detached.
 * @param   fFlags      Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 *
 * @remarks Caller enters the device critical section.
 */
typedef DECLCALLBACK(void)  FNPDMDEVDETACH(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags);
/** Pointer to a FNPDMDEVDETACH() function. */
typedef FNPDMDEVDETACH *PFNPDMDEVDETACH;

/**
 * Query the base interface of a logical unit.
 *
 * @returns VBOX status code.
 * @param   pDevIns     The device instance.
 * @param   iLUN        The logicial unit to query.
 * @param   ppBase      Where to store the pointer to the base interface of the LUN.
 *
 * @remarks The device critical section is not entered.
 */
typedef DECLCALLBACK(int) FNPDMDEVQUERYINTERFACE(PPDMDEVINS pDevIns, unsigned iLUN, PPDMIBASE *ppBase);
/** Pointer to a FNPDMDEVQUERYINTERFACE() function. */
typedef FNPDMDEVQUERYINTERFACE *PFNPDMDEVQUERYINTERFACE;

/**
 * Init complete notification (after ring-0 & RC init since 5.1).
 *
 * This can be done to do communication with other devices and other
 * initialization which requires everything to be in place.
 *
 * @returns VBOX status code.
 * @param   pDevIns     The device instance.
 *
 * @remarks Caller enters the device critical section.
 */
typedef DECLCALLBACK(int) FNPDMDEVINITCOMPLETE(PPDMDEVINS pDevIns);
/** Pointer to a FNPDMDEVINITCOMPLETE() function. */
typedef FNPDMDEVINITCOMPLETE *PFNPDMDEVINITCOMPLETE;


/**
 * The context of a pfnMemSetup call.
 */
typedef enum PDMDEVMEMSETUPCTX
{
    /** Invalid zero value. */
    PDMDEVMEMSETUPCTX_INVALID = 0,
    /** After construction. */
    PDMDEVMEMSETUPCTX_AFTER_CONSTRUCTION,
    /** After reset. */
    PDMDEVMEMSETUPCTX_AFTER_RESET,
    /** Type size hack. */
    PDMDEVMEMSETUPCTX_32BIT_HACK = 0x7fffffff
} PDMDEVMEMSETUPCTX;


/**
 * PDM Device Registration Structure.
 *
 * This structure is used when registering a device from VBoxInitDevices() in HC
 * Ring-3.  PDM will continue use till the VM is terminated.
 *
 * @note The first part is the same in every context.
 */
typedef struct PDMDEVREGR3
{
    /** Structure version.  PDM_DEVREGR3_VERSION defines the current version. */
    uint32_t            u32Version;
    /** Reserved, must be zero. */
    uint32_t            uReserved0;
    /** Device name, must match the ring-3 one. */
    char                szName[32];
    /** Flags, combination of the PDM_DEVREG_FLAGS_* \#defines. */
    uint32_t            fFlags;
    /** Device class(es), combination of the PDM_DEVREG_CLASS_* \#defines. */
    uint32_t            fClass;
    /** Maximum number of instances (per VM). */
    uint32_t            cMaxInstances;
    /** The shared data structure version number. */
    uint32_t            uSharedVersion;
    /** Size of the instance data. */
    uint32_t            cbInstanceShared;
    /** Size of the ring-0 instance data. */
    uint32_t            cbInstanceCC;
    /** Size of the raw-mode instance data. */
    uint32_t            cbInstanceRC;
    /** Max number of PCI devices. */
    uint16_t            cMaxPciDevices;
    /** Max number of MSI-X vectors in any of the PCI devices. */
    uint16_t            cMaxMsixVectors;
    /** The description of the device. The UTF-8 string pointed to shall, like this structure,
     * remain unchanged from registration till VM destruction. */
    const char         *pszDescription;

    /** Name of the raw-mode context module (no path).
     * Only evalutated if PDM_DEVREG_FLAGS_RC is set. */
    const char         *pszRCMod;
    /** Name of the ring-0 module (no path).
     * Only evalutated if PDM_DEVREG_FLAGS_R0 is set. */
    const char         *pszR0Mod;

    /** Construct instance - required. */
    PFNPDMDEVCONSTRUCT  pfnConstruct;
    /** Destruct instance - optional.
     * Critical section NOT entered (will be destroyed).  */
    PFNPDMDEVDESTRUCT   pfnDestruct;
    /** Relocation command - optional.
     * Critical section NOT entered. */
    PFNPDMDEVRELOCATE   pfnRelocate;
    /**
     * Memory setup callback.
     *
     * @param   pDevIns         The device instance data.
     * @param   enmCtx          Indicates the context of the call.
     * @remarks The critical section is entered prior to calling this method.
     */
    DECLR3CALLBACKMEMBER(void, pfnMemSetup, (PPDMDEVINS pDevIns, PDMDEVMEMSETUPCTX enmCtx));
    /** Power on notification - optional.
     * Critical section is entered. */
    PFNPDMDEVPOWERON    pfnPowerOn;
    /** Reset notification - optional.
     * Critical section is entered. */
    PFNPDMDEVRESET      pfnReset;
    /** Suspend notification  - optional.
     * Critical section is entered. */
    PFNPDMDEVSUSPEND    pfnSuspend;
    /** Resume notification - optional.
     * Critical section is entered. */
    PFNPDMDEVRESUME     pfnResume;
    /** Attach command - optional.
     * Critical section is entered. */
    PFNPDMDEVATTACH     pfnAttach;
    /** Detach notification - optional.
     * Critical section is entered. */
    PFNPDMDEVDETACH     pfnDetach;
    /** Query a LUN base interface - optional.
     * Critical section is NOT entered. */
    PFNPDMDEVQUERYINTERFACE pfnQueryInterface;
    /** Init complete notification - optional.
     * Critical section is entered. */
    PFNPDMDEVINITCOMPLETE   pfnInitComplete;
    /** Power off notification - optional.
     * Critical section is entered. */
    PFNPDMDEVPOWEROFF   pfnPowerOff;
    /** Software system reset notification - optional.
     * Critical section is entered. */
    PFNPDMDEVSOFTRESET  pfnSoftReset;

    /** @name Reserved for future extensions, must be zero.
     * @{ */
    DECLR3CALLBACKMEMBER(int, pfnReserved0, (PPDMDEVINS pDevIns));
    DECLR3CALLBACKMEMBER(int, pfnReserved1, (PPDMDEVINS pDevIns));
    DECLR3CALLBACKMEMBER(int, pfnReserved2, (PPDMDEVINS pDevIns));
    DECLR3CALLBACKMEMBER(int, pfnReserved3, (PPDMDEVINS pDevIns));
    DECLR3CALLBACKMEMBER(int, pfnReserved4, (PPDMDEVINS pDevIns));
    DECLR3CALLBACKMEMBER(int, pfnReserved5, (PPDMDEVINS pDevIns));
    DECLR3CALLBACKMEMBER(int, pfnReserved6, (PPDMDEVINS pDevIns));
    DECLR3CALLBACKMEMBER(int, pfnReserved7, (PPDMDEVINS pDevIns));
    /** @} */

    /** Initialization safty marker. */
    uint32_t            u32VersionEnd;
} PDMDEVREGR3;
/** Pointer to a PDM Device Structure. */
typedef PDMDEVREGR3 *PPDMDEVREGR3;
/** Const pointer to a PDM Device Structure. */
typedef PDMDEVREGR3 const *PCPDMDEVREGR3;
/** Current DEVREGR3 version number. */
#define PDM_DEVREGR3_VERSION                    PDM_VERSION_MAKE(0xffff, 4, 0)


/** PDM Device Flags.
 * @{ */
/** This flag is used to indicate that the device has a R0 component. */
#define PDM_DEVREG_FLAGS_R0                             UINT32_C(0x00000001)
/** Requires the ring-0 component, ignore configuration values. */
#define PDM_DEVREG_FLAGS_REQUIRE_R0                     UINT32_C(0x00000002)
/** Requires the ring-0 component, ignore configuration values. */
#define PDM_DEVREG_FLAGS_OPT_IN_R0                      UINT32_C(0x00000004)

/** This flag is used to indicate that the device has a RC component. */
#define PDM_DEVREG_FLAGS_RC                             UINT32_C(0x00000010)
/** Requires the raw-mode component, ignore configuration values. */
#define PDM_DEVREG_FLAGS_REQUIRE_RC                     UINT32_C(0x00000020)
/** Requires the raw-mode component, ignore configuration values. */
#define PDM_DEVREG_FLAGS_OPT_IN_RC                      UINT32_C(0x00000040)

/** Convenience: PDM_DEVREG_FLAGS_R0 + PDM_DEVREG_FLAGS_RC  */
#define PDM_DEVREG_FLAGS_RZ                             (PDM_DEVREG_FLAGS_R0 | PDM_DEVREG_FLAGS_RC)

/** @def PDM_DEVREG_FLAGS_HOST_BITS_DEFAULT
 * The bit count for the current host.
 * @note Superfluous, but still around for hysterical raisins.  */
#if HC_ARCH_BITS == 32
# define PDM_DEVREG_FLAGS_HOST_BITS_DEFAULT             UINT32_C(0x00000100)
#elif HC_ARCH_BITS == 64
# define PDM_DEVREG_FLAGS_HOST_BITS_DEFAULT             UINT32_C(0x00000200)
#else
# error Unsupported HC_ARCH_BITS value.
#endif
/** The host bit count mask. */
#define PDM_DEVREG_FLAGS_HOST_BITS_MASK                 UINT32_C(0x00000300)

/** The device support only 32-bit guests. */
#define PDM_DEVREG_FLAGS_GUEST_BITS_32                  UINT32_C(0x00001000)
/** The device support only 64-bit guests. */
#define PDM_DEVREG_FLAGS_GUEST_BITS_64                  UINT32_C(0x00002000)
/** The device support both 32-bit & 64-bit guests. */
#define PDM_DEVREG_FLAGS_GUEST_BITS_32_64               UINT32_C(0x00003000)
/** @def PDM_DEVREG_FLAGS_GUEST_BITS_DEFAULT
 * The guest bit count for the current compilation. */
#if GC_ARCH_BITS == 32
# define PDM_DEVREG_FLAGS_GUEST_BITS_DEFAULT            PDM_DEVREG_FLAGS_GUEST_BITS_32
#elif GC_ARCH_BITS == 64
# define PDM_DEVREG_FLAGS_GUEST_BITS_DEFAULT            PDM_DEVREG_FLAGS_GUEST_BITS_32_64
#else
# error Unsupported GC_ARCH_BITS value.
#endif
/** The guest bit count mask. */
#define PDM_DEVREG_FLAGS_GUEST_BITS_MASK                UINT32_C(0x00003000)

/** A convenience. */
#define PDM_DEVREG_FLAGS_DEFAULT_BITS                   (PDM_DEVREG_FLAGS_GUEST_BITS_DEFAULT | PDM_DEVREG_FLAGS_HOST_BITS_DEFAULT)

/** Indicates that the device needs to be notified before the drivers when suspending. */
#define PDM_DEVREG_FLAGS_FIRST_SUSPEND_NOTIFICATION     UINT32_C(0x00010000)
/** Indicates that the device needs to be notified before the drivers when powering off. */
#define PDM_DEVREG_FLAGS_FIRST_POWEROFF_NOTIFICATION    UINT32_C(0x00020000)
/** Indicates that the device needs to be notified before the drivers when resetting. */
#define PDM_DEVREG_FLAGS_FIRST_RESET_NOTIFICATION       UINT32_C(0x00040000)

/** This flag is used to indicate that the device has been converted to the
 *  new device style. */
#define PDM_DEVREG_FLAGS_NEW_STYLE                      UINT32_C(0x80000000)

/** @} */


/** PDM Device Classes.
 * The order is important, lower bit earlier instantiation.
 * @{ */
/** Architecture device. */
#define PDM_DEVREG_CLASS_ARCH                   RT_BIT(0)
/** Architecture BIOS device. */
#define PDM_DEVREG_CLASS_ARCH_BIOS              RT_BIT(1)
/** PCI bus brigde. */
#define PDM_DEVREG_CLASS_BUS_PCI                RT_BIT(2)
/** ISA bus brigde. */
#define PDM_DEVREG_CLASS_BUS_ISA                RT_BIT(3)
/** Input device (mouse, keyboard, joystick, HID, ...). */
#define PDM_DEVREG_CLASS_INPUT                  RT_BIT(4)
/** Interrupt controller (PIC). */
#define PDM_DEVREG_CLASS_PIC                    RT_BIT(5)
/** Interval controoler (PIT). */
#define PDM_DEVREG_CLASS_PIT                    RT_BIT(6)
/** RTC/CMOS. */
#define PDM_DEVREG_CLASS_RTC                    RT_BIT(7)
/** DMA controller. */
#define PDM_DEVREG_CLASS_DMA                    RT_BIT(8)
/** VMM Device. */
#define PDM_DEVREG_CLASS_VMM_DEV                RT_BIT(9)
/** Graphics device, like VGA. */
#define PDM_DEVREG_CLASS_GRAPHICS               RT_BIT(10)
/** Storage controller device. */
#define PDM_DEVREG_CLASS_STORAGE                RT_BIT(11)
/** Network interface controller. */
#define PDM_DEVREG_CLASS_NETWORK                RT_BIT(12)
/** Audio. */
#define PDM_DEVREG_CLASS_AUDIO                  RT_BIT(13)
/** USB HIC. */
#define PDM_DEVREG_CLASS_BUS_USB                RT_BIT(14)
/** ACPI. */
#define PDM_DEVREG_CLASS_ACPI                   RT_BIT(15)
/** Serial controller device. */
#define PDM_DEVREG_CLASS_SERIAL                 RT_BIT(16)
/** Parallel controller device */
#define PDM_DEVREG_CLASS_PARALLEL               RT_BIT(17)
/** Host PCI pass-through device */
#define PDM_DEVREG_CLASS_HOST_DEV               RT_BIT(18)
/** Misc devices (always last). */
#define PDM_DEVREG_CLASS_MISC                   RT_BIT(31)
/** @} */


/**
 * PDM Device Registration Structure, ring-0.
 *
 * This structure is used when registering a device from VBoxInitDevices() in HC
 * Ring-0.  PDM will continue use till the VM is terminated.
 */
typedef struct PDMDEVREGR0
{
    /** Structure version. PDM_DEVREGR0_VERSION defines the current version. */
    uint32_t            u32Version;
    /** Reserved, must be zero. */
    uint32_t            uReserved0;
    /** Device name, must match the ring-3 one. */
    char                szName[32];
    /** Flags, combination of the PDM_DEVREG_FLAGS_* \#defines. */
    uint32_t            fFlags;
    /** Device class(es), combination of the PDM_DEVREG_CLASS_* \#defines. */
    uint32_t            fClass;
    /** Maximum number of instances (per VM). */
    uint32_t            cMaxInstances;
    /** The shared data structure version number. */
    uint32_t            uSharedVersion;
    /** Size of the instance data. */
    uint32_t            cbInstanceShared;
    /** Size of the ring-0 instance data. */
    uint32_t            cbInstanceCC;
    /** Size of the raw-mode instance data. */
    uint32_t            cbInstanceRC;
    /** Max number of PCI devices. */
    uint16_t            cMaxPciDevices;
    /** Max number of MSI-X vectors in any of the PCI devices. */
    uint16_t            cMaxMsixVectors;
    /** The description of the device. The UTF-8 string pointed to shall, like this structure,
     * remain unchanged from registration till VM destruction. */
    const char         *pszDescription;

    /**
     * Early construction callback (optional).
     *
     * This is called right after the device instance structure has been allocated
     * and before the ring-3 constructor gets called.
     *
     * @returns VBox status code.
     * @param   pDevIns         The device instance data.
     * @note    The destructure is always called, regardless of the return status.
     */
    DECLR0CALLBACKMEMBER(int, pfnEarlyConstruct, (PPDMDEVINS pDevIns));

    /**
     * Regular construction callback (optional).
     *
     * This is called after (or during) the ring-3 constructor.
     *
     * @returns VBox status code.
     * @param   pDevIns         The device instance data.
     * @note    The destructure is always called, regardless of the return status.
     */
    DECLR0CALLBACKMEMBER(int, pfnConstruct, (PPDMDEVINS pDevIns));

    /**
     * Destructor (optional).
     *
     * This is called after the ring-3 destruction.  This is not called if ring-3
     * fails to trigger it (e.g. process is killed or crashes).
     *
     * @param   pDevIns         The device instance data.
     */
    DECLR0CALLBACKMEMBER(void, pfnDestruct, (PPDMDEVINS pDevIns));

    /**
     * Final destructor (optional).
     *
     * This is called right before the memory is freed, which happens when the
     * VM/GVM object is destroyed.  This is always called.
     *
     * @param   pDevIns         The device instance data.
     */
    DECLR0CALLBACKMEMBER(void, pfnFinalDestruct, (PPDMDEVINS pDevIns));

    /**
     * Generic request handler (optional).
     *
     * @param   pDevIns         The device instance data.
     * @param   uReq            Device specific request.
     * @param   uArg            Request argument.
     */
    DECLR0CALLBACKMEMBER(int, pfnRequest, (PPDMDEVINS pDevIns, uint32_t uReq, uint64_t uArg));

    /** @name Reserved for future extensions, must be zero.
     * @{ */
    DECLR0CALLBACKMEMBER(int, pfnReserved0, (PPDMDEVINS pDevIns));
    DECLR0CALLBACKMEMBER(int, pfnReserved1, (PPDMDEVINS pDevIns));
    DECLR0CALLBACKMEMBER(int, pfnReserved2, (PPDMDEVINS pDevIns));
    DECLR0CALLBACKMEMBER(int, pfnReserved3, (PPDMDEVINS pDevIns));
    DECLR0CALLBACKMEMBER(int, pfnReserved4, (PPDMDEVINS pDevIns));
    DECLR0CALLBACKMEMBER(int, pfnReserved5, (PPDMDEVINS pDevIns));
    DECLR0CALLBACKMEMBER(int, pfnReserved6, (PPDMDEVINS pDevIns));
    DECLR0CALLBACKMEMBER(int, pfnReserved7, (PPDMDEVINS pDevIns));
    /** @} */

    /** Initialization safty marker. */
    uint32_t            u32VersionEnd;
} PDMDEVREGR0;
/** Pointer to a ring-0 PDM device registration structure. */
typedef PDMDEVREGR0 *PPDMDEVREGR0;
/** Pointer to a const ring-0 PDM device registration structure. */
typedef PDMDEVREGR0 const *PCPDMDEVREGR0;
/** Current DEVREGR0 version number. */
#define PDM_DEVREGR0_VERSION                    PDM_VERSION_MAKE(0xff80, 1, 0)


/**
 * PDM Device Registration Structure, raw-mode
 *
 * At the moment, this structure is mostly here to match the other two contexts.
 */
typedef struct PDMDEVREGRC
{
    /** Structure version. PDM_DEVREGRC_VERSION defines the current version. */
    uint32_t            u32Version;
    /** Reserved, must be zero. */
    uint32_t            uReserved0;
    /** Device name, must match the ring-3 one. */
    char                szName[32];
    /** Flags, combination of the PDM_DEVREG_FLAGS_* \#defines. */
    uint32_t            fFlags;
    /** Device class(es), combination of the PDM_DEVREG_CLASS_* \#defines. */
    uint32_t            fClass;
    /** Maximum number of instances (per VM). */
    uint32_t            cMaxInstances;
    /** The shared data structure version number. */
    uint32_t            uSharedVersion;
    /** Size of the instance data. */
    uint32_t            cbInstanceShared;
    /** Size of the ring-0 instance data. */
    uint32_t            cbInstanceCC;
    /** Size of the raw-mode instance data. */
    uint32_t            cbInstanceRC;
    /** Max number of PCI devices. */
    uint16_t            cMaxPciDevices;
    /** Max number of MSI-X vectors in any of the PCI devices. */
    uint16_t            cMaxMsixVectors;
    /** The description of the device. The UTF-8 string pointed to shall, like this structure,
     * remain unchanged from registration till VM destruction. */
    const char         *pszDescription;

    /**
     * Constructor callback.
     *
     * This is called much later than both the ring-0 and ring-3 constructors, since
     * raw-mode v2 require a working VMM to run actual code.
     *
     * @returns VBox status code.
     * @param   pDevIns         The device instance data.
     * @note    The destructure is always called, regardless of the return status.
     */
    DECLRGCALLBACKMEMBER(int, pfnConstruct, (PPDMDEVINS pDevIns));

    /** @name Reserved for future extensions, must be zero.
     * @{ */
    DECLRCCALLBACKMEMBER(int, pfnReserved0, (PPDMDEVINS pDevIns));
    DECLRCCALLBACKMEMBER(int, pfnReserved1, (PPDMDEVINS pDevIns));
    DECLRCCALLBACKMEMBER(int, pfnReserved2, (PPDMDEVINS pDevIns));
    DECLRCCALLBACKMEMBER(int, pfnReserved3, (PPDMDEVINS pDevIns));
    DECLRCCALLBACKMEMBER(int, pfnReserved4, (PPDMDEVINS pDevIns));
    DECLRCCALLBACKMEMBER(int, pfnReserved5, (PPDMDEVINS pDevIns));
    DECLRCCALLBACKMEMBER(int, pfnReserved6, (PPDMDEVINS pDevIns));
    DECLRCCALLBACKMEMBER(int, pfnReserved7, (PPDMDEVINS pDevIns));
    /** @} */

    /** Initialization safty marker. */
    uint32_t            u32VersionEnd;
} PDMDEVREGRC;
/** Pointer to a raw-mode PDM device registration structure. */
typedef PDMDEVREGRC *PPDMDEVREGRC;
/** Pointer to a const raw-mode PDM device registration structure. */
typedef PDMDEVREGRC const *PCPDMDEVREGRC;
/** Current DEVREGRC version number. */
#define PDM_DEVREGRC_VERSION                    PDM_VERSION_MAKE(0xff81, 1, 0)



/** @def PDM_DEVREG_VERSION
 * Current DEVREG version number. */
/** @typedef PDMDEVREGR3
 * A current context PDM device registration structure. */
/** @typedef PPDMDEVREGR3
 * Pointer to a current context PDM device registration structure. */
/** @typedef PCPDMDEVREGR3
 * Pointer to a const current context PDM device registration structure. */
#if defined(IN_RING3) || defined(DOXYGEN_RUNNING)
# define PDM_DEVREG_VERSION                     PDM_DEVREGR3_VERSION
typedef PDMDEVREGR3                             PDMDEVREG;
typedef PPDMDEVREGR3                            PPDMDEVREG;
typedef PCPDMDEVREGR3                           PCPDMDEVREG;
#elif defined(IN_RING0)
# define PDM_DEVREG_VERSION                     PDM_DEVREGR0_VERSION
typedef PDMDEVREGR0                             PDMDEVREG;
typedef PPDMDEVREGR0                            PPDMDEVREG;
typedef PCPDMDEVREGR0                           PCPDMDEVREG;
#elif defined(IN_RC)
# define PDM_DEVREG_VERSION                     PDM_DEVREGRC_VERSION
typedef PDMDEVREGRC                             PDMDEVREG;
typedef PPDMDEVREGRC                            PPDMDEVREG;
typedef PCPDMDEVREGRC                           PCPDMDEVREG;
#else
# error "Not IN_RING3, IN_RING0 or IN_RC"
#endif


/**
 * Device registrations for ring-0 modules.
 *
 * This structure is used directly and must therefore reside in persistent
 * memory (i.e. the data section).
 */
typedef struct PDMDEVMODREGR0
{
    /** The structure version (PDM_DEVMODREGR0_VERSION). */
    uint32_t            u32Version;
    /** Number of devices in the array papDevRegs points to. */
    uint32_t            cDevRegs;
    /** Pointer to device registration structures. */
    PCPDMDEVREGR0      *papDevRegs;
    /** The ring-0 module handle - PDM internal, fingers off. */
    void               *hMod;
    /** List entry - PDM internal, fingers off. */
    RTLISTNODE          ListEntry;
} PDMDEVMODREGR0;
/** Pointer to device registriations for a ring-0 module. */
typedef PDMDEVMODREGR0 *PPDMDEVMODREGR0;
/** Current PDMDEVMODREGR0 version number. */
#define PDM_DEVMODREGR0_VERSION                 PDM_VERSION_MAKE(0xff85, 1, 0)


/** @name IRQ Level for use with the *SetIrq APIs.
 * @{
 */
/** Assert the IRQ (can assume value 1). */
#define PDM_IRQ_LEVEL_HIGH                      RT_BIT(0)
/** Deassert the IRQ (can assume value 0). */
#define PDM_IRQ_LEVEL_LOW                       0
/** flip-flop - deassert and then assert the IRQ again immediately. */
#define PDM_IRQ_LEVEL_FLIP_FLOP                 (RT_BIT(1) | PDM_IRQ_LEVEL_HIGH)
/** @} */

/**
 * Registration record for MSI/MSI-X emulation.
 */
typedef struct PDMMSIREG
{
    /** Number of MSI interrupt vectors, 0 if MSI not supported */
    uint16_t   cMsiVectors;
    /** Offset of MSI capability */
    uint8_t    iMsiCapOffset;
    /** Offset of next capability to MSI */
    uint8_t    iMsiNextOffset;
    /** If we support 64-bit MSI addressing */
    bool       fMsi64bit;
    /** If we do not support per-vector masking */
    bool       fMsiNoMasking;

    /** Number of MSI-X interrupt vectors, 0 if MSI-X not supported */
    uint16_t   cMsixVectors;
    /** Offset of MSI-X capability */
    uint8_t    iMsixCapOffset;
    /** Offset of next capability to MSI-X */
    uint8_t    iMsixNextOffset;
    /** Value of PCI BAR (base addresss register) assigned by device for MSI-X page access */
    uint8_t    iMsixBar;
} PDMMSIREG;
typedef PDMMSIREG *PPDMMSIREG;

/**
 * PCI Bus registration structure.
 * All the callbacks, except the PCIBIOS hack, are working on PCI devices.
 */
typedef struct PDMPCIBUSREGR3
{
    /** Structure version number. PDM_PCIBUSREGR3_VERSION defines the current version. */
    uint32_t            u32Version;

    /**
     * Registers the device with the default PCI bus.
     *
     * @returns VBox status code.
     * @param   pDevIns         Device instance of the PCI Bus.
     * @param   pPciDev         The PCI device structure.
     * @param   fFlags          Reserved for future use, PDMPCIDEVREG_F_MBZ.
     * @param   uPciDevNo       PDMPCIDEVREG_DEV_NO_FIRST_UNUSED, or a specific
     *                          device number (0-31).
     * @param   uPciFunNo       PDMPCIDEVREG_FUN_NO_FIRST_UNUSED, or a specific
     *                          function number (0-7).
     * @param   pszName         Device name (static but not unique).
     *
     * @remarks Caller enters the PDM critical section.
     */
    DECLR3CALLBACKMEMBER(int, pfnRegisterR3,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t fFlags,
                                             uint8_t uPciDevNo, uint8_t uPciFunNo, const char *pszName));

    /**
     * Initialize MSI or MSI-X emulation support in a PCI device.
     *
     * This cannot handle all corner cases of the MSI/MSI-X spec, but for the
     * vast majority of device emulation it covers everything necessary. It's
     * fully automatic, taking care of all BAR and config space requirements,
     * and interrupt delivery is done using PDMDevHlpPCISetIrq and friends.
     * When MSI/MSI-X is enabled then the iIrq parameter is redefined to take
     * the vector number (otherwise it has the usual INTA-D meaning for PCI).
     *
     * A device not using this can still offer MSI/MSI-X. In this case it's
     * completely up to the device (in the MSI-X case) to create/register the
     * necessary MMIO BAR, handle all config space/BAR updating and take care
     * of delivering the interrupts appropriately.
     *
     * @returns VBox status code.
     * @param   pDevIns         Device instance of the PCI Bus.
     * @param   pPciDev         The PCI device structure.
     * @param   pMsiReg         MSI emulation registration structure
     * @remarks Caller enters the PDM critical section.
     */
    DECLR3CALLBACKMEMBER(int, pfnRegisterMsiR3,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, PPDMMSIREG pMsiReg));

    /**
     * Registers a I/O region (memory mapped or I/O ports) for a PCI device.
     *
     * @returns VBox status code.
     * @param   pDevIns         Device instance of the PCI Bus.
     * @param   pPciDev         The PCI device structure.
     * @param   iRegion         The region number.
     * @param   cbRegion        Size of the region.
     * @param   enmType         PCI_ADDRESS_SPACE_MEM, PCI_ADDRESS_SPACE_IO or
     *                          PCI_ADDRESS_SPACE_MEM_PREFETCH, optionally with
     *                          PCI_ADDRESS_SPACE_BAR64 or'ed in.
     * @param   fFlags          PDMPCIDEV_IORGN_F_XXX.
     * @param   hHandle         An I/O port, MMIO or MMIO2 handle according to
     *                          @a fFlags, UINT64_MAX if no handle is passed
     *                          (old style).
     * @param   pfnMapUnmap     Callback for doing the mapping. Optional if a handle
     *                          is given.
     * @remarks Caller enters the PDM critical section.
     */
    DECLR3CALLBACKMEMBER(int, pfnIORegionRegisterR3,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                     RTGCPHYS cbRegion, PCIADDRESSSPACE enmType, uint32_t fFlags,
                                                     uint64_t hHandle, PFNPCIIOREGIONMAP pfnMapUnmap));

    /**
     * Register PCI configuration space read/write intercept callbacks.
     *
     * @param   pDevIns         Device instance of the PCI Bus.
     * @param   pPciDev         The PCI device structure.
     * @param   pfnRead         Pointer to the user defined PCI config read function.
     * @param   pfnWrite        Pointer to the user defined PCI config write function.
     *                          to call default PCI config write function. Can be NULL.
     * @remarks Caller enters the PDM critical section.
     * @thread  EMT
     */
    DECLR3CALLBACKMEMBER(void, pfnInterceptConfigAccesses,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                           PFNPCICONFIGREAD pfnRead, PFNPCICONFIGWRITE pfnWrite));

    /**
     * Perform a PCI configuration space write, bypassing interception.
     *
     * This is for devices that make use of PDMDevHlpPCIInterceptConfigAccesses().
     *
     * @returns Strict VBox status code (mainly DBGFSTOP).
     * @param   pDevIns         Device instance of the PCI Bus.
     * @param   pPciDev         The PCI device which config space is being read.
     * @param   uAddress        The config space address.
     * @param   cb              The size of the read: 1, 2 or 4 bytes.
     * @param   u32Value        The value to write.
     * @note    The caller (PDM) does not enter the PDM critsect, but it is possible
     *          that the (root) bus will have done that already.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnConfigWrite,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                       uint32_t uAddress, unsigned cb, uint32_t u32Value));

    /**
     * Perform a PCI configuration space read, bypassing interception.
     *
     * This is for devices that make use of PDMDevHlpPCIInterceptConfigAccesses().
     *
     * @returns Strict VBox status code (mainly DBGFSTOP).
     * @param   pDevIns         Device instance of the PCI Bus.
     * @param   pPciDev         The PCI device which config space is being read.
     * @param   uAddress        The config space address.
     * @param   cb              The size of the read: 1, 2 or 4 bytes.
     * @param   pu32Value       Where to return the value.
     * @note    The caller (PDM) does not enter the PDM critsect, but it is possible
     *          that the (root) bus will have done that already.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnConfigRead,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                      uint32_t uAddress, unsigned cb, uint32_t *pu32Value));

    /**
     * Set the IRQ for a PCI device.
     *
     * @param   pDevIns         Device instance of the PCI Bus.
     * @param   pPciDev         The PCI device structure.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     * @remarks Caller enters the PDM critical section.
     */
    DECLR3CALLBACKMEMBER(void, pfnSetIrqR3,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel, uint32_t uTagSrc));

    /** Marks the end of the structure with PDM_PCIBUSREGR3_VERSION. */
    uint32_t            u32EndVersion;
} PDMPCIBUSREGR3;
/** Pointer to a PCI bus registration structure. */
typedef PDMPCIBUSREGR3 *PPDMPCIBUSREGR3;
/** Current PDMPCIBUSREGR3 version number. */
#define PDM_PCIBUSREGR3_VERSION                 PDM_VERSION_MAKE(0xff86, 2, 0)

/**
 * PCI Bus registration structure for ring-0.
 */
typedef struct PDMPCIBUSREGR0
{
    /** Structure version number. PDM_PCIBUSREGR0_VERSION defines the current version. */
    uint32_t            u32Version;
    /** The PCI bus number (from ring-3 registration). */
    uint32_t            iBus;
    /**
     * Set the IRQ for a PCI device.
     *
     * @param   pDevIns         Device instance of the PCI Bus.
     * @param   pPciDev         The PCI device structure.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     * @remarks Caller enters the PDM critical section.
     */
    DECLR0CALLBACKMEMBER(void, pfnSetIrq,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel, uint32_t uTagSrc));
    /** Marks the end of the structure with PDM_PCIBUSREGR0_VERSION. */
    uint32_t            u32EndVersion;
} PDMPCIBUSREGR0;
/** Pointer to a PCI bus ring-0 registration structure. */
typedef PDMPCIBUSREGR0 *PPDMPCIBUSREGR0;
/** Current PDMPCIBUSREGR0 version number. */
#define PDM_PCIBUSREGR0_VERSION                  PDM_VERSION_MAKE(0xff87, 1, 0)

/**
 * PCI Bus registration structure for raw-mode.
 */
typedef struct PDMPCIBUSREGRC
{
    /** Structure version number. PDM_PCIBUSREGRC_VERSION defines the current version. */
    uint32_t            u32Version;
    /** The PCI bus number (from ring-3 registration). */
    uint32_t            iBus;
    /**
     * Set the IRQ for a PCI device.
     *
     * @param   pDevIns         Device instance of the PCI Bus.
     * @param   pPciDev         The PCI device structure.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     * @remarks Caller enters the PDM critical section.
     */
    DECLRCCALLBACKMEMBER(void, pfnSetIrq,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel, uint32_t uTagSrc));
    /** Marks the end of the structure with PDM_PCIBUSREGRC_VERSION. */
    uint32_t            u32EndVersion;
} PDMPCIBUSREGRC;
/** Pointer to a PCI bus raw-mode registration structure. */
typedef PDMPCIBUSREGRC *PPDMPCIBUSREGRC;
/** Current PDMPCIBUSREGRC version number. */
#define PDM_PCIBUSREGRC_VERSION                  PDM_VERSION_MAKE(0xff88, 1, 0)

/** PCI bus registration structure for the current context. */
typedef CTX_SUFF(PDMPCIBUSREG)  PDMPCIBUSREGCC;
/** Pointer to a PCI bus registration structure for the current context. */
typedef CTX_SUFF(PPDMPCIBUSREG) PPDMPCIBUSREGCC;
/** PCI bus registration structure version for the current context. */
#define PDM_PCIBUSREGCC_VERSION CTX_MID(PDM_PCIBUSREG,_VERSION)


/**
 * PCI Bus RC helpers.
 */
typedef struct PDMPCIHLPRC
{
    /** Structure version. PDM_PCIHLPRC_VERSION defines the current version. */
    uint32_t                    u32Version;

    /**
     * Set an ISA IRQ.
     *
     * @param   pDevIns         PCI device instance.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     * @thread  EMT only.
     */
    DECLRCCALLBACKMEMBER(void,  pfnIsaSetIrq,(PPDMDEVINS pDevIns, int iIrq, int iLevel, uint32_t uTagSrc));

    /**
     * Set an I/O-APIC IRQ.
     *
     * @param   pDevIns         PCI device instance.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     * @thread  EMT only.
     */
    DECLRCCALLBACKMEMBER(void,  pfnIoApicSetIrq,(PPDMDEVINS pDevIns, int iIrq, int iLevel, uint32_t uTagSrc));

    /**
     * Send an MSI.
     *
     * @param   pDevIns         PCI device instance.
     * @param   GCPhys          Physical address MSI request was written.
     * @param   uValue          Value written.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     * @thread  EMT only.
     */
    DECLRCCALLBACKMEMBER(void,  pfnIoApicSendMsi,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t uValue, uint32_t uTagSrc));


    /**
     * Acquires the PDM lock.
     *
     * @returns VINF_SUCCESS on success.
     * @returns rc if we failed to acquire the lock.
     * @param   pDevIns         The PCI device instance.
     * @param   rc              What to return if we fail to acquire the lock.
     */
    DECLRCCALLBACKMEMBER(int,   pfnLock,(PPDMDEVINS pDevIns, int rc));

    /**
     * Releases the PDM lock.
     *
     * @param   pDevIns         The PCI device instance.
     */
    DECLRCCALLBACKMEMBER(void,  pfnUnlock,(PPDMDEVINS pDevIns));

    /**
     * Gets a bus by it's PDM ordinal (typically the parent bus).
     *
     * @returns Pointer to the device instance of the bus.
     * @param   pDevIns         The PCI bus device instance.
     * @param   idxPdmBus       The PDM ordinal value of the bus to get.
     */
    DECLRCCALLBACKMEMBER(PPDMDEVINS, pfnGetBusByNo,(PPDMDEVINS pDevIns, uint32_t idxPdmBus));

    /** Just a safety precaution. */
    uint32_t                    u32TheEnd;
} PDMPCIHLPRC;
/** Pointer to PCI helpers. */
typedef RCPTRTYPE(PDMPCIHLPRC *) PPDMPCIHLPRC;
/** Pointer to const PCI helpers. */
typedef RCPTRTYPE(const PDMPCIHLPRC *) PCPDMPCIHLPRC;

/** Current PDMPCIHLPRC version number. */
#define PDM_PCIHLPRC_VERSION                    PDM_VERSION_MAKE(0xfffd, 3, 0)


/**
 * PCI Bus R0 helpers.
 */
typedef struct PDMPCIHLPR0
{
    /** Structure version. PDM_PCIHLPR0_VERSION defines the current version. */
    uint32_t                    u32Version;

    /**
     * Set an ISA IRQ.
     *
     * @param   pDevIns         PCI device instance.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     * @thread  EMT only.
     */
    DECLR0CALLBACKMEMBER(void,  pfnIsaSetIrq,(PPDMDEVINS pDevIns, int iIrq, int iLevel, uint32_t uTagSrc));

    /**
     * Set an I/O-APIC IRQ.
     *
     * @param   pDevIns         PCI device instance.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     * @thread  EMT only.
     */
    DECLR0CALLBACKMEMBER(void,  pfnIoApicSetIrq,(PPDMDEVINS pDevIns, int iIrq, int iLevel, uint32_t uTagSrc));

    /**
     * Send an MSI.
     *
     * @param   pDevIns         PCI device instance.
     * @param   GCPhys          Physical address MSI request was written.
     * @param   uValue          Value written.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     * @thread  EMT only.
     */
    DECLR0CALLBACKMEMBER(void,  pfnIoApicSendMsi,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t uValue, uint32_t uTagSrc));

    /**
     * Acquires the PDM lock.
     *
     * @returns VINF_SUCCESS on success.
     * @returns rc if we failed to acquire the lock.
     * @param   pDevIns         The PCI device instance.
     * @param   rc              What to return if we fail to acquire the lock.
     */
    DECLR0CALLBACKMEMBER(int,   pfnLock,(PPDMDEVINS pDevIns, int rc));

    /**
     * Releases the PDM lock.
     *
     * @param   pDevIns         The PCI device instance.
     */
    DECLR0CALLBACKMEMBER(void,  pfnUnlock,(PPDMDEVINS pDevIns));

    /**
     * Gets a bus by it's PDM ordinal (typically the parent bus).
     *
     * @returns Pointer to the device instance of the bus.
     * @param   pDevIns         The PCI bus device instance.
     * @param   idxPdmBus       The PDM ordinal value of the bus to get.
     */
    DECLR0CALLBACKMEMBER(PPDMDEVINS, pfnGetBusByNo,(PPDMDEVINS pDevIns, uint32_t idxPdmBus));

    /** Just a safety precaution. */
    uint32_t                    u32TheEnd;
} PDMPCIHLPR0;
/** Pointer to PCI helpers. */
typedef R0PTRTYPE(PDMPCIHLPR0 *) PPDMPCIHLPR0;
/** Pointer to const PCI helpers. */
typedef R0PTRTYPE(const PDMPCIHLPR0 *) PCPDMPCIHLPR0;

/** Current PDMPCIHLPR0 version number. */
#define PDM_PCIHLPR0_VERSION                    PDM_VERSION_MAKE(0xfffc, 5, 0)

/**
 * PCI device helpers.
 */
typedef struct PDMPCIHLPR3
{
    /** Structure version. PDM_PCIHLPR3_VERSION defines the current version. */
    uint32_t                    u32Version;

    /**
     * Set an ISA IRQ.
     *
     * @param   pDevIns         The PCI device instance.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     */
    DECLR3CALLBACKMEMBER(void,  pfnIsaSetIrq,(PPDMDEVINS pDevIns, int iIrq, int iLevel, uint32_t uTagSrc));

    /**
     * Set an I/O-APIC IRQ.
     *
     * @param   pDevIns         The PCI device instance.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     */
    DECLR3CALLBACKMEMBER(void,  pfnIoApicSetIrq,(PPDMDEVINS pDevIns, int iIrq, int iLevel, uint32_t uTagSrc));

    /**
     * Send an MSI.
     *
     * @param   pDevIns         PCI device instance.
     * @param   GCPhys          Physical address MSI request was written.
     * @param   uValue          Value written.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     */
    DECLR3CALLBACKMEMBER(void,  pfnIoApicSendMsi,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t uValue, uint32_t uTagSrc));

    /**
     * Acquires the PDM lock.
     *
     * @returns VINF_SUCCESS on success.
     * @returns Fatal error on failure.
     * @param   pDevIns         The PCI device instance.
     * @param   rc              Dummy for making the interface identical to the RC and R0 versions.
     */
    DECLR3CALLBACKMEMBER(int,   pfnLock,(PPDMDEVINS pDevIns, int rc));

    /**
     * Releases the PDM lock.
     *
     * @param   pDevIns         The PCI device instance.
     */
    DECLR3CALLBACKMEMBER(void,  pfnUnlock,(PPDMDEVINS pDevIns));

    /**
     * Gets a bus by it's PDM ordinal (typically the parent bus).
     *
     * @returns Pointer to the device instance of the bus.
     * @param   pDevIns         The PCI bus device instance.
     * @param   idxPdmBus       The PDM ordinal value of the bus to get.
     */
    DECLR3CALLBACKMEMBER(PPDMDEVINS, pfnGetBusByNo,(PPDMDEVINS pDevIns, uint32_t idxPdmBus));

    /** Just a safety precaution. */
    uint32_t                    u32TheEnd;
} PDMPCIHLPR3;
/** Pointer to PCI helpers. */
typedef R3PTRTYPE(PDMPCIHLPR3 *) PPDMPCIHLPR3;
/** Pointer to const PCI helpers. */
typedef R3PTRTYPE(const PDMPCIHLPR3 *) PCPDMPCIHLPR3;

/** Current PDMPCIHLPR3 version number. */
#define PDM_PCIHLPR3_VERSION                    PDM_VERSION_MAKE(0xfffb, 4, 0)


/**
 * Programmable Interrupt Controller registration structure (all contexts).
 */
typedef struct PDMPICREG
{
    /** Structure version number. PDM_PICREG_VERSION defines the current version. */
    uint32_t            u32Version;

    /**
     * Set the an IRQ.
     *
     * @param   pDevIns         Device instance of the PIC.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     * @remarks Caller enters the PDM critical section.
     */
    DECLCALLBACKMEMBER(void, pfnSetIrq)(PPDMDEVINS pDevIns, int iIrq, int iLevel, uint32_t uTagSrc);

    /**
     * Get a pending interrupt.
     *
     * @returns Pending interrupt number.
     * @param   pDevIns         Device instance of the PIC.
     * @param   puTagSrc        Where to return the IRQ tag and source.
     * @remarks Caller enters the PDM critical section.
     */
    DECLCALLBACKMEMBER(int, pfnGetInterrupt)(PPDMDEVINS pDevIns, uint32_t *puTagSrc);

    /** Just a safety precaution. */
    uint32_t                    u32TheEnd;
} PDMPICREG;
/** Pointer to a PIC registration structure. */
typedef PDMPICREG *PPDMPICREG;

/** Current PDMPICREG version number. */
#define PDM_PICREG_VERSION                      PDM_VERSION_MAKE(0xfffa, 3, 0)

/**
 * PIC helpers, same in all contexts.
 */
typedef struct PDMPICHLP
{
    /** Structure version. PDM_PICHLP_VERSION defines the current version. */
    uint32_t                u32Version;

    /**
     * Set the interrupt force action flag.
     *
     * @param   pDevIns         Device instance of the PIC.
     */
    DECLCALLBACKMEMBER(void, pfnSetInterruptFF)(PPDMDEVINS pDevIns);

    /**
     * Clear the interrupt force action flag.
     *
     * @param   pDevIns         Device instance of the PIC.
     */
    DECLCALLBACKMEMBER(void, pfnClearInterruptFF)(PPDMDEVINS pDevIns);

    /**
     * Acquires the PDM lock.
     *
     * @returns VINF_SUCCESS on success.
     * @returns rc if we failed to acquire the lock.
     * @param   pDevIns         The PIC device instance.
     * @param   rc              What to return if we fail to acquire the lock.
     */
    DECLCALLBACKMEMBER(int,   pfnLock)(PPDMDEVINS pDevIns, int rc);

    /**
     * Releases the PDM lock.
     *
     * @param   pDevIns         The PIC device instance.
     */
    DECLCALLBACKMEMBER(void,  pfnUnlock)(PPDMDEVINS pDevIns);

    /** Just a safety precaution. */
    uint32_t                u32TheEnd;
} PDMPICHLP;
/** Pointer to PIC helpers. */
typedef PDMPICHLP *PPDMPICHLP;
/** Pointer to const PIC helpers. */
typedef const PDMPICHLP *PCPDMPICHLP;

/** Current PDMPICHLP version number. */
#define PDM_PICHLP_VERSION                      PDM_VERSION_MAKE(0xfff9, 3, 0)


/**
 * Firmware registration structure.
 */
typedef struct PDMFWREG
{
    /** Struct version+magic number (PDM_FWREG_VERSION). */
    uint32_t                u32Version;

    /**
     * Checks whether this is a hard or soft reset.
     *
     * The current definition of soft reset is what the PC BIOS does when CMOS[0xF]
     * is 5, 9 or 0xA.
     *
     * @returns true if hard reset, false if soft.
     * @param   pDevIns         Device instance of the firmware.
     * @param   fFlags          PDMRESET_F_XXX passed to the PDMDevHlpVMReset API.
     */
    DECLR3CALLBACKMEMBER(bool, pfnIsHardReset,(PPDMDEVINS pDevIns, uint32_t fFlags));

    /** Just a safety precaution. */
    uint32_t                u32TheEnd;
} PDMFWREG;
/** Pointer to a FW registration structure. */
typedef PDMFWREG *PPDMFWREG;
/** Pointer to a const FW registration structure. */
typedef PDMFWREG const *PCPDMFWREG;

/** Current PDMFWREG version number. */
#define PDM_FWREG_VERSION                       PDM_VERSION_MAKE(0xffdd, 1, 0)

/**
 * Firmware R3 helpers.
 */
typedef struct PDMFWHLPR3
{
    /** Structure version. PDM_FWHLP_VERSION defines the current version. */
    uint32_t                u32Version;

    /** Just a safety precaution. */
    uint32_t                u32TheEnd;
} PDMFWHLPR3;

/** Pointer to FW R3 helpers. */
typedef R3PTRTYPE(PDMFWHLPR3 *) PPDMFWHLPR3;
/** Pointer to const FW R3 helpers. */
typedef R3PTRTYPE(const PDMFWHLPR3 *) PCPDMFWHLPR3;

/** Current PDMFWHLPR3 version number. */
#define PDM_FWHLPR3_VERSION                     PDM_VERSION_MAKE(0xffdb, 1, 0)


/**
 * APIC mode argument for apicR3SetCpuIdFeatureLevel.
 *
 * Also used in saved-states, CFGM don't change existing values.
 */
typedef enum PDMAPICMODE
{
    /** Invalid 0 entry. */
    PDMAPICMODE_INVALID = 0,
    /** No APIC. */
    PDMAPICMODE_NONE,
    /** Standard APIC (X86_CPUID_FEATURE_EDX_APIC). */
    PDMAPICMODE_APIC,
    /** Intel X2APIC (X86_CPUID_FEATURE_ECX_X2APIC). */
    PDMAPICMODE_X2APIC,
    /** The usual 32-bit paranoia. */
    PDMAPICMODE_32BIT_HACK = 0x7fffffff
} PDMAPICMODE;

/**
 * APIC irq argument for pfnSetInterruptFF and pfnClearInterruptFF.
 */
typedef enum PDMAPICIRQ
{
    /** Invalid 0 entry. */
    PDMAPICIRQ_INVALID = 0,
    /** Normal hardware interrupt. */
    PDMAPICIRQ_HARDWARE,
    /** NMI. */
    PDMAPICIRQ_NMI,
    /** SMI. */
    PDMAPICIRQ_SMI,
    /** ExtINT (HW interrupt via PIC). */
    PDMAPICIRQ_EXTINT,
    /** Interrupt arrived, needs to be updated to the IRR. */
    PDMAPICIRQ_UPDATE_PENDING,
    /** The usual 32-bit paranoia. */
    PDMAPICIRQ_32BIT_HACK = 0x7fffffff
} PDMAPICIRQ;


/**
 * I/O APIC registration structure (all contexts).
 */
typedef struct PDMIOAPICREG
{
    /** Struct version+magic number (PDM_IOAPICREG_VERSION). */
    uint32_t            u32Version;

    /**
     * Set an IRQ.
     *
     * @param   pDevIns         Device instance of the I/O APIC.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     *
     * @remarks Caller enters the PDM critical section
     *          Actually, as per 2018-07-21 this isn't true (bird).
     */
    DECLCALLBACKMEMBER(void, pfnSetIrq)(PPDMDEVINS pDevIns, int iIrq, int iLevel, uint32_t uTagSrc);

    /**
     * Send a MSI.
     *
     * @param   pDevIns         Device instance of the I/O APIC.
     * @param   GCPhys          Request address.
     * @param   uValue          Request value.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     *
     * @remarks Caller enters the PDM critical section
     *          Actually, as per 2018-07-21 this isn't true (bird).
     */
    DECLCALLBACKMEMBER(void, pfnSendMsi)(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t uValue, uint32_t uTagSrc);

    /**
     * Set the EOI for an interrupt vector.
     *
     * @returns Strict VBox status code - only the following informational status codes:
     * @retval  VINF_IOM_R3_MMIO_WRITE if the I/O APIC lock is contenteded and we're in R0 or RC.
     * @retval  VINF_SUCCESS
     *
     * @param   pDevIns         Device instance of the I/O APIC.
     * @param   u8Vector        The vector.
     *
     * @remarks Caller enters the PDM critical section
     *          Actually, as per 2018-07-21 this isn't true (bird).
     */
    DECLCALLBACKMEMBER(VBOXSTRICTRC, pfnSetEoi)(PPDMDEVINS pDevIns, uint8_t u8Vector);

    /** Just a safety precaution. */
    uint32_t                u32TheEnd;
} PDMIOAPICREG;
/** Pointer to an APIC registration structure. */
typedef PDMIOAPICREG *PPDMIOAPICREG;

/** Current PDMAPICREG version number. */
#define PDM_IOAPICREG_VERSION                   PDM_VERSION_MAKE(0xfff2, 6, 0)


/**
 * IOAPIC helpers, same in all contexts.
 */
typedef struct PDMIOAPICHLP
{
    /** Structure version. PDM_IOAPICHLP_VERSION defines the current version. */
    uint32_t                u32Version;

    /**
     * Private interface between the IOAPIC and APIC.
     *
     * @returns status code.
     * @param   pDevIns         Device instance of the IOAPIC.
     * @param   u8Dest          See APIC implementation.
     * @param   u8DestMode      See APIC implementation.
     * @param   u8DeliveryMode  See APIC implementation.
     * @param   uVector         See APIC implementation.
     * @param   u8Polarity      See APIC implementation.
     * @param   u8TriggerMode   See APIC implementation.
     * @param   uTagSrc         The IRQ tag and source (for tracing).
     *
     * @sa      APICBusDeliver()
     */
    DECLCALLBACKMEMBER(int, pfnApicBusDeliver)(PPDMDEVINS pDevIns, uint8_t u8Dest, uint8_t u8DestMode, uint8_t u8DeliveryMode,
                                               uint8_t uVector, uint8_t u8Polarity, uint8_t u8TriggerMode, uint32_t uTagSrc);

    /**
     * Acquires the PDM lock.
     *
     * @returns VINF_SUCCESS on success.
     * @returns rc if we failed to acquire the lock.
     * @param   pDevIns         The IOAPIC device instance.
     * @param   rc              What to return if we fail to acquire the lock.
     */
    DECLCALLBACKMEMBER(int,   pfnLock)(PPDMDEVINS pDevIns, int rc);

    /**
     * Releases the PDM lock.
     *
     * @param   pDevIns         The IOAPIC device instance.
     */
    DECLCALLBACKMEMBER(void,  pfnUnlock)(PPDMDEVINS pDevIns);

    /** Just a safety precaution. */
    uint32_t                u32TheEnd;
} PDMIOAPICHLP;
/** Pointer to IOAPIC helpers. */
typedef PDMIOAPICHLP * PPDMIOAPICHLP;
/** Pointer to const IOAPIC helpers. */
typedef const PDMIOAPICHLP * PCPDMIOAPICHLP;

/** Current PDMIOAPICHLP version number. */
#define PDM_IOAPICHLP_VERSION                   PDM_VERSION_MAKE(0xfff0, 2, 0)


/**
 * HPET registration structure.
 */
typedef struct PDMHPETREG
{
    /** Struct version+magic number (PDM_HPETREG_VERSION). */
    uint32_t            u32Version;
} PDMHPETREG;
/** Pointer to an HPET registration structure. */
typedef PDMHPETREG *PPDMHPETREG;

/** Current PDMHPETREG version number. */
#define PDM_HPETREG_VERSION                     PDM_VERSION_MAKE(0xffe2, 1, 0)

/**
 * HPET RC helpers.
 *
 * @remarks Keep this around in case HPET will need PDM interaction in again RC
 *          at some later point.
 */
typedef struct PDMHPETHLPRC
{
    /** Structure version. PDM_HPETHLPRC_VERSION defines the current version. */
    uint32_t                u32Version;

    /** Just a safety precaution. */
    uint32_t                u32TheEnd;
} PDMHPETHLPRC;

/** Pointer to HPET RC helpers. */
typedef RCPTRTYPE(PDMHPETHLPRC *) PPDMHPETHLPRC;
/** Pointer to const HPET RC helpers. */
typedef RCPTRTYPE(const PDMHPETHLPRC *) PCPDMHPETHLPRC;

/** Current PDMHPETHLPRC version number. */
#define PDM_HPETHLPRC_VERSION                   PDM_VERSION_MAKE(0xffee, 2, 0)


/**
 * HPET R0 helpers.
 *
 * @remarks Keep this around in case HPET will need PDM interaction in again R0
 *          at some later point.
 */
typedef struct PDMHPETHLPR0
{
    /** Structure version. PDM_HPETHLPR0_VERSION defines the current version. */
    uint32_t                u32Version;

    /** Just a safety precaution. */
    uint32_t                u32TheEnd;
} PDMHPETHLPR0;

/** Pointer to HPET R0 helpers. */
typedef R0PTRTYPE(PDMHPETHLPR0 *) PPDMHPETHLPR0;
/** Pointer to const HPET R0 helpers. */
typedef R0PTRTYPE(const PDMHPETHLPR0 *) PCPDMHPETHLPR0;

/** Current PDMHPETHLPR0 version number. */
#define PDM_HPETHLPR0_VERSION                   PDM_VERSION_MAKE(0xffed, 2, 0)

/**
 * HPET R3 helpers.
 */
typedef struct PDMHPETHLPR3
{
    /** Structure version. PDM_HPETHLP_VERSION defines the current version. */
    uint32_t                u32Version;

    /**
     * Set legacy mode on PIT and RTC.
     *
     * @returns VINF_SUCCESS on success.
     * @returns rc if we failed to set legacy mode.
     * @param   pDevIns         Device instance of the HPET.
     * @param   fActivated      Whether legacy mode is activated or deactivated.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetLegacyMode,(PPDMDEVINS pDevIns, bool fActivated));


    /**
     * Set IRQ, bypassing ISA bus override rules.
     *
     * @returns VINF_SUCCESS on success.
     * @returns rc if we failed to set legacy mode.
     * @param   pDevIns         Device instance of the HPET.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetIrq,(PPDMDEVINS pDevIns, int iIrq, int iLevel));

    /** Just a safety precaution. */
    uint32_t                u32TheEnd;
} PDMHPETHLPR3;

/** Pointer to HPET R3 helpers. */
typedef R3PTRTYPE(PDMHPETHLPR3 *) PPDMHPETHLPR3;
/** Pointer to const HPET R3 helpers. */
typedef R3PTRTYPE(const PDMHPETHLPR3 *) PCPDMHPETHLPR3;

/** Current PDMHPETHLPR3 version number. */
#define PDM_HPETHLPR3_VERSION                   PDM_VERSION_MAKE(0xffec, 3, 0)


/**
 * Raw PCI device registration structure.
 */
typedef struct PDMPCIRAWREG
{
    /** Struct version+magic number (PDM_PCIRAWREG_VERSION). */
    uint32_t                u32Version;
    /** Just a safety precaution. */
    uint32_t                u32TheEnd;
} PDMPCIRAWREG;
/** Pointer to a raw PCI registration structure. */
typedef PDMPCIRAWREG *PPDMPCIRAWREG;

/** Current PDMPCIRAWREG version number. */
#define PDM_PCIRAWREG_VERSION                   PDM_VERSION_MAKE(0xffe1, 1, 0)

/**
 * Raw PCI device raw-mode context helpers.
 */
typedef struct PDMPCIRAWHLPRC
{
    /** Structure version and magic number (PDM_PCIRAWHLPRC_VERSION). */
    uint32_t u32Version;
    /** Just a safety precaution. */
    uint32_t u32TheEnd;
} PDMPCIRAWHLPRC;
/** Pointer to a raw PCI deviec raw-mode context helper structure. */
typedef RCPTRTYPE(PDMPCIRAWHLPRC *) PPDMPCIRAWHLPRC;
/** Pointer to a const raw PCI deviec raw-mode context helper structure. */
typedef RCPTRTYPE(const PDMPCIRAWHLPRC *) PCPDMPCIRAWHLPRC;

/** Current PDMPCIRAWHLPRC version number. */
#define PDM_PCIRAWHLPRC_VERSION                 PDM_VERSION_MAKE(0xffe0, 1, 0)

/**
 * Raw PCI device ring-0 context helpers.
 */
typedef struct PDMPCIRAWHLPR0
{
    /** Structure version and magic number (PDM_PCIRAWHLPR0_VERSION). */
    uint32_t u32Version;
    /** Just a safety precaution. */
    uint32_t u32TheEnd;
} PDMPCIRAWHLPR0;
/** Pointer to a raw PCI deviec ring-0 context helper structure. */
typedef R0PTRTYPE(PDMPCIRAWHLPR0 *) PPDMPCIRAWHLPR0;
/** Pointer to a const raw PCI deviec ring-0 context helper structure. */
typedef R0PTRTYPE(const PDMPCIRAWHLPR0 *) PCPDMPCIRAWHLPR0;

/** Current PDMPCIRAWHLPR0 version number. */
#define PDM_PCIRAWHLPR0_VERSION                 PDM_VERSION_MAKE(0xffdf, 1, 0)


/**
 * Raw PCI device ring-3 context helpers.
 */
typedef struct PDMPCIRAWHLPR3
{
    /** Undefined structure version and magic number. */
    uint32_t u32Version;

    /**
     * Gets the address of the RC raw PCI device helpers.
     *
     * This should be called at both construction and relocation time to obtain
     * the correct address of the RC helpers.
     *
     * @returns RC pointer to the raw PCI device helpers.
     * @param   pDevIns         Device instance of the raw PCI device.
     */
    DECLR3CALLBACKMEMBER(PCPDMPCIRAWHLPRC, pfnGetRCHelpers,(PPDMDEVINS pDevIns));

    /**
     * Gets the address of the R0 raw PCI device helpers.
     *
     * This should be called at both construction and relocation time to obtain
     * the correct address of the R0 helpers.
     *
     * @returns R0 pointer to the raw PCI device helpers.
     * @param   pDevIns         Device instance of the raw PCI device.
     */
    DECLR3CALLBACKMEMBER(PCPDMPCIRAWHLPR0, pfnGetR0Helpers,(PPDMDEVINS pDevIns));

    /** Just a safety precaution. */
    uint32_t                u32TheEnd;
} PDMPCIRAWHLPR3;
/** Pointer to raw PCI R3 helpers. */
typedef R3PTRTYPE(PDMPCIRAWHLPR3 *) PPDMPCIRAWHLPR3;
/** Pointer to const raw PCI R3 helpers. */
typedef R3PTRTYPE(const PDMPCIRAWHLPR3 *) PCPDMPCIRAWHLPR3;

/** Current PDMPCIRAWHLPR3 version number. */
#define PDM_PCIRAWHLPR3_VERSION                   PDM_VERSION_MAKE(0xffde, 1, 0)


#ifdef IN_RING3

/**
 * DMA Transfer Handler.
 *
 * @returns Number of bytes transferred.
 * @param   pDevIns     The device instance that registered the handler.
 * @param   pvUser      User pointer.
 * @param   uChannel    Channel number.
 * @param   off         DMA position.
 * @param   cb          Block size.
 * @remarks The device lock is take before the callback (in fact, the locks of
 *          DMA devices and the DMA controller itself are taken).
 */
typedef DECLCALLBACK(uint32_t) FNDMATRANSFERHANDLER(PPDMDEVINS pDevIns, void *pvUser, unsigned uChannel, uint32_t off, uint32_t cb);
/** Pointer to a FNDMATRANSFERHANDLER(). */
typedef FNDMATRANSFERHANDLER *PFNDMATRANSFERHANDLER;

/**
 * DMA Controller registration structure.
 */
typedef struct PDMDMAREG
{
    /** Structure version number. PDM_DMACREG_VERSION defines the current version. */
    uint32_t            u32Version;

    /**
     * Execute pending transfers.
     *
     * @returns A more work indiciator. I.e. 'true' if there is more to be done, and 'false' if all is done.
     * @param pDevIns           Device instance of the DMAC.
     * @remarks No locks held, called on EMT(0) as a form of serialization.
     */
    DECLR3CALLBACKMEMBER(bool, pfnRun,(PPDMDEVINS pDevIns));

    /**
     * Register transfer function for DMA channel.
     *
     * @param pDevIns               Device instance of the DMAC.
     * @param uChannel              Channel number.
     * @param pDevInsHandler        The device instance of the device making the
     *                              regstration (will be passed to the callback).
     * @param pfnTransferHandler    Device specific transfer function.
     * @param pvUser                User pointer to be passed to the callback.
     * @remarks No locks held, called on an EMT.
     */
    DECLR3CALLBACKMEMBER(void, pfnRegister,(PPDMDEVINS pDevIns, unsigned uChannel, PPDMDEVINS pDevInsHandler,
                                            PFNDMATRANSFERHANDLER pfnTransferHandler, void *pvUser));

    /**
     * Read memory
     *
     * @returns Number of bytes read.
     * @param pDevIns           Device instance of the DMAC.
     * @param uChannel          Channel number.
     * @param pvBuffer          Pointer to target buffer.
     * @param off               DMA position.
     * @param cbBlock           Block size.
     * @remarks No locks held, called on an EMT.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnReadMemory,(PPDMDEVINS pDevIns, unsigned uChannel, void *pvBuffer, uint32_t off, uint32_t cbBlock));

    /**
     * Write memory
     *
     * @returns Number of bytes written.
     * @param pDevIns           Device instance of the DMAC.
     * @param uChannel          Channel number.
     * @param pvBuffer          Memory to write.
     * @param off               DMA position.
     * @param cbBlock           Block size.
     * @remarks No locks held, called on an EMT.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnWriteMemory,(PPDMDEVINS pDevIns, unsigned uChannel, const void *pvBuffer, uint32_t off, uint32_t cbBlock));

    /**
     * Set the DREQ line.
     *
     * @param pDevIns           Device instance of the DMAC.
     * @param uChannel          Channel number.
     * @param uLevel            Level of the line.
     * @remarks No locks held, called on an EMT.
     */
    DECLR3CALLBACKMEMBER(void, pfnSetDREQ,(PPDMDEVINS pDevIns, unsigned uChannel, unsigned uLevel));

    /**
     * Get channel mode
     *
     * @returns                 Channel mode.
     * @param pDevIns           Device instance of the DMAC.
     * @param uChannel          Channel number.
     * @remarks No locks held, called on an EMT.
     */
    DECLR3CALLBACKMEMBER(uint8_t, pfnGetChannelMode,(PPDMDEVINS pDevIns, unsigned uChannel));

} PDMDMACREG;
/** Pointer to a DMAC registration structure. */
typedef PDMDMACREG *PPDMDMACREG;

/** Current PDMDMACREG version number. */
#define PDM_DMACREG_VERSION                     PDM_VERSION_MAKE(0xffeb, 2, 0)


/**
 * DMA Controller device helpers.
 */
typedef struct PDMDMACHLP
{
    /** Structure version. PDM_DMACHLP_VERSION defines the current version. */
    uint32_t                u32Version;

    /* to-be-defined */

} PDMDMACHLP;
/** Pointer to DMAC helpers. */
typedef PDMDMACHLP *PPDMDMACHLP;
/** Pointer to const DMAC helpers. */
typedef const PDMDMACHLP *PCPDMDMACHLP;

/** Current PDMDMACHLP version number. */
#define PDM_DMACHLP_VERSION                     PDM_VERSION_MAKE(0xffea, 1, 0)

#endif /* IN_RING3 */



/**
 * RTC registration structure.
 */
typedef struct PDMRTCREG
{
    /** Structure version number. PDM_RTCREG_VERSION defines the current version. */
    uint32_t            u32Version;
    uint32_t            u32Alignment;   /**< structure size alignment. */

    /**
     * Write to a CMOS register and update the checksum if necessary.
     *
     * @returns VBox status code.
     * @param   pDevIns     Device instance of the RTC.
     * @param   iReg        The CMOS register index.
     * @param   u8Value     The CMOS register value.
     * @remarks Caller enters the device critical section.
     */
    DECLR3CALLBACKMEMBER(int, pfnWrite,(PPDMDEVINS pDevIns, unsigned iReg, uint8_t u8Value));

    /**
     * Read a CMOS register.
     *
     * @returns VBox status code.
     * @param   pDevIns     Device instance of the RTC.
     * @param   iReg        The CMOS register index.
     * @param   pu8Value    Where to store the CMOS register value.
     * @remarks Caller enters the device critical section.
     */
    DECLR3CALLBACKMEMBER(int, pfnRead,(PPDMDEVINS pDevIns, unsigned iReg, uint8_t *pu8Value));

} PDMRTCREG;
/** Pointer to a RTC registration structure. */
typedef PDMRTCREG *PPDMRTCREG;
/** Pointer to a const RTC registration structure. */
typedef const PDMRTCREG *PCPDMRTCREG;

/** Current PDMRTCREG version number. */
#define PDM_RTCREG_VERSION                      PDM_VERSION_MAKE(0xffe9, 2, 0)


/**
 * RTC device helpers.
 */
typedef struct PDMRTCHLP
{
    /** Structure version. PDM_RTCHLP_VERSION defines the current version. */
    uint32_t                u32Version;

    /* to-be-defined */

} PDMRTCHLP;
/** Pointer to RTC helpers. */
typedef PDMRTCHLP *PPDMRTCHLP;
/** Pointer to const RTC helpers. */
typedef const PDMRTCHLP *PCPDMRTCHLP;

/** Current PDMRTCHLP version number. */
#define PDM_RTCHLP_VERSION                      PDM_VERSION_MAKE(0xffe8, 1, 0)



/** @name Flags for PCI I/O region registration
 * @{ */
/** No handle is passed. */
#define PDMPCIDEV_IORGN_F_NO_HANDLE         UINT32_C(0x00000000)
/** An I/O port handle is passed. */
#define PDMPCIDEV_IORGN_F_IOPORT_HANDLE     UINT32_C(0x00000001)
/** An MMIO range handle is passed. */
#define PDMPCIDEV_IORGN_F_MMIO_HANDLE       UINT32_C(0x00000002)
/** An MMIO2 handle is passed. */
#define PDMPCIDEV_IORGN_F_MMIO2_HANDLE      UINT32_C(0x00000003)
/** Handle type mask.  */
#define PDMPCIDEV_IORGN_F_HANDLE_MASK       UINT32_C(0x00000003)
/** New-style (mostly wrt callbacks).  */
#define PDMPCIDEV_IORGN_F_NEW_STYLE         UINT32_C(0x00000004)
/** Mask of valid flags.   */
#define PDMPCIDEV_IORGN_F_VALID_MASK        UINT32_C(0x00000007)
/** @} */


#ifdef IN_RING3

/** @name Special values for PDMDEVHLPR3::pfnPCIRegister parameters.
 * @{ */
/** Same device number (and bus) as the previous PCI device registered with the PDM device.
 * This is handy when registering multiple PCI device functions and the device
 * number is left up to the PCI bus.  In order to facilitate one PDM device
 * instance for each PCI function, this searches earlier PDM device
 * instances as well. */
# define PDMPCIDEVREG_DEV_NO_SAME_AS_PREV   UINT8_C(0xfd)
/** Use the first unused device number (all functions must be unused). */
# define PDMPCIDEVREG_DEV_NO_FIRST_UNUSED   UINT8_C(0xfe)
/** Use the first unused device function. */
# define PDMPCIDEVREG_FUN_NO_FIRST_UNUSED   UINT8_C(0xff)

/** The device and function numbers are not mandatory, just suggestions. */
# define PDMPCIDEVREG_F_NOT_MANDATORY_NO    RT_BIT_32(0)
/** Registering a PCI bridge device. */
# define PDMPCIDEVREG_F_PCI_BRIDGE          RT_BIT_32(1)
/** Valid flag mask. */
# define PDMPCIDEVREG_F_VALID_MASK          UINT32_C(0x00000003)
/** @} */

/** Current PDMDEVHLPR3 version number. */
#define PDM_DEVHLPR3_VERSION                    PDM_VERSION_MAKE_PP(0xffe7, 41, 0)

/**
 * PDM Device API.
 */
typedef struct PDMDEVHLPR3
{
    /** Structure version. PDM_DEVHLPR3_VERSION defines the current version. */
    uint32_t                        u32Version;

    /** @name I/O ports
     * @{ */
    /**
     * Creates a range of I/O ports for a device.
     *
     * The I/O port range must be mapped in a separately call.  Any ring-0 and
     * raw-mode context callback handlers needs to be set up in the respective
     * contexts.
     *
     * @returns VBox status.
     * @param   pDevIns     The device instance to register the ports with.
     * @param   cPorts      Number of ports to register.
     * @param   fFlags      IOM_IOPORT_F_XXX.
     * @param   pPciDev     The PCI device the range is associated with, if
     *                      applicable.
     * @param   iPciRegion  The PCI device region in the high 16-bit word and
     *                      sub-region in the low 16-bit word.  UINT32_MAX if NA.
     * @param   pfnOut      Pointer to function which is gonna handle OUT
     *                      operations. Optional.
     * @param   pfnIn       Pointer to function which is gonna handle IN operations.
     *                      Optional.
     * @param   pfnOutStr   Pointer to function which is gonna handle string OUT
     *                      operations.  Optional.
     * @param   pfnInStr    Pointer to function which is gonna handle string IN
     *                      operations.  Optional.
     * @param   pvUser      User argument to pass to the callbacks.
     * @param   pszDesc     Pointer to description string. This must not be freed.
     * @param   paExtDescs  Extended per-port descriptions, optional.  Partial range
     *                      coverage is allowed.  This must not be freed.
     * @param   phIoPorts   Where to return the I/O port range handle.
     *
     * @remarks Caller enters the device critical section prior to invoking the
     *          registered callback methods.
     *
     * @sa      PDMDevHlpIoPortSetUpContext, PDMDevHlpIoPortMap,
     *          PDMDevHlpIoPortUnmap.
     */
    DECLR3CALLBACKMEMBER(int, pfnIoPortCreateEx,(PPDMDEVINS pDevIns, RTIOPORT cPorts, uint32_t fFlags, PPDMPCIDEV pPciDev,
                                                 uint32_t iPciRegion, PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn,
                                                 PFNIOMIOPORTNEWOUTSTRING pfnOutStr, PFNIOMIOPORTNEWINSTRING pfnInStr, RTR3PTR pvUser,
                                                 const char *pszDesc, PCIOMIOPORTDESC paExtDescs, PIOMIOPORTHANDLE phIoPorts));

    /**
     * Maps an I/O port range.
     *
     * @returns VBox status.
     * @param   pDevIns     The device instance to register the ports with.
     * @param   hIoPorts    The I/O port range handle.
     * @param   Port        Where to map the range.
     * @sa      PDMDevHlpIoPortUnmap, PDMDevHlpIoPortSetUpContext,
     *          PDMDevHlpIoPortCreate, PDMDevHlpIoPortCreateEx.
     */
    DECLR3CALLBACKMEMBER(int, pfnIoPortMap,(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts, RTIOPORT Port));

    /**
     * Unmaps an I/O port range.
     *
     * @returns VBox status.
     * @param   pDevIns     The device instance to register the ports with.
     * @param   hIoPorts    The I/O port range handle.
     * @sa      PDMDevHlpIoPortMap, PDMDevHlpIoPortSetUpContext,
     *          PDMDevHlpIoPortCreate, PDMDevHlpIoPortCreateEx.
     */
    DECLR3CALLBACKMEMBER(int, pfnIoPortUnmap,(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts));

    /**
     * Gets the mapping address of the I/O port range @a hIoPorts.
     *
     * @returns Mapping address (0..65535) or UINT32_MAX if not mapped (or invalid
     *          parameters).
     * @param   pDevIns     The device instance to register the ports with.
     * @param   hIoPorts    The I/O port range handle.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnIoPortGetMappingAddress,(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts));
    /** @}  */

    /** @name MMIO
     * @{ */
    /**
     * Creates a memory mapped I/O (MMIO) region for a device.
     *
     * The MMIO region must be mapped in a separately call.  Any ring-0 and
     * raw-mode context callback handlers needs to be set up in the respective
     * contexts.
     *
     * @returns VBox status.
     * @param   pDevIns     The device instance to register the ports with.
     * @param   cbRegion    The size of the region in bytes.
     * @param   fFlags      Flags, IOMMMIO_FLAGS_XXX.
     * @param   pPciDev     The PCI device the range is associated with, if
     *                      applicable.
     * @param   iPciRegion  The PCI device region in the high 16-bit word and
     *                      sub-region in the low 16-bit word.  UINT32_MAX if NA.
     * @param   pfnWrite    Pointer to function which is gonna handle Write
     *                      operations.
     * @param   pfnRead     Pointer to function which is gonna handle Read
     *                      operations.
     * @param   pfnFill     Pointer to function which is gonna handle Fill/memset
     *                      operations. (optional)
     * @param   pvUser      User argument to pass to the callbacks.
     * @param   pszDesc     Pointer to description string. This must not be freed.
     * @param   phRegion    Where to return the MMIO region handle.
     *
     * @remarks Caller enters the device critical section prior to invoking the
     *          registered callback methods.
     *
     * @sa      PDMDevHlpMmioSetUpContext, PDMDevHlpMmioMap, PDMDevHlpMmioUnmap.
     */
    DECLR3CALLBACKMEMBER(int, pfnMmioCreateEx,(PPDMDEVINS pDevIns, RTGCPHYS cbRegion,
                                               uint32_t fFlags, PPDMPCIDEV pPciDev, uint32_t iPciRegion,
                                               PFNIOMMMIONEWWRITE pfnWrite, PFNIOMMMIONEWREAD pfnRead, PFNIOMMMIONEWFILL pfnFill,
                                               void *pvUser, const char *pszDesc, PIOMMMIOHANDLE phRegion));

    /**
     * Maps a memory mapped I/O (MMIO) region (into the guest physical address space).
     *
     * @returns VBox status.
     * @param   pDevIns     The device instance the region is associated with.
     * @param   hRegion     The MMIO region handle.
     * @param   GCPhys       Where to map the region.
     * @note    An MMIO range may overlap with base memory if a lot of RAM is
     *          configured for the VM, in  which case we'll drop the base memory
     *          pages.  Presently we will make no attempt to preserve anything that
     *          happens to be present in the base memory that is replaced, this is
     *          technically incorrect but it's just not worth the effort to do
     *          right, at least not at this point.
     * @sa      PDMDevHlpMmioUnmap, PDMDevHlpMmioCreate, PDMDevHlpMmioCreateEx,
     *          PDMDevHlpMmioSetUpContext
     */
    DECLR3CALLBACKMEMBER(int, pfnMmioMap,(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS GCPhys));

    /**
     * Unmaps a memory mapped I/O (MMIO) region.
     *
     * @returns VBox status.
     * @param   pDevIns     The device instance the region is associated with.
     * @param   hRegion     The MMIO region handle.
     * @sa      PDMDevHlpMmioMap, PDMDevHlpMmioCreate, PDMDevHlpMmioCreateEx,
     *          PDMDevHlpMmioSetUpContext
     */
    DECLR3CALLBACKMEMBER(int, pfnMmioUnmap,(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion));

    /**
     * Reduces the length of a MMIO range.
     *
     * This is for implementations of PDMPCIDEV::pfnRegionLoadChangeHookR3 and will
     * only work during saved state restore.  It will not call the PCI bus code, as
     * that is expected to restore the saved resource configuration.
     *
     * It just adjusts the mapping length of the region so that when pfnMmioMap is
     * called it will only map @a cbRegion bytes and not the value set during
     * registration.
     *
     * @return VBox status code.
     * @param   pDevIns     The device owning the range.
     * @param   hRegion     The MMIO region handle.
     * @param   cbRegion    The new size, must be smaller.
     */
    DECLR3CALLBACKMEMBER(int, pfnMmioReduce,(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS cbRegion));

    /**
     * Gets the mapping address of the MMIO region @a hRegion.
     *
     * @returns Mapping address, NIL_RTGCPHYS if not mapped (or invalid parameters).
     * @param   pDevIns     The device instance to register the ports with.
     * @param   hRegion     The MMIO region handle.
     */
    DECLR3CALLBACKMEMBER(RTGCPHYS, pfnMmioGetMappingAddress,(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion));
    /** @} */

    /** @name MMIO2
     * @{ */
    /**
     * Creates a MMIO2 region.
     *
     * As mentioned elsewhere, MMIO2 is just RAM spelled differently.  It's RAM
     * associated with a device.  It is also non-shared memory with a permanent
     * ring-3 mapping and page backing (presently).
     *
     * @returns VBox status.
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device the region is associated with, or
     *                              NULL if no PCI device association.
     * @param   iPciRegion          The region number. Use the PCI region number as
     *                              this must be known to the PCI bus device too. If
     *                              it's not associated with the PCI device, then
     *                              any number up to UINT8_MAX is fine.
     * @param   cbRegion            The size (in bytes) of the region.
     * @param   fFlags              Reserved for future use, must be zero.
     * @param   pszDesc             Pointer to description string. This must not be
     *                              freed.
     * @param   ppvMapping          Where to store the address of the ring-3 mapping
     *                              of the memory.
     * @param   phRegion            Where to return the MMIO2 region handle.
     *
     * @thread  EMT(0)
     */
    DECLR3CALLBACKMEMBER(int, pfnMmio2Create,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iPciRegion, RTGCPHYS cbRegion,
                                              uint32_t fFlags, const char *pszDesc, void **ppvMapping, PPGMMMIO2HANDLE phRegion));

    /**
     * Destroys a MMIO2 region, unmapping it and freeing the memory.
     *
     * Any physical access handlers registered for the region must be deregistered
     * before calling this function.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   hRegion             The MMIO2 region handle.
     * @thread  EMT.
     */
    DECLR3CALLBACKMEMBER(int, pfnMmio2Destroy,(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion));

    /**
     * Maps a MMIO2 region (into the guest physical address space).
     *
     * @returns VBox status.
     * @param   pDevIns     The device instance the region is associated with.
     * @param   hRegion     The MMIO2 region handle.
     * @param   GCPhys       Where to map the region.
     * @note    A MMIO2 region overlap with base memory if a lot of RAM is
     *          configured for the VM, in  which case we'll drop the base memory
     *          pages.  Presently we will make no attempt to preserve anything that
     *          happens to be present in the base memory that is replaced, this is
     *          technically incorrect but it's just not worth the effort to do
     *          right, at least not at this point.
     * @sa      PDMDevHlpMmio2Unmap, PDMDevHlpMmio2Create, PDMDevHlpMmio2SetUpContext
     */
    DECLR3CALLBACKMEMBER(int, pfnMmio2Map,(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion, RTGCPHYS GCPhys));

    /**
     * Unmaps a MMIO2 region.
     *
     * @returns VBox status.
     * @param   pDevIns     The device instance the region is associated with.
     * @param   hRegion     The MMIO2 region handle.
     * @sa      PDMDevHlpMmio2Map, PDMDevHlpMmio2Create, PDMDevHlpMmio2SetUpContext
     */
    DECLR3CALLBACKMEMBER(int, pfnMmio2Unmap,(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion));

    /**
     * Reduces the length of a MMIO range.
     *
     * This is for implementations of PDMPCIDEV::pfnRegionLoadChangeHookR3 and will
     * only work during saved state restore.  It will not call the PCI bus code, as
     * that is expected to restore the saved resource configuration.
     *
     * It just adjusts the mapping length of the region so that when pfnMmioMap is
     * called it will only map @a cbRegion bytes and not the value set during
     * registration.
     *
     * @return VBox status code.
     * @param   pDevIns     The device owning the range.
     * @param   hRegion     The MMIO2 region handle.
     * @param   cbRegion    The new size, must be smaller.
     */
    DECLR3CALLBACKMEMBER(int, pfnMmio2Reduce,(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion, RTGCPHYS cbRegion));

    /**
     * Gets the mapping address of the MMIO region @a hRegion.
     *
     * @returns Mapping address, NIL_RTGCPHYS if not mapped (or invalid parameters).
     * @param   pDevIns     The device instance to register the ports with.
     * @param   hRegion     The MMIO2 region handle.
     */
    DECLR3CALLBACKMEMBER(RTGCPHYS, pfnMmio2GetMappingAddress,(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion));

    /**
     * Changes the number of an MMIO2 or pre-registered MMIO region.
     *
     * This should only be used to deal with saved state problems, so there is no
     * convenience inline wrapper for this method.
     *
     * @returns VBox status code.
     * @param   pDevIns     The device instance.
     * @param   hRegion     The MMIO2 region handle.
     * @param   iNewRegion  The new region index.
     *
     * @sa      @bugref{9359}
     */
    DECLR3CALLBACKMEMBER(int, pfnMmio2ChangeRegionNo,(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion, uint32_t iNewRegion));
    /** @} */

    /**
     * Register a ROM (BIOS) region.
     *
     * It goes without saying that this is read-only memory. The memory region must be
     * in unassigned memory. I.e. from the top of the address space or on the PC in
     * the 0xa0000-0xfffff range.
     *
     * @returns VBox status.
     * @param   pDevIns             The device instance owning the ROM region.
     * @param   GCPhysStart         First physical address in the range.
     *                              Must be page aligned!
     * @param   cbRange             The size of the range (in bytes).
     *                              Must be page aligned!
     * @param   pvBinary            Pointer to the binary data backing the ROM image.
     * @param   cbBinary            The size of the binary pointer.  This must
     *                              be equal or smaller than @a cbRange.
     * @param   fFlags              Shadow ROM flags, PGMPHYS_ROM_FLAGS_* in pgm.h.
     * @param   pszDesc             Pointer to description string. This must not be freed.
     *
     * @remark  There is no way to remove the rom, automatically on device cleanup or
     *          manually from the device yet. At present I doubt we need such features...
     */
    DECLR3CALLBACKMEMBER(int, pfnROMRegister,(PPDMDEVINS pDevIns, RTGCPHYS GCPhysStart, uint32_t cbRange,
                                              const void *pvBinary, uint32_t cbBinary, uint32_t fFlags, const char *pszDesc));

    /**
     * Changes the protection of shadowed ROM mapping.
     *
     * This is intented for use by the system BIOS, chipset or device in question to
     * change the protection of shadowed ROM code after init and on reset.
     *
     * @param   pDevIns             The device instance.
     * @param   GCPhysStart         Where the mapping starts.
     * @param   cbRange             The size of the mapping.
     * @param   enmProt             The new protection type.
     */
    DECLR3CALLBACKMEMBER(int, pfnROMProtectShadow,(PPDMDEVINS pDevIns, RTGCPHYS GCPhysStart, uint32_t cbRange, PGMROMPROT enmProt));

    /**
     * Register a save state data unit.
     *
     * @returns VBox status.
     * @param   pDevIns             The device instance.
     * @param   uVersion            Data layout version number.
     * @param   cbGuess             The approximate amount of data in the unit.
     *                              Only for progress indicators.
     * @param   pszBefore           Name of data unit which we should be put in
     *                              front of. Optional (NULL).
     *
     * @param   pfnLivePrep         Prepare live save callback, optional.
     * @param   pfnLiveExec         Execute live save callback, optional.
     * @param   pfnLiveVote         Vote live save callback, optional.
     *
     * @param   pfnSavePrep         Prepare save callback, optional.
     * @param   pfnSaveExec         Execute save callback, optional.
     * @param   pfnSaveDone         Done save callback, optional.
     *
     * @param   pfnLoadPrep         Prepare load callback, optional.
     * @param   pfnLoadExec         Execute load callback, optional.
     * @param   pfnLoadDone         Done load callback, optional.
     * @remarks Caller enters the device critical section prior to invoking the
     *          registered callback methods.
     */
    DECLR3CALLBACKMEMBER(int, pfnSSMRegister,(PPDMDEVINS pDevIns, uint32_t uVersion, size_t cbGuess, const char *pszBefore,
                                              PFNSSMDEVLIVEPREP pfnLivePrep, PFNSSMDEVLIVEEXEC pfnLiveExec, PFNSSMDEVLIVEVOTE pfnLiveVote,
                                              PFNSSMDEVSAVEPREP pfnSavePrep, PFNSSMDEVSAVEEXEC pfnSaveExec, PFNSSMDEVSAVEDONE pfnSaveDone,
                                              PFNSSMDEVLOADPREP pfnLoadPrep, PFNSSMDEVLOADEXEC pfnLoadExec, PFNSSMDEVLOADDONE pfnLoadDone));

    /** @name Exported SSM Functions
     * @{ */
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutStruct,(PSSMHANDLE pSSM, const void *pvStruct, PCSSMFIELD paFields));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutStructEx,(PSSMHANDLE pSSM, const void *pvStruct, size_t cbStruct, uint32_t fFlags, PCSSMFIELD paFields, void *pvUser));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutBool,(PSSMHANDLE pSSM, bool fBool));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutU8,(PSSMHANDLE pSSM, uint8_t u8));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutS8,(PSSMHANDLE pSSM, int8_t i8));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutU16,(PSSMHANDLE pSSM, uint16_t u16));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutS16,(PSSMHANDLE pSSM, int16_t i16));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutU32,(PSSMHANDLE pSSM, uint32_t u32));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutS32,(PSSMHANDLE pSSM, int32_t i32));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutU64,(PSSMHANDLE pSSM, uint64_t u64));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutS64,(PSSMHANDLE pSSM, int64_t i64));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutU128,(PSSMHANDLE pSSM, uint128_t u128));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutS128,(PSSMHANDLE pSSM, int128_t i128));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutUInt,(PSSMHANDLE pSSM, RTUINT u));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutSInt,(PSSMHANDLE pSSM, RTINT i));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutGCUInt,(PSSMHANDLE pSSM, RTGCUINT u));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutGCUIntReg,(PSSMHANDLE pSSM, RTGCUINTREG u));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutGCPhys32,(PSSMHANDLE pSSM, RTGCPHYS32 GCPhys));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutGCPhys64,(PSSMHANDLE pSSM, RTGCPHYS64 GCPhys));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutGCPhys,(PSSMHANDLE pSSM, RTGCPHYS GCPhys));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutGCPtr,(PSSMHANDLE pSSM, RTGCPTR GCPtr));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutGCUIntPtr,(PSSMHANDLE pSSM, RTGCUINTPTR GCPtr));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutRCPtr,(PSSMHANDLE pSSM, RTRCPTR RCPtr));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutIOPort,(PSSMHANDLE pSSM, RTIOPORT IOPort));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutSel,(PSSMHANDLE pSSM, RTSEL Sel));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutMem,(PSSMHANDLE pSSM, const void *pv, size_t cb));
    DECLR3CALLBACKMEMBER(int,      pfnSSMPutStrZ,(PSSMHANDLE pSSM, const char *psz));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetStruct,(PSSMHANDLE pSSM, void *pvStruct, PCSSMFIELD paFields));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetStructEx,(PSSMHANDLE pSSM, void *pvStruct, size_t cbStruct, uint32_t fFlags, PCSSMFIELD paFields, void *pvUser));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetBool,(PSSMHANDLE pSSM, bool *pfBool));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetBoolV,(PSSMHANDLE pSSM, bool volatile *pfBool));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetU8,(PSSMHANDLE pSSM, uint8_t *pu8));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetU8V,(PSSMHANDLE pSSM, uint8_t volatile *pu8));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetS8,(PSSMHANDLE pSSM, int8_t *pi8));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetS8V,(PSSMHANDLE pSSM, int8_t volatile *pi8));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetU16,(PSSMHANDLE pSSM, uint16_t *pu16));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetU16V,(PSSMHANDLE pSSM, uint16_t volatile *pu16));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetS16,(PSSMHANDLE pSSM, int16_t *pi16));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetS16V,(PSSMHANDLE pSSM, int16_t volatile *pi16));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetU32,(PSSMHANDLE pSSM, uint32_t *pu32));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetU32V,(PSSMHANDLE pSSM, uint32_t volatile *pu32));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetS32,(PSSMHANDLE pSSM, int32_t *pi32));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetS32V,(PSSMHANDLE pSSM, int32_t volatile *pi32));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetU64,(PSSMHANDLE pSSM, uint64_t *pu64));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetU64V,(PSSMHANDLE pSSM, uint64_t volatile *pu64));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetS64,(PSSMHANDLE pSSM, int64_t *pi64));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetS64V,(PSSMHANDLE pSSM, int64_t volatile *pi64));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetU128,(PSSMHANDLE pSSM, uint128_t *pu128));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetU128V,(PSSMHANDLE pSSM, uint128_t volatile *pu128));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetS128,(PSSMHANDLE pSSM, int128_t *pi128));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetS128V,(PSSMHANDLE pSSM, int128_t  volatile *pi128));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetGCPhys32,(PSSMHANDLE pSSM, PRTGCPHYS32 pGCPhys));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetGCPhys32V,(PSSMHANDLE pSSM, RTGCPHYS32 volatile *pGCPhys));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetGCPhys64,(PSSMHANDLE pSSM, PRTGCPHYS64 pGCPhys));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetGCPhys64V,(PSSMHANDLE pSSM, RTGCPHYS64 volatile *pGCPhys));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetGCPhys,(PSSMHANDLE pSSM, PRTGCPHYS pGCPhys));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetGCPhysV,(PSSMHANDLE pSSM, RTGCPHYS volatile *pGCPhys));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetUInt,(PSSMHANDLE pSSM, PRTUINT pu));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetSInt,(PSSMHANDLE pSSM, PRTINT pi));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetGCUInt,(PSSMHANDLE pSSM, PRTGCUINT pu));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetGCUIntReg,(PSSMHANDLE pSSM, PRTGCUINTREG pu));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetGCPtr,(PSSMHANDLE pSSM, PRTGCPTR pGCPtr));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetGCUIntPtr,(PSSMHANDLE pSSM, PRTGCUINTPTR pGCPtr));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetRCPtr,(PSSMHANDLE pSSM, PRTRCPTR pRCPtr));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetIOPort,(PSSMHANDLE pSSM, PRTIOPORT pIOPort));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetSel,(PSSMHANDLE pSSM, PRTSEL pSel));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetMem,(PSSMHANDLE pSSM, void *pv, size_t cb));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetStrZ,(PSSMHANDLE pSSM, char *psz, size_t cbMax));
    DECLR3CALLBACKMEMBER(int,      pfnSSMGetStrZEx,(PSSMHANDLE pSSM, char *psz, size_t cbMax, size_t *pcbStr));
    DECLR3CALLBACKMEMBER(int,      pfnSSMSkip,(PSSMHANDLE pSSM, size_t cb));
    DECLR3CALLBACKMEMBER(int,      pfnSSMSkipToEndOfUnit,(PSSMHANDLE pSSM));
    DECLR3CALLBACKMEMBER(int,      pfnSSMSetLoadError,(PSSMHANDLE pSSM, int rc, RT_SRC_POS_DECL, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(6, 7));
    DECLR3CALLBACKMEMBER(int,      pfnSSMSetLoadErrorV,(PSSMHANDLE pSSM, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(6, 0));
    DECLR3CALLBACKMEMBER(int,      pfnSSMSetCfgError,(PSSMHANDLE pSSM, RT_SRC_POS_DECL, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(5, 6));
    DECLR3CALLBACKMEMBER(int,      pfnSSMSetCfgErrorV,(PSSMHANDLE pSSM, RT_SRC_POS_DECL, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(5, 0));
    DECLR3CALLBACKMEMBER(int,      pfnSSMHandleGetStatus,(PSSMHANDLE pSSM));
    DECLR3CALLBACKMEMBER(SSMAFTER, pfnSSMHandleGetAfter,(PSSMHANDLE pSSM));
    DECLR3CALLBACKMEMBER(bool,     pfnSSMHandleIsLiveSave,(PSSMHANDLE pSSM));
    DECLR3CALLBACKMEMBER(uint32_t, pfnSSMHandleMaxDowntime,(PSSMHANDLE pSSM));
    DECLR3CALLBACKMEMBER(uint32_t, pfnSSMHandleHostBits,(PSSMHANDLE pSSM));
    DECLR3CALLBACKMEMBER(uint32_t, pfnSSMHandleRevision,(PSSMHANDLE pSSM));
    DECLR3CALLBACKMEMBER(uint32_t, pfnSSMHandleVersion,(PSSMHANDLE pSSM));
    DECLR3CALLBACKMEMBER(const char *, pfnSSMHandleHostOSAndArch,(PSSMHANDLE pSSM));
    /** @} */

    /**
     * Creates a timer.
     *
     * @returns VBox status.
     * @param   pDevIns             The device instance.
     * @param   enmClock            The clock to use on this timer.
     * @param   pfnCallback         Callback function.
     * @param   pvUser              User argument for the callback.
     * @param   fFlags              Flags, see TMTIMER_FLAGS_*.
     * @param   pszDesc             Pointer to description string which must stay around
     *                              until the timer is fully destroyed (i.e. a bit after TMTimerDestroy()).
     * @param   ppTimer             Where to store the timer on success.
     * @remarks Caller enters the device critical section prior to invoking the
     *          callback.
     */
    DECLR3CALLBACKMEMBER(int, pfnTMTimerCreate,(PPDMDEVINS pDevIns, TMCLOCK enmClock, PFNTMTIMERDEV pfnCallback,
                                                void *pvUser, uint32_t fFlags, const char *pszDesc, PPTMTIMERR3 ppTimer));

    /**
     * Creates a timer w/ a cross context handle.
     *
     * @returns VBox status.
     * @param   pDevIns             The device instance.
     * @param   enmClock            The clock to use on this timer.
     * @param   pfnCallback         Callback function.
     * @param   pvUser              User argument for the callback.
     * @param   fFlags              Flags, see TMTIMER_FLAGS_*.
     * @param   pszDesc             Pointer to description string which must stay around
     *                              until the timer is fully destroyed (i.e. a bit after TMTimerDestroy()).
     * @param   phTimer             Where to store the timer handle on success.
     * @remarks Caller enters the device critical section prior to invoking the
     *          callback.
     */
    DECLR3CALLBACKMEMBER(int, pfnTimerCreate,(PPDMDEVINS pDevIns, TMCLOCK enmClock, PFNTMTIMERDEV pfnCallback,
                                              void *pvUser, uint32_t fFlags, const char *pszDesc, PTMTIMERHANDLE phTimer));

    /**
     * Translates a timer handle to a pointer.
     *
     * @returns The time address.
     * @param   pDevIns             The device instance.
     * @param   hTimer              The timer handle.
     */
    DECLR3CALLBACKMEMBER(PTMTIMERR3, pfnTimerToPtr,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));

    /** @name Timer handle method wrappers
     * @{ */
    DECLR3CALLBACKMEMBER(uint64_t, pfnTimerFromMicro,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMicroSecs));
    DECLR3CALLBACKMEMBER(uint64_t, pfnTimerFromMilli,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMilliSecs));
    DECLR3CALLBACKMEMBER(uint64_t, pfnTimerFromNano,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cNanoSecs));
    DECLR3CALLBACKMEMBER(uint64_t, pfnTimerGet,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    DECLR3CALLBACKMEMBER(uint64_t, pfnTimerGetFreq,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    DECLR3CALLBACKMEMBER(uint64_t, pfnTimerGetNano,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    DECLR3CALLBACKMEMBER(bool,     pfnTimerIsActive,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    DECLR3CALLBACKMEMBER(bool,     pfnTimerIsLockOwner,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnTimerLockClock,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, int rcBusy));
    /** Takes the clock lock then enters the specified critical section. */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnTimerLockClock2,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect, int rcBusy));
    DECLR3CALLBACKMEMBER(int,      pfnTimerSet,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t uExpire));
    DECLR3CALLBACKMEMBER(int,      pfnTimerSetFrequencyHint,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint32_t uHz));
    DECLR3CALLBACKMEMBER(int,      pfnTimerSetMicro,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMicrosToNext));
    DECLR3CALLBACKMEMBER(int,      pfnTimerSetMillies,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMilliesToNext));
    DECLR3CALLBACKMEMBER(int,      pfnTimerSetNano,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cNanosToNext));
    DECLR3CALLBACKMEMBER(int,      pfnTimerSetRelative,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cTicksToNext, uint64_t *pu64Now));
    DECLR3CALLBACKMEMBER(int,      pfnTimerStop,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    DECLR3CALLBACKMEMBER(void,     pfnTimerUnlockClock,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    DECLR3CALLBACKMEMBER(void,     pfnTimerUnlockClock2,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect));
    DECLR3CALLBACKMEMBER(int,      pfnTimerSetCritSect,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect));
    DECLR3CALLBACKMEMBER(int,      pfnTimerSave,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PSSMHANDLE pSSM));
    DECLR3CALLBACKMEMBER(int,      pfnTimerLoad,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PSSMHANDLE pSSM));
    DECLR3CALLBACKMEMBER(int,      pfnTimerDestroy,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    /** @sa TMR3TimerSkip */
    DECLR3CALLBACKMEMBER(int,      pfnTimerSkipLoad,(PSSMHANDLE pSSM, bool *pfActive));
    /** @} */

    /**
     * Get the real world UTC time adjusted for VM lag, user offset and warpdrive.
     *
     * @returns pTime.
     * @param   pDevIns             The device instance.
     * @param   pTime               Where to store the time.
     */
    DECLR3CALLBACKMEMBER(PRTTIMESPEC, pfnTMUtcNow,(PPDMDEVINS pDevIns, PRTTIMESPEC pTime));

    /** @name Exported CFGM Functions.
     * @{ */
    DECLR3CALLBACKMEMBER(bool,      pfnCFGMExists,(           PCFGMNODE pNode, const char *pszName));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryType,(        PCFGMNODE pNode, const char *pszName, PCFGMVALUETYPE penmType));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQuerySize,(        PCFGMNODE pNode, const char *pszName, size_t *pcb));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryInteger,(     PCFGMNODE pNode, const char *pszName, uint64_t *pu64));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryIntegerDef,(  PCFGMNODE pNode, const char *pszName, uint64_t *pu64, uint64_t u64Def));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryString,(      PCFGMNODE pNode, const char *pszName, char *pszString, size_t cchString));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryStringDef,(   PCFGMNODE pNode, const char *pszName, char *pszString, size_t cchString, const char *pszDef));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryBytes,(       PCFGMNODE pNode, const char *pszName, void *pvData, size_t cbData));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryU64,(         PCFGMNODE pNode, const char *pszName, uint64_t *pu64));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryU64Def,(      PCFGMNODE pNode, const char *pszName, uint64_t *pu64, uint64_t u64Def));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryS64,(         PCFGMNODE pNode, const char *pszName, int64_t *pi64));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryS64Def,(      PCFGMNODE pNode, const char *pszName, int64_t *pi64, int64_t i64Def));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryU32,(         PCFGMNODE pNode, const char *pszName, uint32_t *pu32));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryU32Def,(      PCFGMNODE pNode, const char *pszName, uint32_t *pu32, uint32_t u32Def));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryS32,(         PCFGMNODE pNode, const char *pszName, int32_t *pi32));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryS32Def,(      PCFGMNODE pNode, const char *pszName, int32_t *pi32, int32_t i32Def));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryU16,(         PCFGMNODE pNode, const char *pszName, uint16_t *pu16));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryU16Def,(      PCFGMNODE pNode, const char *pszName, uint16_t *pu16, uint16_t u16Def));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryS16,(         PCFGMNODE pNode, const char *pszName, int16_t *pi16));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryS16Def,(      PCFGMNODE pNode, const char *pszName, int16_t *pi16, int16_t i16Def));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryU8,(          PCFGMNODE pNode, const char *pszName, uint8_t *pu8));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryU8Def,(       PCFGMNODE pNode, const char *pszName, uint8_t *pu8, uint8_t u8Def));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryS8,(          PCFGMNODE pNode, const char *pszName, int8_t *pi8));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryS8Def,(       PCFGMNODE pNode, const char *pszName, int8_t *pi8, int8_t i8Def));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryBool,(        PCFGMNODE pNode, const char *pszName, bool *pf));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryBoolDef,(     PCFGMNODE pNode, const char *pszName, bool *pf, bool fDef));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryPort,(        PCFGMNODE pNode, const char *pszName, PRTIOPORT pPort));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryPortDef,(     PCFGMNODE pNode, const char *pszName, PRTIOPORT pPort, RTIOPORT PortDef));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryUInt,(        PCFGMNODE pNode, const char *pszName, unsigned int *pu));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryUIntDef,(     PCFGMNODE pNode, const char *pszName, unsigned int *pu, unsigned int uDef));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQuerySInt,(        PCFGMNODE pNode, const char *pszName, signed int *pi));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQuerySIntDef,(     PCFGMNODE pNode, const char *pszName, signed int *pi, signed int iDef));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryPtr,(         PCFGMNODE pNode, const char *pszName, void **ppv));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryPtrDef,(      PCFGMNODE pNode, const char *pszName, void **ppv, void *pvDef));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryGCPtr,(       PCFGMNODE pNode, const char *pszName, PRTGCPTR pGCPtr));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryGCPtrDef,(    PCFGMNODE pNode, const char *pszName, PRTGCPTR pGCPtr, RTGCPTR GCPtrDef));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryGCPtrU,(      PCFGMNODE pNode, const char *pszName, PRTGCUINTPTR pGCPtr));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryGCPtrUDef,(   PCFGMNODE pNode, const char *pszName, PRTGCUINTPTR pGCPtr, RTGCUINTPTR GCPtrDef));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryGCPtrS,(      PCFGMNODE pNode, const char *pszName, PRTGCINTPTR pGCPtr));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryGCPtrSDef,(   PCFGMNODE pNode, const char *pszName, PRTGCINTPTR pGCPtr, RTGCINTPTR GCPtrDef));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryStringAlloc,( PCFGMNODE pNode, const char *pszName, char **ppszString));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMQueryStringAllocDef,(PCFGMNODE pNode, const char *pszName, char **ppszString, const char *pszDef));
    DECLR3CALLBACKMEMBER(PCFGMNODE, pfnCFGMGetParent,(PCFGMNODE pNode));
    DECLR3CALLBACKMEMBER(PCFGMNODE, pfnCFGMGetChild,(PCFGMNODE pNode, const char *pszPath));
    DECLR3CALLBACKMEMBER(PCFGMNODE, pfnCFGMGetChildF,(PCFGMNODE pNode, const char *pszPathFormat, ...) RT_IPRT_FORMAT_ATTR(2, 3));
    DECLR3CALLBACKMEMBER(PCFGMNODE, pfnCFGMGetChildFV,(PCFGMNODE pNode, const char *pszPathFormat, va_list Args) RT_IPRT_FORMAT_ATTR(3, 0));
    DECLR3CALLBACKMEMBER(PCFGMNODE, pfnCFGMGetFirstChild,(PCFGMNODE pNode));
    DECLR3CALLBACKMEMBER(PCFGMNODE, pfnCFGMGetNextChild,(PCFGMNODE pCur));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMGetName,(PCFGMNODE pCur, char *pszName, size_t cchName));
    DECLR3CALLBACKMEMBER(size_t,    pfnCFGMGetNameLen,(PCFGMNODE pCur));
    DECLR3CALLBACKMEMBER(bool,      pfnCFGMAreChildrenValid,(PCFGMNODE pNode, const char *pszzValid));
    DECLR3CALLBACKMEMBER(PCFGMLEAF, pfnCFGMGetFirstValue,(PCFGMNODE pCur));
    DECLR3CALLBACKMEMBER(PCFGMLEAF, pfnCFGMGetNextValue,(PCFGMLEAF pCur));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMGetValueName,(PCFGMLEAF pCur, char *pszName, size_t cchName));
    DECLR3CALLBACKMEMBER(size_t,    pfnCFGMGetValueNameLen,(PCFGMLEAF pCur));
    DECLR3CALLBACKMEMBER(CFGMVALUETYPE, pfnCFGMGetValueType,(PCFGMLEAF pCur));
    DECLR3CALLBACKMEMBER(bool,      pfnCFGMAreValuesValid,(PCFGMNODE pNode, const char *pszzValid));
    DECLR3CALLBACKMEMBER(int,       pfnCFGMValidateConfig,(PCFGMNODE pNode, const char *pszNode,
                                                           const char *pszValidValues, const char *pszValidNodes,
                                                           const char *pszWho, uint32_t uInstance));
    /** @} */

    /**
     * Read physical memory.
     *
     * @returns VINF_SUCCESS (for now).
     * @param   pDevIns             The device instance.
     * @param   GCPhys              Physical address start reading from.
     * @param   pvBuf               Where to put the read bits.
     * @param   cbRead              How many bytes to read.
     * @thread  Any thread, but the call may involve the emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnPhysRead,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead));

    /**
     * Write to physical memory.
     *
     * @returns VINF_SUCCESS for now, and later maybe VERR_EM_MEMORY.
     * @param   pDevIns             The device instance.
     * @param   GCPhys              Physical address to write to.
     * @param   pvBuf               What to write.
     * @param   cbWrite             How many bytes to write.
     * @thread  Any thread, but the call may involve the emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnPhysWrite,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite));

    /**
     * Requests the mapping of a guest page into ring-3.
     *
     * When you're done with the page, call pfnPhysReleasePageMappingLock() ASAP to
     * release it.
     *
     * This API will assume your intention is to write to the page, and will
     * therefore replace shared and zero pages. If you do not intend to modify the
     * page, use the pfnPhysGCPhys2CCPtrReadOnly() API.
     *
     * @returns VBox status code.
     * @retval  VINF_SUCCESS on success.
     * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical
     *          backing or if the page has any active access handlers. The caller
     *          must fall back on using PGMR3PhysWriteExternal.
     * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
     *
     * @param   pDevIns             The device instance.
     * @param   GCPhys              The guest physical address of the page that
     *                              should be mapped.
     * @param   fFlags              Flags reserved for future use, MBZ.
     * @param   ppv                 Where to store the address corresponding to
     *                              GCPhys.
     * @param   pLock               Where to store the lock information that
     *                              pfnPhysReleasePageMappingLock needs.
     *
     * @remark  Avoid calling this API from within critical sections (other than the
     *          PGM one) because of the deadlock risk when we have to delegating the
     *          task to an EMT.
     * @thread  Any.
     */
    DECLR3CALLBACKMEMBER(int, pfnPhysGCPhys2CCPtr,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t fFlags, void **ppv,
                                                   PPGMPAGEMAPLOCK pLock));

    /**
     * Requests the mapping of a guest page into ring-3, external threads.
     *
     * When you're done with the page, call pfnPhysReleasePageMappingLock() ASAP to
     * release it.
     *
     * @returns VBox status code.
     * @retval  VINF_SUCCESS on success.
     * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical
     *          backing or if the page as an active ALL access handler. The caller
     *          must fall back on using PGMPhysRead.
     * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
     *
     * @param   pDevIns             The device instance.
     * @param   GCPhys              The guest physical address of the page that
     *                              should be mapped.
     * @param   fFlags              Flags reserved for future use, MBZ.
     * @param   ppv                 Where to store the address corresponding to
     *                              GCPhys.
     * @param   pLock               Where to store the lock information that
     *                              pfnPhysReleasePageMappingLock needs.
     *
     * @remark  Avoid calling this API from within critical sections.
     * @thread  Any.
     */
    DECLR3CALLBACKMEMBER(int, pfnPhysGCPhys2CCPtrReadOnly,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t fFlags,
                                                           void const **ppv, PPGMPAGEMAPLOCK pLock));

    /**
     * Release the mapping of a guest page.
     *
     * This is the counter part of pfnPhysGCPhys2CCPtr and
     * pfnPhysGCPhys2CCPtrReadOnly.
     *
     * @param   pDevIns             The device instance.
     * @param   pLock               The lock structure initialized by the mapping
     *                              function.
     */
    DECLR3CALLBACKMEMBER(void, pfnPhysReleasePageMappingLock,(PPDMDEVINS pDevIns, PPGMPAGEMAPLOCK pLock));

    /**
     * Read guest physical memory by virtual address.
     *
     * @param   pDevIns             The device instance.
     * @param   pvDst               Where to put the read bits.
     * @param   GCVirtSrc           Guest virtual address to start reading from.
     * @param   cb                  How many bytes to read.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnPhysReadGCVirt,(PPDMDEVINS pDevIns, void *pvDst, RTGCPTR GCVirtSrc, size_t cb));

    /**
     * Write to guest physical memory by virtual address.
     *
     * @param   pDevIns             The device instance.
     * @param   GCVirtDst           Guest virtual address to write to.
     * @param   pvSrc               What to write.
     * @param   cb                  How many bytes to write.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnPhysWriteGCVirt,(PPDMDEVINS pDevIns, RTGCPTR GCVirtDst, const void *pvSrc, size_t cb));

    /**
     * Convert a guest virtual address to a guest physical address.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   GCPtr               Guest virtual address.
     * @param   pGCPhys             Where to store the GC physical address
     *                              corresponding to GCPtr.
     * @thread  The emulation thread.
     * @remark  Careful with page boundaries.
     */
    DECLR3CALLBACKMEMBER(int, pfnPhysGCPtr2GCPhys, (PPDMDEVINS pDevIns, RTGCPTR GCPtr, PRTGCPHYS pGCPhys));

    /**
     * Allocate memory which is associated with current VM instance
     * and automatically freed on it's destruction.
     *
     * @returns Pointer to allocated memory. The memory is *NOT* zero-ed.
     * @param   pDevIns             The device instance.
     * @param   cb                  Number of bytes to allocate.
     */
    DECLR3CALLBACKMEMBER(void *, pfnMMHeapAlloc,(PPDMDEVINS pDevIns, size_t cb));

    /**
     * Allocate memory which is associated with current VM instance
     * and automatically freed on it's destruction. The memory is ZEROed.
     *
     * @returns Pointer to allocated memory. The memory is *NOT* zero-ed.
     * @param   pDevIns             The device instance.
     * @param   cb                  Number of bytes to allocate.
     */
    DECLR3CALLBACKMEMBER(void *, pfnMMHeapAllocZ,(PPDMDEVINS pDevIns, size_t cb));

    /**
     * Free memory allocated with pfnMMHeapAlloc() and pfnMMHeapAllocZ().
     *
     * @param   pDevIns             The device instance.
     * @param   pv                  Pointer to the memory to free.
     */
    DECLR3CALLBACKMEMBER(void, pfnMMHeapFree,(PPDMDEVINS pDevIns, void *pv));

    /**
     * Gets the VM state.
     *
     * @returns VM state.
     * @param   pDevIns             The device instance.
     * @thread  Any thread (just keep in mind that it's volatile info).
     */
    DECLR3CALLBACKMEMBER(VMSTATE, pfnVMState, (PPDMDEVINS pDevIns));

    /**
     * Checks if the VM was teleported and hasn't been fully resumed yet.
     *
     * @returns true / false.
     * @param   pDevIns             The device instance.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(bool, pfnVMTeleportedAndNotFullyResumedYet,(PPDMDEVINS pDevIns));

    /**
     * Set the VM error message
     *
     * @returns rc.
     * @param   pDevIns             The device instance.
     * @param   rc                  VBox status code.
     * @param   SRC_POS             Use RT_SRC_POS.
     * @param   pszFormat           Error message format string.
     * @param   ...                 Error message arguments.
     */
    DECLR3CALLBACKMEMBER(int, pfnVMSetError,(PPDMDEVINS pDevIns, int rc, RT_SRC_POS_DECL,
                                             const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(6, 7));

    /**
     * Set the VM error message
     *
     * @returns rc.
     * @param   pDevIns             The device instance.
     * @param   rc                  VBox status code.
     * @param   SRC_POS             Use RT_SRC_POS.
     * @param   pszFormat           Error message format string.
     * @param   va                  Error message arguments.
     */
    DECLR3CALLBACKMEMBER(int, pfnVMSetErrorV,(PPDMDEVINS pDevIns, int rc, RT_SRC_POS_DECL,
                                              const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(6, 0));

    /**
     * Set the VM runtime error message
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   fFlags              The action flags. See VMSETRTERR_FLAGS_*.
     * @param   pszErrorId          Error ID string.
     * @param   pszFormat           Error message format string.
     * @param   ...                 Error message arguments.
     */
    DECLR3CALLBACKMEMBER(int, pfnVMSetRuntimeError,(PPDMDEVINS pDevIns, uint32_t fFlags, const char *pszErrorId,
                                                    const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(4, 5));

    /**
     * Set the VM runtime error message
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   fFlags              The action flags. See VMSETRTERR_FLAGS_*.
     * @param   pszErrorId          Error ID string.
     * @param   pszFormat           Error message format string.
     * @param   va                  Error message arguments.
     */
    DECLR3CALLBACKMEMBER(int, pfnVMSetRuntimeErrorV,(PPDMDEVINS pDevIns, uint32_t fFlags, const char *pszErrorId,
                                                     const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(4, 0));

    /**
     * Stops the VM and enters the debugger to look at the guest state.
     *
     * Use the PDMDeviceDBGFStop() inline function with the RT_SRC_POS macro instead of
     * invoking this function directly.
     *
     * @returns VBox status code which must be passed up to the VMM.
     * @param   pDevIns             The device instance.
     * @param   pszFile             Filename of the assertion location.
     * @param   iLine               The linenumber of the assertion location.
     * @param   pszFunction         Function of the assertion location.
     * @param   pszFormat           Message. (optional)
     * @param   args                Message parameters.
     */
    DECLR3CALLBACKMEMBER(int, pfnDBGFStopV,(PPDMDEVINS pDevIns, const char *pszFile, unsigned iLine, const char *pszFunction,
                                            const char *pszFormat, va_list args) RT_IPRT_FORMAT_ATTR(5, 0));

    /**
     * Register a info handler with DBGF.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pszName             The identifier of the info.
     * @param   pszDesc             The description of the info and any arguments
     *                              the handler may take.
     * @param   pfnHandler          The handler function to be called to display the
     *                              info.
     */
    DECLR3CALLBACKMEMBER(int, pfnDBGFInfoRegister,(PPDMDEVINS pDevIns, const char *pszName, const char *pszDesc, PFNDBGFHANDLERDEV pfnHandler));

    /**
     * Register a info handler with DBGF, argv style.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pszName             The identifier of the info.
     * @param   pszDesc             The description of the info and any arguments
     *                              the handler may take.
     * @param   pfnHandler          The handler function to be called to display the
     *                              info.
     */
    DECLR3CALLBACKMEMBER(int, pfnDBGFInfoRegisterArgv,(PPDMDEVINS pDevIns, const char *pszName, const char *pszDesc, PFNDBGFINFOARGVDEV pfnHandler));

    /**
     * Registers a set of registers for a device.
     *
     * The @a pvUser argument of the getter and setter callbacks will be
     * @a pDevIns.  The register names will be prefixed by the device name followed
     * immediately by the instance number.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   paRegisters         The register descriptors.
     *
     * @remarks The device critical section is NOT entered prior to working the
     *          callbacks registered via this helper!
     */
    DECLR3CALLBACKMEMBER(int, pfnDBGFRegRegister,(PPDMDEVINS pDevIns, PCDBGFREGDESC paRegisters));

    /**
     * Gets the trace buffer handle.
     *
     * This is used by the macros found in VBox/vmm/dbgftrace.h and is not
     * really inteded for direct usage, thus no inline wrapper function.
     *
     * @returns Trace buffer handle or NIL_RTTRACEBUF.
     * @param   pDevIns             The device instance.
     */
    DECLR3CALLBACKMEMBER(RTTRACEBUF, pfnDBGFTraceBuf,(PPDMDEVINS pDevIns));

    /**
     * Registers a statistics sample.
     *
     * @param   pDevIns             Device instance of the DMA.
     * @param   pvSample            Pointer to the sample.
     * @param   enmType             Sample type. This indicates what pvSample is
     *                              pointing at.
     * @param   pszName             Sample name, unix path style.  If this does not
     *                              start with a '/', the default prefix will be
     *                              prepended, otherwise it will be used as-is.
     * @param   enmUnit             Sample unit.
     * @param   pszDesc             Sample description.
     */
    DECLR3CALLBACKMEMBER(void, pfnSTAMRegister,(PPDMDEVINS pDevIns, void *pvSample, STAMTYPE enmType, const char *pszName, STAMUNIT enmUnit, const char *pszDesc));

    /**
     * Same as pfnSTAMRegister except that the name is specified in a
     * RTStrPrintfV like fashion.
     *
     * @returns VBox status.
     * @param   pDevIns             Device instance of the DMA.
     * @param   pvSample            Pointer to the sample.
     * @param   enmType             Sample type. This indicates what pvSample is
     *                              pointing at.
     * @param   enmVisibility       Visibility type specifying whether unused
     *                              statistics should be visible or not.
     * @param   enmUnit             Sample unit.
     * @param   pszDesc             Sample description.
     * @param   pszName             Sample name format string, unix path style.  If
     *                              this does not start with a '/', the default
     *                              prefix will be prepended, otherwise it will be
     *                              used as-is.
     * @param   args                Arguments to the format string.
     */
    DECLR3CALLBACKMEMBER(void, pfnSTAMRegisterV,(PPDMDEVINS pDevIns, void *pvSample, STAMTYPE enmType,
                                                 STAMVISIBILITY enmVisibility, STAMUNIT enmUnit, const char *pszDesc,
                                                 const char *pszName, va_list args) RT_IPRT_FORMAT_ATTR(7, 0));

    /**
     * Registers a PCI device with the default PCI bus.
     *
     * If a PDM device has more than one PCI device, they must be registered in the
     * order of PDMDEVINSR3::apPciDevs.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device structure.
     *                              This must be kept in the instance data.
     *                              The PCI configuration must be initialized before registration.
     * @param   fFlags              0, PDMPCIDEVREG_F_PCI_BRIDGE or
     *                              PDMPCIDEVREG_F_NOT_MANDATORY_NO.
     * @param   uPciDevNo           PDMPCIDEVREG_DEV_NO_FIRST_UNUSED,
     *                              PDMPCIDEVREG_DEV_NO_SAME_AS_PREV, or a specific
     *                              device number (0-31).  This will be ignored if
     *                              the CFGM configuration contains a PCIDeviceNo
     *                              value.
     * @param   uPciFunNo           PDMPCIDEVREG_FUN_NO_FIRST_UNUSED, or a specific
     *                              function number (0-7).  This will be ignored if
     *                              the CFGM configuration contains a PCIFunctionNo
     *                              value.
     * @param   pszName             Device name, if NULL PDMDEVREG::szName is used.
     *                              The pointer is saved, so don't free or changed.
     * @note    The PCI device configuration is now implicit from the apPciDevs
     *          index, meaning that the zero'th entry is the primary one and
     *          subsequent uses CFGM subkeys "PciDev1", "PciDev2" and so on.
     */
    DECLR3CALLBACKMEMBER(int, pfnPCIRegister,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t fFlags,
                                              uint8_t uPciDevNo, uint8_t uPciFunNo, const char *pszName));

    /**
     * Initialize MSI or MSI-X emulation support for the given PCI device.
     *
     * @see PDMPCIBUSREG::pfnRegisterMsiR3 for details.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device.  NULL is an alias for the first
     *                              one registered.
     * @param   pMsiReg             MSI emulation registration structure.
     */
    DECLR3CALLBACKMEMBER(int, pfnPCIRegisterMsi,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, PPDMMSIREG pMsiReg));

    /**
     * Registers a I/O region (memory mapped or I/O ports) for a PCI device.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device structure.  If NULL the default
     *                              PCI device for this device instance is used.
     * @param   iRegion             The region number.
     * @param   cbRegion            Size of the region.
     * @param   enmType             PCI_ADDRESS_SPACE_MEM, PCI_ADDRESS_SPACE_IO or PCI_ADDRESS_SPACE_MEM_PREFETCH.
     * @param   fFlags              PDMPCIDEV_IORGN_F_XXX.
     * @param   hHandle             An I/O port, MMIO or MMIO2 handle according to
     *                              @a fFlags, UINT64_MAX if no handle is passed
     *                              (old style).
     * @param   pfnMapUnmap         Callback for doing the mapping, optional when a
     *                              handle is specified.  The callback will be
     *                              invoked holding only the PDM lock.  The device
     *                              lock will _not_ be taken (due to lock order).
     */
    DECLR3CALLBACKMEMBER(int, pfnPCIIORegionRegister,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                      RTGCPHYS cbRegion, PCIADDRESSSPACE enmType, uint32_t fFlags,
                                                      uint64_t hHandle, PFNPCIIOREGIONMAP pfnMapUnmap));

    /**
     * Register PCI configuration space read/write callbacks.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device structure.  If NULL the default
     *                              PCI device for this device instance is used.
     * @param   pfnRead             Pointer to the user defined PCI config read function.
     *                              to call default PCI config read function. Can be NULL.
     * @param   pfnWrite            Pointer to the user defined PCI config write function.
     * @remarks The callbacks will be invoked holding the PDM lock. The device lock
     *          is NOT take because that is very likely be a lock order violation.
     * @thread  EMT(0)
     * @note    Only callable during VM creation.
     * @sa      PDMDevHlpPCIConfigRead, PDMDevHlpPCIConfigWrite
     */
    DECLR3CALLBACKMEMBER(int, pfnPCIInterceptConfigAccesses,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                             PFNPCICONFIGREAD pfnRead, PFNPCICONFIGWRITE pfnWrite));

    /**
     * Perform a PCI configuration space write.
     *
     * This is for devices that make use of PDMDevHlpPCIInterceptConfigAccesses().
     *
     * @returns Strict VBox status code (mainly DBGFSTOP).
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device which config space is being read.
     * @param   uAddress            The config space address.
     * @param   cb                  The size of the read: 1, 2 or 4 bytes.
     * @param   u32Value            The value to write.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnPCIConfigWrite,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                          uint32_t uAddress, unsigned cb, uint32_t u32Value));

    /**
     * Perform a PCI configuration space read.
     *
     * This is for devices that make use of PDMDevHlpPCIInterceptConfigAccesses().
     *
     * @returns Strict VBox status code (mainly DBGFSTOP).
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device which config space is being read.
     * @param   uAddress            The config space address.
     * @param   cb                  The size of the read: 1, 2 or 4 bytes.
     * @param   pu32Value           Where to return the value.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnPCIConfigRead,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                         uint32_t uAddress, unsigned cb, uint32_t *pu32Value));

    /**
     * Bus master physical memory read.
     *
     * @returns VINF_SUCCESS or VERR_PGM_PCI_PHYS_READ_BM_DISABLED, later maybe
     *          VERR_EM_MEMORY.  The informational status shall NOT be propagated!
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device structure.  If NULL the default
     *                              PCI device for this device instance is used.
     * @param   GCPhys              Physical address start reading from.
     * @param   pvBuf               Where to put the read bits.
     * @param   cbRead              How many bytes to read.
     * @thread  Any thread, but the call may involve the emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnPCIPhysRead,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead));

    /**
     * Bus master physical memory write.
     *
     * @returns VINF_SUCCESS or VERR_PGM_PCI_PHYS_WRITE_BM_DISABLED, later maybe
     *          VERR_EM_MEMORY.  The informational status shall NOT be propagated!
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device structure.  If NULL the default
     *                              PCI device for this device instance is used.
     * @param   GCPhys              Physical address to write to.
     * @param   pvBuf               What to write.
     * @param   cbWrite             How many bytes to write.
     * @thread  Any thread, but the call may involve the emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnPCIPhysWrite,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite));

    /**
     * Sets the IRQ for the given PCI device.
     *
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device structure.  If NULL the default
     *                              PCI device for this device instance is used.
     * @param   iIrq                IRQ number to set.
     * @param   iLevel              IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @thread  Any thread, but will involve the emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnPCISetIrq,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel));

    /**
     * Sets the IRQ for the given PCI device, but doesn't wait for EMT to process
     * the request when not called from EMT.
     *
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device structure.  If NULL the default
     *                              PCI device for this device instance is used.
     * @param   iIrq                IRQ number to set.
     * @param   iLevel              IRQ level.
     * @thread  Any thread, but will involve the emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnPCISetIrqNoWait,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel));

    /**
     * Set ISA IRQ for a device.
     *
     * @param   pDevIns             The device instance.
     * @param   iIrq                IRQ number to set.
     * @param   iLevel              IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @thread  Any thread, but will involve the emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnISASetIrq,(PPDMDEVINS pDevIns, int iIrq, int iLevel));

    /**
     * Set the ISA IRQ for a device, but don't wait for EMT to process
     * the request when not called from EMT.
     *
     * @param   pDevIns             The device instance.
     * @param   iIrq                IRQ number to set.
     * @param   iLevel              IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @thread  Any thread, but will involve the emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnISASetIrqNoWait,(PPDMDEVINS pDevIns, int iIrq, int iLevel));

    /**
     * Send an MSI straight to the I/O APIC.
     *
     * @param   pDevIns         PCI device instance.
     * @param   GCPhys          Physical address MSI request was written.
     * @param   uValue          Value written.
     * @thread  Any thread, but will involve the emulation thread.
     */
    DECLR3CALLBACKMEMBER(void,  pfnIoApicSendMsi,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t uValue));

    /**
     * Attaches a driver (chain) to the device.
     *
     * The first call for a LUN this will serve as a registration of the LUN. The pBaseInterface and
     * the pszDesc string will be registered with that LUN and kept around for PDMR3QueryDeviceLun().
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   iLun                The logical unit to attach.
     * @param   pBaseInterface      Pointer to the base interface for that LUN. (device side / down)
     * @param   ppBaseInterface     Where to store the pointer to the base interface. (driver side / up)
     * @param   pszDesc             Pointer to a string describing the LUN. This string must remain valid
     *                              for the live of the device instance.
     */
    DECLR3CALLBACKMEMBER(int, pfnDriverAttach,(PPDMDEVINS pDevIns, uint32_t iLun, PPDMIBASE pBaseInterface,
                                               PPDMIBASE *ppBaseInterface, const char *pszDesc));

    /**
     * Detaches an attached driver (chain) from the device again.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pDrvIns             The driver instance to detach.
     * @param   fFlags              Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
     */
    DECLR3CALLBACKMEMBER(int, pfnDriverDetach,(PPDMDEVINS pDevIns, PPDMDRVINS pDrvIns, uint32_t fFlags));

    /**
     * Reconfigures the driver chain for a LUN, detaching any driver currently
     * present there.
     *
     * Caller will have attach it, of course.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   iLun                The logical unit to reconfigure.
     * @param   cDepth              The depth of the driver chain. Determins the
     *                              size of @a papszDrivers and @a papConfigs.
     * @param   papszDrivers        The names of the drivers to configure in the
     *                              chain, first entry is the one immediately
     *                              below the device/LUN
     * @param   papConfigs          The configurations for each of the drivers
     *                              in @a papszDrivers array.  NULL entries
     *                              corresponds to empty 'Config' nodes.  This
     *                              function will take ownership of non-NULL
     *                              CFGM sub-trees and set the array member to
     *                              NULL, so the caller can do cleanups on
     *                              failure.  This parameter is optional.
     * @param   fFlags              Reserved, MBZ.
     */
    DECLR3CALLBACKMEMBER(int, pfnDriverReconfigure,(PPDMDEVINS pDevIns, uint32_t iLun, uint32_t cDepth,
                                                    const char * const *papszDrivers, PCFGMNODE *papConfigs, uint32_t fFlags));

    /** @name Exported PDM Queue Functions
     * @{ */
    /**
     * Create a queue.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   cbItem              The size of a queue item.
     * @param   cItems              The number of items in the queue.
     * @param   cMilliesInterval    The number of milliseconds between polling the queue.
     *                              If 0 then the emulation thread will be notified whenever an item arrives.
     * @param   pfnCallback         The consumer function.
     * @param   fRZEnabled          Set if the queue should work in RC and R0.
     * @param   pszName             The queue base name. The instance number will be
     *                              appended automatically.
     * @param   ppQueue             Where to store the queue pointer on success.
     * @thread  The emulation thread.
     * @remarks The device critical section will NOT be entered before calling the
     *          callback.  No locks will be held, but for now it's safe to assume
     *          that only one EMT will do queue callbacks at any one time.
     */
    DECLR3CALLBACKMEMBER(int, pfnQueueCreatePtr,(PPDMDEVINS pDevIns, size_t cbItem, uint32_t cItems, uint32_t cMilliesInterval,
                                                 PFNPDMQUEUEDEV pfnCallback, bool fRZEnabled, const char *pszName,
                                                 PPDMQUEUE *ppQueue));

    /**
     * Create a queue.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   cbItem              The size of a queue item.
     * @param   cItems              The number of items in the queue.
     * @param   cMilliesInterval    The number of milliseconds between polling the queue.
     *                              If 0 then the emulation thread will be notified whenever an item arrives.
     * @param   pfnCallback         The consumer function.
     * @param   fRZEnabled          Set if the queue should work in RC and R0.
     * @param   pszName             The queue base name. The instance number will be
     *                              appended automatically.
     * @param   phQueue             Where to store the queue handle on success.
     * @thread  EMT(0)
     * @remarks The device critical section will NOT be entered before calling the
     *          callback.  No locks will be held, but for now it's safe to assume
     *          that only one EMT will do queue callbacks at any one time.
     */
    DECLR3CALLBACKMEMBER(int, pfnQueueCreate,(PPDMDEVINS pDevIns, size_t cbItem, uint32_t cItems, uint32_t cMilliesInterval,
                                              PFNPDMQUEUEDEV pfnCallback, bool fRZEnabled, const char *pszName,
                                              PDMQUEUEHANDLE *phQueue));

    DECLR3CALLBACKMEMBER(PPDMQUEUE, pfnQueueToPtr,(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue));
    DECLR3CALLBACKMEMBER(PPDMQUEUEITEMCORE, pfnQueueAlloc,(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue));
    DECLR3CALLBACKMEMBER(void, pfnQueueInsert,(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue, PPDMQUEUEITEMCORE pItem));
    DECLR3CALLBACKMEMBER(void, pfnQueueInsertEx,(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue, PPDMQUEUEITEMCORE pItem, uint64_t cNanoMaxDelay));
    DECLR3CALLBACKMEMBER(bool, pfnQueueFlushIfNecessary,(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue));
    /** @} */

    /** @name PDM Task
     * @{ */
    /**
     * Create an asynchronous ring-3 task.
     *
     * @returns VBox status code.
     * @param   pDevIns         The device instance.
     * @param   fFlags          PDMTASK_F_XXX
     * @param   pszName         The function name or similar.  Used for statistics,
     *                          so no slashes.
     * @param   pfnCallback     The task function.
     * @param   pvUser          User argument for the task function.
     * @param   phTask          Where to return the task handle.
     * @thread  EMT(0)
     */
    DECLR3CALLBACKMEMBER(int, pfnTaskCreate,(PPDMDEVINS pDevIns, uint32_t fFlags, const char *pszName,
                                             PFNPDMTASKDEV pfnCallback, void *pvUser, PDMTASKHANDLE *phTask));
    /**
     * Triggers the running the given task.
     *
     * @returns VBox status code.
     * @retval  VINF_ALREADY_POSTED is the task is already pending.
     * @param   pDevIns         The device instance.
     * @param   hTask           The task to trigger.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnTaskTrigger,(PPDMDEVINS pDevIns, PDMTASKHANDLE hTask));
    /** @} */

    /** @name SUP Event Semaphore Wrappers (single release / auto reset)
     * These semaphores can be signalled from ring-0.
     * @{ */
    /** @sa SUPSemEventCreate */
    DECLR3CALLBACKMEMBER(int, pfnSUPSemEventCreate,(PPDMDEVINS pDevIns, PSUPSEMEVENT phEvent));
    /** @sa SUPSemEventClose */
    DECLR3CALLBACKMEMBER(int, pfnSUPSemEventClose,(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent));
    /** @sa SUPSemEventSignal */
    DECLR3CALLBACKMEMBER(int, pfnSUPSemEventSignal,(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent));
    /** @sa SUPSemEventWaitNoResume */
    DECLR3CALLBACKMEMBER(int, pfnSUPSemEventWaitNoResume,(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint32_t cMillies));
    /** @sa SUPSemEventWaitNsAbsIntr */
    DECLR3CALLBACKMEMBER(int, pfnSUPSemEventWaitNsAbsIntr,(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint64_t uNsTimeout));
    /** @sa SUPSemEventWaitNsRelIntr */
    DECLR3CALLBACKMEMBER(int, pfnSUPSemEventWaitNsRelIntr,(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint64_t cNsTimeout));
    /** @sa SUPSemEventGetResolution */
    DECLR3CALLBACKMEMBER(uint32_t, pfnSUPSemEventGetResolution,(PPDMDEVINS pDevIns));
    /** @} */

    /** @name SUP Multi Event Semaphore Wrappers (multiple release / manual reset)
     * These semaphores can be signalled from ring-0.
     * @{ */
    /** @sa SUPSemEventMultiCreate */
    DECLR3CALLBACKMEMBER(int, pfnSUPSemEventMultiCreate,(PPDMDEVINS pDevIns, PSUPSEMEVENTMULTI phEventMulti));
    /** @sa SUPSemEventMultiClose */
    DECLR3CALLBACKMEMBER(int, pfnSUPSemEventMultiClose,(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti));
    /** @sa SUPSemEventMultiSignal */
    DECLR3CALLBACKMEMBER(int, pfnSUPSemEventMultiSignal,(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti));
    /** @sa SUPSemEventMultiReset */
    DECLR3CALLBACKMEMBER(int, pfnSUPSemEventMultiReset,(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti));
    /** @sa SUPSemEventMultiWaitNoResume */
    DECLR3CALLBACKMEMBER(int, pfnSUPSemEventMultiWaitNoResume,(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti, uint32_t cMillies));
    /** @sa SUPSemEventMultiWaitNsAbsIntr */
    DECLR3CALLBACKMEMBER(int, pfnSUPSemEventMultiWaitNsAbsIntr,(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti, uint64_t uNsTimeout));
    /** @sa SUPSemEventMultiWaitNsRelIntr */
    DECLR3CALLBACKMEMBER(int, pfnSUPSemEventMultiWaitNsRelIntr,(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti, uint64_t cNsTimeout));
    /** @sa SUPSemEventMultiGetResolution */
    DECLR3CALLBACKMEMBER(uint32_t, pfnSUPSemEventMultiGetResolution,(PPDMDEVINS pDevIns));
    /** @} */

    /**
     * Initializes a PDM critical section.
     *
     * The PDM critical sections are derived from the IPRT critical sections, but
     * works in RC and R0 as well.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pCritSect           Pointer to the critical section.
     * @param   SRC_POS             Use RT_SRC_POS.
     * @param   pszNameFmt          Format string for naming the critical section.
     *                              For statistics and lock validation.
     * @param   va                  Arguments for the format string.
     */
    DECLR3CALLBACKMEMBER(int, pfnCritSectInit,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, RT_SRC_POS_DECL,
                                               const char *pszNameFmt, va_list va) RT_IPRT_FORMAT_ATTR(6, 0));

    /**
     * Gets the NOP critical section.
     *
     * @returns The ring-3 address of the NOP critical section.
     * @param   pDevIns             The device instance.
     */
    DECLR3CALLBACKMEMBER(PPDMCRITSECT, pfnCritSectGetNop,(PPDMDEVINS pDevIns));

    /**
     * Gets the NOP critical section.
     *
     * @returns The ring-0 address of the NOP critical section.
     * @param   pDevIns             The device instance.
     * @deprecated
     */
    DECLR3CALLBACKMEMBER(R0PTRTYPE(PPDMCRITSECT), pfnCritSectGetNopR0,(PPDMDEVINS pDevIns));

    /**
     * Gets the NOP critical section.
     *
     * @returns The raw-mode context address of the NOP critical section.
     * @param   pDevIns             The device instance.
     * @deprecated
     */
    DECLR3CALLBACKMEMBER(RCPTRTYPE(PPDMCRITSECT), pfnCritSectGetNopRC,(PPDMDEVINS pDevIns));

    /**
     * Changes the device level critical section from the automatically created
     * default to one desired by the device constructor.
     *
     * For ring-0 and raw-mode capable devices, the call must be repeated in each of
     * the additional contexts.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pCritSect           The critical section to use.  NULL is not
     *                              valid, instead use the NOP critical
     *                              section.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetDeviceCritSect,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect));

    /** @name Exported PDM Critical Section Functions
     * @{ */
    DECLR3CALLBACKMEMBER(bool,     pfnCritSectYield,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect));
    DECLR3CALLBACKMEMBER(int,      pfnCritSectEnter,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy));
    DECLR3CALLBACKMEMBER(int,      pfnCritSectEnterDebug,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy, RTHCUINTPTR uId, RT_SRC_POS_DECL));
    DECLR3CALLBACKMEMBER(int,      pfnCritSectTryEnter,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect));
    DECLR3CALLBACKMEMBER(int,      pfnCritSectTryEnterDebug,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, RTHCUINTPTR uId, RT_SRC_POS_DECL));
    DECLR3CALLBACKMEMBER(int,      pfnCritSectLeave,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect));
    DECLR3CALLBACKMEMBER(bool,     pfnCritSectIsOwner,(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect));
    DECLR3CALLBACKMEMBER(bool,     pfnCritSectIsInitialized,(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect));
    DECLR3CALLBACKMEMBER(bool,     pfnCritSectHasWaiters,(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect));
    DECLR3CALLBACKMEMBER(uint32_t, pfnCritSectGetRecursion,(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect));
    DECLR3CALLBACKMEMBER(int,      pfnCritSectScheduleExitEvent,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, SUPSEMEVENT hEventToSignal));
    DECLR3CALLBACKMEMBER(int,      pfnCritSectDelete,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect));
    /** @} */

    /**
     * Creates a PDM thread.
     *
     * This differs from the RTThreadCreate() API in that PDM takes care of suspending,
     * resuming, and destroying the thread as the VM state changes.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   ppThread            Where to store the thread 'handle'.
     * @param   pvUser              The user argument to the thread function.
     * @param   pfnThread           The thread function.
     * @param   pfnWakeup           The wakup callback. This is called on the EMT
     *                              thread when a state change is pending.
     * @param   cbStack             See RTThreadCreate.
     * @param   enmType             See RTThreadCreate.
     * @param   pszName             See RTThreadCreate.
     * @remarks The device critical section will NOT be entered prior to invoking
     *          the function pointers.
     */
    DECLR3CALLBACKMEMBER(int, pfnThreadCreate,(PPDMDEVINS pDevIns, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADDEV pfnThread,
                                               PFNPDMTHREADWAKEUPDEV pfnWakeup, size_t cbStack, RTTHREADTYPE enmType, const char *pszName));

    /** @name Exported PDM Thread Functions
     * @{ */
    DECLR3CALLBACKMEMBER(int, pfnThreadDestroy,(PPDMTHREAD pThread, int *pRcThread));
    DECLR3CALLBACKMEMBER(int, pfnThreadIAmSuspending,(PPDMTHREAD pThread));
    DECLR3CALLBACKMEMBER(int, pfnThreadIAmRunning,(PPDMTHREAD pThread));
    DECLR3CALLBACKMEMBER(int, pfnThreadSleep,(PPDMTHREAD pThread, RTMSINTERVAL cMillies));
    DECLR3CALLBACKMEMBER(int, pfnThreadSuspend,(PPDMTHREAD pThread));
    DECLR3CALLBACKMEMBER(int, pfnThreadResume,(PPDMTHREAD pThread));
    /** @} */

    /**
     * Set up asynchronous handling of a suspend, reset or power off notification.
     *
     * This shall only be called when getting the notification.  It must be called
     * for each one.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pfnAsyncNotify      The callback.
     * @thread  EMT(0)
     * @remarks The caller will enter the device critical section prior to invoking
     *          the callback.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetAsyncNotification, (PPDMDEVINS pDevIns, PFNPDMDEVASYNCNOTIFY pfnAsyncNotify));

    /**
     * Notify EMT(0) that the device has completed the asynchronous notification
     * handling.
     *
     * This can be called at any time, spurious calls will simply be ignored.
     *
     * @param   pDevIns             The device instance.
     * @thread  Any
     */
    DECLR3CALLBACKMEMBER(void, pfnAsyncNotificationCompleted, (PPDMDEVINS pDevIns));

    /**
     * Register the RTC device.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pRtcReg             Pointer to a RTC registration structure.
     * @param   ppRtcHlp            Where to store the pointer to the helper
     *                              functions.
     */
    DECLR3CALLBACKMEMBER(int, pfnRTCRegister,(PPDMDEVINS pDevIns, PCPDMRTCREG pRtcReg, PCPDMRTCHLP *ppRtcHlp));

    /**
     * Register a PCI Bus.
     *
     * @returns VBox status code, but the positive values 0..31 are used to indicate
     *          bus number rather than informational status codes.
     * @param   pDevIns             The device instance.
     * @param   pPciBusReg          Pointer to PCI bus registration structure.
     * @param   ppPciHlp            Where to store the pointer to the PCI Bus
     *                              helpers.
     * @param   piBus               Where to return the PDM bus number. Optional.
     */
    DECLR3CALLBACKMEMBER(int, pfnPCIBusRegister,(PPDMDEVINS pDevIns, PPDMPCIBUSREGR3 pPciBusReg,
                                                 PCPDMPCIHLPR3 *ppPciHlp, uint32_t *piBus));

    /**
     * Register the PIC device.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pPicReg             Pointer to a PIC registration structure.
     * @param   ppPicHlp            Where to store the pointer to the ring-3 PIC
     *                              helpers.
     * @sa      PDMDevHlpPICSetUpContext
     */
    DECLR3CALLBACKMEMBER(int, pfnPICRegister,(PPDMDEVINS pDevIns, PPDMPICREG pPicReg, PCPDMPICHLP *ppPicHlp));

    /**
     * Register the APIC device.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     */
    DECLR3CALLBACKMEMBER(int, pfnApicRegister,(PPDMDEVINS pDevIns));

    /**
     * Register the I/O APIC device.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pIoApicReg          Pointer to a I/O APIC registration structure.
     * @param   ppIoApicHlp         Where to store the pointer to the IOAPIC
     *                              helpers.
     */
    DECLR3CALLBACKMEMBER(int, pfnIoApicRegister,(PPDMDEVINS pDevIns, PPDMIOAPICREG pIoApicReg, PCPDMIOAPICHLP *ppIoApicHlp));

    /**
     * Register the HPET device.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pHpetReg            Pointer to a HPET registration structure.
     * @param   ppHpetHlpR3         Where to store the pointer to the HPET
     *                              helpers.
     */
    DECLR3CALLBACKMEMBER(int, pfnHpetRegister,(PPDMDEVINS pDevIns, PPDMHPETREG pHpetReg, PCPDMHPETHLPR3 *ppHpetHlpR3));

    /**
     * Register a raw PCI device.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pPciRawReg          Pointer to a raw PCI registration structure.
     * @param   ppPciRawHlpR3       Where to store the pointer to the raw PCI
     *                              device helpers.
     */
    DECLR3CALLBACKMEMBER(int, pfnPciRawRegister,(PPDMDEVINS pDevIns, PPDMPCIRAWREG pPciRawReg, PCPDMPCIRAWHLPR3 *ppPciRawHlpR3));

    /**
     * Register the DMA device.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pDmacReg            Pointer to a DMAC registration structure.
     * @param   ppDmacHlp           Where to store the pointer to the DMA helpers.
     */
    DECLR3CALLBACKMEMBER(int, pfnDMACRegister,(PPDMDEVINS pDevIns, PPDMDMACREG pDmacReg, PCPDMDMACHLP *ppDmacHlp));

    /**
     * Register transfer function for DMA channel.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   uChannel            Channel number.
     * @param   pfnTransferHandler  Device specific transfer callback function.
     * @param   pvUser              User pointer to pass to the callback.
     * @thread  EMT
     */
    DECLR3CALLBACKMEMBER(int, pfnDMARegister,(PPDMDEVINS pDevIns, unsigned uChannel, PFNDMATRANSFERHANDLER pfnTransferHandler, void *pvUser));

    /**
     * Read memory.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   uChannel            Channel number.
     * @param   pvBuffer            Pointer to target buffer.
     * @param   off                 DMA position.
     * @param   cbBlock             Block size.
     * @param   pcbRead             Where to store the number of bytes which was
     *                              read. optional.
     * @thread  EMT
     */
    DECLR3CALLBACKMEMBER(int, pfnDMAReadMemory,(PPDMDEVINS pDevIns, unsigned uChannel, void *pvBuffer, uint32_t off, uint32_t cbBlock, uint32_t *pcbRead));

    /**
     * Write memory.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   uChannel            Channel number.
     * @param   pvBuffer            Memory to write.
     * @param   off                 DMA position.
     * @param   cbBlock             Block size.
     * @param   pcbWritten          Where to store the number of bytes which was
     *                              written. optional.
     * @thread  EMT
     */
    DECLR3CALLBACKMEMBER(int, pfnDMAWriteMemory,(PPDMDEVINS pDevIns, unsigned uChannel, const void *pvBuffer, uint32_t off, uint32_t cbBlock, uint32_t *pcbWritten));

    /**
     * Set the DREQ line.
     *
     * @returns VBox status code.
     * @param pDevIns               Device instance.
     * @param uChannel              Channel number.
     * @param uLevel                Level of the line.
     * @thread  EMT
     */
    DECLR3CALLBACKMEMBER(int, pfnDMASetDREQ,(PPDMDEVINS pDevIns, unsigned uChannel, unsigned uLevel));

    /**
     * Get channel mode.
     *
     * @returns Channel mode. See specs.
     * @param   pDevIns             The device instance.
     * @param   uChannel            Channel number.
     * @thread  EMT
     */
    DECLR3CALLBACKMEMBER(uint8_t, pfnDMAGetChannelMode,(PPDMDEVINS pDevIns, unsigned uChannel));

    /**
     * Schedule DMA execution.
     *
     * @param   pDevIns             The device instance.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnDMASchedule,(PPDMDEVINS pDevIns));

    /**
     * Write CMOS value and update the checksum(s).
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   iReg                The CMOS register index.
     * @param   u8Value             The CMOS register value.
     * @thread  EMT
     */
    DECLR3CALLBACKMEMBER(int, pfnCMOSWrite,(PPDMDEVINS pDevIns, unsigned iReg, uint8_t u8Value));

    /**
     * Read CMOS value.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   iReg                The CMOS register index.
     * @param   pu8Value            Where to store the CMOS register value.
     * @thread  EMT
     */
    DECLR3CALLBACKMEMBER(int, pfnCMOSRead,(PPDMDEVINS pDevIns, unsigned iReg, uint8_t *pu8Value));

    /**
     * Assert that the current thread is the emulation thread.
     *
     * @returns True if correct.
     * @returns False if wrong.
     * @param   pDevIns             The device instance.
     * @param   pszFile             Filename of the assertion location.
     * @param   iLine               The linenumber of the assertion location.
     * @param   pszFunction         Function of the assertion location.
     */
    DECLR3CALLBACKMEMBER(bool, pfnAssertEMT,(PPDMDEVINS pDevIns, const char *pszFile, unsigned iLine, const char *pszFunction));

    /**
     * Assert that the current thread is NOT the emulation thread.
     *
     * @returns True if correct.
     * @returns False if wrong.
     * @param   pDevIns             The device instance.
     * @param   pszFile             Filename of the assertion location.
     * @param   iLine               The linenumber of the assertion location.
     * @param   pszFunction         Function of the assertion location.
     */
    DECLR3CALLBACKMEMBER(bool, pfnAssertOther,(PPDMDEVINS pDevIns, const char *pszFile, unsigned iLine, const char *pszFunction));

    /**
     * Resolves the symbol for a raw-mode context interface.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pvInterface         The interface structure.
     * @param   cbInterface         The size of the interface structure.
     * @param   pszSymPrefix        What to prefix the symbols in the list with
     *                              before resolving them.  This must start with
     *                              'dev' and contain the driver name.
     * @param   pszSymList          List of symbols corresponding to the interface.
     *                              There is generally a there is generally a define
     *                              holding this list associated with the interface
     *                              definition (INTERFACE_SYM_LIST).  For more
     *                              details see PDMR3LdrGetInterfaceSymbols.
     * @thread  EMT
     */
    DECLR3CALLBACKMEMBER(int, pfnLdrGetRCInterfaceSymbols,(PPDMDEVINS pDevIns, void *pvInterface, size_t cbInterface,
                                                           const char *pszSymPrefix, const char *pszSymList));

    /**
     * Resolves the symbol for a ring-0 context interface.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pvInterface         The interface structure.
     * @param   cbInterface         The size of the interface structure.
     * @param   pszSymPrefix        What to prefix the symbols in the list with
     *                              before resolving them.  This must start with
     *                              'dev' and contain the driver name.
     * @param   pszSymList          List of symbols corresponding to the interface.
     *                              There is generally a there is generally a define
     *                              holding this list associated with the interface
     *                              definition (INTERFACE_SYM_LIST).  For more
     *                              details see PDMR3LdrGetInterfaceSymbols.
     * @thread  EMT
     */
    DECLR3CALLBACKMEMBER(int, pfnLdrGetR0InterfaceSymbols,(PPDMDEVINS pDevIns, void *pvInterface, size_t cbInterface,
                                                           const char *pszSymPrefix, const char *pszSymList));

    /**
     * Calls the PDMDEVREGR0::pfnRequest callback (in ring-0 context).
     *
     * @returns VBox status code.
     * @retval  VERR_INVALID_FUNCTION if the callback member is NULL.
     * @retval  VERR_ACCESS_DENIED if the device isn't ring-0 capable.
     *
     * @param   pDevIns             The device instance.
     * @param   uOperation          The operation to perform.
     * @param   u64Arg              64-bit integer argument.
     * @thread  EMT
     */
    DECLR3CALLBACKMEMBER(int, pfnCallR0,(PPDMDEVINS pDevIns, uint32_t uOperation, uint64_t u64Arg));

    /**
     * Gets the reason for the most recent VM suspend.
     *
     * @returns The suspend reason. VMSUSPENDREASON_INVALID is returned if no
     *          suspend has been made or if the pDevIns is invalid.
     * @param   pDevIns             The device instance.
     */
    DECLR3CALLBACKMEMBER(VMSUSPENDREASON, pfnVMGetSuspendReason,(PPDMDEVINS pDevIns));

    /**
     * Gets the reason for the most recent VM resume.
     *
     * @returns The resume reason. VMRESUMEREASON_INVALID is returned if no
     *          resume has been made or if the pDevIns is invalid.
     * @param   pDevIns             The device instance.
     */
    DECLR3CALLBACKMEMBER(VMRESUMEREASON, pfnVMGetResumeReason,(PPDMDEVINS pDevIns));

    /**
     * Requests the mapping of multiple guest page into ring-3.
     *
     * When you're done with the pages, call pfnPhysBulkReleasePageMappingLocks()
     * ASAP to release them.
     *
     * This API will assume your intention is to write to the pages, and will
     * therefore replace shared and zero pages. If you do not intend to modify the
     * pages, use the pfnPhysBulkGCPhys2CCPtrReadOnly() API.
     *
     * @returns VBox status code.
     * @retval  VINF_SUCCESS on success.
     * @retval  VERR_PGM_PHYS_PAGE_RESERVED if any of the pages has no physical
     *          backing or if any of the pages the page has any active access
     *          handlers. The caller must fall back on using PGMR3PhysWriteExternal.
     * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if @a paGCPhysPages contains
     *          an invalid physical address.
     *
     * @param   pDevIns             The device instance.
     * @param   cPages              Number of pages to lock.
     * @param   paGCPhysPages       The guest physical address of the pages that
     *                              should be mapped (@a cPages entries).
     * @param   fFlags              Flags reserved for future use, MBZ.
     * @param   papvPages           Where to store the ring-3 mapping addresses
     *                              corresponding to @a paGCPhysPages.
     * @param   paLocks             Where to store the locking information that
     *                              pfnPhysBulkReleasePageMappingLock needs (@a cPages
     *                              in length).
     *
     * @remark  Avoid calling this API from within critical sections (other than the
     *          PGM one) because of the deadlock risk when we have to delegating the
     *          task to an EMT.
     * @thread  Any.
     * @since   6.0.6
     */
    DECLR3CALLBACKMEMBER(int, pfnPhysBulkGCPhys2CCPtr,(PPDMDEVINS pDevIns, uint32_t cPages, PCRTGCPHYS paGCPhysPages,
                                                       uint32_t fFlags, void **papvPages, PPGMPAGEMAPLOCK paLocks));

    /**
     * Requests the mapping of multiple guest page into ring-3, for reading only.
     *
     * When you're done with the pages, call pfnPhysBulkReleasePageMappingLocks()
     * ASAP to release them.
     *
     * @returns VBox status code.
     * @retval  VINF_SUCCESS on success.
     * @retval  VERR_PGM_PHYS_PAGE_RESERVED if any of the pages has no physical
     *          backing or if any of the pages the page has an active ALL access
     *          handler. The caller must fall back on using PGMR3PhysWriteExternal.
     * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if @a paGCPhysPages contains
     *          an invalid physical address.
     *
     * @param   pDevIns             The device instance.
     * @param   cPages              Number of pages to lock.
     * @param   paGCPhysPages       The guest physical address of the pages that
     *                              should be mapped (@a cPages entries).
     * @param   fFlags              Flags reserved for future use, MBZ.
     * @param   papvPages           Where to store the ring-3 mapping addresses
     *                              corresponding to @a paGCPhysPages.
     * @param   paLocks             Where to store the lock information that
     *                              pfnPhysReleasePageMappingLock needs (@a cPages
     *                              in length).
     *
     * @remark  Avoid calling this API from within critical sections.
     * @thread  Any.
     * @since   6.0.6
     */
    DECLR3CALLBACKMEMBER(int, pfnPhysBulkGCPhys2CCPtrReadOnly,(PPDMDEVINS pDevIns, uint32_t cPages, PCRTGCPHYS paGCPhysPages,
                                                               uint32_t fFlags, void const **papvPages, PPGMPAGEMAPLOCK paLocks));

    /**
     * Release the mappings of multiple guest pages.
     *
     * This is the counter part of pfnPhysBulkGCPhys2CCPtr and
     * pfnPhysBulkGCPhys2CCPtrReadOnly.
     *
     * @param   pDevIns             The device instance.
     * @param   cPages              Number of pages to unlock.
     * @param   paLocks             The lock structures initialized by the mapping
     *                              function (@a cPages in length).
     * @thread  Any.
     * @since   6.0.6
     */
    DECLR3CALLBACKMEMBER(void, pfnPhysBulkReleasePageMappingLocks,(PPDMDEVINS pDevIns, uint32_t cPages, PPGMPAGEMAPLOCK paLocks));

    /** Space reserved for future members.
     * @{ */
    DECLR3CALLBACKMEMBER(void, pfnReserved1,(void));
    DECLR3CALLBACKMEMBER(void, pfnReserved2,(void));
    DECLR3CALLBACKMEMBER(void, pfnReserved3,(void));
    DECLR3CALLBACKMEMBER(void, pfnReserved4,(void));
    DECLR3CALLBACKMEMBER(void, pfnReserved5,(void));
    DECLR3CALLBACKMEMBER(void, pfnReserved6,(void));
    DECLR3CALLBACKMEMBER(void, pfnReserved7,(void));
    DECLR3CALLBACKMEMBER(void, pfnReserved8,(void));
    DECLR3CALLBACKMEMBER(void, pfnReserved9,(void));
    DECLR3CALLBACKMEMBER(void, pfnReserved10,(void));
    /** @} */


    /** API available to trusted devices only.
     *
     * These APIs are providing unrestricted access to the guest and the VM,
     * or they are interacting intimately with PDM.
     *
     * @{
     */

    /**
     * Gets the user mode VM handle. Restricted API.
     *
     * @returns User mode VM Handle.
     * @param   pDevIns             The device instance.
     */
    DECLR3CALLBACKMEMBER(PUVM, pfnGetUVM,(PPDMDEVINS pDevIns));

    /**
     * Gets the global VM handle. Restricted API.
     *
     * @returns VM Handle.
     * @param   pDevIns             The device instance.
     */
    DECLR3CALLBACKMEMBER(PVMCC, pfnGetVM,(PPDMDEVINS pDevIns));

    /**
     * Gets the VMCPU handle. Restricted API.
     *
     * @returns VMCPU Handle.
     * @param   pDevIns             The device instance.
     */
    DECLR3CALLBACKMEMBER(PVMCPU, pfnGetVMCPU,(PPDMDEVINS pDevIns));

    /**
     * The the VM CPU ID of the current thread (restricted API).
     *
     * @returns The VMCPUID of the calling thread, NIL_VMCPUID if not EMT.
     * @param   pDevIns             The device instance.
     */
    DECLR3CALLBACKMEMBER(VMCPUID, pfnGetCurrentCpuId,(PPDMDEVINS pDevIns));

    /**
     * Registers the VMM device heap or notifies about mapping/unmapping.
     *
     * This interface serves three purposes:
     *
     *      -# Register the VMM device heap during device construction
     *         for the HM to use.
     *      -# Notify PDM/HM that it's mapped into guest address
     *         space (i.e. usable).
     *      -# Notify PDM/HM that it is being unmapped from the guest
     *         address space (i.e. not usable).
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   GCPhys              The physical address if mapped, NIL_RTGCPHYS if
     *                              not mapped.
     * @param   pvHeap              Ring 3 heap pointer.
     * @param   cbHeap              Size of the heap.
     * @thread  EMT.
     */
    DECLR3CALLBACKMEMBER(int, pfnRegisterVMMDevHeap,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTR3PTR pvHeap, unsigned cbHeap));

    /**
     * Registers the firmware (BIOS, EFI) device with PDM.
     *
     * The firmware provides a callback table and gets a special PDM helper table.
     * There can only be one firmware device for a VM.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pFwReg              Firmware registration structure.
     * @param   ppFwHlp             Where to return the firmware helper structure.
     * @remarks Only valid during device construction.
     * @thread  EMT(0)
     */
    DECLR3CALLBACKMEMBER(int, pfnFirmwareRegister,(PPDMDEVINS pDevIns, PCPDMFWREG pFwReg, PCPDMFWHLPR3 *ppFwHlp));

    /**
     * Resets the VM.
     *
     * @returns The appropriate VBox status code to pass around on reset.
     * @param   pDevIns             The device instance.
     * @param   fFlags              PDMVMRESET_F_XXX flags.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnVMReset,(PPDMDEVINS pDevIns, uint32_t fFlags));

    /**
     * Suspends the VM.
     *
     * @returns The appropriate VBox status code to pass around on suspend.
     * @param   pDevIns             The device instance.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnVMSuspend,(PPDMDEVINS pDevIns));

    /**
     * Suspends, saves and powers off the VM.
     *
     * @returns The appropriate VBox status code to pass around.
     * @param   pDevIns             The device instance.
     * @thread  An emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnVMSuspendSaveAndPowerOff,(PPDMDEVINS pDevIns));

    /**
     * Power off the VM.
     *
     * @returns The appropriate VBox status code to pass around on power off.
     * @param   pDevIns             The device instance.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnVMPowerOff,(PPDMDEVINS pDevIns));

    /**
     * Checks if the Gate A20 is enabled or not.
     *
     * @returns true if A20 is enabled.
     * @returns false if A20 is disabled.
     * @param   pDevIns             The device instance.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(bool, pfnA20IsEnabled,(PPDMDEVINS pDevIns));

    /**
     * Enables or disables the Gate A20.
     *
     * @param   pDevIns             The device instance.
     * @param   fEnable             Set this flag to enable the Gate A20; clear it
     *                              to disable.
     * @thread  The emulation thread.
     */
    DECLR3CALLBACKMEMBER(void, pfnA20Set,(PPDMDEVINS pDevIns, bool fEnable));

    /**
     * Get the specified CPUID leaf for the virtual CPU associated with the calling
     * thread.
     *
     * @param   pDevIns             The device instance.
     * @param   iLeaf               The CPUID leaf to get.
     * @param   pEax                Where to store the EAX value.
     * @param   pEbx                Where to store the EBX value.
     * @param   pEcx                Where to store the ECX value.
     * @param   pEdx                Where to store the EDX value.
     * @thread  EMT.
     */
    DECLR3CALLBACKMEMBER(void, pfnGetCpuId,(PPDMDEVINS pDevIns, uint32_t iLeaf, uint32_t *pEax, uint32_t *pEbx, uint32_t *pEcx, uint32_t *pEdx));

    /**
     * Get the current virtual clock time in a VM. The clock frequency must be
     * queried separately.
     *
     * @returns Current clock time.
     * @param   pDevIns             The device instance.
     */
    DECLR3CALLBACKMEMBER(uint64_t, pfnTMTimeVirtGet,(PPDMDEVINS pDevIns));

    /**
     * Get the frequency of the virtual clock.
     *
     * @returns The clock frequency (not variable at run-time).
     * @param   pDevIns             The device instance.
     */
    DECLR3CALLBACKMEMBER(uint64_t, pfnTMTimeVirtGetFreq,(PPDMDEVINS pDevIns));

    /**
     * Get the current virtual clock time in a VM, in nanoseconds.
     *
     * @returns Current clock time (in ns).
     * @param   pDevIns             The device instance.
     */
    DECLR3CALLBACKMEMBER(uint64_t, pfnTMTimeVirtGetNano,(PPDMDEVINS pDevIns));

    /**
     * Gets the support driver session.
     *
     * This is intended for working with the semaphore API.
     *
     * @returns Support driver session handle.
     * @param   pDevIns             The device instance.
     */
    DECLR3CALLBACKMEMBER(PSUPDRVSESSION, pfnGetSupDrvSession,(PPDMDEVINS pDevIns));

    /**
     * Queries a generic object from the VMM user.
     *
     * @returns Pointer to the object if found, NULL if not.
     * @param   pDevIns             The device instance.
     * @param   pUuid               The UUID of what's being queried.  The UUIDs and
     *                              the usage conventions are defined by the user.
     *
     * @note    It is strictly forbidden to call this internally in VBox!  This
     *          interface is exclusively for hacks in externally developed devices.
     */
    DECLR3CALLBACKMEMBER(void *, pfnQueryGenericUserObject,(PPDMDEVINS pDevIns, PCRTUUID pUuid));

    /** @} */

    /** Just a safety precaution. (PDM_DEVHLPR3_VERSION) */
    uint32_t                        u32TheEnd;
} PDMDEVHLPR3;
#endif /* !IN_RING3 || DOXYGEN_RUNNING */
/** Pointer to the R3 PDM Device API. */
typedef R3PTRTYPE(struct PDMDEVHLPR3 *) PPDMDEVHLPR3;
/** Pointer to the R3 PDM Device API, const variant. */
typedef R3PTRTYPE(const struct PDMDEVHLPR3 *) PCPDMDEVHLPR3;


/**
 * PDM Device API - RC Variant.
 */
typedef struct PDMDEVHLPRC
{
    /** Structure version. PDM_DEVHLPRC_VERSION defines the current version. */
    uint32_t                    u32Version;

    /**
     * Sets up raw-mode context callback handlers for an I/O port range.
     *
     * The range must have been registered in ring-3 first using
     * PDMDevHlpIoPortCreate() or PDMDevHlpIoPortCreateEx().
     *
     * @returns VBox status.
     * @param   pDevIns     The device instance to register the ports with.
     * @param   hIoPorts    The I/O port range handle.
     * @param   pfnOut      Pointer to function which is gonna handle OUT
     *                      operations. Optional.
     * @param   pfnIn       Pointer to function which is gonna handle IN operations.
     *                      Optional.
     * @param   pfnOutStr   Pointer to function which is gonna handle string OUT
     *                      operations.  Optional.
     * @param   pfnInStr    Pointer to function which is gonna handle string IN
     *                      operations.  Optional.
     * @param   pvUser      User argument to pass to the callbacks.
     *
     * @remarks Caller enters the device critical section prior to invoking the
     *          registered callback methods.
     *
     * @sa      PDMDevHlpIoPortCreate, PDMDevHlpIoPortCreateEx, PDMDevHlpIoPortMap,
     *          PDMDevHlpIoPortUnmap.
     */
    DECLRCCALLBACKMEMBER(int, pfnIoPortSetUpContextEx,(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts,
                                                       PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn,
                                                       PFNIOMIOPORTNEWOUTSTRING pfnOutStr, PFNIOMIOPORTNEWINSTRING pfnInStr,
                                                       void *pvUser));

    /**
     * Sets up raw-mode context callback handlers for an MMIO region.
     *
     * The region must have been registered in ring-3 first using
     * PDMDevHlpMmioCreate() or PDMDevHlpMmioCreateEx().
     *
     * @returns VBox status.
     * @param   pDevIns     The device instance to register the ports with.
     * @param   hRegion     The MMIO region handle.
     * @param   pfnWrite    Pointer to function which is gonna handle Write
     *                      operations.
     * @param   pfnRead     Pointer to function which is gonna handle Read
     *                      operations.
     * @param   pfnFill     Pointer to function which is gonna handle Fill/memset
     *                      operations. (optional)
     * @param   pvUser      User argument to pass to the callbacks.
     *
     * @remarks Caller enters the device critical section prior to invoking the
     *          registered callback methods.
     *
     * @sa      PDMDevHlpMmioCreate, PDMDevHlpMmioCreateEx, PDMDevHlpMmioMap,
     *          PDMDevHlpMmioUnmap.
     */
    DECLRCCALLBACKMEMBER(int, pfnMmioSetUpContextEx,(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, PFNIOMMMIONEWWRITE pfnWrite,
                                                     PFNIOMMMIONEWREAD pfnRead, PFNIOMMMIONEWFILL pfnFill, void *pvUser));

    /**
     * Sets up a raw-mode mapping for an MMIO2 region.
     *
     * The region must have been created in ring-3 first using
     * PDMDevHlpMmio2Create().
     *
     * @returns VBox status.
     * @param   pDevIns     The device instance to register the ports with.
     * @param   hRegion     The MMIO2 region handle.
     * @param   offSub      Start of what to map into raw-mode.  Must be page aligned.
     * @param   cbSub       Number of bytes to map into raw-mode.  Must be page
     *                      aligned.  Zero is an alias for everything.
     * @param   ppvMapping  Where to return the mapping corresponding to @a offSub.
     * @thread  EMT(0)
     * @note    Only available at VM creation time.
     *
     * @sa      PDMDevHlpMmio2Create().
     */
    DECLRCCALLBACKMEMBER(int, pfnMmio2SetUpContext,(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion,
                                                    size_t offSub, size_t cbSub, void **ppvMapping));

    /**
     * Bus master physical memory read from the given PCI device.
     *
     * @returns VINF_SUCCESS or VERR_PGM_PCI_PHYS_READ_BM_DISABLED, later maybe
     *          VERR_EM_MEMORY.  The informational status shall NOT be propagated!
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device structure.  If NULL the default
     *                              PCI device for this device instance is used.
     * @param   GCPhys              Physical address start reading from.
     * @param   pvBuf               Where to put the read bits.
     * @param   cbRead              How many bytes to read.
     * @thread  Any thread, but the call may involve the emulation thread.
     */
    DECLRCCALLBACKMEMBER(int, pfnPCIPhysRead,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys,
                                              void *pvBuf, size_t cbRead));

    /**
     * Bus master physical memory write from the given PCI device.
     *
     * @returns VINF_SUCCESS or VERR_PGM_PCI_PHYS_WRITE_BM_DISABLED, later maybe
     *          VERR_EM_MEMORY.  The informational status shall NOT be propagated!
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device structure.  If NULL the default
     *                              PCI device for this device instance is used.
     * @param   GCPhys              Physical address to write to.
     * @param   pvBuf               What to write.
     * @param   cbWrite             How many bytes to write.
     * @thread  Any thread, but the call may involve the emulation thread.
     */
    DECLRCCALLBACKMEMBER(int, pfnPCIPhysWrite,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys,
                                               const void *pvBuf, size_t cbWrite));

    /**
     * Set the IRQ for the given PCI device.
     *
     * @param   pDevIns         Device instance.
     * @param   pPciDev             The PCI device structure.  If NULL the default
     *                              PCI device for this device instance is used.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @thread  Any thread, but will involve the emulation thread.
     */
    DECLRCCALLBACKMEMBER(void, pfnPCISetIrq,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel));

    /**
     * Set ISA IRQ for a device.
     *
     * @param   pDevIns         Device instance.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @thread  Any thread, but will involve the emulation thread.
     */
    DECLRCCALLBACKMEMBER(void, pfnISASetIrq,(PPDMDEVINS pDevIns, int iIrq, int iLevel));

    /**
     * Send an MSI straight to the I/O APIC.
     *
     * @param   pDevIns         PCI device instance.
     * @param   GCPhys          Physical address MSI request was written.
     * @param   uValue          Value written.
     * @thread  Any thread, but will involve the emulation thread.
     */
    DECLRCCALLBACKMEMBER(void,  pfnIoApicSendMsi,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t uValue));

    /**
     * Read physical memory.
     *
     * @returns VINF_SUCCESS (for now).
     * @param   pDevIns         Device instance.
     * @param   GCPhys          Physical address start reading from.
     * @param   pvBuf           Where to put the read bits.
     * @param   cbRead          How many bytes to read.
     */
    DECLRCCALLBACKMEMBER(int, pfnPhysRead,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead));

    /**
     * Write to physical memory.
     *
     * @returns VINF_SUCCESS for now, and later maybe VERR_EM_MEMORY.
     * @param   pDevIns         Device instance.
     * @param   GCPhys          Physical address to write to.
     * @param   pvBuf           What to write.
     * @param   cbWrite         How many bytes to write.
     */
    DECLRCCALLBACKMEMBER(int, pfnPhysWrite,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite));

    /**
     * Checks if the Gate A20 is enabled or not.
     *
     * @returns true if A20 is enabled.
     * @returns false if A20 is disabled.
     * @param   pDevIns         Device instance.
     * @thread  The emulation thread.
     */
    DECLRCCALLBACKMEMBER(bool, pfnA20IsEnabled,(PPDMDEVINS pDevIns));

    /**
     * Gets the VM state.
     *
     * @returns VM state.
     * @param   pDevIns             The device instance.
     * @thread  Any thread (just keep in mind that it's volatile info).
     */
    DECLRCCALLBACKMEMBER(VMSTATE, pfnVMState, (PPDMDEVINS pDevIns));

    /**
     * Set the VM error message
     *
     * @returns rc.
     * @param   pDevIns         Driver instance.
     * @param   rc              VBox status code.
     * @param   SRC_POS         Use RT_SRC_POS.
     * @param   pszFormat       Error message format string.
     * @param   ...             Error message arguments.
     */
    DECLRCCALLBACKMEMBER(int, pfnVMSetError,(PPDMDEVINS pDevIns, int rc, RT_SRC_POS_DECL,
                                             const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(6, 7));

    /**
     * Set the VM error message
     *
     * @returns rc.
     * @param   pDevIns         Driver instance.
     * @param   rc              VBox status code.
     * @param   SRC_POS         Use RT_SRC_POS.
     * @param   pszFormat       Error message format string.
     * @param   va              Error message arguments.
     */
    DECLRCCALLBACKMEMBER(int, pfnVMSetErrorV,(PPDMDEVINS pDevIns, int rc, RT_SRC_POS_DECL,
                                              const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(6, 0));

    /**
     * Set the VM runtime error message
     *
     * @returns VBox status code.
     * @param   pDevIns         Device instance.
     * @param   fFlags          The action flags. See VMSETRTERR_FLAGS_*.
     * @param   pszErrorId      Error ID string.
     * @param   pszFormat       Error message format string.
     * @param   ...             Error message arguments.
     */
    DECLRCCALLBACKMEMBER(int, pfnVMSetRuntimeError,(PPDMDEVINS pDevIns, uint32_t fFlags, const char *pszErrorId,
                                                    const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(4, 5));

    /**
     * Set the VM runtime error message
     *
     * @returns VBox status code.
     * @param   pDevIns         Device instance.
     * @param   fFlags          The action flags. See VMSETRTERR_FLAGS_*.
     * @param   pszErrorId      Error ID string.
     * @param   pszFormat       Error message format string.
     * @param   va              Error message arguments.
     */
    DECLRCCALLBACKMEMBER(int, pfnVMSetRuntimeErrorV,(PPDMDEVINS pDevIns, uint32_t fFlags, const char *pszErrorId,
                                                     const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(4, 0));

    /**
     * Gets the VM handle. Restricted API.
     *
     * @returns VM Handle.
     * @param   pDevIns         Device instance.
     */
    DECLRCCALLBACKMEMBER(PVMCC, pfnGetVM,(PPDMDEVINS pDevIns));

    /**
     * Gets the VMCPU handle. Restricted API.
     *
     * @returns VMCPU Handle.
     * @param   pDevIns             The device instance.
     */
    DECLRCCALLBACKMEMBER(PVMCPUCC, pfnGetVMCPU,(PPDMDEVINS pDevIns));

    /**
     * The the VM CPU ID of the current thread (restricted API).
     *
     * @returns The VMCPUID of the calling thread, NIL_VMCPUID if not EMT.
     * @param   pDevIns             The device instance.
     */
    DECLRCCALLBACKMEMBER(VMCPUID, pfnGetCurrentCpuId,(PPDMDEVINS pDevIns));

    /**
     * Get the current virtual clock time in a VM. The clock frequency must be
     * queried separately.
     *
     * @returns Current clock time.
     * @param   pDevIns             The device instance.
     */
    DECLRCCALLBACKMEMBER(uint64_t, pfnTMTimeVirtGet,(PPDMDEVINS pDevIns));

    /**
     * Get the frequency of the virtual clock.
     *
     * @returns The clock frequency (not variable at run-time).
     * @param   pDevIns             The device instance.
     */
    DECLRCCALLBACKMEMBER(uint64_t, pfnTMTimeVirtGetFreq,(PPDMDEVINS pDevIns));

    /**
     * Get the current virtual clock time in a VM, in nanoseconds.
     *
     * @returns Current clock time (in ns).
     * @param   pDevIns             The device instance.
     */
    DECLRCCALLBACKMEMBER(uint64_t, pfnTMTimeVirtGetNano,(PPDMDEVINS pDevIns));

    /**
     * Gets the NOP critical section.
     *
     * @returns The ring-3 address of the NOP critical section.
     * @param   pDevIns             The device instance.
     */
    DECLRCCALLBACKMEMBER(PPDMCRITSECT, pfnCritSectGetNop,(PPDMDEVINS pDevIns));

    /**
     * Changes the device level critical section from the automatically created
     * default to one desired by the device constructor.
     *
     * Must first be done in ring-3.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pCritSect           The critical section to use.  NULL is not
     *                              valid, instead use the NOP critical
     *                              section.
     */
    DECLRCCALLBACKMEMBER(int, pfnSetDeviceCritSect,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect));

    /** @name Exported PDM Critical Section Functions
     * @{ */
    DECLRCCALLBACKMEMBER(int,      pfnCritSectEnter,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy));
    DECLRCCALLBACKMEMBER(int,      pfnCritSectEnterDebug,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy, RTHCUINTPTR uId, RT_SRC_POS_DECL));
    DECLRCCALLBACKMEMBER(int,      pfnCritSectTryEnter,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect));
    DECLRCCALLBACKMEMBER(int,      pfnCritSectTryEnterDebug,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, RTHCUINTPTR uId, RT_SRC_POS_DECL));
    DECLRCCALLBACKMEMBER(int,      pfnCritSectLeave,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect));
    DECLRCCALLBACKMEMBER(bool,     pfnCritSectIsOwner,(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect));
    DECLRCCALLBACKMEMBER(bool,     pfnCritSectIsInitialized,(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect));
    DECLRCCALLBACKMEMBER(bool,     pfnCritSectHasWaiters,(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect));
    DECLRCCALLBACKMEMBER(uint32_t, pfnCritSectGetRecursion,(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect));
    /** @} */

    /**
     * Gets the trace buffer handle.
     *
     * This is used by the macros found in VBox/vmm/dbgftrace.h and is not
     * really inteded for direct usage, thus no inline wrapper function.
     *
     * @returns Trace buffer handle or NIL_RTTRACEBUF.
     * @param   pDevIns             The device instance.
     */
    DECLRCCALLBACKMEMBER(RTTRACEBUF, pfnDBGFTraceBuf,(PPDMDEVINS pDevIns));

    /**
     * Sets up the PCI bus for the raw-mode context.
     *
     * This must be called after ring-3 has registered the PCI bus using
     * PDMDevHlpPCIBusRegister().
     *
     * @returns VBox status code.
     * @param   pDevIns     The device instance.
     * @param   pPciBusReg  The PCI bus registration information for raw-mode,
     *                      considered volatile.
     * @param   ppPciHlp    Where to return the raw-mode PCI bus helpers.
     */
    DECLRCCALLBACKMEMBER(int, pfnPCIBusSetUpContext,(PPDMDEVINS pDevIns, PPDMPCIBUSREGRC pPciBusReg, PCPDMPCIHLPRC *ppPciHlp));

    /**
     * Sets up the PIC for the ring-0 context.
     *
     * This must be called after ring-3 has registered the PIC using
     * PDMDevHlpPICRegister().
     *
     * @returns VBox status code.
     * @param   pDevIns     The device instance.
     * @param   pPicReg     The PIC registration information for ring-0,
     *                      considered volatile and copied.
     * @param   ppPicHlp    Where to return the ring-0 PIC helpers.
     */
    DECLRCCALLBACKMEMBER(int, pfnPICSetUpContext,(PPDMDEVINS pDevIns, PPDMPICREG pPicReg, PCPDMPICHLP *ppPicHlp));

    /**
     * Sets up the APIC for the raw-mode context.
     *
     * This must be called after ring-3 has registered the APIC using
     * PDMDevHlpApicRegister().
     *
     * @returns VBox status code.
     * @param   pDevIns     The device instance.
     */
    DECLRCCALLBACKMEMBER(int, pfnApicSetUpContext,(PPDMDEVINS pDevIns));

    /**
     * Sets up the IOAPIC for the ring-0 context.
     *
     * This must be called after ring-3 has registered the PIC using
     * PDMDevHlpIoApicRegister().
     *
     * @returns VBox status code.
     * @param   pDevIns     The device instance.
     * @param   pIoApicReg  The PIC registration information for ring-0,
     *                      considered volatile and copied.
     * @param   ppIoApicHlp Where to return the ring-0 IOAPIC helpers.
     */
    DECLRCCALLBACKMEMBER(int, pfnIoApicSetUpContext,(PPDMDEVINS pDevIns, PPDMIOAPICREG pIoApicReg, PCPDMIOAPICHLP *ppIoApicHlp));

    /**
     * Sets up the HPET for the raw-mode context.
     *
     * This must be called after ring-3 has registered the PIC using
     * PDMDevHlpHpetRegister().
     *
     * @returns VBox status code.
     * @param   pDevIns     The device instance.
     * @param   pHpetReg    The PIC registration information for raw-mode,
     *                      considered volatile and copied.
     * @param   ppHpetHlp   Where to return the raw-mode HPET helpers.
     */
    DECLRCCALLBACKMEMBER(int, pfnHpetSetUpContext,(PPDMDEVINS pDevIns, PPDMHPETREG pHpetReg, PCPDMHPETHLPRC *ppHpetHlp));

    /** Space reserved for future members.
     * @{ */
    DECLRCCALLBACKMEMBER(void, pfnReserved1,(void));
    DECLRCCALLBACKMEMBER(void, pfnReserved2,(void));
    DECLRCCALLBACKMEMBER(void, pfnReserved3,(void));
    DECLRCCALLBACKMEMBER(void, pfnReserved4,(void));
    DECLRCCALLBACKMEMBER(void, pfnReserved5,(void));
    DECLRCCALLBACKMEMBER(void, pfnReserved6,(void));
    DECLRCCALLBACKMEMBER(void, pfnReserved7,(void));
    DECLRCCALLBACKMEMBER(void, pfnReserved8,(void));
    DECLRCCALLBACKMEMBER(void, pfnReserved9,(void));
    DECLRCCALLBACKMEMBER(void, pfnReserved10,(void));
    /** @} */

    /** Just a safety precaution. */
    uint32_t                        u32TheEnd;
} PDMDEVHLPRC;
/** Pointer PDM Device RC API. */
typedef RGPTRTYPE(struct PDMDEVHLPRC *) PPDMDEVHLPRC;
/** Pointer PDM Device RC API. */
typedef RGPTRTYPE(const struct PDMDEVHLPRC *) PCPDMDEVHLPRC;

/** Current PDMDEVHLP version number. */
#define PDM_DEVHLPRC_VERSION                    PDM_VERSION_MAKE(0xffe6, 14, 0)


/**
 * PDM Device API - R0 Variant.
 */
typedef struct PDMDEVHLPR0
{
    /** Structure version. PDM_DEVHLPR0_VERSION defines the current version. */
    uint32_t                    u32Version;

    /**
     * Sets up ring-0 callback handlers for an I/O port range.
     *
     * The range must have been created in ring-3 first using
     * PDMDevHlpIoPortCreate() or PDMDevHlpIoPortCreateEx().
     *
     * @returns VBox status.
     * @param   pDevIns     The device instance to register the ports with.
     * @param   hIoPorts    The I/O port range handle.
     * @param   pfnOut      Pointer to function which is gonna handle OUT
     *                      operations. Optional.
     * @param   pfnIn       Pointer to function which is gonna handle IN operations.
     *                      Optional.
     * @param   pfnOutStr   Pointer to function which is gonna handle string OUT
     *                      operations.  Optional.
     * @param   pfnInStr    Pointer to function which is gonna handle string IN
     *                      operations.  Optional.
     * @param   pvUser      User argument to pass to the callbacks.
     *
     * @remarks Caller enters the device critical section prior to invoking the
     *          registered callback methods.
     *
     * @sa      PDMDevHlpIoPortCreate(), PDMDevHlpIoPortCreateEx(),
     *          PDMDevHlpIoPortMap(), PDMDevHlpIoPortUnmap().
     */
    DECLR0CALLBACKMEMBER(int, pfnIoPortSetUpContextEx,(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts,
                                                       PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn,
                                                       PFNIOMIOPORTNEWOUTSTRING pfnOutStr, PFNIOMIOPORTNEWINSTRING pfnInStr,
                                                       void *pvUser));

    /**
     * Sets up ring-0 callback handlers for an MMIO region.
     *
     * The region must have been created in ring-3 first using
     * PDMDevHlpMmioCreate(), PDMDevHlpMmioCreateEx(), PDMDevHlpMmioCreateAndMap(),
     * PDMDevHlpMmioCreateExAndMap() or PDMDevHlpPCIIORegionCreateMmio().
     *
     * @returns VBox status.
     * @param   pDevIns     The device instance to register the ports with.
     * @param   hRegion     The MMIO region handle.
     * @param   pfnWrite    Pointer to function which is gonna handle Write
     *                      operations.
     * @param   pfnRead     Pointer to function which is gonna handle Read
     *                      operations.
     * @param   pfnFill     Pointer to function which is gonna handle Fill/memset
     *                      operations. (optional)
     * @param   pvUser      User argument to pass to the callbacks.
     *
     * @remarks Caller enters the device critical section prior to invoking the
     *          registered callback methods.
     *
     * @sa      PDMDevHlpMmioCreate(), PDMDevHlpMmioCreateEx(), PDMDevHlpMmioMap(),
     *          PDMDevHlpMmioUnmap().
     */
    DECLR0CALLBACKMEMBER(int, pfnMmioSetUpContextEx,(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, PFNIOMMMIONEWWRITE pfnWrite,
                                                     PFNIOMMMIONEWREAD pfnRead, PFNIOMMMIONEWFILL pfnFill, void *pvUser));

    /**
     * Sets up a ring-0 mapping for an MMIO2 region.
     *
     * The region must have been created in ring-3 first using
     * PDMDevHlpMmio2Create().
     *
     * @returns VBox status.
     * @param   pDevIns     The device instance to register the ports with.
     * @param   hRegion     The MMIO2 region handle.
     * @param   offSub      Start of what to map into ring-0.  Must be page aligned.
     * @param   cbSub       Number of bytes to map into ring-0.  Must be page
     *                      aligned.  Zero is an alias for everything.
     * @param   ppvMapping  Where to return the mapping corresponding to @a offSub.
     *
     * @thread  EMT(0)
     * @note    Only available at VM creation time.
     *
     * @sa      PDMDevHlpMmio2Create().
     */
    DECLR0CALLBACKMEMBER(int, pfnMmio2SetUpContext,(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion, size_t offSub, size_t cbSub,
                                                    void **ppvMapping));

    /**
     * Bus master physical memory read from the given PCI device.
     *
     * @returns VINF_SUCCESS or VERR_PDM_NOT_PCI_BUS_MASTER, later maybe
     *          VERR_EM_MEMORY.
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device structure.  If NULL the default
     *                              PCI device for this device instance is used.
     * @param   GCPhys              Physical address start reading from.
     * @param   pvBuf               Where to put the read bits.
     * @param   cbRead              How many bytes to read.
     * @thread  Any thread, but the call may involve the emulation thread.
     */
    DECLR0CALLBACKMEMBER(int, pfnPCIPhysRead,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys,
                                              void *pvBuf, size_t cbRead));

    /**
     * Bus master physical memory write from the given PCI device.
     *
     * @returns VINF_SUCCESS or VERR_PDM_NOT_PCI_BUS_MASTER, later maybe
     *          VERR_EM_MEMORY.
     * @param   pDevIns             The device instance.
     * @param   pPciDev             The PCI device structure.  If NULL the default
     *                              PCI device for this device instance is used.
     * @param   GCPhys              Physical address to write to.
     * @param   pvBuf               What to write.
     * @param   cbWrite             How many bytes to write.
     * @thread  Any thread, but the call may involve the emulation thread.
     */
    DECLR0CALLBACKMEMBER(int, pfnPCIPhysWrite,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys,
                                               const void *pvBuf, size_t cbWrite));

    /**
     * Set the IRQ for the given PCI device.
     *
     * @param   pDevIns             Device instance.
     * @param   pPciDev             The PCI device structure.  If NULL the default
     *                              PCI device for this device instance is used.
     * @param   iIrq                IRQ number to set.
     * @param   iLevel              IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @thread  Any thread, but will involve the emulation thread.
     */
    DECLR0CALLBACKMEMBER(void, pfnPCISetIrq,(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel));

    /**
     * Set ISA IRQ for a device.
     *
     * @param   pDevIns         Device instance.
     * @param   iIrq            IRQ number to set.
     * @param   iLevel          IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
     * @thread  Any thread, but will involve the emulation thread.
     */
    DECLR0CALLBACKMEMBER(void, pfnISASetIrq,(PPDMDEVINS pDevIns, int iIrq, int iLevel));

    /**
     * Send an MSI straight to the I/O APIC.
     *
     * @param   pDevIns         PCI device instance.
     * @param   GCPhys          Physical address MSI request was written.
     * @param   uValue          Value written.
     * @thread  Any thread, but will involve the emulation thread.
     */
    DECLR0CALLBACKMEMBER(void,  pfnIoApicSendMsi,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t uValue));

    /**
     * Read physical memory.
     *
     * @returns VINF_SUCCESS (for now).
     * @param   pDevIns         Device instance.
     * @param   GCPhys          Physical address start reading from.
     * @param   pvBuf           Where to put the read bits.
     * @param   cbRead          How many bytes to read.
     */
    DECLR0CALLBACKMEMBER(int, pfnPhysRead,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead));

    /**
     * Write to physical memory.
     *
     * @returns VINF_SUCCESS for now, and later maybe VERR_EM_MEMORY.
     * @param   pDevIns         Device instance.
     * @param   GCPhys          Physical address to write to.
     * @param   pvBuf           What to write.
     * @param   cbWrite         How many bytes to write.
     */
    DECLR0CALLBACKMEMBER(int, pfnPhysWrite,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite));

    /**
     * Checks if the Gate A20 is enabled or not.
     *
     * @returns true if A20 is enabled.
     * @returns false if A20 is disabled.
     * @param   pDevIns         Device instance.
     * @thread  The emulation thread.
     */
    DECLR0CALLBACKMEMBER(bool, pfnA20IsEnabled,(PPDMDEVINS pDevIns));

    /**
     * Gets the VM state.
     *
     * @returns VM state.
     * @param   pDevIns             The device instance.
     * @thread  Any thread (just keep in mind that it's volatile info).
     */
    DECLR0CALLBACKMEMBER(VMSTATE, pfnVMState, (PPDMDEVINS pDevIns));

    /**
     * Set the VM error message
     *
     * @returns rc.
     * @param   pDevIns         Driver instance.
     * @param   rc              VBox status code.
     * @param   SRC_POS         Use RT_SRC_POS.
     * @param   pszFormat       Error message format string.
     * @param   ...             Error message arguments.
     */
    DECLR0CALLBACKMEMBER(int, pfnVMSetError,(PPDMDEVINS pDevIns, int rc, RT_SRC_POS_DECL,
                                             const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(6, 7));

    /**
     * Set the VM error message
     *
     * @returns rc.
     * @param   pDevIns         Driver instance.
     * @param   rc              VBox status code.
     * @param   SRC_POS         Use RT_SRC_POS.
     * @param   pszFormat       Error message format string.
     * @param   va              Error message arguments.
     */
    DECLR0CALLBACKMEMBER(int, pfnVMSetErrorV,(PPDMDEVINS pDevIns, int rc, RT_SRC_POS_DECL,
                                              const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(6, 0));

    /**
     * Set the VM runtime error message
     *
     * @returns VBox status code.
     * @param   pDevIns         Device instance.
     * @param   fFlags          The action flags. See VMSETRTERR_FLAGS_*.
     * @param   pszErrorId      Error ID string.
     * @param   pszFormat       Error message format string.
     * @param   ...             Error message arguments.
     */
    DECLR0CALLBACKMEMBER(int, pfnVMSetRuntimeError,(PPDMDEVINS pDevIns, uint32_t fFlags, const char *pszErrorId,
                                                    const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(4, 5));

    /**
     * Set the VM runtime error message
     *
     * @returns VBox status code.
     * @param   pDevIns         Device instance.
     * @param   fFlags          The action flags. See VMSETRTERR_FLAGS_*.
     * @param   pszErrorId      Error ID string.
     * @param   pszFormat       Error message format string.
     * @param   va              Error message arguments.
     */
    DECLR0CALLBACKMEMBER(int, pfnVMSetRuntimeErrorV,(PPDMDEVINS pDevIns, uint32_t fFlags, const char *pszErrorId,
                                                     const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(4, 0));

    /**
     * Gets the VM handle. Restricted API.
     *
     * @returns VM Handle.
     * @param   pDevIns         Device instance.
     */
    DECLR0CALLBACKMEMBER(PVMCC, pfnGetVM,(PPDMDEVINS pDevIns));

    /**
     * Gets the VMCPU handle. Restricted API.
     *
     * @returns VMCPU Handle.
     * @param   pDevIns             The device instance.
     */
    DECLR0CALLBACKMEMBER(PVMCPUCC, pfnGetVMCPU,(PPDMDEVINS pDevIns));

    /**
     * The the VM CPU ID of the current thread (restricted API).
     *
     * @returns The VMCPUID of the calling thread, NIL_VMCPUID if not EMT.
     * @param   pDevIns             The device instance.
     */
    DECLR0CALLBACKMEMBER(VMCPUID, pfnGetCurrentCpuId,(PPDMDEVINS pDevIns));

    /**
     * Translates a timer handle to a pointer.
     *
     * @returns The time address.
     * @param   pDevIns             The device instance.
     * @param   hTimer              The timer handle.
     */
    DECLR0CALLBACKMEMBER(PTMTIMERR0, pfnTimerToPtr,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));

    /** @name Timer handle method wrappers
     * @{ */
    DECLR0CALLBACKMEMBER(uint64_t, pfnTimerFromMicro,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMicroSecs));
    DECLR0CALLBACKMEMBER(uint64_t, pfnTimerFromMilli,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMilliSecs));
    DECLR0CALLBACKMEMBER(uint64_t, pfnTimerFromNano,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cNanoSecs));
    DECLR0CALLBACKMEMBER(uint64_t, pfnTimerGet,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    DECLR0CALLBACKMEMBER(uint64_t, pfnTimerGetFreq,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    DECLR0CALLBACKMEMBER(uint64_t, pfnTimerGetNano,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    DECLR0CALLBACKMEMBER(bool,     pfnTimerIsActive,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    DECLR0CALLBACKMEMBER(bool,     pfnTimerIsLockOwner,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    DECLR0CALLBACKMEMBER(VBOXSTRICTRC, pfnTimerLockClock,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, int rcBusy));
    /** Takes the clock lock then enters the specified critical section. */
    DECLR0CALLBACKMEMBER(VBOXSTRICTRC, pfnTimerLockClock2,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect, int rcBusy));
    DECLR0CALLBACKMEMBER(int,      pfnTimerSet,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t uExpire));
    DECLR0CALLBACKMEMBER(int,      pfnTimerSetFrequencyHint,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint32_t uHz));
    DECLR0CALLBACKMEMBER(int,      pfnTimerSetMicro,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMicrosToNext));
    DECLR0CALLBACKMEMBER(int,      pfnTimerSetMillies,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMilliesToNext));
    DECLR0CALLBACKMEMBER(int,      pfnTimerSetNano,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cNanosToNext));
    DECLR0CALLBACKMEMBER(int,      pfnTimerSetRelative,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cTicksToNext, uint64_t *pu64Now));
    DECLR0CALLBACKMEMBER(int,      pfnTimerStop,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    DECLR0CALLBACKMEMBER(void,     pfnTimerUnlockClock,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer));
    DECLR0CALLBACKMEMBER(void,     pfnTimerUnlockClock2,(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect));
    /** @} */

    /**
     * Get the current virtual clock time in a VM. The clock frequency must be
     * queried separately.
     *
     * @returns Current clock time.
     * @param   pDevIns             The device instance.
     */
    DECLR0CALLBACKMEMBER(uint64_t, pfnTMTimeVirtGet,(PPDMDEVINS pDevIns));

    /**
     * Get the frequency of the virtual clock.
     *
     * @returns The clock frequency (not variable at run-time).
     * @param   pDevIns             The device instance.
     */
    DECLR0CALLBACKMEMBER(uint64_t, pfnTMTimeVirtGetFreq,(PPDMDEVINS pDevIns));

    /**
     * Get the current virtual clock time in a VM, in nanoseconds.
     *
     * @returns Current clock time (in ns).
     * @param   pDevIns             The device instance.
     */
    DECLR0CALLBACKMEMBER(uint64_t, pfnTMTimeVirtGetNano,(PPDMDEVINS pDevIns));

    /** @name Exported PDM Queue Functions
     * @{ */
    DECLR0CALLBACKMEMBER(PPDMQUEUE, pfnQueueToPtr,(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue));
    DECLR0CALLBACKMEMBER(PPDMQUEUEITEMCORE, pfnQueueAlloc,(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue));
    DECLR0CALLBACKMEMBER(void, pfnQueueInsert,(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue, PPDMQUEUEITEMCORE pItem));
    DECLR0CALLBACKMEMBER(void, pfnQueueInsertEx,(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue, PPDMQUEUEITEMCORE pItem, uint64_t cNanoMaxDelay));
    DECLR0CALLBACKMEMBER(bool, pfnQueueFlushIfNecessary,(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue));
    /** @} */

    /** @name PDM Task
     * @{ */
    /**
     * Triggers the running the given task.
     *
     * @returns VBox status code.
     * @retval  VINF_ALREADY_POSTED is the task is already pending.
     * @param   pDevIns         The device instance.
     * @param   hTask           The task to trigger.
     * @thread  Any thread.
     */
    DECLR0CALLBACKMEMBER(int, pfnTaskTrigger,(PPDMDEVINS pDevIns, PDMTASKHANDLE hTask));
    /** @} */

    /** @name SUP Event Semaphore Wrappers (single release / auto reset)
     * These semaphores can be signalled from ring-0.
     * @{ */
    /** @sa SUPSemEventSignal */
    DECLR0CALLBACKMEMBER(int, pfnSUPSemEventSignal,(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent));
    /** @sa SUPSemEventWaitNoResume */
    DECLR0CALLBACKMEMBER(int, pfnSUPSemEventWaitNoResume,(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint32_t cMillies));
    /** @sa SUPSemEventWaitNsAbsIntr */
    DECLR0CALLBACKMEMBER(int, pfnSUPSemEventWaitNsAbsIntr,(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint64_t uNsTimeout));
    /** @sa SUPSemEventWaitNsRelIntr */
    DECLR0CALLBACKMEMBER(int, pfnSUPSemEventWaitNsRelIntr,(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint64_t cNsTimeout));
    /** @sa SUPSemEventGetResolution */
    DECLR0CALLBACKMEMBER(uint32_t, pfnSUPSemEventGetResolution,(PPDMDEVINS pDevIns));
    /** @} */

    /** @name SUP Multi Event Semaphore Wrappers (multiple release / manual reset)
     * These semaphores can be signalled from ring-0.
     * @{ */
    /** @sa SUPSemEventMultiSignal */
    DECLR0CALLBACKMEMBER(int, pfnSUPSemEventMultiSignal,(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti));
    /** @sa SUPSemEventMultiReset */
    DECLR0CALLBACKMEMBER(int, pfnSUPSemEventMultiReset,(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti));
    /** @sa SUPSemEventMultiWaitNoResume */
    DECLR0CALLBACKMEMBER(int, pfnSUPSemEventMultiWaitNoResume,(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti, uint32_t cMillies));
    /** @sa SUPSemEventMultiWaitNsAbsIntr */
    DECLR0CALLBACKMEMBER(int, pfnSUPSemEventMultiWaitNsAbsIntr,(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti, uint64_t uNsTimeout));
    /** @sa SUPSemEventMultiWaitNsRelIntr */
    DECLR0CALLBACKMEMBER(int, pfnSUPSemEventMultiWaitNsRelIntr,(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti, uint64_t cNsTimeout));
    /** @sa SUPSemEventMultiGetResolution */
    DECLR0CALLBACKMEMBER(uint32_t, pfnSUPSemEventMultiGetResolution,(PPDMDEVINS pDevIns));
    /** @} */

    /**
     * Gets the NOP critical section.
     *
     * @returns The ring-3 address of the NOP critical section.
     * @param   pDevIns             The device instance.
     */
    DECLR0CALLBACKMEMBER(PPDMCRITSECT, pfnCritSectGetNop,(PPDMDEVINS pDevIns));

    /**
     * Changes the device level critical section from the automatically created
     * default to one desired by the device constructor.
     *
     * Must first be done in ring-3.
     *
     * @returns VBox status code.
     * @param   pDevIns             The device instance.
     * @param   pCritSect           The critical section to use.  NULL is not
     *                              valid, instead use the NOP critical
     *                              section.
     */
    DECLR0CALLBACKMEMBER(int, pfnSetDeviceCritSect,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect));

    /** @name Exported PDM Critical Section Functions
     * @{ */
    DECLR0CALLBACKMEMBER(int,      pfnCritSectEnter,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy));
    DECLR0CALLBACKMEMBER(int,      pfnCritSectEnterDebug,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy, RTHCUINTPTR uId, RT_SRC_POS_DECL));
    DECLR0CALLBACKMEMBER(int,      pfnCritSectTryEnter,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect));
    DECLR0CALLBACKMEMBER(int,      pfnCritSectTryEnterDebug,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, RTHCUINTPTR uId, RT_SRC_POS_DECL));
    DECLR0CALLBACKMEMBER(int,      pfnCritSectLeave,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect));
    DECLR0CALLBACKMEMBER(bool,     pfnCritSectIsOwner,(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect));
    DECLR0CALLBACKMEMBER(bool,     pfnCritSectIsInitialized,(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect));
    DECLR0CALLBACKMEMBER(bool,     pfnCritSectHasWaiters,(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect));
    DECLR0CALLBACKMEMBER(uint32_t, pfnCritSectGetRecursion,(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect));
    DECLR0CALLBACKMEMBER(int,      pfnCritSectScheduleExitEvent,(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, SUPSEMEVENT hEventToSignal));
    /** @} */

    /**
     * Gets the trace buffer handle.
     *
     * This is used by the macros found in VBox/vmm/dbgftrace.h and is not
     * really inteded for direct usage, thus no inline wrapper function.
     *
     * @returns Trace buffer handle or NIL_RTTRACEBUF.
     * @param   pDevIns             The device instance.
     */
    DECLR0CALLBACKMEMBER(RTTRACEBUF, pfnDBGFTraceBuf,(PPDMDEVINS pDevIns));

    /**
     * Sets up the PCI bus for the ring-0 context.
     *
     * This must be called after ring-3 has registered the PCI bus using
     * PDMDevHlpPCIBusRegister().
     *
     * @returns VBox status code.
     * @param   pDevIns     The device instance.
     * @param   pPciBusReg  The PCI bus registration information for ring-0,
     *                      considered volatile and copied.
     * @param   ppPciHlp    Where to return the ring-0 PCI bus helpers.
     */
    DECLR0CALLBACKMEMBER(int, pfnPCIBusSetUpContext,(PPDMDEVINS pDevIns, PPDMPCIBUSREGR0 pPciBusReg, PCPDMPCIHLPR0 *ppPciHlp));

    /**
     * Sets up the PIC for the ring-0 context.
     *
     * This must be called after ring-3 has registered the PIC using
     * PDMDevHlpPICRegister().
     *
     * @returns VBox status code.
     * @param   pDevIns     The device instance.
     * @param   pPicReg     The PIC registration information for ring-0,
     *                      considered volatile and copied.
     * @param   ppPicHlp    Where to return the ring-0 PIC helpers.
     */
    DECLR0CALLBACKMEMBER(int, pfnPICSetUpContext,(PPDMDEVINS pDevIns, PPDMPICREG pPicReg, PCPDMPICHLP *ppPicHlp));

    /**
     * Sets up the APIC for the ring-0 context.
     *
     * This must be called after ring-3 has registered the APIC using
     * PDMDevHlpApicRegister().
     *
     * @returns VBox status code.
     * @param   pDevIns     The device instance.
     */
    DECLR0CALLBACKMEMBER(int, pfnApicSetUpContext,(PPDMDEVINS pDevIns));

    /**
     * Sets up the IOAPIC for the ring-0 context.
     *
     * This must be called after ring-3 has registered the PIC using
     * PDMDevHlpIoApicRegister().
     *
     * @returns VBox status code.
     * @param   pDevIns     The device instance.
     * @param   pIoApicReg  The PIC registration information for ring-0,
     *                      considered volatile and copied.
     * @param   ppIoApicHlp Where to return the ring-0 IOAPIC helpers.
     */
    DECLR0CALLBACKMEMBER(int, pfnIoApicSetUpContext,(PPDMDEVINS pDevIns, PPDMIOAPICREG pIoApicReg, PCPDMIOAPICHLP *ppIoApicHlp));

    /**
     * Sets up the HPET for the ring-0 context.
     *
     * This must be called after ring-3 has registered the PIC using
     * PDMDevHlpHpetRegister().
     *
     * @returns VBox status code.
     * @param   pDevIns     The device instance.
     * @param   pHpetReg    The PIC registration information for ring-0,
     *                      considered volatile and copied.
     * @param   ppHpetHlp   Where to return the ring-0 HPET helpers.
     */
    DECLR0CALLBACKMEMBER(int, pfnHpetSetUpContext,(PPDMDEVINS pDevIns, PPDMHPETREG pHpetReg, PCPDMHPETHLPR0 *ppHpetHlp));

    /** Space reserved for future members.
     * @{ */
    DECLR0CALLBACKMEMBER(void, pfnReserved1,(void));
    DECLR0CALLBACKMEMBER(void, pfnReserved2,(void));
    DECLR0CALLBACKMEMBER(void, pfnReserved3,(void));
    DECLR0CALLBACKMEMBER(void, pfnReserved4,(void));
    DECLR0CALLBACKMEMBER(void, pfnReserved5,(void));
    DECLR0CALLBACKMEMBER(void, pfnReserved6,(void));
    DECLR0CALLBACKMEMBER(void, pfnReserved7,(void));
    DECLR0CALLBACKMEMBER(void, pfnReserved8,(void));
    DECLR0CALLBACKMEMBER(void, pfnReserved9,(void));
    DECLR0CALLBACKMEMBER(void, pfnReserved10,(void));
    /** @} */

    /** Just a safety precaution. */
    uint32_t                        u32TheEnd;
} PDMDEVHLPR0;
/** Pointer PDM Device R0 API. */
typedef R0PTRTYPE(struct PDMDEVHLPR0 *) PPDMDEVHLPR0;
/** Pointer PDM Device GC API. */
typedef R0PTRTYPE(const struct PDMDEVHLPR0 *) PCPDMDEVHLPR0;

/** Current PDMDEVHLP version number. */
#define PDM_DEVHLPR0_VERSION                    PDM_VERSION_MAKE(0xffe5, 16, 0)


/**
 * PDM Device Instance.
 */
typedef struct PDMDEVINSR3
{
    /** Structure version. PDM_DEVINSR3_VERSION defines the current version. */
    uint32_t                        u32Version;
    /** Device instance number. */
    uint32_t                        iInstance;
    /** Size of the ring-3, raw-mode and shared bits. */
    uint32_t                        cbRing3;
    /** Set if ring-0 context is enabled. */
    bool                            fR0Enabled;
    /** Set if raw-mode context is enabled. */
    bool                            fRCEnabled;
    /** Alignment padding. */
    bool                            afReserved[2];
    /** Pointer the HC PDM Device API. */
    PCPDMDEVHLPR3                   pHlpR3;
    /** Pointer to the shared device instance data. */
    RTR3PTR                         pvInstanceDataR3;
    /** Pointer to the device instance data for ring-3. */
    RTR3PTR                         pvInstanceDataForR3;
    /** The critical section for the device.
     *
     * TM and IOM will enter this critical section before calling into the device
     * code.  PDM will when doing power on, power off, reset, suspend and resume
     * notifications.  SSM will currently not, but this will be changed later on.
     *
     * The device gets a critical section automatically assigned to it before
     * the constructor is called.  If the constructor wishes to use a different
     * critical section, it calls PDMDevHlpSetDeviceCritSect() to change it
     * very early on.
     */
    R3PTRTYPE(PPDMCRITSECT)         pCritSectRoR3;
    /** Pointer to device registration structure.  */
    R3PTRTYPE(PCPDMDEVREG)          pReg;
    /** Configuration handle. */
    R3PTRTYPE(PCFGMNODE)            pCfg;
    /** The base interface of the device.
     *
     * The device constructor initializes this if it has any
     * device level interfaces to export. To obtain this interface
     * call PDMR3QueryDevice(). */
    PDMIBASE                        IBase;

    /** Tracing indicator. */
    uint32_t                        fTracing;
    /** The tracing ID of this device.  */
    uint32_t                        idTracing;

    /** Ring-3 pointer to the raw-mode device instance. */
    R3PTRTYPE(struct PDMDEVINSRC *) pDevInsForRCR3;
    /** Raw-mode address of the raw-mode device instance. */
    RTRGPTR                         pDevInsForRC;
    /** Ring-3 pointer to the raw-mode instance data. */
    RTR3PTR                         pvInstanceDataForRCR3;

    /** PCI device structure size. */
    uint32_t                        cbPciDev;
    /** Number of PCI devices in apPciDevs. */
    uint32_t                        cPciDevs;
    /** Pointer to the PCI devices for this device.
     * (Allocated after the shared instance data.)
     * @note If we want to extend this beyond 8 sub-functions/devices, those 1 or
     *       two devices ever needing it can use cbPciDev and do the address
     *       calculations that for entries 8+. */
    R3PTRTYPE(struct PDMPCIDEV *)   apPciDevs[8];

    /** Temporarily. */
    R0PTRTYPE(struct PDMDEVINSR0 *) pDevInsR0RemoveMe;
    /** Temporarily. */
    RTR0PTR                         pvInstanceDataR0;
    /** Temporarily. */
    RTRCPTR                         pvInstanceDataRC;
    /** Align the internal data more naturally. */
    uint32_t                        au32Padding[HC_ARCH_BITS == 32 ? 13 : 11];

    /** Internal data. */
    union
    {
#ifdef PDMDEVINSINT_DECLARED
        PDMDEVINSINTR3              s;
#endif
        uint8_t                     padding[HC_ARCH_BITS == 32 ? 0x30 : 0x50];
    } Internal;

    /** Device instance data for ring-3.  The size of this area is defined
     * in the PDMDEVREG::cbInstanceR3 field. */
    char                            achInstanceData[8];
} PDMDEVINSR3;

/** Current PDMDEVINSR3 version number. */
#define PDM_DEVINSR3_VERSION        PDM_VERSION_MAKE(0xff82, 4, 0)

/** Converts a pointer to the PDMDEVINSR3::IBase to a pointer to PDMDEVINS. */
#define PDMIBASE_2_PDMDEV(pInterface) ( (PPDMDEVINS)((char *)(pInterface) - RT_UOFFSETOF(PDMDEVINS, IBase)) )


/**
 * PDM ring-0 device instance.
 */
typedef struct PDMDEVINSR0
{
    /** Structure version. PDM_DEVINSR0_VERSION defines the current version. */
    uint32_t                        u32Version;
    /** Device instance number. */
    uint32_t                        iInstance;

    /** Pointer the HC PDM Device API. */
    PCPDMDEVHLPR0                   pHlpR0;
    /** Pointer to the shared device instance data. */
    RTR0PTR                         pvInstanceDataR0;
    /** Pointer to the device instance data for ring-0. */
    RTR0PTR                         pvInstanceDataForR0;
    /** The critical section for the device.
     *
     * TM and IOM will enter this critical section before calling into the device
     * code.  PDM will when doing power on, power off, reset, suspend and resume
     * notifications.  SSM will currently not, but this will be changed later on.
     *
     * The device gets a critical section automatically assigned to it before
     * the constructor is called.  If the constructor wishes to use a different
     * critical section, it calls PDMDevHlpSetDeviceCritSect() to change it
     * very early on.
     */
    R0PTRTYPE(PPDMCRITSECT)         pCritSectRoR0;
    /** Pointer to the ring-0 device registration structure.  */
    R0PTRTYPE(PCPDMDEVREGR0)        pReg;
    /** Ring-3 address of the ring-3 device instance. */
    R3PTRTYPE(struct PDMDEVINSR3 *) pDevInsForR3;
    /** Ring-0 pointer to the ring-3 device instance. */
    R0PTRTYPE(struct PDMDEVINSR3 *) pDevInsForR3R0;
    /** Ring-0 pointer to the ring-3 instance data. */
    RTR0PTR                         pvInstanceDataForR3R0;
    /** Raw-mode address of the raw-mode device instance. */
    RGPTRTYPE(struct PDMDEVINSRC *) pDevInsForRC;
    /** Ring-0 pointer to the raw-mode device instance. */
    R0PTRTYPE(struct PDMDEVINSRC *) pDevInsForRCR0;
    /** Ring-0 pointer to the raw-mode instance data. */
    RTR0PTR                         pvInstanceDataForRCR0;

    /** PCI device structure size. */
    uint32_t                        cbPciDev;
    /** Number of PCI devices in apPciDevs. */
    uint32_t                        cPciDevs;
    /** Pointer to the PCI devices for this device.
     * (Allocated after the shared instance data.)
     * @note If we want to extend this beyond 8 sub-functions/devices, those 1 or
     *       two devices ever needing it can use cbPciDev and do the address
     *       calculations that for entries 8+. */
    R0PTRTYPE(struct PDMPCIDEV *)   apPciDevs[8];

    /** Align the internal data more naturally. */
    uint32_t                        au32Padding[HC_ARCH_BITS == 32 ? 3 : 2 + 4];

    /** Internal data. */
    union
    {
#ifdef PDMDEVINSINT_DECLARED
        PDMDEVINSINTR0              s;
#endif
        uint8_t                     padding[HC_ARCH_BITS == 32 ? 0x20 : 0x40];
    } Internal;

    /** Device instance data for ring-0. The size of this area is defined
     * in the PDMDEVREG::cbInstanceR0 field. */
    char                            achInstanceData[8];
} PDMDEVINSR0;

/** Current PDMDEVINSR0 version number. */
#define PDM_DEVINSR0_VERSION        PDM_VERSION_MAKE(0xff83, 4, 0)


/**
 * PDM raw-mode device instance.
 */
typedef struct PDMDEVINSRC
{
    /** Structure version. PDM_DEVINSRC_VERSION defines the current version. */
    uint32_t                        u32Version;
    /** Device instance number. */
    uint32_t                        iInstance;

    /** Pointer the HC PDM Device API. */
    PCPDMDEVHLPRC                   pHlpRC;
    /** Pointer to the shared device instance data. */
    RTRGPTR                         pvInstanceDataRC;
    /** Pointer to the device instance data for raw-mode. */
    RTRGPTR                         pvInstanceDataForRC;
    /** The critical section for the device.
     *
     * TM and IOM will enter this critical section before calling into the device
     * code.  PDM will when doing power on, power off, reset, suspend and resume
     * notifications.  SSM will currently not, but this will be changed later on.
     *
     * The device gets a critical section automatically assigned to it before
     * the constructor is called.  If the constructor wishes to use a different
     * critical section, it calls PDMDevHlpSetDeviceCritSect() to change it
     * very early on.
     */
    RGPTRTYPE(PPDMCRITSECT)         pCritSectRoRC;
    /** Pointer to the raw-mode device registration structure.  */
    RGPTRTYPE(PCPDMDEVREGRC)        pReg;

    /** PCI device structure size. */
    uint32_t                        cbPciDev;
    /** Number of PCI devices in apPciDevs. */
    uint32_t                        cPciDevs;
    /** Pointer to the PCI devices for this device.
     * (Allocated after the shared instance data.)  */
    RGPTRTYPE(struct PDMPCIDEV *)   apPciDevs[8];

    /** Align the internal data more naturally. */
    uint32_t                        au32Padding[14];

    /** Internal data. */
    union
    {
#ifdef PDMDEVINSINT_DECLARED
        PDMDEVINSINTRC              s;
#endif
        uint8_t                     padding[0x10];
    } Internal;

    /** Device instance data for ring-0. The size of this area is defined
     * in the PDMDEVREG::cbInstanceR0 field. */
    char                            achInstanceData[8];
} PDMDEVINSRC;

/** Current PDMDEVINSR0 version number. */
#define PDM_DEVINSRC_VERSION        PDM_VERSION_MAKE(0xff84, 4, 0)


/** @def PDM_DEVINS_VERSION
 * Current PDMDEVINS version number. */
/** @typedef PDMDEVINS
 * The device instance structure for the current context. */
#ifdef IN_RING3
# define PDM_DEVINS_VERSION         PDM_DEVINSR3_VERSION
typedef PDMDEVINSR3                 PDMDEVINS;
#elif defined(IN_RING0)
# define PDM_DEVINS_VERSION         PDM_DEVINSR0_VERSION
typedef PDMDEVINSR0                 PDMDEVINS;
#elif defined(IN_RC)
# define PDM_DEVINS_VERSION         PDM_DEVINSRC_VERSION
typedef PDMDEVINSRC                 PDMDEVINS;
#else
# error "Missing context defines: IN_RING0, IN_RING3, IN_RC"
#endif

/**
 * Get the pointer to an PCI device.
 * @note Returns NULL if @a a_idxPciDev is out of bounds.
 */
#define PDMDEV_GET_PPCIDEV(a_pDevIns, a_idxPciDev) \
    (  (uintptr_t)(a_idxPciDev) < RT_ELEMENTS((a_pDevIns)->apPciDevs) ? (a_pDevIns)->apPciDevs[(uintptr_t)(a_idxPciDev)] \
     : PDMDEV_CALC_PPCIDEV(a_pDevIns, a_idxPciDev) )

/**
 * Calc the pointer to of a given PCI device.
 * @note Returns NULL if @a a_idxPciDev is out of bounds.
 */
#define PDMDEV_CALC_PPCIDEV(a_pDevIns, a_idxPciDev) \
    (  (uintptr_t)(a_idxPciDev) < (a_pDevIns)->cPciDevs \
     ? (PPDMPCIDEV)((uint8_t *)((a_pDevIns)->apPciDevs[0]) + (a_pDevIns->cbPciDev) * (uintptr_t)(a_idxPciDev)) \
     : (PPDMPCIDEV)NULL )


/**
 * Checks the structure versions of the device instance and device helpers,
 * returning if they are incompatible.
 *
 * This is for use in the constructor.
 *
 * @param   pDevIns     The device instance pointer.
 */
#define PDMDEV_CHECK_VERSIONS_RETURN(pDevIns) \
    do \
    { \
        PPDMDEVINS pDevInsTypeCheck = (pDevIns); NOREF(pDevInsTypeCheck); \
        AssertLogRelMsgReturn(PDM_VERSION_ARE_COMPATIBLE((pDevIns)->u32Version, PDM_DEVINS_VERSION), \
                              ("DevIns=%#x  mine=%#x\n", (pDevIns)->u32Version, PDM_DEVINS_VERSION), \
                              VERR_PDM_DEVINS_VERSION_MISMATCH); \
        AssertLogRelMsgReturn(PDM_VERSION_ARE_COMPATIBLE((pDevIns)->CTX_SUFF(pHlp)->u32Version, CTX_MID(PDM_DEVHLP,_VERSION)), \
                              ("DevHlp=%#x  mine=%#x\n", (pDevIns)->CTX_SUFF(pHlp)->u32Version, CTX_MID(PDM_DEVHLP,_VERSION)), \
                              VERR_PDM_DEVHLP_VERSION_MISMATCH); \
    } while (0)

/**
 * Quietly checks the structure versions of the device instance and device
 * helpers, returning if they are incompatible.
 *
 * This is for use in the destructor.
 *
 * @param   pDevIns     The device instance pointer.
 */
#define PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns) \
    do \
    { \
        PPDMDEVINS pDevInsTypeCheck = (pDevIns); NOREF(pDevInsTypeCheck); \
        if (RT_LIKELY(PDM_VERSION_ARE_COMPATIBLE((pDevIns)->u32Version, PDM_DEVINS_VERSION) )) \
        { /* likely */ } else return VERR_PDM_DEVINS_VERSION_MISMATCH; \
        if (RT_LIKELY(PDM_VERSION_ARE_COMPATIBLE((pDevIns)->CTX_SUFF(pHlp)->u32Version, CTX_MID(PDM_DEVHLP,_VERSION)) )) \
        { /* likely */ } else return VERR_PDM_DEVHLP_VERSION_MISMATCH; \
    } while (0)

/**
 * Wrapper around CFGMR3ValidateConfig for the root config for use in the
 * constructor - returns on failure.
 *
 * This should be invoked after having initialized the instance data
 * sufficiently for the correct operation of the destructor.  The destructor is
 * always called!
 *
 * @param   pDevIns             Pointer to the PDM device instance.
 * @param   pszValidValues      Patterns describing the valid value names.  See
 *                              RTStrSimplePatternMultiMatch for details on the
 *                              pattern syntax.
 * @param   pszValidNodes       Patterns describing the valid node (key) names.
 *                              Pass empty string if no valid nodes.
 */
#define PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, pszValidValues, pszValidNodes) \
    do \
    { \
        int rcValCfg = pDevIns->pHlpR3->pfnCFGMValidateConfig((pDevIns)->pCfg, "/", pszValidValues, pszValidNodes, \
                                                              (pDevIns)->pReg->szName, (pDevIns)->iInstance); \
        if (RT_SUCCESS(rcValCfg)) \
        { /* likely */ } else return rcValCfg; \
    } while (0)

/** @def PDMDEV_ASSERT_EMT
 * Assert that the current thread is the emulation thread.
 */
#ifdef VBOX_STRICT
# define PDMDEV_ASSERT_EMT(pDevIns)  pDevIns->pHlpR3->pfnAssertEMT(pDevIns, __FILE__, __LINE__, __FUNCTION__)
#else
# define PDMDEV_ASSERT_EMT(pDevIns)  do { } while (0)
#endif

/** @def PDMDEV_ASSERT_OTHER
 * Assert that the current thread is NOT the emulation thread.
 */
#ifdef VBOX_STRICT
# define PDMDEV_ASSERT_OTHER(pDevIns)  pDevIns->pHlpR3->pfnAssertOther(pDevIns, __FILE__, __LINE__, __FUNCTION__)
#else
# define PDMDEV_ASSERT_OTHER(pDevIns)  do { } while (0)
#endif

/** @def PDMDEV_ASSERT_VMLOCK_OWNER
 * Assert that the current thread is owner of the VM lock.
 */
#ifdef VBOX_STRICT
# define PDMDEV_ASSERT_VMLOCK_OWNER(pDevIns)  pDevIns->pHlpR3->pfnAssertVMLock(pDevIns, __FILE__, __LINE__, __FUNCTION__)
#else
# define PDMDEV_ASSERT_VMLOCK_OWNER(pDevIns)  do { } while (0)
#endif

/** @def PDMDEV_SET_ERROR
 * Set the VM error. See PDMDevHlpVMSetError() for printf like message formatting.
 */
#define PDMDEV_SET_ERROR(pDevIns, rc, pszError) \
    PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS, "%s", pszError)

/** @def PDMDEV_SET_RUNTIME_ERROR
 * Set the VM runtime error. See PDMDevHlpVMSetRuntimeError() for printf like message formatting.
 */
#define PDMDEV_SET_RUNTIME_ERROR(pDevIns, fFlags, pszErrorId, pszError) \
    PDMDevHlpVMSetRuntimeError(pDevIns, fFlags, pszErrorId, "%s", pszError)

/** @def PDMDEVINS_2_RCPTR
 * Converts a PDM Device instance pointer a RC PDM Device instance pointer.
 */
#ifdef IN_RC
# define PDMDEVINS_2_RCPTR(pDevIns)  (pDevIns)
#else
# define PDMDEVINS_2_RCPTR(pDevIns)  ( (pDevIns)->pDevInsForRC )
#endif

/** @def PDMDEVINS_2_R3PTR
 * Converts a PDM Device instance pointer a R3 PDM Device instance pointer.
 */
#ifdef IN_RING3
# define PDMDEVINS_2_R3PTR(pDevIns)  (pDevIns)
#else
# define PDMDEVINS_2_R3PTR(pDevIns)  ( (pDevIns)->pDevInsForR3 )
#endif

/** @def PDMDEVINS_2_R0PTR
 * Converts a PDM Device instance pointer a R0 PDM Device instance pointer.
 */
#ifdef IN_RING0
# define PDMDEVINS_2_R0PTR(pDevIns)  (pDevIns)
#else
# define PDMDEVINS_2_R0PTR(pDevIns)  ( (pDevIns)->pDevInsR0RemoveMe )
#endif

/** @def PDMDEVINS_DATA_2_R0_REMOVE_ME
 * Converts a PDM device instance data pointer to a ring-0 one.
 * @deprecated
 */
#ifdef IN_RING0
# define PDMDEVINS_DATA_2_R0_REMOVE_ME(pDevIns, pvCC)  (pvCC)
#else
# define PDMDEVINS_DATA_2_R0_REMOVE_ME(pDevIns, pvCC)  ( (pDevIns)->pvInstanceDataR0 + (uintptr_t)(pvCC) - (uintptr_t)(pDevIns)->CTX_SUFF(pvInstanceData) )
#endif


/** @def PDMDEVINS_2_DATA
 * This is a safer edition of PDMINS_2_DATA that checks that the size of the
 * target type is same as PDMDEVREG::cbInstanceShared in strict builds.
 *
 * @note Do no use this macro in common code working on a core structure which
 *       device specific code has expanded.
 */
#if defined(VBOX_STRICT) && defined(RT_COMPILER_SUPPORTS_LAMBDA)
# define PDMDEVINS_2_DATA(a_pDevIns, a_PtrType)  \
    ([](PPDMDEVINS a_pLambdaDevIns) -> a_PtrType \
    { \
        a_PtrType pLambdaRet = (a_PtrType)(a_pLambdaDevIns)->CTX_SUFF(pvInstanceData); \
        Assert(sizeof(*pLambdaRet) == a_pLambdaDevIns->pReg->cbInstanceShared); \
        return pLambdaRet; \
    }(a_pDevIns))
#else
# define PDMDEVINS_2_DATA(a_pDevIns, a_PtrType)  ( (a_PtrType)(a_pDevIns)->CTX_SUFF(pvInstanceData) )
#endif

/** @def PDMDEVINS_2_DATA_CC
 * This is a safer edition of PDMINS_2_DATA_CC that checks that the size of the
 * target type is same as PDMDEVREG::cbInstanceCC in strict builds.
 *
 * @note Do no use this macro in common code working on a core structure which
 *       device specific code has expanded.
 */
#if defined(VBOX_STRICT) && defined(RT_COMPILER_SUPPORTS_LAMBDA)
# define PDMDEVINS_2_DATA_CC(a_pDevIns, a_PtrType)  \
    ([](PPDMDEVINS a_pLambdaDevIns) -> a_PtrType \
    { \
        a_PtrType pLambdaRet = (a_PtrType)&(a_pLambdaDevIns)->achInstanceData[0]; \
        Assert(sizeof(*pLambdaRet) == a_pLambdaDevIns->pReg->cbInstanceCC); \
        return pLambdaRet; \
    }(a_pDevIns))
#else
# define PDMDEVINS_2_DATA_CC(a_pDevIns, a_PtrType)  ( (a_PtrType)(void *)&(a_pDevIns)->achInstanceData[0] )
#endif


#ifdef IN_RING3

/**
 * Combines PDMDevHlpIoPortCreate() & PDMDevHlpIoPortMap().
 */
DECLINLINE(int) PDMDevHlpIoPortCreateAndMap(PPDMDEVINS pDevIns, RTIOPORT Port, RTIOPORT cPorts, PFNIOMIOPORTNEWOUT pfnOut,
                                            PFNIOMIOPORTNEWIN pfnIn, const char *pszDesc, PCIOMIOPORTDESC paExtDescs,
                                            PIOMIOPORTHANDLE phIoPorts)
{
    int rc = pDevIns->pHlpR3->pfnIoPortCreateEx(pDevIns, cPorts, 0, NULL, UINT32_MAX,
                                                pfnOut, pfnIn, NULL, NULL, NULL, pszDesc, paExtDescs, phIoPorts);
    if (RT_SUCCESS(rc))
        rc = pDevIns->pHlpR3->pfnIoPortMap(pDevIns, *phIoPorts, Port);
    return rc;
}

/**
 * Combines PDMDevHlpIoPortCreate() & PDMDevHlpIoPortMap(), but with pvUser.
 */
DECLINLINE(int) PDMDevHlpIoPortCreateUAndMap(PPDMDEVINS pDevIns, RTIOPORT Port, RTIOPORT cPorts, PFNIOMIOPORTNEWOUT pfnOut,
                                             PFNIOMIOPORTNEWIN pfnIn, void *pvUser,
                                             const char *pszDesc, PCIOMIOPORTDESC paExtDescs, PIOMIOPORTHANDLE phIoPorts)
{
    int rc = pDevIns->pHlpR3->pfnIoPortCreateEx(pDevIns, cPorts, 0, NULL, UINT32_MAX,
                                                pfnOut, pfnIn, NULL, NULL, pvUser, pszDesc, paExtDescs, phIoPorts);
    if (RT_SUCCESS(rc))
        rc = pDevIns->pHlpR3->pfnIoPortMap(pDevIns, *phIoPorts, Port);
    return rc;
}

/**
 * Combines PDMDevHlpIoPortCreate() & PDMDevHlpIoPortMap(), but with flags.
 */
DECLINLINE(int) PDMDevHlpIoPortCreateFlagsAndMap(PPDMDEVINS pDevIns, RTIOPORT Port, RTIOPORT cPorts, uint32_t fFlags,
                                                 PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn,
                                                 const char *pszDesc, PCIOMIOPORTDESC paExtDescs, PIOMIOPORTHANDLE phIoPorts)
{
    int rc = pDevIns->pHlpR3->pfnIoPortCreateEx(pDevIns, cPorts, fFlags, NULL, UINT32_MAX,
                                                pfnOut, pfnIn, NULL, NULL, NULL, pszDesc, paExtDescs, phIoPorts);
    if (RT_SUCCESS(rc))
        rc = pDevIns->pHlpR3->pfnIoPortMap(pDevIns, *phIoPorts, Port);
    return rc;
}

/**
 * Combines PDMDevHlpIoPortCreateEx() & PDMDevHlpIoPortMap().
 */
DECLINLINE(int) PDMDevHlpIoPortCreateExAndMap(PPDMDEVINS pDevIns, RTIOPORT Port, RTIOPORT cPorts, uint32_t fFlags,
                                              PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn,
                                              PFNIOMIOPORTNEWOUTSTRING pfnOutStr, PFNIOMIOPORTNEWINSTRING pfnInStr, void *pvUser,
                                              const char *pszDesc, PCIOMIOPORTDESC paExtDescs, PIOMIOPORTHANDLE phIoPorts)
{
    int rc = pDevIns->pHlpR3->pfnIoPortCreateEx(pDevIns, cPorts, fFlags, NULL, UINT32_MAX,
                                                pfnOut, pfnIn, pfnOutStr, pfnInStr, pvUser, pszDesc, paExtDescs, phIoPorts);
    if (RT_SUCCESS(rc))
        rc = pDevIns->pHlpR3->pfnIoPortMap(pDevIns, *phIoPorts, Port);
    return rc;
}

/**
 * @sa PDMDevHlpIoPortCreateEx
 */
DECLINLINE(int) PDMDevHlpIoPortCreate(PPDMDEVINS pDevIns, RTIOPORT cPorts, PPDMPCIDEV pPciDev, uint32_t iPciRegion,
                                      PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn, void *pvUser, const char *pszDesc,
                                      PCIOMIOPORTDESC paExtDescs, PIOMIOPORTHANDLE phIoPorts)
{
    return pDevIns->pHlpR3->pfnIoPortCreateEx(pDevIns, cPorts, 0, pPciDev, iPciRegion,
                                              pfnOut, pfnIn, NULL, NULL, pvUser, pszDesc, paExtDescs, phIoPorts);
}


/**
 * @sa PDMDevHlpIoPortCreateEx
 */
DECLINLINE(int) PDMDevHlpIoPortCreateIsa(PPDMDEVINS pDevIns, RTIOPORT cPorts, PFNIOMIOPORTNEWOUT pfnOut,
                                         PFNIOMIOPORTNEWIN pfnIn, void *pvUser, const char *pszDesc,
                                         PCIOMIOPORTDESC paExtDescs, PIOMIOPORTHANDLE phIoPorts)
{
    return pDevIns->pHlpR3->pfnIoPortCreateEx(pDevIns, cPorts, 0, NULL, UINT32_MAX,
                                              pfnOut, pfnIn, NULL, NULL, pvUser, pszDesc, paExtDescs, phIoPorts);
}

/**
 * @copydoc PDMDEVHLPR3::pfnIoPortCreateEx
 */
DECLINLINE(int) PDMDevHlpIoPortCreateEx(PPDMDEVINS pDevIns, RTIOPORT cPorts, uint32_t fFlags, PPDMPCIDEV pPciDev,
                                        uint32_t iPciRegion, PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn,
                                        PFNIOMIOPORTNEWOUTSTRING pfnOutStr, PFNIOMIOPORTNEWINSTRING pfnInStr, void *pvUser,
                                        const char *pszDesc, PCIOMIOPORTDESC paExtDescs, PIOMIOPORTHANDLE phIoPorts)
{
    return pDevIns->pHlpR3->pfnIoPortCreateEx(pDevIns, cPorts, fFlags, pPciDev, iPciRegion,
                                              pfnOut, pfnIn, pfnOutStr, pfnInStr, pvUser, pszDesc, paExtDescs, phIoPorts);
}

/**
 * @copydoc PDMDEVHLPR3::pfnIoPortMap
 */
DECLINLINE(int) PDMDevHlpIoPortMap(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts, RTIOPORT Port)
{
    return pDevIns->pHlpR3->pfnIoPortMap(pDevIns, hIoPorts, Port);
}

/**
 * @copydoc PDMDEVHLPR3::pfnIoPortUnmap
 */
DECLINLINE(int) PDMDevHlpIoPortUnmap(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts)
{
    return pDevIns->pHlpR3->pfnIoPortUnmap(pDevIns, hIoPorts);
}

/**
 * @copydoc PDMDEVHLPR3::pfnIoPortGetMappingAddress
 */
DECLINLINE(uint32_t) PDMDevHlpIoPortGetMappingAddress(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts)
{
    return pDevIns->pHlpR3->pfnIoPortGetMappingAddress(pDevIns, hIoPorts);
}


#endif /* IN_RING3 */
#if !defined(IN_RING3) || defined(DOXYGEN_RUNNING)

/**
 * @sa PDMDevHlpIoPortSetUpContextEx
 */
DECLINLINE(int) PDMDevHlpIoPortSetUpContext(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts,
                                            PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn, void *pvUser)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnIoPortSetUpContextEx(pDevIns, hIoPorts, pfnOut, pfnIn, NULL, NULL, pvUser);
}

/**
 * @copydoc PDMDEVHLPR0::pfnIoPortSetUpContextEx
 */
DECLINLINE(int) PDMDevHlpIoPortSetUpContextEx(PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts,
                                              PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn,
                                              PFNIOMIOPORTNEWOUTSTRING pfnOutStr, PFNIOMIOPORTNEWINSTRING pfnInStr, void *pvUser)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnIoPortSetUpContextEx(pDevIns, hIoPorts, pfnOut, pfnIn, pfnOutStr, pfnInStr, pvUser);
}

#endif /* !IN_RING3 || DOXYGEN_RUNNING */
#ifdef IN_RING3

/**
 * @sa PDMDevHlpMmioCreateEx
 */
DECLINLINE(int) PDMDevHlpMmioCreate(PPDMDEVINS pDevIns, RTGCPHYS cbRegion, PPDMPCIDEV pPciDev, uint32_t iPciRegion,
                                    PFNIOMMMIONEWWRITE pfnWrite, PFNIOMMMIONEWREAD pfnRead, void *pvUser,
                                    uint32_t fFlags, const char *pszDesc, PIOMMMIOHANDLE phRegion)
{
    return pDevIns->pHlpR3->pfnMmioCreateEx(pDevIns, cbRegion, fFlags, pPciDev, iPciRegion,
                                            pfnWrite, pfnRead, NULL, pvUser, pszDesc, phRegion);
}

/**
 * @copydoc PDMDEVHLPR3::pfnMmioCreateEx
 */
DECLINLINE(int) PDMDevHlpMmioCreateEx(PPDMDEVINS pDevIns, RTGCPHYS cbRegion,
                                      uint32_t fFlags, PPDMPCIDEV pPciDev, uint32_t iPciRegion,
                                      PFNIOMMMIONEWWRITE pfnWrite, PFNIOMMMIONEWREAD pfnRead, PFNIOMMMIONEWFILL pfnFill,
                                      void *pvUser, const char *pszDesc, PIOMMMIOHANDLE phRegion)
{
    return pDevIns->pHlpR3->pfnMmioCreateEx(pDevIns, cbRegion, fFlags, pPciDev, iPciRegion,
                                            pfnWrite, pfnRead, pfnFill, pvUser, pszDesc, phRegion);
}

/**
 * @sa PDMDevHlpMmioCreate and PDMDevHlpMmioMap
 */
DECLINLINE(int) PDMDevHlpMmioCreateAndMap(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTGCPHYS cbRegion,
                                          PFNIOMMMIONEWWRITE pfnWrite, PFNIOMMMIONEWREAD pfnRead,
                                          uint32_t fFlags, const char *pszDesc, PIOMMMIOHANDLE phRegion)
{
    int rc = pDevIns->pHlpR3->pfnMmioCreateEx(pDevIns, cbRegion, fFlags, NULL /*pPciDev*/, UINT32_MAX /*iPciRegion*/,
                                              pfnWrite, pfnRead, NULL /*pfnFill*/, NULL /*pvUser*/, pszDesc, phRegion);
    if (RT_SUCCESS(rc))
        rc = pDevIns->pHlpR3->pfnMmioMap(pDevIns, *phRegion, GCPhys);
    return rc;
}

/**
 * @sa PDMDevHlpMmioCreateEx and PDMDevHlpMmioMap
 */
DECLINLINE(int) PDMDevHlpMmioCreateExAndMap(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTGCPHYS cbRegion, uint32_t fFlags,
                                            PPDMPCIDEV pPciDev, uint32_t iPciRegion, PFNIOMMMIONEWWRITE pfnWrite,
                                            PFNIOMMMIONEWREAD pfnRead, PFNIOMMMIONEWFILL pfnFill, void *pvUser,
                                            const char *pszDesc, PIOMMMIOHANDLE phRegion)
{
    int rc = pDevIns->pHlpR3->pfnMmioCreateEx(pDevIns, cbRegion, fFlags, pPciDev, iPciRegion,
                                              pfnWrite, pfnRead, pfnFill, pvUser, pszDesc, phRegion);
    if (RT_SUCCESS(rc))
        rc = pDevIns->pHlpR3->pfnMmioMap(pDevIns, *phRegion, GCPhys);
    return rc;
}

/**
 * @copydoc PDMDEVHLPR3::pfnMmioMap
 */
DECLINLINE(int) PDMDevHlpMmioMap(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS GCPhys)
{
    return pDevIns->pHlpR3->pfnMmioMap(pDevIns, hRegion, GCPhys);
}

/**
 * @copydoc PDMDEVHLPR3::pfnMmioUnmap
 */
DECLINLINE(int) PDMDevHlpMmioUnmap(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion)
{
    return pDevIns->pHlpR3->pfnMmioUnmap(pDevIns, hRegion);
}

/**
 * @copydoc PDMDEVHLPR3::pfnMmioReduce
 */
DECLINLINE(int) PDMDevHlpMmioReduce(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS cbRegion)
{
    return pDevIns->pHlpR3->pfnMmioReduce(pDevIns, hRegion, cbRegion);
}

/**
 * @copydoc PDMDEVHLPR3::pfnMmioGetMappingAddress
 */
DECLINLINE(RTGCPHYS) PDMDevHlpMmioGetMappingAddress(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion)
{
    return pDevIns->pHlpR3->pfnMmioGetMappingAddress(pDevIns, hRegion);
}

#endif /* IN_RING3 */
#if !defined(IN_RING3) || defined(DOXYGEN_RUNNING)

/**
 * @sa PDMDevHlpMmioSetUpContextEx
 */
DECLINLINE(int) PDMDevHlpMmioSetUpContext(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion,
                                          PFNIOMMMIONEWWRITE pfnWrite, PFNIOMMMIONEWREAD pfnRead, void *pvUser)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnMmioSetUpContextEx(pDevIns, hRegion, pfnWrite, pfnRead, NULL, pvUser);
}

/**
 * @copydoc PDMDEVHLPR0::pfnMmioSetUpContextEx
 */
DECLINLINE(int) PDMDevHlpMmioSetUpContextEx(PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, PFNIOMMMIONEWWRITE pfnWrite,
                                            PFNIOMMMIONEWREAD pfnRead, PFNIOMMMIONEWFILL pfnFill, void *pvUser)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnMmioSetUpContextEx(pDevIns, hRegion, pfnWrite, pfnRead, pfnFill, pvUser);
}

#endif /* !IN_RING3 || DOXYGEN_RUNNING */
#ifdef IN_RING3

/**
 * @copydoc PDMDEVHLPR3::pfnMmio2Create
 */
DECLINLINE(int) PDMDevHlpMmio2Create(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iPciRegion, RTGCPHYS cbRegion,
                                     uint32_t fFlags, const char *pszDesc, void **ppvMapping, PPGMMMIO2HANDLE phRegion)
{
    return pDevIns->pHlpR3->pfnMmio2Create(pDevIns, pPciDev, iPciRegion, cbRegion, fFlags, pszDesc, ppvMapping, phRegion);
}

/**
 * @copydoc PDMDEVHLPR3::pfnMmio2Map
 */
DECLINLINE(int) PDMDevHlpMmio2Map(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion, RTGCPHYS GCPhys)
{
    return pDevIns->pHlpR3->pfnMmio2Map(pDevIns, hRegion, GCPhys);
}

/**
 * @copydoc PDMDEVHLPR3::pfnMmio2Unmap
 */
DECLINLINE(int) PDMDevHlpMmio2Unmap(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion)
{
    return pDevIns->pHlpR3->pfnMmio2Unmap(pDevIns, hRegion);
}

/**
 * @copydoc PDMDEVHLPR3::pfnMmio2Reduce
 */
DECLINLINE(int) PDMDevHlpMmio2Reduce(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion, RTGCPHYS cbRegion)
{
    return pDevIns->pHlpR3->pfnMmio2Reduce(pDevIns, hRegion, cbRegion);
}

/**
 * @copydoc PDMDEVHLPR3::pfnMmio2GetMappingAddress
 */
DECLINLINE(RTGCPHYS) PDMDevHlpMmio2GetMappingAddress(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion)
{
    return pDevIns->pHlpR3->pfnMmio2GetMappingAddress(pDevIns, hRegion);
}

#endif /* IN_RING3 */
#if !defined(IN_RING3) || defined(DOXYGEN_RUNNING)

/**
 * @copydoc PDMDEVHLPR0::pfnMmio2SetUpContext
 */
DECLINLINE(int) PDMDevHlpMmio2SetUpContext(PPDMDEVINS pDevIns, PGMMMIO2HANDLE hRegion,
                                           size_t offSub, size_t cbSub, void **ppvMapping)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnMmio2SetUpContext(pDevIns, hRegion, offSub, cbSub, ppvMapping);
}

#endif /* !IN_RING3 || DOXYGEN_RUNNING */
#ifdef IN_RING3

/**
 * @copydoc PDMDEVHLPR3::pfnROMRegister
 */
DECLINLINE(int) PDMDevHlpROMRegister(PPDMDEVINS pDevIns, RTGCPHYS GCPhysStart, uint32_t cbRange,
                                     const void *pvBinary, uint32_t cbBinary, uint32_t fFlags, const char *pszDesc)
{
    return pDevIns->pHlpR3->pfnROMRegister(pDevIns, GCPhysStart, cbRange, pvBinary, cbBinary, fFlags, pszDesc);
}

/**
 * @copydoc PDMDEVHLPR3::pfnROMProtectShadow
 */
DECLINLINE(int) PDMDevHlpROMProtectShadow(PPDMDEVINS pDevIns, RTGCPHYS GCPhysStart, uint32_t cbRange, PGMROMPROT enmProt)
{
    return pDevIns->pHlpR3->pfnROMProtectShadow(pDevIns, GCPhysStart, cbRange, enmProt);
}

/**
 * Register a save state data unit.
 *
 * @returns VBox status.
 * @param   pDevIns             The device instance.
 * @param   uVersion            Data layout version number.
 * @param   cbGuess             The approximate amount of data in the unit.
 *                              Only for progress indicators.
 * @param   pfnSaveExec         Execute save callback, optional.
 * @param   pfnLoadExec         Execute load callback, optional.
 */
DECLINLINE(int) PDMDevHlpSSMRegister(PPDMDEVINS pDevIns, uint32_t uVersion, size_t cbGuess,
                                     PFNSSMDEVSAVEEXEC pfnSaveExec, PFNSSMDEVLOADEXEC pfnLoadExec)
{
    return pDevIns->pHlpR3->pfnSSMRegister(pDevIns, uVersion, cbGuess, NULL /*pszBefore*/,
                                           NULL /*pfnLivePrep*/, NULL /*pfnLiveExec*/,  NULL /*pfnLiveDone*/,
                                           NULL /*pfnSavePrep*/, pfnSaveExec,           NULL /*pfnSaveDone*/,
                                           NULL /*pfnLoadPrep*/, pfnLoadExec,           NULL /*pfnLoadDone*/);
}

/**
 * Register a save state data unit with a live save callback as well.
 *
 * @returns VBox status.
 * @param   pDevIns             The device instance.
 * @param   uVersion            Data layout version number.
 * @param   cbGuess             The approximate amount of data in the unit.
 *                              Only for progress indicators.
 * @param   pfnLiveExec         Execute live callback, optional.
 * @param   pfnSaveExec         Execute save callback, optional.
 * @param   pfnLoadExec         Execute load callback, optional.
 */
DECLINLINE(int) PDMDevHlpSSMRegister3(PPDMDEVINS pDevIns, uint32_t uVersion, size_t cbGuess,
                                      PFNSSMDEVLIVEEXEC pfnLiveExec, PFNSSMDEVSAVEEXEC pfnSaveExec, PFNSSMDEVLOADEXEC pfnLoadExec)
{
    return pDevIns->pHlpR3->pfnSSMRegister(pDevIns, uVersion, cbGuess, NULL /*pszBefore*/,
                                           NULL /*pfnLivePrep*/, pfnLiveExec,  NULL /*pfnLiveDone*/,
                                           NULL /*pfnSavePrep*/, pfnSaveExec,  NULL /*pfnSaveDone*/,
                                           NULL /*pfnLoadPrep*/, pfnLoadExec,  NULL /*pfnLoadDone*/);
}

/**
 * @copydoc PDMDEVHLPR3::pfnSSMRegister
 */
DECLINLINE(int) PDMDevHlpSSMRegisterEx(PPDMDEVINS pDevIns, uint32_t uVersion, size_t cbGuess, const char *pszBefore,
                                       PFNSSMDEVLIVEPREP pfnLivePrep, PFNSSMDEVLIVEEXEC pfnLiveExec, PFNSSMDEVLIVEVOTE pfnLiveVote,
                                       PFNSSMDEVSAVEPREP pfnSavePrep, PFNSSMDEVSAVEEXEC pfnSaveExec, PFNSSMDEVSAVEDONE pfnSaveDone,
                                       PFNSSMDEVLOADPREP pfnLoadPrep, PFNSSMDEVLOADEXEC pfnLoadExec, PFNSSMDEVLOADDONE pfnLoadDone)
{
    return pDevIns->pHlpR3->pfnSSMRegister(pDevIns, uVersion, cbGuess, pszBefore,
                                           pfnLivePrep, pfnLiveExec, pfnLiveVote,
                                           pfnSavePrep, pfnSaveExec, pfnSaveDone,
                                           pfnLoadPrep, pfnLoadExec, pfnLoadDone);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTMTimerCreate
 */
DECLINLINE(int) PDMDevHlpTMTimerCreate(PPDMDEVINS pDevIns, TMCLOCK enmClock, PFNTMTIMERDEV pfnCallback, void *pvUser,
                                       uint32_t fFlags, const char *pszDesc, PPTMTIMERR3 ppTimer)
{
    return pDevIns->pHlpR3->pfnTMTimerCreate(pDevIns, enmClock, pfnCallback, pvUser, fFlags, pszDesc, ppTimer);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerCreate
 */
DECLINLINE(int) PDMDevHlpTimerCreate(PPDMDEVINS pDevIns, TMCLOCK enmClock, PFNTMTIMERDEV pfnCallback, void *pvUser,
                                     uint32_t fFlags, const char *pszDesc, PTMTIMERHANDLE phTimer)
{
    return pDevIns->pHlpR3->pfnTimerCreate(pDevIns, enmClock, pfnCallback, pvUser, fFlags, pszDesc, phTimer);
}

#endif /* IN_RING3 */

/**
 * @copydoc PDMDEVHLPR3::pfnTimerToPtr
 */
DECLINLINE(PTMTIMER) PDMDevHlpTimerToPtr(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerToPtr(pDevIns, hTimer);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerFromMicro
 */
DECLINLINE(uint64_t) PDMDevHlpTimerFromMicro(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMicroSecs)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerFromMicro(pDevIns, hTimer, cMicroSecs);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerFromMilli
 */
DECLINLINE(uint64_t) PDMDevHlpTimerFromMilli(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMilliSecs)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerFromMilli(pDevIns, hTimer, cMilliSecs);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerFromNano
 */
DECLINLINE(uint64_t) PDMDevHlpTimerFromNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cNanoSecs)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerFromNano(pDevIns, hTimer, cNanoSecs);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerGet
 */
DECLINLINE(uint64_t) PDMDevHlpTimerGet(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerGet(pDevIns, hTimer);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerGetFreq
 */
DECLINLINE(uint64_t) PDMDevHlpTimerGetFreq(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerGetFreq(pDevIns, hTimer);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerGetNano
 */
DECLINLINE(uint64_t) PDMDevHlpTimerGetNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerGetNano(pDevIns, hTimer);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerIsActive
 */
DECLINLINE(bool)     PDMDevHlpTimerIsActive(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerIsActive(pDevIns, hTimer);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerIsLockOwner
 */
DECLINLINE(bool)     PDMDevHlpTimerIsLockOwner(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerIsLockOwner(pDevIns, hTimer);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerLockClock
 */
DECLINLINE(VBOXSTRICTRC) PDMDevHlpTimerLockClock(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, int rcBusy)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerLockClock(pDevIns, hTimer, rcBusy);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerLockClock2
 */
DECLINLINE(VBOXSTRICTRC) PDMDevHlpTimerLockClock2(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect, int rcBusy)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerLockClock2(pDevIns, hTimer, pCritSect, rcBusy);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerSet
 */
DECLINLINE(int)      PDMDevHlpTimerSet(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t uExpire)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerSet(pDevIns, hTimer, uExpire);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerSetFrequencyHint
 */
DECLINLINE(int)      PDMDevHlpTimerSetFrequencyHint(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint32_t uHz)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerSetFrequencyHint(pDevIns, hTimer, uHz);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerSetMicro
 */
DECLINLINE(int)      PDMDevHlpTimerSetMicro(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMicrosToNext)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerSetMicro(pDevIns, hTimer, cMicrosToNext);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerSetMillies
 */
DECLINLINE(int)      PDMDevHlpTimerSetMillies(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cMilliesToNext)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerSetMillies(pDevIns, hTimer, cMilliesToNext);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerSetNano
 */
DECLINLINE(int)      PDMDevHlpTimerSetNano(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cNanosToNext)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerSetNano(pDevIns, hTimer, cNanosToNext);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerSetRelative
 */
DECLINLINE(int)      PDMDevHlpTimerSetRelative(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, uint64_t cTicksToNext, uint64_t *pu64Now)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerSetRelative(pDevIns, hTimer, cTicksToNext, pu64Now);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerStop
 */
DECLINLINE(int)      PDMDevHlpTimerStop(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTimerStop(pDevIns, hTimer);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerUnlockClock
 */
DECLINLINE(void)     PDMDevHlpTimerUnlockClock(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    pDevIns->CTX_SUFF(pHlp)->pfnTimerUnlockClock(pDevIns, hTimer);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerUnlockClock2
 */
DECLINLINE(void)     PDMDevHlpTimerUnlockClock2(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect)
{
    pDevIns->CTX_SUFF(pHlp)->pfnTimerUnlockClock2(pDevIns, hTimer, pCritSect);
}

#ifdef IN_RING3

/**
 * @copydoc PDMDEVHLPR3::pfnTimerSetCritSect
 */
DECLINLINE(int) PDMDevHlpTimerSetCritSect(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect)
{
    return pDevIns->pHlpR3->pfnTimerSetCritSect(pDevIns, hTimer, pCritSect);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerSave
 */
DECLINLINE(int) PDMDevHlpTimerSave(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PSSMHANDLE pSSM)
{
    return pDevIns->pHlpR3->pfnTimerSave(pDevIns, hTimer, pSSM);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerLoad
 */
DECLINLINE(int) PDMDevHlpTimerLoad(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, PSSMHANDLE pSSM)
{
    return pDevIns->pHlpR3->pfnTimerLoad(pDevIns, hTimer, pSSM);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTimerDestroy
 */
DECLINLINE(int) PDMDevHlpTimerDestroy(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer)
{
    return pDevIns->pHlpR3->pfnTimerDestroy(pDevIns, hTimer);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTMUtcNow
 */
DECLINLINE(PRTTIMESPEC) PDMDevHlpTMUtcNow(PPDMDEVINS pDevIns, PRTTIMESPEC pTime)
{
    return pDevIns->pHlpR3->pfnTMUtcNow(pDevIns, pTime);
}

#endif

/**
 * @copydoc PDMDEVHLPR3::pfnPhysRead
 */
DECLINLINE(int) PDMDevHlpPhysRead(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnPhysRead(pDevIns, GCPhys, pvBuf, cbRead);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPhysWrite
 */
DECLINLINE(int) PDMDevHlpPhysWrite(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnPhysWrite(pDevIns, GCPhys, pvBuf, cbWrite);
}

#ifdef IN_RING3

/**
 * @copydoc PDMDEVHLPR3::pfnPhysGCPhys2CCPtr
 */
DECLINLINE(int) PDMDevHlpPhysGCPhys2CCPtr(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t fFlags, void **ppv, PPGMPAGEMAPLOCK pLock)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnPhysGCPhys2CCPtr(pDevIns, GCPhys, fFlags, ppv, pLock);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPhysGCPhys2CCPtrReadOnly
 */
DECLINLINE(int) PDMDevHlpPhysGCPhys2CCPtrReadOnly(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t fFlags, void const **ppv,
                                                  PPGMPAGEMAPLOCK pLock)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnPhysGCPhys2CCPtrReadOnly(pDevIns, GCPhys, fFlags, ppv, pLock);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPhysReleasePageMappingLock
 */
DECLINLINE(void) PDMDevHlpPhysReleasePageMappingLock(PPDMDEVINS pDevIns, PPGMPAGEMAPLOCK pLock)
{
    pDevIns->CTX_SUFF(pHlp)->pfnPhysReleasePageMappingLock(pDevIns, pLock);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPhysBulkGCPhys2CCPtr
 */
DECLINLINE(int) PDMDevHlpPhysBulkGCPhys2CCPtr(PPDMDEVINS pDevIns, uint32_t cPages, PCRTGCPHYS paGCPhysPages,
                                              uint32_t fFlags, void **papvPages, PPGMPAGEMAPLOCK paLocks)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnPhysBulkGCPhys2CCPtr(pDevIns, cPages, paGCPhysPages, fFlags, papvPages, paLocks);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPhysBulkGCPhys2CCPtrReadOnly
 */
DECLINLINE(int) PDMDevHlpPhysBulkGCPhys2CCPtrReadOnly(PPDMDEVINS pDevIns, uint32_t cPages, PCRTGCPHYS paGCPhysPages,
                                                      uint32_t fFlags, void const **papvPages, PPGMPAGEMAPLOCK paLocks)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnPhysBulkGCPhys2CCPtrReadOnly(pDevIns, cPages, paGCPhysPages, fFlags, papvPages, paLocks);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPhysBulkReleasePageMappingLocks
 */
DECLINLINE(void) PDMDevHlpPhysBulkReleasePageMappingLocks(PPDMDEVINS pDevIns, uint32_t cPages, PPGMPAGEMAPLOCK paLocks)
{
    pDevIns->CTX_SUFF(pHlp)->pfnPhysBulkReleasePageMappingLocks(pDevIns, cPages, paLocks);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPhysReadGCVirt
 */
DECLINLINE(int) PDMDevHlpPhysReadGCVirt(PPDMDEVINS pDevIns, void *pvDst, RTGCPTR GCVirtSrc, size_t cb)
{
    return pDevIns->pHlpR3->pfnPhysReadGCVirt(pDevIns, pvDst, GCVirtSrc, cb);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPhysWriteGCVirt
 */
DECLINLINE(int) PDMDevHlpPhysWriteGCVirt(PPDMDEVINS pDevIns, RTGCPTR GCVirtDst, const void *pvSrc, size_t cb)
{
    return pDevIns->pHlpR3->pfnPhysWriteGCVirt(pDevIns, GCVirtDst, pvSrc, cb);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPhysGCPtr2GCPhys
 */
DECLINLINE(int) PDMDevHlpPhysGCPtr2GCPhys(PPDMDEVINS pDevIns, RTGCPTR GCPtr, PRTGCPHYS pGCPhys)
{
    return pDevIns->pHlpR3->pfnPhysGCPtr2GCPhys(pDevIns, GCPtr, pGCPhys);
}

/**
 * @copydoc PDMDEVHLPR3::pfnMMHeapAlloc
 */
DECLINLINE(void *) PDMDevHlpMMHeapAlloc(PPDMDEVINS pDevIns, size_t cb)
{
    return pDevIns->pHlpR3->pfnMMHeapAlloc(pDevIns, cb);
}

/**
 * @copydoc PDMDEVHLPR3::pfnMMHeapAllocZ
 */
DECLINLINE(void *) PDMDevHlpMMHeapAllocZ(PPDMDEVINS pDevIns, size_t cb)
{
    return pDevIns->pHlpR3->pfnMMHeapAllocZ(pDevIns, cb);
}

/**
 * @copydoc PDMDEVHLPR3::pfnMMHeapFree
 */
DECLINLINE(void) PDMDevHlpMMHeapFree(PPDMDEVINS pDevIns, void *pv)
{
    pDevIns->pHlpR3->pfnMMHeapFree(pDevIns, pv);
}
#endif /* IN_RING3 */

/**
 * @copydoc PDMDEVHLPR3::pfnVMState
 */
DECLINLINE(VMSTATE) PDMDevHlpVMState(PPDMDEVINS pDevIns)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnVMState(pDevIns);
}

#ifdef IN_RING3
/**
 * @copydoc PDMDEVHLPR3::pfnVMTeleportedAndNotFullyResumedYet
 */
DECLINLINE(bool) PDMDevHlpVMTeleportedAndNotFullyResumedYet(PPDMDEVINS pDevIns)
{
    return pDevIns->pHlpR3->pfnVMTeleportedAndNotFullyResumedYet(pDevIns);
}
#endif /* IN_RING3 */

/**
 * @copydoc PDMDEVHLPR3::pfnVMSetError
 */
DECLINLINE(int) RT_IPRT_FORMAT_ATTR(6, 7) PDMDevHlpVMSetError(PPDMDEVINS pDevIns, const int rc, RT_SRC_POS_DECL,
                                                              const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    pDevIns->CTX_SUFF(pHlp)->pfnVMSetErrorV(pDevIns, rc, RT_SRC_POS_ARGS, pszFormat, va);
    va_end(va);
    return rc;
}

/**
 * @copydoc PDMDEVHLPR3::pfnVMSetRuntimeError
 */
DECLINLINE(int) RT_IPRT_FORMAT_ATTR(4, 5) PDMDevHlpVMSetRuntimeError(PPDMDEVINS pDevIns, uint32_t fFlags, const char *pszErrorId,
                                                                     const char *pszFormat, ...)
{
    va_list va;
    int rc;
    va_start(va, pszFormat);
    rc = pDevIns->CTX_SUFF(pHlp)->pfnVMSetRuntimeErrorV(pDevIns, fFlags, pszErrorId, pszFormat, va);
    va_end(va);
    return rc;
}

/**
 * VBOX_STRICT wrapper for pHlp->pfnDBGFStopV.
 *
 * @returns VBox status code which must be passed up to the VMM.  This will be
 *          VINF_SUCCESS in non-strict builds.
 * @param   pDevIns             The device instance.
 * @param   SRC_POS             Use RT_SRC_POS.
 * @param   pszFormat           Message. (optional)
 * @param   ...                 Message parameters.
 */
DECLINLINE(int) RT_IPRT_FORMAT_ATTR(5, 6) PDMDevHlpDBGFStop(PPDMDEVINS pDevIns, RT_SRC_POS_DECL, const char *pszFormat, ...)
{
#ifdef VBOX_STRICT
# ifdef IN_RING3
    int rc;
    va_list args;
    va_start(args, pszFormat);
    rc = pDevIns->pHlpR3->pfnDBGFStopV(pDevIns, RT_SRC_POS_ARGS, pszFormat, args);
    va_end(args);
    return rc;
# else
    NOREF(pDevIns);
    NOREF(pszFile);
    NOREF(iLine);
    NOREF(pszFunction);
    NOREF(pszFormat);
    return VINF_EM_DBG_STOP;
# endif
#else
    NOREF(pDevIns);
    NOREF(pszFile);
    NOREF(iLine);
    NOREF(pszFunction);
    NOREF(pszFormat);
    return VINF_SUCCESS;
#endif
}

#ifdef IN_RING3

/**
 * @copydoc PDMDEVHLPR3::pfnDBGFInfoRegister
 */
DECLINLINE(int) PDMDevHlpDBGFInfoRegister(PPDMDEVINS pDevIns, const char *pszName, const char *pszDesc, PFNDBGFHANDLERDEV pfnHandler)
{
    return pDevIns->pHlpR3->pfnDBGFInfoRegister(pDevIns, pszName, pszDesc, pfnHandler);
}

/**
 * @copydoc PDMDEVHLPR3::pfnDBGFInfoRegisterArgv
 */
DECLINLINE(int) PDMDevHlpDBGFInfoRegisterArgv(PPDMDEVINS pDevIns, const char *pszName, const char *pszDesc, PFNDBGFINFOARGVDEV pfnHandler)
{
    return pDevIns->pHlpR3->pfnDBGFInfoRegisterArgv(pDevIns, pszName, pszDesc, pfnHandler);
}

/**
 * @copydoc PDMDEVHLPR3::pfnDBGFRegRegister
 */
DECLINLINE(int) PDMDevHlpDBGFRegRegister(PPDMDEVINS pDevIns, PCDBGFREGDESC paRegisters)
{
    return pDevIns->pHlpR3->pfnDBGFRegRegister(pDevIns, paRegisters);
}

/**
 * @copydoc PDMDEVHLPR3::pfnSTAMRegister
 */
DECLINLINE(void) PDMDevHlpSTAMRegister(PPDMDEVINS pDevIns, void *pvSample, STAMTYPE enmType, const char *pszName, STAMUNIT enmUnit, const char *pszDesc)
{
    pDevIns->pHlpR3->pfnSTAMRegister(pDevIns, pvSample, enmType, pszName, enmUnit, pszDesc);
}

/**
 * Same as pfnSTAMRegister except that the name is specified in a
 * RTStrPrintf like fashion.
 *
 * @returns VBox status.
 * @param   pDevIns             Device instance of the DMA.
 * @param   pvSample            Pointer to the sample.
 * @param   enmType             Sample type. This indicates what pvSample is
 *                              pointing at.
 * @param   enmVisibility       Visibility type specifying whether unused
 *                              statistics should be visible or not.
 * @param   enmUnit             Sample unit.
 * @param   pszDesc             Sample description.
 * @param   pszName             Sample name format string, unix path style.  If
 *                              this does not start with a '/', the default
 *                              prefix will be prepended, otherwise it will be
 *                              used as-is.
 * @param   ...                 Arguments to the format string.
 */
DECLINLINE(void) RT_IPRT_FORMAT_ATTR(7, 8) PDMDevHlpSTAMRegisterF(PPDMDEVINS pDevIns, void *pvSample, STAMTYPE enmType,
                                                                  STAMVISIBILITY enmVisibility, STAMUNIT enmUnit,
                                                                  const char *pszDesc, const char *pszName, ...)
{
    va_list va;
    va_start(va, pszName);
    pDevIns->pHlpR3->pfnSTAMRegisterV(pDevIns, pvSample, enmType, enmVisibility, enmUnit, pszDesc, pszName, va);
    va_end(va);
}

/**
 * Registers the device with the default PCI bus.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pPciDev             The PCI device structure.
 *                              This must be kept in the instance data.
 *                              The PCI configuration must be initialized before registration.
 */
DECLINLINE(int) PDMDevHlpPCIRegister(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev)
{
    return pDevIns->pHlpR3->pfnPCIRegister(pDevIns, pPciDev, 0 /*fFlags*/,
                                           PDMPCIDEVREG_DEV_NO_FIRST_UNUSED, PDMPCIDEVREG_FUN_NO_FIRST_UNUSED, NULL);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPCIRegister
 */
DECLINLINE(int) PDMDevHlpPCIRegisterEx(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t fFlags,
                                       uint8_t uPciDevNo, uint8_t uPciFunNo, const char *pszName)
{
    return pDevIns->pHlpR3->pfnPCIRegister(pDevIns, pPciDev, fFlags, uPciDevNo, uPciFunNo, pszName);
}

/**
 * Initialize MSI emulation support for the first PCI device.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pMsiReg             MSI emulation registration structure.
 */
DECLINLINE(int) PDMDevHlpPCIRegisterMsi(PPDMDEVINS pDevIns, PPDMMSIREG pMsiReg)
{
    return pDevIns->pHlpR3->pfnPCIRegisterMsi(pDevIns, NULL, pMsiReg);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPCIRegisterMsi
 */
DECLINLINE(int) PDMDevHlpPCIRegisterMsiEx(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, PPDMMSIREG pMsiReg)
{
    return pDevIns->pHlpR3->pfnPCIRegisterMsi(pDevIns, pPciDev, pMsiReg);
}

/**
 * Registers a I/O port region for the default PCI device.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   iRegion         The region number.
 * @param   cbRegion        Size of the region.
 * @param   hIoPorts        Handle to the I/O port region.
 */
DECLINLINE(int) PDMDevHlpPCIIORegionRegisterIo(PPDMDEVINS pDevIns, uint32_t iRegion, RTGCPHYS cbRegion, IOMIOPORTHANDLE hIoPorts)
{
    return pDevIns->pHlpR3->pfnPCIIORegionRegister(pDevIns, NULL, iRegion, cbRegion, PCI_ADDRESS_SPACE_IO,
                                                   PDMPCIDEV_IORGN_F_IOPORT_HANDLE | PDMPCIDEV_IORGN_F_NEW_STYLE, hIoPorts, NULL);
}

/**
 * Registers a I/O port region for the default PCI device, custom map/unmap.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   iRegion         The region number.
 * @param   cbRegion        Size of the region.
 * @param   pfnMapUnmap     Callback for doing the mapping, optional.  The
 *                          callback will be invoked holding only the PDM lock.
 *                          The device lock will _not_ be taken (due to lock
 *                          order).
 */
DECLINLINE(int) PDMDevHlpPCIIORegionRegisterIoCustom(PPDMDEVINS pDevIns, uint32_t iRegion, RTGCPHYS cbRegion,
                                                     PFNPCIIOREGIONMAP pfnMapUnmap)
{
    return pDevIns->pHlpR3->pfnPCIIORegionRegister(pDevIns, NULL, iRegion, cbRegion, PCI_ADDRESS_SPACE_IO,
                                                   PDMPCIDEV_IORGN_F_NO_HANDLE | PDMPCIDEV_IORGN_F_NEW_STYLE,
                                                   UINT64_MAX, pfnMapUnmap);
}

/**
 * Combines PDMDevHlpIoPortCreate and PDMDevHlpPCIIORegionRegisterIo, creating
 * and registering an I/O port region for the default PCI device.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance to register the ports with.
 * @param   cPorts          The count of I/O ports in the region (the size).
 * @param   iPciRegion      The PCI device region.
 * @param   pfnOut          Pointer to function which is gonna handle OUT
 *                          operations. Optional.
 * @param   pfnIn           Pointer to function which is gonna handle IN operations.
 *                          Optional.
 * @param   pvUser          User argument to pass to the callbacks.
 * @param   pszDesc         Pointer to description string. This must not be freed.
 * @param   paExtDescs      Extended per-port descriptions, optional.  Partial range
 *                          coverage is allowed.  This must not be freed.
 * @param   phIoPorts       Where to return the I/O port range handle.
 *
 */
DECLINLINE(int) PDMDevHlpPCIIORegionCreateIo(PPDMDEVINS pDevIns, uint32_t iPciRegion, RTIOPORT cPorts,
                                             PFNIOMIOPORTNEWOUT pfnOut, PFNIOMIOPORTNEWIN pfnIn, void *pvUser,
                                             const char *pszDesc, PCIOMIOPORTDESC paExtDescs, PIOMIOPORTHANDLE phIoPorts)

{
    int rc = pDevIns->pHlpR3->pfnIoPortCreateEx(pDevIns, cPorts, 0 /*fFlags*/, pDevIns->apPciDevs[0], iPciRegion << 16,
                                                pfnOut, pfnIn, NULL, NULL, pvUser, pszDesc, paExtDescs, phIoPorts);
    if (RT_SUCCESS(rc))
        rc = pDevIns->pHlpR3->pfnPCIIORegionRegister(pDevIns, pDevIns->apPciDevs[0], iPciRegion, cPorts, PCI_ADDRESS_SPACE_IO,
                                                     PDMPCIDEV_IORGN_F_IOPORT_HANDLE | PDMPCIDEV_IORGN_F_NEW_STYLE,
                                                     *phIoPorts, NULL /*pfnMapUnmap*/);
    return rc;
}

/**
 * Registers an MMIO region for the default PCI device.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   iRegion         The region number.
 * @param   cbRegion        Size of the region.
 * @param   enmType         PCI_ADDRESS_SPACE_MEM or
 *                          PCI_ADDRESS_SPACE_MEM_PREFETCH, optionally or-ing in
 *                          PCI_ADDRESS_SPACE_BAR64 or PCI_ADDRESS_SPACE_BAR32.
 * @param   hMmioRegion     Handle to the MMIO region.
 * @param   pfnMapUnmap     Callback for doing the mapping, optional.  The
 *                          callback will be invoked holding only the PDM lock.
 *                          The device lock will _not_ be taken (due to lock
 *                          order).
 */
DECLINLINE(int) PDMDevHlpPCIIORegionRegisterMmio(PPDMDEVINS pDevIns, uint32_t iRegion, RTGCPHYS cbRegion, PCIADDRESSSPACE enmType,
                                                 IOMMMIOHANDLE hMmioRegion, PFNPCIIOREGIONMAP pfnMapUnmap)
{
    return pDevIns->pHlpR3->pfnPCIIORegionRegister(pDevIns, NULL, iRegion, cbRegion, enmType,
                                                   PDMPCIDEV_IORGN_F_MMIO_HANDLE | PDMPCIDEV_IORGN_F_NEW_STYLE,
                                                   hMmioRegion, pfnMapUnmap);
}

/**
 * Registers an MMIO region for the default PCI device, extended version.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pPciDev         The PCI device structure.
 * @param   iRegion         The region number.
 * @param   cbRegion        Size of the region.
 * @param   enmType         PCI_ADDRESS_SPACE_MEM or
 *                          PCI_ADDRESS_SPACE_MEM_PREFETCH, optionally or-ing in
 *                          PCI_ADDRESS_SPACE_BAR64 or PCI_ADDRESS_SPACE_BAR32.
 * @param   hMmioRegion     Handle to the MMIO region.
 * @param   pfnMapUnmap     Callback for doing the mapping, optional.  The
 *                          callback will be invoked holding only the PDM lock.
 *                          The device lock will _not_ be taken (due to lock
 *                          order).
 */
DECLINLINE(int) PDMDevHlpPCIIORegionRegisterMmioEx(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                   RTGCPHYS cbRegion, PCIADDRESSSPACE enmType, IOMMMIOHANDLE hMmioRegion,
                                                   PFNPCIIOREGIONMAP pfnMapUnmap)
{
    return pDevIns->pHlpR3->pfnPCIIORegionRegister(pDevIns, pPciDev, iRegion, cbRegion, enmType,
                                                   PDMPCIDEV_IORGN_F_MMIO_HANDLE | PDMPCIDEV_IORGN_F_NEW_STYLE,
                                                   hMmioRegion, pfnMapUnmap);
}

/**
 * Combines PDMDevHlpMmioCreate and PDMDevHlpPCIIORegionRegisterMmio, creating
 * and registering an MMIO region for the default PCI device.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance to register the ports with.
 * @param   cbRegion        The size of the region in bytes.
 * @param   iPciRegion      The PCI device region.
 * @param   enmType         PCI_ADDRESS_SPACE_MEM or
 *                          PCI_ADDRESS_SPACE_MEM_PREFETCH, optionally or-ing in
 *                          PCI_ADDRESS_SPACE_BAR64 or PCI_ADDRESS_SPACE_BAR32.
 * @param   fFlags          Flags, IOMMMIO_FLAGS_XXX.
 * @param   pfnWrite        Pointer to function which is gonna handle Write
 *                          operations.
 * @param   pfnRead         Pointer to function which is gonna handle Read
 *                          operations.
 * @param   pvUser          User argument to pass to the callbacks.
 * @param   pszDesc         Pointer to description string. This must not be freed.
 * @param   phRegion        Where to return the MMIO region handle.
 *
 */
DECLINLINE(int) PDMDevHlpPCIIORegionCreateMmio(PPDMDEVINS pDevIns, uint32_t iPciRegion, RTGCPHYS cbRegion, PCIADDRESSSPACE enmType,
                                               PFNIOMMMIONEWWRITE pfnWrite, PFNIOMMMIONEWREAD pfnRead, void *pvUser,
                                               uint32_t fFlags, const char *pszDesc, PIOMMMIOHANDLE phRegion)

{
    int rc = pDevIns->pHlpR3->pfnMmioCreateEx(pDevIns, cbRegion, fFlags, pDevIns->apPciDevs[0], iPciRegion << 16,
                                              pfnWrite, pfnRead, NULL /*pfnFill*/, pvUser, pszDesc, phRegion);
    if (RT_SUCCESS(rc))
        rc = pDevIns->pHlpR3->pfnPCIIORegionRegister(pDevIns, pDevIns->apPciDevs[0], iPciRegion, cbRegion, enmType,
                                                     PDMPCIDEV_IORGN_F_MMIO_HANDLE | PDMPCIDEV_IORGN_F_NEW_STYLE,
                                                     *phRegion, NULL /*pfnMapUnmap*/);
    return rc;
}


/**
 * Registers an MMIO2 region for the default PCI device.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   iRegion         The region number.
 * @param   cbRegion        Size of the region.
 * @param   enmType         PCI_ADDRESS_SPACE_MEM or
 *                          PCI_ADDRESS_SPACE_MEM_PREFETCH, optionally or-ing in
 *                          PCI_ADDRESS_SPACE_BAR64 or PCI_ADDRESS_SPACE_BAR32.
 * @param   hMmio2Region    Handle to the MMIO2 region.
 */
DECLINLINE(int) PDMDevHlpPCIIORegionRegisterMmio2(PPDMDEVINS pDevIns, uint32_t iRegion, RTGCPHYS cbRegion,
                                                  PCIADDRESSSPACE enmType, PGMMMIO2HANDLE hMmio2Region)
{
    return pDevIns->pHlpR3->pfnPCIIORegionRegister(pDevIns, NULL, iRegion, cbRegion, enmType,
                                                   PDMPCIDEV_IORGN_F_MMIO2_HANDLE | PDMPCIDEV_IORGN_F_NEW_STYLE,
                                                   hMmio2Region, NULL);
}

/**
 * Combines PDMDevHlpMmio2Create and PDMDevHlpPCIIORegionRegisterMmio2, creating
 * and registering an MMIO2 region for the default PCI device, extended edition.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance to register the ports with.
 * @param   cbRegion        The size of the region in bytes.
 * @param   iPciRegion      The PCI device region.
 * @param   enmType         PCI_ADDRESS_SPACE_MEM or
 *                          PCI_ADDRESS_SPACE_MEM_PREFETCH, optionally or-ing in
 *                          PCI_ADDRESS_SPACE_BAR64 or PCI_ADDRESS_SPACE_BAR32.
 * @param   pszDesc         Pointer to description string. This must not be freed.
 * @param   ppvMapping      Where to store the address of the ring-3 mapping of
 *                          the memory.
 * @param   phRegion        Where to return the MMIO2 region handle.
 *
 */
DECLINLINE(int) PDMDevHlpPCIIORegionCreateMmio2(PPDMDEVINS pDevIns, uint32_t iPciRegion, RTGCPHYS cbRegion,
                                                PCIADDRESSSPACE enmType, const char *pszDesc,
                                                void **ppvMapping, PPGMMMIO2HANDLE phRegion)

{
    int rc = pDevIns->pHlpR3->pfnMmio2Create(pDevIns, pDevIns->apPciDevs[0], iPciRegion << 16, cbRegion, 0 /*fFlags*/,
                                             pszDesc, ppvMapping, phRegion);
    if (RT_SUCCESS(rc))
        rc = pDevIns->pHlpR3->pfnPCIIORegionRegister(pDevIns, pDevIns->apPciDevs[0], iPciRegion, cbRegion, enmType,
                                                     PDMPCIDEV_IORGN_F_MMIO2_HANDLE | PDMPCIDEV_IORGN_F_NEW_STYLE,
                                                     *phRegion, NULL /*pfnCallback*/);
    return rc;
}

/**
 * Combines PDMDevHlpMmio2Create and PDMDevHlpPCIIORegionRegisterMmio2, creating
 * and registering an MMIO2 region for the default PCI device.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance to register the ports with.
 * @param   cbRegion        The size of the region in bytes.
 * @param   iPciRegion      The PCI device region.
 * @param   enmType         PCI_ADDRESS_SPACE_MEM or
 *                          PCI_ADDRESS_SPACE_MEM_PREFETCH, optionally or-ing in
 *                          PCI_ADDRESS_SPACE_BAR64 or PCI_ADDRESS_SPACE_BAR32.
 * @param   fMmio2Flags     To be defined, must be zero.
 * @param   pfnMapUnmap     Callback for doing the mapping, optional.  The
 *                          callback will be invoked holding only the PDM lock.
 *                          The device lock will _not_ be taken (due to lock
 *                          order).
 * @param   pszDesc         Pointer to description string. This must not be freed.
 * @param   ppvMapping      Where to store the address of the ring-3 mapping of
 *                          the memory.
 * @param   phRegion        Where to return the MMIO2 region handle.
 *
 */
DECLINLINE(int) PDMDevHlpPCIIORegionCreateMmio2Ex(PPDMDEVINS pDevIns, uint32_t iPciRegion, RTGCPHYS cbRegion,
                                                  PCIADDRESSSPACE enmType, uint32_t fMmio2Flags, PFNPCIIOREGIONMAP pfnMapUnmap,
                                                  const char *pszDesc, void **ppvMapping, PPGMMMIO2HANDLE phRegion)

{
    int rc = pDevIns->pHlpR3->pfnMmio2Create(pDevIns, pDevIns->apPciDevs[0], iPciRegion << 16, cbRegion, fMmio2Flags,
                                             pszDesc, ppvMapping, phRegion);
    if (RT_SUCCESS(rc))
        rc = pDevIns->pHlpR3->pfnPCIIORegionRegister(pDevIns, pDevIns->apPciDevs[0], iPciRegion, cbRegion, enmType,
                                                     PDMPCIDEV_IORGN_F_MMIO2_HANDLE | PDMPCIDEV_IORGN_F_NEW_STYLE,
                                                     *phRegion, pfnMapUnmap);
    return rc;
}

/**
 * @copydoc PDMDEVHLPR3::pfnPCIInterceptConfigAccesses
 */
DECLINLINE(int) PDMDevHlpPCIInterceptConfigAccesses(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                    PFNPCICONFIGREAD pfnRead, PFNPCICONFIGWRITE pfnWrite)
{
    return pDevIns->pHlpR3->pfnPCIInterceptConfigAccesses(pDevIns, pPciDev, pfnRead, pfnWrite);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPCIConfigRead
 */
DECLINLINE(VBOXSTRICTRC) PDMDevHlpPCIConfigRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t uAddress,
                                                unsigned cb, uint32_t *pu32Value)
{
    return pDevIns->pHlpR3->pfnPCIConfigRead(pDevIns, pPciDev, uAddress, cb, pu32Value);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPCIConfigWrite
 */
DECLINLINE(VBOXSTRICTRC) PDMDevHlpPCIConfigWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t uAddress,
                                                 unsigned cb, uint32_t u32Value)
{
    return pDevIns->pHlpR3->pfnPCIConfigWrite(pDevIns, pPciDev, uAddress, cb, u32Value);
}

#endif /* IN_RING3 */

/**
 * Bus master physical memory read from the default PCI device.
 *
 * @returns VINF_SUCCESS or VERR_PGM_PCI_PHYS_READ_BM_DISABLED, later maybe
 *          VERR_EM_MEMORY.  The informational status shall NOT be propagated!
 * @param   pDevIns             The device instance.
 * @param   GCPhys              Physical address start reading from.
 * @param   pvBuf               Where to put the read bits.
 * @param   cbRead              How many bytes to read.
 * @thread  Any thread, but the call may involve the emulation thread.
 */
DECLINLINE(int) PDMDevHlpPCIPhysRead(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnPCIPhysRead(pDevIns, NULL, GCPhys, pvBuf, cbRead);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPCIPhysRead
 */
DECLINLINE(int) PDMDevHlpPCIPhysReadEx(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnPCIPhysRead(pDevIns, pPciDev, GCPhys, pvBuf, cbRead);
}

/**
 * Bus master physical memory write from the default PCI device.
 *
 * @returns VINF_SUCCESS or VERR_PGM_PCI_PHYS_WRITE_BM_DISABLED, later maybe
 *          VERR_EM_MEMORY.  The informational status shall NOT be propagated!
 * @param   pDevIns             The device instance.
 * @param   GCPhys              Physical address to write to.
 * @param   pvBuf               What to write.
 * @param   cbWrite             How many bytes to write.
 * @thread  Any thread, but the call may involve the emulation thread.
 */
DECLINLINE(int) PDMDevHlpPCIPhysWrite(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnPCIPhysWrite(pDevIns, NULL, GCPhys, pvBuf, cbWrite);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPCIPhysWrite
 */
DECLINLINE(int) PDMDevHlpPCIPhysWriteEx(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnPCIPhysWrite(pDevIns, pPciDev, GCPhys, pvBuf, cbWrite);
}

/**
 * Sets the IRQ for the default PCI device.
 *
 * @param   pDevIns             The device instance.
 * @param   iIrq                IRQ number to set.
 * @param   iLevel              IRQ level. See the PDM_IRQ_LEVEL_* \#defines.
 * @thread  Any thread, but will involve the emulation thread.
 */
DECLINLINE(void) PDMDevHlpPCISetIrq(PPDMDEVINS pDevIns, int iIrq, int iLevel)
{
    pDevIns->CTX_SUFF(pHlp)->pfnPCISetIrq(pDevIns, NULL, iIrq, iLevel);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPCISetIrq
 */
DECLINLINE(void) PDMDevHlpPCISetIrqEx(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel)
{
    pDevIns->CTX_SUFF(pHlp)->pfnPCISetIrq(pDevIns, pPciDev, iIrq, iLevel);
}

/**
 * Sets the IRQ for the given PCI device, but doesn't wait for EMT to process
 * the request when not called from EMT.
 *
 * @param   pDevIns             The device instance.
 * @param   iIrq                IRQ number to set.
 * @param   iLevel              IRQ level.
 * @thread  Any thread, but will involve the emulation thread.
 */
DECLINLINE(void) PDMDevHlpPCISetIrqNoWait(PPDMDEVINS pDevIns, int iIrq, int iLevel)
{
    pDevIns->CTX_SUFF(pHlp)->pfnPCISetIrq(pDevIns, NULL, iIrq, iLevel);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPCISetIrqNoWait
 */
DECLINLINE(void) PDMDevHlpPCISetIrqNoWaitEx(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq, int iLevel)
{
    pDevIns->CTX_SUFF(pHlp)->pfnPCISetIrq(pDevIns, pPciDev, iIrq, iLevel);
}

/**
 * @copydoc PDMDEVHLPR3::pfnISASetIrq
 */
DECLINLINE(void) PDMDevHlpISASetIrq(PPDMDEVINS pDevIns, int iIrq, int iLevel)
{
    pDevIns->CTX_SUFF(pHlp)->pfnISASetIrq(pDevIns, iIrq, iLevel);
}

/**
 * @copydoc PDMDEVHLPR3::pfnISASetIrqNoWait
 */
DECLINLINE(void) PDMDevHlpISASetIrqNoWait(PPDMDEVINS pDevIns, int iIrq, int iLevel)
{
    pDevIns->CTX_SUFF(pHlp)->pfnISASetIrq(pDevIns, iIrq, iLevel);
}

/**
 * @copydoc PDMDEVHLPR3::pfnIoApicSendMsi
 */
DECLINLINE(void) PDMDevHlpIoApicSendMsi(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint32_t uValue)
{
    pDevIns->CTX_SUFF(pHlp)->pfnIoApicSendMsi(pDevIns, GCPhys, uValue);
}

#ifdef IN_RING3

/**
 * @copydoc PDMDEVHLPR3::pfnDriverAttach
 */
DECLINLINE(int) PDMDevHlpDriverAttach(PPDMDEVINS pDevIns, uint32_t iLun, PPDMIBASE pBaseInterface, PPDMIBASE *ppBaseInterface, const char *pszDesc)
{
    return pDevIns->pHlpR3->pfnDriverAttach(pDevIns, iLun, pBaseInterface, ppBaseInterface, pszDesc);
}

/**
 * @copydoc PDMDEVHLPR3::pfnDriverDetach
 */
DECLINLINE(int) PDMDevHlpDriverDetach(PPDMDEVINS pDevIns, PPDMDRVINS pDrvIns, uint32_t fFlags)
{
    return pDevIns->pHlpR3->pfnDriverDetach(pDevIns, pDrvIns, fFlags);
}

/**
 * @copydoc PDMDEVHLPR3::pfnDriverReconfigure
 */
DECLINLINE(int) PDMDevHlpDriverReconfigure(PPDMDEVINS pDevIns, uint32_t iLun, uint32_t cDepth,
                                           const char * const *papszDrivers, PCFGMNODE *papConfigs, uint32_t fFlags)
{
    return pDevIns->pHlpR3->pfnDriverReconfigure(pDevIns, iLun, cDepth, papszDrivers, papConfigs, fFlags);
}

/**
 * Reconfigures with a single driver reattachement, no config, noflags.
 * @sa PDMDevHlpDriverReconfigure
 */
DECLINLINE(int) PDMDevHlpDriverReconfigure1(PPDMDEVINS pDevIns, uint32_t iLun, const char *pszDriver0)
{
    return pDevIns->pHlpR3->pfnDriverReconfigure(pDevIns, iLun, 1, &pszDriver0, NULL, 0);
}

/**
 * Reconfigures with a two drivers reattachement, no config, noflags.
 * @sa PDMDevHlpDriverReconfigure
 */
DECLINLINE(int) PDMDevHlpDriverReconfigure2(PPDMDEVINS pDevIns, uint32_t iLun, const char *pszDriver0, const char *pszDriver1)
{
    char const * apszDrivers[2];
    apszDrivers[0] = pszDriver0;
    apszDrivers[1] = pszDriver1;
    return pDevIns->pHlpR3->pfnDriverReconfigure(pDevIns, iLun, 2, apszDrivers, NULL, 0);
}

/**
 * @copydoc PDMDEVHLPR3::pfnQueueCreatePtr
 */
DECLINLINE(int) PDMDevHlpQueueCreate(PPDMDEVINS pDevIns, size_t cbItem, uint32_t cItems, uint32_t cMilliesInterval,
                                     PFNPDMQUEUEDEV pfnCallback, bool fRZEnabled, const char *pszName, PPDMQUEUE *ppQueue)
{
    return pDevIns->pHlpR3->pfnQueueCreatePtr(pDevIns, cbItem, cItems, cMilliesInterval, pfnCallback, fRZEnabled, pszName, ppQueue);
}

/**
 * @copydoc PDMDEVHLPR3::pfnQueueCreate
 */
DECLINLINE(int) PDMDevHlpQueueCreateNew(PPDMDEVINS pDevIns, size_t cbItem, uint32_t cItems, uint32_t cMilliesInterval,
                                        PFNPDMQUEUEDEV pfnCallback, bool fRZEnabled, const char *pszName, PDMQUEUEHANDLE *phQueue)
{
    return pDevIns->pHlpR3->pfnQueueCreate(pDevIns, cbItem, cItems, cMilliesInterval, pfnCallback, fRZEnabled, pszName, phQueue);
}

#endif /* IN_RING3 */

/**
 * @copydoc PDMDEVHLPR3::pfnQueueAlloc
 */
DECLINLINE(PPDMQUEUEITEMCORE) PDMDevHlpQueueAlloc(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnQueueAlloc(pDevIns, hQueue);
}

/**
 * @copydoc PDMDEVHLPR3::pfnQueueInsert
 */
DECLINLINE(void) PDMDevHlpQueueInsert(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue, PPDMQUEUEITEMCORE pItem)
{
    pDevIns->CTX_SUFF(pHlp)->pfnQueueInsert(pDevIns, hQueue, pItem);
}

/**
 * @copydoc PDMDEVHLPR3::pfnQueueInsertEx
 */
DECLINLINE(void) PDMDevHlpQueueInsertEx(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue, PPDMQUEUEITEMCORE pItem, uint64_t cNanoMaxDelay)
{
    pDevIns->CTX_SUFF(pHlp)->pfnQueueInsertEx(pDevIns, hQueue, pItem, cNanoMaxDelay);
}

/**
 * @copydoc PDMDEVHLPR3::pfnQueueFlushIfNecessary
 */
DECLINLINE(bool) PDMDevHlpQueueFlushIfNecessary(PPDMDEVINS pDevIns, PDMQUEUEHANDLE hQueue)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnQueueFlushIfNecessary(pDevIns, hQueue);
}

#ifdef IN_RING3
/**
 * @copydoc PDMDEVHLPR3::pfnTaskCreate
 */
DECLINLINE(int) PDMDevHlpTaskCreate(PPDMDEVINS pDevIns, uint32_t fFlags, const char *pszName,
                                    PFNPDMTASKDEV pfnCallback, void *pvUser, PDMTASKHANDLE *phTask)
{
    return pDevIns->pHlpR3->pfnTaskCreate(pDevIns, fFlags, pszName, pfnCallback, pvUser, phTask);
}
#endif

/**
 * @copydoc PDMDEVHLPR3::pfnTaskTrigger
 */
DECLINLINE(int) PDMDevHlpTaskTrigger(PPDMDEVINS pDevIns, PDMTASKHANDLE hTask)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTaskTrigger(pDevIns, hTask);
}

#ifdef IN_RING3

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventCreate
 */
DECLINLINE(int) PDMDevHlpSUPSemEventCreate(PPDMDEVINS pDevIns, PSUPSEMEVENT phEvent)
{
    return pDevIns->pHlpR3->pfnSUPSemEventCreate(pDevIns, phEvent);
}

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventClose
 */
DECLINLINE(int) PDMDevHlpSUPSemEventClose(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent)
{
    return pDevIns->pHlpR3->pfnSUPSemEventClose(pDevIns, hEvent);
}

#endif /* IN_RING3 */

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventSignal
 */
DECLINLINE(int) PDMDevHlpSUPSemEventSignal(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnSUPSemEventSignal(pDevIns, hEvent);
}

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventWaitNoResume
 */
DECLINLINE(int) PDMDevHlpSUPSemEventWaitNoResume(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint32_t cMillies)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnSUPSemEventWaitNoResume(pDevIns, hEvent, cMillies);
}

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventWaitNsAbsIntr
 */
DECLINLINE(int) PDMDevHlpSUPSemEventWaitNsAbsIntr(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint64_t uNsTimeout)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnSUPSemEventWaitNsAbsIntr(pDevIns, hEvent, uNsTimeout);
}

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventWaitNsRelIntr
 */
DECLINLINE(int) PDMDevHlpSUPSemEventWaitNsRelIntr(PPDMDEVINS pDevIns, SUPSEMEVENT hEvent, uint64_t cNsTimeout)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnSUPSemEventWaitNsRelIntr(pDevIns, hEvent, cNsTimeout);
}

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventGetResolution
 */
DECLINLINE(uint32_t) PDMDevHlpSUPSemEventGetResolution(PPDMDEVINS pDevIns)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnSUPSemEventGetResolution(pDevIns);
}

#ifdef IN_RING3

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventMultiCreate
 */
DECLINLINE(int) PDMDevHlpSUPSemEventMultiCreate(PPDMDEVINS pDevIns, PSUPSEMEVENTMULTI phEventMulti)
{
    return pDevIns->pHlpR3->pfnSUPSemEventMultiCreate(pDevIns, phEventMulti);
}

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventMultiClose
 */
DECLINLINE(int) PDMDevHlpSUPSemEventMultiClose(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti)
{
    return pDevIns->pHlpR3->pfnSUPSemEventMultiClose(pDevIns, hEventMulti);
}

#endif /* IN_RING3 */

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventMultiSignal
 */
DECLINLINE(int) PDMDevHlpSUPSemEventMultiSignal(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnSUPSemEventMultiSignal(pDevIns, hEventMulti);
}

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventMultiReset
 */
DECLINLINE(int) PDMDevHlpSUPSemEventMultiReset(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnSUPSemEventMultiReset(pDevIns, hEventMulti);
}

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventMultiWaitNoResume
 */
DECLINLINE(int) PDMDevHlpSUPSemEventMultiWaitNoResume(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti, uint32_t cMillies)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnSUPSemEventMultiWaitNsRelIntr(pDevIns, hEventMulti, cMillies);
}

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventMultiWaitNsAbsIntr
 */
DECLINLINE(int) PDMDevHlpSUPSemEventMultiWaitNsAbsIntr(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti, uint64_t uNsTimeout)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnSUPSemEventMultiWaitNsAbsIntr(pDevIns, hEventMulti, uNsTimeout);
}

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventMultiWaitNsRelIntr
 */
DECLINLINE(int) PDMDevHlpSUPSemEventMultiWaitNsRelIntr(PPDMDEVINS pDevIns, SUPSEMEVENTMULTI hEventMulti, uint64_t cNsTimeout)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnSUPSemEventMultiWaitNsRelIntr(pDevIns, hEventMulti, cNsTimeout);
}

/**
 * @copydoc PDMDEVHLPR3::pfnSUPSemEventMultiGetResolution
 */
DECLINLINE(uint32_t) PDMDevHlpSUPSemEventMultiGetResolution(PPDMDEVINS pDevIns)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnSUPSemEventMultiGetResolution(pDevIns);
}

#ifdef IN_RING3

/**
 * Initializes a PDM critical section.
 *
 * The PDM critical sections are derived from the IPRT critical sections, but
 * works in RC and R0 as well.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pCritSect           Pointer to the critical section.
 * @param   SRC_POS             Use RT_SRC_POS.
 * @param   pszNameFmt          Format string for naming the critical section.
 *                              For statistics and lock validation.
 * @param   ...                 Arguments for the format string.
 */
DECLINLINE(int) RT_IPRT_FORMAT_ATTR(6, 7) PDMDevHlpCritSectInit(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, RT_SRC_POS_DECL,
                                                                const char *pszNameFmt, ...)
{
    int     rc;
    va_list va;
    va_start(va, pszNameFmt);
    rc = pDevIns->pHlpR3->pfnCritSectInit(pDevIns, pCritSect, RT_SRC_POS_ARGS, pszNameFmt, va);
    va_end(va);
    return rc;
}

#endif /* IN_RING3 */

/**
 * @copydoc PDMDEVHLPR3::pfnCritSectGetNop
 */
DECLINLINE(PPDMCRITSECT) PDMDevHlpCritSectGetNop(PPDMDEVINS pDevIns)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnCritSectGetNop(pDevIns);
}

#ifdef IN_RING3

/**
 * @copydoc PDMDEVHLPR3::pfnCritSectGetNopR0
 */
DECLINLINE(R0PTRTYPE(PPDMCRITSECT)) PDMDevHlpCritSectGetNopR0(PPDMDEVINS pDevIns)
{
    return pDevIns->pHlpR3->pfnCritSectGetNopR0(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR3::pfnCritSectGetNopRC
 */
DECLINLINE(RCPTRTYPE(PPDMCRITSECT)) PDMDevHlpCritSectGetNopRC(PPDMDEVINS pDevIns)
{
    return pDevIns->pHlpR3->pfnCritSectGetNopRC(pDevIns);
}

#endif /* IN_RING3 */

/**
 * @copydoc PDMDEVHLPR3::pfnSetDeviceCritSect
 */
DECLINLINE(int) PDMDevHlpSetDeviceCritSect(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnSetDeviceCritSect(pDevIns, pCritSect);
}

/**
 * @copydoc PDMCritSectEnter
 * @param   pDevIns  The device instance.
 */
DECLINLINE(int) PDMDevHlpCritSectEnter(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnCritSectEnter(pDevIns, pCritSect, rcBusy);
}

/**
 * @copydoc PDMCritSectEnterDebug
 * @param   pDevIns  The device instance.
 */
DECLINLINE(int) PDMDevHlpCritSectEnterDebug(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, int rcBusy, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnCritSectEnterDebug(pDevIns, pCritSect, rcBusy, uId, RT_SRC_POS_ARGS);
}

/**
 * @copydoc PDMCritSectTryEnter
 * @param   pDevIns  The device instance.
 */
DECLINLINE(int)      PDMDevHlpCritSectTryEnter(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnCritSectTryEnter(pDevIns, pCritSect);
}

/**
 * @copydoc PDMCritSectTryEnterDebug
 * @param   pDevIns  The device instance.
 */
DECLINLINE(int)      PDMDevHlpCritSectTryEnterDebug(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnCritSectTryEnterDebug(pDevIns, pCritSect, uId, RT_SRC_POS_ARGS);
}

/**
 * @copydoc PDMCritSectLeave
 * @param   pDevIns  The device instance.
 */
DECLINLINE(int)      PDMDevHlpCritSectLeave(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnCritSectLeave(pDevIns, pCritSect);
}

/**
 * @copydoc PDMCritSectIsOwner
 * @param   pDevIns  The device instance.
 */
DECLINLINE(bool)     PDMDevHlpCritSectIsOwner(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnCritSectIsOwner(pDevIns, pCritSect);
}

/**
 * @copydoc PDMCritSectIsInitialized
 * @param   pDevIns  The device instance.
 */
DECLINLINE(bool)     PDMDevHlpCritSectIsInitialized(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnCritSectIsInitialized(pDevIns, pCritSect);
}

/**
 * @copydoc PDMCritSectHasWaiters
 * @param   pDevIns  The device instance.
 */
DECLINLINE(bool)     PDMDevHlpCritSectHasWaiters(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnCritSectHasWaiters(pDevIns, pCritSect);
}

/**
 * @copydoc PDMCritSectGetRecursion
 * @param   pDevIns  The device instance.
 */
DECLINLINE(uint32_t) PDMDevHlpCritSectGetRecursion(PPDMDEVINS pDevIns, PCPDMCRITSECT pCritSect)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnCritSectGetRecursion(pDevIns, pCritSect);
}

#if defined(IN_RING3) || defined(IN_RING0)
/**
 * @copydoc PDMHCCritSectScheduleExitEvent
 * @param   pDevIns  The device instance.
 */
DECLINLINE(int) PDMDevHlpCritSectScheduleExitEvent(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect, SUPSEMEVENT hEventToSignal)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnCritSectScheduleExitEvent(pDevIns, pCritSect, hEventToSignal);
}
#endif

/* Strict build: Remap the two enter calls to the debug versions. */
#ifdef VBOX_STRICT
# ifdef IPRT_INCLUDED_asm_h
#  define PDMDevHlpCritSectEnter(pDevIns, pCritSect, rcBusy) PDMDevHlpCritSectEnterDebug((pDevIns), (pCritSect), (rcBusy), (uintptr_t)ASMReturnAddress(), RT_SRC_POS)
#  define PDMDevHlpCritSectTryEnter(pDevIns, pCritSect)      PDMDevHlpCritSectTryEnterDebug((pDevIns), (pCritSect), (uintptr_t)ASMReturnAddress(), RT_SRC_POS)
# else
#  define PDMDevHlpCritSectEnter(pDevIns, pCritSect, rcBusy) PDMDevHlpCritSectEnterDebug((pDevIns), (pCritSect), (rcBusy), 0, RT_SRC_POS)
#  define PDMDevHlpCritSectTryEnter(pDevIns, pCritSect)      PDMDevHlpCritSectTryEnterDebug((pDevIns), (pCritSect), 0, RT_SRC_POS)
# endif
#endif

#if defined(IN_RING3) || defined(DOXYGEN_RUNNING)

/**
 * @copydoc PDMR3CritSectDelete
 * @param   pDevIns  The device instance.
 */
DECLINLINE(int) PDMDevHlpCritSectDelete(PPDMDEVINS pDevIns, PPDMCRITSECT pCritSect)
{
    return pDevIns->pHlpR3->pfnCritSectDelete(pDevIns, pCritSect);
}

/**
 * @copydoc PDMDEVHLPR3::pfnThreadCreate
 */
DECLINLINE(int) PDMDevHlpThreadCreate(PPDMDEVINS pDevIns, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADDEV pfnThread,
                                      PFNPDMTHREADWAKEUPDEV pfnWakeup, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    return pDevIns->pHlpR3->pfnThreadCreate(pDevIns, ppThread, pvUser, pfnThread, pfnWakeup, cbStack, enmType, pszName);
}

/**
 * @copydoc PDMR3ThreadDestroy
 * @param   pDevIns     The device instance.
 */
DECLINLINE(int) PDMDevHlpThreadDestroy(PPDMDEVINS pDevIns, PPDMTHREAD pThread, int *pRcThread)
{
    return pDevIns->pHlpR3->pfnThreadDestroy(pThread, pRcThread);
}

/**
 * @copydoc PDMR3ThreadIAmSuspending
 * @param   pDevIns     The device instance.
 */
DECLINLINE(int) PDMDevHlpThreadIAmSuspending(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    return pDevIns->pHlpR3->pfnThreadIAmSuspending(pThread);
}

/**
 * @copydoc PDMR3ThreadIAmRunning
 * @param   pDevIns     The device instance.
 */
DECLINLINE(int) PDMDevHlpThreadIAmRunning(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    return pDevIns->pHlpR3->pfnThreadIAmRunning(pThread);
}

/**
 * @copydoc PDMR3ThreadSleep
 * @param   pDevIns     The device instance.
 */
DECLINLINE(int) PDMDevHlpThreadSleep(PPDMDEVINS pDevIns, PPDMTHREAD pThread, RTMSINTERVAL cMillies)
{
    return pDevIns->pHlpR3->pfnThreadSleep(pThread, cMillies);
}

/**
 * @copydoc PDMR3ThreadSuspend
 * @param   pDevIns     The device instance.
 */
DECLINLINE(int) PDMDevHlpThreadSuspend(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    return pDevIns->pHlpR3->pfnThreadSuspend(pThread);
}

/**
 * @copydoc PDMR3ThreadResume
 * @param   pDevIns     The device instance.
 */
DECLINLINE(int) PDMDevHlpThreadResume(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    return pDevIns->pHlpR3->pfnThreadResume(pThread);
}

/**
 * @copydoc PDMDEVHLPR3::pfnSetAsyncNotification
 */
DECLINLINE(int) PDMDevHlpSetAsyncNotification(PPDMDEVINS pDevIns, PFNPDMDEVASYNCNOTIFY pfnAsyncNotify)
{
    return pDevIns->pHlpR3->pfnSetAsyncNotification(pDevIns, pfnAsyncNotify);
}

/**
 * @copydoc PDMDEVHLPR3::pfnAsyncNotificationCompleted
 */
DECLINLINE(void) PDMDevHlpAsyncNotificationCompleted(PPDMDEVINS pDevIns)
{
    pDevIns->pHlpR3->pfnAsyncNotificationCompleted(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR3::pfnA20Set
 */
DECLINLINE(void) PDMDevHlpA20Set(PPDMDEVINS pDevIns, bool fEnable)
{
    pDevIns->pHlpR3->pfnA20Set(pDevIns, fEnable);
}

/**
 * @copydoc PDMDEVHLPR3::pfnRTCRegister
 */
DECLINLINE(int) PDMDevHlpRTCRegister(PPDMDEVINS pDevIns, PCPDMRTCREG pRtcReg, PCPDMRTCHLP *ppRtcHlp)
{
    return pDevIns->pHlpR3->pfnRTCRegister(pDevIns, pRtcReg, ppRtcHlp);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPCIBusRegister
 */
DECLINLINE(int) PDMDevHlpPCIBusRegister(PPDMDEVINS pDevIns, PPDMPCIBUSREGR3 pPciBusReg, PCPDMPCIHLPR3 *ppPciHlp, uint32_t *piBus)
{
    return pDevIns->pHlpR3->pfnPCIBusRegister(pDevIns, pPciBusReg, ppPciHlp, piBus);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPICRegister
 */
DECLINLINE(int) PDMDevHlpPICRegister(PPDMDEVINS pDevIns, PPDMPICREG pPicReg, PCPDMPICHLP *ppPicHlp)
{
    return pDevIns->pHlpR3->pfnPICRegister(pDevIns, pPicReg, ppPicHlp);
}

/**
 * @copydoc PDMDEVHLPR3::pfnApicRegister
 */
DECLINLINE(int) PDMDevHlpApicRegister(PPDMDEVINS pDevIns)
{
    return pDevIns->pHlpR3->pfnApicRegister(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR3::pfnIoApicRegister
 */
DECLINLINE(int) PDMDevHlpIoApicRegister(PPDMDEVINS pDevIns, PPDMIOAPICREG pIoApicReg, PCPDMIOAPICHLP *ppIoApicHlp)
{
    return pDevIns->pHlpR3->pfnIoApicRegister(pDevIns, pIoApicReg, ppIoApicHlp);
}

/**
 * @copydoc PDMDEVHLPR3::pfnHpetRegister
 */
DECLINLINE(int) PDMDevHlpHpetRegister(PPDMDEVINS pDevIns, PPDMHPETREG pHpetReg, PCPDMHPETHLPR3 *ppHpetHlpR3)
{
    return pDevIns->pHlpR3->pfnHpetRegister(pDevIns, pHpetReg, ppHpetHlpR3);
}

/**
 * @copydoc PDMDEVHLPR3::pfnPciRawRegister
 */
DECLINLINE(int) PDMDevHlpPciRawRegister(PPDMDEVINS pDevIns, PPDMPCIRAWREG pPciRawReg, PCPDMPCIRAWHLPR3 *ppPciRawHlpR3)
{
    return pDevIns->pHlpR3->pfnPciRawRegister(pDevIns, pPciRawReg, ppPciRawHlpR3);
}

/**
 * @copydoc PDMDEVHLPR3::pfnDMACRegister
 */
DECLINLINE(int) PDMDevHlpDMACRegister(PPDMDEVINS pDevIns, PPDMDMACREG pDmacReg, PCPDMDMACHLP *ppDmacHlp)
{
    return pDevIns->pHlpR3->pfnDMACRegister(pDevIns, pDmacReg, ppDmacHlp);
}

/**
 * @copydoc PDMDEVHLPR3::pfnDMARegister
 */
DECLINLINE(int) PDMDevHlpDMARegister(PPDMDEVINS pDevIns, unsigned uChannel, PFNDMATRANSFERHANDLER pfnTransferHandler, void *pvUser)
{
    return pDevIns->pHlpR3->pfnDMARegister(pDevIns, uChannel, pfnTransferHandler, pvUser);
}

/**
 * @copydoc PDMDEVHLPR3::pfnDMAReadMemory
 */
DECLINLINE(int) PDMDevHlpDMAReadMemory(PPDMDEVINS pDevIns, unsigned uChannel, void *pvBuffer, uint32_t off, uint32_t cbBlock, uint32_t *pcbRead)
{
    return pDevIns->pHlpR3->pfnDMAReadMemory(pDevIns, uChannel, pvBuffer, off, cbBlock, pcbRead);
}

/**
 * @copydoc PDMDEVHLPR3::pfnDMAWriteMemory
 */
DECLINLINE(int) PDMDevHlpDMAWriteMemory(PPDMDEVINS pDevIns, unsigned uChannel, const void *pvBuffer, uint32_t off, uint32_t cbBlock, uint32_t *pcbWritten)
{
    return pDevIns->pHlpR3->pfnDMAWriteMemory(pDevIns, uChannel, pvBuffer, off, cbBlock, pcbWritten);
}

/**
 * @copydoc PDMDEVHLPR3::pfnDMASetDREQ
 */
DECLINLINE(int) PDMDevHlpDMASetDREQ(PPDMDEVINS pDevIns, unsigned uChannel, unsigned uLevel)
{
    return pDevIns->pHlpR3->pfnDMASetDREQ(pDevIns, uChannel, uLevel);
}

/**
 * @copydoc PDMDEVHLPR3::pfnDMAGetChannelMode
 */
DECLINLINE(uint8_t) PDMDevHlpDMAGetChannelMode(PPDMDEVINS pDevIns, unsigned uChannel)
{
    return pDevIns->pHlpR3->pfnDMAGetChannelMode(pDevIns, uChannel);
}

/**
 * @copydoc PDMDEVHLPR3::pfnDMASchedule
 */
DECLINLINE(void) PDMDevHlpDMASchedule(PPDMDEVINS pDevIns)
{
    pDevIns->pHlpR3->pfnDMASchedule(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR3::pfnCMOSWrite
 */
DECLINLINE(int) PDMDevHlpCMOSWrite(PPDMDEVINS pDevIns, unsigned iReg, uint8_t u8Value)
{
    return pDevIns->pHlpR3->pfnCMOSWrite(pDevIns, iReg, u8Value);
}

/**
 * @copydoc PDMDEVHLPR3::pfnCMOSRead
 */
DECLINLINE(int) PDMDevHlpCMOSRead(PPDMDEVINS pDevIns, unsigned iReg, uint8_t *pu8Value)
{
    return pDevIns->pHlpR3->pfnCMOSRead(pDevIns, iReg, pu8Value);
}

/**
 * @copydoc PDMDEVHLPR3::pfnCallR0
 */
DECLINLINE(int) PDMDevHlpCallR0(PPDMDEVINS pDevIns, uint32_t uOperation, uint64_t u64Arg)
{
    return pDevIns->pHlpR3->pfnCallR0(pDevIns, uOperation, u64Arg);
}

/**
 * @copydoc PDMDEVHLPR3::pfnVMGetSuspendReason
 */
DECLINLINE(VMSUSPENDREASON) PDMDevHlpVMGetSuspendReason(PPDMDEVINS pDevIns)
{
    return pDevIns->pHlpR3->pfnVMGetSuspendReason(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR3::pfnVMGetResumeReason
 */
DECLINLINE(VMRESUMEREASON) PDMDevHlpVMGetResumeReason(PPDMDEVINS pDevIns)
{
    return pDevIns->pHlpR3->pfnVMGetResumeReason(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR3::pfnGetUVM
 */
DECLINLINE(PUVM) PDMDevHlpGetUVM(PPDMDEVINS pDevIns)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnGetUVM(pDevIns);
}

#endif /* IN_RING3 || DOXYGEN_RUNNING */

#if !defined(IN_RING3) || defined(DOXYGEN_RUNNING)

/**
 * @copydoc PDMDEVHLPR0::pfnPCIBusSetUpContext
 */
DECLINLINE(int) PDMDevHlpPCIBusSetUpContext(PPDMDEVINS pDevIns, CTX_SUFF(PPDMPCIBUSREG) pPciBusReg, CTX_SUFF(PCPDMPCIHLP) *ppPciHlp)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnPCIBusSetUpContext(pDevIns, pPciBusReg, ppPciHlp);
}

/**
 * @copydoc PDMDEVHLPR0::pfnPICSetUpContext
 */
DECLINLINE(int) PDMDevHlpPICSetUpContext(PPDMDEVINS pDevIns, PPDMPICREG pPicReg, PCPDMPICHLP *ppPicHlp)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnPICSetUpContext(pDevIns, pPicReg, ppPicHlp);
}

/**
 * @copydoc PDMDEVHLPR0::pfnApicSetUpContext
 */
DECLINLINE(int) PDMDevHlpApicSetUpContext(PPDMDEVINS pDevIns)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnApicSetUpContext(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR0::pfnIoApicSetUpContext
 */
DECLINLINE(int) PDMDevHlpIoApicSetUpContext(PPDMDEVINS pDevIns, PPDMIOAPICREG pIoApicReg, PCPDMIOAPICHLP *ppIoApicHlp)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnIoApicSetUpContext(pDevIns, pIoApicReg, ppIoApicHlp);
}

/**
 * @copydoc PDMDEVHLPR0::pfnHpetSetUpContext
 */
DECLINLINE(int) PDMDevHlpHpetSetUpContext(PPDMDEVINS pDevIns, PPDMHPETREG pHpetReg, CTX_SUFF(PCPDMHPETHLP) *ppHpetHlp)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnHpetSetUpContext(pDevIns, pHpetReg, ppHpetHlp);
}

#endif /* !IN_RING3 || DOXYGEN_RUNNING */

/**
 * @copydoc PDMDEVHLPR3::pfnGetVM
 */
DECLINLINE(PVMCC) PDMDevHlpGetVM(PPDMDEVINS pDevIns)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnGetVM(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR3::pfnGetVMCPU
 */
DECLINLINE(PVMCPUCC) PDMDevHlpGetVMCPU(PPDMDEVINS pDevIns)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnGetVMCPU(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR3::pfnGetCurrentCpuId
 */
DECLINLINE(VMCPUID) PDMDevHlpGetCurrentCpuId(PPDMDEVINS pDevIns)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnGetCurrentCpuId(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTMTimeVirtGet
 */
DECLINLINE(uint64_t) PDMDevHlpTMTimeVirtGet(PPDMDEVINS pDevIns)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTMTimeVirtGet(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTMTimeVirtGetFreq
 */
DECLINLINE(uint64_t) PDMDevHlpTMTimeVirtGetFreq(PPDMDEVINS pDevIns)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTMTimeVirtGetFreq(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR3::pfnTMTimeVirtGetFreq
 */
DECLINLINE(uint64_t) PDMDevHlpTMTimeVirtGetNano(PPDMDEVINS pDevIns)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnTMTimeVirtGetNano(pDevIns);
}

#ifdef IN_RING3

/**
 * @copydoc PDMDEVHLPR3::pfnRegisterVMMDevHeap
 */
DECLINLINE(int) PDMDevHlpRegisterVMMDevHeap(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTR3PTR pvHeap, unsigned cbHeap)
{
    return pDevIns->pHlpR3->pfnRegisterVMMDevHeap(pDevIns, GCPhys, pvHeap, cbHeap);
}

/**
 * @copydoc PDMDEVHLPR3::pfnFirmwareRegister
 */
DECLINLINE(int) PDMDevHlpFirmwareRegister(PPDMDEVINS pDevIns, PCPDMFWREG pFwReg, PCPDMFWHLPR3 *ppFwHlp)
{
    return pDevIns->pHlpR3->pfnFirmwareRegister(pDevIns, pFwReg, ppFwHlp);
}

/**
 * @copydoc PDMDEVHLPR3::pfnVMReset
 */
DECLINLINE(int) PDMDevHlpVMReset(PPDMDEVINS pDevIns, uint32_t fFlags)
{
    return pDevIns->pHlpR3->pfnVMReset(pDevIns, fFlags);
}

/**
 * @copydoc PDMDEVHLPR3::pfnVMSuspend
 */
DECLINLINE(int) PDMDevHlpVMSuspend(PPDMDEVINS pDevIns)
{
    return pDevIns->pHlpR3->pfnVMSuspend(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR3::pfnVMSuspendSaveAndPowerOff
 */
DECLINLINE(int) PDMDevHlpVMSuspendSaveAndPowerOff(PPDMDEVINS pDevIns)
{
    return pDevIns->pHlpR3->pfnVMSuspendSaveAndPowerOff(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR3::pfnVMPowerOff
 */
DECLINLINE(int) PDMDevHlpVMPowerOff(PPDMDEVINS pDevIns)
{
    return pDevIns->pHlpR3->pfnVMPowerOff(pDevIns);
}

#endif /* IN_RING3 */

/**
 * @copydoc PDMDEVHLPR3::pfnA20IsEnabled
 */
DECLINLINE(bool) PDMDevHlpA20IsEnabled(PPDMDEVINS pDevIns)
{
    return pDevIns->CTX_SUFF(pHlp)->pfnA20IsEnabled(pDevIns);
}

#ifdef IN_RING3

/**
 * @copydoc PDMDEVHLPR3::pfnGetCpuId
 */
DECLINLINE(void) PDMDevHlpGetCpuId(PPDMDEVINS pDevIns, uint32_t iLeaf, uint32_t *pEax, uint32_t *pEbx, uint32_t *pEcx, uint32_t *pEdx)
{
    pDevIns->pHlpR3->pfnGetCpuId(pDevIns, iLeaf, pEax, pEbx, pEcx, pEdx);
}

/**
 * @copydoc PDMDEVHLPR3::pfnGetSupDrvSession
 */
DECLINLINE(PSUPDRVSESSION) PDMDevHlpGetSupDrvSession(PPDMDEVINS pDevIns)
{
    return pDevIns->pHlpR3->pfnGetSupDrvSession(pDevIns);
}

/**
 * @copydoc PDMDEVHLPR3::pfnQueryGenericUserObject
 */
DECLINLINE(void *) PDMDevHlpQueryGenericUserObject(PPDMDEVINS pDevIns, PCRTUUID pUuid)
{
    return pDevIns->pHlpR3->pfnQueryGenericUserObject(pDevIns, pUuid);
}

/** Wrapper around SSMR3GetU32 for simplifying getting enum values saved as uint32_t. */
# define PDMDEVHLP_SSM_GET_ENUM32_RET(a_pHlp, a_pSSM, a_enmDst, a_EnumType) \
    do { \
        uint32_t u32GetEnumTmp = 0; \
        int rcGetEnum32Tmp = (a_pHlp)->pfnSSMGetU32((a_pSSM), &u32GetEnumTmp); \
        AssertRCReturn(rcGetEnum32Tmp, rcGetEnum32Tmp); \
        (a_enmDst) = (a_EnumType)u32GetEnumTmp; \
        AssertCompile(sizeof(a_EnumType) == sizeof(u32GetEnumTmp)); \
    } while (0)

/** Wrapper around SSMR3GetU8 for simplifying getting enum values saved as uint8_t. */
# define PDMDEVHLP_SSM_GET_ENUM8_RET(a_pHlp, a_pSSM, a_enmDst, a_EnumType) \
    do { \
        uint8_t bGetEnumTmp = 0; \
        int rcGetEnum32Tmp = (a_pHlp)->pfnSSMGetU8((a_pSSM), &bGetEnumTmp); \
        AssertRCReturn(rcGetEnum32Tmp, rcGetEnum32Tmp); \
        (a_enmDst) = (a_EnumType)bGetEnumTmp; \
    } while (0)

#endif /* IN_RING3 */

/** Pointer to callbacks provided to the VBoxDeviceRegister() call. */
typedef struct PDMDEVREGCB *PPDMDEVREGCB;

/**
 * Callbacks for VBoxDeviceRegister().
 */
typedef struct PDMDEVREGCB
{
    /** Interface version.
     * This is set to PDM_DEVREG_CB_VERSION. */
    uint32_t                    u32Version;

    /**
     * Registers a device with the current VM instance.
     *
     * @returns VBox status code.
     * @param   pCallbacks      Pointer to the callback table.
     * @param   pReg            Pointer to the device registration record.
     *                          This data must be permanent and readonly.
     */
    DECLR3CALLBACKMEMBER(int, pfnRegister,(PPDMDEVREGCB pCallbacks, PCPDMDEVREG pReg));
} PDMDEVREGCB;

/** Current version of the PDMDEVREGCB structure.  */
#define PDM_DEVREG_CB_VERSION                   PDM_VERSION_MAKE(0xffe3, 1, 0)


/**
 * The VBoxDevicesRegister callback function.
 *
 * PDM will invoke this function after loading a device module and letting
 * the module decide which devices to register and how to handle conflicts.
 *
 * @returns VBox status code.
 * @param   pCallbacks      Pointer to the callback table.
 * @param   u32Version      VBox version number.
 */
typedef DECLCALLBACK(int) FNPDMVBOXDEVICESREGISTER(PPDMDEVREGCB pCallbacks, uint32_t u32Version);

/** @} */

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_vmm_pdmdev_h */
