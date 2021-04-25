/* $Id: DrvHostAudioAlsa.cpp $ */
/** @file
 * Host audio driver - Advanced Linux Sound Architecture (ALSA).
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
 * --------------------------------------------------------------------
 *
 * This code is based on: alsaaudio.c
 *
 * QEMU ALSA audio driver
 *
 * Copyright (c) 2005 Vassili Karpov (malc)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#include <VBox/log.h>
#include <iprt/alloc.h>
#include <iprt/uuid.h> /* For PDMIBASE_2_PDMDRV. */
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>

RT_C_DECLS_BEGIN
#include "DrvHostAudioAlsaStubs.h"
#include "DrvHostAudioAlsaStubsMangling.h"
RT_C_DECLS_END

#include <alsa/asoundlib.h>
#include <alsa/control.h> /* For device enumeration. */

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/

/** Makes DRVHOSTALSAAUDIO out of PDMIHOSTAUDIO. */
#define PDMIHOSTAUDIO_2_DRVHOSTALSAAUDIO(pInterface) \
    ( (PDRVHOSTALSAAUDIO)((uintptr_t)pInterface - RT_UOFFSETOF(DRVHOSTALSAAUDIO, IHostAudio)) )


/*********************************************************************************************************************************
*   Structures                                                                                                                   *
*********************************************************************************************************************************/

/**
 * Structure for maintaining an ALSA audio stream.
 */
typedef struct ALSAAUDIOSTREAM
{
    /** The stream's acquired configuration. */
    PPDMAUDIOSTREAMCFG pCfg;
    /** Pointer to allocated ALSA PCM configuration to use. */
    snd_pcm_t          *phPCM;
    /** Scratch buffer. */
    void               *pvBuf;
    /** Size (in bytes) of allocated scratch buffer. */
    size_t              cbBuf;
} ALSAAUDIOSTREAM, *PALSAAUDIOSTREAM;

/* latency = period_size * periods / (rate * bytes_per_frame) */

static int alsaStreamRecover(snd_pcm_t *phPCM);

/**
 * Host Alsa audio driver instance data.
 * @implements PDMIAUDIOCONNECTOR
 */
typedef struct DRVHOSTALSAAUDIO
{
    /** Pointer to the driver instance structure. */
    PPDMDRVINS          pDrvIns;
    /** Pointer to host audio interface. */
    PDMIHOSTAUDIO       IHostAudio;
    /** Error count for not flooding the release log.
     *  UINT32_MAX for unlimited logging. */
    uint32_t            cLogErrors;
    /** Default input device name.   */
    char                szDefaultIn[256];
    /** Default output device name. */
    char                szDefaultOut[256];
} DRVHOSTALSAAUDIO, *PDRVHOSTALSAAUDIO;

/** Maximum number of tries to recover a broken pipe. */
#define ALSA_RECOVERY_TRIES_MAX    5

/**
 * Structure for maintaining an ALSA audio stream configuration.
 */
typedef struct ALSAAUDIOSTREAMCFG
{
    unsigned int        freq;
    /** PCM sound format. */
    snd_pcm_format_t    fmt;
    /** PCM data access type. */
    snd_pcm_access_t    access;
    /** Whether resampling should be performed by alsalib or not. */
    int                 resample;
    /** Number of audio channels. */
    int                 nchannels;
    /** Buffer size (in audio frames). */
    unsigned long       buffer_size;
    /** Periods (in audio frames). */
    unsigned long       period_size;
    /** For playback: Starting to play threshold (in audio frames).
     *  For Capturing: Starting to capture threshold (in audio frames). */
    unsigned long       threshold;
} ALSAAUDIOSTREAMCFG, *PALSAAUDIOSTREAMCFG;


/**
 * Converts internal audio PCM properties to an ALSA PCM format.
 *
 * @returns Converted ALSA PCM format.
 * @param   pProps              Internal audio PCM configuration to convert.
 */
static snd_pcm_format_t alsaAudioPropsToALSA(PPDMAUDIOPCMPROPS pProps)
{
    switch (pProps->cbSample)
    {
        case 1:
            return pProps->fSigned ? SND_PCM_FORMAT_S8 : SND_PCM_FORMAT_U8;

        case 2:
            return pProps->fSigned ? SND_PCM_FORMAT_S16_LE : SND_PCM_FORMAT_U16_LE;

        case 4:
            return pProps->fSigned ? SND_PCM_FORMAT_S32_LE : SND_PCM_FORMAT_U32_LE;

        default:
            break;
    }

    AssertMsgFailed(("%RU8 bytes not supported\n", pProps->cbSample));
    return SND_PCM_FORMAT_U8;
}


/**
 * Converts an ALSA PCM format to internal PCM properties.
 *
 * @returns VBox status code.
 * @param   fmt                 ALSA PCM format to convert.
 * @param   pProps              Where to store the converted PCM properties on success.
 */
static int alsaALSAToAudioProps(snd_pcm_format_t fmt, PPDMAUDIOPCMPROPS pProps)
{
    switch (fmt)
    {
        case SND_PCM_FORMAT_S8:
            pProps->cbSample    = 1;
            pProps->fSigned     = true;
            pProps->fSwapEndian = false;
            break;

        case SND_PCM_FORMAT_U8:
            pProps->cbSample    = 1;
            pProps->fSigned     = false;
            pProps->fSwapEndian = false;
            break;

        case SND_PCM_FORMAT_S16_LE:
            pProps->cbSample    = 2;
            pProps->fSigned     = true;
            pProps->fSwapEndian = false;
            break;

        case SND_PCM_FORMAT_U16_LE:
            pProps->cbSample    = 2;
            pProps->fSigned     = false;
            pProps->fSwapEndian = false;
            break;

        case SND_PCM_FORMAT_S16_BE:
            pProps->cbSample    = 2;
            pProps->fSigned     = true;
#ifdef RT_LITTLE_ENDIAN
            pProps->fSwapEndian = true;
#endif
            break;

        case SND_PCM_FORMAT_U16_BE:
            pProps->cbSample    = 2;
            pProps->fSigned     = false;
#ifdef RT_LITTLE_ENDIAN
            pProps->fSwapEndian = true;
#endif
            break;

        case SND_PCM_FORMAT_S32_LE:
            pProps->cbSample    = 4;
            pProps->fSigned     = true;
            pProps->fSwapEndian = false;
            break;

        case SND_PCM_FORMAT_U32_LE:
            pProps->cbSample    = 4;
            pProps->fSigned     = false;
            pProps->fSwapEndian = false;
            break;

        case SND_PCM_FORMAT_S32_BE:
            pProps->cbSample    = 4;
            pProps->fSigned     = true;
#ifdef RT_LITTLE_ENDIAN
            pProps->fSwapEndian = true;
#endif
            break;

        case SND_PCM_FORMAT_U32_BE:
            pProps->cbSample    = 4;
            pProps->fSigned     = false;
#ifdef RT_LITTLE_ENDIAN
            pProps->fSwapEndian = true;
#endif
            break;

        default:
            AssertMsgFailedReturn(("Format %d not supported\n", fmt), VERR_NOT_SUPPORTED);
    }

    AssertReturn(pProps->cbSample > 0,  VERR_NOT_SUPPORTED);
    AssertReturn(pProps->cChannels > 0, VERR_INVALID_PARAMETER);

    pProps->cShift = PDMAUDIOPCMPROPS_MAKE_SHIFT_PARMS(pProps->cbSample, pProps->cChannels);

    return VINF_SUCCESS;
}


