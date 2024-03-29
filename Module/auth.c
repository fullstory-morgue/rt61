/***************************************************************************
 * RT2400/RT2500 SourceForge Project - http://rt2x00.serialmonkey.com      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                         *
 *   Licensed under the GNU GPL                                            *
 *   Original code supplied under license from RaLink Inc, 2005.           *
 ***************************************************************************/

/***************************************************************************
 *      Module Name: auth.c
 *
 *      Abstract:
 *
 *      Revision History:
 *      Who             When            What
 *      --------        -----------     -----------------------------
 *      John            3rd  Sep 04     Porting from RT2500
 *      GertjanW        21st Jan 06     Baseline code
 ***************************************************************************/

#include "rt_config.h"


/*
    ==========================================================================
    Description:
    ==========================================================================
 */
static VOID InvalidStateWhenAuth(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	USHORT Status;
	DBGPRINT(RT_DEBUG_TRACE,
		 "AUTH - InvalidStateWhenAuth (state=%d), reset AUTH state machine\n",
		 pAd->Mlme.AuthMachine.CurrState);
	pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
	Status = MLME_STATE_MACHINE_REJECT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_AUTH_CONF, 2, &Status);
}

/*
    ==========================================================================
    Description:
        authenticate state machine init, including state transition and timer init
    Parameters:
        Sm - pointer to the auth state machine
    Note:
        The state machine looks like this

                        AUTH_REQ_IDLE           AUTH_WAIT_SEQ2                   AUTH_WAIT_SEQ4
    MT2_MLME_AUTH_REQ   mlme_auth_req_action    invalid_state_when_auth          invalid_state_when_auth
    MT2_MLME_DEAUTH_REQ mlme_deauth_req_action  mlme_deauth_req_action           mlme_deauth_req_action
    MT2_CLS2ERR         cls2err_action          cls2err_action                   cls2err_action
    MT2_PEER_AUTH_EVEN  drop                    peer_auth_even_at_seq2_action    peer_auth_even_at_seq4_action
    MT2_AUTH_TIMEOUT    Drop                    auth_timeout_action              auth_timeout_action
    ==========================================================================
 */


/*
    ==========================================================================
    Description:
    ==========================================================================
 */
static VOID AuthTimeoutAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	USHORT Status;

	DBGPRINT(RT_DEBUG_TRACE, "AUTH - AuthTimeoutAction\n");
	pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
	Status = MLME_REJ_TIMEOUT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_AUTH_CONF, 2, &Status);
}

/*
    ==========================================================================
    Description:
        function to be executed at timer thread when auth timer expires
    ==========================================================================
 */
static VOID AuthTimeout(IN unsigned long data)
{
	RTMP_ADAPTER *pAd = (RTMP_ADAPTER *) data;

	DBGPRINT(RT_DEBUG_TRACE, "AUTH - AuthTimeout\n");

	pAd->BbpTuning.R17LowerUpperSelect =
	    !pAd->BbpTuning.R17LowerUpperSelect;

	MlmeEnqueue(pAd, AUTH_STATE_MACHINE, MT2_AUTH_TIMEOUT, 0, NULL);
	MlmeHandler(pAd);
}

/*
    ==========================================================================
    Description:
    ==========================================================================
 */
