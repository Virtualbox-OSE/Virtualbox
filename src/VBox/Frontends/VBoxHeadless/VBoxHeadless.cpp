/* $Id: VBoxHeadless.cpp $ */
/** @file
 * VBoxHeadless - The VirtualBox Headless frontend for running VMs on servers.
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

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/array.h>
#include <VBox/com/Guid.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/NativeEventQueue.h>

#include <VBox/com/VirtualBox.h>
#include <VBox/com/listeners.h>

using namespace com;

#define LOG_GROUP LOG_GROUP_GUI

#include <VBox/log.h>
#include <VBox/version.h>
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/initterm.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/ldr.h>
#include <iprt/getopt.h>
#include <iprt/env.h>
#include <VBox/err.h>
#include <VBoxVideo.h>

#ifdef VBOX_WITH_RECORDING
# include <cstdlib>
# include <cerrno>
# include <iprt/process.h>
#endif

#ifdef RT_OS_DARWIN
# include <iprt/asm.h>
# include <dlfcn.h>
# include <sys/mman.h>
#endif

//#define VBOX_WITH_SAVESTATE_ON_SIGNAL
#ifdef VBOX_WITH_SAVESTATE_ON_SIGNAL
#include <signal.h>
#endif

#include "PasswordInput.h"

////////////////////////////////////////////////////////////////////////////////

#define LogError(m,rc) \
    do { \
        Log(("VBoxHeadless: ERROR: " m " [rc=0x%08X]\n", rc)); \
        RTPrintf("%s\n", m); \
    } while (0)

////////////////////////////////////////////////////////////////////////////////

/* global weak references (for event handlers) */
static IConsole *gConsole = NULL;
static NativeEventQueue *gEventQ = NULL;

/* flag whether frontend should terminate */
static volatile bool g_fTerminateFE = false;

////////////////////////////////////////////////////////////////////////////////

/**
 *  Handler for VirtualBoxClient events.
 */
class VirtualBoxClientEventListener
{
public:
    VirtualBoxClientEventListener()
    {
    }

    virtual ~VirtualBoxClientEventListener()
    {
    }

    HRESULT init()
    {
        return S_OK;
    }

    void uninit()
    {
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        switch (aType)
        {
            case VBoxEventType_OnVBoxSVCAvailabilityChanged:
            {
                ComPtr<IVBoxSVCAvailabilityChangedEvent> pVSACEv = aEvent;
                Assert(pVSACEv);
                BOOL fAvailable = FALSE;
                pVSACEv->COMGETTER(Available)(&fAvailable);
                if (!fAvailable)
                {
                    LogRel(("VBoxHeadless: VBoxSVC became unavailable, exiting.\n"));
                    RTPrintf("VBoxSVC became unavailable, exiting.\n");
                    /* Terminate the VM as cleanly as possible given that VBoxSVC
                     * is no longer present. */
                    g_fTerminateFE = true;
                    gEventQ->interruptEventQueueProcessing();
                }
                break;
            }
            default:
                AssertFailed();
        }

        return S_OK;
    }

private:
};

/**
 *  Handler for machine events.
 */
class ConsoleEventListener
{
public:
    ConsoleEventListener() :
        mLastVRDEPort(-1),
        m_fIgnorePowerOffEvents(false),
        m_fNoLoggedInUsers(true)
    {
    }

    virtual ~ConsoleEventListener()
    {
    }

    HRESULT init()
    {
        return S_OK;
    }

