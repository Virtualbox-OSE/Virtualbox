/* $Id: DrvHostAudioCoreAudio.cpp $ */
/** @file
 * Host audio driver - Mac OS X CoreAudio.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#include <VBox/log.h>
#include <VBox/vmm/pdmaudioinline.h>
#include <VBox/vmm/pdmaudiohostenuminline.h>

#include "VBoxDD.h"

#include <iprt/asm.h>
#include <iprt/cdefs.h>
#include <iprt/circbuf.h>
#include <iprt/mem.h>

#include <iprt/uuid.h>

#include <CoreAudio/CoreAudio.h>
#include <CoreServices/CoreServices.h>
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioConverter.h>
#include <AudioToolbox/AudioToolbox.h>



/* Enables utilizing the Core Audio converter unit for converting
 * input / output from/to our requested formats. That might be more
 * performant than using our own routines later down the road. */
/** @todo Needs more investigation and testing first before enabling. */
//# define VBOX_WITH_AUDIO_CA_CONVERTER

/** @todo
 * - Maybe make sure the threads are immediately stopped if playing/recording stops.
 */

/*
 * Most of this is based on:
 * http://developer.apple.com/mac/library/technotes/tn2004/tn2097.html
 * http://developer.apple.com/mac/library/technotes/tn2002/tn2091.html
 * http://developer.apple.com/mac/library/qa/qa2007/qa1533.html
 * http://developer.apple.com/mac/library/qa/qa2001/qa1317.html
 * http://developer.apple.com/mac/library/documentation/AudioUnit/Reference/AUComponentServicesReference/Reference/reference.html
 */

/* Prototypes needed for COREAUDIODEVICE. */
struct DRVHOSTCOREAUDIO;
typedef struct DRVHOSTCOREAUDIO *PDRVHOSTCOREAUDIO;

/**
 * Core Audio-specific device entry.
 *
 * @note This is definitely not safe to just copy!
 */
typedef struct COREAUDIODEVICEDATA
{
    /** The core PDM structure. */
    PDMAUDIOHOSTDEV      Core;

    /** Pointer to driver instance this device is bound to. */
    PDRVHOSTCOREAUDIO   pDrv;
    /** The audio device ID of the currently used device (UInt32 typedef). */
    AudioDeviceID       deviceID;
    /** The device' "UUID".
     * @todo r=bird: We leak this.  Header say we must CFRelease it. */
    CFStringRef         UUID;
    /** List of attached (native) Core Audio streams attached to this device. */
    RTLISTANCHOR        lstStreams;
} COREAUDIODEVICEDATA;
typedef COREAUDIODEVICEDATA *PCOREAUDIODEVICEDATA;

/**
 * Host Coreaudio driver instance data.
 * @implements PDMIAUDIOCONNECTOR
 */
typedef struct DRVHOSTCOREAUDIO
{
    /** Pointer to the driver instance structure. */
    PPDMDRVINS              pDrvIns;
    /** Pointer to host audio interface. */
    PDMIHOSTAUDIO           IHostAudio;
    /** Critical section to serialize access. */
    RTCRITSECT              CritSect;
    /** Current (last reported) device enumeration. */
    PDMAUDIOHOSTENUM        Devices;
    /** Pointer to the currently used input device in the device enumeration.
     *  Can be NULL if none assigned. */
    PCOREAUDIODEVICEDATA    pDefaultDevIn;
    /** Pointer to the currently used output device in the device enumeration.
     *  Can be NULL if none assigned. */
    PCOREAUDIODEVICEDATA    pDefaultDevOut;
#ifdef VBOX_WITH_AUDIO_CALLBACKS
    /** Callback function to the upper driver.
     *  Can be NULL if not being used / registered. */
    PFNPDMHOSTAUDIOCALLBACK pfnCallback;
#endif
} DRVHOSTCOREAUDIO, *PDRVHOSTCOREAUDIO;

/** Converts a pointer to DRVHOSTCOREAUDIO::IHostAudio to a PDRVHOSTCOREAUDIO. */
#define PDMIHOSTAUDIO_2_DRVHOSTCOREAUDIO(pInterface) RT_FROM_MEMBER(pInterface, DRVHOSTCOREAUDIO, IHostAudio)

/**
 * Structure for holding a Core Audio unit
 * and its data.
 */
typedef struct COREAUDIOUNIT
{
    /** Pointer to the device this audio unit is bound to.
     *  Can be NULL if not bound to a device (anymore). */
    PCOREAUDIODEVICEDATA        pDevice;
    /** The actual audio unit object. */
    AudioUnit                   audioUnit;
    /** Stream description for using with VBox:
     *  - When using this audio unit for input (capturing), this format states
     *    the unit's output format.
     *  - When using this audio unit for output (playback), this format states
     *    the unit's input format. */
    AudioStreamBasicDescription streamFmt;
} COREAUDIOUNIT, *PCOREAUDIOUNIT;


DECLHIDDEN(int) coreAudioInputPermissionCheck(void);


/*********************************************************************************************************************************
*   Helper function section                                                                                                      *
*********************************************************************************************************************************/

/* Move these down below the internal function prototypes... */

static void coreAudioPrintASBD(const char *pszDesc, const AudioStreamBasicDescription *pASBD)
{
    char pszSampleRate[32];
    LogRel2(("CoreAudio: %s description:\n", pszDesc));
    LogRel2(("CoreAudio:\tFormat ID: %RU32 (%c%c%c%c)\n", pASBD->mFormatID,
             RT_BYTE4(pASBD->mFormatID), RT_BYTE3(pASBD->mFormatID),
             RT_BYTE2(pASBD->mFormatID), RT_BYTE1(pASBD->mFormatID)));
    LogRel2(("CoreAudio:\tFlags: %RU32", pASBD->mFormatFlags));
    if (pASBD->mFormatFlags & kAudioFormatFlagIsFloat)
        LogRel2((" Float"));
    if (pASBD->mFormatFlags & kAudioFormatFlagIsBigEndian)
        LogRel2((" BigEndian"));
    if (pASBD->mFormatFlags & kAudioFormatFlagIsSignedInteger)
        LogRel2((" SignedInteger"));
    if (pASBD->mFormatFlags & kAudioFormatFlagIsPacked)
        LogRel2((" Packed"));
    if (pASBD->mFormatFlags & kAudioFormatFlagIsAlignedHigh)
        LogRel2((" AlignedHigh"));
    if (pASBD->mFormatFlags & kAudioFormatFlagIsNonInterleaved)
        LogRel2((" NonInterleaved"));
    if (pASBD->mFormatFlags & kAudioFormatFlagIsNonMixable)
        LogRel2((" NonMixable"));
    if (pASBD->mFormatFlags & kAudioFormatFlagsAreAllClear)
        LogRel2((" AllClear"));
    LogRel2(("\n"));
    snprintf(pszSampleRate, 32, "%.2f", (float)pASBD->mSampleRate); /** @todo r=andy Use RTStrPrint*. */
    LogRel2(("CoreAudio:\tSampleRate      : %s\n", pszSampleRate));
    LogRel2(("CoreAudio:\tChannelsPerFrame: %RU32\n", pASBD->mChannelsPerFrame));
    LogRel2(("CoreAudio:\tFramesPerPacket : %RU32\n", pASBD->mFramesPerPacket));
    LogRel2(("CoreAudio:\tBitsPerChannel  : %RU32\n", pASBD->mBitsPerChannel));
    LogRel2(("CoreAudio:\tBytesPerFrame   : %RU32\n", pASBD->mBytesPerFrame));
    LogRel2(("CoreAudio:\tBytesPerPacket  : %RU32\n", pASBD->mBytesPerPacket));
}

static void coreAudioPCMPropsToASBD(PDMAUDIOPCMPROPS *pPCMProps, AudioStreamBasicDescription *pASBD)
{
    AssertPtrReturnVoid(pPCMProps);
    AssertPtrReturnVoid(pASBD);

    RT_BZERO(pASBD, sizeof(AudioStreamBasicDescription));

    pASBD->mFormatID         = kAudioFormatLinearPCM;
    pASBD->mFormatFlags      = kAudioFormatFlagIsPacked;
    pASBD->mFramesPerPacket  = 1; /* For uncompressed audio, set this to 1. */
    pASBD->mSampleRate       = (Float64)pPCMProps->uHz;
    pASBD->mChannelsPerFrame = pPCMProps->cChannels;
    pASBD->mBitsPerChannel   = pPCMProps->cbSample * 8;
    if (pPCMProps->fSigned)
        pASBD->mFormatFlags |= kAudioFormatFlagIsSignedInteger;
    pASBD->mBytesPerFrame    = pASBD->mChannelsPerFrame * (pASBD->mBitsPerChannel / 8);
    pASBD->mBytesPerPacket   = pASBD->mFramesPerPacket * pASBD->mBytesPerFrame;
}

#ifndef VBOX_WITH_AUDIO_CALLBACKS
static int coreAudioASBDToStreamCfg(AudioStreamBasicDescription *pASBD, PPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pASBD, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pCfg,  VERR_INVALID_PARAMETER);

    pCfg->Props.cChannels = pASBD->mChannelsPerFrame;
    pCfg->Props.uHz       = (uint32_t)pASBD->mSampleRate;
    AssertMsg(!(pASBD->mBitsPerChannel & 7), ("%u\n", pASBD->mBitsPerChannel));
    pCfg->Props.cbSample  = pASBD->mBitsPerChannel / 8;
    pCfg->Props.fSigned   = RT_BOOL(pASBD->mFormatFlags & kAudioFormatFlagIsSignedInteger);
    pCfg->Props.cShift    = PDMAUDIOPCMPROPS_MAKE_SHIFT_PARMS(pCfg->Props.cbSample, pCfg->Props.cChannels);
    /** @todo r=bird: pCfg->Props.fSwapEndian is not initialized here!  */

    return VINF_SUCCESS;
}
#endif /* !VBOX_WITH_AUDIO_CALLBACKS */

#if 0 /* unused */
static int coreAudioCFStringToCString(const CFStringRef pCFString, char **ppszString)
{
    CFIndex cLen = CFStringGetLength(pCFString) + 1;
    char *pszResult = (char *)RTMemAllocZ(cLen * sizeof(char));
    if (!CFStringGetCString(pCFString, pszResult, cLen, kCFStringEncodingUTF8))
    {
        RTMemFree(pszResult);
        return VERR_NOT_FOUND;
    }

    *ppszString = pszResult;
    return VINF_SUCCESS;
}

static AudioDeviceID coreAudioDeviceUIDtoID(const char* pszUID)
{
    /* Create a CFString out of our CString. */
    CFStringRef strUID = CFStringCreateWithCString(NULL, pszUID, kCFStringEncodingMacRoman);

    /* Fill the translation structure. */
    AudioDeviceID deviceID;

    AudioValueTranslation translation;
    translation.mInputData      = &strUID;
    translation.mInputDataSize  = sizeof(CFStringRef);
    translation.mOutputData     = &deviceID;
    translation.mOutputDataSize = sizeof(AudioDeviceID);

    /* Fetch the translation from the UID to the device ID. */
    AudioObjectPropertyAddress propAdr = { kAudioHardwarePropertyDeviceForUID, kAudioObjectPropertyScopeGlobal,
                                           kAudioObjectPropertyElementMaster };

    UInt32 uSize = sizeof(AudioValueTranslation);
    OSStatus err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propAdr, 0, NULL, &uSize, &translation);

    /* Release the temporary CFString */
    CFRelease(strUID);

    if (RT_LIKELY(err == noErr))
        return deviceID;

    /* Return the unknown device on error. */
    return kAudioDeviceUnknown;
}
#endif /* unused */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** @name Initialization status indicator used for the recreation of the AudioUnits.
 *
 * Global structures section
 *
 ******************************************************************************/

/**
 * Enumeration for a Core Audio stream status.
 */
typedef enum COREAUDIOSTATUS
{
    /** The device is uninitialized. */
    COREAUDIOSTATUS_UNINIT  = 0,
    /** The device is currently initializing. */
    COREAUDIOSTATUS_IN_INIT,
    /** The device is initialized. */
    COREAUDIOSTATUS_INIT,
    /** The device is currently uninitializing. */
    COREAUDIOSTATUS_IN_UNINIT,
#ifndef VBOX_WITH_AUDIO_CALLBACKS
    /** The device has to be reinitialized.
     *  Note: Only needed if VBOX_WITH_AUDIO_CALLBACKS is not defined, as otherwise
     *        the Audio Connector will take care of this as soon as this backend
     *        tells it to do so via the provided audio callback. */
    COREAUDIOSTATUS_REINIT,
#endif
    /** The usual 32-bit hack. */
    COREAUDIOSTATUS_32BIT_HACK = 0x7fffffff
} COREAUDIOSTATUS, *PCOREAUDIOSTATUS;