static VOID MlmeAuthReqAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	UCHAR Addr[ETH_ALEN];
	USHORT Alg, Seq, Status;
	ULONG Timeout;
	HEADER_802_11 AuthHdr;
	PUCHAR pOutBuffer = NULL;
	ULONG FrameLen = 0;

	// Block all authentication request durning WPA block period
	if (pAd->PortCfg.bBlockAssoc == TRUE) {
		DBGPRINT(RT_DEBUG_TRACE,
			 "AUTH - Block Auth request durning WPA block period!\n");
		pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
		Status = MLME_STATE_MACHINE_REJECT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_AUTH_CONF, 2,
			    &Status);
	} else
	    if (MlmeAuthReqSanity
		(pAd, Elem->Msg, Elem->MsgLen, Addr, &Timeout, &Alg)) {
		// reset timer if caller isn't the timer function itself
		if (timer_pending(&pAd->MlmeAux.AuthTimer))
			del_timer_sync(&pAd->MlmeAux.AuthTimer);

		memcpy(pAd->MlmeAux.Bssid, Addr, ETH_ALEN);
		pAd->MlmeAux.Alg = Alg;

		Seq = 1;
		Status = MLME_SUCCESS;

		// allocate and send out AuthReq frame
		pOutBuffer = kmalloc(MAX_LEN_OF_MLME_BUFFER, MEM_ALLOC_FLAG);
		if (pOutBuffer == NULL) {
			DBGPRINT(RT_DEBUG_TRACE,
				 "AUTH - MlmeAuthReqAction() allocate memory failed\n");
			pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
			Status = MLME_FAIL_NO_RESOURCE;
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_AUTH_CONF,
				    2, &Status);
			return;
		}

		DBGPRINT(RT_DEBUG_TRACE,
			 "AUTH - Send AUTH request seq#1 (Alg=%d)...\n", Alg);
		MgtMacHeaderInit(pAd, &AuthHdr, SUBTYPE_AUTH, 0, Addr,
				 pAd->MlmeAux.Bssid);

		MakeOutgoingFrame(pOutBuffer, &FrameLen,
				  sizeof(HEADER_802_11), &AuthHdr,
				  2, &Alg, 2, &Seq, 2, &Status, END_OF_ARGS);

		MiniportMMRequest(pAd, pOutBuffer, FrameLen);
		kfree(pOutBuffer);

		pAd->MlmeAux.AuthTimer.expires = jiffies + AUTH_TIMEOUT;
		add_timer(&pAd->MlmeAux.AuthTimer);

		pAd->Mlme.AuthMachine.CurrState = AUTH_WAIT_SEQ2;
	} else {
		printk(KERN_ERR DRIVER_NAME
		       "AUTH - MlmeAuthReqAction() sanity check failed. BUG!!!!!\n");
		pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
		Status = MLME_INVALID_FORMAT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_AUTH_CONF, 2,
			    &Status);
	}
}

/*
    ==========================================================================
    Description:
    ==========================================================================
 */
static VOID PeerAuthRspAtSeq2Action(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	UCHAR Addr2[ETH_ALEN];
	USHORT Seq, Status, RemoteStatus, Alg;
	UCHAR ChlgText[CIPHER_TEXT_LEN];
	UCHAR CyperChlgText[CIPHER_TEXT_LEN + 8 + 8];
	UCHAR Element[2];
	HEADER_802_11 AuthHdr;
	PUCHAR pOutBuffer = NULL;
	ULONG FrameLen = 0;
	USHORT Status2;

	if (PeerAuthSanity
	    (pAd, Elem->Msg, Elem->MsgLen, Addr2, &Alg, &Seq, &Status,
	     ChlgText)) {
		if ((memcmp(&pAd->MlmeAux.Bssid, Addr2, ETH_ALEN) == 0)
		    && Seq == 2) {
			DBGPRINT(RT_DEBUG_TRACE,
				 "AUTH - Receive AUTH_RSP seq#2 to me (Alg=%d, Status=%d)\n",
				 Alg, Status);
			del_timer_sync(&pAd->MlmeAux.AuthTimer);

			if (Status == MLME_SUCCESS) {
				if (pAd->MlmeAux.Alg == Ndis802_11AuthModeOpen) {
					pAd->Mlme.AuthMachine.CurrState =
					    AUTH_REQ_IDLE;
					MlmeEnqueue(pAd,
						    MLME_CNTL_STATE_MACHINE,
						    MT2_AUTH_CONF, 2, &Status);
				} else {
					// 2. shared key, need to be challenged
					Seq++;
					RemoteStatus = MLME_SUCCESS;
					// allocate and send out AuthRsp frame
					pOutBuffer =
					    kmalloc(MAX_LEN_OF_MLME_BUFFER,
						    MEM_ALLOC_FLAG);
					if (pOutBuffer == NULL) {
						DBGPRINT(RT_DEBUG_TRACE,
							 "AUTH - PeerAuthRspAtSeq2Action() allocate memory fail\n");
						pAd->Mlme.AuthMachine.
						    CurrState = AUTH_REQ_IDLE;
						Status2 = MLME_FAIL_NO_RESOURCE;
						MlmeEnqueue(pAd,
							    MLME_CNTL_STATE_MACHINE,
							    MT2_AUTH_CONF, 2,
							    &Status2);
						return;
					}

					DBGPRINT(RT_DEBUG_TRACE,
						 "AUTH - Send AUTH request seq#3...\n");
					MgtMacHeaderInit(pAd, &AuthHdr,
							 SUBTYPE_AUTH, 0, Addr2,
							 pAd->MlmeAux.Bssid);
					AuthHdr.FC.Wep = 1;
					// Encrypt challenge text & auth information
					RTMPInitWepEngine(pAd,
							  pAd->SharedKey[pAd->
									 PortCfg.
									 DefaultKeyId].
							  Key,
							  pAd->PortCfg.
							  DefaultKeyId,
							  pAd->SharedKey[pAd->
									 PortCfg.
									 DefaultKeyId].
							  KeyLen,
							  CyperChlgText);
#ifdef BIG_ENDIAN
					Alg = SWAP16(*(USHORT *) & Alg);
					Seq = SWAP16(*(USHORT *) & Seq);
					RemoteStatus =
					    SWAP16(*(USHORT *) & RemoteStatus);
#endif
					RTMPEncryptData(pAd, (PUCHAR) & Alg,
							CyperChlgText + 4, 2);
					RTMPEncryptData(pAd, (PUCHAR) & Seq,
							CyperChlgText + 6, 2);
					RTMPEncryptData(pAd,
							(PUCHAR) & RemoteStatus,
							CyperChlgText + 8, 2);

					Element[0] = 16;
					Element[1] = 128;
					RTMPEncryptData(pAd, Element,
							CyperChlgText + 10, 2);
					RTMPEncryptData(pAd, ChlgText,
							CyperChlgText + 12,
							128);
					RTMPSetICV(pAd, CyperChlgText + 140);

					MakeOutgoingFrame(pOutBuffer, &FrameLen,
							  sizeof(HEADER_802_11),
							  &AuthHdr,
							  CIPHER_TEXT_LEN + 16,
							  CyperChlgText,
							  END_OF_ARGS);

					MiniportMMRequest(pAd, pOutBuffer,
							  FrameLen);
					kfree(pOutBuffer);

					pAd->MlmeAux.AuthTimer.expires =
					    jiffies + AUTH_TIMEOUT;
					add_timer(&pAd->MlmeAux.AuthTimer);
					pAd->Mlme.AuthMachine.CurrState =
					    AUTH_WAIT_SEQ4;
				}
			} else {
				pAd->PortCfg.AuthFailReason = Status;
				memcpy(pAd->PortCfg.AuthFailSta, Addr2,
				       ETH_ALEN);
				pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
				MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE,
					    MT2_AUTH_CONF, 2, &Status);
			}
		}
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 "AUTH - PeerAuthSanity() sanity check fail\n");
	}
}

