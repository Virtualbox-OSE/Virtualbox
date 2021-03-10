/* $Id: VBoxNetDhcpd.cpp $ */
/** @file
 * VBoxNetDhcpd - DHCP server for host-only and NAT networks.
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


/** @page pg_net_dhcp       VBoxNetDHCP
 *
 * Write a few words...
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/cdefs.h>

/*
 * Need to get host/network order conversion stuff from Windows headers,
 * so we do not define them in LWIP and then try to re-define them in
 * Windows headers.
 */
#ifdef RT_OS_WINDOWS
# include <iprt/win/winsock2.h>
#endif

#include "DhcpdInternal.h"
#include <iprt/param.h>
#include <iprt/errcore.h>

#include <iprt/initterm.h>
#include <iprt/message.h>

#include <iprt/net.h>
#include <iprt/path.h>
#include <iprt/stream.h>

#include <VBox/sup.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/pdmnetinline.h>
#include <VBox/intnet.h>
#include <VBox/intnetinline.h>

#include "VBoxLwipCore.h"
#include "Config.h"
#include "DHCPD.h"
#include "DhcpMessage.h"

extern "C"
{
#include "lwip/sys.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/udp.h"
#include "netif/etharp.h"
}

#include <string>
#include <vector>
#include <memory>

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#endif

struct delete_pbuf
{
    delete_pbuf() {}
    void operator()(struct pbuf *p) const { pbuf_free(p); }
};

typedef std::unique_ptr<pbuf, delete_pbuf> unique_ptr_pbuf;


#define CALL_VMMR0(op, req) \
    (SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, (op), 0, &(req).Hdr))


class VBoxNetDhcpd
{
    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(VBoxNetDhcpd);

private:
    PRTLOGGER m_pStderrReleaseLogger;

    /* intnet plumbing */
    PSUPDRVSESSION m_pSession;
    INTNETIFHANDLE m_hIf;
    PINTNETBUF m_pIfBuf;

    /* lwip stack connected to the intnet */
    struct netif m_LwipNetif;

    Config *m_Config;

    /* listening pcb */
    struct udp_pcb *m_Dhcp4Pcb;

    DHCPD m_server;

public:
    VBoxNetDhcpd();
    ~VBoxNetDhcpd();

    int main(int argc, char **argv);

private:
    int logInitStderr();

    /*
     * Boilerplate code.
     */
    int r3Init();
    void r3Fini();

    int vmmInit();

    int ifInit(const RTCString &strNetwork,
               const RTCString &strTrunk = RTCString(),
               INTNETTRUNKTYPE enmTrunkType = kIntNetTrunkType_WhateverNone);
    int ifOpen(const RTCString &strNetwork,
               const RTCString &strTrunk,
               INTNETTRUNKTYPE enmTrunkType);
    int ifGetBuf();
    int ifActivate();

    int ifProcessInput();
    int ifFlush();

    int ifClose();

    void ifPump();
    int ifInput(void *pvSegFrame, uint32_t cbSegFrame);

    int ifOutput(PCINTNETSEG paSegs, size_t cSegs, size_t cbFrame);


    /*
     * lwIP callbacks
     */
    static DECLCALLBACK(void) lwipInitCB(void *pvArg);
    void lwipInit();

    static err_t netifInitCB(netif *pNetif);
    err_t netifInit(netif *pNetif);

    static err_t netifLinkOutputCB(netif *pNetif, pbuf *pPBuf);
    err_t netifLinkOutput(pbuf *pPBuf);

    static void dhcp4RecvCB(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                            ip_addr_t *addr, u16_t port);
    void dhcp4Recv(struct udp_pcb *pcb, struct pbuf *p, ip_addr_t *addr, u16_t port);
};


VBoxNetDhcpd::VBoxNetDhcpd()
  : m_pStderrReleaseLogger(NULL),
    m_pSession(NIL_RTR0PTR),
    m_hIf(INTNET_HANDLE_INVALID),
    m_pIfBuf(NULL),
    m_LwipNetif(),
    m_Config(NULL),
    m_Dhcp4Pcb(NULL)
{
    int rc;

    logInitStderr();

    rc = r3Init();
    if (RT_FAILURE(rc))
        return;

    vmmInit();
}