#ifdef VBOX_WITH_AUDIO_CA_CONVERTER
 /* Error code which indicates "End of data" */
 static const OSStatus caConverterEOFDErr = 0x656F6664; /* 'eofd' */
#endif

/* Prototypes needed for COREAUDIOSTREAMCBCTX. */
struct COREAUDIOSTREAM;
typedef struct COREAUDIOSTREAM *PCOREAUDIOSTREAM;

/**
 * Structure for keeping a conversion callback context.
 * This is needed when using an audio converter during input/output processing.
 */
typedef struct COREAUDIOCONVCBCTX
{
    /** Pointer to the stream this context is bound to. */
    PCOREAUDIOSTREAM             pStream;
    /** Source stream description. */
    AudioStreamBasicDescription  asbdSrc;
    /** Destination stream description. */
    AudioStreamBasicDescription  asbdDst;
    /** Pointer to native buffer list used for rendering the source audio data into. */
    AudioBufferList             *pBufLstSrc;
    /** Total packet conversion count. */
    UInt32                       uPacketCnt;
    /** Current packet conversion index. */
    UInt32                       uPacketIdx;
    /** Error count, for limiting the logging. */
    UInt32                       cErrors;
} COREAUDIOCONVCBCTX, *PCOREAUDIOCONVCBCTX;

/**
 * Structure for keeping the input stream specifics.
 */
typedef struct COREAUDIOSTREAMIN
{
#ifdef VBOX_WITH_AUDIO_CA_CONVERTER
    /** The audio converter if necessary. NULL if no converter is being used. */
    AudioConverterRef           ConverterRef;
    /** Callback context for the audio converter. */
    COREAUDIOCONVCBCTX          convCbCtx;
#endif
    /** The ratio between the device & the stream sample rate. */
    Float64                     sampleRatio;
} COREAUDIOSTREAMIN, *PCOREAUDIOSTREAMIN;

/**
 * Structure for keeping the output stream specifics.
 */
typedef struct COREAUDIOSTREAMOUT
{
    /** Nothing here yet. */
} COREAUDIOSTREAMOUT, *PCOREAUDIOSTREAMOUT;

/**
 * Structure for maintaining a Core Audio stream.
 */
typedef struct COREAUDIOSTREAM
{
    /** The stream's acquired configuration. */
    PPDMAUDIOSTREAMCFG          pCfg;
    /** Stream-specific data, depending on the stream type. */
    union
    {
        COREAUDIOSTREAMIN       In;
        COREAUDIOSTREAMOUT      Out;
    };
    /** List node for the device's stream list. */
    RTLISTNODE                  Node;
    /** Pointer to driver instance this stream is bound to. */
    PDRVHOSTCOREAUDIO           pDrv;
    /** The stream's thread handle for maintaining the audio queue. */
    RTTHREAD                    hThread;
    /** Flag indicating to start a stream's data processing. */
    bool                        fRun;
    /** Whether the stream is in a running (active) state or not.
     *  For playback streams this means that audio data can be (or is being) played,
     *  for capturing streams this means that audio data is being captured (if available). */
    bool                        fIsRunning;
    /** Thread shutdown indicator. */
    bool                        fShutdown;
    /** Critical section for serializing access between thread + callbacks. */
    RTCRITSECT                  CritSect;
    /** The actual audio queue being used. */
    AudioQueueRef               audioQueue;
    /** The audio buffers which are used with the above audio queue. */
    AudioQueueBufferRef         audioBuffer[2];
    /** The acquired (final) audio format for this stream. */
    AudioStreamBasicDescription asbdStream;
    /** The audio unit for this stream. */
    COREAUDIOUNIT               Unit;
    /** Initialization status tracker, actually COREAUDIOSTATUS.
     * Used when some of the device parameters or the device itself is changed
     * during the runtime. */
    volatile uint32_t           enmStatus;
    /** An internal ring buffer for transferring data from/to the rendering callbacks. */
    PRTCIRCBUF                  pCircBuf;
} COREAUDIOSTREAM, *PCOREAUDIOSTREAM;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int coreAudioStreamInit(PCOREAUDIOSTREAM pCAStream, PDRVHOSTCOREAUDIO pThis, PCOREAUDIODEVICEDATA pDev);
#ifndef VBOX_WITH_AUDIO_CALLBACKS
static int coreAudioStreamReinit(PDRVHOSTCOREAUDIO pThis, PCOREAUDIOSTREAM pCAStream, PCOREAUDIODEVICEDATA pDev);
#endif
static int coreAudioStreamUninit(PCOREAUDIOSTREAM pCAStream);

static int coreAudioStreamControl(PDRVHOSTCOREAUDIO pThis, PCOREAUDIOSTREAM pCAStream, PDMAUDIOSTREAMCMD enmStreamCmd);

static void coreAudioDeviceDataInit(PCOREAUDIODEVICEDATA pDevData, AudioDeviceID deviceID, bool fIsInput, PDRVHOSTCOREAUDIO pDrv);

static DECLCALLBACK(OSStatus) coreAudioDevPropChgCb(AudioObjectID propertyID, UInt32 nAddresses,
                                                    const AudioObjectPropertyAddress properties[], void *pvUser);

static int coreAudioStreamInitQueue(PCOREAUDIOSTREAM pCAStream, PPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq);
static DECLCALLBACK(void) coreAudioInputQueueCb(void *pvUser, AudioQueueRef audioQueue, AudioQueueBufferRef audioBuffer,
                                                const AudioTimeStamp *pAudioTS, UInt32 cPacketDesc,
                                                const AudioStreamPacketDescription *paPacketDesc);
static DECLCALLBACK(void) coreAudioOutputQueueCb(void *pvUser, AudioQueueRef audioQueue, AudioQueueBufferRef audioBuffer);


#ifdef VBOX_WITH_AUDIO_CA_CONVERTER

/**
 * Initializes a conversion callback context.
 *
 * @return  IPRT status code.
 * @param   pConvCbCtx          Conversion callback context to initialize.
 * @param   pStream             Pointer to stream to use.
 * @param   pASBDSrc            Input (source) stream description to use.
 * @param   pASBDDst            Output (destination) stream description to use.
 */
static int coreAudioInitConvCbCtx(PCOREAUDIOCONVCBCTX pConvCbCtx, PCOREAUDIOSTREAM pStream,
                                  AudioStreamBasicDescription *pASBDSrc, AudioStreamBasicDescription *pASBDDst)
{
    AssertPtrReturn(pConvCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pStream,    VERR_INVALID_POINTER);
    AssertPtrReturn(pASBDSrc,   VERR_INVALID_POINTER);
    AssertPtrReturn(pASBDDst,   VERR_INVALID_POINTER);

# ifdef DEBUG
    coreAudioPrintASBD("CbCtx: Src", pASBDSrc);
    coreAudioPrintASBD("CbCtx: Dst", pASBDDst);
# endif

    pConvCbCtx->pStream = pStream;

    memcpy(&pConvCbCtx->asbdSrc, pASBDSrc, sizeof(AudioStreamBasicDescription));
    memcpy(&pConvCbCtx->asbdDst, pASBDDst, sizeof(AudioStreamBasicDescription));

    pConvCbCtx->pBufLstSrc = NULL;
    pConvCbCtx->cErrors    = 0;

    return VINF_SUCCESS;
}


/**
 * Uninitializes a conversion callback context.
 *
 * @return  IPRT status code.
 * @param   pConvCbCtx          Conversion callback context to uninitialize.
 */
static void coreAudioUninitConvCbCtx(PCOREAUDIOCONVCBCTX pConvCbCtx)
{
    AssertPtrReturnVoid(pConvCbCtx);

    pConvCbCtx->pStream = NULL;

    RT_ZERO(pConvCbCtx->asbdSrc);
    RT_ZERO(pConvCbCtx->asbdDst);

    pConvCbCtx->pBufLstSrc = NULL;
    pConvCbCtx->cErrors    = 0;
}

#endif /* VBOX_WITH_AUDIO_CA_CONVERTER */

/**
 * Does a (re-)enumeration of the host's playback + recording devices.
 *
 * @return  IPRT status code.
 * @param   pThis               Host audio driver instance.
 * @param   enmUsage            Which devices to enumerate.
 * @param   pDevEnm             Where to store the enumerated devices.
 */
static int coreAudioDevicesEnumerate(PDRVHOSTCOREAUDIO pThis, PDMAUDIODIR enmUsage, PPDMAUDIOHOSTENUM pDevEnm)
{
    AssertPtrReturn(pThis,   VERR_INVALID_POINTER);
    AssertPtrReturn(pDevEnm, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    do /* (this is not a loop, just a device for avoid gotos while trying not to shoot oneself in the foot too badly.) */
    {
        AudioDeviceID defaultDeviceID = kAudioDeviceUnknown;

        /* Fetch the default audio device currently in use. */
        AudioObjectPropertyAddress propAdrDefaultDev = {   enmUsage == PDMAUDIODIR_IN
                                                         ? kAudioHardwarePropertyDefaultInputDevice
                                                         : kAudioHardwarePropertyDefaultOutputDevice,
                                                         kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster };
        UInt32 uSize = sizeof(defaultDeviceID);
        OSStatus err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propAdrDefaultDev, 0, NULL, &uSize, &defaultDeviceID);
        if (err != noErr)
        {
            LogRel(("CoreAudio: Unable to determine default %s device (%RI32)\n",
                    enmUsage == PDMAUDIODIR_IN ? "capturing" : "playback", err));
            return VERR_NOT_FOUND;
        }

        if (defaultDeviceID == kAudioDeviceUnknown)
        {
            LogFunc(("No default %s device found\n", enmUsage == PDMAUDIODIR_IN ? "capturing" : "playback"));
            /* Keep going. */
        }

        AudioObjectPropertyAddress propAdrDevList = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                                      kAudioObjectPropertyElementMaster };

        err = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propAdrDevList, 0, NULL, &uSize);
        if (err != kAudioHardwareNoError)
            break;

        AudioDeviceID *pDevIDs = (AudioDeviceID *)alloca(uSize);
        if (pDevIDs == NULL)
            break;

        err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propAdrDevList, 0, NULL, &uSize, pDevIDs);
        if (err != kAudioHardwareNoError)
            break;

        PDMAudioHostEnumInit(pDevEnm);

        UInt16 cDevices = uSize / sizeof(AudioDeviceID);

        PCOREAUDIODEVICEDATA pDev = NULL;
        for (UInt16 i = 0; i < cDevices; i++)
        {
            if (pDev) /* Some (skipped) device to clean up first? */
                PDMAudioHostDevFree(&pDev->Core);

            pDev = (PCOREAUDIODEVICEDATA)PDMAudioHostDevAlloc(sizeof(*pDev));
            if (!pDev)
            {
                rc = VERR_NO_MEMORY;
                break;
            }

            /* Set usage. */
            pDev->Core.enmUsage = enmUsage;

            /* Init backend-specific device data. */
            coreAudioDeviceDataInit(pDev, pDevIDs[i], enmUsage == PDMAUDIODIR_IN, pThis);

            /* Check if the device is valid. */
            AudioDeviceID curDevID = pDev->deviceID;

            /* Is the device the default device? */
            if (curDevID == defaultDeviceID)
                pDev->Core.fFlags |= PDMAUDIOHOSTDEV_F_DEFAULT;

            AudioObjectPropertyAddress propAddrCfg = { kAudioDevicePropertyStreamConfiguration,
                                                         enmUsage == PDMAUDIODIR_IN
                                                       ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput,
                                                       kAudioObjectPropertyElementMaster };

            err = AudioObjectGetPropertyDataSize(curDevID, &propAddrCfg, 0, NULL, &uSize);
            if (err != noErr)
                continue;

            AudioBufferList *pBufList = (AudioBufferList *)RTMemAlloc(uSize);
            if (!pBufList)
                continue;

            err = AudioObjectGetPropertyData(curDevID, &propAddrCfg, 0, NULL, &uSize, pBufList);
            if (err == noErr)
            {
                for (UInt32 a = 0; a < pBufList->mNumberBuffers; a++)
                {
                    if (enmUsage == PDMAUDIODIR_IN)
                        pDev->Core.cMaxInputChannels  += pBufList->mBuffers[a].mNumberChannels;
                    else if (enmUsage == PDMAUDIODIR_OUT)
                        pDev->Core.cMaxOutputChannels += pBufList->mBuffers[a].mNumberChannels;
                }
            }

            RTMemFree(pBufList);
            pBufList = NULL;

            /* Check if the device is valid, e.g. has any input/output channels according to its usage. */
            if (   enmUsage == PDMAUDIODIR_IN
                && !pDev->Core.cMaxInputChannels)
                continue;
            if (   enmUsage == PDMAUDIODIR_OUT
                && !pDev->Core.cMaxOutputChannels)
                continue;

            /* Resolve the device's name. */
            AudioObjectPropertyAddress propAddrName = { kAudioObjectPropertyName,
                                                          enmUsage == PDMAUDIODIR_IN
                                                        ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput,
                                                        kAudioObjectPropertyElementMaster };
            uSize = sizeof(CFStringRef);
            CFStringRef pcfstrName = NULL;

            err = AudioObjectGetPropertyData(curDevID, &propAddrName, 0, NULL, &uSize, &pcfstrName);
            if (err != kAudioHardwareNoError)
                continue;

            CFIndex cbName = CFStringGetMaximumSizeForEncoding(CFStringGetLength(pcfstrName), kCFStringEncodingUTF8) + 1;
            if (cbName)
            {
                char *pszName = (char *)RTStrAlloc(cbName);
                if (   pszName
                    && CFStringGetCString(pcfstrName, pszName, cbName, kCFStringEncodingUTF8))
                    RTStrCopy(pDev->Core.szName, sizeof(pDev->Core.szName), pszName);

                LogFunc(("Device '%s': %RU32\n", pszName, curDevID));

                if (pszName)
                {
                    RTStrFree(pszName);
                    pszName = NULL;
                }
            }

            CFRelease(pcfstrName);

            /* Check if the device is alive for the intended usage. */
            AudioObjectPropertyAddress propAddrAlive = { kAudioDevicePropertyDeviceIsAlive,
                                                          enmUsage == PDMAUDIODIR_IN
                                                        ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput,
                                                        kAudioObjectPropertyElementMaster };

            UInt32 uAlive = 0;
            uSize = sizeof(uAlive);

            err = AudioObjectGetPropertyData(curDevID, &propAddrAlive, 0, NULL, &uSize, &uAlive);
            if (   (err == noErr)
                && !uAlive)
            {
                pDev->Core.fFlags |= PDMAUDIOHOSTDEV_F_DEAD;
            }

            /* Check if the device is being hogged by someone else. */
            AudioObjectPropertyAddress propAddrHogged = { kAudioDevicePropertyHogMode,
                                                          kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster };

            pid_t pid = 0;
            uSize = sizeof(pid);

            err = AudioObjectGetPropertyData(curDevID, &propAddrHogged, 0, NULL, &uSize, &pid);
            if (   (err == noErr)
                && (pid != -1))
            {
                pDev->Core.fFlags |= PDMAUDIOHOSTDEV_F_LOCKED;
            }

            /* Add the device to the enumeration. */
            PDMAudioHostEnumAppend(pDevEnm, &pDev->Core);

            /* NULL device pointer because it's now part of the device enumeration. */
            pDev = NULL;
        }

        if (RT_FAILURE(rc))
        {
            PDMAudioHostDevFree(&pDev->Core);
            pDev = NULL;
        }

    } while (0);

    if (RT_SUCCESS(rc))
    {
#ifdef LOG_ENABLED
        LogFunc(("Devices for pDevEnm=%p, enmUsage=%RU32:\n", pDevEnm, enmUsage));
        PDMAudioHostEnumLog(pDevEnm, "Core Audio");
#endif
    }
    else
        PDMAudioHostEnumDelete(pDevEnm);

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Checks if an audio device with a specific device ID is in the given device
 * enumeration or not.
 *
 * @retval  true if the node is the last element in the list.
 * @retval  false otherwise.
 *
 * @param   pEnmSrc               Device enumeration to search device ID in.
 * @param   deviceID              Device ID to search.
 */
