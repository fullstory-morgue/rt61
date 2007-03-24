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
 *      Module Name: auth_rsp.c
 *
 *      Abstract:
 *
 *      Revision History:
 *      Who             When            What
 *      --------        -----------     -----------------------------
 *      GertjanW        21st Jan 06     Baseline code
 ***************************************************************************/

#include "rt_config.h"

/*
    ==========================================================================
    Description:
        authentication state machine init procedure
    Parameters:
        Sm - the state machine
    Note:
        the state machine looks like the following

                                    AUTH_RSP_IDLE                       AUTH_RSP_WAIT_CHAL
    MT2_AUTH_CHALLENGE_TIMEOUT      auth_rsp_challenge_timeout_action   auth_rsp_challenge_timeout_action
    MT2_PEER_AUTH_ODD               peer_auth_at_auth_rsp_idle_action   peer_auth_at_auth_rsp_wait_action
    MT2_PEER_DEAUTH                 peer_deauth_action                  peer_deauth_action
    ==========================================================================
 */
VOID AuthRspStateMachineInit(IN PRTMP_ADAPTER pAd,
			     IN PSTATE_MACHINE Sm,
			     IN STATE_MACHINE_FUNC Trans[])
{
	unsigned long Now;

	StateMachineInit(Sm, (STATE_MACHINE_FUNC *) Trans, MAX_AUTH_RSP_STATE,
			 MAX_AUTH_RSP_MSG, (STATE_MACHINE_FUNC) Drop,
			 AUTH_RSP_IDLE, AUTH_RSP_MACHINE_BASE);

	// column 1
	StateMachineSetAction(Sm, AUTH_RSP_IDLE, MT2_PEER_DEAUTH,
			      (STATE_MACHINE_FUNC) PeerDeauthAction);

	// column 2
	StateMachineSetAction(Sm, AUTH_RSP_WAIT_CHAL, MT2_PEER_DEAUTH,
			      (STATE_MACHINE_FUNC) PeerDeauthAction);

	// initialize the random number generator
	Now = jiffies;
	LfsrInit(pAd, Now);
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
VOID PeerAuthSimpleRspGenAndSend(IN PRTMP_ADAPTER pAd,
				 IN PHEADER_802_11 pHdr80211,
				 IN USHORT Alg,
				 IN USHORT Seq,
				 IN USHORT Reason, IN USHORT Status)
{
	HEADER_802_11 AuthHdr;
	UINT FrameLen = 0;
	PUCHAR pOutBuffer = NULL;

	// allocate and send out Auth_Rsp frame
	pOutBuffer = kmalloc(MAX_LEN_OF_MLME_BUFFER, MEM_ALLOC_FLAG);
	if (pOutBuffer == NULL)
		return;

	if (Reason == MLME_SUCCESS) {
		DBGPRINT(RT_DEBUG_TRACE, "Send AUTH response (seq#2)...\n");
		MgtMacHeaderInit(pAd, &AuthHdr, SUBTYPE_AUTH, 0,
				 pHdr80211->Addr2, pAd->MlmeAux.Bssid);
		MakeOutgoingFrame(pOutBuffer, &FrameLen, sizeof(HEADER_802_11),
				  &AuthHdr, 2, &Alg, 2, &Seq, 2, &Reason,
				  END_OF_ARGS);
		MiniportMMRequest(pAd, pOutBuffer, FrameLen);
	} else {
		kfree(pOutBuffer);
		DBGPRINT(RT_DEBUG_TRACE, "Peer AUTH fail...\n");
	}
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
VOID PeerDeauthAction(IN PRTMP_ADAPTER pAd, IN PMLME_QUEUE_ELEM Elem)
{
	UCHAR Addr2[ETH_ALEN];
	USHORT Reason;

	if (PeerDeauthSanity(pAd, Elem->Msg, Elem->MsgLen, Addr2, &Reason)) {
		if (INFRA_ON(pAd)
		    && (memcmp(Addr2, &pAd->PortCfg.Bssid, ETH_ALEN) == 0)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 "AUTH_RSP - receive DE-AUTH from our AP, reason = %d\n",
				 Reason);
			LinkDown(pAd, TRUE);
		}
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 "AUTH_RSP - PeerDeauthAction() sanity check fail\n");
	}
}
