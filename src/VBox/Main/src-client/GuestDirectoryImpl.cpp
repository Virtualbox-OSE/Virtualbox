/* $Id: GuestDirectoryImpl.cpp $ */
/** @file
 * VirtualBox Main - Guest directory handling.
 */

/*
 * Copyright (C) 2012-2020 Oracle Corporation
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
#define LOG_GROUP LOG_GROUP_MAIN_GUESTDIRECTORY
#include "LoggingNew.h"

#ifndef VBOX_WITH_GUEST_CONTROL
# error "VBOX_WITH_GUEST_CONTROL must defined in this file"
#endif
#include "GuestDirectoryImpl.h"
#include "GuestSessionImpl.h"
#include "GuestCtrlImplPrivate.h"

#include "Global.h"
#include "AutoCaller.h"

#include <VBox/com/array.h>


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(GuestDirectory)

HRESULT GuestDirectory::FinalConstruct(void)
{
    LogFlowThisFunc(("\n"));
    return BaseFinalConstruct();
}

void GuestDirectory::FinalRelease(void)
{
    LogFlowThisFuncEnter();
    uninit();
    BaseFinalRelease();
    LogFlowThisFuncLeave();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

int GuestDirectory::init(Console *pConsole, GuestSession *pSession, ULONG aObjectID, const GuestDirectoryOpenInfo &openInfo)
{
    LogFlowThisFunc(("pConsole=%p, pSession=%p, aObjectID=%RU32, strPath=%s, strFilter=%s, uFlags=%x\n",
                     pConsole, pSession, aObjectID, openInfo.mPath.c_str(), openInfo.mFilter.c_str(), openInfo.mFlags));

    AssertPtrReturn(pConsole, VERR_INVALID_POINTER);
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);

    /* Enclose the state transition NotReady->InInit->Ready. */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), VERR_OBJECT_DESTROYED);

    int vrc = bindToSession(pConsole, pSession, aObjectID);
    if (RT_SUCCESS(vrc))
    {
        mSession  = pSession;
        mObjectID = aObjectID;

        mData.mOpenInfo = openInfo;
    }

    if (RT_SUCCESS(vrc))
    {
        /* Start the directory process on the guest. */
        GuestProcessStartupInfo procInfo;
        procInfo.mName      = Utf8StrFmt(tr("Opening directory \"%s\""), openInfo.mPath.c_str());
        procInfo.mTimeoutMS = 5 * 60 * 1000; /* 5 minutes timeout. */
        procInfo.mFlags     = ProcessCreateFlag_WaitForStdOut;
        procInfo.mExecutable= Utf8Str(VBOXSERVICE_TOOL_LS);

        procInfo.mArguments.push_back(procInfo.mExecutable);
        procInfo.mArguments.push_back(Utf8Str("--machinereadable"));
        /* We want the long output format which contains all the object details. */
        procInfo.mArguments.push_back(Utf8Str("-l"));
#if 0 /* Flags are not supported yet. */
        if (uFlags & DirectoryOpenFlag_NoSymlinks)
            procInfo.mArguments.push_back(Utf8Str("--nosymlinks")); /** @todo What does GNU here? */
#endif
        /** @todo Recursion support? */
        procInfo.mArguments.push_back(openInfo.mPath); /* The directory we want to open. */

        /*
         * Start the process synchronously and keep it around so that we can use
         * it later in subsequent read() calls.
         */
        vrc = mData.mProcessTool.init(mSession, procInfo, false /* Async */, NULL /* Guest rc */);
        if (RT_SUCCESS(vrc))
        {
            /* As we need to know if the directory we were about to open exists and and is accessible,
             * do the first read here in order to return a meaningful status here. */
            int rcGuest = VERR_IPE_UNINITIALIZED_STATUS;
            vrc = i_readInternal(mData.mObjData, &rcGuest);
            if (RT_FAILURE(vrc))
            {
                /*
                 * We need to actively terminate our process tool in case of an error here,
                 * as this otherwise would be done on (directory) object destruction implicitly.
                 * This in turn then will run into a timeout, as the directory object won't be
                 * around anymore at that time. Ugly, but that's how it is for the moment.
                 */
                int vrcTerm = mData.mProcessTool.terminate(30 * RT_MS_1SEC, NULL /* prcGuest */);
                AssertRC(vrcTerm);

                if (vrc == VERR_GSTCTL_GUEST_ERROR)
                    vrc = rcGuest;
            }
        }
    }

    /* Confirm a successful initialization when it's the case. */
    if (RT_SUCCESS(vrc))
        autoInitSpan.setSucceeded();
    else
        autoInitSpan.setFailed();

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Uninitializes the instance.
 * Called from FinalRelease().
 */
void GuestDirectory::uninit(void)
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition Ready->InUninit->NotReady. */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    LogFlowThisFuncLeave();
}

// implementation of private wrapped getters/setters for attributes
/////////////////////////////////////////////////////////////////////////////