bool coreAudioDevicesHasDevice(PPDMAUDIOHOSTENUM pEnmSrc, AudioDeviceID deviceID)
{
    PCOREAUDIODEVICEDATA pDevSrc;
    RTListForEach(&pEnmSrc->LstDevices, pDevSrc, COREAUDIODEVICEDATA, Core.ListEntry)
    {
        if (pDevSrc->deviceID == deviceID)
            return true;
    }

    return false;
}


/**
 * Enumerates all host devices and builds a final device enumeration list, consisting
 * of (duplex) input and output devices.
 *
 * @return  IPRT status code.
 * @param   pThis               Host audio driver instance.
 * @param   pEnmDst             Where to store the device enumeration list.
 */
int coreAudioDevicesEnumerateAll(PDRVHOSTCOREAUDIO pThis, PPDMAUDIOHOSTENUM pEnmDst)
{
    PDMAUDIOHOSTENUM devEnmIn;
    int rc = coreAudioDevicesEnumerate(pThis, PDMAUDIODIR_IN, &devEnmIn);
    if (RT_SUCCESS(rc))
    {
        PDMAUDIOHOSTENUM devEnmOut;
        rc = coreAudioDevicesEnumerate(pThis, PDMAUDIODIR_OUT, &devEnmOut);
        if (RT_SUCCESS(rc))
        {
            /*
             * Build up the final device enumeration, based on the input and output device lists
             * just enumerated.
             *
             * Also make sure to handle duplex devices, that is, devices which act as input and output
             * at the same time.
             */
            PDMAudioHostEnumInit(pEnmDst);
            PCOREAUDIODEVICEDATA pDevSrcIn;
            RTListForEach(&devEnmIn.LstDevices, pDevSrcIn, COREAUDIODEVICEDATA, Core.ListEntry)
            {
                PCOREAUDIODEVICEDATA pDevDst = (PCOREAUDIODEVICEDATA)PDMAudioHostDevAlloc(sizeof(*pDevDst));
                if (!pDevDst)
                {
                    rc = VERR_NO_MEMORY;
                    break;
                }

                coreAudioDeviceDataInit(pDevDst, pDevSrcIn->deviceID, true /* fIsInput */, pThis);

                RTStrCopy(pDevDst->Core.szName, sizeof(pDevDst->Core.szName), pDevSrcIn->Core.szName);

                pDevDst->Core.enmUsage          = PDMAUDIODIR_IN; /* Input device by default (simplex). */
                pDevDst->Core.cMaxInputChannels = pDevSrcIn->Core.cMaxInputChannels;

                /* Handle flags. */
                if (pDevSrcIn->Core.fFlags & PDMAUDIOHOSTDEV_F_DEFAULT)
                    pDevDst->Core.fFlags |= PDMAUDIOHOSTDEV_F_DEFAULT;
                /** @todo Handle hot plugging? */

                /*
                 * Now search through the list of all found output devices and check if we found
                 * an output device with the same device ID as the currently handled input device.
                 *
                 * If found, this means we have to treat that device as a duplex device then.
                 */
                PCOREAUDIODEVICEDATA pDevSrcOut;
                RTListForEach(&devEnmOut.LstDevices, pDevSrcOut, COREAUDIODEVICEDATA, Core.ListEntry)
                {
                    if (pDevSrcIn->deviceID == pDevSrcOut->deviceID)
                    {
                        pDevDst->Core.enmUsage           = PDMAUDIODIR_DUPLEX;
                        pDevDst->Core.cMaxOutputChannels = pDevSrcOut->Core.cMaxOutputChannels;

                        if (pDevSrcOut->Core.fFlags & PDMAUDIOHOSTDEV_F_DEFAULT)
                            pDevDst->Core.fFlags |= PDMAUDIOHOSTDEV_F_DEFAULT;
                        break;
                    }
                }

                if (RT_SUCCESS(rc))
                    PDMAudioHostEnumAppend(pEnmDst, &pDevDst->Core);
                else
                {
                    PDMAudioHostDevFree(&pDevDst->Core);
                    pDevDst = NULL;
                }
            }

            if (RT_SUCCESS(rc))
            {
                /*
                 * As a last step, add all remaining output devices which have not been handled in the loop above,
                 * that is, all output devices which operate in simplex mode.
                 */
                PCOREAUDIODEVICEDATA pDevSrcOut;
                RTListForEach(&devEnmOut.LstDevices, pDevSrcOut, COREAUDIODEVICEDATA, Core.ListEntry)
                {
                    if (coreAudioDevicesHasDevice(pEnmDst, pDevSrcOut->deviceID))
                        continue; /* Already in our list, skip. */

                    PCOREAUDIODEVICEDATA pDevDst = (PCOREAUDIODEVICEDATA)PDMAudioHostDevAlloc(sizeof(*pDevDst));
                    if (!pDevDst)
                    {
                        rc = VERR_NO_MEMORY;
                        break;
                    }

                    coreAudioDeviceDataInit(pDevDst, pDevSrcOut->deviceID, false /* fIsInput */, pThis);

                    RTStrCopy(pDevDst->Core.szName, sizeof(pDevDst->Core.szName), pDevSrcOut->Core.szName);

                    pDevDst->Core.enmUsage           = PDMAUDIODIR_OUT;
                    pDevDst->Core.cMaxOutputChannels = pDevSrcOut->Core.cMaxOutputChannels;

                    pDevDst->deviceID       = pDevSrcOut->deviceID;

                    /* Handle flags. */
                    if (pDevSrcOut->Core.fFlags & PDMAUDIOHOSTDEV_F_DEFAULT)
                        pDevDst->Core.fFlags |= PDMAUDIOHOSTDEV_F_DEFAULT;
                    /** @todo Handle hot plugging? */

                    PDMAudioHostEnumAppend(pEnmDst, &pDevDst->Core);
                }
            }

            if (RT_FAILURE(rc))
                PDMAudioHostEnumDelete(pEnmDst);

            PDMAudioHostEnumDelete(&devEnmOut);
        }

        PDMAudioHostEnumDelete(&devEnmIn);
    }

#ifdef LOG_ENABLED
    if (RT_SUCCESS(rc))
        PDMAudioHostEnumLog(pEnmDst, "Core Audio (Final)");
#endif

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Initializes a Core Audio-specific device data structure.
 *
 * @returns IPRT status code.
 * @param   pDevData            Device data structure to initialize.
 * @param   deviceID            Core Audio device ID to assign this structure to.
 * @param   fIsInput            Whether this is an input device or not.
 * @param   pDrv                Driver instance to use.
 */
static void coreAudioDeviceDataInit(PCOREAUDIODEVICEDATA pDevData, AudioDeviceID deviceID, bool fIsInput, PDRVHOSTCOREAUDIO pDrv)
{
    AssertPtrReturnVoid(pDevData);
    AssertPtrReturnVoid(pDrv);

    pDevData->deviceID = deviceID;
    pDevData->pDrv     = pDrv;

    /* Get the device UUID. */
    AudioObjectPropertyAddress propAdrDevUUID = { kAudioDevicePropertyDeviceUID,
                                                  fIsInput ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput,
                                                  kAudioObjectPropertyElementMaster };
    UInt32 uSize = sizeof(pDevData->UUID);
    OSStatus err = AudioObjectGetPropertyData(pDevData->deviceID, &propAdrDevUUID, 0, NULL, &uSize, &pDevData->UUID);
    if (err != noErr)
        LogRel(("CoreAudio: Failed to retrieve device UUID for device %RU32 (%RI32)\n", deviceID, err));

    RTListInit(&pDevData->lstStreams);
}


/**
 * Propagates an audio device status to all its connected Core Audio streams.
 *
 * @return IPRT status code.
 * @param  pDev                 Audio device to propagate status for.
 * @param  enmSts               Status to propagate.
 */
static int coreAudioDevicePropagateStatus(PCOREAUDIODEVICEDATA pDev, COREAUDIOSTATUS enmSts)
{
    AssertPtrReturn(pDev, VERR_INVALID_POINTER);


    /* Sanity. */
    AssertPtr(pDev->pDrv);

    LogFlowFunc(("pDev=%p enmSts=%RU32\n", pDev, enmSts));

    PCOREAUDIOSTREAM pCAStream;
    RTListForEach(&pDev->lstStreams, pCAStream, COREAUDIOSTREAM, Node)
    {
        LogFlowFunc(("pCAStream=%p\n", pCAStream));

        /* We move the reinitialization to the next output event.
         * This make sure this thread isn't blocked and the
         * reinitialization is done when necessary only. */
        ASMAtomicXchgU32(&pCAStream->enmStatus, enmSts);
    }

    return VINF_SUCCESS;
}


static DECLCALLBACK(OSStatus) coreAudioDeviceStateChangedCb(AudioObjectID propertyID,
                                                            UInt32 nAddresses,
                                                            const AudioObjectPropertyAddress properties[],
                                                            void *pvUser)
{
    RT_NOREF(propertyID, nAddresses, properties);

    LogFlowFunc(("propertyID=%u, nAddresses=%u, pvUser=%p\n", propertyID, nAddresses, pvUser));

    PCOREAUDIODEVICEDATA pDev = (PCOREAUDIODEVICEDATA)pvUser;
    AssertPtr(pDev);

    PDRVHOSTCOREAUDIO pThis = pDev->pDrv;
    AssertPtr(pThis);

    int rc2 = RTCritSectEnter(&pThis->CritSect);
    AssertRC(rc2);

    UInt32 uAlive = 1;
    UInt32 uSize  = sizeof(UInt32);

    AudioObjectPropertyAddress propAdr = { kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal,
                                           kAudioObjectPropertyElementMaster };

    AudioDeviceID deviceID = pDev->deviceID;

    OSStatus err = AudioObjectGetPropertyData(deviceID, &propAdr, 0, NULL, &uSize, &uAlive);

    bool fIsDead = false;

    if (err == kAudioHardwareBadDeviceError)
        fIsDead = true; /* Unplugged. */
    else if ((err == kAudioHardwareNoError) && (!RT_BOOL(uAlive)))
        fIsDead = true; /* Something else happened. */

    if (fIsDead)
    {
        LogRel2(("CoreAudio: Device '%s' stopped functioning\n", pDev->Core.szName));

        /* Mark device as dead. */
        rc2 = coreAudioDevicePropagateStatus(pDev, COREAUDIOSTATUS_UNINIT);
        AssertRC(rc2);
    }

    rc2 = RTCritSectLeave(&pThis->CritSect);
    AssertRC(rc2);

    return noErr;
}

/* Callback for getting notified when the default recording/playback device has been changed. */
/** @todo r=bird: Why DECLCALLBACK? */
static DECLCALLBACK(OSStatus) coreAudioDefaultDeviceChangedCb(AudioObjectID propertyID,
                                                              UInt32 nAddresses,
                                                              const AudioObjectPropertyAddress properties[],
                                                              void *pvUser)
{
    RT_NOREF(propertyID, nAddresses);

    LogFlowFunc(("propertyID=%u, nAddresses=%u, pvUser=%p\n", propertyID, nAddresses, pvUser));

    PDRVHOSTCOREAUDIO pThis = (PDRVHOSTCOREAUDIO)pvUser;
    AssertPtr(pThis);

    int rc2 = RTCritSectEnter(&pThis->CritSect);
    AssertRC(rc2);

    for (UInt32 idxAddress = 0; idxAddress < nAddresses; idxAddress++)
    {
        PCOREAUDIODEVICEDATA pDev = NULL;

        /*
         * Check if the default input / output device has been changed.
         */
        const AudioObjectPropertyAddress *pProperty = &properties[idxAddress];
        switch (pProperty->mSelector)
        {
            case kAudioHardwarePropertyDefaultInputDevice:
                LogFlowFunc(("kAudioHardwarePropertyDefaultInputDevice\n"));
                pDev = pThis->pDefaultDevIn;
                break;

            case kAudioHardwarePropertyDefaultOutputDevice:
                LogFlowFunc(("kAudioHardwarePropertyDefaultOutputDevice\n"));
                pDev = pThis->pDefaultDevOut;
                break;

            default:
                /* Skip others. */
                break;
        }

        LogFlowFunc(("pDev=%p\n", pDev));

#ifndef VBOX_WITH_AUDIO_CALLBACKS
        if (pDev)
        {
            /* This listener is called on every change of the hardware
             * device. So check if the default device has really changed. */
            UInt32 uSize = sizeof(AudioDeviceID);
            UInt32 uResp = 0;

            OSStatus err = AudioObjectGetPropertyData(kAudioObjectSystemObject, pProperty, 0, NULL, &uSize, &uResp);
            if (err == noErr)
            {
                if (pDev->deviceID != uResp) /* Has the device ID changed? */
                {
                    rc2 = coreAudioDevicePropagateStatus(pDev, COREAUDIOSTATUS_REINIT);
                    AssertRC(rc2);
                }
            }
        }
#endif /* VBOX_WITH_AUDIO_CALLBACKS */
    }

#ifdef VBOX_WITH_AUDIO_CALLBACKS
    PFNPDMHOSTAUDIOCALLBACK pfnCallback = pThis->pfnCallback;
#endif

    /* Make sure to leave the critical section before calling the callback. */
    rc2 = RTCritSectLeave(&pThis->CritSect);
    AssertRC(rc2);

#ifdef VBOX_WITH_AUDIO_CALLBACKS
    if (pfnCallback)
        pfnCallback(pThis->pDrvIns, PDMAUDIOBACKENDCBTYPE_DEVICES_CHANGED, NULL, 0); /* Ignore rc */
#endif

    return noErr;
}

#ifndef VBOX_WITH_AUDIO_CALLBACKS

/**
 * Re-initializes a Core Audio stream with a specific audio device and stream configuration.
 *
 * @return IPRT status code.
 * @param  pThis                Driver instance.
 * @param  pCAStream            Audio stream to re-initialize.
 * @param  pDev                 Audio device to use for re-initialization.
 * @param  pCfg                 Stream configuration to use for re-initialization.
 */
static int coreAudioStreamReinitEx(PDRVHOSTCOREAUDIO pThis, PCOREAUDIOSTREAM pCAStream,
                                   PCOREAUDIODEVICEDATA pDev, PPDMAUDIOSTREAMCFG pCfg)
{
    LogFunc(("pCAStream=%p\n", pCAStream));

    int rc = coreAudioStreamUninit(pCAStream);
    if (RT_SUCCESS(rc))
    {
        rc = coreAudioStreamInit(pCAStream, pThis, pDev);
        if (RT_SUCCESS(rc))
        {
            rc = coreAudioStreamInitQueue(pCAStream, pCfg /* pCfgReq */, NULL /* pCfgAcq */);
            if (RT_SUCCESS(rc))
                rc = coreAudioStreamControl(pCAStream->pDrv, pCAStream, PDMAUDIOSTREAMCMD_ENABLE);

            if (RT_FAILURE(rc))
            {
                int rc2 = coreAudioStreamUninit(pCAStream);
                AssertRC(rc2);
            }
        }
    }

    if (RT_FAILURE(rc))
        LogRel(("CoreAudio: Unable to re-init stream: %Rrc\n", rc));

    return rc;
}

/**
 * Re-initializes a Core Audio stream with a specific audio device.
 *
 * @return IPRT status code.
 * @param  pThis                Driver instance.
 * @param  pCAStream            Audio stream to re-initialize.
 * @param  pDev                 Audio device to use for re-initialization.
 */
static int coreAudioStreamReinit(PDRVHOSTCOREAUDIO pThis, PCOREAUDIOSTREAM pCAStream, PCOREAUDIODEVICEDATA pDev)
{
    int rc = coreAudioStreamUninit(pCAStream);
    if (RT_SUCCESS(rc))
    {
        /* Use the acquired stream configuration from the former initialization to
         * re-initialize the stream. */
        PDMAUDIOSTREAMCFG CfgAcq;
        rc = coreAudioASBDToStreamCfg(&pCAStream->Unit.streamFmt, &CfgAcq);
        if (RT_SUCCESS(rc))
            rc = coreAudioStreamReinitEx(pThis, pCAStream, pDev, &CfgAcq);
    }

    return rc;
}

#endif /* !VBOX_WITH_AUDIO_CALLBACKS */

#ifdef VBOX_WITH_AUDIO_CA_CONVERTER
/* Callback to convert audio input data from one format to another. */
static DECLCALLBACK(OSStatus) coreAudioConverterCb(AudioConverterRef              inAudioConverter,
                                                   UInt32                        *ioNumberDataPackets,
                                                   AudioBufferList               *ioData,
                                                   AudioStreamPacketDescription **ppASPD,
                                                   void                          *pvUser)
{
    RT_NOREF(inAudioConverter);

    AssertPtrReturn(ioNumberDataPackets, caConverterEOFDErr);
    AssertPtrReturn(ioData,              caConverterEOFDErr);

    PCOREAUDIOCONVCBCTX pConvCbCtx = (PCOREAUDIOCONVCBCTX)pvUser;
    AssertPtr(pConvCbCtx);

    /* Initialize values. */
    ioData->mBuffers[0].mNumberChannels = 0;
    ioData->mBuffers[0].mDataByteSize   = 0;
    ioData->mBuffers[0].mData           = NULL;

    if (ppASPD)
    {
        Log3Func(("Handling packet description not implemented\n"));
    }
    else
    {
        /** @todo Check converter ID? */

        /** @todo Handled non-interleaved data by going through the full buffer list,
         *        not only through the first buffer like we do now. */
        Log3Func(("ioNumberDataPackets=%RU32\n", *ioNumberDataPackets));

        UInt32 cNumberDataPackets = *ioNumberDataPackets;
        Assert(pConvCbCtx->uPacketIdx + cNumberDataPackets <= pConvCbCtx->uPacketCnt);

        if (cNumberDataPackets)
        {
            AssertPtr(pConvCbCtx->pBufLstSrc);
            Assert(pConvCbCtx->pBufLstSrc->mNumberBuffers == 1); /* Only one buffer for the source supported atm. */

            AudioStreamBasicDescription *pSrcASBD = &pConvCbCtx->asbdSrc;
            AudioBuffer                 *pSrcBuf  = &pConvCbCtx->pBufLstSrc->mBuffers[0];

            size_t cbOff   = pConvCbCtx->uPacketIdx * pSrcASBD->mBytesPerPacket;

            cNumberDataPackets = RT_MIN((pSrcBuf->mDataByteSize - cbOff) / pSrcASBD->mBytesPerPacket,
                                        cNumberDataPackets);

            void  *pvAvail = (uint8_t *)pSrcBuf->mData + cbOff;
            size_t cbAvail = RT_MIN(pSrcBuf->mDataByteSize - cbOff, cNumberDataPackets * pSrcASBD->mBytesPerPacket);

            Log3Func(("cNumberDataPackets=%RU32, cbOff=%zu, cbAvail=%zu\n", cNumberDataPackets, cbOff, cbAvail));

            /* Set input data for the converter to use.
             * Note: For VBR (Variable Bit Rates) or interleaved data handling we need multiple buffers here. */
            ioData->mNumberBuffers = 1;

            ioData->mBuffers[0].mNumberChannels = pSrcBuf->mNumberChannels;
            ioData->mBuffers[0].mDataByteSize   = cbAvail;
            ioData->mBuffers[0].mData           = pvAvail;

#ifdef VBOX_AUDIO_DEBUG_DUMP_PCM_DATA
            RTFILE fh;
            int rc = RTFileOpen(&fh,VBOX_AUDIO_DEBUG_DUMP_PCM_DATA_PATH "caConverterCbInput.pcm",
                                RTFILE_O_OPEN_CREATE | RTFILE_O_APPEND | RTFILE_O_WRITE | RTFILE_O_DENY_NONE);
            if (RT_SUCCESS(rc))
            {
                RTFileWrite(fh, pvAvail, cbAvail, NULL);
                RTFileClose(fh);
            }
            else
                AssertFailed();
#endif
            pConvCbCtx->uPacketIdx += cNumberDataPackets;
            Assert(pConvCbCtx->uPacketIdx <= pConvCbCtx->uPacketCnt);

            *ioNumberDataPackets = cNumberDataPackets;
        }
    }

    Log3Func(("%RU32 / %RU32 -> ioNumberDataPackets=%RU32\n",
              pConvCbCtx->uPacketIdx, pConvCbCtx->uPacketCnt, *ioNumberDataPackets));

    return noErr;
}
#endif /* VBOX_WITH_AUDIO_CA_CONVERTER */


/**
 * Initializes a Core Audio stream.
 *
 * @return IPRT status code.
 * @param  pThis                Driver instance.
 * @param  pCAStream            Stream to initialize.
 * @param  pDev                 Audio device to use for this stream.
 */
static int coreAudioStreamInit(PCOREAUDIOSTREAM pCAStream, PDRVHOSTCOREAUDIO pThis, PCOREAUDIODEVICEDATA pDev)
{
    AssertPtrReturn(pCAStream, VERR_INVALID_POINTER);
    AssertPtrReturn(pThis,     VERR_INVALID_POINTER);
    AssertPtrReturn(pDev,      VERR_INVALID_POINTER);

    Assert(pCAStream->Unit.pDevice == NULL); /* Make sure no device is assigned yet. */
    Assert(pDev->Core.cbSelf == sizeof(COREAUDIODEVICEDATA));

    LogFunc(("pCAStream=%p, pDev=%p ('%s', ID=%RU32)\n", pCAStream, pDev, pDev->Core.szName, pDev->deviceID));

    pCAStream->Unit.pDevice = pDev;
    pCAStream->pDrv = pThis;

    return VINF_SUCCESS;
}

# define CA_BREAK_STMT(stmt) \
    stmt; \
    break;

/**
 * Thread for a Core Audio stream's audio queue handling.
 *
 * This thread is required per audio queue to pump data to/from the Core Audio
 * stream and handling its callbacks.
 *
 * @returns IPRT status code.
 * @param   hThreadSelf         Thread handle.
 * @param   pvUser              User argument.
 */
static DECLCALLBACK(int) coreAudioQueueThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf);

    PCOREAUDIOSTREAM pCAStream = (PCOREAUDIOSTREAM)pvUser;
    AssertPtr(pCAStream);
    AssertPtr(pCAStream->pCfg);

    const bool fIn = pCAStream->pCfg->enmDir == PDMAUDIODIR_IN;

    LogFunc(("Thread started for pCAStream=%p, fIn=%RTbool\n", pCAStream, fIn));

    /*
     * Create audio queue.
     */
    OSStatus err;
    if (fIn)
        err = AudioQueueNewInput(&pCAStream->asbdStream, coreAudioInputQueueCb, pCAStream /* pvData */,
                                 CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, 0, &pCAStream->audioQueue);
    else
        err = AudioQueueNewOutput(&pCAStream->asbdStream, coreAudioOutputQueueCb, pCAStream /* pvData */,
                                  CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, 0, &pCAStream->audioQueue);

    if (err != noErr)
        return VERR_GENERAL_FAILURE; /** @todo Fudge! */

    /*
     * Assign device to queue.
     */
    PCOREAUDIODEVICEDATA pDev = (PCOREAUDIODEVICEDATA)pCAStream->Unit.pDevice;
    AssertPtr(pDev);

    UInt32 uSize = sizeof(pDev->UUID);
    err = AudioQueueSetProperty(pCAStream->audioQueue, kAudioQueueProperty_CurrentDevice, &pDev->UUID, uSize);
    if (err != noErr)
        return VERR_GENERAL_FAILURE; /** @todo Fudge! */

    const size_t cbBufSize = PDMAudioPropsFramesToBytes(&pCAStream->pCfg->Props, pCAStream->pCfg->Backend.cFramesPeriod);

    /*
     * Allocate audio buffers.
     */
    for (size_t i = 0; i < RT_ELEMENTS(pCAStream->audioBuffer); i++)
    {
        err = AudioQueueAllocateBuffer(pCAStream->audioQueue, cbBufSize, &pCAStream->audioBuffer[i]);
        if (err != noErr)
            break;
    }

    if (err != noErr)
        return VERR_GENERAL_FAILURE; /** @todo Fudge! */

    /* Signal the main thread before entering the main loop. */
    RTThreadUserSignal(RTThreadSelf());

    /*
     * Enter the main loop.
     */
    while (!ASMAtomicReadBool(&pCAStream->fShutdown))
    {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.10, 1);
    }

    /*
     * Cleanup.
     */
    if (fIn)
    {
        AudioQueueStop(pCAStream->audioQueue, 1);
    }
    else
    {
        AudioQueueStop(pCAStream->audioQueue, 0);
    }

    for (size_t i = 0; i < RT_ELEMENTS(pCAStream->audioBuffer); i++)
    {
        if (pCAStream->audioBuffer[i])
            AudioQueueFreeBuffer(pCAStream->audioQueue, pCAStream->audioBuffer[i]);
    }

    AudioQueueDispose(pCAStream->audioQueue, 1);

    LogFunc(("Thread ended for pCAStream=%p, fIn=%RTbool\n", pCAStream, fIn));
    return VINF_SUCCESS;
}