/*
    ==========================================================================
    Description:
    ==========================================================================
 */
static VOID PeerAuthRspAtSeq4Action(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	UCHAR Addr2[ETH_ALEN];
	USHORT Alg, Seq, Status;
	CHAR ChlgText[CIPHER_TEXT_LEN];

	if (PeerAuthSanity
	    (pAd, Elem->Msg, Elem->MsgLen, Addr2, &Alg, &Seq, &Status,
	     ChlgText)) {
		if ((memcmp(&(pAd->MlmeAux.Bssid), Addr2, ETH_ALEN) == 0)
		    && Seq == 4) {
			DBGPRINT(RT_DEBUG_TRACE,
				 "AUTH - Receive AUTH_RSP seq#4 to me\n");
			del_timer_sync(&pAd->MlmeAux.AuthTimer);

			if (Status != MLME_SUCCESS) {
				pAd->PortCfg.AuthFailReason = Status;
				memcpy(pAd->PortCfg.AuthFailSta, Addr2,
				       ETH_ALEN);
			}

			pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_AUTH_CONF,
				    2, &Status);
		}
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 "AUTH - PeerAuthRspAtSeq4Action() sanity check fail\n");
	}
}

/*
    ==========================================================================
    Description:
    ==========================================================================
 */
 #if 0
static VOID MlmeDeauthReqAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	MLME_DEAUTH_REQ_STRUCT *pInfo;
	HEADER_802_11 DeauthHdr;
	PUCHAR pOutBuffer = NULL;
	ULONG FrameLen = 0;
	USHORT Status;

	pInfo = (MLME_DEAUTH_REQ_STRUCT *) Elem->Msg;

	// allocate and send out DeauthReq frame
	pOutBuffer = kmalloc(MAX_LEN_OF_MLME_BUFFER, MEM_ALLOC_FLAG);
	if (pOutBuffer == NULL) {
		DBGPRINT(RT_DEBUG_TRACE,
			 "AUTH - MlmeDeauthReqAction() allocate memory fail\n");
		pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
		Status = MLME_FAIL_NO_RESOURCE;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_DEAUTH_CONF, 2,
			    &Status);
		return;
	}

	DBGPRINT(RT_DEBUG_TRACE, "AUTH - Send DE-AUTH request (Reason=%d)...\n",
		 pInfo->Reason);
	MgtMacHeaderInit(pAd, &DeauthHdr, SUBTYPE_DEAUTH, 0, pInfo->Addr,
			 pAd->MlmeAux.Bssid);

	MakeOutgoingFrame(pOutBuffer, &FrameLen,
			  sizeof(HEADER_802_11), &DeauthHdr,
			  2, &pInfo->Reason, END_OF_ARGS);

	MiniportMMRequest(pAd, pOutBuffer, FrameLen);
	kfree(pOutBuffer);

	pAd->PortCfg.DeauthReason = pInfo->Reason;
	memcpy(pAd->PortCfg.DeauthSta, pInfo->Addr, ETH_ALEN);
	pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
	Status = MLME_SUCCESS;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_DEAUTH_CONF, 2, &Status);
}

