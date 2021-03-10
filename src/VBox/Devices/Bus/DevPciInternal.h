/* $Id: DevPciInternal.h $ */
/** @file
 * DevPCI - Common Internal Header.
 */

/*
 * Copyright (C) 2010-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef VBOX_INCLUDED_SRC_Bus_DevPciInternal_h
#define VBOX_INCLUDED_SRC_Bus_DevPciInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef PDMPCIDEV_INCLUDE_PRIVATE
# define PDMPCIDEV_INCLUDE_PRIVATE /* Hack to get pdmpcidevint.h included at the right point. */
#endif
#include <VBox/vmm/pdmdev.h>


/**
 * PCI bus shared instance data (common to both PCI buses).
 *
 * The PCI device for the bus is always the first one (PDMDEVINSR3::apPciDevs[0]).
 */
typedef struct DEVPCIBUS
{
    /** Bus number. */
    uint32_t                iBus;
    /** Number of bridges attached to the bus. */
    uint32_t                cBridges;
    /** Start device number - always zero (only for DevPCI source compat). */
    uint32_t                iDevSearch;
    /** Set if PIIX3 type. */
    uint32_t                fTypePiix3 : 1;
    /** Set if ICH9 type. */
    uint32_t                fTypeIch9 : 1;
    /** Set if this is a pure bridge, i.e. not part of DEVPCIGLOBALS struct. */
    uint32_t                fPureBridge : 1;
    /** Reserved for future config flags. */
    uint32_t                uReservedConfigFlags : 29;

    /** Array of bridges attached to the bus. */
    R3PTRTYPE(PPDMPCIDEV *) papBridgesR3;
    /** Cache line align apDevices. */
    uint32_t                au32Alignment1[HC_ARCH_BITS == 32 ? 3 + 8 : 2 + 8];
    /** Array of PCI devices. We assume 32 slots, each with 8 functions. */
    R3PTRTYPE(PPDMPCIDEV)   apDevices[256];
} DEVPCIBUS;
/** Pointer to PCI bus shared instance data. */
typedef DEVPCIBUS *PDEVPCIBUS;

/**
 * PCI bus ring-3 instance data (common to both PCI buses).
 */
typedef struct DEVPCIBUSR3
{
    /** R3 pointer to the device instance. */
    PPDMDEVINSR3            pDevInsR3;
    /** Pointer to the PCI R3  helpers. */
    PCPDMPCIHLPR3           pPciHlpR3;
} DEVPCIBUSR3;
/** Pointer to PCI bus ring-3 instance data. */
typedef DEVPCIBUSR3 *PDEVPCIBUSR3;

/**
 * PCI bus ring-0 instance data (common to both PCI buses).
 */
typedef struct DEVPCIBUSR0
{
    /** R0 pointer to the device instance. */
    PPDMDEVINSR0            pDevInsR0;
    /** Pointer to the PCI R0 helpers. */
    PCPDMPCIHLPR0           pPciHlpR0;
} DEVPCIBUSR0;
/** Pointer to PCI bus ring-0 instance data. */
typedef DEVPCIBUSR0 *PDEVPCIBUSR0;

/**
 * PCI bus raw-mode instance data (common to both PCI buses).
 */
typedef struct DEVPCIBUSRC
{
    /** R0 pointer to the device instance. */
    PPDMDEVINSRC            pDevInsRC;
    /** Pointer to the PCI raw-mode helpers. */
    PCPDMPCIHLPRC           pPciHlpRC;
} DEVPCIBUSRC;
/** Pointer to PCI bus raw-mode instance data. */
typedef DEVPCIBUSRC *PDEVPCIBUSRC;

/** DEVPCIBUSR3, DEVPCIBUSR0 or DEVPCIBUSRC depending on context.  */
typedef CTX_SUFF(DEVPCIBUS)  DEVPCIBUSCC;
/** PDEVPCIBUSR3, PDEVPCIBUSR0 or PDEVPCIBUSRC depending on context.  */
typedef CTX_SUFF(PDEVPCIBUS) PDEVPCIBUSCC;


/** @def DEVPCI_APIC_IRQ_PINS
 * Number of pins for interrupts if the APIC is used.
 */
#define DEVPCI_APIC_IRQ_PINS    8
/** @def DEVPCI_LEGACY_IRQ_PINS
 * Number of pins for interrupts (PIRQ#0...PIRQ#3).
 * @remarks Labling this "legacy" might be a bit off...
 */
