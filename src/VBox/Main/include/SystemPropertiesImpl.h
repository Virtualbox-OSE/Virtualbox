/* $Id: SystemPropertiesImpl.h $ */

/** @file
 *
 * VirtualBox COM class implementation
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

#ifndef MAIN_INCLUDED_SystemPropertiesImpl_h
#define MAIN_INCLUDED_SystemPropertiesImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "MediumFormatImpl.h"
#include "SystemPropertiesWrap.h"


namespace settings
{
    struct SystemProperties;
}

class ATL_NO_VTABLE SystemProperties :
    public SystemPropertiesWrap
{
public:
    typedef std::list<ComObjPtr<MediumFormat> > MediumFormatList;

    DECLARE_EMPTY_CTOR_DTOR(SystemProperties)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(VirtualBox *aParent);
    void uninit();

    // public methods for internal purposes only
    // (ensure there is a caller and a read lock before calling them!)
    HRESULT i_loadSettings(const settings::SystemProperties &data);
    HRESULT i_saveSettings(settings::SystemProperties &data);

    ComObjPtr<MediumFormat> i_mediumFormat(const Utf8Str &aFormat);
    ComObjPtr<MediumFormat> i_mediumFormatFromExtension(const Utf8Str &aExt);

    int i_loadVDPlugin(const char *pszPluginLibrary);
    int i_unloadVDPlugin(const char *pszPluginLibrary);

    HRESULT i_getDefaultAdditionsISO(com::Utf8Str &aDefaultAdditionsISO);

private:

    // wrapped ISystemProperties properties
    HRESULT getMinGuestRAM(ULONG *aMinGuestRAM) RT_OVERRIDE;
    HRESULT getMaxGuestRAM(ULONG *aMaxGuestRAM) RT_OVERRIDE;
    HRESULT getMinGuestVRAM(ULONG *aMinGuestVRAM) RT_OVERRIDE;
    HRESULT getMaxGuestVRAM(ULONG *aMaxGuestVRAM) RT_OVERRIDE;
    HRESULT getMinGuestCPUCount(ULONG *aMinGuestCPUCount) RT_OVERRIDE;
    HRESULT getMaxGuestCPUCount(ULONG *aMaxGuestCPUCount) RT_OVERRIDE;
    HRESULT getMaxGuestMonitors(ULONG *aMaxGuestMonitors) RT_OVERRIDE;
    HRESULT getInfoVDSize(LONG64 *aInfoVDSize) RT_OVERRIDE;
    HRESULT getSerialPortCount(ULONG *aSerialPortCount) RT_OVERRIDE;
    HRESULT getParallelPortCount(ULONG *aParallelPortCount) RT_OVERRIDE;
    HRESULT getMaxBootPosition(ULONG *aMaxBootPosition) RT_OVERRIDE;
    HRESULT getRawModeSupported(BOOL *aRawModeSupported) RT_OVERRIDE;
    HRESULT getExclusiveHwVirt(BOOL *aExclusiveHwVirt) RT_OVERRIDE;
    HRESULT setExclusiveHwVirt(BOOL aExclusiveHwVirt) RT_OVERRIDE;
    HRESULT getDefaultMachineFolder(com::Utf8Str &aDefaultMachineFolder) RT_OVERRIDE;
    HRESULT setDefaultMachineFolder(const com::Utf8Str &aDefaultMachineFolder) RT_OVERRIDE;
    HRESULT getLoggingLevel(com::Utf8Str &aLoggingLevel) RT_OVERRIDE;
    HRESULT setLoggingLevel(const com::Utf8Str &aLoggingLevel) RT_OVERRIDE;
    HRESULT getMediumFormats(std::vector<ComPtr<IMediumFormat> > &aMediumFormats) RT_OVERRIDE;
    HRESULT getDefaultHardDiskFormat(com::Utf8Str &aDefaultHardDiskFormat) RT_OVERRIDE;
    HRESULT setDefaultHardDiskFormat(const com::Utf8Str &aDefaultHardDiskFormat) RT_OVERRIDE;
    HRESULT getFreeDiskSpaceWarning(LONG64 *aFreeDiskSpaceWarning) RT_OVERRIDE;
    HRESULT setFreeDiskSpaceWarning(LONG64 aFreeDiskSpaceWarning) RT_OVERRIDE;
    HRESULT getFreeDiskSpacePercentWarning(ULONG *aFreeDiskSpacePercentWarning) RT_OVERRIDE;
    HRESULT setFreeDiskSpacePercentWarning(ULONG aFreeDiskSpacePercentWarning) RT_OVERRIDE;
    HRESULT getFreeDiskSpaceError(LONG64 *aFreeDiskSpaceError) RT_OVERRIDE;
    HRESULT setFreeDiskSpaceError(LONG64 aFreeDiskSpaceError) RT_OVERRIDE;
    HRESULT getFreeDiskSpacePercentError(ULONG *aFreeDiskSpacePercentError) RT_OVERRIDE;
    HRESULT setFreeDiskSpacePercentError(ULONG aFreeDiskSpacePercentError) RT_OVERRIDE;
    HRESULT getVRDEAuthLibrary(com::Utf8Str &aVRDEAuthLibrary) RT_OVERRIDE;
    HRESULT setVRDEAuthLibrary(const com::Utf8Str &aVRDEAuthLibrary) RT_OVERRIDE;
    HRESULT getWebServiceAuthLibrary(com::Utf8Str &aWebServiceAuthLibrary) RT_OVERRIDE;
    HRESULT setWebServiceAuthLibrary(const com::Utf8Str &aWebServiceAuthLibrary) RT_OVERRIDE;
    HRESULT getDefaultVRDEExtPack(com::Utf8Str &aDefaultVRDEExtPack) RT_OVERRIDE;
    HRESULT setDefaultVRDEExtPack(const com::Utf8Str &aDefaultVRDEExtPack) RT_OVERRIDE;
    HRESULT getLogHistoryCount(ULONG *aLogHistoryCount) RT_OVERRIDE;
    HRESULT setLogHistoryCount(ULONG aLogHistoryCount) RT_OVERRIDE;
    HRESULT getDefaultAudioDriver(AudioDriverType_T *aDefaultAudioDriver) RT_OVERRIDE;
    HRESULT getAutostartDatabasePath(com::Utf8Str &aAutostartDatabasePath) RT_OVERRIDE;
    HRESULT setAutostartDatabasePath(const com::Utf8Str &aAutostartDatabasePath) RT_OVERRIDE;
    HRESULT getDefaultAdditionsISO(com::Utf8Str &aDefaultAdditionsISO) RT_OVERRIDE;
    HRESULT setDefaultAdditionsISO(const com::Utf8Str &aDefaultAdditionsISO) RT_OVERRIDE;
    HRESULT getDefaultFrontend(com::Utf8Str &aDefaultFrontend) RT_OVERRIDE;
    HRESULT setDefaultFrontend(const com::Utf8Str &aDefaultFrontend) RT_OVERRIDE;
    HRESULT getScreenShotFormats(std::vector<BitmapFormat_T> &aScreenShotFormats) RT_OVERRIDE;
    HRESULT getProxyMode(ProxyMode_T *pProxyMode) RT_OVERRIDE;
    HRESULT setProxyMode(ProxyMode_T aProxyMode) RT_OVERRIDE;
    HRESULT getProxyURL(com::Utf8Str &aProxyURL) RT_OVERRIDE;
    HRESULT setProxyURL(const com::Utf8Str &aProxyURL) RT_OVERRIDE;
    HRESULT getSupportedParavirtProviders(std::vector<ParavirtProvider_T> &aSupportedParavirtProviders) RT_OVERRIDE;
    HRESULT getSupportedClipboardModes(std::vector<ClipboardMode_T> &aSupportedClipboardModes) RT_OVERRIDE;
    HRESULT getSupportedDnDModes(std::vector<DnDMode_T> &aSupportedDnDModes) RT_OVERRIDE;
    HRESULT getSupportedFirmwareTypes(std::vector<FirmwareType_T> &aSupportedFirmwareTypes) RT_OVERRIDE;
    HRESULT getSupportedPointingHIDTypes(std::vector<PointingHIDType_T> &aSupportedPointingHIDTypes) RT_OVERRIDE;
    HRESULT getSupportedKeyboardHIDTypes(std::vector<KeyboardHIDType_T> &aSupportedKeyboardHIDTypes) RT_OVERRIDE;
    HRESULT getSupportedVFSTypes(std::vector<VFSType_T> &aSupportedVFSTypes) RT_OVERRIDE;
    HRESULT getSupportedImportOptions(std::vector<ImportOptions_T> &aSupportedImportOptions) RT_OVERRIDE;
    HRESULT getSupportedExportOptions(std::vector<ExportOptions_T> &aSupportedExportOptions) RT_OVERRIDE;
    HRESULT getSupportedRecordingAudioCodecs(std::vector<RecordingAudioCodec_T> &aSupportedRecordingAudioCodecs) RT_OVERRIDE;
    HRESULT getSupportedRecordingVideoCodecs(std::vector<RecordingVideoCodec_T> &aSupportedRecordingVideoCodecs) RT_OVERRIDE;
    HRESULT getSupportedRecordingVSMethods(std::vector<RecordingVideoScalingMethod_T> &aSupportedRecordingVideoScalingMethods) RT_OVERRIDE;
    HRESULT getSupportedRecordingVRCModes(std::vector<RecordingVideoRateControlMode_T> &aSupportedRecordingVideoRateControlModes) RT_OVERRIDE;
    HRESULT getSupportedGraphicsControllerTypes(std::vector<GraphicsControllerType_T> &aSupportedGraphicsControllerTypes) RT_OVERRIDE;
    HRESULT getSupportedCloneOptions(std::vector<CloneOptions_T> &aSupportedCloneOptions) RT_OVERRIDE;
    HRESULT getSupportedAutostopTypes(std::vector<AutostopType_T> &aSupportedAutostopTypes) RT_OVERRIDE;
    HRESULT getSupportedVMProcPriorities(std::vector<VMProcPriority_T> &aSupportedVMProcPriorities) RT_OVERRIDE;
    HRESULT getSupportedNetworkAttachmentTypes(std::vector<NetworkAttachmentType_T> &aSupportedNetworkAttachmentTypes) RT_OVERRIDE;
    HRESULT getSupportedNetworkAdapterTypes(std::vector<NetworkAdapterType_T> &aSupportedNetworkAdapterTypes) RT_OVERRIDE;
    HRESULT getSupportedPortModes(std::vector<PortMode_T> &aSupportedPortModes) RT_OVERRIDE;
    HRESULT getSupportedUartTypes(std::vector<UartType_T> &aSupportedUartTypes) RT_OVERRIDE;
    HRESULT getSupportedUSBControllerTypes(std::vector<USBControllerType_T> &aSupportedUSBControllerTypes) RT_OVERRIDE;
    HRESULT getSupportedAudioDriverTypes(std::vector<AudioDriverType_T> &aSupportedAudioDriverTypes) RT_OVERRIDE;
    HRESULT getSupportedAudioControllerTypes(std::vector<AudioControllerType_T> &aSupportedAudioControllerTypes) RT_OVERRIDE;
    HRESULT getSupportedStorageBuses(std::vector<StorageBus_T> &aSupportedStorageBuses) RT_OVERRIDE;
    HRESULT getSupportedStorageControllerTypes(std::vector<StorageControllerType_T> &aSupportedStorageControllerTypes) RT_OVERRIDE;
    HRESULT getSupportedChipsetTypes(std::vector<ChipsetType_T> &aSupportedChipsetTypes) RT_OVERRIDE;

    // wrapped ISystemProperties methods
    HRESULT getMaxNetworkAdapters(ChipsetType_T aChipset,
                                  ULONG *aMaxNetworkAdapters) RT_OVERRIDE;
    HRESULT getMaxNetworkAdaptersOfType(ChipsetType_T aChipset,
                                        NetworkAttachmentType_T aType,
                                        ULONG *aMaxNetworkAdapters) RT_OVERRIDE;
    HRESULT getMaxDevicesPerPortForStorageBus(StorageBus_T aBus,
                                              ULONG *aMaxDevicesPerPort) RT_OVERRIDE;
    HRESULT getMinPortCountForStorageBus(StorageBus_T aBus,
                                         ULONG *aMinPortCount) RT_OVERRIDE;
    HRESULT getMaxPortCountForStorageBus(StorageBus_T aBus,
                                         ULONG *aMaxPortCount) RT_OVERRIDE;
    HRESULT getMaxInstancesOfStorageBus(ChipsetType_T aChipset,
                                        StorageBus_T aBus,
                                        ULONG *aMaxInstances) RT_OVERRIDE;
    HRESULT getDeviceTypesForStorageBus(StorageBus_T aBus,
                                        std::vector<DeviceType_T> &aDeviceTypes) RT_OVERRIDE;
    HRESULT getStorageBusForStorageControllerType(StorageControllerType_T aStorageControllerType,
                                                  StorageBus_T *aStorageBus) RT_OVERRIDE;
    HRESULT getStorageControllerTypesForStorageBus(StorageBus_T aStorageBus,
                                                   std::vector<StorageControllerType_T> &aStorageControllerTypes) RT_OVERRIDE;
    HRESULT getDefaultIoCacheSettingForStorageController(StorageControllerType_T aControllerType,
                                                         BOOL *aEnabled) RT_OVERRIDE;
    HRESULT getStorageControllerHotplugCapable(StorageControllerType_T aControllerType,
                                               BOOL *aHotplugCapable) RT_OVERRIDE;
    HRESULT getMaxInstancesOfUSBControllerType(ChipsetType_T aChipset,
                                               USBControllerType_T aType,
                                               ULONG *aMaxInstances) RT_OVERRIDE;

    HRESULT i_getUserHomeDirectory(Utf8Str &strPath);
    HRESULT i_setDefaultMachineFolder(const Utf8Str &strPath);
    HRESULT i_setLoggingLevel(const com::Utf8Str &aLoggingLevel);
    HRESULT i_setDefaultHardDiskFormat(const com::Utf8Str &aFormat);
    HRESULT i_setVRDEAuthLibrary(const com::Utf8Str &aPath);

    HRESULT i_setWebServiceAuthLibrary(const com::Utf8Str &aPath);
    HRESULT i_setDefaultVRDEExtPack(const com::Utf8Str &aExtPack);
    HRESULT i_setAutostartDatabasePath(const com::Utf8Str &aPath);
    HRESULT i_setDefaultAdditionsISO(const com::Utf8Str &aPath);
    HRESULT i_setDefaultFrontend(const com::Utf8Str &aDefaultFrontend);

    VirtualBox * const  mParent;

    settings::SystemProperties *m;

    MediumFormatList m_llMediumFormats;

    friend class VirtualBox;
};

#endif /* !MAIN_INCLUDED_SystemPropertiesImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