HRESULT GuestDirectory::getDirectoryName(com::Utf8Str &aDirectoryName)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aDirectoryName = mData.mOpenInfo.mPath;

    return S_OK;
}

HRESULT GuestDirectory::getFilter(com::Utf8Str &aFilter)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aFilter = mData.mOpenInfo.mFilter;

    return S_OK;
}

// private methods
/////////////////////////////////////////////////////////////////////////////

int GuestDirectory::i_callbackDispatcher(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCb, VERR_INVALID_POINTER);

    LogFlowThisFunc(("strPath=%s, uContextID=%RU32, uFunction=%RU32, pSvcCb=%p\n",
                     mData.mOpenInfo.mPath.c_str(), pCbCtx->uContextID, pCbCtx->uMessage, pSvcCb));

    int vrc;
    switch (pCbCtx->uMessage)
    {
        case GUEST_MSG_DIR_NOTIFY:
        {
            int idx = 1; /* Current parameter index. */
            CALLBACKDATA_DIR_NOTIFY dataCb;
            /* pSvcCb->mpaParms[0] always contains the context ID. */
            HGCMSvcGetU32(&pSvcCb->mpaParms[idx++], &dataCb.uType);
            HGCMSvcGetU32(&pSvcCb->mpaParms[idx++], &dataCb.rc);

            LogFlowFunc(("uType=%RU32, vrcGguest=%Rrc\n", dataCb.uType, (int)dataCb.rc));

            switch (dataCb.uType)
            {
                /* Nothing here yet, nothing to dispatch further. */

                default:
                    vrc = VERR_NOT_SUPPORTED;
                    break;
            }
            break;
        }

        default:
            /* Silently ignore not implemented functions. */
            vrc = VERR_NOT_SUPPORTED;
            break;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Converts a given guest directory error to a string.
 *
 * @returns Error string.
 * @param   rcGuest             Guest file error to return string for.
 * @param   pcszWhat            Hint of what was involved when the error occurred.
 */
/* static */
Utf8Str GuestDirectory::i_guestErrorToString(int rcGuest, const char *pcszWhat)
{
    AssertPtrReturn(pcszWhat, "");

    Utf8Str strErr;

#define CASE_MSG(a_iRc, ...) \
    case a_iRc: strErr = Utf8StrFmt(__VA_ARGS__); break;

    /** @todo pData->u32Flags: int vs. uint32 -- IPRT errors are *negative* !!! */
    switch (rcGuest)
    {
        CASE_MSG(VERR_CANT_CREATE  , tr("Access to guest directory \"%s\" is denied"), pcszWhat);
        CASE_MSG(VERR_DIR_NOT_EMPTY, tr("Guest directory \"%s\" is not empty"), pcszWhat);
        default:
        {
            strErr = Utf8StrFmt("Error \"%s\" (%Rrc) for guest directory \"%s\" occurred\n",
                                RTErrGetFull(rcGuest), rcGuest, pcszWhat);
            break;
        }
    }

#undef CASE_MSG

    return strErr;
}

/**
 * @copydoc GuestObject::i_onUnregister
 */
int GuestDirectory::i_onUnregister(void)
{
    LogFlowThisFuncEnter();

    int vrc = VINF_SUCCESS;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * @copydoc GuestObject::i_onSessionStatusChange
 */
int GuestDirectory::i_onSessionStatusChange(GuestSessionStatus_T enmSessionStatus)
{
    RT_NOREF(enmSessionStatus);

    LogFlowThisFuncEnter();

    int vrc = VINF_SUCCESS;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Closes this guest directory and removes it from the
 * guest session's directory list.
 *
 * @return VBox status code.
 * @param  prcGuest             Where to store the guest result code in case VERR_GSTCTL_GUEST_ERROR is returned.
 */
int GuestDirectory::i_closeInternal(int *prcGuest)
{
    AssertPtrReturn(prcGuest, VERR_INVALID_POINTER);

    int rc = mData.mProcessTool.terminate(30 * 1000 /* 30s timeout */, prcGuest);
    if (RT_FAILURE(rc))
        return rc;

    AssertPtr(mSession);
    int rc2 = mSession->i_directoryUnregister(this);
    if (RT_SUCCESS(rc))
        rc = rc2;

    LogFlowThisFunc(("Returning rc=%Rrc\n", rc));
    return rc;
}

/**
 * Reads the next directory entry, internal version.
 *
 * @return VBox status code. Will return VERR_NO_MORE_FILES if no more entries are available.
 * @param  objData              Where to store the read directory entry as internal object data.
 * @param  prcGuest             Where to store the guest result code in case VERR_GSTCTL_GUEST_ERROR is returned.
 */
int GuestDirectory::i_readInternal(GuestFsObjData &objData, int *prcGuest)
{
    AssertPtrReturn(prcGuest, VERR_INVALID_POINTER);

    GuestProcessStreamBlock curBlock;
    int rc = mData.mProcessTool.waitEx(GUESTPROCESSTOOL_WAIT_FLAG_STDOUT_BLOCK, &curBlock, prcGuest);
    if (RT_SUCCESS(rc))
    {
        /*
         * Note: The guest process can still be around to serve the next
         *       upcoming stream block next time.
         */
        if (!mData.mProcessTool.isRunning())
            rc = mData.mProcessTool.getTerminationStatus(); /* Tool process is not running (anymore). Check termination status. */

        if (RT_SUCCESS(rc))
        {
            if (curBlock.GetCount()) /* Did we get content? */
            {
                if (curBlock.GetString("name"))
                {
                    rc = objData.FromLs(curBlock, true /* fLong */);
                }
                else
                    rc = VERR_PATH_NOT_FOUND;
            }
            else
            {
                /* Nothing to read anymore. Tell the caller. */
                rc = VERR_NO_MORE_FILES;
            }
        }
    }

    LogFlowThisFunc(("Returning rc=%Rrc\n", rc));
    return rc;
}

/**
 * Reads the next directory entry.
 *
 * @return VBox status code. Will return VERR_NO_MORE_FILES if no more entries are available.
 * @param  fsObjInfo            Where to store the read directory entry.
 * @param  prcGuest             Where to store the guest result code in case VERR_GSTCTL_GUEST_ERROR is returned.
 */
int GuestDirectory::i_read(ComObjPtr<GuestFsObjInfo> &fsObjInfo, int *prcGuest)
{
    AssertPtrReturn(prcGuest, VERR_INVALID_POINTER);

    /* Create the FS info object. */
    HRESULT hr = fsObjInfo.createObject();
    if (FAILED(hr))
        return VERR_COM_UNEXPECTED;

    int rc;

    /* If we have a valid object data cache, read from it. */
    if (mData.mObjData.mName.isNotEmpty())
    {
        rc = fsObjInfo->init(mData.mObjData);
        if (RT_SUCCESS(rc))
        {
            mData.mObjData.mName = ""; /* Mark the object data as being empty (beacon). */
        }
    }
    else /* Otherwise ask the guest for the next object data (block). */
    {

        GuestFsObjData objData;
        rc = i_readInternal(objData, prcGuest);
        if (RT_SUCCESS(rc))
            rc = fsObjInfo->init(objData);
    }

    LogFlowThisFunc(("Returning rc=%Rrc\n", rc));
    return rc;
}

// implementation of public methods
/////////////////////////////////////////////////////////////////////////////
HRESULT GuestDirectory::close()
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    LogFlowThisFuncEnter();

    HRESULT hr = S_OK;

    int rcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_closeInternal(&rcGuest);
    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
                hr = setErrorExternal(this, tr("Closing guest directory failed"),
                                      GuestErrorInfo(GuestErrorInfo::Type_Directory, rcGuest, mData.mOpenInfo.mPath.c_str()));
                break;

            case VERR_NOT_SUPPORTED:
                /* Silently skip old Guest Additions which do not support killing the
                 * the guest directory handling process. */
                break;

            default:
                hr = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                  tr("Closing guest directory \"%s\" failed: %Rrc"), mData.mOpenInfo.mPath.c_str(), vrc);
                break;
        }
    }

    return hr;
}