#define DEVPCI_LEGACY_IRQ_PINS  4


/**
 * PCI Globals - This is the host-to-pci bridge and the root bus, shared data.
 *
 * @note Only used by the root bus, not the bridges.
 */
typedef struct DEVPCIROOT
{
    /** PCI bus which is attached to the host-to-PCI bridge.
     * @note This must come first so we can share more code with the bridges!  */
    DEVPCIBUS           PciBus;

    /** I/O APIC usage flag (always true of ICH9, see constructor). */
    bool                fUseIoApic;
    /** Reserved for future config flags. */
    bool                afFutureFlags[3+4+8];
    /** Physical address of PCI config space MMIO region. */
    uint64_t            u64PciConfigMMioAddress;
    /** Length of PCI config space MMIO region. */
    uint64_t            u64PciConfigMMioLength;

    /** I/O APIC irq levels */
    volatile uint32_t   auPciApicIrqLevels[DEVPCI_APIC_IRQ_PINS];
    /** Value latched in Configuration Address Port (0CF8h) */
    uint32_t            uConfigReg;
    /** Alignment padding.   */
    uint32_t            u32Alignment1;
    /** Members only used by the PIIX3 code variant.
     * (The PCI device for the PCI-to-ISA bridge is PDMDEVINSR3::apPciDevs[1].) */
    struct
    {
        /** ACPI IRQ level */
        uint32_t            iAcpiIrqLevel;
        /** ACPI PIC IRQ */
        int32_t             iAcpiIrq;
        /** Irq levels for the four PCI Irqs.
         * These count how many devices asserted the IRQ line.  If greater 0 an IRQ
         * is sent to the guest.  If it drops to 0 the IRQ is deasserted.
         * @remarks Labling this "legacy" might be a bit off...
         */
        volatile uint32_t   auPciLegacyIrqLevels[DEVPCI_LEGACY_IRQ_PINS];
    } Piix3;

    /** The address I/O port handle. */
    IOMIOPORTHANDLE         hIoPortAddress;
    /** The data I/O port handle. */
    IOMIOPORTHANDLE         hIoPortData;
    /** The magic I/O port handle. */
    IOMIOPORTHANDLE         hIoPortMagic;
    /** The MCFG MMIO region. */
    IOMMMIOHANDLE           hMmioMcfg;

#if 1 /* Will be moved into the BIOS "soon". */
    /** Current bus number - obsolete (still used by DevPCI, but merge will fix that). */
    uint8_t             uPciBiosBus;
    uint8_t             abAlignment2[7];
    /** The next I/O port address which the PCI BIOS will use. */
    uint32_t            uPciBiosIo;
    /** The next MMIO address which the PCI BIOS will use. */
    uint32_t            uPciBiosMmio;
    /** The next 64-bit MMIO address which the PCI BIOS will use. */
    uint64_t            uPciBiosMmio64;
#endif

} DEVPCIROOT;
/** Pointer to PCI device globals. */
typedef DEVPCIROOT *PDEVPCIROOT;
/** Converts a PCI bus device instance pointer to a DEVPCIBUS pointer. */
#define DEVINS_2_DEVPCIBUS(pDevIns)     (&PDMINS_2_DATA(pDevIns, PDEVPCIROOT)->PciBus)
/** Converts a pointer to a PCI bus instance to a DEVPCIROOT pointer. */
#define DEVPCIBUS_2_DEVPCIROOT(pPciBus) RT_FROM_MEMBER(pPciBus, DEVPCIROOT, PciBus)

/** @def PCI_LOCK
 * Acquires the PDM lock. This is a NOP if locking is disabled. */
/** @def PCI_UNLOCK
 * Releases the PDM lock. This is a NOP if locking is disabled. */
#define PCI_LOCK(pDevIns, rc) \
    do { \
        int rc2 = PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC)->CTX_SUFF(pPciHlp)->pfnLock((pDevIns), rc); \
        if (rc2 != VINF_SUCCESS) \
            return rc2; \
    } while (0)
#define PCI_UNLOCK(pDevIns) \
    PDMINS_2_DATA_CC(pDevIns, PDEVPCIBUSCC)->CTX_SUFF(pPciHlp)->pfnUnlock(pDevIns)