VBoxNetDhcpd::~VBoxNetDhcpd()
{
    ifClose();
    r3Fini();
}


/*
 * We don't know the name of the release log file until we parse our
 * configuration because we use network name as basename.  To get
 * early logging to work, start with stderr-only release logger.
 *
 * We disable "sup" for this logger to avoid spam from SUPR3Init().
 */
int VBoxNetDhcpd::logInitStderr()
{
    static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;

    PRTLOGGER pLogger;
    int rc;

    uint32_t fFlags = 0;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    fFlags |= RTLOGFLAGS_USECRLF;
#endif

    rc = RTLogCreate(&pLogger, fFlags,
                     "all -sup all.restrict -default.restrict",
                     NULL,      /* environment base */
                     RT_ELEMENTS(s_apszGroups), s_apszGroups,
                     RTLOGDEST_STDERR, NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Failed to init stderr logger: %Rrs\n", rc);
        return rc;
    }

    m_pStderrReleaseLogger = pLogger;
    RTLogRelSetDefaultInstance(m_pStderrReleaseLogger);

    return VINF_SUCCESS;
}


int VBoxNetDhcpd::r3Init()
{
    AssertReturn(m_pSession == NIL_RTR0PTR, VERR_GENERAL_FAILURE);

    int rc = SUPR3Init(&m_pSession);
    return rc;
}


void VBoxNetDhcpd::r3Fini()
{
    if (m_pSession == NIL_RTR0PTR)
        return;

    SUPR3Term();
    m_pSession = NIL_RTR0PTR;
}


int VBoxNetDhcpd::vmmInit()
{
    char szPathVMMR0[RTPATH_MAX];
    int rc = RTPathExecDir(szPathVMMR0, sizeof(szPathVMMR0));
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(szPathVMMR0, sizeof(szPathVMMR0), "VMMR0.r0");
    if (RT_SUCCESS(rc))
        rc = SUPR3LoadVMM(szPathVMMR0, NULL /*pErrInfo*/);
    return rc;
}


int VBoxNetDhcpd::ifInit(const RTCString &strNetwork,
                         const RTCString &strTrunk,
                         INTNETTRUNKTYPE enmTrunkType)
{
    int rc;

    rc = ifOpen(strNetwork, strTrunk, enmTrunkType);
    if (RT_FAILURE(rc))
        return rc;

    rc = ifGetBuf();
    if (RT_FAILURE(rc))
        return rc;

    rc = ifActivate();
    if (RT_FAILURE(rc))
        return rc;

    return VINF_SUCCESS;
}


int VBoxNetDhcpd::ifOpen(const RTCString &strNetwork,
                         const RTCString &strTrunk,
                         INTNETTRUNKTYPE enmTrunkType)
{
    AssertReturn(m_pSession != NIL_RTR0PTR, VERR_GENERAL_FAILURE);
    AssertReturn(m_hIf == INTNET_HANDLE_INVALID, VERR_GENERAL_FAILURE);

    INTNETOPENREQ OpenReq;
    RT_ZERO(OpenReq);

    OpenReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    OpenReq.Hdr.cbReq = sizeof(OpenReq);
    OpenReq.pSession = m_pSession;

    int rc = RTStrCopy(OpenReq.szNetwork, sizeof(OpenReq.szNetwork), strNetwork.c_str());
    AssertRCReturn(rc, rc);

    rc = RTStrCopy(OpenReq.szTrunk, sizeof(OpenReq.szTrunk), strTrunk.c_str());
    AssertRCReturn(rc, rc);

    if (enmTrunkType != kIntNetTrunkType_Invalid)
        OpenReq.enmTrunkType = enmTrunkType;
    else
        OpenReq.enmTrunkType = kIntNetTrunkType_WhateverNone;

    OpenReq.fFlags = 0;
    OpenReq.cbSend = _128K;
    OpenReq.cbRecv = _256K;

    OpenReq.hIf = INTNET_HANDLE_INVALID;

    rc = CALL_VMMR0(VMMR0_DO_INTNET_OPEN, OpenReq);
    if (RT_FAILURE(rc))
        return rc;

    m_hIf = OpenReq.hIf;
    AssertReturn(m_hIf != INTNET_HANDLE_INVALID, VERR_GENERAL_FAILURE);

    return VINF_SUCCESS;
}


