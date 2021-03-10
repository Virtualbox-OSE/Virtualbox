/* $Id: NetworkServiceRunner.cpp $ */
/** @file
 * VirtualBox Main - interface for VBox DHCP server
 */

/*
 * Copyright (C) 2009-2020 Oracle Corporation
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
#include "NetworkServiceRunner.h"

#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/process.h>
#include <iprt/path.h>
#include <iprt/param.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/*static*/ const char * const NetworkServiceRunner::kpszKeyNetwork   = "--network";
/*static*/ const char * const NetworkServiceRunner::kpszKeyTrunkType = "--trunk-type";
/*static*/ const char * const NetworkServiceRunner::kpszTrunkName    = "--trunk-name";
/*static*/ const char * const NetworkServiceRunner::kpszMacAddress   = "--mac-address";
/*static*/ const char * const NetworkServiceRunner::kpszIpAddress    = "--ip-address";
/*static*/ const char * const NetworkServiceRunner::kpszIpNetmask    = "--netmask";
/*static*/ const char * const NetworkServiceRunner::kpszKeyNeedMain  = "--need-main";


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Internal data the rest of the world does not need to be bothered with.
 *
 * @note no 'm' prefix here, as the runner is accessing it thru an 'm' member.
 */
struct NetworkServiceRunner::Data
{
    /** The process filename. */
    const char *pszProcName;
    /** Actual size of papszArgs. */
    size_t      cArgs;
    /** Number of entries allocated for papszArgs.  */
    size_t      cArgsAlloc;
    /** The argument vector.
     * Each entry is a string allocation via RTStrDup.  The zero'th entry is
     * filled in by start(). */
    char      **papszArgs;
    /** The process ID. */
    RTPROCESS   Process;
    /** Whether to kill the process on stopping.   */
    bool        fKillProcessOnStop;

    Data(const char* aProcName)
        : pszProcName(aProcName)
        , cArgs(0)
        , cArgsAlloc(0)
        , papszArgs(NULL)
        , Process(NIL_RTPROCESS)
        , fKillProcessOnStop(false)
    {}

    ~Data()
    {
        resetArguments();
    }

    void resetArguments()
    {
        for (size_t i = 0; i < cArgs; i++)
            RTStrFree(papszArgs[i]);
        RTMemFree(papszArgs);
        cArgs = 0;
        cArgsAlloc = 0;
        papszArgs = NULL;
    }
};



NetworkServiceRunner::NetworkServiceRunner(const char *aProcName)
{
    m = new NetworkServiceRunner::Data(aProcName);
}


NetworkServiceRunner::~NetworkServiceRunner()
{
    stop();
    delete m;
    m = NULL;
}


/**
 * Adds one argument to the server command line.
 *
 * @returns IPRT status code.
 * @param   pszArgument         The argument to add.
 */
int NetworkServiceRunner::addArgument(const char *pszArgument)
{
    AssertPtr(pszArgument);

    /*
     * Grow the argument vector as needed.
     * Make sure unused space is NULL'ed and that we've got an extra entry for
     * the NULL terminator.  Arguments starts at 1 of course, 0 being the executable.
     */
    size_t const i      = RT_MAX(m->cArgs, 1);
    size_t const cAlloc = m->cArgsAlloc;
    if (i + 1 /*NULL terminator*/ >= m->cArgsAlloc)
    {
        size_t cNewAlloc = cAlloc ? cAlloc : 2;
        do
            cNewAlloc *= 2;
        while (cNewAlloc <= i + 1);
        void *pvNew = RTMemRealloc(m->papszArgs, cNewAlloc * sizeof(m->papszArgs[0]));
        AssertReturn(pvNew, VERR_NO_MEMORY);
        m->papszArgs = (char **)pvNew;
        RT_BZERO(&m->papszArgs[m->cArgsAlloc], (cNewAlloc - cAlloc) * sizeof(m->papszArgs[0]));
        m->cArgsAlloc = cNewAlloc;
    }

    /*
     * Add it.
     */
    m->papszArgs[i] = RTStrDup(pszArgument);
    if (m->papszArgs[i])
    {
        m->cArgs = i + 1;
        Assert(m->papszArgs[m->cArgs] == NULL);
        return VINF_SUCCESS;
    }
    return VERR_NO_STR_MEMORY;
}