/**
 * Processes input data of an audio queue buffer and stores it into a Core Audio stream.
 *
 * @returns IPRT status code.
 * @param   pCAStream           Core Audio stream to store input data into.
 * @param   audioBuffer         Audio buffer to process input data from.
 */
int coreAudioInputQueueProcBuffer(PCOREAUDIOSTREAM pCAStream, AudioQueueBufferRef audioBuffer)
{
    PRTCIRCBUF pCircBuf = pCAStream->pCircBuf;
    AssertPtr(pCircBuf);

    UInt8 *pvSrc = (UInt8 *)audioBuffer->mAudioData;
    UInt8 *pvDst = NULL;

    size_t cbWritten = 0;

    size_t cbToWrite = audioBuffer->mAudioDataByteSize;
    size_t cbLeft    = RT_MIN(cbToWrite, RTCircBufFree(pCircBuf));

    while (cbLeft)
    {
        /* Try to acquire the necessary block from the ring buffer. */
        RTCircBufAcquireWriteBlock(pCircBuf, cbLeft, (void **)&pvDst, &cbToWrite);

        if (!cbToWrite)
            break;

        /* Copy the data from our ring buffer to the core audio buffer. */
        memcpy((UInt8 *)pvDst, pvSrc + cbWritten, cbToWrite);

        /* Release the read buffer, so it could be used for new data. */
        RTCircBufReleaseWriteBlock(pCircBuf, cbToWrite);

        cbWritten += cbToWrite;

        Assert(cbLeft >= cbToWrite);
        cbLeft -= cbToWrite;
    }

    Log3Func(("pCAStream=%p, cbBuffer=%RU32/%zu, cbWritten=%zu\n",
              pCAStream, audioBuffer->mAudioDataByteSize, audioBuffer->mAudioDataBytesCapacity, cbWritten));

    return VINF_SUCCESS;
}