int VBoxNetDhcpd::ifGetBuf()
{
    AssertReturn(m_pSession != NIL_RTR0PTR, VERR_GENERAL_FAILURE);
    AssertReturn(m_hIf != INTNET_HANDLE_INVALID, VERR_GENERAL_FAILURE);
    AssertReturn(m_pIfBuf == NULL, VERR_GENERAL_FAILURE);

    INTNETIFGETBUFFERPTRSREQ GetBufferPtrsReq;
    int rc;

    GetBufferPtrsReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    GetBufferPtrsReq.Hdr.cbReq = sizeof(GetBufferPtrsReq);
    GetBufferPtrsReq.pSession = m_pSession;
    GetBufferPtrsReq.hIf = m_hIf;

    GetBufferPtrsReq.pRing0Buf = NIL_RTR0PTR;
    GetBufferPtrsReq.pRing3Buf = NULL;

    rc = CALL_VMMR0(VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS, GetBufferPtrsReq);
    if (RT_FAILURE(rc))
        return rc;

    m_pIfBuf = GetBufferPtrsReq.pRing3Buf;
    AssertReturn(m_pIfBuf != NULL, VERR_GENERAL_FAILURE);

    return VINF_SUCCESS;
}


int VBoxNetDhcpd::ifActivate()
{
    AssertReturn(m_pSession != NIL_RTR0PTR, VERR_GENERAL_FAILURE);
    AssertReturn(m_hIf != INTNET_HANDLE_INVALID, VERR_GENERAL_FAILURE);
    AssertReturn(m_pIfBuf != NULL, VERR_GENERAL_FAILURE);

    INTNETIFSETACTIVEREQ ActiveReq;
    int rc;

    ActiveReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    ActiveReq.Hdr.cbReq = sizeof(ActiveReq);
    ActiveReq.pSession = m_pSession;
    ActiveReq.hIf = m_hIf;

    ActiveReq.fActive = 1;

    rc = CALL_VMMR0(VMMR0_DO_INTNET_IF_SET_ACTIVE, ActiveReq);
    return rc;
}


/**
 * Process incoming packages forever.
 *
 * @note This function normally never returns, given that the process is
 *       typically just killed when shutting down a network.
 */
void VBoxNetDhcpd::ifPump()
{
    for (;;)
    {
        /*
         * Wait for input:
         */
        INTNETIFWAITREQ WaitReq;
        WaitReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        WaitReq.Hdr.cbReq = sizeof(WaitReq);
        WaitReq.pSession = m_pSession;
        WaitReq.hIf = m_hIf;
        WaitReq.cMillies = RT_INDEFINITE_WAIT;
        int rc = CALL_VMMR0(VMMR0_DO_INTNET_IF_WAIT, WaitReq);

        /*
         * Process any pending input before we wait again:
         */
        if (   RT_SUCCESS(rc)
            || rc == VERR_INTERRUPTED
            || rc == VERR_TIMEOUT /* paranoia */)
            ifProcessInput();
        else
        {
            DHCP_LOG_MSG_ERROR(("ifWait failed: %Rrc\n", rc));
            return;
        }
    }
}