    void uninit()
    {
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        switch (aType)
        {
            case VBoxEventType_OnMouseCapabilityChanged:
            {

                ComPtr<IMouseCapabilityChangedEvent> mccev = aEvent;
                Assert(!mccev.isNull());

                BOOL fSupportsAbsolute = false;
                mccev->COMGETTER(SupportsAbsolute)(&fSupportsAbsolute);

                /* Emit absolute mouse event to actually enable the host mouse cursor. */
                if (fSupportsAbsolute && gConsole)
                {
                    ComPtr<IMouse> mouse;
                    gConsole->COMGETTER(Mouse)(mouse.asOutParam());
                    if (mouse)
                    {
                        mouse->PutMouseEventAbsolute(-1, -1, 0, 0 /* Horizontal wheel */, 0);
                    }
                }
                break;
            }
            case VBoxEventType_OnStateChanged:
            {
                ComPtr<IStateChangedEvent> scev = aEvent;
                Assert(scev);

                MachineState_T machineState;
                scev->COMGETTER(State)(&machineState);

                /* Terminate any event wait operation if the machine has been
                 * PoweredDown/Saved/Aborted. */
                if (machineState < MachineState_Running && !m_fIgnorePowerOffEvents)
                {
                    g_fTerminateFE = true;
                    gEventQ->interruptEventQueueProcessing();
                }

                break;
            }
            case VBoxEventType_OnVRDEServerInfoChanged:
            {
                ComPtr<IVRDEServerInfoChangedEvent> rdicev = aEvent;
                Assert(rdicev);

                if (gConsole)
                {
                    ComPtr<IVRDEServerInfo> info;
                    gConsole->COMGETTER(VRDEServerInfo)(info.asOutParam());
                    if (info)
                    {
                        LONG port;
                        info->COMGETTER(Port)(&port);
                        if (port != mLastVRDEPort)
                        {
                            if (port == -1)
                                RTPrintf("VRDE server is inactive.\n");
                            else if (port == 0)
                                RTPrintf("VRDE server failed to start.\n");
                            else
                                RTPrintf("VRDE server is listening on port %d.\n", port);

                            mLastVRDEPort = port;
                        }
                    }
                }
                break;
            }
            case VBoxEventType_OnCanShowWindow:
            {
                ComPtr<ICanShowWindowEvent> cswev = aEvent;
                Assert(cswev);
                cswev->AddVeto(NULL);
                break;
            }
            case VBoxEventType_OnShowWindow:
            {
                ComPtr<IShowWindowEvent> swev = aEvent;
                Assert(swev);
                /* Ignore the event, WinId is either still zero or some other listener assigned it. */
                NOREF(swev); /* swev->COMSETTER(WinId)(0); */
                break;
            }
            case VBoxEventType_OnGuestPropertyChanged:
            {
                ComPtr<IGuestPropertyChangedEvent> pChangedEvent = aEvent;
                Assert(pChangedEvent);

                HRESULT hrc;

                ComPtr <IMachine> pMachine;
                if (gConsole)
                {
                    hrc = gConsole->COMGETTER(Machine)(pMachine.asOutParam());
                    if (FAILED(hrc) || !pMachine)
                        hrc = VBOX_E_OBJECT_NOT_FOUND;
                }
                else
                    hrc = VBOX_E_INVALID_VM_STATE;

                if (SUCCEEDED(hrc))
                {
                    Bstr strKey;
                    hrc = pChangedEvent->COMGETTER(Name)(strKey.asOutParam());
                    AssertComRC(hrc);

                    Bstr strValue;
                    hrc = pChangedEvent->COMGETTER(Value)(strValue.asOutParam());
                    AssertComRC(hrc);

                    Utf8Str utf8Key = strKey;
                    Utf8Str utf8Value = strValue;
                    LogRelFlow(("Guest property \"%s\" has been changed to \"%s\"\n",
                                utf8Key.c_str(), utf8Value.c_str()));

                    if (utf8Key.equals("/VirtualBox/GuestInfo/OS/NoLoggedInUsers"))
                    {
                        LogRelFlow(("Guest indicates that there %s logged in users\n",
                                    utf8Value.equals("true") ? "are no" : "are"));

                        /* Check if this is our machine and the "disconnect on logout feature" is enabled. */
                        BOOL fProcessDisconnectOnGuestLogout = FALSE;

                        /* Does the machine handle VRDP disconnects? */
                        Bstr strDiscon;
                        hrc = pMachine->GetExtraData(Bstr("VRDP/DisconnectOnGuestLogout").raw(),
                                                    strDiscon.asOutParam());
                        if (SUCCEEDED(hrc))
                        {
                            Utf8Str utf8Discon = strDiscon;
                            fProcessDisconnectOnGuestLogout = utf8Discon.equals("1")
                                                            ? TRUE : FALSE;
                        }

                        LogRelFlow(("VRDE: hrc=%Rhrc: Host %s disconnecting clients (current host state known: %s)\n",
                                    hrc, fProcessDisconnectOnGuestLogout ? "will handle" : "does not handle",
                                    m_fNoLoggedInUsers ? "No users logged in" : "Users logged in"));

                        if (fProcessDisconnectOnGuestLogout)
                        {
                            bool fDropConnection = false;
                            if (!m_fNoLoggedInUsers) /* Only if the property really changes. */
                            {
                                if (   utf8Value == "true"
                                    /* Guest property got deleted due to reset,
                                     * so it has no value anymore. */
                                    || utf8Value.isEmpty())
                                {
                                    m_fNoLoggedInUsers = true;
                                    fDropConnection = true;
                                }
                            }
                            else if (utf8Value == "false")
                                m_fNoLoggedInUsers = false;
                            /* Guest property got deleted due to reset,
                             * take the shortcut without touching the m_fNoLoggedInUsers
                             * state. */
                            else if (utf8Value.isEmpty())
                                fDropConnection = true;

                            LogRelFlow(("VRDE: szNoLoggedInUsers=%s, m_fNoLoggedInUsers=%RTbool, fDropConnection=%RTbool\n",
                                        utf8Value.c_str(), m_fNoLoggedInUsers, fDropConnection));

                            if (fDropConnection)
                            {
                                /* If there is a connection, drop it. */
                                ComPtr<IVRDEServerInfo> info;
                                hrc = gConsole->COMGETTER(VRDEServerInfo)(info.asOutParam());
                                if (SUCCEEDED(hrc) && info)
                                {
                                    ULONG cClients = 0;
                                    hrc = info->COMGETTER(NumberOfClients)(&cClients);

                                    LogRelFlow(("VRDE: connected clients=%RU32\n", cClients));
                                    if (SUCCEEDED(hrc) && cClients > 0)
                                    {
                                        ComPtr <IVRDEServer> vrdeServer;
                                        hrc = pMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
                                        if (SUCCEEDED(hrc) && vrdeServer)
                                        {
                                            LogRel(("VRDE: the guest user has logged out, disconnecting remote clients.\n"));
                                            hrc = vrdeServer->COMSETTER(Enabled)(FALSE);
                                            AssertComRC(hrc);
                                            HRESULT hrc2 = vrdeServer->COMSETTER(Enabled)(TRUE);
                                            if (SUCCEEDED(hrc))
                                                hrc = hrc2;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (FAILED(hrc))
                        LogRelFlow(("VRDE: returned error=%Rhrc\n", hrc));
                }

                break;
            }

            default:
                AssertFailed();
        }
        return S_OK;
    }

    void ignorePowerOffEvents(bool fIgnore)
    {
        m_fIgnorePowerOffEvents = fIgnore;
    }

private:

    long mLastVRDEPort;
    bool m_fIgnorePowerOffEvents;
    bool m_fNoLoggedInUsers;
};

typedef ListenerImpl<VirtualBoxClientEventListener> VirtualBoxClientEventListenerImpl;
typedef ListenerImpl<ConsoleEventListener> ConsoleEventListenerImpl;

VBOX_LISTENER_DECLARE(VirtualBoxClientEventListenerImpl)
VBOX_LISTENER_DECLARE(ConsoleEventListenerImpl)

#ifdef VBOX_WITH_SAVESTATE_ON_SIGNAL
static void SaveState(int sig)
{
    ComPtr <IProgress> progress = NULL;

/** @todo Deal with nested signals, multithreaded signal dispatching (esp. on windows),
 * and multiple signals (both SIGINT and SIGTERM in some order).
 * Consider processing the signal request asynchronously since there are lots of things
 * which aren't safe (like RTPrintf and printf IIRC) in a signal context. */

    RTPrintf("Signal received, saving state.\n");

    HRESULT rc = gConsole->SaveState(progress.asOutParam());
    if (FAILED(rc))
    {
        RTPrintf("Error saving state! rc = 0x%x\n", rc);
        return;
    }
    Assert(progress);
    LONG cPercent = 0;

    RTPrintf("0%%");
    RTStrmFlush(g_pStdOut);
    for (;;)
    {
        BOOL fCompleted = false;
        rc = progress->COMGETTER(Completed)(&fCompleted);
        if (FAILED(rc) || fCompleted)
            break;
        ULONG cPercentNow;
        rc = progress->COMGETTER(Percent)(&cPercentNow);
        if (FAILED(rc))
            break;
        if ((cPercentNow / 10) != (cPercent / 10))
        {
            cPercent = cPercentNow;
            RTPrintf("...%d%%", cPercentNow);
            RTStrmFlush(g_pStdOut);
        }

        /* wait */
        rc = progress->WaitForCompletion(100);
    }

    HRESULT lrc;
    rc = progress->COMGETTER(ResultCode)(&lrc);
    if (FAILED(rc))
        lrc = ~0;
    if (!lrc)
    {
        RTPrintf(" -- Saved the state successfully.\n");
        RTThreadYield();
    }
    else
        RTPrintf("-- Error saving state, lrc=%d (%#x)\n", lrc, lrc);

}
#endif /* VBOX_WITH_SAVESTATE_ON_SIGNAL */

////////////////////////////////////////////////////////////////////////////////

static void show_usage()
{
    RTPrintf("Usage:\n"
             "   -s, -startvm, --startvm <name|uuid>   Start given VM (required argument)\n"
             "   -v, -vrde, --vrde on|off|config       Enable or disable the VRDE server\n"
             "                                           or don't change the setting (default)\n"
             "   -e, -vrdeproperty, --vrdeproperty <name=[value]> Set a VRDE property:\n"
             "                                     \"TCP/Ports\" - comma-separated list of\n"
             "                                       ports the VRDE server can bind to; dash\n"
             "                                       between two port numbers specifies range\n"
             "                                     \"TCP/Address\" - interface IP the VRDE\n"
             "                                       server will bind to\n"
             "   --settingspw <pw>                 Specify the settings password\n"
             "   --settingspwfile <file>           Specify a file containing the\n"
             "                                       settings password\n"
             "   -start-paused, --start-paused     Start the VM in paused state\n"
#ifdef VBOX_WITH_RECORDING
             "   -c, -record, --record             Record the VM screen output to a file\n"
             "   -w, --videowidth                  Video frame width when recording\n"
             "   -h, --videoheight                 Video frame height when recording\n"
             "   -r, --videobitrate                Recording bit rate when recording\n"
             "   -f, --filename                    File name when recording. The codec used\n"
             "                                     will be chosen based on file extension\n"
#endif
             "\n");
}

#ifdef VBOX_WITH_RECORDING
/**
 * Parse the environment for variables which can influence the VIDEOREC settings.
 * purely for backwards compatibility.
 * @param pulFrameWidth may be updated with a desired frame width
 * @param pulFrameHeight may be updated with a desired frame height
 * @param pulBitRate may be updated with a desired bit rate
 * @param ppszFilename may be updated with a desired file name
 */
static void parse_environ(uint32_t *pulFrameWidth, uint32_t *pulFrameHeight,
                          uint32_t *pulBitRate, const char **ppszFilename)
{
    const char *pszEnvTemp;
/** @todo r=bird: This isn't up to scratch. The life time of an RTEnvGet
 *        return value is only up to the next RTEnv*, *getenv, *putenv,
 *        setenv call in _any_ process in the system and the it has known and
 *        documented code page issues.
 *
 *        Use RTEnvGetEx instead! */
    if ((pszEnvTemp = RTEnvGet("VBOX_RECORDWIDTH")) != 0)
    {
        errno = 0;
        unsigned long ulFrameWidth = strtoul(pszEnvTemp, 0, 10);
        if (errno != 0)
            LogError("VBoxHeadless: ERROR: invalid VBOX_RECORDWIDTH environment variable", 0);
        else
            *pulFrameWidth = ulFrameWidth;
    }
    if ((pszEnvTemp = RTEnvGet("VBOX_RECORDHEIGHT")) != 0)
    {
        errno = 0;
        unsigned long ulFrameHeight = strtoul(pszEnvTemp, 0, 10);
        if (errno != 0)
            LogError("VBoxHeadless: ERROR: invalid VBOX_RECORDHEIGHT environment variable", 0);
        else
            *pulFrameHeight = ulFrameHeight;
    }
    if ((pszEnvTemp = RTEnvGet("VBOX_RECORDBITRATE")) != 0)
    {
        errno = 0;
        unsigned long ulBitRate = strtoul(pszEnvTemp, 0, 10);
        if (errno != 0)
            LogError("VBoxHeadless: ERROR: invalid VBOX_RECORDBITRATE environment variable", 0);
        else
            *pulBitRate = ulBitRate;
    }
    if ((pszEnvTemp = RTEnvGet("VBOX_RECORDFILE")) != 0)
        *ppszFilename = pszEnvTemp;
}
#endif /* VBOX_WITH_RECORDING defined */

#ifdef RT_OS_DARWIN

# include <unistd.h>
# include <stdio.h>
# include <dlfcn.h>
# include <iprt/formats/mach-o.h>

/**
 * Override this one to try hide the fact that we're setuid to root
 * orginially.
 */
int issetugid_for_AppKit(void)
{
    Dl_info Info = {0};
    char szMsg[512];
    size_t cchMsg;
    const void * uCaller = __builtin_return_address(0);
    if (dladdr(uCaller, &Info))
        cchMsg = snprintf(szMsg, sizeof(szMsg), "DEBUG: issetugid_for_AppKit was called by %p %s::%s+%p (via %p)\n",
                          uCaller, Info.dli_fname, Info.dli_sname, (void *)((uintptr_t)uCaller - (uintptr_t)Info.dli_saddr), __builtin_return_address(1));
    else
        cchMsg = snprintf(szMsg, sizeof(szMsg), "DEBUG: issetugid_for_AppKit was called by %p (via %p)\n", uCaller, __builtin_return_address(1));
    write(2, szMsg, cchMsg);
    return 0;
}

static bool patchExtSym(mach_header_64_t *pHdr, const char *pszSymbol, uintptr_t uNewValue)
{
    /*
     * First do some basic header checks and the scan the load
     * commands for the symbol table info.
     */
    AssertLogRelMsgReturn(pHdr->magic == (ARCH_BITS == 64 ? MH_MAGIC_64 : MH_MAGIC),
                          ("%p: magic=%#x\n", pHdr, pHdr->magic), false);
    uint32_t const cCmds = pHdr->ncmds;
    uint32_t const cbCmds = pHdr->sizeofcmds;
    AssertLogRelMsgReturn(cCmds < 16384 && cbCmds < _2M, ("%p: ncmds=%u sizeofcmds=%u\n", pHdr, cCmds, cbCmds), false);

    /*
     * First command pass: Locate the symbol table and dynamic symbol table info
     *                     commands, also calc the slide (load addr - link addr).
     */
    dysymtab_command_t const   *pDySymTab = NULL;
    symtab_command_t const     *pSymTab   = NULL;
    segment_command_64_t const *pFirstSeg = NULL;
    uintptr_t                   offSlide  = 0;
    uint32_t                    offCmd    = 0;
    for (uint32_t iCmd = 0; iCmd < cCmds; iCmd++)
    {
        AssertLogRelMsgReturn(offCmd + sizeof(load_command_t) <= cbCmds,
                              ("%p: iCmd=%u offCmd=%#x cbCmds=%#x\n", pHdr, iCmd, offCmd, cbCmds), false);
        load_command_t const * const pCmd = (load_command_t const *)((uintptr_t)(pHdr + 1) + offCmd);
        uint32_t const cbCurCmd = pCmd->cmdsize;
        AssertLogRelMsgReturn(offCmd + cbCurCmd <= cbCmds && cbCurCmd <= cbCmds,
                              ("%p: iCmd=%u offCmd=%#x cbCurCmd=%#x cbCmds=%#x\n", pHdr, iCmd, offCmd, cbCurCmd, cbCmds), false);
        offCmd += cbCurCmd;

        if (pCmd->cmd == LC_SYMTAB)
        {
            AssertLogRelMsgReturn(!pSymTab, ("%p: pSymTab=%p pCmd=%p\n", pHdr, pSymTab, pCmd), false);
            pSymTab = (symtab_command_t const *)pCmd;
            AssertLogRelMsgReturn(cbCurCmd == sizeof(*pSymTab), ("%p: pSymTab=%p cbCurCmd=%#x\n", pHdr, pCmd, cbCurCmd), false);

        }
        else if (pCmd->cmd == LC_DYSYMTAB)
        {
            AssertLogRelMsgReturn(!pDySymTab, ("%p: pDySymTab=%p pCmd=%p\n", pHdr, pDySymTab, pCmd), false);
            pDySymTab = (dysymtab_command_t const *)pCmd;
            AssertLogRelMsgReturn(cbCurCmd == sizeof(*pDySymTab), ("%p: pDySymTab=%p cbCurCmd=%#x\n", pHdr, pCmd, cbCurCmd),
                                  false);
        }
        else if (pCmd->cmd == LC_SEGMENT_64 && !pFirstSeg) /* ASSUMES the first seg is the one with the header and stuff. */
        {
            /* Note! the fileoff and vmaddr seems to be modified. */
            pFirstSeg = (segment_command_64_t const *)pCmd;
            AssertLogRelMsgReturn(cbCurCmd >= sizeof(*pFirstSeg), ("%p: iCmd=%u cbCurCmd=%#x\n", pHdr, iCmd, cbCurCmd), false);
            AssertLogRelMsgReturn(/*pFirstSeg->fileoff == 0 && */ pFirstSeg->vmsize >= sizeof(*pHdr) + cbCmds,
                                  ("%p: iCmd=%u fileoff=%llx vmsize=%#llx cbCmds=%#x name=%.16s\n",
                                   pHdr, iCmd, pFirstSeg->fileoff, pFirstSeg->vmsize, cbCmds, pFirstSeg->segname), false);
            offSlide = (uintptr_t)pHdr - pFirstSeg->vmaddr;
        }
    }
    AssertLogRelMsgReturn(pSymTab, ("%p: no LC_SYMTAB\n", pHdr), false);
    AssertLogRelMsgReturn(pDySymTab, ("%p: no LC_DYSYMTAB\n", pHdr), false);
    AssertLogRelMsgReturn(pFirstSeg, ("%p: no LC_SEGMENT_64\n", pHdr), false);

    /*
     * Second command pass: Locate the memory locations of the symbol table, string
     *                      table and the indirect symbol table by checking LC_SEGMENT_xx.
     */
    macho_nlist_64_t const *paSymbols  = NULL;
    uint32_t const          offSymbols = pSymTab->symoff;
    uint32_t const          cSymbols   = pSymTab->nsyms;
    AssertLogRelMsgReturn(cSymbols > 0 && offSymbols >= sizeof(pHdr) + cbCmds,
                          ("%p: cSymbols=%#x offSymbols=%#x\n", pHdr, cSymbols, offSymbols), false);

    const char    *pchStrTab = NULL;
    uint32_t const offStrTab = pSymTab->stroff;
    uint32_t const cbStrTab  = pSymTab->strsize;
    AssertLogRelMsgReturn(cbStrTab > 0 && offStrTab >= sizeof(pHdr) + cbCmds,
                          ("%p: cbStrTab=%#x offStrTab=%#x\n", pHdr, cbStrTab, offStrTab), false);

    uint32_t const *paidxIndirSymbols = NULL;
    uint32_t const  offIndirSymbols = pDySymTab->indirectsymboff;
    uint32_t const  cIndirSymbols   = pDySymTab->nindirectsymb;
    AssertLogRelMsgReturn(cIndirSymbols > 0 && offIndirSymbols >= sizeof(pHdr) + cbCmds,
                          ("%p: cIndirSymbols=%#x offIndirSymbols=%#x\n", pHdr, cIndirSymbols, offIndirSymbols), false);

    offCmd = 0;
    for (uint32_t iCmd = 0; iCmd < cCmds; iCmd++)
    {
        load_command_t const * const pCmd = (load_command_t const *)((uintptr_t)(pHdr + 1) + offCmd);
        uint32_t const cbCurCmd = pCmd->cmdsize;
        AssertLogRelMsgReturn(offCmd + cbCurCmd <= cbCmds && cbCurCmd <= cbCmds,
                              ("%p: iCmd=%u offCmd=%#x cbCurCmd=%#x cbCmds=%#x\n", pHdr, iCmd, offCmd, cbCurCmd, cbCmds), false);
        offCmd += cbCurCmd;

        if (pCmd->cmd == LC_SEGMENT_64)
        {
            segment_command_64_t const *pSeg = (segment_command_64_t const *)pCmd;
            AssertLogRelMsgReturn(cbCurCmd >= sizeof(*pSeg), ("%p: iCmd=%u cbCurCmd=%#x\n", pHdr, iCmd, cbCurCmd), false);
            uintptr_t const uPtrSeg = pSeg->vmaddr + offSlide;
            uint64_t const  cbSeg   = pSeg->vmsize;
            uint64_t const  offFile = pSeg->fileoff;

            uint64_t offSeg = offSymbols - offFile;
            if (offSeg < cbSeg)
            {
                AssertLogRelMsgReturn(!paSymbols, ("%p: paSymbols=%p uPtrSeg=%p off=%#llx\n", pHdr, paSymbols, uPtrSeg, offSeg),
                                      false);
                AssertLogRelMsgReturn(offSeg + cSymbols * sizeof(paSymbols[0]) <= cbSeg,
                                      ("%p: offSeg=%#llx cSymbols=%#x cbSeg=%llx\n", pHdr, offSeg, cSymbols, cbSeg), false);
                paSymbols = (macho_nlist_64_t const *)(uPtrSeg + offSeg);
            }

            offSeg = offStrTab - offFile;
            if (offSeg < cbSeg)
            {
                AssertLogRelMsgReturn(!pchStrTab, ("%p: paSymbols=%p uPtrSeg=%p\n", pHdr, pchStrTab, uPtrSeg), false);
                AssertLogRelMsgReturn(offSeg + cbStrTab <= cbSeg,
                                      ("%p: offSeg=%#llx cbStrTab=%#x cbSeg=%llx\n", pHdr, offSeg, cbStrTab, cbSeg), false);
                pchStrTab = (const char *)(uPtrSeg + offSeg);
            }

            offSeg = offIndirSymbols - offFile;
            if (offSeg < cbSeg)
            {
                AssertLogRelMsgReturn(!paidxIndirSymbols,
                                      ("%p: paidxIndirSymbols=%p uPtrSeg=%p\n", pHdr, paidxIndirSymbols, uPtrSeg), false);
                AssertLogRelMsgReturn(offSeg + cIndirSymbols * sizeof(paidxIndirSymbols[0]) <= cbSeg,
                                      ("%p: offSeg=%#llx cIndirSymbols=%#x cbSeg=%llx\n", pHdr, offSeg, cIndirSymbols, cbSeg),
                                      false);
                paidxIndirSymbols = (uint32_t const *)(uPtrSeg + offSeg);
            }
        }
    }

    AssertLogRelMsgReturn(paSymbols, ("%p: offSymbols=%#x\n", pHdr, offSymbols), false);
    AssertLogRelMsgReturn(pchStrTab, ("%p: offStrTab=%#x\n", pHdr, offStrTab), false);
    AssertLogRelMsgReturn(paidxIndirSymbols, ("%p: offIndirSymbols=%#x\n", pHdr, offIndirSymbols), false);

    /*
     * Third command pass: Process sections of types S_NON_LAZY_SYMBOL_POINTERS
     *                     and S_LAZY_SYMBOL_POINTERS
     */
    bool fFound = false;
    offCmd = 0;
    for (uint32_t iCmd = 0; iCmd < cCmds; iCmd++)
    {
        load_command_t const * const pCmd = (load_command_t const *)((uintptr_t)(pHdr + 1) + offCmd);
        uint32_t const cbCurCmd = pCmd->cmdsize;
        AssertLogRelMsgReturn(offCmd + cbCurCmd <= cbCmds && cbCurCmd <= cbCmds,
                              ("%p: iCmd=%u offCmd=%#x cbCurCmd=%#x cbCmds=%#x\n", pHdr, iCmd, offCmd, cbCurCmd, cbCmds), false);
        offCmd += cbCurCmd;
        if (pCmd->cmd == LC_SEGMENT_64)
        {
            segment_command_64_t const *pSeg = (segment_command_64_t const *)pCmd;
            AssertLogRelMsgReturn(cbCurCmd >= sizeof(*pSeg), ("%p: iCmd=%u cbCurCmd=%#x\n", pHdr, iCmd, cbCurCmd), false);
            uint64_t const  uSegAddr = pSeg->vmaddr;
            uint64_t const  cbSeg    = pSeg->vmsize;

            uint32_t const             cSections  = pSeg->nsects;
            section_64_t const * const paSections = (section_64_t const *)(pSeg + 1);
            AssertLogRelMsgReturn(cSections < _256K && sizeof(*pSeg) + cSections * sizeof(paSections[0]) <= cbCurCmd,
                                  ("%p: iCmd=%u cSections=%#x cbCurCmd=%#x\n", pHdr, iCmd, cSections, cbCurCmd), false);
            for (uint32_t iSection = 0; iSection < cSections; iSection++)
            {
                if (   paSections[iSection].flags == S_NON_LAZY_SYMBOL_POINTERS
                    || paSections[iSection].flags == S_LAZY_SYMBOL_POINTERS)
                {
                    uint32_t const idxIndirBase = paSections[iSection].reserved1;
                    uint32_t const cEntries     = paSections[iSection].size / sizeof(uintptr_t);
                    AssertLogRelMsgReturn(idxIndirBase <= cIndirSymbols && idxIndirBase + cEntries <= cIndirSymbols,
                                          ("%p: idxIndirBase=%#x cEntries=%#x cIndirSymbols=%#x\n",
                                           pHdr, idxIndirBase, cEntries, cIndirSymbols), false);
                    uint64_t const uSecAddr = paSections[iSection].addr;
                    uint64_t const offInSeg = uSecAddr - uSegAddr;
                    AssertLogRelMsgReturn(offInSeg < cbSeg && offInSeg + cEntries * sizeof(uintptr_t) <= cbSeg,
                                          ("%p: offInSeg=%#llx cEntries=%#x cbSeg=%#llx\n", pHdr, offInSeg, cEntries, cbSeg),
                                          false);
                    uintptr_t *pauPtrs = (uintptr_t *)(uSecAddr + offSlide);
                    for (uint32_t iEntry = 0; iEntry < cEntries; iEntry++)
                    {
                        uint32_t const idxSym = paidxIndirSymbols[idxIndirBase + iEntry];
                        if (idxSym < cSymbols)
                        {
                            macho_nlist_64_t const * const pSym    = &paSymbols[idxSym];
                            const char * const             pszName = pSym->n_un.n_strx < cbStrTab
                                                                   ? &pchStrTab[pSym->n_un.n_strx] : "!invalid symtab offset!";
                            if (strcmp(pszName, pszSymbol) == 0)
                            {
                                pauPtrs[iEntry] = uNewValue;
                                fFound = true;
                                break;
                            }
                        }
                        else
                            AssertMsg(idxSym == INDIRECT_SYMBOL_LOCAL || idxSym == INDIRECT_SYMBOL_ABS, ("%#x\n", idxSym));
                    }
                }
            }
        }
    }
    AssertLogRel(fFound);
    return fFound;
}

/**
 * Mac OS X: Really ugly hack to bypass a set-uid check in AppKit.
 *
 * This will modify the issetugid() function to always return zero.  This must
 * be done _before_ AppKit is initialized, otherwise it will refuse to play ball
 * with us as it distrusts set-uid processes since Snow Leopard.  We, however,
 * have carefully dropped all root privileges at this point and there should be
 * no reason for any security concern here.
 */
static void hideSetUidRootFromAppKit()
{
    void *pvAddr;
    /* Find issetguid() and make it always return 0 by modifying the code: */
# if 0
    pvAddr = dlsym(RTLD_DEFAULT, "issetugid");
    int rc = mprotect((void *)((uintptr_t)pvAddr & ~(uintptr_t)0xfff), 0x2000, PROT_WRITE | PROT_READ | PROT_EXEC);
    if (!rc)
        ASMAtomicWriteU32((volatile uint32_t *)pvAddr, 0xccc3c031); /* xor eax, eax; ret; int3 */
    else
# endif
    {
        /* Failing that, find AppKit and patch its import table: */
        void *pvAppKit = dlopen("/System/Library/Frameworks/AppKit.framework/AppKit", RTLD_NOLOAD);
        pvAddr = dlsym(pvAppKit, "NSApplicationMain");
        Dl_info Info = {0};
        if (   dladdr(pvAddr, &Info)
            && Info.dli_fbase != NULL)
        {
            if (!patchExtSym((mach_header_64_t *)Info.dli_fbase, "_issetugid", (uintptr_t)&issetugid_for_AppKit))
                write(2, RT_STR_TUPLE("WARNING: Failed to patch issetugid in AppKit! (patchExtSym)\n"));
# ifdef DEBUG
            else
                write(2, RT_STR_TUPLE("INFO: Successfully patched _issetugid import for AppKit!\n"));
# endif
        }
        else
            write(2, RT_STR_TUPLE("WARNING: Failed to patch issetugid in AppKit! (dladdr)\n"));
    }

}

#endif /* RT_OS_DARWIN */

/**
 *  Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv, char **envp)
{
    RT_NOREF(envp);
    const char *vrdePort = NULL;
    const char *vrdeAddress = NULL;
    const char *vrdeEnabled = NULL;
    unsigned cVRDEProperties = 0;
    const char *aVRDEProperties[16];
    unsigned fRawR0 = ~0U;
    unsigned fRawR3 = ~0U;
    unsigned fPATM  = ~0U;
    unsigned fCSAM  = ~0U;
    unsigned fPaused = 0;
#ifdef VBOX_WITH_RECORDING
    bool fRecordEnabled = false;
    uint32_t ulRecordVideoWidth = 800;
    uint32_t ulRecordVideoHeight = 600;
    uint32_t ulRecordVideoRate = 300000;
    char szRecordFilename[RTPATH_MAX];
    const char *pszRecordFilenameTemplate = "VBox-%d.webm"; /* .webm container by default. */
#endif /* VBOX_WITH_RECORDING */
#ifdef RT_OS_WINDOWS
    ATL::CComModule _Module; /* Required internally by ATL (constructor records instance in global variable). */
#endif

    LogFlow(("VBoxHeadless STARTED.\n"));
    RTPrintf(VBOX_PRODUCT " Headless Interface " VBOX_VERSION_STRING "\n"
             "(C) 2008-" VBOX_C_YEAR " " VBOX_VENDOR "\n"
             "All rights reserved.\n\n");

#ifdef VBOX_WITH_RECORDING
    /* Parse the environment */
    parse_environ(&ulRecordVideoWidth, &ulRecordVideoHeight, &ulRecordVideoRate, &pszRecordFilenameTemplate);
#endif

    enum eHeadlessOptions
    {
        OPT_RAW_R0 = 0x100,
        OPT_NO_RAW_R0,
        OPT_RAW_R3,
        OPT_NO_RAW_R3,
        OPT_PATM,
        OPT_NO_PATM,
        OPT_CSAM,
        OPT_NO_CSAM,
        OPT_SETTINGSPW,
        OPT_SETTINGSPW_FILE,
        OPT_COMMENT,
        OPT_PAUSED
    };

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "-startvm", 's', RTGETOPT_REQ_STRING },
        { "--startvm", 's', RTGETOPT_REQ_STRING },
        { "-vrdpport", 'p', RTGETOPT_REQ_STRING },     /* VRDE: deprecated. */
        { "--vrdpport", 'p', RTGETOPT_REQ_STRING },    /* VRDE: deprecated. */
        { "-vrdpaddress", 'a', RTGETOPT_REQ_STRING },  /* VRDE: deprecated. */
        { "--vrdpaddress", 'a', RTGETOPT_REQ_STRING }, /* VRDE: deprecated. */
        { "-vrdp", 'v', RTGETOPT_REQ_STRING },         /* VRDE: deprecated. */
        { "--vrdp", 'v', RTGETOPT_REQ_STRING },        /* VRDE: deprecated. */
        { "-vrde", 'v', RTGETOPT_REQ_STRING },
        { "--vrde", 'v', RTGETOPT_REQ_STRING },
        { "-vrdeproperty", 'e', RTGETOPT_REQ_STRING },
        { "--vrdeproperty", 'e', RTGETOPT_REQ_STRING },
        { "-rawr0", OPT_RAW_R0, 0 },
        { "--rawr0", OPT_RAW_R0, 0 },
        { "-norawr0", OPT_NO_RAW_R0, 0 },
        { "--norawr0", OPT_NO_RAW_R0, 0 },
        { "-rawr3", OPT_RAW_R3, 0 },
        { "--rawr3", OPT_RAW_R3, 0 },
        { "-norawr3", OPT_NO_RAW_R3, 0 },
        { "--norawr3", OPT_NO_RAW_R3, 0 },
        { "-patm", OPT_PATM, 0 },
        { "--patm", OPT_PATM, 0 },
        { "-nopatm", OPT_NO_PATM, 0 },
        { "--nopatm", OPT_NO_PATM, 0 },
        { "-csam", OPT_CSAM, 0 },
        { "--csam", OPT_CSAM, 0 },
        { "-nocsam", OPT_NO_CSAM, 0 },
        { "--nocsam", OPT_NO_CSAM, 0 },
        { "--settingspw", OPT_SETTINGSPW, RTGETOPT_REQ_STRING },
        { "--settingspwfile", OPT_SETTINGSPW_FILE, RTGETOPT_REQ_STRING },
#ifdef VBOX_WITH_RECORDING
        { "-record", 'c', 0 },
        { "--record", 'c', 0 },
        { "--videowidth", 'w', RTGETOPT_REQ_UINT32 },
        { "--videoheight", 'h', RTGETOPT_REQ_UINT32 }, /* great choice of short option! */
        { "--videorate", 'r', RTGETOPT_REQ_UINT32 },
        { "--filename", 'f', RTGETOPT_REQ_STRING },
#endif /* VBOX_WITH_RECORDING defined */
        { "-comment", OPT_COMMENT, RTGETOPT_REQ_STRING },
        { "--comment", OPT_COMMENT, RTGETOPT_REQ_STRING },
        { "-start-paused", OPT_PAUSED, 0 },
        { "--start-paused", OPT_PAUSED, 0 }
    };

    const char *pcszNameOrUUID = NULL;

#ifdef RT_OS_DARWIN
    hideSetUidRootFromAppKit();
#endif

    // parse the command line
    int ch;
    const char *pcszSettingsPw = NULL;
    const char *pcszSettingsPwFile = NULL;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch(ch)
        {
            case 's':
                pcszNameOrUUID = ValueUnion.psz;
                break;
            case 'p':
                RTPrintf("Warning: '-p' or '-vrdpport' are deprecated. Use '-e \"TCP/Ports=%s\"'\n", ValueUnion.psz);
                vrdePort = ValueUnion.psz;
                break;
            case 'a':
                RTPrintf("Warning: '-a' or '-vrdpaddress' are deprecated. Use '-e \"TCP/Address=%s\"'\n", ValueUnion.psz);
                vrdeAddress = ValueUnion.psz;
                break;
            case 'v':
                vrdeEnabled = ValueUnion.psz;
                break;
            case 'e':
                if (cVRDEProperties < RT_ELEMENTS(aVRDEProperties))
                    aVRDEProperties[cVRDEProperties++] = ValueUnion.psz;
                else
                     RTPrintf("Warning: too many VRDE properties. Ignored: '%s'\n", ValueUnion.psz);
                break;
            case OPT_RAW_R0:
                fRawR0 = true;
                break;
            case OPT_NO_RAW_R0:
                fRawR0 = false;
                break;
            case OPT_RAW_R3:
                fRawR3 = true;
                break;
            case OPT_NO_RAW_R3:
                fRawR3 = false;
                break;
            case OPT_PATM:
                fPATM = true;
                break;
            case OPT_NO_PATM:
                fPATM = false;
                break;
            case OPT_CSAM:
                fCSAM = true;
                break;
            case OPT_NO_CSAM:
                fCSAM = false;
                break;
            case OPT_SETTINGSPW:
                pcszSettingsPw = ValueUnion.psz;
                break;
            case OPT_SETTINGSPW_FILE:
                pcszSettingsPwFile = ValueUnion.psz;
                break;
            case OPT_PAUSED:
                fPaused = true;
                break;
#ifdef VBOX_WITH_RECORDING
            case 'c':
                fRecordEnabled = true;
                break;
            case 'w':
                ulRecordVideoWidth = ValueUnion.u32;
                break;
            case 'r':
                ulRecordVideoRate = ValueUnion.u32;
                break;
            case 'f':
                pszRecordFilenameTemplate = ValueUnion.psz;
                break;
#endif /* VBOX_WITH_RECORDING defined */
            case 'h':
#ifdef VBOX_WITH_RECORDING
                if ((GetState.pDef->fFlags & RTGETOPT_REQ_MASK) != RTGETOPT_REQ_NOTHING)
                {
                    ulRecordVideoHeight = ValueUnion.u32;
                    break;
                }
#endif
                show_usage();
                return 0;
            case OPT_COMMENT:
                /* nothing to do */
                break;
            case 'V':
                RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                return 0;
            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                show_usage();
                return ch;
        }
    }

#ifdef VBOX_WITH_RECORDING
    if (ulRecordVideoWidth < 512 || ulRecordVideoWidth > 2048 || ulRecordVideoWidth % 2)
    {
        LogError("VBoxHeadless: ERROR: please specify an even video frame width between 512 and 2048", 0);
        return 1;
    }
    if (ulRecordVideoHeight < 384 || ulRecordVideoHeight > 1536 || ulRecordVideoHeight % 2)
    {
        LogError("VBoxHeadless: ERROR: please specify an even video frame height between 384 and 1536", 0);
        return 1;
    }
    if (ulRecordVideoRate < 300000 || ulRecordVideoRate > 1000000)
    {
        LogError("VBoxHeadless: ERROR: please specify an even video bitrate between 300000 and 1000000", 0);
        return 1;
    }
    /* Make sure we only have %d or %u (or none) in the file name specified */
    char *pcPercent = (char*)strchr(pszRecordFilenameTemplate, '%');
    if (pcPercent != 0 && *(pcPercent + 1) != 'd' && *(pcPercent + 1) != 'u')
    {
        LogError("VBoxHeadless: ERROR: Only %%d and %%u are allowed in the recording file name.", -1);
        return 1;
    }
    /* And no more than one % in the name */
    if (pcPercent != 0 && strchr(pcPercent + 1, '%') != 0)
    {
        LogError("VBoxHeadless: ERROR: Only one format modifier is allowed in the recording file name.", -1);
        return 1;
    }
    RTStrPrintf(&szRecordFilename[0], RTPATH_MAX, pszRecordFilenameTemplate, RTProcSelf());
#endif /* defined VBOX_WITH_RECORDING */

    if (!pcszNameOrUUID)
    {
        show_usage();
        return 1;
    }

    HRESULT rc;

    rc = com::Initialize();
#ifdef VBOX_WITH_XPCOM
    if (rc == NS_ERROR_FILE_ACCESS_DENIED)
    {
        char szHome[RTPATH_MAX] = "";
        com::GetVBoxUserHomeDirectory(szHome, sizeof(szHome));
        RTPrintf("Failed to initialize COM because the global settings directory '%s' is not accessible!", szHome);
        return 1;
    }
#endif
    if (FAILED(rc))
    {
        RTPrintf("VBoxHeadless: ERROR: failed to initialize COM!\n");
        return 1;
    }

    ComPtr<IVirtualBoxClient> pVirtualBoxClient;
    ComPtr<IVirtualBox> virtualBox;
    ComPtr<ISession> session;
    ComPtr<IMachine> machine;
    bool fSessionOpened = false;
    ComPtr<IEventListener> vboxClientListener;
    ComPtr<IEventListener> vboxListener;
    ComObjPtr<ConsoleEventListenerImpl> consoleListener;

    do
    {
        rc = pVirtualBoxClient.createInprocObject(CLSID_VirtualBoxClient);
        if (FAILED(rc))
        {
            RTPrintf("VBoxHeadless: ERROR: failed to create the VirtualBoxClient object!\n");
            com::ErrorInfo info;
            if (!info.isFullAvailable() && !info.isBasicAvailable())
            {
                com::GluePrintRCMessage(rc);
                RTPrintf("Most likely, the VirtualBox COM server is not running or failed to start.\n");
            }
            else
                GluePrintErrorInfo(info);
            break;
        }

        rc = pVirtualBoxClient->COMGETTER(VirtualBox)(virtualBox.asOutParam());
        if (FAILED(rc))
        {
            RTPrintf("Failed to get VirtualBox object (rc=%Rhrc)!\n", rc);
            break;
        }
        rc = pVirtualBoxClient->COMGETTER(Session)(session.asOutParam());
        if (FAILED(rc))
        {
            RTPrintf("Failed to get session object (rc=%Rhrc)!\n", rc);
            break;
        }

        if (pcszSettingsPw)
        {
            CHECK_ERROR(virtualBox, SetSettingsSecret(Bstr(pcszSettingsPw).raw()));
            if (FAILED(rc))
                break;
        }
        else if (pcszSettingsPwFile)
        {
            int rcExit = settingsPasswordFile(virtualBox, pcszSettingsPwFile);
            if (rcExit != RTEXITCODE_SUCCESS)
                break;
        }

        ComPtr<IMachine> m;

        rc = virtualBox->FindMachine(Bstr(pcszNameOrUUID).raw(), m.asOutParam());
        if (FAILED(rc))
        {
            LogError("Invalid machine name or UUID!\n", rc);
            break;
        }
        Bstr id;
        m->COMGETTER(Id)(id.asOutParam());
        AssertComRC(rc);
        if (FAILED(rc))
            break;

        Log(("VBoxHeadless: Opening a session with machine (id={%s})...\n",
              Utf8Str(id).c_str()));

        // set session name
        CHECK_ERROR_BREAK(session, COMSETTER(Name)(Bstr("headless").raw()));
        // open a session
        CHECK_ERROR_BREAK(m, LockMachine(session, LockType_VM));
        fSessionOpened = true;

        /* get the console */
        ComPtr<IConsole> console;
        CHECK_ERROR_BREAK(session, COMGETTER(Console)(console.asOutParam()));

        /* get the mutable machine */
        CHECK_ERROR_BREAK(console, COMGETTER(Machine)(machine.asOutParam()));

        ComPtr<IDisplay> display;
        CHECK_ERROR_BREAK(console, COMGETTER(Display)(display.asOutParam()));

#ifdef VBOX_WITH_RECORDING
        if (fRecordEnabled)
        {
            ComPtr<IRecordingSettings> recordingSettings;
            CHECK_ERROR_BREAK(machine, COMGETTER(RecordingSettings)(recordingSettings.asOutParam()));
            CHECK_ERROR_BREAK(recordingSettings, COMSETTER(Enabled)(TRUE));

            SafeIfaceArray <IRecordingScreenSettings> saRecordScreenScreens;
            CHECK_ERROR_BREAK(recordingSettings, COMGETTER(Screens)(ComSafeArrayAsOutParam(saRecordScreenScreens)));

            /* Note: For now all screens have the same configuration. */
            for (size_t i = 0; i < saRecordScreenScreens.size(); ++i)
            {
                CHECK_ERROR_BREAK(saRecordScreenScreens[i], COMSETTER(Enabled)(TRUE));
                CHECK_ERROR_BREAK(saRecordScreenScreens[i], COMSETTER(Filename)(Bstr(szRecordFilename).raw()));
                CHECK_ERROR_BREAK(saRecordScreenScreens[i], COMSETTER(VideoWidth)(ulRecordVideoWidth));
                CHECK_ERROR_BREAK(saRecordScreenScreens[i], COMSETTER(VideoHeight)(ulRecordVideoHeight));
                CHECK_ERROR_BREAK(saRecordScreenScreens[i], COMSETTER(VideoRate)(ulRecordVideoRate));
            }
        }
#endif /* defined(VBOX_WITH_RECORDING) */

        /* get the machine debugger (isn't necessarily available) */
        ComPtr <IMachineDebugger> machineDebugger;
        console->COMGETTER(Debugger)(machineDebugger.asOutParam());
        if (machineDebugger)
        {
            Log(("Machine debugger available!\n"));
        }

        if (fRawR0 != ~0U)
        {
            if (!machineDebugger)
            {
                RTPrintf("Error: No debugger object; -%srawr0 cannot be executed!\n", fRawR0 ? "" : "no");
                break;
            }
            machineDebugger->COMSETTER(RecompileSupervisor)(!fRawR0);
        }
        if (fRawR3 != ~0U)
        {
            if (!machineDebugger)
            {
                RTPrintf("Error: No debugger object; -%srawr3 cannot be executed!\n", fRawR3 ? "" : "no");
                break;
            }
            machineDebugger->COMSETTER(RecompileUser)(!fRawR3);
        }
        if (fPATM != ~0U)
        {
            if (!machineDebugger)
            {
                RTPrintf("Error: No debugger object; -%spatm cannot be executed!\n", fPATM ? "" : "no");
                break;
            }
            machineDebugger->COMSETTER(PATMEnabled)(fPATM);
        }
        if (fCSAM != ~0U)
        {
            if (!machineDebugger)
            {
                RTPrintf("Error: No debugger object; -%scsam cannot be executed!\n", fCSAM ? "" : "no");
                break;
            }
            machineDebugger->COMSETTER(CSAMEnabled)(fCSAM);
        }

        /* initialize global references */
        gConsole = console;
        gEventQ = com::NativeEventQueue::getMainEventQueue();

        /* VirtualBoxClient events registration. */
        {
            ComPtr<IEventSource> pES;
            CHECK_ERROR(pVirtualBoxClient, COMGETTER(EventSource)(pES.asOutParam()));
            ComObjPtr<VirtualBoxClientEventListenerImpl> listener;
            listener.createObject();
            listener->init(new VirtualBoxClientEventListener());
            vboxClientListener = listener;
            com::SafeArray<VBoxEventType_T> eventTypes;
            eventTypes.push_back(VBoxEventType_OnVBoxSVCAvailabilityChanged);
            CHECK_ERROR(pES, RegisterListener(vboxClientListener, ComSafeArrayAsInParam(eventTypes), true));
        }

        /* Console events registration. */
        {
            ComPtr<IEventSource> es;
            CHECK_ERROR(console, COMGETTER(EventSource)(es.asOutParam()));
            consoleListener.createObject();
            consoleListener->init(new ConsoleEventListener());
            com::SafeArray<VBoxEventType_T> eventTypes;
            eventTypes.push_back(VBoxEventType_OnMouseCapabilityChanged);
            eventTypes.push_back(VBoxEventType_OnStateChanged);
            eventTypes.push_back(VBoxEventType_OnVRDEServerInfoChanged);
            eventTypes.push_back(VBoxEventType_OnCanShowWindow);
            eventTypes.push_back(VBoxEventType_OnShowWindow);
            eventTypes.push_back(VBoxEventType_OnGuestPropertyChanged);
            CHECK_ERROR(es, RegisterListener(consoleListener, ComSafeArrayAsInParam(eventTypes), true));
        }

        /* Default is to use the VM setting for the VRDE server. */
        enum VRDEOption
        {
            VRDEOption_Config,
            VRDEOption_Off,
            VRDEOption_On
        };
        VRDEOption enmVRDEOption = VRDEOption_Config;
        BOOL fVRDEEnabled;
        ComPtr <IVRDEServer> vrdeServer;
        CHECK_ERROR_BREAK(machine, COMGETTER(VRDEServer)(vrdeServer.asOutParam()));
        CHECK_ERROR_BREAK(vrdeServer, COMGETTER(Enabled)(&fVRDEEnabled));

        if (vrdeEnabled != NULL)
        {
            /* -vrde on|off|config */
            if (!strcmp(vrdeEnabled, "off") || !strcmp(vrdeEnabled, "disable"))
                enmVRDEOption = VRDEOption_Off;
            else if (!strcmp(vrdeEnabled, "on") || !strcmp(vrdeEnabled, "enable"))
                enmVRDEOption = VRDEOption_On;
            else if (strcmp(vrdeEnabled, "config"))
            {
                RTPrintf("-vrde requires an argument (on|off|config)\n");
                break;
            }
        }

        Log(("VBoxHeadless: enmVRDE %d, fVRDEEnabled %d\n", enmVRDEOption, fVRDEEnabled));

        if (enmVRDEOption != VRDEOption_Off)
        {
            /* Set other specified options. */

            /* set VRDE port if requested by the user */
            if (vrdePort != NULL)
            {
                Bstr bstr = vrdePort;
                CHECK_ERROR_BREAK(vrdeServer, SetVRDEProperty(Bstr("TCP/Ports").raw(), bstr.raw()));
            }
            /* set VRDE address if requested by the user */
            if (vrdeAddress != NULL)
            {
                CHECK_ERROR_BREAK(vrdeServer, SetVRDEProperty(Bstr("TCP/Address").raw(), Bstr(vrdeAddress).raw()));
            }

            /* Set VRDE properties. */
            if (cVRDEProperties > 0)
            {
                for (unsigned i = 0; i < cVRDEProperties; i++)
                {
                    /* Parse 'name=value' */
                    char *pszProperty = RTStrDup(aVRDEProperties[i]);
                    if (pszProperty)
                    {
                        char *pDelimiter = strchr(pszProperty, '=');
                        if (pDelimiter)
                        {
                            *pDelimiter = '\0';

                            Bstr bstrName = pszProperty;
                            Bstr bstrValue = &pDelimiter[1];
                            CHECK_ERROR_BREAK(vrdeServer, SetVRDEProperty(bstrName.raw(), bstrValue.raw()));
                        }
                        else
                        {
                            RTPrintf("Error: Invalid VRDE property '%s'\n", aVRDEProperties[i]);
                            RTStrFree(pszProperty);
                            rc = E_INVALIDARG;
                            break;
                        }
                        RTStrFree(pszProperty);
                    }
                    else
                    {
                        RTPrintf("Error: Failed to allocate memory for VRDE property '%s'\n", aVRDEProperties[i]);
                        rc = E_OUTOFMEMORY;
                        break;
                    }
                }
                if (FAILED(rc))
                    break;
            }

        }

        if (enmVRDEOption == VRDEOption_On)
        {
            /* enable VRDE server (only if currently disabled) */
            if (!fVRDEEnabled)
            {
                CHECK_ERROR_BREAK(vrdeServer, COMSETTER(Enabled)(TRUE));
            }
        }
        else if (enmVRDEOption == VRDEOption_Off)
        {
            /* disable VRDE server (only if currently enabled */
            if (fVRDEEnabled)
            {
                CHECK_ERROR_BREAK(vrdeServer, COMSETTER(Enabled)(FALSE));
            }
        }

        /* Disable the host clipboard before powering up */
        console->COMSETTER(UseHostClipboard)(false);

        Log(("VBoxHeadless: Powering up the machine...\n"));

        ComPtr <IProgress> progress;
        if (!fPaused)
            CHECK_ERROR_BREAK(console, PowerUp(progress.asOutParam()));
        else
            CHECK_ERROR_BREAK(console, PowerUpPaused(progress.asOutParam()));

        /*
         * Wait for the result because there can be errors.
         *
         * It's vital to process events while waiting (teleportation deadlocks),
         * so we'll poll for the completion instead of waiting on it.
         */
        for (;;)
        {
            BOOL fCompleted;
            rc = progress->COMGETTER(Completed)(&fCompleted);
            if (FAILED(rc) || fCompleted)
                break;

            /* Process pending events, then wait for new ones. Note, this
             * processes NULL events signalling event loop termination. */
            gEventQ->processEventQueue(0);
            if (!g_fTerminateFE)
                gEventQ->processEventQueue(500);
        }

        if (SUCCEEDED(progress->WaitForCompletion(-1)))
        {
            /* Figure out if the operation completed with a failed status
             * and print the error message. Terminate immediately, and let
             * the cleanup code take care of potentially pending events. */
            LONG progressRc;
            progress->COMGETTER(ResultCode)(&progressRc);
            rc = progressRc;
            if (FAILED(rc))
            {
                com::ProgressErrorInfo info(progress);
                if (info.isBasicAvailable())
                {
                    RTPrintf("Error: failed to start machine. Error message: %ls\n", info.getText().raw());
                }
                else
                {
                    RTPrintf("Error: failed to start machine. No error message available!\n");
                }
                break;
            }
        }

#ifdef VBOX_WITH_SAVESTATE_ON_SIGNAL
        signal(SIGINT, SaveState);
        signal(SIGTERM, SaveState);
#endif

        Log(("VBoxHeadless: Waiting for PowerDown...\n"));

        while (   !g_fTerminateFE
               && RT_SUCCESS(gEventQ->processEventQueue(RT_INDEFINITE_WAIT)))
            /* nothing */ ;

        Log(("VBoxHeadless: event loop has terminated...\n"));

#ifdef VBOX_WITH_RECORDING
        if (fRecordEnabled)
        {
            if (!machine.isNull())
            {
                ComPtr<IRecordingSettings> recordingSettings;
                CHECK_ERROR_BREAK(machine, COMGETTER(RecordingSettings)(recordingSettings.asOutParam()));
                CHECK_ERROR_BREAK(recordingSettings, COMSETTER(Enabled)(FALSE));
            }
        }
#endif /* VBOX_WITH_RECORDING */

        /* we don't have to disable VRDE here because we don't save the settings of the VM */
    }
    while (0);

    /*
     * Get the machine state.
     */
    MachineState_T machineState = MachineState_Aborted;
    if (!machine.isNull())
        machine->COMGETTER(State)(&machineState);

    /*
     * Turn off the VM if it's running
     */
    if (   gConsole
        && (   machineState == MachineState_Running
            || machineState == MachineState_Teleporting
            || machineState == MachineState_LiveSnapshotting
            /** @todo power off paused VMs too? */
           )
       )
    do
    {
        consoleListener->getWrapped()->ignorePowerOffEvents(true);
        ComPtr<IProgress> pProgress;
        CHECK_ERROR_BREAK(gConsole, PowerDown(pProgress.asOutParam()));
        CHECK_ERROR_BREAK(pProgress, WaitForCompletion(-1));
        BOOL completed;
        CHECK_ERROR_BREAK(pProgress, COMGETTER(Completed)(&completed));
        ASSERT(completed);
        LONG hrc;
        CHECK_ERROR_BREAK(pProgress, COMGETTER(ResultCode)(&hrc));
        if (FAILED(hrc))
        {
            RTPrintf("VBoxHeadless: ERROR: Failed to power down VM!");
            com::ErrorInfo info;
            if (!info.isFullAvailable() && !info.isBasicAvailable())
                com::GluePrintRCMessage(hrc);
            else
                GluePrintErrorInfo(info);
            break;
        }
    } while (0);

    /* VirtualBox callback unregistration. */
    if (vboxListener)
    {
        ComPtr<IEventSource> es;
        CHECK_ERROR(virtualBox, COMGETTER(EventSource)(es.asOutParam()));
        if (!es.isNull())
            CHECK_ERROR(es, UnregisterListener(vboxListener));
        vboxListener.setNull();
    }

    /* Console callback unregistration. */
    if (consoleListener)
    {
        ComPtr<IEventSource> es;
        CHECK_ERROR(gConsole, COMGETTER(EventSource)(es.asOutParam()));
        if (!es.isNull())
            CHECK_ERROR(es, UnregisterListener(consoleListener));
        consoleListener.setNull();
    }

    /* VirtualBoxClient callback unregistration. */
    if (vboxClientListener)
    {
        ComPtr<IEventSource> pES;
        CHECK_ERROR(pVirtualBoxClient, COMGETTER(EventSource)(pES.asOutParam()));
        if (!pES.isNull())
            CHECK_ERROR(pES, UnregisterListener(vboxClientListener));
        vboxClientListener.setNull();
    }

    /* No more access to the 'console' object, which will be uninitialized by the next session->Close call. */
    gConsole = NULL;

    if (fSessionOpened)
    {
        /*
         * Close the session. This will also uninitialize the console and
         * unregister the callback we've registered before.
         */
        Log(("VBoxHeadless: Closing the session...\n"));
        session->UnlockMachine();
    }

    /* Must be before com::Shutdown */
    session.setNull();
    virtualBox.setNull();
    pVirtualBoxClient.setNull();
    machine.setNull();

    com::Shutdown();

    LogFlow(("VBoxHeadless FINISHED.\n"));

    return FAILED(rc) ? 1 : 0;
}


#ifndef VBOX_WITH_HARDENING
/**
 * Main entry point.
 */
int main(int argc, char **argv, char **envp)
{
    // initialize VBox Runtime
    int rc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_SUPLIB);
    if (RT_FAILURE(rc))
    {
        RTPrintf("VBoxHeadless: Runtime Error:\n"
                 " %Rrc -- %Rrf\n", rc, rc);
        switch (rc)
        {
            case VERR_VM_DRIVER_NOT_INSTALLED:
                RTPrintf("Cannot access the kernel driver. Make sure the kernel module has been \n"
                        "loaded successfully. Aborting ...\n");
                break;
            default:
                break;
        }
        return 1;
    }

    return TrustedMain(argc, argv, envp);
}
#endif /* !VBOX_WITH_HARDENING */
