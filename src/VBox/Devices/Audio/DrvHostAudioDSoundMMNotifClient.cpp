/* $Id: DrvHostAudioDSoundMMNotifClient.cpp $ */
/** @file
 * Host audio driver - DSound - Implementation of the IMMNotificationClient interface to detect audio endpoint changes.
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
 */

#include "DrvHostAudioDSoundMMNotifClient.h"

#include <iprt/win/windows.h>
#include <mmdeviceapi.h>
#include <iprt/win/endpointvolume.h>
#include <iprt/errcore.h>

#ifdef LOG_GROUP  /** @todo r=bird: wtf? Put it before all other includes like you're supposed to. */
# undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#include <VBox/log.h>


DrvHostAudioDSoundMMNotifClient::DrvHostAudioDSoundMMNotifClient(void)
    : m_fRegisteredClient(false)
    , m_cRef(1)
{
}

DrvHostAudioDSoundMMNotifClient::~DrvHostAudioDSoundMMNotifClient(void)
{
}

/**
 * Registers the mulitmedia notification client implementation.
 */
HRESULT DrvHostAudioDSoundMMNotifClient::Register(void)
{
    HRESULT hr = m_pEnum->RegisterEndpointNotificationCallback(this);
    if (SUCCEEDED(hr))
    {
        m_fRegisteredClient = true;

        hr = AttachToDefaultEndpoint();
    }

    return hr;
}

/**
 * Unregisters the mulitmedia notification client implementation.
 */
void DrvHostAudioDSoundMMNotifClient::Unregister(void)
{
    DetachFromEndpoint();

    if (m_fRegisteredClient)
    {
        m_pEnum->UnregisterEndpointNotificationCallback(this);

        m_fRegisteredClient = false;
    }
}

/**
 * Initializes the mulitmedia notification client implementation.
 *
 * @return  HRESULT
 */
HRESULT DrvHostAudioDSoundMMNotifClient::Initialize(void)
{
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), 0, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  (void **)&m_pEnum);

    LogFunc(("Returning %Rhrc\n",  hr));
    return hr;
}

/**
 * Registration callback implementation for storing our (required) contexts.
 *
 * @return  IPRT status code.
 * @param   pDrvIns             Driver instance to register the notification client to.
 * @param   pfnCallback         Audio callback to call by the notification client in case of new events.
 */
int DrvHostAudioDSoundMMNotifClient::RegisterCallback(PPDMDRVINS pDrvIns, PFNPDMHOSTAUDIOCALLBACK pfnCallback)
{
    this->m_pDrvIns     = pDrvIns;
    this->m_pfnCallback = pfnCallback;

    return VINF_SUCCESS;
}

/**
 * Unregistration callback implementation for cleaning up our mess when we're done handling
 * with notifications.
 */
void DrvHostAudioDSoundMMNotifClient::UnregisterCallback(void)
{
    this->m_pDrvIns     = NULL;
    this->m_pfnCallback = NULL;
}

/**
 * Stub being called when attaching to the default audio endpoint.
 * Does nothing at the moment.
 */
HRESULT DrvHostAudioDSoundMMNotifClient::AttachToDefaultEndpoint(void)
{
    return S_OK;
}

/**
 * Stub being called when detaching from the default audio endpoint.
 * Does nothing at the moment.
 */
void DrvHostAudioDSoundMMNotifClient::DetachFromEndpoint(void)
{

}

/**
 * Helper function for invoking the audio connector callback (if any).
 */
void DrvHostAudioDSoundMMNotifClient::doCallback(void)
{
#ifdef VBOX_WITH_AUDIO_CALLBACKS
    AssertPtr(this->m_pDrvIns);
    AssertPtr(this->m_pfnCallback);

    if (this->m_pfnCallback)
        /* Ignore rc */ this->m_pfnCallback(this->m_pDrvIns, PDMAUDIOBACKENDCBTYPE_DEVICES_CHANGED, NULL, 0);
#endif
}

/**
 * Handler implementation which is called when an audio device state
 * has been changed.
 *
 * @return  HRESULT
 * @param   pwstrDeviceId       Device ID the state is announced for.
 * @param   dwNewState          New state the device is now in.
 */
STDMETHODIMP DrvHostAudioDSoundMMNotifClient::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState)
{
    char *pszState = "unknown";

    switch (dwNewState)
    {
        case DEVICE_STATE_ACTIVE:
            pszState = "active";
            break;
        case DEVICE_STATE_DISABLED:
            pszState = "disabled";
            break;
        case DEVICE_STATE_NOTPRESENT:
            pszState = "not present";
            break;
        case DEVICE_STATE_UNPLUGGED:
            pszState = "unplugged";
            break;
        default:
            break;
    }

    LogRel(("Audio: Device '%ls' has changed state to '%s'\n", pwstrDeviceId, pszState));

    doCallback();

    return S_OK;
}

/**
 * Handler implementation which is called when a new audio device has been added.
 *
 * @return  HRESULT
 * @param   pwstrDeviceId       Device ID which has been added.
 */
STDMETHODIMP DrvHostAudioDSoundMMNotifClient::OnDeviceAdded(LPCWSTR pwstrDeviceId)
{
    LogRel(("Audio: Device '%ls' has been added\n", pwstrDeviceId));

    return S_OK;
}

/**
 * Handler implementation which is called when an audio device has been removed.
 *
 * @return  HRESULT
 * @param   pwstrDeviceId       Device ID which has been removed.
 */
STDMETHODIMP DrvHostAudioDSoundMMNotifClient::OnDeviceRemoved(LPCWSTR pwstrDeviceId)
{
    LogRel(("Audio: Device '%ls' has been removed\n", pwstrDeviceId));

    return S_OK;
}

/**
 * Handler implementation which is called when the device audio device has been
 * changed.
 *
 * @return  HRESULT
 * @param   eFlow                     Flow direction of the new default device.
 * @param   eRole                     Role of the new default device.
 * @param   pwstrDefaultDeviceId      ID of the new default device.
 */
STDMETHODIMP DrvHostAudioDSoundMMNotifClient::OnDefaultDeviceChanged(EDataFlow eFlow, ERole eRole, LPCWSTR pwstrDefaultDeviceId)
{
    RT_NOREF(eRole);

    char *pszRole = "unknown";

    if (eFlow == eRender)
        pszRole = "output";
    else if (eFlow == eCapture)
        pszRole = "input";

    LogRel(("Audio: Default %s device has been changed to '%ls'\n", pszRole, pwstrDefaultDeviceId));

    doCallback();

    return S_OK;
}

STDMETHODIMP DrvHostAudioDSoundMMNotifClient::QueryInterface(REFIID interfaceID, void **ppvInterface)
{
    const IID MY_IID_IMMNotificationClient = __uuidof(IMMNotificationClient);

    if (   IsEqualIID(interfaceID, IID_IUnknown)
        || IsEqualIID(interfaceID, MY_IID_IMMNotificationClient))
    {
        *ppvInterface = static_cast<IMMNotificationClient*>(this);
        AddRef();
        return S_OK;
    }

    *ppvInterface = NULL;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DrvHostAudioDSoundMMNotifClient::AddRef(void)
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) DrvHostAudioDSoundMMNotifClient::Release(void)
{
    long lRef = InterlockedDecrement(&m_cRef);
    if (lRef == 0)
        delete this;

    return lRef;
}