/**
 * Input audio queue callback. Called whenever input data from the audio queue becomes available.
 *
 * @param   pvUser              User argument.
 * @param   audioQueue          Audio queue to process input data from.
 * @param   audioBuffer         Audio buffer to process input data from. Must be part of audio queue.
 * @param   pAudioTS            Audio timestamp.
 * @param   cPacketDesc         Number of packet descriptors.
 * @param   paPacketDesc        Array of packet descriptors.
 */
static DECLCALLBACK(void) coreAudioInputQueueCb(void *pvUser, AudioQueueRef audioQueue, AudioQueueBufferRef audioBuffer,
                                                const AudioTimeStamp *pAudioTS,
                                                UInt32 cPacketDesc, const AudioStreamPacketDescription *paPacketDesc)
{
    RT_NOREF(audioQueue, pAudioTS, cPacketDesc, paPacketDesc);

    PCOREAUDIOSTREAM pCAStream = (PCOREAUDIOSTREAM)pvUser;
    AssertPtr(pCAStream);

    int rc = RTCritSectEnter(&pCAStream->CritSect);
    AssertRC(rc);

    rc = coreAudioInputQueueProcBuffer(pCAStream, audioBuffer);
    if (RT_SUCCESS(rc))
        AudioQueueEnqueueBuffer(audioQueue, audioBuffer, 0, NULL);

    rc = RTCritSectLeave(&pCAStream->CritSect);
    AssertRC(rc);
}

/**
 * Processes output data of a Core Audio stream into an audio queue buffer.
 *
 * @returns IPRT status code.
 * @param   pCAStream           Core Audio stream to process output data for.
 * @param   audioBuffer         Audio buffer to store data into.
 */
int coreAudioOutputQueueProcBuffer(PCOREAUDIOSTREAM pCAStream, AudioQueueBufferRef audioBuffer)
{
    AssertPtr(pCAStream);

    PRTCIRCBUF pCircBuf = pCAStream->pCircBuf;
    AssertPtr(pCircBuf);

    size_t cbRead = 0;

    UInt8 *pvSrc = NULL;
    UInt8 *pvDst = (UInt8 *)audioBuffer->mAudioData;

    size_t cbToRead = RT_MIN(RTCircBufUsed(pCircBuf), audioBuffer->mAudioDataBytesCapacity);
    size_t cbLeft   = cbToRead;

    while (cbLeft)
    {
        /* Try to acquire the necessary block from the ring buffer. */
        RTCircBufAcquireReadBlock(pCircBuf, cbLeft, (void **)&pvSrc, &cbToRead);

        if (cbToRead)
        {
            /* Copy the data from our ring buffer to the core audio buffer. */
            memcpy((UInt8 *)pvDst + cbRead, pvSrc, cbToRead);
        }

        /* Release the read buffer, so it could be used for new data. */
        RTCircBufReleaseReadBlock(pCircBuf, cbToRead);

        if (!cbToRead)
            break;

        /* Move offset. */
        cbRead += cbToRead;
        Assert(cbRead <= audioBuffer->mAudioDataBytesCapacity);

        Assert(cbToRead <= cbLeft);
        cbLeft -= cbToRead;
    }

    audioBuffer->mAudioDataByteSize = cbRead;

    if (audioBuffer->mAudioDataByteSize < audioBuffer->mAudioDataBytesCapacity)
    {
        RT_BZERO((UInt8 *)audioBuffer->mAudioData + audioBuffer->mAudioDataByteSize,
                 audioBuffer->mAudioDataBytesCapacity - audioBuffer->mAudioDataByteSize);

        audioBuffer->mAudioDataByteSize = audioBuffer->mAudioDataBytesCapacity;
    }

    Log3Func(("pCAStream=%p, cbCapacity=%RU32, cbRead=%zu\n",
              pCAStream, audioBuffer->mAudioDataBytesCapacity, cbRead));

    return VINF_SUCCESS;
}

/**
 * Output audio queue callback. Called whenever an audio queue is ready to process more output data.
 *
 * @param   pvUser              User argument.
 * @param   audioQueue          Audio queue to process output data for.
 * @param   audioBuffer         Audio buffer to store output data in. Must be part of audio queue.
 */
static DECLCALLBACK(void) coreAudioOutputQueueCb(void *pvUser, AudioQueueRef audioQueue, AudioQueueBufferRef audioBuffer)
{
    RT_NOREF(audioQueue);

    PCOREAUDIOSTREAM pCAStream = (PCOREAUDIOSTREAM)pvUser;
    AssertPtr(pCAStream);

    int rc = RTCritSectEnter(&pCAStream->CritSect);
    AssertRC(rc);

    rc = coreAudioOutputQueueProcBuffer(pCAStream, audioBuffer);
    if (RT_SUCCESS(rc))
        AudioQueueEnqueueBuffer(audioQueue, audioBuffer, 0, NULL);

    rc = RTCritSectLeave(&pCAStream->CritSect);
    AssertRC(rc);
}

/**
 * Invalidates a Core Audio stream's audio queue.
 *
 * @returns IPRT status code.
 * @param   pCAStream           Core Audio stream to invalidate its queue for.
 */
static int coreAudioStreamInvalidateQueue(PCOREAUDIOSTREAM pCAStream)
{
    int rc = VINF_SUCCESS;

    Log3Func(("pCAStream=%p\n", pCAStream));

    for (size_t i = 0; i < RT_ELEMENTS(pCAStream->audioBuffer); i++)
    {
        AudioQueueBufferRef pBuf = pCAStream->audioBuffer[i];

        if (pCAStream->pCfg->enmDir == PDMAUDIODIR_IN)
        {
            int rc2 = coreAudioInputQueueProcBuffer(pCAStream, pBuf);
            if (RT_SUCCESS(rc2))
            {
                AudioQueueEnqueueBuffer(pCAStream->audioQueue, pBuf, 0, NULL);
            }
        }
        else if (pCAStream->pCfg->enmDir == PDMAUDIODIR_OUT)
        {
            int rc2 = coreAudioOutputQueueProcBuffer(pCAStream, pBuf);
            if (   RT_SUCCESS(rc2)
                && pBuf->mAudioDataByteSize)
            {
                AudioQueueEnqueueBuffer(pCAStream->audioQueue, pBuf, 0, NULL);
            }

            if (RT_SUCCESS(rc))
                rc = rc2;
        }
        else
            AssertFailed();
    }

    return rc;
}

/**
 * Initializes a Core Audio stream's audio queue.
 *
 * @returns IPRT status code.
 * @param   pCAStream           Core Audio stream to initialize audio queue for.
 * @param   pCfgReq             Requested stream configuration.
 * @param   pCfgAcq             Acquired stream configuration on success.
 */
static int coreAudioStreamInitQueue(PCOREAUDIOSTREAM pCAStream, PPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    RT_NOREF(pCfgAcq);

    LogFunc(("pCAStream=%p, pCfgReq=%p, pCfgAcq=%p\n", pCAStream, pCfgReq, pCfgAcq));

    /* No device assigned? Bail out early. */
    if (pCAStream->Unit.pDevice == NULL)
        return VERR_NOT_AVAILABLE;

    const bool fIn = pCfgReq->enmDir == PDMAUDIODIR_IN;

    int rc = VINF_SUCCESS;

    if (fIn)
    {
        rc = coreAudioInputPermissionCheck();
        if (RT_FAILURE(rc))
            return rc;
    }

    /* Create the recording device's out format based on our required audio settings. */
    Assert(pCAStream->pCfg == NULL);
    pCAStream->pCfg = PDMAudioStrmCfgDup(pCfgReq);
    if (!pCAStream->pCfg)
        rc = VERR_NO_MEMORY;

    coreAudioPCMPropsToASBD(&pCfgReq->Props, &pCAStream->asbdStream);
    /** @todo Do some validation? */

    coreAudioPrintASBD(  fIn
                       ? "Capturing queue format"
                       : "Playback queue format", &pCAStream->asbdStream);

    if (RT_FAILURE(rc))
    {
        LogRel(("CoreAudio: Failed to convert requested %s format to native format (%Rrc)\n", fIn ? "input" : "output", rc));
        return rc;
    }

    rc = RTCircBufCreate(&pCAStream->pCircBuf, PDMAUDIOSTREAMCFG_F2B(pCfgReq, pCfgReq->Backend.cFramesBufferSize));
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Start the thread.
     */
    rc = RTThreadCreate(&pCAStream->hThread, coreAudioQueueThread,
                        pCAStream /* pvUser */, 0 /* Default stack size */,
                        RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "CAQUEUE");
    if (RT_SUCCESS(rc))
        rc = RTThreadUserWait(pCAStream->hThread, 10 * 1000 /* 10s timeout */);

    LogFunc(("Returning %Rrc\n", rc));
    return rc;
}

