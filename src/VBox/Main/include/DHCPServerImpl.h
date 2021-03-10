/* $Id: DHCPServerImpl.h $ */
/** @file
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

#ifndef MAIN_INCLUDED_DHCPServerImpl_h
#define MAIN_INCLUDED_DHCPServerImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "DHCPServerWrap.h"
#include <map>

namespace settings
{
    struct DHCPServer;
    struct DhcpOptValue;
    typedef std::map<DHCPOption_T, DhcpOptValue> DhcpOptionMap;
}

class DHCPConfig;
class DHCPIndividualConfig;

/**
 * A DHCP server for internal host-only & NAT networks.
 *
 * Old notes:
 *
 *  for server configuration needs, it's perhaps better to use (VM,slot) pair
 *  (vm-name, slot) <----> (MAC)
 *
 *  but for client configuration, when server will have MACs at hand, it'd be
 *  easier to requiest options by MAC.
 *  (MAC) <----> (option-list)
 *
 *  Doubts: What should be done if MAC changed for (vm-name, slot), when syncing should?
 *  XML: serialization of dependecy (DHCP options) - (VM,slot) shouldn't be done via MAC in
 *  the middle.
 */
class ATL_NO_VTABLE DHCPServer
    : public DHCPServerWrap
{
public:
    /** @name Constructors and destructors
     * @{ */
    DECLARE_EMPTY_CTOR_DTOR(DHCPServer)
    HRESULT FinalConstruct();
    void    FinalRelease();

    HRESULT init(VirtualBox *aVirtualBox, const com::Utf8Str &aName);
    HRESULT init(VirtualBox *aVirtualBox, const settings::DHCPServer &data);
    void    uninit();
    /** @} */

    /** @name Public internal methods.
     * @{ */
    HRESULT i_saveSettings(settings::DHCPServer &data);
    HRESULT i_removeConfig(DHCPConfig *pConfig, DHCPConfigScope_T enmScope);
    /** @} */

private:
    /** @name IDHCPServer Properties
     * @{ */
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource) RT_OVERRIDE;
    HRESULT getEnabled(BOOL *aEnabled) RT_OVERRIDE;
    HRESULT setEnabled(BOOL aEnabled) RT_OVERRIDE;
    HRESULT getIPAddress(com::Utf8Str &aIPAddress) RT_OVERRIDE;
    HRESULT getNetworkMask(com::Utf8Str &aNetworkMask) RT_OVERRIDE;
    HRESULT getNetworkName(com::Utf8Str &aName) RT_OVERRIDE;
    HRESULT getLowerIP(com::Utf8Str &aIPAddress) RT_OVERRIDE;
    HRESULT getUpperIP(com::Utf8Str &aIPAddress) RT_OVERRIDE;
    HRESULT setConfiguration(const com::Utf8Str &aIPAddress, const com::Utf8Str &aNetworkMask,
                             const com::Utf8Str &aFromIPAddress, const com::Utf8Str &aToIPAddress) RT_OVERRIDE;
    HRESULT getGlobalConfig(ComPtr<IDHCPGlobalConfig> &aGlobalConfig) RT_OVERRIDE;
    HRESULT getGroupConfigs(std::vector<ComPtr<IDHCPGroupConfig> > &aGroupConfigs) RT_OVERRIDE;
    HRESULT getIndividualConfigs(std::vector<ComPtr<IDHCPIndividualConfig> > &aIndividualConfigs) ;
    /** @} */

    /** @name IDHCPServer Methods
     * @{ */
    HRESULT start(const com::Utf8Str &aTrunkName, const com::Utf8Str &aTrunkType) RT_OVERRIDE;
    HRESULT stop() RT_OVERRIDE;
    HRESULT restart() RT_OVERRIDE;
    HRESULT findLeaseByMAC(const com::Utf8Str &aMac, LONG aType, com::Utf8Str &aAddress, com::Utf8Str &aState,
                           LONG64 *aIssued, LONG64 *aExpire) RT_OVERRIDE;
    HRESULT getConfig(DHCPConfigScope_T aScope, const com::Utf8Str &aName, ULONG aSlot, BOOL aMayAdd,
                      ComPtr<IDHCPConfig> &aConfig) RT_OVERRIDE;
    /** @} */

    /** @name Helpers
     * @{  */
    HRESULT i_doSaveSettings();
    HRESULT i_calcLeasesConfigAndLogFilenames(const com::Utf8Str &aNetwork) RT_NOEXCEPT;
    HRESULT i_writeDhcpdConfig(const char *pszFilename, uint32_t uMACAddressVersion) RT_NOEXCEPT;

    HRESULT i_vmNameToIdAndValidateSlot(const com::Utf8Str &aVmName, LONG aSlot, com::Guid &idMachine);
    HRESULT i_vmNameAndSlotToConfig(const com::Utf8Str &a_strVmName, LONG a_uSlot, bool a_fCreateIfNeeded,
                                    ComObjPtr<DHCPIndividualConfig> &a_rPtrConfig);
    /** @} */

    /** Private data */
    struct Data;
    /** Private data. */
    Data *m;
};

#endif /* !MAIN_INCLUDED_DHCPServerImpl_h */