/*
    ==========================================================================
    Description:
        Some STA/AP
    Note:
        This action should never trigger AUTH state transition, therefore we
        separate it from AUTH state machine, and make it as a standalone service
    ==========================================================================
 */
static VOID Cls2errAction(IN PRTMP_ADAPTER pAd, IN PUCHAR pAddr)
{
	HEADER_802_11 DeauthHdr;
	PUCHAR pOutBuffer = NULL;
	ULONG FrameLen = 0;
	USHORT Reason = REASON_CLS2ERR;

	// allocate memory
	pOutBuffer = kmalloc(MAX_LEN_OF_MLME_BUFFER, MEM_ALLOC_FLAG);
	if (pOutBuffer == NULL)
		return;

	DBGPRINT(RT_DEBUG_TRACE,
		 "AUTH - Class 2 error, Send DEAUTH frame...\n");
	MgtMacHeaderInit(pAd, &DeauthHdr, SUBTYPE_DEAUTH, 0, pAddr,
			 pAd->MlmeAux.Bssid);
	MakeOutgoingFrame(pOutBuffer, &FrameLen, sizeof(HEADER_802_11),
			  &DeauthHdr, 2, &Reason, END_OF_ARGS);
	MiniportMMRequest(pAd, pOutBuffer, FrameLen);
	kfree(pOutBuffer);

	pAd->PortCfg.DeauthReason = Reason;
	memcpy(pAd->PortCfg.DeauthSta, pAddr, ETH_ALEN);
}
#endif

VOID AuthStateMachineInit(IN PRTMP_ADAPTER pAd,
			  IN PSTATE_MACHINE Sm, OUT STATE_MACHINE_FUNC Trans[])
{
	StateMachineInit(Sm, (STATE_MACHINE_FUNC *) Trans, MAX_AUTH_STATE,
			 MAX_AUTH_MSG, (STATE_MACHINE_FUNC) Drop, AUTH_REQ_IDLE,
			 AUTH_MACHINE_BASE);

	// the first column
	StateMachineSetAction(Sm, AUTH_REQ_IDLE, MT2_MLME_AUTH_REQ,
			      (STATE_MACHINE_FUNC) MlmeAuthReqAction);

	// the second column
	StateMachineSetAction(Sm, AUTH_WAIT_SEQ2, MT2_MLME_AUTH_REQ,
			      (STATE_MACHINE_FUNC) InvalidStateWhenAuth);
	StateMachineSetAction(Sm, AUTH_WAIT_SEQ2, MT2_PEER_AUTH_EVEN,
			      (STATE_MACHINE_FUNC) PeerAuthRspAtSeq2Action);
	StateMachineSetAction(Sm, AUTH_WAIT_SEQ2, MT2_AUTH_TIMEOUT,
			      (STATE_MACHINE_FUNC) AuthTimeoutAction);

	// the third column
	StateMachineSetAction(Sm, AUTH_WAIT_SEQ4, MT2_MLME_AUTH_REQ,
			      (STATE_MACHINE_FUNC) InvalidStateWhenAuth);
	StateMachineSetAction(Sm, AUTH_WAIT_SEQ4, MT2_PEER_AUTH_EVEN,
			      (STATE_MACHINE_FUNC) PeerAuthRspAtSeq4Action);
	StateMachineSetAction(Sm, AUTH_WAIT_SEQ4, MT2_AUTH_TIMEOUT,
			      (STATE_MACHINE_FUNC) AuthTimeoutAction);

	// timer init
	init_timer(&pAd->MlmeAux.AuthTimer);
	pAd->MlmeAux.AuthTimer.data = (unsigned long)pAd;
	pAd->MlmeAux.AuthTimer.function = &AuthTimeout;

}