/**
 * Unitializes a Core Audio stream's audio queue.
 *
 * @returns IPRT status code.
 * @param   pCAStream           Core Audio stream to unitialize audio queue for.
 */
static int coreAudioStreamUninitQueue(PCOREAUDIOSTREAM pCAStream)
{
    LogFunc(("pCAStream=%p\n", pCAStream));

    if (pCAStream->hThread != NIL_RTTHREAD)
    {
        LogFunc(("Waiting for thread ...\n"));

        ASMAtomicXchgBool(&pCAStream->fShutdown, true);

        int rcThread;
        int rc = RTThreadWait(pCAStream->hThread, 30 * 1000, &rcThread);
        if (RT_FAILURE(rc))
            return rc;

        RT_NOREF(rcThread);
        LogFunc(("Thread stopped with %Rrc\n", rcThread));

        pCAStream->hThread = NIL_RTTHREAD;
    }

    if (pCAStream->pCfg)
    {
        PDMAudioStrmCfgFree(pCAStream->pCfg);
        pCAStream->pCfg = NULL;
    }

    if (pCAStream->pCircBuf)
    {
        RTCircBufDestroy(pCAStream->pCircBuf);
        pCAStream->pCircBuf = NULL;
    }

    LogFunc(("Returning\n"));
    return VINF_SUCCESS;
}

/**
 * Unitializes a Core Audio stream.
 *
 * @returns IPRT status code.
 * @param   pCAStream           Core Audio stream to uninitialize.
 */
static int coreAudioStreamUninit(PCOREAUDIOSTREAM pCAStream)
{
    LogFunc(("pCAStream=%p\n", pCAStream));

    int rc = coreAudioStreamUninitQueue(pCAStream);
    if (RT_SUCCESS(rc))
    {
        pCAStream->Unit.pDevice = NULL;
        pCAStream->pDrv         = NULL;
    }

    return rc;
}

/**
 * Registers callbacks for a specific Core Audio device.
 *
 * @return IPRT status code.
 * @param  pThis                Host audio driver instance.
 * @param  pDev                 Audio device to use for the registered callbacks.
 */
static int coreAudioDeviceRegisterCallbacks(PDRVHOSTCOREAUDIO pThis, PCOREAUDIODEVICEDATA pDev)
{
    RT_NOREF(pThis);

    AudioDeviceID deviceID = kAudioDeviceUnknown;
    Assert(pDev && pDev->Core.cbSelf == sizeof(*pDev));
    if (pDev && pDev->Core.cbSelf == sizeof(*pDev)) /* paranoia or actually needed? */
        deviceID = pDev->deviceID;

    if (deviceID != kAudioDeviceUnknown)
    {
        LogFunc(("deviceID=%RU32\n", deviceID));

        /*
         * Register device callbacks.
         */
        AudioObjectPropertyAddress propAdr = { kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal,
                                               kAudioObjectPropertyElementMaster };
        OSStatus err = AudioObjectAddPropertyListener(deviceID, &propAdr,
                                                      coreAudioDeviceStateChangedCb, pDev /* pvUser */);
        if (   err != noErr
            && err != kAudioHardwareIllegalOperationError)
        {
            LogRel(("CoreAudio: Failed to add the recording device state changed listener (%RI32)\n", err));
        }

        propAdr.mSelector = kAudioDeviceProcessorOverload;
        propAdr.mScope    = kAudioUnitScope_Global;
        err = AudioObjectAddPropertyListener(deviceID, &propAdr,
                                             coreAudioDevPropChgCb, pDev /* pvUser */);
        if (err != noErr)
            LogRel(("CoreAudio: Failed to register processor overload listener (%RI32)\n", err));

        propAdr.mSelector = kAudioDevicePropertyNominalSampleRate;
        propAdr.mScope    = kAudioUnitScope_Global;
        err = AudioObjectAddPropertyListener(deviceID, &propAdr,
                                             coreAudioDevPropChgCb, pDev /* pvUser */);
        if (err != noErr)
            LogRel(("CoreAudio: Failed to register sample rate changed listener (%RI32)\n", err));
    }

    return VINF_SUCCESS;
}

/**
 * Unregisters all formerly registered callbacks of a Core Audio device again.
 *
 * @return IPRT status code.
 * @param  pThis                Host audio driver instance.
 * @param  pDev                 Audio device to use for the registered callbacks.
 */
static int coreAudioDeviceUnregisterCallbacks(PDRVHOSTCOREAUDIO pThis, PCOREAUDIODEVICEDATA pDev)
{
    RT_NOREF(pThis);

    AudioDeviceID deviceID = kAudioDeviceUnknown;
    Assert(pDev && pDev->Core.cbSelf == sizeof(*pDev));
    if (pDev && pDev->Core.cbSelf == sizeof(*pDev)) /* paranoia or actually needed? */
        deviceID = pDev->deviceID;

    if (deviceID != kAudioDeviceUnknown)
    {
        LogFunc(("deviceID=%RU32\n", deviceID));

        /*
         * Unregister per-device callbacks.
         */
        AudioObjectPropertyAddress propAdr = { kAudioDeviceProcessorOverload, kAudioObjectPropertyScopeGlobal,
                                               kAudioObjectPropertyElementMaster };
        OSStatus err = AudioObjectRemovePropertyListener(deviceID, &propAdr,
                                                         coreAudioDevPropChgCb, pDev /* pvUser */);
        if (   err != noErr
            && err != kAudioHardwareBadObjectError)
        {
            LogRel(("CoreAudio: Failed to remove the recording processor overload listener (%RI32)\n", err));
        }

        propAdr.mSelector = kAudioDevicePropertyNominalSampleRate;
        err = AudioObjectRemovePropertyListener(deviceID, &propAdr,
                                                coreAudioDevPropChgCb, pDev /* pvUser */);
        if (   err != noErr
            && err != kAudioHardwareBadObjectError)
        {
            LogRel(("CoreAudio: Failed to remove the sample rate changed listener (%RI32)\n", err));
        }

        propAdr.mSelector = kAudioDevicePropertyDeviceIsAlive;
        err = AudioObjectRemovePropertyListener(deviceID, &propAdr,
                                                coreAudioDeviceStateChangedCb, pDev /* pvUser */);
        if (   err != noErr
            && err != kAudioHardwareBadObjectError)
        {
            LogRel(("CoreAudio: Failed to remove the device alive listener (%RI32)\n", err));
        }
    }

    return VINF_SUCCESS;
}

/* Callback for getting notified when some of the properties of an audio device have changed. */
static DECLCALLBACK(OSStatus) coreAudioDevPropChgCb(AudioObjectID                     propertyID,
                                                    UInt32                            cAddresses,
                                                    const AudioObjectPropertyAddress  properties[],
                                                    void                             *pvUser)
{
    RT_NOREF(cAddresses, properties, pvUser);

    PCOREAUDIODEVICEDATA pDev = (PCOREAUDIODEVICEDATA)pvUser;
    AssertPtr(pDev);

    LogFlowFunc(("propertyID=%u, nAddresses=%u, pDev=%p\n", propertyID, cAddresses, pDev));

    switch (propertyID)
    {
#ifdef DEBUG
       case kAudioDeviceProcessorOverload:
        {
            LogFunc(("Processor overload detected!\n"));
            break;
        }
#endif /* DEBUG */
        case kAudioDevicePropertyNominalSampleRate:
        {
#ifndef VBOX_WITH_AUDIO_CALLBACKS
            int rc2 = coreAudioDevicePropagateStatus(pDev, COREAUDIOSTATUS_REINIT);
            AssertRC(rc2);
#else
            RT_NOREF(pDev);
#endif
            break;
        }

        default:
            /* Just skip. */
            break;
    }

    return noErr;
}

/**
 * Enumerates all available host audio devices internally.
 *
 * @returns IPRT status code.
 * @param   pThis               Host audio driver instance.
 */
static int coreAudioEnumerateDevices(PDRVHOSTCOREAUDIO pThis)
{
    LogFlowFuncEnter();

    /*
     * Unregister old default devices, if any.
     */
    if (pThis->pDefaultDevIn)
    {
        coreAudioDeviceUnregisterCallbacks(pThis, pThis->pDefaultDevIn);
        pThis->pDefaultDevIn = NULL;
    }

    if (pThis->pDefaultDevOut)
    {
        coreAudioDeviceUnregisterCallbacks(pThis, pThis->pDefaultDevOut);
        pThis->pDefaultDevOut = NULL;
    }

    /* Remove old / stale device entries. */
    PDMAudioHostEnumDelete(&pThis->Devices);

    /* Enumerate all devices internally. */
    int rc = coreAudioDevicesEnumerateAll(pThis, &pThis->Devices);
    if (RT_SUCCESS(rc))
    {
        /*
         * Default input device.
         */
        pThis->pDefaultDevIn = (PCOREAUDIODEVICEDATA)PDMAudioHostEnumGetDefault(&pThis->Devices, PDMAUDIODIR_IN);
        if (pThis->pDefaultDevIn)
        {
            LogRel2(("CoreAudio: Default capturing device is '%s'\n", pThis->pDefaultDevIn->Core.szName));
            LogFunc(("pDefaultDevIn=%p, ID=%RU32\n", pThis->pDefaultDevIn, pThis->pDefaultDevIn->deviceID));
            rc = coreAudioDeviceRegisterCallbacks(pThis, pThis->pDefaultDevIn);
        }
        else
            LogRel2(("CoreAudio: No default capturing device found\n"));

        /*
         * Default output device.
         */
        pThis->pDefaultDevOut = (PCOREAUDIODEVICEDATA)PDMAudioHostEnumGetDefault(&pThis->Devices, PDMAUDIODIR_OUT);
        if (pThis->pDefaultDevOut)
        {
            LogRel2(("CoreAudio: Default playback device is '%s'\n", pThis->pDefaultDevOut->Core.szName));
            LogFunc(("pDefaultDevOut=%p, ID=%RU32\n", pThis->pDefaultDevOut, pThis->pDefaultDevOut->deviceID));
            rc = coreAudioDeviceRegisterCallbacks(pThis, pThis->pDefaultDevOut);
        }
        else
            LogRel2(("CoreAudio: No default playback device found\n"));
    }

    LogFunc(("Returning %Rrc\n", rc));
    return rc;
}

/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCapture}
 */
