/* $Id: GuestDnDTargetImpl.h $ */
/** @file
 * VBox Console COM Class implementation - Guest drag'n drop target.
 */

/*
 * Copyright (C) 2014-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef MAIN_INCLUDED_GuestDnDTargetImpl_h
#define MAIN_INCLUDED_GuestDnDTargetImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "GuestDnDTargetWrap.h"
#include "GuestDnDPrivate.h"

#include <VBox/GuestHost/DragAndDrop.h>
#include <VBox/HostServices/DragAndDropSvc.h>

struct GuestDnDSendCtx;
class GuestDnDSendDataTask;

class ATL_NO_VTABLE GuestDnDTarget :
    public GuestDnDTargetWrap,
    public GuestDnDBase
{
public:
    /** @name COM and internal init/term/mapping cruft.
     * @{ */
    DECLARE_EMPTY_CTOR_DTOR(GuestDnDTarget)

    HRESULT init(const ComObjPtr<Guest>& pGuest);
    void    uninit(void);

    HRESULT FinalConstruct(void);
    void    FinalRelease(void);
    /** @}  */

private:

    /** Private wrapped @name IDnDBase methods.
     * @{ */
    HRESULT isFormatSupported(const com::Utf8Str &aFormat, BOOL *aSupported);
    HRESULT getFormats(GuestDnDMIMEList &aFormats);
    HRESULT addFormats(const GuestDnDMIMEList &aFormats);
    HRESULT removeFormats(const GuestDnDMIMEList &aFormats);

    HRESULT getProtocolVersion(ULONG *aProtocolVersion);
    /** @}  */

    /** Private wrapped @name IDnDTarget methods.
     * @{ */
    HRESULT enter(ULONG aScreenId, ULONG ax, ULONG aY, DnDAction_T aDefaultAction, const std::vector<DnDAction_T> &aAllowedActions, const GuestDnDMIMEList &aFormats, DnDAction_T *aResultAction);
    HRESULT move(ULONG aScreenId, ULONG aX, ULONG aY, DnDAction_T aDefaultAction, const std::vector<DnDAction_T> &aAllowedActions, const GuestDnDMIMEList &aFormats, DnDAction_T *aResultAction);
    HRESULT leave(ULONG aScreenId);
    HRESULT drop(ULONG aScreenId, ULONG aX, ULONG aY, DnDAction_T aDefaultAction, const std::vector<DnDAction_T> &aAllowedActions, const GuestDnDMIMEList &aFormats, com::Utf8Str &aFormat, DnDAction_T *aResultAction);
    HRESULT sendData(ULONG aScreenId, const com::Utf8Str &aFormat, const std::vector<BYTE> &aData, ComPtr<IProgress> &aProgress);
    HRESULT cancel(BOOL *aVeto);
    /** @}  */

protected:

    static Utf8Str i_guestErrorToString(int guestRc);
    static Utf8Str i_hostErrorToString(int hostRc);

    /** @name Callbacks for dispatch handler.
     * @{ */
    static DECLCALLBACK(int) i_sendTransferDataCallback(uint32_t uMsg, void *pvParms, size_t cbParms, void *pvUser);
    /** @}  */

protected:

    void i_reset(void);

    int i_sendData(GuestDnDSendCtx *pCtx, RTMSINTERVAL msTimeout);

    int i_sendMetaDataBody(GuestDnDSendCtx *pCtx);
    int i_sendMetaDataHeader(GuestDnDSendCtx *pCtx);

    int i_sendTransferData(GuestDnDSendCtx *pCtx, RTMSINTERVAL msTimeout);
    int i_sendTransferListObject(GuestDnDSendCtx *pCtx,  PDNDTRANSFERLIST pList, GuestDnDMsg *pMsg);

    int i_sendDirectory(GuestDnDSendCtx *pCtx, PDNDTRANSFEROBJECT pObj, GuestDnDMsg *pMsg);
    int i_sendFile(GuestDnDSendCtx *pCtx, PDNDTRANSFEROBJECT pObj, GuestDnDMsg *pMsg);
    int i_sendFileData(GuestDnDSendCtx *pCtx, PDNDTRANSFEROBJECT pObj, GuestDnDMsg *pMsg);

    int i_sendRawData(GuestDnDSendCtx *pCtx, RTMSINTERVAL msTimeout);

protected:

    struct
    {
        /** Maximum data block size (in bytes) the target can handle. */
        uint32_t        mcbBlockSize;
        /** The context for sending data to the guest.
         *  At the moment only one transfer at a time is supported. */
        GuestDnDSendCtx mSendCtx;
    } mData;

    friend class GuestDnDSendDataTask;
};

#endif /* !MAIN_INCLUDED_GuestDnDTargetImpl_h */