/**
 * Closes an ALSA stream
 *
 * @returns VBox status code.
 * @param   pphPCM              ALSA stream to close.
 */
static int alsaStreamClose(snd_pcm_t **pphPCM)
{
    if (!pphPCM || !*pphPCM)
        return VINF_SUCCESS;

    int rc;
    int rc2 = snd_pcm_close(*pphPCM);
    if (rc2)
    {
        LogRel(("ALSA: Closing PCM descriptor failed: %s\n", snd_strerror(rc2)));
        rc = VERR_GENERAL_FAILURE; /** @todo */
    }
    else
    {
        *pphPCM = NULL;
        rc = VINF_SUCCESS;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Sets the software parameters of an ALSA stream.
 *
 * @returns 0 on success, negative errno on failure.
 * @param   phPCM               ALSA stream to set software parameters for.
 * @param   fIn                 Whether this is an input stream or not.
 * @param   pCfgReq             Requested configuration to set.
 * @param   pCfgObt             Obtained configuration on success. Might differ from requested configuration.
 */
static int alsaStreamSetSWParams(snd_pcm_t *phPCM, bool fIn, PALSAAUDIOSTREAMCFG pCfgReq, PALSAAUDIOSTREAMCFG pCfgObt)
{
    if (fIn) /* For input streams there's nothing to do in here right now. */
        return VINF_SUCCESS;

    snd_pcm_sw_params_t *pSWParms = NULL;
    snd_pcm_sw_params_alloca(&pSWParms);
    AssertReturn(pSWParms, -ENOMEM);

    int err = snd_pcm_sw_params_current(phPCM, pSWParms);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to get current software parameters: %s\n", snd_strerror(err)), err);

    /* Must make sure we don't require ALSA to prebuffer more than
       it has buffer space for, because that means output will
       never start. */
    unsigned long cFramesPreBuffer = pCfgReq->threshold;
    if (cFramesPreBuffer >= pCfgObt->buffer_size - pCfgObt->buffer_size / 16)
    {
        cFramesPreBuffer = pCfgObt->buffer_size - pCfgObt->buffer_size / 16;
        LogRel2(("ALSA: Reducing threshold from %lu to %lu due to buffer size of %lu.\n",
                 pCfgReq->threshold, cFramesPreBuffer, pCfgObt->buffer_size));
    }
    err = snd_pcm_sw_params_set_start_threshold(phPCM, pSWParms, cFramesPreBuffer);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set software threshold to %lu: %s\n", cFramesPreBuffer, snd_strerror(err)), err);

    err = snd_pcm_sw_params_set_avail_min(phPCM, pSWParms, pCfgReq->period_size);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set available minimum to %lu: %s\n", pCfgReq->period_size, snd_strerror(err)), err);

    /* Commit the software parameters: */
    err = snd_pcm_sw_params(phPCM, pSWParms);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set new software parameters: %s\n", snd_strerror(err)), err);

    /* Get the actual parameters: */
    err = snd_pcm_sw_params_get_start_threshold(pSWParms, &pCfgObt->threshold);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to get start threshold: %s\n", snd_strerror(err)), err);

    LogRel2(("ALSA: SW params: %ul frames threshold, %ul frame avail minimum\n",
             pCfgObt->threshold, pCfgReq->period_size));
    return 0;
}


/**
 * Sets the hardware parameters of an ALSA stream.
 *
 * @returns 0 on success, negative errno on failure.
 * @param   phPCM   ALSA stream to set software parameters for.
 * @param   pCfgReq Requested configuration to set.
 * @param   pCfgObt Obtained configuration on success. Might differ from
 *                       requested configuration.
 */
static int alsaStreamSetHwParams(snd_pcm_t *phPCM, PALSAAUDIOSTREAMCFG pCfgReq, PALSAAUDIOSTREAMCFG pCfgObt)
{
    /*
     * Get the current hardware parameters.
     */
    snd_pcm_hw_params_t *pHWParms = NULL;
    snd_pcm_hw_params_alloca(&pHWParms);
    AssertReturn(pHWParms, -ENOMEM);

    int err = snd_pcm_hw_params_any(phPCM, pHWParms);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to initialize hardware parameters: %s\n", snd_strerror(err)), err);

    /*
     * Modify them according to pCfgReq.
     * We update pCfgObt as we go for parameters set by "near" methods.
     */
    /* We'll use snd_pcm_writei/snd_pcm_readi: */
    err = snd_pcm_hw_params_set_access(phPCM, pHWParms, SND_PCM_ACCESS_RW_INTERLEAVED);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set access type: %s\n", snd_strerror(err)), err);

    /* Set the format, frequency and channel count. */
    err = snd_pcm_hw_params_set_format(phPCM, pHWParms, pCfgReq->fmt);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set audio format to %d: %s\n", pCfgReq->fmt, snd_strerror(err)), err);

    unsigned int uFreq = pCfgReq->freq;
    err = snd_pcm_hw_params_set_rate_near(phPCM, pHWParms, &uFreq, NULL /*dir*/);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set frequency to %uHz: %s\n", pCfgReq->freq, snd_strerror(err)), err);
    pCfgObt->freq      = uFreq;

    unsigned int cChannels = pCfgReq->nchannels;
    err = snd_pcm_hw_params_set_channels_near(phPCM, pHWParms, &cChannels);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set number of channels to %d\n", pCfgReq->nchannels), err);
    AssertLogRelMsgReturn(cChannels == 1 || cChannels == 2, ("ALSA: Number of audio channels (%u) not supported\n", cChannels), -1);
    pCfgObt->nchannels = cChannels;

    /* The period size (reportedly frame count per hw interrupt): */
    int               dir    = 0;
    snd_pcm_uframes_t minval = pCfgReq->period_size;
    err = snd_pcm_hw_params_get_period_size_min(pHWParms, &minval, &dir);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Could not determine minimal period size: %s\n", snd_strerror(err)), err);

    snd_pcm_uframes_t period_size_f = pCfgReq->period_size;
    if (period_size_f < minval)
        period_size_f = minval;
    err = snd_pcm_hw_params_set_period_size_near(phPCM, pHWParms, &period_size_f, 0);
    LogRel2(("ALSA: Period size is: %lu frames (min %lu, requested %lu)\n", period_size_f, minval, pCfgReq->period_size));
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set period size %d (%s)\n", period_size_f, snd_strerror(err)), err);

    /* The buffer size: */
    minval = pCfgReq->buffer_size;
    err = snd_pcm_hw_params_get_buffer_size_min(pHWParms, &minval);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Could not retrieve minimal buffer size: %s\n", snd_strerror(err)), err);

    snd_pcm_uframes_t buffer_size_f = pCfgReq->buffer_size;
    if (buffer_size_f < minval)
        buffer_size_f = minval;
    err = snd_pcm_hw_params_set_buffer_size_near(phPCM, pHWParms, &buffer_size_f);
    LogRel2(("ALSA: Buffer size is: %lu frames (min %lu, requested %lu)\n", buffer_size_f, minval, pCfgReq->buffer_size));
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set near buffer size %RU32: %s\n", buffer_size_f, snd_strerror(err)), err);

    /*
     * Set the hardware parameters.
     */
    err = snd_pcm_hw_params(phPCM, pHWParms);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to apply audio parameters: %s\n", snd_strerror(err)), err);

    /*
     * Get relevant parameters and put them in the pCfgObt structure.
     */
    snd_pcm_uframes_t obt_buffer_size = buffer_size_f;
    err = snd_pcm_hw_params_get_buffer_size(pHWParms, &obt_buffer_size);
    AssertLogRelMsgStmt(err >= 0, ("ALSA: Failed to get buffer size: %s\n", snd_strerror(err)), obt_buffer_size = buffer_size_f);
    pCfgObt->buffer_size = obt_buffer_size;

    snd_pcm_uframes_t obt_period_size = period_size_f;
    err = snd_pcm_hw_params_get_period_size(pHWParms, &obt_period_size, &dir);
    AssertLogRelMsgStmt(err >= 0, ("ALSA: Failed to get period size: %s\n", snd_strerror(err)), obt_period_size = period_size_f);
    pCfgObt->period_size = obt_period_size;

    pCfgObt->access  = pCfgReq->access;
    pCfgObt->fmt     = pCfgReq->fmt;

    LogRel2(("ALSA: HW params: %u Hz, %ul frames period, %ul frames buffer, %u channel(s), fmt=%d, access=%d\n",
             pCfgObt->freq, pCfgObt->period_size, pCfgObt->buffer_size, pCfgObt->nchannels, pCfgObt->fmt, pCfgObt->access));
    return 0;
}


/**
 * Opens (creates) an ALSA stream.
 *
 * @returns VBox status code.
 * @param   pszDev  The name of the device to open.
 * @param   fIn     Whether this is an input stream to create or not.
 * @param   pCfgReq Requested configuration to create stream with.
 * @param   pCfgObt Obtained configuration the stream got created on success.
 * @param   pphPCM  Where to store the ALSA stream handle on success.
 */
static int alsaStreamOpen(const char *pszDev, bool fIn, PALSAAUDIOSTREAMCFG pCfgReq,
                          PALSAAUDIOSTREAMCFG pCfgObt,  snd_pcm_t **pphPCM)
{
    AssertLogRelMsgReturn(pszDev && *pszDev,
                          ("ALSA: Invalid or no %s device name set\n", fIn ? "input" : "output"),
                          VERR_INVALID_NAME);

    /*
     * Open the stream.
     */
    int rc = VERR_AUDIO_STREAM_COULD_NOT_CREATE;
    snd_pcm_t *phPCM = NULL;
    LogRel(("ALSA: Using %s device \"%s\"\n", fIn ? "input" : "output", pszDev));
    int err = snd_pcm_open(&phPCM, pszDev,
                           fIn ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK,
                           SND_PCM_NONBLOCK);
    if (err >= 0)
    {
        err = snd_pcm_nonblock(phPCM, 1);
        if (err >= 0)
        {
            /*
             * Configure hardware stream parameters.
             */
            err = alsaStreamSetHwParams(phPCM, pCfgReq, pCfgObt);
            if (err >= 0)
            {
                /*
                 * Prepare it.
                 */
                rc = VERR_AUDIO_BACKEND_INIT_FAILED;
                err = snd_pcm_prepare(phPCM);
                if (err >= 0)
                {
                    /*
                     * Configure software stream parameters and we're done.
                     */
                    rc = alsaStreamSetSWParams(phPCM, fIn, pCfgReq, pCfgObt);
                    if (RT_SUCCESS(rc))
                    {
                        *pphPCM = phPCM;
                        return VINF_SUCCESS;
                    }
                }
                else
                    LogRel(("ALSA: snd_pcm_prepare failed: %s\n", snd_strerror(err)));
            }
        }
        else
            LogRel(("ALSA: Error setting output non-blocking mode: %s\n", snd_strerror(err)));
        alsaStreamClose(&phPCM);
    }
    else
        LogRel(("ALSA: Failed to open \"%s\" as %s device: %s\n", pszDev, fIn ? "input" : "output", snd_strerror(err)));
    return rc;
}


#ifdef DEBUG
static void alsaDbgErrorHandler(const char *file, int line, const char *function,
                                int err, const char *fmt, ...)
{
    /** @todo Implement me! */
    RT_NOREF(file, line, function, err, fmt);
}
#endif

/**
 * Returns the available audio frames queued.
 *
 * @returns VBox status code.
 * @param   phPCM               ALSA stream handle.
 * @param   pFramesAvail        Where to store the available frames.
 */
static int alsaStreamGetAvail(snd_pcm_t *phPCM, snd_pcm_sframes_t *pFramesAvail)
{
    AssertPtrReturn(phPCM, VERR_INVALID_POINTER);
    /* pFramesAvail is optional. */

    int rc;

    snd_pcm_sframes_t framesAvail = snd_pcm_avail_update(phPCM);
    if (framesAvail < 0)
    {
        if (framesAvail == -EPIPE)
        {
            rc = alsaStreamRecover(phPCM);
            if (RT_SUCCESS(rc))
                framesAvail = snd_pcm_avail_update(phPCM);
        }
        else
            rc = VERR_ACCESS_DENIED; /** @todo Find a better rc. */
    }
    else
        rc = VINF_SUCCESS;

    if (RT_SUCCESS(rc))
    {
        if (pFramesAvail)
            *pFramesAvail = framesAvail;
    }

    LogFunc(("cFrames=%ld, rc=%Rrc\n", framesAvail, rc));
    return rc;
}

/**
 * Tries to recover an ALSA stream.
 *
 * @returns VBox status code.
 * @param   phPCM               ALSA stream handle.
 */
static int alsaStreamRecover(snd_pcm_t *phPCM)
{
    AssertPtrReturn(phPCM, VERR_INVALID_POINTER);

    int err = snd_pcm_prepare(phPCM);
    if (err < 0)
    {
        LogFunc(("Failed to recover stream %p: %s\n", phPCM, snd_strerror(err)));
        return VERR_ACCESS_DENIED; /** @todo Find a better rc. */
    }

    return VINF_SUCCESS;
}

/**
 * Resumes an ALSA stream.
 *
 * @returns VBox status code.
 * @param   phPCM               ALSA stream to resume.
 */
static int alsaStreamResume(snd_pcm_t *phPCM)
{
    AssertPtrReturn(phPCM, VERR_INVALID_POINTER);

    int err = snd_pcm_resume(phPCM);
    if (err < 0)
    {
        LogFunc(("Failed to resume stream %p: %s\n", phPCM, snd_strerror(err)));
        return VERR_ACCESS_DENIED; /** @todo Find a better rc. */
    }

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnInit}
 */
static DECLCALLBACK(int) drvHostAlsaAudioHA_Init(PPDMIHOSTAUDIO pInterface)
{
    RT_NOREF(pInterface);

    LogFlowFuncEnter();

    int rc = audioLoadAlsaLib();
    if (RT_FAILURE(rc))
        LogRel(("ALSA: Failed to load the ALSA shared library, rc=%Rrc\n", rc));
    else
    {
#ifdef DEBUG
        snd_lib_error_set_handler(alsaDbgErrorHandler);
#endif
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCapture}
 */
static DECLCALLBACK(int) drvHostAlsaAudioHA_StreamCapture(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                          void *pvBuf, uint32_t uBufSize, uint32_t *puRead)
{
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    AssertPtrReturn(pStream,    VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf,      VERR_INVALID_POINTER);
    AssertReturn(uBufSize,         VERR_INVALID_PARAMETER);
    /* pcbRead is optional. */

    PALSAAUDIOSTREAM pStreamALSA = (PALSAAUDIOSTREAM)pStream;

    snd_pcm_sframes_t cAvail;
    int rc = alsaStreamGetAvail(pStreamALSA->phPCM, &cAvail);
    if (RT_FAILURE(rc))
    {
        LogFunc(("Error getting number of captured frames, rc=%Rrc\n", rc));
        return rc;
    }

    PPDMAUDIOSTREAMCFG pCfg = pStreamALSA->pCfg;
    AssertPtr(pCfg);

    if (!cAvail) /* No data yet? */
    {
        snd_pcm_state_t state = snd_pcm_state(pStreamALSA->phPCM);
        switch (state)
        {
            case SND_PCM_STATE_PREPARED:
                cAvail = PDMAUDIOSTREAMCFG_B2F(pCfg, uBufSize);
                break;

            case SND_PCM_STATE_SUSPENDED:
            {
                rc = alsaStreamResume(pStreamALSA->phPCM);
                if (RT_FAILURE(rc))
                    break;

                LogFlow(("Resuming suspended input stream\n"));
                break;
            }

            default:
                LogFlow(("No frames available, state=%d\n", state));
                break;
        }

        if (!cAvail)
        {
            if (puRead)
                *puRead = 0;
            return VINF_SUCCESS;
        }
    }

    /*
     * Check how much we can read from the capture device without overflowing
     * the mixer buffer.
     */
    size_t cbToRead = RT_MIN((size_t)PDMAUDIOSTREAMCFG_F2B(pCfg, cAvail), uBufSize);

    LogFlowFunc(("cbToRead=%zu, cAvail=%RI32\n", cbToRead, cAvail));

    uint32_t cbReadTotal = 0;

    snd_pcm_uframes_t cToRead;
    snd_pcm_sframes_t cRead;

    while (   cbToRead
           && RT_SUCCESS(rc))
    {
        cToRead = RT_MIN(PDMAUDIOSTREAMCFG_B2F(pCfg, cbToRead),
                         PDMAUDIOSTREAMCFG_B2F(pCfg, pStreamALSA->cbBuf));
        AssertBreakStmt(cToRead, rc = VERR_NO_DATA);
        cRead = snd_pcm_readi(pStreamALSA->phPCM, pStreamALSA->pvBuf, cToRead);
        if (cRead <= 0)
        {
            switch (cRead)
            {
                case 0:
                {
                    LogFunc(("No input frames available\n"));
                    rc = VERR_ACCESS_DENIED;
                    break;
                }

                case -EAGAIN:
                {
                    /*
                     * Don't set error here because EAGAIN means there are no further frames
                     * available at the moment, try later. As we might have read some frames
                     * already these need to be processed instead.
                     */
                    cbToRead = 0;
                    break;
                }

                case -EPIPE:
                {
                    rc = alsaStreamRecover(pStreamALSA->phPCM);
                    if (RT_FAILURE(rc))
                        break;

                    LogFlowFunc(("Recovered from capturing\n"));
                    continue;
                }

                default:
                {
                    LogFunc(("Failed to read input frames: %s\n", snd_strerror(cRead)));
                    rc = VERR_GENERAL_FAILURE; /** @todo Fudge! */
                    break;
                }
            }
        }
        else
        {
            /*
             * We should not run into a full mixer buffer or we loose samples and
             * run into an endless loop if ALSA keeps producing samples ("null"
             * capture device for example).
             */
            uint32_t cbRead = PDMAUDIOSTREAMCFG_F2B(pCfg, cRead);

            memcpy(pvBuf, pStreamALSA->pvBuf, cbRead);

            Assert(cbToRead >= cbRead);
            cbToRead    -= cbRead;
            cbReadTotal += cbRead;
        }
    }

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
static DECLCALLBACK(int) drvHostAlsaAudioHA_StreamPlay(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                       const void *pvBuf, uint32_t uBufSize, uint32_t *puWritten)
{
    PALSAAUDIOSTREAM pStreamALSA = (PALSAAUDIOSTREAM)pStream;
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    AssertPtrReturn(pStream,    VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf,      VERR_INVALID_POINTER);
    AssertReturn(uBufSize,         VERR_INVALID_PARAMETER);
    /* puWritten is optional. */
    Log4Func(("pvBuf=%p uBufSize=%#x (%u) state=%s - %s\n", pvBuf, uBufSize, uBufSize,
              snd_pcm_state_name(snd_pcm_state(pStreamALSA->phPCM)), pStreamALSA->pCfg->szName));

    PPDMAUDIOSTREAMCFG pCfg = pStreamALSA->pCfg;
    AssertPtr(pCfg);

    int rc;

    uint32_t cbWrittenTotal = 0;

    do
    {
        snd_pcm_sframes_t csAvail;
        rc = alsaStreamGetAvail(pStreamALSA->phPCM, &csAvail);
        if (RT_FAILURE(rc))
        {
            LogFunc(("Error getting number of playback frames, rc=%Rrc\n", rc));
            break;
        }

        if (!csAvail)
            break;

        size_t cbToWrite = RT_MIN((unsigned)PDMAUDIOSTREAMCFG_F2B(pCfg, csAvail), pStreamALSA->cbBuf);
        if (!cbToWrite)
            break;

        /* Do not write more than available. */
        if (cbToWrite > uBufSize)
            cbToWrite = uBufSize;

        memcpy(pStreamALSA->pvBuf, pvBuf, cbToWrite);

        snd_pcm_sframes_t csWritten = 0;

        /* Don't try infinitely on recoverable errors. */
        unsigned iTry;
        for (iTry = 0; iTry < ALSA_RECOVERY_TRIES_MAX; iTry++)
        {
            csWritten = snd_pcm_writei(pStreamALSA->phPCM, pStreamALSA->pvBuf,
                                       PDMAUDIOSTREAMCFG_B2F(pCfg, cbToWrite));
            Log4Func(("snd_pcm_writei w/ cbToWrite=%u -> %ld (frames) [csAvail=%ld]\n", cbToWrite, csWritten, csAvail));
            if (csWritten <= 0)
            {
                switch (csWritten)
                {
                    case 0:
                    {
                        LogFunc(("Failed to write %zu bytes\n", cbToWrite));
                        rc = VERR_ACCESS_DENIED;
                        break;
                    }

                    case -EPIPE:
                    {
                        rc = alsaStreamRecover(pStreamALSA->phPCM);
                        if (RT_FAILURE(rc))
                            break;

                        LogFlowFunc(("Recovered from playback\n"));
                        continue;
                    }

                    case -ESTRPIPE:
                    {
                        /* Stream was suspended and waiting for a recovery. */
                        rc = alsaStreamResume(pStreamALSA->phPCM);
                        if (RT_FAILURE(rc))
                        {
                            LogRel(("ALSA: Failed to resume output stream\n"));
                            break;
                        }

                        LogFlowFunc(("Resumed suspended output stream\n"));
                        continue;
                    }

                    default:
                        LogFlowFunc(("Failed to write %RU32 bytes, error unknown\n", cbToWrite));
                        rc = VERR_GENERAL_FAILURE; /** @todo */
                        break;
                }
            }
            else
                break;
        } /* For number of tries. */

        if (   iTry == ALSA_RECOVERY_TRIES_MAX
            && csWritten <= 0)
            rc = VERR_BROKEN_PIPE;

        if (RT_FAILURE(rc))
            break;

        cbWrittenTotal = PDMAUDIOSTREAMCFG_F2B(pCfg, csWritten);

    } while (0);

    if (RT_SUCCESS(rc))
    {
        if (puWritten)
            *puWritten = cbWrittenTotal;
    }

    return rc;
}

/**
 * Destroys an ALSA input stream.
 *
 * @returns VBox status code.
 * @param   pStreamALSA         ALSA input stream to destroy.
 */
static int alsaDestroyStreamIn(PALSAAUDIOSTREAM pStreamALSA)
{
    alsaStreamClose(&pStreamALSA->phPCM);

    if (pStreamALSA->pvBuf)
    {
        RTMemFree(pStreamALSA->pvBuf);
        pStreamALSA->pvBuf = NULL;
    }

    return VINF_SUCCESS;
}

/**
 * Destroys an ALSA output stream.
 *
 * @returns VBox status code.
 * @param   pStreamALSA         ALSA output stream to destroy.
 */
static int alsaDestroyStreamOut(PALSAAUDIOSTREAM pStreamALSA)
{
    alsaStreamClose(&pStreamALSA->phPCM);

    if (pStreamALSA->pvBuf)
    {
        RTMemFree(pStreamALSA->pvBuf);
        pStreamALSA->pvBuf = NULL;
    }

    return VINF_SUCCESS;
}

/**
 * Creates an ALSA output stream.
 *
 * @returns VBox status code.
 * @param   pThis       The ALSA driver instance data.
 * @param   pStreamALSA ALSA output stream to create.
 * @param   pCfgReq     Requested configuration to create stream with.
 * @param   pCfgAcq     Obtained configuration the stream got created
 *                      with on success.
 */
static int alsaCreateStreamOut(PDRVHOSTALSAAUDIO pThis, PALSAAUDIOSTREAM pStreamALSA,
                               PPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    snd_pcm_t *phPCM = NULL;

    int rc;

    do
    {
        ALSAAUDIOSTREAMCFG req;
        req.fmt         = alsaAudioPropsToALSA(&pCfgReq->Props);
        req.freq        = pCfgReq->Props.uHz;
        req.nchannels   = pCfgReq->Props.cChannels;
        req.period_size = pCfgReq->Backend.cFramesPeriod;
        req.buffer_size = pCfgReq->Backend.cFramesBufferSize;
        req.threshold   = pCfgReq->Backend.cFramesPreBuffering;

        ALSAAUDIOSTREAMCFG obt;
        rc = alsaStreamOpen(pThis->szDefaultOut, false /* fIn */, &req, &obt, &phPCM);
        if (RT_FAILURE(rc))
            break;

        pCfgAcq->Props.uHz       = obt.freq;
        pCfgAcq->Props.cChannels = obt.nchannels;

        rc = alsaALSAToAudioProps(obt.fmt, &pCfgAcq->Props);
        if (RT_FAILURE(rc))
            break;

        pCfgAcq->Backend.cFramesPeriod     = obt.period_size;
        pCfgAcq->Backend.cFramesBufferSize = obt.buffer_size;
        pCfgAcq->Backend.cFramesPreBuffering     = obt.threshold;

        pStreamALSA->cbBuf = pCfgAcq->Backend.cFramesBufferSize * PDMAudioPropsBytesPerFrame(&pCfgAcq->Props);
        pStreamALSA->pvBuf = RTMemAllocZ(pStreamALSA->cbBuf);
        if (!pStreamALSA->pvBuf)
        {
            LogRel(("ALSA: Not enough memory for output DAC buffer (%zu frames)\n", pCfgAcq->Backend.cFramesBufferSize));
            rc = VERR_NO_MEMORY;
            break;
        }

        pStreamALSA->phPCM = phPCM;
    }
    while (0);

    if (RT_FAILURE(rc))
        alsaStreamClose(&phPCM);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Creates an ALSA input stream.
 *
 * @returns VBox status code.
 * @param   pThis       The ALSA driver instance data.
 * @param   pStreamALSA ALSA input stream to create.
 * @param   pCfgReq     Requested configuration to create stream with.
 * @param   pCfgAcq     Obtained configuration the stream got created
 *                      with on success.
 */
static int alsaCreateStreamIn(PDRVHOSTALSAAUDIO pThis, PALSAAUDIOSTREAM pStreamALSA,
                              PPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    int rc;

    snd_pcm_t *phPCM = NULL;

    do
    {
        ALSAAUDIOSTREAMCFG req;
        req.fmt         = alsaAudioPropsToALSA(&pCfgReq->Props);
        req.freq        = pCfgReq->Props.uHz;
        req.nchannels   = pCfgReq->Props.cChannels;
        req.period_size = PDMAudioPropsMilliToFrames(&pCfgReq->Props, 50 /*ms*/); /** @todo Make this configurable. */
        req.buffer_size = req.period_size * 2; /** @todo Make this configurable. */
        req.threshold   = req.period_size;

        ALSAAUDIOSTREAMCFG obt;
        rc = alsaStreamOpen(pThis->szDefaultIn, true /* fIn */, &req, &obt, &phPCM);
        if (RT_FAILURE(rc))
            break;

        pCfgAcq->Props.uHz       = obt.freq;
        pCfgAcq->Props.cChannels = obt.nchannels;

        rc = alsaALSAToAudioProps(obt.fmt, &pCfgAcq->Props);
        if (RT_FAILURE(rc))
            break;

        pCfgAcq->Backend.cFramesPeriod     = obt.period_size;
        pCfgAcq->Backend.cFramesBufferSize = obt.buffer_size;
        /* No pre-buffering. */

        pStreamALSA->cbBuf = pCfgAcq->Backend.cFramesBufferSize * PDMAudioPropsBytesPerFrame(&pCfgAcq->Props);
        pStreamALSA->pvBuf = RTMemAlloc(pStreamALSA->cbBuf);
        if (!pStreamALSA->pvBuf)
        {
            LogRel(("ALSA: Not enough memory for input ADC buffer (%zu frames)\n", pCfgAcq->Backend.cFramesBufferSize));
            rc = VERR_NO_MEMORY;
            break;
        }

        pStreamALSA->phPCM = phPCM;
    }
    while (0);

    if (RT_FAILURE(rc))
        alsaStreamClose(&phPCM);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Controls an ALSA input stream.
 *
 * @returns VBox status code.
 * @param   pStreamALSA         ALSA input stream to control.
 * @param   enmStreamCmd        Stream command to issue.
 */
static int alsaControlStreamIn(PALSAAUDIOSTREAM pStreamALSA, PDMAUDIOSTREAMCMD enmStreamCmd)
{
    int rc = VINF_SUCCESS;

    int err;

    switch (enmStreamCmd)
    {
        case PDMAUDIOSTREAMCMD_ENABLE:
        case PDMAUDIOSTREAMCMD_RESUME:
        {
            err = snd_pcm_prepare(pStreamALSA->phPCM);
            if (err < 0)
            {
                LogRel(("ALSA: Error preparing input stream: %s\n", snd_strerror(err)));
                rc = VERR_ACCESS_DENIED; /** @todo Find a better rc. */
            }
            else
            {
                Assert(snd_pcm_state(pStreamALSA->phPCM) == SND_PCM_STATE_PREPARED);

                /* Only start the PCM stream for input streams. */
                err = snd_pcm_start(pStreamALSA->phPCM);
                if (err < 0)
                {
                    LogRel(("ALSA: Error starting input stream: %s\n", snd_strerror(err)));
                    rc = VERR_ACCESS_DENIED; /** @todo Find a better rc. */
                }
            }

            break;
        }

        case PDMAUDIOSTREAMCMD_DISABLE:
        {
            err = snd_pcm_drop(pStreamALSA->phPCM);
            if (err < 0)
            {
                LogRel(("ALSA: Error disabling input stream: %s\n", snd_strerror(err)));
                rc = VERR_ACCESS_DENIED; /** @todo Find a better rc. */
            }
            break;
        }

        case PDMAUDIOSTREAMCMD_PAUSE:
        {
            err = snd_pcm_drop(pStreamALSA->phPCM);
            if (err < 0)
            {
                LogRel(("ALSA: Error pausing input stream: %s\n", snd_strerror(err)));
                rc = VERR_ACCESS_DENIED; /** @todo Find a better rc. */
            }
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
 * Controls an ALSA output stream.
 *
 * @returns VBox status code.
 * @param   pStreamALSA         ALSA output stream to control.
 * @param   enmStreamCmd        Stream command to issue.
 */
static int alsaControlStreamOut(PALSAAUDIOSTREAM pStreamALSA, PDMAUDIOSTREAMCMD enmStreamCmd)
{
    int rc = VINF_SUCCESS;

    int err;

    switch (enmStreamCmd)
    {
        case PDMAUDIOSTREAMCMD_ENABLE:
        case PDMAUDIOSTREAMCMD_RESUME:
        {
            err = snd_pcm_prepare(pStreamALSA->phPCM);
            if (err < 0)
            {
                LogRel(("ALSA: Error preparing output stream: %s\n", snd_strerror(err)));
                rc = VERR_ACCESS_DENIED; /** @todo Find a better rc. */
            }
            else
            {
                Assert(snd_pcm_state(pStreamALSA->phPCM) == SND_PCM_STATE_PREPARED);
            }

            break;
        }

        case PDMAUDIOSTREAMCMD_DISABLE:
        {
            err = snd_pcm_drop(pStreamALSA->phPCM);
            if (err < 0)
            {
                LogRel(("ALSA: Error disabling output stream: %s\n", snd_strerror(err)));
                rc = VERR_ACCESS_DENIED; /** @todo Find a better rc. */
            }
            break;
        }

        case PDMAUDIOSTREAMCMD_PAUSE:
        {
            err = snd_pcm_drop(pStreamALSA->phPCM);
            if (err < 0)
            {
                LogRel(("ALSA: Error pausing output stream: %s\n", snd_strerror(err)));
                rc = VERR_ACCESS_DENIED; /** @todo Find a better rc. */
            }
            break;
        }

        case PDMAUDIOSTREAMCMD_DRAIN:
        {
            snd_pcm_state_t streamState = snd_pcm_state(pStreamALSA->phPCM);
            Log2Func(("Stream state is: %d\n", streamState));

            if (   streamState == SND_PCM_STATE_PREPARED
                || streamState == SND_PCM_STATE_RUNNING)
            {
                err = snd_pcm_nonblock(pStreamALSA->phPCM, 0);
                if (err < 0)
                {
                    LogRel(("ALSA: Error disabling output non-blocking mode: %s\n", snd_strerror(err)));
                    rc = VERR_ACCESS_DENIED; /** @todo Find a better rc. */
                    break;
                }

                err = snd_pcm_drain(pStreamALSA->phPCM);
                if (err < 0)
                {
                    LogRel(("ALSA: Error draining output: %s\n", snd_strerror(err)));
                    rc = VERR_ACCESS_DENIED; /** @todo Find a better rc. */
                    break;
                }

                err = snd_pcm_nonblock(pStreamALSA->phPCM, 1);
                if (err < 0)
                {
                    LogRel(("ALSA: Error re-enabling output non-blocking mode: %s\n", snd_strerror(err)));
                    rc = VERR_ACCESS_DENIED; /** @todo Find a better rc. */
                }
            }
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
static DECLCALLBACK(int) drvHostAlsaAudioHA_GetConfig(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDCFG pBackendCfg)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pBackendCfg, VERR_INVALID_POINTER);

    RTStrPrintf2(pBackendCfg->szName, sizeof(pBackendCfg->szName), "ALSA");

    pBackendCfg->cbStreamIn  = sizeof(ALSAAUDIOSTREAM);
    pBackendCfg->cbStreamOut = sizeof(ALSAAUDIOSTREAM);

    /* Enumerate sound devices. */
    char **pszHints;
    int err = snd_device_name_hint(-1 /* All cards */, "pcm", (void***)&pszHints);
    if (err == 0)
    {
        char** pszHintCur = pszHints;
        while (*pszHintCur != NULL)
        {
            char *pszDev = snd_device_name_get_hint(*pszHintCur, "NAME");
            bool fSkip =    !pszDev
                         || !RTStrICmp("null", pszDev);
            if (fSkip)
            {
                if (pszDev)
                    free(pszDev);
                pszHintCur++;
                continue;
            }

            char *pszIOID = snd_device_name_get_hint(*pszHintCur, "IOID");
            if (pszIOID)
            {
#if 0
                if (!RTStrICmp("input", pszIOID))

                else if (!RTStrICmp("output", pszIOID))
#endif
            }
            else /* NULL means bidirectional, input + output. */
            {
            }

            LogRel2(("ALSA: Found %s device: %s\n", pszIOID ?  RTStrToLower(pszIOID) : "bidirectional", pszDev));

            /* Special case for ALSAAudio. */
            if (   pszDev
                && RTStrIStr("pulse", pszDev) != NULL)
                LogRel2(("ALSA: ALSAAudio plugin in use\n"));

            if (pszIOID)
                free(pszIOID);

            if (pszDev)
                free(pszDev);

            pszHintCur++;
        }

        snd_device_name_free_hint((void **)pszHints);
        pszHints = NULL;
    }
    else
        LogRel2(("ALSA: Error enumerating PCM devices: %Rrc (%d)\n", RTErrConvertFromErrno(err), err));

    /* ALSA allows exactly one input and one output used at a time for the selected device(s). */
    pBackendCfg->cMaxStreamsIn  = 1;
    pBackendCfg->cMaxStreamsOut = 1;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnShutdown}
 */
static DECLCALLBACK(void) drvHostAlsaAudioHA_Shutdown(PPDMIHOSTAUDIO pInterface)
{
    RT_NOREF(pInterface);
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetStatus}
 */
static DECLCALLBACK(PDMAUDIOBACKENDSTS) drvHostAlsaAudioHA_GetStatus(PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir)
{
    RT_NOREF(enmDir);
    AssertPtrReturn(pInterface, PDMAUDIOBACKENDSTS_UNKNOWN);

    return PDMAUDIOBACKENDSTS_RUNNING;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCreate}
 */
static DECLCALLBACK(int) drvHostAlsaAudioHA_StreamCreate(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                         PPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    PDRVHOSTALSAAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVHOSTALSAAUDIO, IHostAudio);
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    AssertPtrReturn(pStream,    VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgReq,    VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgAcq,    VERR_INVALID_POINTER);

    PALSAAUDIOSTREAM pStreamALSA = (PALSAAUDIOSTREAM)pStream;

    int rc;
    if (pCfgReq->enmDir == PDMAUDIODIR_IN)
        rc = alsaCreateStreamIn( pThis, pStreamALSA, pCfgReq, pCfgAcq);
    else
        rc = alsaCreateStreamOut(pThis, pStreamALSA, pCfgReq, pCfgAcq);

    if (RT_SUCCESS(rc))
    {
        pStreamALSA->pCfg = PDMAudioStrmCfgDup(pCfgAcq);
        if (!pStreamALSA->pCfg)
            rc = VERR_NO_MEMORY;
    }

    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDestroy}
 */
static DECLCALLBACK(int) drvHostAlsaAudioHA_StreamDestroy(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    AssertPtrReturn(pStream,    VERR_INVALID_POINTER);

    PALSAAUDIOSTREAM pStreamALSA = (PALSAAUDIOSTREAM)pStream;

    if (!pStreamALSA->pCfg) /* Not (yet) configured? Skip. */
        return VINF_SUCCESS;

    int rc;
    if (pStreamALSA->pCfg->enmDir == PDMAUDIODIR_IN)
        rc = alsaDestroyStreamIn(pStreamALSA);
    else
        rc = alsaDestroyStreamOut(pStreamALSA);

    if (RT_SUCCESS(rc))
    {
        PDMAudioStrmCfgFree(pStreamALSA->pCfg);
        pStreamALSA->pCfg = NULL;
    }

    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamControl}
 */
static DECLCALLBACK(int) drvHostAlsaAudioHA_StreamControl(PPDMIHOSTAUDIO pInterface,
                                                          PPDMAUDIOBACKENDSTREAM pStream, PDMAUDIOSTREAMCMD enmStreamCmd)
{
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    AssertPtrReturn(pStream,    VERR_INVALID_POINTER);

    PALSAAUDIOSTREAM pStreamALSA = (PALSAAUDIOSTREAM)pStream;

    if (!pStreamALSA->pCfg) /* Not (yet) configured? Skip. */
        return VINF_SUCCESS;

    int rc;
    if (pStreamALSA->pCfg->enmDir == PDMAUDIODIR_IN)
        rc = alsaControlStreamIn (pStreamALSA, enmStreamCmd);
    else
        rc = alsaControlStreamOut(pStreamALSA, enmStreamCmd);

    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetReadable}
 */
static DECLCALLBACK(uint32_t) drvHostAlsaAudioHA_StreamGetReadable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);

    PALSAAUDIOSTREAM pStreamALSA = (PALSAAUDIOSTREAM)pStream;

    uint32_t cbAvail = 0;

    snd_pcm_sframes_t cFramesAvail;
    int rc = alsaStreamGetAvail(pStreamALSA->phPCM, &cFramesAvail);
    if (RT_SUCCESS(rc))
        cbAvail = PDMAUDIOSTREAMCFG_F2B(pStreamALSA->pCfg, cFramesAvail);

    return cbAvail;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetWritable}
 */
static DECLCALLBACK(uint32_t) drvHostAlsaAudioHA_StreamGetWritable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);

    PALSAAUDIOSTREAM pStreamALSA = (PALSAAUDIOSTREAM)pStream;

    uint32_t cbAvail = 0;

    snd_pcm_sframes_t cFramesAvail;
    int rc = alsaStreamGetAvail(pStreamALSA->phPCM, &cFramesAvail);
    if (RT_SUCCESS(rc))
        cbAvail = PDMAUDIOSTREAMCFG_F2B(pStreamALSA->pCfg, cFramesAvail);

    return cbAvail;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetPending}
 */
static DECLCALLBACK(uint32_t) drvHostALSAStreamGetPending(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pStream, 0);

    PALSAAUDIOSTREAM pStreamALSA = (PALSAAUDIOSTREAM)pStream;

    snd_pcm_sframes_t cFramesDelay  = 0;
    snd_pcm_state_t   enmState = snd_pcm_state(pStreamALSA->phPCM);

    int rc = VINF_SUCCESS;

    AssertPtr(pStreamALSA->pCfg);
    if (pStreamALSA->pCfg->enmDir == PDMAUDIODIR_OUT)
    {
        /* Getting the delay (in audio frames) reports the time it will take
         * to hear a new sample after all queued samples have been played out. */
        int rc2 = snd_pcm_delay(pStreamALSA->phPCM, &cFramesDelay);
        if (RT_SUCCESS(rc))
            rc = rc2;

        /* Make sure to check the stream's status.
         * If it's anything but SND_PCM_STATE_RUNNING, the delay is meaningless and therefore 0. */
        if (enmState != SND_PCM_STATE_RUNNING)
            cFramesDelay = 0;
    }

    /* Note: For input streams we never have pending data left. */

    Log2Func(("cFramesDelay=%RI32, enmState=%d, rc=%d\n", cFramesDelay, enmState, rc));

    return PDMAudioPropsFramesToBytes(&pStreamALSA->pCfg->Props, cFramesDelay);
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetStatus}
 */
static DECLCALLBACK(PDMAUDIOSTREAMSTS) drvHostAlsaAudioHA_StreamGetStatus(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);

    return PDMAUDIOSTREAMSTS_FLAGS_INITIALIZED | PDMAUDIOSTREAMSTS_FLAGS_ENABLED;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamIterate}
 */
static DECLCALLBACK(int) drvHostAlsaAudioHA_StreamIterate(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    AssertPtrReturn(pStream,    VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    /* Nothing to do here for ALSA. */
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvHostAlsaAudioQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS        pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVHOSTALSAAUDIO pThis   = PDMINS_2_DATA(pDrvIns, PDRVHOSTALSAAUDIO);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHOSTAUDIO, &pThis->IHostAudio);

    return NULL;
}


/**
 * Construct a DirectSound Audio driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvHostAlsaAudioConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVHOSTALSAAUDIO pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTALSAAUDIO);
    LogRel(("Audio: Initializing ALSA driver\n"));

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                   = pDrvIns;
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface = drvHostAlsaAudioQueryInterface;
    /* IHostAudio */
    pThis->IHostAudio.pfnInit               = drvHostAlsaAudioHA_Init;
    pThis->IHostAudio.pfnShutdown           = drvHostAlsaAudioHA_Shutdown;
    pThis->IHostAudio.pfnGetConfig          = drvHostAlsaAudioHA_GetConfig;
    pThis->IHostAudio.pfnGetStatus          = drvHostAlsaAudioHA_GetStatus;
    pThis->IHostAudio.pfnStreamCreate       = drvHostAlsaAudioHA_StreamCreate;
    pThis->IHostAudio.pfnStreamDestroy      = drvHostAlsaAudioHA_StreamDestroy;
    pThis->IHostAudio.pfnStreamControl      = drvHostAlsaAudioHA_StreamControl;
    pThis->IHostAudio.pfnStreamGetReadable  = drvHostAlsaAudioHA_StreamGetReadable;
    pThis->IHostAudio.pfnStreamGetWritable  = drvHostAlsaAudioHA_StreamGetWritable;
    pThis->IHostAudio.pfnStreamGetStatus    = drvHostAlsaAudioHA_StreamGetStatus;
    pThis->IHostAudio.pfnStreamIterate      = drvHostAlsaAudioHA_StreamIterate;
    pThis->IHostAudio.pfnStreamPlay         = drvHostAlsaAudioHA_StreamPlay;
    pThis->IHostAudio.pfnStreamCapture      = drvHostAlsaAudioHA_StreamCapture;
    pThis->IHostAudio.pfnSetCallback        = NULL;
    pThis->IHostAudio.pfnGetDevices         = NULL;
    pThis->IHostAudio.pfnStreamGetPending   = drvHostALSAStreamGetPending;
    pThis->IHostAudio.pfnStreamPlayBegin    = NULL;
    pThis->IHostAudio.pfnStreamPlayEnd      = NULL;
    pThis->IHostAudio.pfnStreamCaptureBegin = NULL;
    pThis->IHostAudio.pfnStreamCaptureEnd   = NULL;

    /*
     * Read configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "StreamName|DefaultOutput|DefaultInput", "");

    int rc = CFGMR3QueryStringDef(pCfg, "DefaultInput", pThis->szDefaultIn, sizeof(pThis->szDefaultIn), "default");
    AssertRCReturn(rc, rc);
    rc = CFGMR3QueryStringDef(pCfg, "DefaultOutput", pThis->szDefaultOut, sizeof(pThis->szDefaultOut), "default");
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}


/**
 * Char driver registration record.
 */
const PDMDRVREG g_DrvHostALSAAudio =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "ALSAAudio",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "ALSA host audio driver",
    /* fFlags */
     PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_AUDIO,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVHOSTALSAAUDIO),
    /* pfnConstruct */
    drvHostAlsaAudioConstruct,
    /* pfnDestruct */
    NULL,
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