static DECLCALLBACK(int) drvHostCoreAudioHA_StreamCapture(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                          void *pvBuf, uint32_t uBufSize, uint32_t *puRead)
{
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    AssertPtrReturn(pStream,    VERR_INVALID_POINTER);
    /* puRead is optional. */

    PCOREAUDIOSTREAM  pCAStream = (PCOREAUDIOSTREAM)pStream;
    PDRVHOSTCOREAUDIO pThis     = PDMIHOSTAUDIO_2_DRVHOSTCOREAUDIO(pInterface);

#ifndef VBOX_WITH_AUDIO_CALLBACKS
    /* Check if the audio device should be reinitialized. If so do it. */
    if (ASMAtomicReadU32(&pCAStream->enmStatus) == COREAUDIOSTATUS_REINIT)
    {
        /* For now re just re-initialize with the current input device. */
        if (pThis->pDefaultDevIn)
        {
            int rc2 = coreAudioStreamReinit(pThis, pCAStream, pThis->pDefaultDevIn);
            if (RT_FAILURE(rc2))
                return VERR_NOT_AVAILABLE;
        }
        else
            return VERR_NOT_AVAILABLE;
    }
#else
    RT_NOREF(pThis);
#endif

    if (ASMAtomicReadU32(&pCAStream->enmStatus) != COREAUDIOSTATUS_INIT)
    {
        if (puRead)
            *puRead = 0;
        return VINF_SUCCESS;
    }

    int rc = VINF_SUCCESS;

    uint32_t cbReadTotal = 0;

    rc = RTCritSectEnter(&pCAStream->CritSect);
    AssertRC(rc);

    do
    {
        size_t cbToWrite = RT_MIN(uBufSize, RTCircBufUsed(pCAStream->pCircBuf));

        uint8_t *pvChunk;
        size_t   cbChunk;

        Log3Func(("cbToWrite=%zu/%zu\n", cbToWrite, RTCircBufSize(pCAStream->pCircBuf)));

        while (cbToWrite)
        {
            /* Try to acquire the necessary block from the ring buffer. */
            RTCircBufAcquireReadBlock(pCAStream->pCircBuf, cbToWrite, (void **)&pvChunk, &cbChunk);
            if (cbChunk)
                memcpy((uint8_t *)pvBuf + cbReadTotal, pvChunk, cbChunk);

            /* Release the read buffer, so it could be used for new data. */
            RTCircBufReleaseReadBlock(pCAStream->pCircBuf, cbChunk);

            if (RT_FAILURE(rc))
                break;

            Assert(cbToWrite >= cbChunk);
            cbToWrite      -= cbChunk;

            cbReadTotal    += cbChunk;
        }
    }
    while (0);

    int rc2 = RTCritSectLeave(&pCAStream->CritSect);
    AssertRC(rc2);

    if (RT_SUCCESS(rc))
    {
        if (puRead)
            *puRead = cbReadTotal;
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPlay}
 */
static DECLCALLBACK(int) drvHostCoreAudioHA_StreamPlay(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                       const void *pvBuf, uint32_t uBufSize, uint32_t *puWritten)
{
    PDRVHOSTCOREAUDIO pThis     = PDMIHOSTAUDIO_2_DRVHOSTCOREAUDIO(pInterface);
    PCOREAUDIOSTREAM  pCAStream = (PCOREAUDIOSTREAM)pStream;

#ifndef VBOX_WITH_AUDIO_CALLBACKS
    /* Check if the audio device should be reinitialized. If so do it. */
    if (ASMAtomicReadU32(&pCAStream->enmStatus) == COREAUDIOSTATUS_REINIT)
    {
        if (pThis->pDefaultDevOut)
        {
            /* For now re just re-initialize with the current output device. */
            int rc2 = coreAudioStreamReinit(pThis, pCAStream, pThis->pDefaultDevOut);
            if (RT_FAILURE(rc2))
                return VERR_NOT_AVAILABLE;
        }
        else
            return VERR_NOT_AVAILABLE;
    }
#else
    RT_NOREF(pThis);
#endif

    if (ASMAtomicReadU32(&pCAStream->enmStatus) != COREAUDIOSTATUS_INIT)
    {
        if (puWritten)
            *puWritten = 0;
        return VINF_SUCCESS;
    }

    uint32_t cbWrittenTotal = 0;

    int rc = VINF_SUCCESS;

    rc = RTCritSectEnter(&pCAStream->CritSect);
    AssertRC(rc);

    size_t cbToWrite = RT_MIN(uBufSize, RTCircBufFree(pCAStream->pCircBuf));
    Log3Func(("cbToWrite=%zu\n", cbToWrite));

    uint8_t *pvChunk;
    size_t   cbChunk;

    while (cbToWrite)
    {
        /* Try to acquire the necessary space from the ring buffer. */
        RTCircBufAcquireWriteBlock(pCAStream->pCircBuf, cbToWrite, (void **)&pvChunk, &cbChunk);
        if (!cbChunk)
        {
            RTCircBufReleaseWriteBlock(pCAStream->pCircBuf, cbChunk);
            break;
        }

        Assert(cbChunk <= cbToWrite);
        Assert(cbWrittenTotal + cbChunk <= uBufSize);

        memcpy(pvChunk, (uint8_t *)pvBuf + cbWrittenTotal, cbChunk);

#ifdef VBOX_AUDIO_DEBUG_DUMP_PCM_DATA
        RTFILE fh;
        rc = RTFileOpen(&fh,VBOX_AUDIO_DEBUG_DUMP_PCM_DATA_PATH "caPlayback.pcm",
                        RTFILE_O_OPEN_CREATE | RTFILE_O_APPEND | RTFILE_O_WRITE | RTFILE_O_DENY_NONE);
        if (RT_SUCCESS(rc))
        {
            RTFileWrite(fh, pvChunk, cbChunk, NULL);
            RTFileClose(fh);
        }
        else
            AssertFailed();
#endif

        /* Release the ring buffer, so the read thread could start reading this data. */
        RTCircBufReleaseWriteBlock(pCAStream->pCircBuf, cbChunk);

        if (RT_FAILURE(rc))
            break;

        Assert(cbToWrite >= cbChunk);
        cbToWrite      -= cbChunk;

        cbWrittenTotal += cbChunk;
    }

    if (    RT_SUCCESS(rc)
        &&  pCAStream->fRun
        && !pCAStream->fIsRunning)
    {
        rc = coreAudioStreamInvalidateQueue(pCAStream);
        if (RT_SUCCESS(rc))
        {
            AudioQueueStart(pCAStream->audioQueue, NULL);
            pCAStream->fRun       = false;
            pCAStream->fIsRunning = true;
        }
    }

    int rc2 = RTCritSectLeave(&pCAStream->CritSect);
    AssertRC(rc2);

    if (RT_SUCCESS(rc))
    {
        if (puWritten)
            *puWritten = cbWrittenTotal;
    }

    return rc;
}

static int coreAudioStreamControl(PDRVHOSTCOREAUDIO pThis, PCOREAUDIOSTREAM pCAStream, PDMAUDIOSTREAMCMD enmStreamCmd)
{
    RT_NOREF(pThis);

    uint32_t enmStatus = ASMAtomicReadU32(&pCAStream->enmStatus);

    LogFlowFunc(("enmStreamCmd=%RU32, enmStatus=%RU32\n", enmStreamCmd, enmStatus));

    if (!(   enmStatus == COREAUDIOSTATUS_INIT
#ifndef VBOX_WITH_AUDIO_CALLBACKS
          || enmStatus == COREAUDIOSTATUS_REINIT
#endif
          ))
    {
        return VINF_SUCCESS;
    }

    if (!pCAStream->pCfg) /* Not (yet) configured? Skip. */
        return VINF_SUCCESS;

    int rc = VINF_SUCCESS;

    switch (enmStreamCmd)
    {
        case PDMAUDIOSTREAMCMD_ENABLE:
        case PDMAUDIOSTREAMCMD_RESUME:
        {
            LogFunc(("Queue enable\n"));
            if (pCAStream->pCfg->enmDir == PDMAUDIODIR_IN)
            {
                rc = coreAudioStreamInvalidateQueue(pCAStream);
                if (RT_SUCCESS(rc))
                {
                    /* Start the audio queue immediately. */
                    AudioQueueStart(pCAStream->audioQueue, NULL);
                }
            }
            else if (pCAStream->pCfg->enmDir == PDMAUDIODIR_OUT)
            {
                /* Touch the run flag to start the audio queue as soon as
                 * we have anough data to actually play something. */
                ASMAtomicXchgBool(&pCAStream->fRun, true);
            }
            break;
        }

        case PDMAUDIOSTREAMCMD_DISABLE:
        {
            LogFunc(("Queue disable\n"));
            AudioQueueStop(pCAStream->audioQueue, 1 /* Immediately */);
            ASMAtomicXchgBool(&pCAStream->fRun,       false);
            ASMAtomicXchgBool(&pCAStream->fIsRunning, false);
            break;
        }
        case PDMAUDIOSTREAMCMD_PAUSE:
        {
            LogFunc(("Queue pause\n"));
            AudioQueuePause(pCAStream->audioQueue);
            ASMAtomicXchgBool(&pCAStream->fIsRunning, false);
            break;
        }

        default:
            rc = VERR_NOT_SUPPORTED;
            break;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetConfig}
 */
static DECLCALLBACK(int) drvHostCoreAudioHA_GetConfig(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDCFG pBackendCfg)
{
    AssertPtrReturn(pInterface,  VERR_INVALID_POINTER);
    AssertPtrReturn(pBackendCfg, VERR_INVALID_POINTER);

    PDRVHOSTCOREAUDIO pThis = PDMIHOSTAUDIO_2_DRVHOSTCOREAUDIO(pInterface);

    RT_BZERO(pBackendCfg, sizeof(PDMAUDIOBACKENDCFG));

    RTStrPrintf2(pBackendCfg->szName, sizeof(pBackendCfg->szName), "Core Audio");

    pBackendCfg->cbStreamIn  = sizeof(COREAUDIOSTREAM);
    pBackendCfg->cbStreamOut = sizeof(COREAUDIOSTREAM);

    /* For Core Audio we provide one stream per device for now. */
    pBackendCfg->cMaxStreamsIn  = PDMAudioHostEnumCountMatching(&pThis->Devices, PDMAUDIODIR_IN);
    pBackendCfg->cMaxStreamsOut = PDMAudioHostEnumCountMatching(&pThis->Devices, PDMAUDIODIR_OUT);

    LogFlowFunc(("Returning %Rrc\n", VINF_SUCCESS));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetDevices}
 */
static DECLCALLBACK(int) drvHostCoreAudioHA_GetDevices(PPDMIHOSTAUDIO pInterface, PPDMAUDIOHOSTENUM pDeviceEnum)
{
    AssertPtrReturn(pInterface,  VERR_INVALID_POINTER);
    AssertPtrReturn(pDeviceEnum, VERR_INVALID_POINTER);

    PDRVHOSTCOREAUDIO pThis = PDMIHOSTAUDIO_2_DRVHOSTCOREAUDIO(pInterface);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        rc = coreAudioEnumerateDevices(pThis);
        if (RT_SUCCESS(rc))
        {
            if (pDeviceEnum)
            {
                /* Return a copy with only PDMAUDIOHOSTDEV, none of the extra bits in COREAUDIODEVICEDATA. */
                PDMAudioHostEnumInit(pDeviceEnum);
                rc = PDMAudioHostEnumCopy(pDeviceEnum, &pThis->Devices, PDMAUDIODIR_INVALID /*all*/, true /*fOnlyCoreData*/);
                if (RT_FAILURE(rc))
                    PDMAudioHostEnumDelete(pDeviceEnum);
            }
        }

        int rc2 = RTCritSectLeave(&pThis->CritSect);
        AssertRC(rc2);
    }

    LogFlowFunc(("Returning %Rrc\n", rc));
    return rc;
}


#ifdef VBOX_WITH_AUDIO_CALLBACKS
/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnSetCallback}
 */
static DECLCALLBACK(int) drvHostCoreAudioHA_SetCallback(PPDMIHOSTAUDIO pInterface, PFNPDMHOSTAUDIOCALLBACK pfnCallback)
{
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    /* pfnCallback will be handled below. */

    PDRVHOSTCOREAUDIO pThis = PDMIHOSTAUDIO_2_DRVHOSTCOREAUDIO(pInterface);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        LogFunc(("pfnCallback=%p\n", pfnCallback));

        if (pfnCallback) /* Register. */
        {
            Assert(pThis->pfnCallback == NULL);
            pThis->pfnCallback = pfnCallback;
        }
        else /* Unregister. */
        {
            if (pThis->pfnCallback)
                pThis->pfnCallback = NULL;
        }

        int rc2 = RTCritSectLeave(&pThis->CritSect);
        AssertRC(rc2);
    }

    return rc;
}
#endif


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetStatus}
 */
static DECLCALLBACK(PDMAUDIOBACKENDSTS) drvHostCoreAudioHA_GetStatus(PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir)
{
    RT_NOREF(pInterface, enmDir);
    AssertPtrReturn(pInterface, PDMAUDIOBACKENDSTS_UNKNOWN);

    return PDMAUDIOBACKENDSTS_RUNNING;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCreate}
 */
