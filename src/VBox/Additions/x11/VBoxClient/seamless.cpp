/* $Id: seamless.cpp $ */
/** @file
 * X11 Guest client - seamless mode: main logic, communication with the host and
 * wrapper interface for the main code of the VBoxClient deamon.  The
 * X11-specific parts are split out into their own file for ease of testing.
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
*   Header files                                                                                                                 *
*********************************************************************************************************************************/
#include <X11/Xlib.h>

#include <VBox/log.h>
#include <VBox/VBoxGuestLib.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>

#include "VBoxClient.h"
#include "seamless.h"

#include <new>

SeamlessMain::SeamlessMain(void)
{
    LogRelFlowFuncEnter();
    mX11MonitorThread = NIL_RTTHREAD;
    mX11MonitorThreadStopping = false;
    mMode = VMMDev_Seamless_Disabled;
    mfPaused = true;
}

SeamlessMain::~SeamlessMain()
{
    LogRelFlowFuncEnter();
    stop();
}

/**
 * Update the set of visible rectangles in the host.
 */
static void sendRegionUpdate(RTRECT *pRects, size_t cRects)
{
    LogRelFlowFuncEnter();
    if (cRects && !pRects)  /* Assertion */
    {
        VBClLogError(("Region update called with NULL pointer!\n"));
        return;
    }
    VbglR3SeamlessSendRects(cRects, pRects);
    LogRelFlowFuncLeave();
}

/**
 * initialise the service.
 */
int SeamlessMain::init(void)
{
    int rc;
    const char *pcszStage;

    LogRelFlowFuncEnter();
    do {
        pcszStage = "Connecting to the X server";
        rc = mX11Monitor.init(sendRegionUpdate);
        if (RT_FAILURE(rc))
            break;
        pcszStage = "Setting guest IRQ filter mask";
        rc = VbglR3CtlFilterMask(VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST, 0);
        if (RT_FAILURE(rc))
            break;
        pcszStage = "Reporting support for seamless capability";
        rc = VbglR3SeamlessSetCap(true);
        if (RT_FAILURE(rc))
            break;
        rc = startX11MonitorThread();
        if (RT_FAILURE(rc))
            break;
    } while(0);
    if (RT_FAILURE(rc))
        VBClLogError("Failed to start in stage '%s' -- error: %Rrc\n", pcszStage, rc);
    return rc;
}

/**
 * Run the main service thread which listens for host state change
 * notifications.
 * @returns iprt status value.  Service will be set to the stopped state on
 *          failure.
 */
int SeamlessMain::run(void)
{
    int rc = VINF_SUCCESS;

    LogRelFlowFuncEnter();
    /* This will only exit if something goes wrong. */
    while (RT_SUCCESS(rc) || rc == VERR_INTERRUPTED)
    {
        if (RT_FAILURE(rc))
            /* If we are not stopping, sleep for a bit to avoid using up too
                much CPU while retrying. */
            RTThreadYield();
        rc = nextStateChangeEvent();
    }
    if (RT_FAILURE(rc))
    {
        VBClLogError("Event loop failed with error: %Rrc\n", rc);
        stop();
    }
    return rc;
}

/** Stops the service. */
void SeamlessMain::stop()
{
    LogRelFlowFuncEnter();
    VbglR3SeamlessSetCap(false);
    VbglR3CtlFilterMask(0, VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST);
    stopX11MonitorThread();
    mX11Monitor.uninit();
    LogRelFlowFuncLeave();
}

/**
 * Waits for a seamless state change events from the host and dispatch it.
 *
 * @returns        IRPT return code.
 */