HRESULT GuestDirectory::read(ComPtr<IFsObjInfo> &aObjInfo)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    LogFlowThisFuncEnter();

    HRESULT hr = S_OK;

    ComObjPtr<GuestFsObjInfo> fsObjInfo;
    int rcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_read(fsObjInfo, &rcGuest);
    if (RT_SUCCESS(vrc))
    {
        /* Return info object to the caller. */
        hr = fsObjInfo.queryInterfaceTo(aObjInfo.asOutParam());
    }
    else
    {
        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
                hr = setErrorExternal(this, tr("Reading guest directory failed"),
                                      GuestErrorInfo(GuestErrorInfo::Type_ToolLs, rcGuest, mData.mOpenInfo.mPath.c_str()));
                break;

            case VERR_GSTCTL_PROCESS_EXIT_CODE:
                hr = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Reading guest directory \"%s\" failed: %Rrc"),
                                  mData.mOpenInfo.mPath.c_str(), mData.mProcessTool.getRc());
                break;

            case VERR_PATH_NOT_FOUND:
                hr = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Reading guest directory \"%s\" failed: Path not found"),
                                  mData.mOpenInfo.mPath.c_str());
                break;

            case VERR_NO_MORE_FILES:
                /* See SDK reference. */
                hr = setErrorBoth(VBOX_E_OBJECT_NOT_FOUND, vrc, tr("Reading guest directory \"%s\" failed: No more entries"),
                                  mData.mOpenInfo.mPath.c_str());
                break;

            default:
                hr = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Reading guest directory \"%s\" returned error: %Rrc\n"),
                                  mData.mOpenInfo.mPath.c_str(), vrc);
                break;
        }
    }

    LogFlowThisFunc(("Returning hr=%Rhrc / vrc=%Rrc\n", hr, vrc));
    return hr;
}