int VBoxNetDhcpd::ifProcessInput()
{
    AssertReturn(m_pSession != NIL_RTR0PTR, VERR_GENERAL_FAILURE);
    AssertReturn(m_hIf != INTNET_HANDLE_INVALID, VERR_GENERAL_FAILURE);
    AssertReturn(m_pIfBuf != NULL, VERR_GENERAL_FAILURE);

    PCINTNETHDR pHdr = IntNetRingGetNextFrameToRead(&m_pIfBuf->Recv);
    while (pHdr)
    {
        const uint8_t u8Type = pHdr->u8Type;
        void *pvSegFrame;
        uint32_t cbSegFrame;

        if (u8Type == INTNETHDR_TYPE_FRAME)
        {
            pvSegFrame = IntNetHdrGetFramePtr(pHdr, m_pIfBuf);
            cbSegFrame = pHdr->cbFrame;

            ifInput(pvSegFrame, cbSegFrame);
        }
        else if (u8Type == INTNETHDR_TYPE_GSO)
        {
            size_t cbGso = pHdr->cbFrame;
            size_t cbFrame = cbGso - sizeof(PDMNETWORKGSO);

            PCPDMNETWORKGSO pGso = IntNetHdrGetGsoContext(pHdr, m_pIfBuf);
            if (PDMNetGsoIsValid(pGso, cbGso, cbFrame))
            {
                const uint32_t cSegs = PDMNetGsoCalcSegmentCount(pGso, cbFrame);
                for (uint32_t i = 0; i < cSegs; ++i)
                {
                    uint8_t abHdrScratch[256];
                    pvSegFrame = PDMNetGsoCarveSegmentQD(pGso, (uint8_t *)(pGso + 1), cbFrame,
                                                         abHdrScratch,
                                                         i, cSegs,
                                                         &cbSegFrame);
                    ifInput(pvSegFrame, (uint32_t)cbFrame);
                }
            }
        }

        /* Advance: */
        IntNetRingSkipFrame(&m_pIfBuf->Recv);
        pHdr = IntNetRingGetNextFrameToRead(&m_pIfBuf->Recv);
    }

    return VINF_SUCCESS;
}


/**
 * Got a frame from the internal network, feed it to the lwIP stack.
 */
int VBoxNetDhcpd::ifInput(void *pvFrame, uint32_t cbFrame)
{
    if (pvFrame == NULL)
        return VERR_INVALID_PARAMETER;

    if (   cbFrame <= sizeof(RTNETETHERHDR)
        || cbFrame > UINT16_MAX - ETH_PAD_SIZE)
        return VERR_INVALID_PARAMETER;

    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)cbFrame + ETH_PAD_SIZE, PBUF_POOL);
    if (RT_UNLIKELY(p == NULL))
        return VERR_NO_MEMORY;

    /*
     * The code below is inlined version of:
     *
     *   pbuf_header(p, -ETH_PAD_SIZE); // hide padding
     *   pbuf_take(p, pvFrame, cbFrame);
     *   pbuf_header(p, ETH_PAD_SIZE);  // reveal padding
     */
    struct pbuf *q = p;
    uint8_t *pbChunk = (uint8_t *)pvFrame;
    do
    {
        uint8_t *payload = (uint8_t *)q->payload;
        size_t len = q->len;

#if ETH_PAD_SIZE
        if (RT_LIKELY(q == p))  /* single pbuf is large enough */
        {
            payload += ETH_PAD_SIZE;
            len -= ETH_PAD_SIZE;
        }
#endif
        memcpy(payload, pbChunk, len);
        pbChunk += len;
        q = q->next;
    } while (RT_UNLIKELY(q != NULL));

    m_LwipNetif.input(p, &m_LwipNetif);
    return VINF_SUCCESS;
}


/**
 * Got a frame from the lwIP stack, feed it to the internal network.
 */
err_t VBoxNetDhcpd::netifLinkOutput(pbuf *pPBuf)
{
    if (pPBuf->tot_len < sizeof(struct eth_hdr)) /* includes ETH_PAD_SIZE */
        return ERR_ARG;

    PINTNETHDR pHdr;
    void *pvFrame;
    u16_t cbFrame = pPBuf->tot_len - ETH_PAD_SIZE;
    int rc = IntNetRingAllocateFrame(&m_pIfBuf->Send, cbFrame, &pHdr, &pvFrame);
    if (RT_FAILURE(rc))
        return ERR_MEM;

    pbuf_copy_partial(pPBuf, pvFrame, cbFrame, ETH_PAD_SIZE);
    IntNetRingCommitFrameEx(&m_pIfBuf->Send, pHdr, cbFrame);

    ifFlush();
    return ERR_OK;
}