/**
 * Adds a pair of arguments, e.g. option + value.
 *
 * @returns IPRT status code.
 */
int NetworkServiceRunner::addArgPair(const char *pszOption, const char *pszValue)
{
    int rc = addArgument(pszOption);
    if (RT_SUCCESS(rc))
        rc = addArgument(pszValue);
    return rc;
}


void NetworkServiceRunner::resetArguments()
{
    m->resetArguments();
}


void NetworkServiceRunner::detachFromServer()
{
    m->Process = NIL_RTPROCESS;
}


int NetworkServiceRunner::start(bool aKillProcessOnStop)
{
    if (isRunning())
        return VINF_ALREADY_INITIALIZED;

    /*
     * Construct the path to the executable and put in into the argument vector.
     * ASSUME it is relative to the directory that holds VBoxSVC.
     */
    char szExePath[RTPATH_MAX];
    AssertReturn(RTProcGetExecutablePath(szExePath, RTPATH_MAX), VERR_FILENAME_TOO_LONG);
    RTPathStripFilename(szExePath);
    int vrc = RTPathAppend(szExePath, sizeof(szExePath), m->pszProcName);
    AssertLogRelRCReturn(vrc, vrc);

    if (m->cArgs == 0 && m->cArgsAlloc == 0)
    {
        m->cArgsAlloc = 2;
        m->papszArgs = (char **)RTMemAllocZ(sizeof(m->papszArgs[0]) * 2);
        AssertReturn(m->papszArgs, VERR_NO_MEMORY);
    }
    else
        Assert(m->cArgsAlloc >= 2);
    RTStrFree(m->papszArgs[0]);
    m->papszArgs[0] = RTStrDup(szExePath);
    AssertReturn(m->papszArgs[0], VERR_NO_MEMORY);
    if (m->cArgs == 0)
        m->cArgs = 1;

    /*
     * Start the process:
     */
    int rc = RTProcCreate(szExePath, m->papszArgs, RTENV_DEFAULT, 0, &m->Process);
    if (RT_SUCCESS(rc))
        LogRel(("NetworkServiceRunning: started '%s', pid %RTproc\n", m->pszProcName, m->Process));
    else
        m->Process = NIL_RTPROCESS;

    m->fKillProcessOnStop = aKillProcessOnStop;

    return rc;
}


int NetworkServiceRunner::stop()
{
    /*
     * If the process already terminated, this function will also grab the exit
     * status and transition the process out of zombie status.
     */
    if (!isRunning())
        return VINF_OBJECT_DESTROYED;

    bool fDoKillProc = true;

    if (!m->fKillProcessOnStop)
    {
        /*
         * This is a VBoxSVC Main client. Do NOT kill it but assume it was shut
         * down politely. Wait up to 1 second until the process is killed before
         * doing the final hard kill.
         */
        for (unsigned int i = 0; i < 100; i++)
        {
            if (!isRunning())
            {
                fDoKillProc = false;
                break;
            }
            RTThreadSleep(10);
        }
    }

    if (fDoKillProc)
    {
        LogRel(("NetworkServiceRunning: killing %s, pid %RTproc...\n", m->pszProcName, m->Process));
        RTProcTerminate(m->Process);

        int rc = RTProcWait(m->Process, RTPROCWAIT_FLAGS_BLOCK, NULL);
        NOREF(rc);
    }

    m->Process = NIL_RTPROCESS;
    return VINF_SUCCESS;
}


/**
 * Checks if the service process is still running.
 *
 * @returns true if running, false if not.
 */
bool NetworkServiceRunner::isRunning()
{
    RTPROCESS Process = m->Process;
    if (Process != NIL_RTPROCESS)
    {
        RTPROCSTATUS ExitStatus;
        int rc = RTProcWait(Process, RTPROCWAIT_FLAGS_NOBLOCK, &ExitStatus);
        if (rc == VERR_PROCESS_RUNNING)
            return true;
        LogRel(("NetworkServiceRunning: %s (pid %RTproc) stopped: iStatus=%u enmReason=%d\n",
                m->pszProcName, m->Process, ExitStatus.iStatus, ExitStatus.enmReason));
        m->Process = NIL_RTPROCESS;
    }
    return false;
}


/**
 * Gets the process ID of a running service, NIL_PROCESS if not running.
 */
RTPROCESS NetworkServiceRunner::getPid() const
{
    return m->Process;
}