static DECLCALLBACK(int) drvHostCoreAudioHA_StreamCreate(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                         PPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    AssertPtrReturn(pStream,    VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgReq,    VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgAcq,    VERR_INVALID_POINTER);

    PDRVHOSTCOREAUDIO pThis     = PDMIHOSTAUDIO_2_DRVHOSTCOREAUDIO(pInterface);
    PCOREAUDIOSTREAM  pCAStream = (PCOREAUDIOSTREAM)pStream;

    int rc = RTCritSectInit(&pCAStream->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    pCAStream->hThread    = NIL_RTTHREAD;
    pCAStream->fRun       = false;
    pCAStream->fIsRunning = false;
    pCAStream->fShutdown  = false;

    /* Input or output device? */
    bool fIn = pCfgReq->enmDir == PDMAUDIODIR_IN;

    /* For now, just use the default device available. */
    PCOREAUDIODEVICEDATA pDev = fIn ? pThis->pDefaultDevIn : pThis->pDefaultDevOut;

    LogFunc(("pStream=%p, pCfgReq=%p, pCfgAcq=%p, fIn=%RTbool, pDev=%p\n", pStream, pCfgReq, pCfgAcq, fIn, pDev));

    if (pDev) /* (Default) device available? */
    {
        /* Sanity. */
        Assert(pDev->Core.cbSelf == sizeof(*pDev));

        /* Init the Core Audio stream. */
        rc = coreAudioStreamInit(pCAStream, pThis, pDev);
        if (RT_SUCCESS(rc))
        {
            ASMAtomicXchgU32(&pCAStream->enmStatus, COREAUDIOSTATUS_IN_INIT);

            rc = coreAudioStreamInitQueue(pCAStream, pCfgReq, pCfgAcq);
            if (RT_SUCCESS(rc))
            {
                ASMAtomicXchgU32(&pCAStream->enmStatus, COREAUDIOSTATUS_INIT);
            }
            else
            {
                ASMAtomicXchgU32(&pCAStream->enmStatus, COREAUDIOSTATUS_IN_UNINIT);

                int rc2 = coreAudioStreamUninit(pCAStream);
                AssertRC(rc2);

                ASMAtomicXchgU32(&pCAStream->enmStatus, COREAUDIOSTATUS_UNINIT);
            }
        }
    }
    else
        rc = VERR_AUDIO_STREAM_COULD_NOT_CREATE;

    LogFunc(("Returning %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDestroy}
 */
static DECLCALLBACK(int) drvHostCoreAudioHA_StreamDestroy(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    AssertPtrReturn(pStream,    VERR_INVALID_POINTER);

    PDRVHOSTCOREAUDIO pThis = PDMIHOSTAUDIO_2_DRVHOSTCOREAUDIO(pInterface);

    PCOREAUDIOSTREAM pCAStream = (PCOREAUDIOSTREAM)pStream;

    uint32_t status = ASMAtomicReadU32(&pCAStream->enmStatus);
    if (!(   status == COREAUDIOSTATUS_INIT
#ifndef VBOX_WITH_AUDIO_CALLBACKS
          || status == COREAUDIOSTATUS_REINIT
#endif
          ))
    {
        return VINF_SUCCESS;
    }

    if (!pCAStream->pCfg) /* Not (yet) configured? Skip. */
        return VINF_SUCCESS;

    int rc = coreAudioStreamControl(pThis, pCAStream, PDMAUDIOSTREAMCMD_DISABLE);
    if (RT_SUCCESS(rc))
    {
        ASMAtomicXchgU32(&pCAStream->enmStatus, COREAUDIOSTATUS_IN_UNINIT);

        rc = coreAudioStreamUninit(pCAStream);

        if (RT_SUCCESS(rc))
            ASMAtomicXchgU32(&pCAStream->enmStatus, COREAUDIOSTATUS_UNINIT);
    }

    if (RT_SUCCESS(rc))
    {
        if (RTCritSectIsInitialized(&pCAStream->CritSect))
            RTCritSectDelete(&pCAStream->CritSect);
    }

    LogFunc(("rc=%Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamControl}
 */
static DECLCALLBACK(int) drvHostCoreAudioHA_StreamControl(PPDMIHOSTAUDIO pInterface,
                                                          PPDMAUDIOBACKENDSTREAM pStream, PDMAUDIOSTREAMCMD enmStreamCmd)
{
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    AssertPtrReturn(pStream,    VERR_INVALID_POINTER);

    PDRVHOSTCOREAUDIO pThis    = PDMIHOSTAUDIO_2_DRVHOSTCOREAUDIO(pInterface);
    PCOREAUDIOSTREAM pCAStream = (PCOREAUDIOSTREAM)pStream;

    return coreAudioStreamControl(pThis, pCAStream, enmStreamCmd);
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetReadable}
 */
static DECLCALLBACK(uint32_t) drvHostCoreAudioHA_StreamGetReadable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pStream, VERR_INVALID_POINTER);

    PCOREAUDIOSTREAM pCAStream = (PCOREAUDIOSTREAM)pStream;

    if (ASMAtomicReadU32(&pCAStream->enmStatus) != COREAUDIOSTATUS_INIT)
        return 0;

    AssertPtr(pCAStream->pCfg);
    AssertPtr(pCAStream->pCircBuf);

    switch (pCAStream->pCfg->enmDir)
    {
        case PDMAUDIODIR_IN:
            return (uint32_t)RTCircBufUsed(pCAStream->pCircBuf);

        case PDMAUDIODIR_OUT:
        default:
            AssertFailed();
            break;
    }

    return 0;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetWritable}
 */
static DECLCALLBACK(uint32_t) drvHostCoreAudioHA_StreamGetWritable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pStream, VERR_INVALID_POINTER);

    PCOREAUDIOSTREAM pCAStream = (PCOREAUDIOSTREAM)pStream;

    uint32_t cbWritable = 0;

    if (ASMAtomicReadU32(&pCAStream->enmStatus) == COREAUDIOSTATUS_INIT)
    {
        AssertPtr(pCAStream->pCfg);
        AssertPtr(pCAStream->pCircBuf);

        switch (pCAStream->pCfg->enmDir)
        {
            case PDMAUDIODIR_OUT:
                cbWritable = (uint32_t)RTCircBufFree(pCAStream->pCircBuf);
                break;

            default:
                break;
        }
    }

    LogFlowFunc(("cbWritable=%RU32\n", cbWritable));
    return cbWritable;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetStatus}
 */
static DECLCALLBACK(PDMAUDIOSTREAMSTS) drvHostCoreAudioHA_StreamGetStatus(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pStream, VERR_INVALID_POINTER);

    PCOREAUDIOSTREAM pCAStream = (PCOREAUDIOSTREAM)pStream;

    PDMAUDIOSTREAMSTS fStrmStatus = PDMAUDIOSTREAMSTS_FLAGS_NONE;

    if (pCAStream->pCfg) /* Configured?  */
    {
        if (ASMAtomicReadU32(&pCAStream->enmStatus) == COREAUDIOSTATUS_INIT)
            fStrmStatus |= PDMAUDIOSTREAMSTS_FLAGS_INITIALIZED | PDMAUDIOSTREAMSTS_FLAGS_ENABLED;
    }

    return fStrmStatus;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamIterate}
 */
static DECLCALLBACK(int) drvHostCoreAudioHA_StreamIterate(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    AssertPtrReturn(pStream,    VERR_INVALID_POINTER);

    RT_NOREF(pInterface, pStream);

    /* Nothing to do here for Core Audio. */
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnInit}
 */
static DECLCALLBACK(int) drvHostCoreAudioHA_Init(PPDMIHOSTAUDIO pInterface)
{
    PDRVHOSTCOREAUDIO pThis = PDMIHOSTAUDIO_2_DRVHOSTCOREAUDIO(pInterface);

    PDMAudioHostEnumInit(&pThis->Devices);
    /* Do the first (initial) internal device enumeration. */
    int rc = coreAudioEnumerateDevices(pThis);
    if (RT_SUCCESS(rc))
    {
        /* Register system callbacks. */
        AudioObjectPropertyAddress propAdr = { kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal,
                                               kAudioObjectPropertyElementMaster };

        OSStatus err = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &propAdr,
                                                      coreAudioDefaultDeviceChangedCb, pThis /* pvUser */);
        if (   err != noErr
            && err != kAudioHardwareIllegalOperationError)
        {
            LogRel(("CoreAudio: Failed to add the input default device changed listener (%RI32)\n", err));
        }

        propAdr.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
        err = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &propAdr,
                                             coreAudioDefaultDeviceChangedCb, pThis /* pvUser */);
        if (   err != noErr
            && err != kAudioHardwareIllegalOperationError)
        {
            LogRel(("CoreAudio: Failed to add the output default device changed listener (%RI32)\n", err));
        }
    }

    LogFlowFunc(("Returning %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnShutdown}
 */
static DECLCALLBACK(void) drvHostCoreAudioHA_Shutdown(PPDMIHOSTAUDIO pInterface)
{
    PDRVHOSTCOREAUDIO pThis = PDMIHOSTAUDIO_2_DRVHOSTCOREAUDIO(pInterface);

    /*
     * Unregister system callbacks.
     */
    AudioObjectPropertyAddress propAdr = { kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal,
                                           kAudioObjectPropertyElementMaster };

    OSStatus err = AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &propAdr,
                                                     coreAudioDefaultDeviceChangedCb, pThis /* pvUser */);
    if (   err != noErr
        && err != kAudioHardwareBadObjectError)
    {
        LogRel(("CoreAudio: Failed to remove the default input device changed listener (%RI32)\n", err));
    }

    propAdr.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    err = AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &propAdr,
                                            coreAudioDefaultDeviceChangedCb, pThis /* pvUser */);
    if (   err != noErr
        && err != kAudioHardwareBadObjectError)
    {
        LogRel(("CoreAudio: Failed to remove the default output device changed listener (%RI32)\n", err));
    }

    LogFlowFuncEnter();
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvHostCoreAudioQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS        pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVHOSTCOREAUDIO pThis   = PDMINS_2_DATA(pDrvIns, PDRVHOSTCOREAUDIO);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE,      &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHOSTAUDIO, &pThis->IHostAudio);

    return NULL;
}


/**
 * @callback_method_impl{FNPDMDRVCONSTRUCT,
 *      Construct a Core Audio driver instance.}
 */
static DECLCALLBACK(int) drvHostCoreAudioConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(pCfg, fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVHOSTCOREAUDIO pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTCOREAUDIO);
    LogRel(("Audio: Initializing Core Audio driver\n"));

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                   = pDrvIns;
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface = drvHostCoreAudioQueryInterface;
    /* IHostAudio */
    pThis->IHostAudio.pfnInit               = drvHostCoreAudioHA_Init;
    pThis->IHostAudio.pfnShutdown           = drvHostCoreAudioHA_Shutdown;
    pThis->IHostAudio.pfnGetConfig          = drvHostCoreAudioHA_GetConfig;
    pThis->IHostAudio.pfnGetStatus          = drvHostCoreAudioHA_GetStatus;
    pThis->IHostAudio.pfnStreamCreate       = drvHostCoreAudioHA_StreamCreate;
    pThis->IHostAudio.pfnStreamDestroy      = drvHostCoreAudioHA_StreamDestroy;
    pThis->IHostAudio.pfnStreamControl      = drvHostCoreAudioHA_StreamControl;
    pThis->IHostAudio.pfnStreamGetReadable  = drvHostCoreAudioHA_StreamGetReadable;
    pThis->IHostAudio.pfnStreamGetWritable  = drvHostCoreAudioHA_StreamGetWritable;
    pThis->IHostAudio.pfnStreamGetStatus    = drvHostCoreAudioHA_StreamGetStatus;
    pThis->IHostAudio.pfnStreamIterate      = drvHostCoreAudioHA_StreamIterate;
    pThis->IHostAudio.pfnStreamPlay         = drvHostCoreAudioHA_StreamPlay;
    pThis->IHostAudio.pfnStreamCapture      = drvHostCoreAudioHA_StreamCapture;
#ifdef VBOX_WITH_AUDIO_CALLBACKS
    pThis->IHostAudio.pfnSetCallback        = drvHostCoreAudioHA_SetCallback;
    pThis->pfnCallback                      = NULL;
#else
    pThis->IHostAudio.pfnSetCallback        = NULL;
#endif
    pThis->IHostAudio.pfnGetDevices         = drvHostCoreAudioHA_GetDevices;
    pThis->IHostAudio.pfnStreamGetPending   = NULL;
    pThis->IHostAudio.pfnStreamPlayBegin    = NULL;
    pThis->IHostAudio.pfnStreamPlayEnd      = NULL;
    pThis->IHostAudio.pfnStreamCaptureBegin = NULL;
    pThis->IHostAudio.pfnStreamCaptureEnd   = NULL;

    int rc = RTCritSectInit(&pThis->CritSect);
    AssertRCReturn(rc, rc);

#ifdef VBOX_AUDIO_DEBUG_DUMP_PCM_DATA
    RTFileDelete(VBOX_AUDIO_DEBUG_DUMP_PCM_DATA_PATH "caConverterCbInput.pcm");
    RTFileDelete(VBOX_AUDIO_DEBUG_DUMP_PCM_DATA_PATH "caPlayback.pcm");
#endif

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * @callback_method_impl{FNPDMDRVDESTRUCT}
 */
static DECLCALLBACK(void) drvHostCoreAudioDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVHOSTCOREAUDIO pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTCOREAUDIO);

    int rc2 = RTCritSectDelete(&pThis->CritSect);
    AssertRC(rc2);

    LogFlowFuncLeaveRC(rc2);
}


/**
 * Char driver registration record.
 */
const PDMDRVREG g_DrvHostCoreAudio =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "CoreAudio",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Core Audio host driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_AUDIO,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVHOSTCOREAUDIO),
    /* pfnConstruct */
    drvHostCoreAudioConstruct,
    /* pfnDestruct */
    drvHostCoreAudioDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