int VBoxNetDhcpd::ifFlush()
{
    INTNETIFSENDREQ SendReq;

    SendReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    SendReq.Hdr.cbReq = sizeof(SendReq);
    SendReq.pSession = m_pSession;

    SendReq.hIf = m_hIf;

    return CALL_VMMR0(VMMR0_DO_INTNET_IF_SEND, SendReq);
}


int VBoxNetDhcpd::ifClose()
{
    if (m_hIf == INTNET_HANDLE_INVALID)
        return VINF_SUCCESS;

    INTNETIFCLOSEREQ CloseReq;

    CloseReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    CloseReq.Hdr.cbReq = sizeof(CloseReq);
    CloseReq.pSession = m_pSession;

    CloseReq.hIf = m_hIf;

    m_hIf = INTNET_HANDLE_INVALID;
    m_pIfBuf = NULL;

    CALL_VMMR0(VMMR0_DO_INTNET_IF_CLOSE, CloseReq);
    return VINF_SUCCESS;
}


/* static */ DECLCALLBACK(void) VBoxNetDhcpd::lwipInitCB(void *pvArg)
{
    AssertPtrReturnVoid(pvArg);

    VBoxNetDhcpd *self = static_cast<VBoxNetDhcpd *>(pvArg);
    self->lwipInit();
}


/* static */ err_t VBoxNetDhcpd::netifInitCB(netif *pNetif)
{
    AssertPtrReturn(pNetif, ERR_ARG);

    VBoxNetDhcpd *self = static_cast<VBoxNetDhcpd *>(pNetif->state);
    return self->netifInit(pNetif);
}


/* static */ err_t VBoxNetDhcpd::netifLinkOutputCB(netif *pNetif, pbuf *pPBuf)
{
    AssertPtrReturn(pNetif, ERR_ARG);
    AssertPtrReturn(pPBuf, ERR_ARG);

    VBoxNetDhcpd *self = static_cast<VBoxNetDhcpd *>(pNetif->state);
    AssertPtrReturn(self, ERR_IF);

    return self->netifLinkOutput(pPBuf);
}


/* static */ void VBoxNetDhcpd::dhcp4RecvCB(void *arg, struct udp_pcb *pcb,
                                            struct pbuf *p,
                                            ip_addr_t *addr, u16_t port)
{
    AssertPtrReturnVoid(arg);

    VBoxNetDhcpd *self = static_cast<VBoxNetDhcpd *>(arg);
    self->dhcp4Recv(pcb, p, addr, port);
    pbuf_free(p);
}





int VBoxNetDhcpd::main(int argc, char **argv)
{
    /*
     * Register string format types.
     */
    ClientId::registerFormat();
    Binding::registerFormat();

    /*
     * Parse the command line into a configuration object.
     */
    m_Config = Config::create(argc, argv);
    if (m_Config == NULL)
        return VERR_GENERAL_FAILURE;

    /*
     * Initialize the server.
     */
    int rc = m_server.init(m_Config);
    if (RT_SUCCESS(rc))
    {
        /* connect to the intnet */
        rc = ifInit(m_Config->getNetwork(), m_Config->getTrunk(), m_Config->getTrunkType());
        if (RT_SUCCESS(rc))
        {
            /* setup lwip */
            rc = vboxLwipCoreInitialize(lwipInitCB, this);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Pump packets more or less for ever.
                 */
                ifPump();
            }
            else
                DHCP_LOG_MSG_ERROR(("Terminating - vboxLwipCoreInitialize failed: %Rrc\n", rc));
        }
        else
            DHCP_LOG_MSG_ERROR(("Terminating - ifInit failed: %Rrc\n", rc));
    }
    else
        DHCP_LOG_MSG_ERROR(("Terminating - Dhcpd::init failed: %Rrc\n", rc));
    return rc;
}