int SeamlessMain::nextStateChangeEvent(void)
{
    VMMDevSeamlessMode newMode = VMMDev_Seamless_Disabled;

    LogRelFlowFuncEnter();
    int rc = VbglR3SeamlessWaitEvent(&newMode);
    if (RT_SUCCESS(rc))
    {
        mMode = newMode;
        switch (newMode)
        {
            case VMMDev_Seamless_Visible_Region:
            /* A simplified seamless mode, obtained by making the host VM window
             * borderless and making the guest desktop transparent. */
                LogRelFlowFunc(("\"Visible region\" mode requested (VBoxClient)\n"));
                break;
            case VMMDev_Seamless_Disabled:
                LogRelFlowFunc(("\"Disabled\" mode requested (VBoxClient)\n"));
                break;
            case VMMDev_Seamless_Host_Window:
            /* One host window represents one guest window.  Not yet implemented. */
                LogRelFunc(("Unsupported \"host window\" mode requested (VBoxClient)\n"));
                return VERR_NOT_SUPPORTED;
            default:
                LogRelFunc(("Unsupported mode %d requested (VBoxClient)\n",
                            newMode));
                return VERR_NOT_SUPPORTED;
        }
    }
    if (RT_SUCCESS(rc) || rc == VERR_TRY_AGAIN)
    {
        if (mMode == VMMDev_Seamless_Visible_Region)
            mfPaused = false;
        else
            mfPaused = true;
        mX11Monitor.interruptEventWait();
    }
    else
    {
        LogRelFunc(("VbglR3SeamlessWaitEvent returned %Rrc (VBoxClient)\n", rc));
    }
    LogRelFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * The actual X11 window configuration change monitor thread function.
 */
int SeamlessMain::x11MonitorThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF1(hThreadSelf);
    SeamlessMain *pHost = (SeamlessMain *)pvUser;
    int rc = VINF_SUCCESS;

    LogRelFlowFuncEnter();
    while (!pHost->mX11MonitorThreadStopping)
    {
        if (!pHost->mfPaused)
        {
            rc = pHost->mX11Monitor.start();
            if (RT_FAILURE(rc))
                VBClLogFatalError("Failed to change the X11 seamless service state, mfPaused=%RTbool, rc=%Rrc\n",
                                  pHost->mfPaused, rc);
        }
        pHost->mX11Monitor.nextConfigurationEvent();
        if (pHost->mfPaused || pHost->mX11MonitorThreadStopping)
            pHost->mX11Monitor.stop();
    }
    LogRelFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Start the X11 window configuration change monitor thread.
 */
int SeamlessMain::startX11MonitorThread(void)
{
    int rc;

    mX11MonitorThreadStopping = false;
    if (isX11MonitorThreadRunning())
        return VINF_SUCCESS;
    rc = RTThreadCreate(&mX11MonitorThread, x11MonitorThread, this, 0,
                        RTTHREADTYPE_MSG_PUMP, RTTHREADFLAGS_WAITABLE,
                        "X11 events");
    if (RT_FAILURE(rc))
        LogRelFunc(("Warning: failed to start X11 monitor thread (VBoxClient)\n"));
    return rc;
}

/**
 * Send a signal to the thread function that it should exit
 */
int SeamlessMain::stopX11MonitorThread(void)
{
    int rc;

    mX11MonitorThreadStopping = true;
    if (!isX11MonitorThreadRunning())
        return VINF_SUCCESS;
    mX11Monitor.interruptEventWait();
    rc = RTThreadWait(mX11MonitorThread, RT_INDEFINITE_WAIT, NULL);
    if (RT_SUCCESS(rc))
        mX11MonitorThread = NIL_RTTHREAD;
    else
        LogRelThisFunc(("Failed to stop X11 monitor thread, rc=%Rrc!\n",
                        rc));
    return rc;
}

/** Service magic number, start of a UUID. */
#define SEAMLESSSERVICE_MAGIC 0xd28ba727

/** VBoxClient service class wrapping the logic for the seamless service while
 *  the main VBoxClient code provides the daemon logic needed by all services.
 */
struct SEAMLESSSERVICE
{
    /** The service interface. */
    struct VBCLSERVICE *pInterface;
    /** Magic number for sanity checks. */
    uint32_t magic;
    /** Seamless service object. */
    SeamlessMain mSeamless;
    /** Are we initialised yet? */
    bool mIsInitialised;
};

static const char *getName()
{
    return "Seamless";
}

static const char *getPidFilePath(void)
{
    return ".vboxclient-seamless.pid";
}

static struct SEAMLESSSERVICE *getClassFromInterface(struct VBCLSERVICE **
                                                         ppInterface)
{
    struct SEAMLESSSERVICE *pSelf = (struct SEAMLESSSERVICE *)ppInterface;
    if (pSelf->magic != SEAMLESSSERVICE_MAGIC)
        VBClLogFatalError("Bad seamless service object!\n");
    return pSelf;
}

static int init(struct VBCLSERVICE **ppInterface)
{
    struct SEAMLESSSERVICE *pSelf = getClassFromInterface(ppInterface);

    if (pSelf->mIsInitialised)
        return VERR_INTERNAL_ERROR;

    int rc = pSelf->mSeamless.init();
    if (RT_FAILURE(rc))
        return rc;
    pSelf->mIsInitialised = true;
    return VINF_SUCCESS;
}

static int run(struct VBCLSERVICE **ppInterface, bool fDaemonised)
{
    RT_NOREF1(fDaemonised);
    struct SEAMLESSSERVICE *pSelf = getClassFromInterface(ppInterface);
    int rc;

    if (!pSelf->mIsInitialised)
        return VERR_INTERNAL_ERROR;
    /* This only exits on error. */
    rc = pSelf->mSeamless.run();
    pSelf->mIsInitialised = false;
    return rc;
}

static void cleanup(struct VBCLSERVICE **ppInterface)
{
    RT_NOREF(ppInterface);
    VbglR3SeamlessSetCap(false);
    VbglR3Term();
}

struct VBCLSERVICE vbclSeamlessInterface =
{
    getName,
    getPidFilePath,
    init,
    run,
    cleanup
};

struct VBCLSERVICE **VBClGetSeamlessService()
{
    struct SEAMLESSSERVICE *pService =
        (struct SEAMLESSSERVICE *)RTMemAlloc(sizeof(*pService));

    if (!pService)
        VBClLogFatalError("Out of memory\n");
    pService->pInterface = &vbclSeamlessInterface;
    pService->magic = SEAMLESSSERVICE_MAGIC;
    new(&pService->mSeamless) SeamlessMain();
    pService->mIsInitialised = false;
    return &pService->pInterface;
}