DECLHIDDEN(PPDMDEVINS)     devpcibridgeCommonSetIrqRootWalk(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int iIrq,
                                                            PDEVPCIBUS *ppBus, uint8_t *puDevFnBridge, int *piIrqPinBridge);

#ifdef IN_RING3

DECLCALLBACK(void) devpciR3InfoPci(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs);
DECLCALLBACK(void) devpciR3InfoPciIrq(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs);
DECLCALLBACK(int)  devpciR3CommonRegisterDevice(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t fFlags,
                                                uint8_t uPciDevNo, uint8_t uPciFunNo, const char *pszName);
DECLCALLBACK(int)  devpcibridgeR3CommonRegisterDevice(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t fFlags,
                                                      uint8_t uPciDevNo, uint8_t uPciFunNo, const char *pszName);
DECLCALLBACK(int)  devpciR3CommonIORegionRegister(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                  RTGCPHYS cbRegion, PCIADDRESSSPACE enmType, uint32_t fFlags,
                                                  uint64_t hHandle, PFNPCIIOREGIONMAP pfnMapUnmap);
DECLCALLBACK(void) devpciR3CommonInterceptConfigAccesses(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                         PFNPCICONFIGREAD pfnRead, PFNPCICONFIGWRITE pfnWrite);
DECLCALLBACK(VBOXSTRICTRC) devpciR3CommonConfigRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                    uint32_t uAddress, unsigned cb, uint32_t *pu32Value);
DECLHIDDEN(VBOXSTRICTRC)   devpciR3CommonConfigReadWorker(PPDMPCIDEV pPciDev, uint32_t uAddress, unsigned cb, uint32_t *pu32Value);
DECLCALLBACK(VBOXSTRICTRC) devpciR3CommonConfigWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                     uint32_t uAddress, unsigned cb, uint32_t u32Value);
DECLHIDDEN(VBOXSTRICTRC)   devpciR3CommonConfigWriteWorker(PPDMDEVINS pDevIns, PDEVPCIBUSCC pBusCC,
                                                           PPDMPCIDEV pPciDev, uint32_t uAddress, unsigned cb, uint32_t u32Value);
void devpciR3CommonRestoreConfig(PPDMDEVINS pDevIns, PPDMPCIDEV pDev, uint8_t const *pbSrcConfig);
int  devpciR3CommonRestoreRegions(PSSMHANDLE pSSM, PPDMPCIDEV pPciDev, PPCIIOREGION paIoRegions, bool fNewState);
void devpciR3ResetDevice(PPDMDEVINS pDevIns, PPDMPCIDEV pDev);
void devpciR3BiosInitSetRegionAddress(PPDMDEVINS pDevIns, PDEVPCIBUS pBus, PPDMPCIDEV pPciDev, int iRegion, uint64_t addr);
uint32_t devpciR3GetCfg(PPDMPCIDEV pPciDev, int32_t iRegister, int cb);
void devpciR3SetCfg(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int32_t iRegister, uint32_t u32, int cb);

DECLINLINE(uint8_t) devpciR3GetByte(PPDMPCIDEV pPciDev, int32_t iRegister)
{
    return (uint8_t)devpciR3GetCfg(pPciDev, iRegister, 1);
}

DECLINLINE(uint16_t) devpciR3GetWord(PPDMPCIDEV pPciDev, int32_t iRegister)
{
    return (uint16_t)devpciR3GetCfg(pPciDev, iRegister, 2);
}

DECLINLINE(uint32_t) devpciR3GetDWord(PPDMPCIDEV pPciDev, int32_t iRegister)
{
    return (uint32_t)devpciR3GetCfg(pPciDev, iRegister, 4);
}

DECLINLINE(void) devpciR3SetByte(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int32_t iRegister, uint8_t u8)
{
    devpciR3SetCfg(pDevIns, pPciDev, iRegister, u8, 1);
}

DECLINLINE(void) devpciR3SetWord(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int32_t iRegister, uint16_t u16)
{
    devpciR3SetCfg(pDevIns, pPciDev, iRegister, u16, 2);
}

DECLINLINE(void) devpciR3SetDWord(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, int32_t iRegister, uint32_t u32)
{
    devpciR3SetCfg(pDevIns, pPciDev, iRegister, u32, 4);
}

#endif /* IN_RING3 */

#endif /* !VBOX_INCLUDED_SRC_Bus_DevPciInternal_h */