void VBoxNetDhcpd::lwipInit()
{
    err_t error;

    ip_addr_t addr, mask;
    ip4_addr_set_u32(&addr, m_Config->getIPv4Address().u);
    ip4_addr_set_u32(&mask, m_Config->getIPv4Netmask().u);

    netif *pNetif = netif_add(&m_LwipNetif,
                              &addr, &mask,
                              IP_ADDR_ANY,               /* gateway */
                              this,                      /* state */
                              VBoxNetDhcpd::netifInitCB, /* netif_init_fn */
                              tcpip_input);              /* netif_input_fn */
    if (pNetif == NULL)
        return;

    netif_set_up(pNetif);
    netif_set_link_up(pNetif);

    m_Dhcp4Pcb = udp_new();
    if (RT_UNLIKELY(m_Dhcp4Pcb == NULL))
        return;                 /* XXX? */

    ip_set_option(m_Dhcp4Pcb, SOF_BROADCAST);
    udp_recv(m_Dhcp4Pcb, dhcp4RecvCB, this);

    error = udp_bind(m_Dhcp4Pcb, IP_ADDR_ANY, RTNETIPV4_PORT_BOOTPS);
    if (error != ERR_OK)
    {
        udp_remove(m_Dhcp4Pcb);
        m_Dhcp4Pcb = NULL;
        return;                 /* XXX? */
    }
}


err_t VBoxNetDhcpd::netifInit(netif *pNetif)
{
    pNetif->hwaddr_len = sizeof(RTMAC);
    memcpy(pNetif->hwaddr, &m_Config->getMacAddress(), sizeof(RTMAC));

    pNetif->mtu = 1500;

    pNetif->flags = NETIF_FLAG_BROADCAST
                  | NETIF_FLAG_ETHARP
                  | NETIF_FLAG_ETHERNET;

    pNetif->linkoutput = netifLinkOutputCB;
    pNetif->output = etharp_output;

    netif_set_default(pNetif);
    return ERR_OK;
}


void VBoxNetDhcpd::dhcp4Recv(struct udp_pcb *pcb, struct pbuf *p,
                             ip_addr_t *addr, u16_t port)
{
    RT_NOREF(pcb, addr, port);

    if (RT_UNLIKELY(p->next != NULL))
        return;                 /* XXX: we want it in one chunk */

    bool broadcasted = ip_addr_cmp(ip_current_dest_addr(), &ip_addr_broadcast)
                    || ip_addr_cmp(ip_current_dest_addr(), &ip_addr_any);

    try
    {
        DhcpClientMessage *msgIn = DhcpClientMessage::parse(broadcasted, p->payload, p->len);
        if (msgIn == NULL)
            return;

        std::unique_ptr<DhcpClientMessage> autoFreeMsgIn(msgIn);

        DhcpServerMessage *msgOut = m_server.process(*msgIn);
        if (msgOut == NULL)
            return;

        std::unique_ptr<DhcpServerMessage> autoFreeMsgOut(msgOut);

        ip_addr_t dst = { msgOut->dst().u };
        if (ip_addr_cmp(&dst, &ip_addr_any))
            ip_addr_copy(dst, ip_addr_broadcast);

        octets_t data;
        int rc = msgOut->encode(data);
        if (RT_FAILURE(rc))
            return;

        unique_ptr_pbuf q ( pbuf_alloc(PBUF_RAW, (u16_t)data.size(), PBUF_RAM) );
        if (!q)
            return;

        err_t error = pbuf_take(q.get(), &data.front(), (u16_t)data.size());
        if (error != ERR_OK)
            return;

        error = udp_sendto(pcb, q.get(), &dst, RTNETIPV4_PORT_BOOTPC);
        if (error != ERR_OK)
            return;
    }
    catch (std::bad_alloc &)
    {
        LogRel(("VBoxNetDhcpd::dhcp4Recv: Caught std::bad_alloc!\n"));
    }
}




/*
 * Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv)
{
    VBoxNetDhcpd Dhcpd;
    int rc = Dhcpd.main(argc, argv);

    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


#ifndef VBOX_WITH_HARDENING

int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_SUPLIB);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    return TrustedMain(argc, argv);
}


# ifdef RT_OS_WINDOWS
/** (We don't want a console usually.) */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    RT_NOREF(hInstance, hPrevInstance, lpCmdLine, nCmdShow);

    return main(__argc, __argv);
}
# endif /* RT_OS_WINDOWS */

#endif /* !VBOX_WITH_HARDENING */
