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
 *      Module Name: connect.c
 *
 *      Abstract:
 *
 *      Revision History:
 *      Who             When            What
 *      --------        -----------     -----------------------------
 *      John            8th  Aug 04     Major modification from RT2560
 *      GertjanW        21st Jan 06     Baseline code
 *      Ivo (rt2400)    28th Jan 06     Timing ESSID set
 ***************************************************************************/

#include "rt_config.h"

UCHAR CipherSuiteWpaNoneTkip[] = {
	0x00, 0x50, 0xf2, 0x01,	// oui
	0x01, 0x00,		// Version
	0x00, 0x50, 0xf2, 0x02,	// Multicast
	0x01, 0x00,		// Number of unicast
	0x00, 0x50, 0xf2, 0x02,	// unicast
	0x01, 0x00,		// number of authentication method
	0x00, 0x50, 0xf2, 0x00	// authentication
};
UCHAR CipherSuiteWpaNoneTkipLen =
    (sizeof(CipherSuiteWpaNoneTkip) / sizeof(UCHAR));

UCHAR CipherSuiteWpaNoneAes[] = {
	0x00, 0x50, 0xf2, 0x01,	// oui
	0x01, 0x00,		// Version
	0x00, 0x50, 0xf2, 0x04,	// Multicast
	0x01, 0x00,		// Number of unicast
	0x00, 0x50, 0xf2, 0x04,	// unicast
	0x01, 0x00,		// number of authentication method
	0x00, 0x50, 0xf2, 0x00	// authentication
};
UCHAR CipherSuiteWpaNoneAesLen =
    (sizeof(CipherSuiteWpaNoneAes) / sizeof(UCHAR));

// The following MACRO is called after 1. starting an new IBSS, 2. succesfully JOIN an IBSS,
// or 3. succesfully ASSOCIATE to a BSS, 4. successfully RE_ASSOCIATE to a BSS
// All settings successfuly negotiated furing MLME state machines become final settings
// and are copied to pAd->ActiveCfg
#define COPY_SETTINGS_FROM_MLME_AUX_TO_ACTIVE_CFG(_pAd)                                 \
{                                                                                       \
    _pAd->PortCfg.SsidLen = _pAd->MlmeAux.SsidLen;                                    \
    memcpy(_pAd->PortCfg.Ssid, _pAd->MlmeAux.Ssid, _pAd->MlmeAux.SsidLen);    \
    memcpy(_pAd->PortCfg.Bssid, _pAd->MlmeAux.Bssid, ETH_ALEN);                       \
    _pAd->PortCfg.Channel = _pAd->MlmeAux.Channel;                                    \
    _pAd->ActiveCfg.Aid = _pAd->MlmeAux.Aid;                                            \
    _pAd->ActiveCfg.AtimWin = _pAd->MlmeAux.AtimWin;                                    \
    _pAd->ActiveCfg.CapabilityInfo = _pAd->MlmeAux.CapabilityInfo;                      \
    _pAd->PortCfg.BeaconPeriod = _pAd->MlmeAux.BeaconPeriod;                          \
    _pAd->ActiveCfg.CfpMaxDuration = _pAd->MlmeAux.CfpMaxDuration;                      \
    _pAd->ActiveCfg.CfpPeriod = _pAd->MlmeAux.CfpPeriod;                                \
    _pAd->ActiveCfg.SupRateLen = _pAd->MlmeAux.SupRateLen;                              \
    memcpy(_pAd->ActiveCfg.SupRate, _pAd->MlmeAux.SupRate, _pAd->MlmeAux.SupRateLen);   \
    _pAd->ActiveCfg.ExtRateLen = _pAd->MlmeAux.ExtRateLen;                                      \
    memcpy(_pAd->ActiveCfg.ExtRate, _pAd->MlmeAux.ExtRate, _pAd->MlmeAux.ExtRateLen);   \
    memcpy(&_pAd->PortCfg.APEdcaParm, &_pAd->MlmeAux.APEdcaParm, sizeof(EDCA_PARM));  \
    memcpy(&_pAd->PortCfg.APQosCapability, &_pAd->MlmeAux.APQosCapability, sizeof(QOS_CAPABILITY_PARM));  \
    memcpy(&_pAd->PortCfg.APQbssLoad, &_pAd->MlmeAux.APQbssLoad, sizeof(QBSS_LOAD_PARM)); \
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
static VOID JoinParmFill(IN PRTMP_ADAPTER pAd,
		  IN OUT MLME_JOIN_REQ_STRUCT * JoinReq, IN ULONG BssIdx)
{
	JoinReq->BssIdx = BssIdx;
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
static VOID AssocParmFill(IN PRTMP_ADAPTER pAd,
		   IN OUT MLME_ASSOC_REQ_STRUCT * AssocReq,
		   IN PUCHAR pAddr,
		   IN USHORT CapabilityInfo,
		   IN ULONG Timeout, IN USHORT ListenIntv)
{
	memcpy(AssocReq->Addr, pAddr, ETH_ALEN);
	// Add mask to support 802.11b mode only
	AssocReq->CapabilityInfo = CapabilityInfo & SUPPORTED_CAPABILITY_INFO;	// not cf-pollable, not cf-poll-request
	AssocReq->Timeout = Timeout;
	AssocReq->ListenIntv = ListenIntv;
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
VOID ScanParmFill(IN PRTMP_ADAPTER pAd,
		  IN OUT MLME_SCAN_REQ_STRUCT * ScanReq,
		  IN CHAR Ssid[],
		  IN UCHAR SsidLen, IN UCHAR BssType, IN UCHAR ScanType)
{
	ScanReq->SsidLen = SsidLen;
	memcpy(ScanReq->Ssid, Ssid, SsidLen);
	ScanReq->BssType = BssType;
	ScanReq->ScanType = ScanType;
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
VOID DisassocParmFill(IN PRTMP_ADAPTER pAd,
		      IN OUT MLME_DISASSOC_REQ_STRUCT * DisassocReq,
		      IN PUCHAR pAddr, IN USHORT Reason)
{
	memcpy(DisassocReq->Addr, pAddr, ETH_ALEN);
	DisassocReq->Reason = Reason;
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
static VOID StartParmFill(IN PRTMP_ADAPTER pAd,
		   IN OUT MLME_START_REQ_STRUCT * StartReq,
		   IN CHAR Ssid[], IN UCHAR SsidLen)
{
	memcpy(StartReq->Ssid, Ssid, SsidLen);
	StartReq->SsidLen = SsidLen;
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
static VOID AuthParmFill(IN PRTMP_ADAPTER pAd,
		  IN OUT MLME_AUTH_REQ_STRUCT * AuthReq,
		  IN PUCHAR pAddr, IN USHORT Alg)
{
	memcpy(AuthReq->Addr, pAddr, ETH_ALEN);
	AuthReq->Alg = Alg;
	AuthReq->Timeout = AUTH_TIMEOUT;
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
static VOID IterateOnBssTab(IN PRTMP_ADAPTER pAd)
{
	MLME_START_REQ_STRUCT StartReq;
	MLME_JOIN_REQ_STRUCT JoinReq;
	ULONG BssIdx;

	BssIdx = pAd->MlmeAux.BssIdx;
	if (BssIdx < pAd->MlmeAux.SsidBssTab.BssNr) {
		// Check cipher suite, AP must have more secured cipher than station setting
		// Set the Pairwise and Group cipher to match the intended AP setting
		// We can only connect to AP with less secured cipher setting
		if ((pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPA)
		    || (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPAPSK)) {
			pAd->PortCfg.GroupCipher =
			    pAd->MlmeAux.SsidBssTab.BssEntry[BssIdx].WPA.
			    GroupCipher;

			if (pAd->PortCfg.WepStatus ==
			    pAd->MlmeAux.SsidBssTab.BssEntry[BssIdx].WPA.
			    PairCipher)
				pAd->PortCfg.PairCipher =
				    pAd->MlmeAux.SsidBssTab.BssEntry[BssIdx].
				    WPA.PairCipher;
			else if (pAd->MlmeAux.SsidBssTab.BssEntry[BssIdx].WPA.
				 PairCipherAux != Ndis802_11WEPDisabled)
				pAd->PortCfg.PairCipher =
				    pAd->MlmeAux.SsidBssTab.BssEntry[BssIdx].
				    WPA.PairCipherAux;
			else	// There is no PairCipher Aux, downgrade our capability to TKIP
				pAd->PortCfg.PairCipher =
				    Ndis802_11Encryption2Enabled;
		} else if ((pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPA2)
			   || (pAd->PortCfg.AuthMode ==
			       Ndis802_11AuthModeWPA2PSK)) {
			pAd->PortCfg.GroupCipher =
			    pAd->MlmeAux.SsidBssTab.BssEntry[BssIdx].WPA2.
			    GroupCipher;

			if (pAd->PortCfg.WepStatus ==
			    pAd->MlmeAux.SsidBssTab.BssEntry[BssIdx].WPA2.
			    PairCipher)
				pAd->PortCfg.PairCipher =
				    pAd->MlmeAux.SsidBssTab.BssEntry[BssIdx].
				    WPA2.PairCipher;
			else if (pAd->MlmeAux.SsidBssTab.BssEntry[BssIdx].WPA2.
				 PairCipherAux != Ndis802_11WEPDisabled)
				pAd->PortCfg.PairCipher =
				    pAd->MlmeAux.SsidBssTab.BssEntry[BssIdx].
				    WPA2.PairCipherAux;
			else	// There is no PairCipher Aux, downgrade our capability to TKIP
				pAd->PortCfg.PairCipher =
				    Ndis802_11Encryption2Enabled;

			// RSN capability
			pAd->PortCfg.RsnCapability =
			    pAd->MlmeAux.SsidBssTab.BssEntry[BssIdx].WPA2.
			    RsnCapability;
		}
		// Set Mix cipher flag
		if (pAd->PortCfg.PairCipher != pAd->PortCfg.GroupCipher)
			pAd->PortCfg.bMixCipher = TRUE;

		DBGPRINT(RT_DEBUG_TRACE, "CNTL - iterate BSS %d of %d\n",
			 BssIdx, pAd->MlmeAux.SsidBssTab.BssNr);
		JoinParmFill(pAd, &JoinReq, BssIdx);
		MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_JOIN_REQ,
			    sizeof(MLME_JOIN_REQ_STRUCT), &JoinReq);
		pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_JOIN;
	} else if (pAd->PortCfg.BssType == BSS_ADHOC) {
		DBGPRINT(RT_DEBUG_TRACE,
			 "CNTL - All BSS fail; start a new ADHOC (Ssid=%s)...\n",
			 pAd->MlmeAux.Ssid);
		StartParmFill(pAd, &StartReq, pAd->MlmeAux.Ssid,
			      pAd->MlmeAux.SsidLen);
		MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_START_REQ,
			    sizeof(MLME_START_REQ_STRUCT), &StartReq);
		pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_START;
	} else			// no more BSS
	{
		DBGPRINT(RT_DEBUG_TRACE,
			 "CNTL - All roaming failed, stay @ ch #%d\n",
			 pAd->PortCfg.Channel);
		AsicSwitchChannel(pAd, pAd->PortCfg.Channel);
		AsicLockChannel(pAd, pAd->PortCfg.Channel);
		pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;
	}
}

// for re-association only
static VOID IterateOnBssTab2(IN PRTMP_ADAPTER pAd)
{
	MLME_REASSOC_REQ_STRUCT ReassocReq;
	ULONG BssIdx;
	BSS_ENTRY *pBss;

	BssIdx = pAd->MlmeAux.RoamIdx;
	pBss = &pAd->MlmeAux.RoamTab.BssEntry[BssIdx];

	if (BssIdx < pAd->MlmeAux.RoamTab.BssNr) {
		DBGPRINT(RT_DEBUG_TRACE, "CNTL - iterate BSS %d of %d\n",
			 BssIdx, pAd->MlmeAux.RoamTab.BssNr);

		AsicSwitchChannel(pAd, pBss->Channel);
		AsicLockChannel(pAd, pBss->Channel);

		// reassociate message has the same structure as associate message
		AssocParmFill(pAd, &ReassocReq, pBss->Bssid,
			      pBss->CapabilityInfo, ASSOC_TIMEOUT,
			      pAd->PortCfg.DefaultListenCount);
		MlmeEnqueue(pAd, ASSOC_STATE_MACHINE, MT2_MLME_REASSOC_REQ,
			    sizeof(MLME_REASSOC_REQ_STRUCT), &ReassocReq);

		pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_REASSOC;
	} else			// no more BSS
	{
		DBGPRINT(RT_DEBUG_TRACE,
			 "CNTL - All fast roaming failed, back to ch #%d\n",
			 pAd->PortCfg.Channel);
		AsicSwitchChannel(pAd, pAd->PortCfg.Channel);
		AsicLockChannel(pAd, pAd->PortCfg.Channel);
		pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;
	}
}

// Roaming is the only external request triggering CNTL state machine
// despite of other "SET OID" operation. All "SET OID" related oerations
// happen in sequence, because no other SET OID will be sent to this device
// until the the previous SET operation is complete (successful o failed).
// So, how do we quarantee this ROAMING request won't corrupt other "SET OID"?
// or been corrupted by other "SET OID"?
static VOID CntlMlmeRoamingProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	// TODO:
	// AP in different channel may show lower RSSI than actual value??
	// should we add a weighting factor to compensate it?
	DBGPRINT(RT_DEBUG_TRACE, "CNTL - Roaming in MlmeAux.RoamTab...\n");

	memcpy(&pAd->MlmeAux.SsidBssTab, &pAd->MlmeAux.RoamTab,
	       sizeof(pAd->MlmeAux.RoamTab));
	pAd->MlmeAux.SsidBssTab.BssNr = pAd->MlmeAux.RoamTab.BssNr;

	BssTableSortByRssi(&pAd->MlmeAux.SsidBssTab);
	pAd->MlmeAux.BssIdx = 0;
	IterateOnBssTab(pAd);
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
static VOID CntlWaitDisassocProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	MLME_START_REQ_STRUCT StartReq;

	if (Elem->MsgType == MT2_DISASSOC_CONF) {
		DBGPRINT(RT_DEBUG_TRACE, "CNTL - Dis-associate successful\n");
		LinkDown(pAd, FALSE);

		// case 1. no matching BSS, and user wants ADHOC, so we just start a new one
		if ((pAd->MlmeAux.SsidBssTab.BssNr == 0)
		    && (pAd->PortCfg.BssType == BSS_ADHOC)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 "CNTL - No matching BSS, start a new ADHOC (Ssid=%s)...\n",
				 pAd->MlmeAux.Ssid);
			StartParmFill(pAd, &StartReq, pAd->MlmeAux.Ssid,
				      pAd->MlmeAux.SsidLen);
			MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_START_REQ,
				    sizeof(MLME_START_REQ_STRUCT), &StartReq);
			pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_START;
		}
		// case 2. try each matched BSS
		else {
			pAd->MlmeAux.BssIdx = 0;
			IterateOnBssTab(pAd);
		}
	}
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
static VOID CntlIdleProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	MLME_DISASSOC_REQ_STRUCT DisassocReq;

	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF)) {
		if (pAd->MlmeAux.CurrReqIsFromNdis)
			pAd->MlmeAux.CurrReqIsFromNdis = FALSE;
		return;
	}

	switch (Elem->MsgType) {
	case OID_802_11_DISASSOCIATE:
		DisassocParmFill(pAd, &DisassocReq, pAd->PortCfg.Bssid,
				 REASON_DISASSOC_STA_LEAVING);
		MlmeEnqueue(pAd, ASSOC_STATE_MACHINE, MT2_MLME_DISASSOC_REQ,
			    sizeof(MLME_DISASSOC_REQ_STRUCT), &DisassocReq);
		pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_DISASSOC;
		// Set the AutoReconnectSsid to prevent it reconnect to old SSID
		// Since calling this indicate user don't want to connect to that SSID anymore.
		pAd->MlmeAux.AutoReconnectSsidLen = 32;
		memset(pAd->MlmeAux.AutoReconnectSsid, 0,
		       pAd->MlmeAux.AutoReconnectSsidLen);
		break;

	case MT2_MLME_ROAMING_REQ:
		CntlMlmeRoamingProc(pAd, Elem);
		break;

	default:
		DBGPRINT(RT_DEBUG_TRACE,
			 "CNTL - Illegal message in CntlIdleProc(MsgType=%d)\n",
			 Elem->MsgType);
		break;
	}
}

static VOID CntlOidScanProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	MLME_SCAN_REQ_STRUCT ScanReq;
	ULONG BssIdx = BSS_NOT_FOUND;
	BSS_ENTRY CurrBss;

	DBGPRINT(RT_DEBUG_INFO, "CNTL - SCAN starts\n");

	// record current BSS if network is connected.
	// 2003-2-13 do not include current IBSS if this is the only STA in this IBSS.
	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)) {
		BssIdx =
		    BssSsidTableSearch(&pAd->ScanTab, pAd->PortCfg.Bssid,
				       pAd->PortCfg.Ssid, pAd->PortCfg.SsidLen,
				       pAd->PortCfg.Channel);
		if (BssIdx != BSS_NOT_FOUND) {
			memcpy(&CurrBss, &pAd->ScanTab.BssEntry[BssIdx],
			       sizeof(BSS_ENTRY));

			// 2003-2-20 reset this RSSI to a low value but not zero. In normal case, the coming SCAN
			//     should return a correct RSSI to overwrite this. If no BEEACON received after SCAN,
			//     at least we still report a "greater than 0" RSSI since we claim it's CONNECTED.
			//CurrBss.Rssi = pAd->BbpRssiToDbmDelta - 85; // assume -85 dB
		}
	}
	// clean up previous SCAN result, add current BSS back to table if any
	BssTableInit(&pAd->ScanTab);
	if (BssIdx != BSS_NOT_FOUND) {
		// DDK Note: If the NIC is associated with a particular BSSID and SSID
		//    that are not contained in the list of BSSIDs generated by this scan, the
		//    BSSID description of the currently associated BSSID and SSID should be
		//    appended to the list of BSSIDs in the NIC's database.
		// To ensure this, we append this BSS as the first entry in SCAN result
		memcpy(&pAd->ScanTab.BssEntry[0], &CurrBss, sizeof(BSS_ENTRY));
		pAd->ScanTab.BssNr = 1;
	}

	ScanParmFill(pAd, &ScanReq, "", 0, BSS_ANY, SCAN_ACTIVE);
	MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_SCAN_REQ,
		    sizeof(MLME_SCAN_REQ_STRUCT), &ScanReq);
	pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_LIST_SCAN;
}

/*
    ==========================================================================
    Description:
        Before calling this routine, user desired SSID should already been
        recorded in PortCfg.Ssid[]
    ==========================================================================
*/
static VOID CntlOidSsidProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	PNDIS_802_11_SSID pOidSsid = (NDIS_802_11_SSID *) Elem->Msg;
	MLME_DISASSOC_REQ_STRUCT DisassocReq;
	unsigned long Now;

	// Step 1. record the desired user settings to MlmeAux
	memcpy(pAd->MlmeAux.Ssid, pOidSsid->Ssid, pOidSsid->SsidLength);
	pAd->MlmeAux.SsidLen = (UCHAR) pOidSsid->SsidLength;
	memset(pAd->MlmeAux.Bssid, 0, ETH_ALEN);
	pAd->MlmeAux.BssType = pAd->PortCfg.BssType;

	//
	// Update Reconnect Ssid, that user desired to connect.
	//
	memcpy(pAd->MlmeAux.AutoReconnectSsid, pAd->MlmeAux.Ssid,
	       pAd->MlmeAux.SsidLen);
	pAd->MlmeAux.AutoReconnectSsidLen = pAd->MlmeAux.SsidLen;
#if 0
	// if any SSID, reset its security to open/non-wep
	if ((pAd->PortCfg.BssType == BSS_INFRA) && (pAd->MlmeAux.SsidLen == 0)) {
		pAd->PortCfg.AuthMode = Ndis802_11AuthModeOpen;
		pAd->PortCfg.WepStatus = Ndis802_11EncryptionDisabled;
	}
#endif

	// step 2. find all matching BSS in the lastest SCAN result (inBssTab)
	//    & log them into MlmeAux.SsidBssTab for later-on iteration. Sort by RSSI order
	BssTableSsidSort(pAd, &pAd->MlmeAux.SsidBssTab, pAd->MlmeAux.Ssid,
			 pAd->MlmeAux.SsidLen);

	DBGPRINT(RT_DEBUG_TRACE, "CNTL - %d BSS match the desire SSID - %s\n",
		 pAd->MlmeAux.SsidBssTab.BssNr, pAd->MlmeAux.Ssid);

	Now = jiffies;

	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED) &&
	    (pAd->PortCfg.SsidLen ==
	     pAd->MlmeAux.SsidBssTab.BssEntry[0].SsidLen)
	    &&
	    (memcmp
	     (pAd->PortCfg.Ssid, pAd->MlmeAux.SsidBssTab.BssEntry[0].Ssid,
	      pAd->PortCfg.SsidLen) == 0)
	    &&
	    (memcmp
	     (pAd->PortCfg.Bssid, pAd->MlmeAux.SsidBssTab.BssEntry[0].Bssid,
	      ETH_ALEN) == 0)) {
		// Case 1. already connected with an AP who has the desired SSID
		//         with highest RSSI
		if (((pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPA) ||
		     (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPAPSK) ||
		     (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPA2) ||
		     (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPA2PSK)
#if WPA_SUPPLICANT_SUPPORT
		     || (pAd->PortCfg.IEEE8021X == TRUE)
#endif
		    ) &&
		    (pAd->PortCfg.PortSecured == WPA_802_1X_PORT_NOT_SECURED)) {
			// case 1.1 For WPA, WPA-PSK, if the 1x port is not secured, we have to redo
			//          connection process
			DBGPRINT(RT_DEBUG_TRACE,
				 "CNTL - disassociate with current AP...\n");
			DisassocParmFill(pAd, &DisassocReq, pAd->PortCfg.Bssid,
					 REASON_DISASSOC_STA_LEAVING);
			MlmeEnqueue(pAd, ASSOC_STATE_MACHINE,
				    MT2_MLME_DISASSOC_REQ,
				    sizeof(MLME_DISASSOC_REQ_STRUCT),
				    &DisassocReq);
			pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_DISASSOC;
		} else if (pAd->bConfigChanged == TRUE) {
			// case 1.2 Important Config has changed, we have to reconnect to the same AP
			DBGPRINT(RT_DEBUG_TRACE,
				 "CNTL - disassociate with current AP Because config changed...\n");
			DisassocParmFill(pAd, &DisassocReq, pAd->PortCfg.Bssid,
					 REASON_DISASSOC_STA_LEAVING);
			MlmeEnqueue(pAd, ASSOC_STATE_MACHINE,
				    MT2_MLME_DISASSOC_REQ,
				    sizeof(MLME_DISASSOC_REQ_STRUCT),
				    &DisassocReq);
			pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_DISASSOC;
		} else {
			// case 1.3. already connected to the SSID with highest RSSI.
			DBGPRINT(RT_DEBUG_TRACE,
				 "CNTL - already with this BSSID. ignore this SET_SSID request\n");

			// We only check if same to the BSSID with highest RSSI.
			// If roaming of same SSID required, we still do the reconnection.
			// same BSSID, go back to idle state directly
			if (pAd->MlmeAux.CurrReqIsFromNdis) {
			}
			pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;
		}
	} else if (INFRA_ON(pAd)) {
		// case 2. active INFRA association existent
		//    roaming is done within miniport driver, nothing to do with configuration
		//    utility. so upon a new SET(OID_802_11_SSID) is received, we just
		//    disassociate with the current associated AP,
		//    then perform a new association with this new SSID, no matter the
		//    new/old SSID are the same or not.
		DBGPRINT(RT_DEBUG_TRACE,
			 "CNTL - disassociate with current AP...\n");
		DisassocParmFill(pAd, &DisassocReq, pAd->PortCfg.Bssid,
				 REASON_DISASSOC_STA_LEAVING);
		MlmeEnqueue(pAd, ASSOC_STATE_MACHINE, MT2_MLME_DISASSOC_REQ,
			    sizeof(MLME_DISASSOC_REQ_STRUCT), &DisassocReq);
		pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_DISASSOC;
	} else {
		if (ADHOC_ON(pAd)) {
			DBGPRINT(RT_DEBUG_TRACE, "CNTL - drop current ADHOC\n");
			LinkDown(pAd, FALSE);
			OPSTATUS_CLEAR_FLAG(pAd,
					    fOP_STATUS_MEDIA_STATE_CONNECTED);
			DBGPRINT(RT_DEBUG_TRACE,
				 "NDIS_STATUS_MEDIA_DISCONNECT Event C!\n");
		}

		if ((pAd->MlmeAux.SsidBssTab.BssNr == 0) &&
		    (pAd->PortCfg.bAutoReconnect == TRUE) &&
		    (pAd->MlmeAux.BssType == BSS_INFRA) &&
		    (MlmeValidateSSID(pAd->MlmeAux.Ssid, pAd->MlmeAux.SsidLen)
		     == TRUE)) {
			MLME_SCAN_REQ_STRUCT ScanReq;

			DBGPRINT(RT_DEBUG_TRACE,
				 "CNTL - No matching BSS, start a new scan\n");

			ScanParmFill(pAd, &ScanReq, pAd->MlmeAux.Ssid,
				     pAd->MlmeAux.SsidLen, BSS_ANY,
				     SCAN_ACTIVE);
			MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_SCAN_REQ,
				    sizeof(MLME_SCAN_REQ_STRUCT), &ScanReq);
			pAd->Mlme.CntlMachine.CurrState =
			    CNTL_WAIT_OID_LIST_SCAN;
			// Reset Missed scan number
			pAd->PortCfg.LastScanTime = Now;
		} else {
			pAd->MlmeAux.BssIdx = 0;
			IterateOnBssTab(pAd);
		}
	}
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
VOID CntlOidRTBssidProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	ULONG BssIdx;
	PUCHAR pOidBssid = (PUCHAR) Elem->Msg;
	MLME_DISASSOC_REQ_STRUCT DisassocReq;
	MLME_JOIN_REQ_STRUCT JoinReq;

	// record user desired settings
	memcpy(pAd->MlmeAux.Bssid, pOidBssid, ETH_ALEN);
	pAd->MlmeAux.BssType = pAd->PortCfg.BssType;

	// find the desired BSS in the latest SCAN result table
	BssIdx = BssTableSearch(&pAd->ScanTab, pOidBssid, pAd->MlmeAux.Channel);
	if (BssIdx == BSS_NOT_FOUND) {
		DBGPRINT(RT_DEBUG_TRACE,
			 "CNTL - BSSID not found. reply NDIS_STATUS_NOT_ACCEPTED\n");
		pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;
		return;
	}
	// copy the matched BSS entry from ScanTab to MlmeAux.SsidBssTab. Why?
	// Because we need this entry to become the JOIN target in later on SYNC state machine
	pAd->MlmeAux.BssIdx = 0;
	pAd->MlmeAux.SsidBssTab.BssNr = 1;
	memcpy(&pAd->MlmeAux.SsidBssTab.BssEntry[0],
	       &pAd->ScanTab.BssEntry[BssIdx], sizeof(BSS_ENTRY));

	//
	// Update Reconnect Ssid, that user desired to connect.
	//
	pAd->MlmeAux.AutoReconnectSsidLen =
	    pAd->ScanTab.BssEntry[BssIdx].SsidLen;
	memcpy(pAd->MlmeAux.AutoReconnectSsid,
	       pAd->ScanTab.BssEntry[BssIdx].Ssid,
	       pAd->ScanTab.BssEntry[BssIdx].SsidLen);

	// Add SSID into MlmeAux for site surey joining hidden SSID
	pAd->MlmeAux.SsidLen = pAd->ScanTab.BssEntry[BssIdx].SsidLen;
	memcpy(pAd->MlmeAux.Ssid, pAd->ScanTab.BssEntry[BssIdx].Ssid,
	       pAd->MlmeAux.SsidLen);

	// 2002-11-26 skip the following checking. i.e. if user wants to re-connect to same AP
	//   we just follow normal procedure. The reason of user doing this may because he/she changed
	//   AP to another channel, but we still received BEACON from it thus don't claim Link Down.
	//   Since user knows he's changed AP channel, he'll re-connect again. By skipping the following
	//   checking, we'll disassociate then re-do normal association with this AP at the new channel.
	// 2003-1-6 Re-enable this feature based on microsoft requirement which prefer not to re-do
	//   connection when setting the same BSSID.
	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED) &&
	    (memcmp(pAd->PortCfg.Bssid, pOidBssid, ETH_ALEN) == 0)) {
		// already connected to the same BSSID, go back to idle state directly
		DBGPRINT(RT_DEBUG_TRACE,
			 "CNTL - already in this BSSID. ignore this SET_BSSID request\n");
		pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;
	} else {
		if (INFRA_ON(pAd)) {
			// disassoc from current AP first
			DBGPRINT(RT_DEBUG_TRACE,
				 "CNTL - disassociate with current AP ...\n");
			DisassocParmFill(pAd, &DisassocReq, pAd->PortCfg.Bssid,
					 REASON_DISASSOC_STA_LEAVING);
			MlmeEnqueue(pAd, ASSOC_STATE_MACHINE,
				    MT2_MLME_DISASSOC_REQ,
				    sizeof(MLME_DISASSOC_REQ_STRUCT),
				    &DisassocReq);

			pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_DISASSOC;
		} else {
			if (ADHOC_ON(pAd)) {
				DBGPRINT(RT_DEBUG_TRACE,
					 "CNTL - drop current ADHOC\n");
				LinkDown(pAd, FALSE);
				OPSTATUS_CLEAR_FLAG(pAd,
						    fOP_STATUS_MEDIA_STATE_CONNECTED);
				DBGPRINT(RT_DEBUG_TRACE,
					 "NDIS_STATUS_MEDIA_DISCONNECT Event C!\n");
			}

			// Check cipher suite, AP must have more secured cipher than station setting
			// Set the Pairwise and Group cipher to match the intended AP setting
			// We can only connect to AP with less secured cipher setting
			if ((pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPA)
			    || (pAd->PortCfg.AuthMode ==
				Ndis802_11AuthModeWPAPSK)) {
				pAd->PortCfg.GroupCipher =
				    pAd->ScanTab.BssEntry[BssIdx].WPA.
				    GroupCipher;

				if (pAd->PortCfg.WepStatus ==
				    pAd->ScanTab.BssEntry[BssIdx].WPA.
				    PairCipher)
					pAd->PortCfg.PairCipher =
					    pAd->ScanTab.BssEntry[BssIdx].WPA.
					    PairCipher;
				else if (pAd->ScanTab.BssEntry[BssIdx].WPA.
					 PairCipherAux != Ndis802_11WEPDisabled)
					pAd->PortCfg.PairCipher =
					    pAd->ScanTab.BssEntry[BssIdx].WPA.
					    PairCipherAux;
				else	// There is no PairCipher Aux, downgrade our capability to TKIP
					pAd->PortCfg.PairCipher =
					    Ndis802_11Encryption2Enabled;
			} else
			    if ((pAd->PortCfg.AuthMode ==
				 Ndis802_11AuthModeWPA2)
				|| (pAd->PortCfg.AuthMode ==
				    Ndis802_11AuthModeWPA2PSK)) {
				pAd->PortCfg.GroupCipher =
				    pAd->ScanTab.BssEntry[BssIdx].WPA2.
				    GroupCipher;

				if (pAd->PortCfg.WepStatus ==
				    pAd->ScanTab.BssEntry[BssIdx].WPA2.
				    PairCipher)
					pAd->PortCfg.PairCipher =
					    pAd->ScanTab.BssEntry[BssIdx].WPA2.
					    PairCipher;
				else if (pAd->ScanTab.BssEntry[BssIdx].WPA2.
					 PairCipherAux != Ndis802_11WEPDisabled)
					pAd->PortCfg.PairCipher =
					    pAd->ScanTab.BssEntry[BssIdx].WPA2.
					    PairCipherAux;
				else	// There is no PairCipher Aux, downgrade our capability to TKIP
					pAd->PortCfg.PairCipher =
					    Ndis802_11Encryption2Enabled;

				// RSN capability
				pAd->PortCfg.RsnCapability =
				    pAd->ScanTab.BssEntry[BssIdx].WPA2.
				    RsnCapability;
			}
			// Set Mix cipher flag
			if (pAd->PortCfg.PairCipher != pAd->PortCfg.GroupCipher)
				pAd->PortCfg.bMixCipher = TRUE;

			// No active association, join the BSS immediately
			DBGPRINT(RT_DEBUG_TRACE,
				 "CNTL - joining %02x:%02x:%02x:%02x:%02x:%02x ...\n",
				 pOidBssid[0], pOidBssid[1], pOidBssid[2],
				 pOidBssid[3], pOidBssid[4], pOidBssid[5]);

			JoinParmFill(pAd, &JoinReq, pAd->MlmeAux.BssIdx);
			MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_JOIN_REQ,
				    sizeof(MLME_JOIN_REQ_STRUCT), &JoinReq);

			pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_JOIN;
		}
	}
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
static VOID CntlWaitJoinProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	USHORT Reason;
	MLME_AUTH_REQ_STRUCT AuthReq;

	if (Elem->MsgType == MT2_JOIN_CONF) {
		memcpy(&Reason, Elem->Msg, sizeof(USHORT));
		if (Reason == MLME_SUCCESS) {
			// 1. joined an IBSS, we are pretty much done here
			if (pAd->MlmeAux.BssType == BSS_ADHOC) {
				LinkUp(pAd, BSS_ADHOC);
				pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;
				DBGPRINT(RT_DEBUG_TRACE,
					 "CNTL - join the IBSS = %02x:%02x:%02x:%02x:%02x:%02x ...\n",
					 pAd->PortCfg.Bssid[0],
					 pAd->PortCfg.Bssid[1],
					 pAd->PortCfg.Bssid[2],
					 pAd->PortCfg.Bssid[3],
					 pAd->PortCfg.Bssid[4],
					 pAd->PortCfg.Bssid[5]);
			}
			// 2. joined a new INFRA network, start from authentication
			else {
				// either Ndis802_11AuthModeShared or Ndis802_11AuthModeAutoSwitch, try shared key first
				if ((pAd->PortCfg.AuthMode ==
				     Ndis802_11AuthModeShared)
				    || (pAd->PortCfg.AuthMode ==
					Ndis802_11AuthModeAutoSwitch)) {
					AuthParmFill(pAd, &AuthReq,
						     pAd->MlmeAux.Bssid,
						     Ndis802_11AuthModeShared);
				} else {
					AuthParmFill(pAd, &AuthReq,
						     pAd->MlmeAux.Bssid,
						     Ndis802_11AuthModeOpen);
				}

				MlmeEnqueue(pAd, AUTH_STATE_MACHINE,
					    MT2_MLME_AUTH_REQ,
					    sizeof(MLME_AUTH_REQ_STRUCT),
					    &AuthReq);

				pAd->Mlme.CntlMachine.CurrState =
				    CNTL_WAIT_AUTH;
			}
		} else {
			// 3. failed, try next BSS
			pAd->MlmeAux.BssIdx++;
			IterateOnBssTab(pAd);
		}
	}
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
static VOID CntlWaitStartProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	USHORT Result;

	if (Elem->MsgType == MT2_START_CONF) {
		memcpy(&Result, Elem->Msg, sizeof(USHORT));
		if (Result == MLME_SUCCESS) {
			LinkUp(pAd, BSS_ADHOC);
			pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;

			// Before send beacon, driver need do radar detection
			if (((pAd->PortCfg.PhyMode == PHY_11A)
			     || (pAd->PortCfg.PhyMode == PHY_11ABG_MIXED))
			    && (pAd->PortCfg.bIEEE80211H == 1)
			    && RadarChannelCheck(pAd, pAd->PortCfg.Channel)) {
				pAd->PortCfg.RadarDetect.RDMode =
				    RD_SILENCE_MODE;
				pAd->PortCfg.RadarDetect.RDCount = 0;
				RadarDetectionStart(pAd);
			}

			DBGPRINT(RT_DEBUG_TRACE,
				 "CNTL - start a new IBSS = %02x:%02x:%02x:%02x:%02x:%02x ...\n",
				 pAd->PortCfg.Bssid[0], pAd->PortCfg.Bssid[1],
				 pAd->PortCfg.Bssid[2], pAd->PortCfg.Bssid[3],
				 pAd->PortCfg.Bssid[4], pAd->PortCfg.Bssid[5]);
		} else {
			DBGPRINT(RT_DEBUG_TRACE,
				 "CNTL - Start IBSS fail. BUG!!!!!\n");
			pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;
		}
	}
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
static VOID CntlWaitAuthProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	USHORT Reason;
	MLME_ASSOC_REQ_STRUCT AssocReq;
	MLME_AUTH_REQ_STRUCT AuthReq;

	if (Elem->MsgType == MT2_AUTH_CONF) {
		memcpy(&Reason, Elem->Msg, sizeof(USHORT));
		if (Reason == MLME_SUCCESS) {
			DBGPRINT(RT_DEBUG_TRACE, "CNTL - AUTH OK\n");
			AssocParmFill(pAd, &AssocReq, pAd->MlmeAux.Bssid,
				      pAd->MlmeAux.CapabilityInfo,
				      ASSOC_TIMEOUT,
				      pAd->PortCfg.DefaultListenCount);

			MlmeEnqueue(pAd, ASSOC_STATE_MACHINE,
				    MT2_MLME_ASSOC_REQ,
				    sizeof(MLME_ASSOC_REQ_STRUCT), &AssocReq);

			pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_ASSOC;

		} else {
			// This fail may because of the AP already keep us in its MAC table without
			// ageing-out. The previous authentication attempt must have let it remove us.
			// so try Authentication again may help. For D-Link DWL-900AP+ compatibility.
			DBGPRINT(RT_DEBUG_TRACE,
				 "CNTL - AUTH FAIL, try again...\n");

			if ((pAd->PortCfg.AuthMode == Ndis802_11AuthModeShared)
			    || (pAd->PortCfg.AuthMode ==
				Ndis802_11AuthModeAutoSwitch)) {
				// either Ndis802_11AuthModeShared or Ndis802_11AuthModeAutoSwitch, try shared key first
				AuthParmFill(pAd, &AuthReq, pAd->MlmeAux.Bssid,
					     Ndis802_11AuthModeShared);
			} else {
				AuthParmFill(pAd, &AuthReq, pAd->MlmeAux.Bssid,
					     Ndis802_11AuthModeOpen);
			}

			MlmeEnqueue(pAd, AUTH_STATE_MACHINE, MT2_MLME_AUTH_REQ,
				    sizeof(MLME_AUTH_REQ_STRUCT), &AuthReq);

			pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_AUTH2;
		}
	}
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
static VOID CntlWaitAuthProc2(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	USHORT Reason;
	MLME_ASSOC_REQ_STRUCT AssocReq;
	MLME_AUTH_REQ_STRUCT AuthReq;

	if (Elem->MsgType == MT2_AUTH_CONF) {
		memcpy(&Reason, Elem->Msg, sizeof(USHORT));
		if (Reason == MLME_SUCCESS) {
			DBGPRINT(RT_DEBUG_TRACE, "CNTL - AUTH OK\n");
			AssocParmFill(pAd, &AssocReq, pAd->MlmeAux.Bssid,
				      pAd->MlmeAux.CapabilityInfo,
				      ASSOC_TIMEOUT,
				      pAd->PortCfg.DefaultListenCount);
			MlmeEnqueue(pAd, ASSOC_STATE_MACHINE,
				    MT2_MLME_ASSOC_REQ,
				    sizeof(MLME_ASSOC_REQ_STRUCT), &AssocReq);

			pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_ASSOC;
		} else {

			if ((pAd->PortCfg.AuthMode ==
			     Ndis802_11AuthModeAutoSwitch)
			    && (pAd->MlmeAux.Alg == Ndis802_11AuthModeShared)) {
				DBGPRINT(RT_DEBUG_TRACE,
					 "CNTL - AUTH FAIL, try OPEN system...\n");
				AuthParmFill(pAd, &AuthReq, pAd->MlmeAux.Bssid,
					     Ndis802_11AuthModeOpen);
				MlmeEnqueue(pAd, AUTH_STATE_MACHINE,
					    MT2_MLME_AUTH_REQ,
					    sizeof(MLME_AUTH_REQ_STRUCT),
					    &AuthReq);

				pAd->Mlme.CntlMachine.CurrState =
				    CNTL_WAIT_AUTH2;
			} else {
				// not success, try next BSS
				DBGPRINT(RT_DEBUG_TRACE,
					 "CNTL - AUTH FAIL, give up; try next BSS\n");
// 2004-09-11 john -  why change state?
//             pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE; //???????
				pAd->MlmeAux.BssIdx++;
				IterateOnBssTab(pAd);
			}
		}
	}
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
static VOID CntlWaitAssocProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	USHORT Reason;

	if (Elem->MsgType == MT2_ASSOC_CONF) {
		memcpy(&Reason, Elem->Msg, sizeof(USHORT));
		if (Reason == MLME_SUCCESS) {
			LinkUp(pAd, BSS_INFRA);
			pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;
			DBGPRINT(RT_DEBUG_TRACE,
				 "CNTL - Association successful on BSS #%d\n",
				 pAd->MlmeAux.BssIdx);
		} else {
			// not success, try next BSS
			DBGPRINT(RT_DEBUG_TRACE,
				 "CNTL - Association fails on BSS #%d\n",
				 pAd->MlmeAux.BssIdx);
			pAd->MlmeAux.BssIdx++;
			IterateOnBssTab(pAd);
		}
	}
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
static VOID CntlWaitReassocProc(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	USHORT Result;

	if (Elem->MsgType == MT2_REASSOC_CONF) {
		memcpy(&Result, Elem->Msg, sizeof(USHORT));
		if (Result == MLME_SUCCESS) {
			//
			// NDIS requires a new Link UP indication but no Link Down for RE-ASSOC
			//
			LinkUp(pAd, BSS_INFRA);

			pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;
			DBGPRINT(RT_DEBUG_TRACE,
				 "CNTL - Re-assocition successful on BSS #%d\n",
				 pAd->MlmeAux.RoamIdx);
		} else {
			// reassoc failed, try to pick next BSS in the BSS Table
			DBGPRINT(RT_DEBUG_TRACE,
				 "CNTL - Re-assocition fails on BSS #%d\n",
				 pAd->MlmeAux.RoamIdx);
			pAd->MlmeAux.RoamIdx++;
			IterateOnBssTab2(pAd);
		}
	}
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
VOID MlmeCntlInit(IN PRTMP_ADAPTER pAd,
		  IN STATE_MACHINE * S, OUT STATE_MACHINE_FUNC Trans[])
{
	// Control state machine differs from other state machines, the interface
	// follows the standard interface
	pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
VOID MlmeCntlMachinePerformAction(IN PRTMP_ADAPTER pAd,
				  IN STATE_MACHINE * S,
				  IN MLME_QUEUE_ELEM * Elem)
{
	switch (Elem->MsgType) {
	case OID_802_11_SSID:
		CntlOidSsidProc(pAd, Elem);
		return;
	case OID_802_11_BSSID:
		CntlOidRTBssidProc(pAd, Elem);
		return;
	case OID_802_11_BSSID_LIST_SCAN:
		CntlOidScanProc(pAd, Elem);
		return;
	}

	switch (pAd->Mlme.CntlMachine.CurrState) {
	case CNTL_IDLE:
		CntlIdleProc(pAd, Elem);
		break;
	case CNTL_WAIT_DISASSOC:
		CntlWaitDisassocProc(pAd, Elem);
		break;
	case CNTL_WAIT_JOIN:
		CntlWaitJoinProc(pAd, Elem);
		break;

		// CNTL_WAIT_REASSOC is the only state in CNTL machine that does
		// not triggered directly or indirectly by "RTMPSetInformation(OID_xxx)".
		// Therefore not protected by NDIS's "only one outstanding OID request"
		// rule. Which means NDIS may SET OID in the middle of ROAMing attempts.
		// Current approach is to block new SET request at RTMPSetInformation()
		// when CntlMachine.CurrState is not CNTL_IDLE
	case CNTL_WAIT_REASSOC:
		CntlWaitReassocProc(pAd, Elem);
		break;

	case CNTL_WAIT_START:
		CntlWaitStartProc(pAd, Elem);
		break;
	case CNTL_WAIT_AUTH:
		CntlWaitAuthProc(pAd, Elem);
		break;
	case CNTL_WAIT_AUTH2:
		CntlWaitAuthProc2(pAd, Elem);
		break;
	case CNTL_WAIT_ASSOC:
		CntlWaitAssocProc(pAd, Elem);
		break;

	case CNTL_WAIT_OID_LIST_SCAN:
		if (Elem->MsgType == MT2_SCAN_CONF) {
			// Resume TxRing after SCANING complete. We hope the out-of-service time
			// won't be too long to let upper layer time-out the waiting frames
			RTMPResumeMsduTransmission(pAd);

			pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;
		}
		break;

	case CNTL_WAIT_OID_DISASSOC:
		if (Elem->MsgType == MT2_DISASSOC_CONF) {
			LinkDown(pAd, FALSE);
			pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;
		}
		break;

	default:
		printk(KERN_ERR DRIVER_NAME
		       "!ERROR! CNTL - Illegal message type(=%d)",
		       Elem->MsgType);
		break;
	}
}

/*
    ==========================================================================
    Description:
    ==========================================================================
 */
static VOID ComposePsPoll(IN PRTMP_ADAPTER pAd)
{
	memset(&pAd->PsPollFrame, 0, sizeof(PSPOLL_FRAME));
	pAd->PsPollFrame.FC.Type = BTYPE_CNTL;
	pAd->PsPollFrame.FC.SubType = SUBTYPE_PS_POLL;
	pAd->PsPollFrame.Aid = pAd->ActiveCfg.Aid | 0xC000;
	memcpy(pAd->PsPollFrame.Bssid, pAd->PortCfg.Bssid, ETH_ALEN);
	memcpy(pAd->PsPollFrame.Ta, pAd->CurrentAddress, ETH_ALEN);
}

static VOID ComposeNullFrame(IN PRTMP_ADAPTER pAd)
{
	memset(&pAd->NullFrame, 0, sizeof(HEADER_802_11));
	pAd->NullFrame.FC.Type = BTYPE_DATA;
	pAd->NullFrame.FC.SubType = SUBTYPE_NULL_FUNC;
	pAd->NullFrame.FC.ToDs = 1;
	memcpy(pAd->NullFrame.Addr1, pAd->PortCfg.Bssid, ETH_ALEN);
	memcpy(pAd->NullFrame.Addr2, pAd->CurrentAddress, ETH_ALEN);
	memcpy(pAd->NullFrame.Addr3, pAd->PortCfg.Bssid, ETH_ALEN);
}

/*
    ==========================================================================
    Description:
    ==========================================================================
*/
VOID LinkUp(IN PRTMP_ADAPTER pAd, IN UCHAR BssType)
{
	unsigned long Now;
	TXRX_CSR4_STRUC NewTxRxCsr4, CurTxRxCsr4;

	COPY_SETTINGS_FROM_MLME_AUX_TO_ACTIVE_CFG(pAd);
	DBGPRINT(RT_DEBUG_TRACE,
		 "!!! LINK UP !!! (Infra=%d, AID=%d, ssid=%s)\n", BssType,
		 pAd->ActiveCfg.Aid, pAd->PortCfg.Ssid);

	if (BssType == BSS_ADHOC) {
		OPSTATUS_SET_FLAG(pAd, fOP_STATUS_ADHOC_ON);
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_INFRA_ON);
	} else {
		OPSTATUS_SET_FLAG(pAd, fOP_STATUS_INFRA_ON);
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_ADHOC_ON);
	}

	AsicSetBssid(pAd, pAd->PortCfg.Bssid);
	AsicSwitchChannel(pAd, pAd->PortCfg.Channel);
	AsicLockChannel(pAd, pAd->PortCfg.Channel);
	AsicSetSlotTime(pAd, TRUE);	//FALSE);
	AsicSetEdcaParm(pAd, &pAd->PortCfg.APEdcaParm);

	MlmeUpdateTxRates(pAd, TRUE);
	memset(&pAd->DrsCounters, 0, sizeof(COUNTER_DRS));

	Now = jiffies;
	pAd->PortCfg.LastBeaconRxTime = Now;	// last RX timestamp

	if ((pAd->PortCfg.TxPreamble != Rt802_11PreambleLong) &&
	    CAP_IS_SHORT_PREAMBLE_ON(pAd->ActiveCfg.CapabilityInfo)) {

		DBGPRINT(RT_DEBUG_INFO,
			 "CNTL - !!! Set to short preamble!!!\n");
		MlmeSetTxPreamble(pAd, Rt802_11PreambleShort);
	}

	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_AGGREGATION_INUSED);

	pAd->PortCfg.RadarDetect.RDMode = RD_NORMAL_MODE;
	if (pAd->PortCfg.RadarDetect.RDMode == RD_SILENCE_MODE) {
		RadarDetectionStop(pAd);
	}

	if (BssType == BSS_ADHOC) {
		//
		// We also need to cancel the LinkDownTimer, no matter it was been set or not.
		// It may be set when we start an Infrastructure mode.
		// And not be canceled yet then we switch to Adohc at meanwhile.
		//
		del_timer_sync(&pAd->Mlme.LinkDownTimer);

		MakeIbssBeacon(pAd);

		if (((pAd->PortCfg.PhyMode == PHY_11A)
		     || (pAd->PortCfg.PhyMode == PHY_11ABG_MIXED))
		    && (pAd->PortCfg.bIEEE80211H == 1)
		    && RadarChannelCheck(pAd, pAd->PortCfg.Channel)) {
			;;
		} else {
			AsicEnableIbssSync(pAd);
		}

#ifdef	SINGLE_ADHOC_LINKUP
		// Although this did not follow microsoft's recommendation.
		//Change based on customer's request
		OPSTATUS_SET_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED);
#endif

	} else			// BSS_INFRA
	{
		int t;

		// First cancel linkdown timer
		t = del_timer_sync(&pAd->Mlme.LinkDownTimer);

		// Check the new SSID with last SSID
		if (t) {
			if ((pAd->PortCfg.LastSsidLen != pAd->PortCfg.SsidLen)
			    ||
			    ((pAd->PortCfg.LastSsidLen == pAd->PortCfg.SsidLen)
			     &&
			     (memcmp
			      (pAd->PortCfg.LastSsid, pAd->PortCfg.Ssid,
			       pAd->PortCfg.LastSsidLen) != 0))) {
				// Send link down event before set to link up
				OPSTATUS_CLEAR_FLAG(pAd,
						    fOP_STATUS_MEDIA_STATE_CONNECTED);
				DBGPRINT(RT_DEBUG_TRACE,
					 "NDIS_STATUS_MEDIA_DISCONNECT Event AA!\n");
			}
		}
		//
		// On WPA mode, Remove All Keys if not connect to the last BSSID
		// Key will be set after 4-way handshake.
		//
		if ((pAd->PortCfg.AuthMode >= Ndis802_11AuthModeWPA) &&
		    (memcmp
		     (pAd->PortCfg.LastBssid, pAd->PortCfg.Bssid,
		      ETH_ALEN) != 0)) {
			// Remove all WPA keys
			RTMPWPARemoveAllKeys(pAd);
		}
		// NOTE:
		// the decision of using "short slot time" or not may change dynamically due to
		// new STA association to the AP. so we have to decide that upon parsing BEACON, not here

		// NOTE:
		// the decision to use "RTC/CTS" or "CTS-to-self" protection or not may change dynamically
		// due to new STA association to the AP. so we have to decide that upon parsing BEACON, not here

		ComposePsPoll(pAd);
		ComposeNullFrame(pAd);
		AsicEnableBssSync(pAd);

		// only INFRASTRUCTURE mode need to indicate connectivity immediately; ADHOC mode
		// should wait until at least 2 active nodes in this BSSID.
		OPSTATUS_SET_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED);

#ifdef AGGREGATION_SUPPORT
		if (pAd->PortCfg.bAggregationCapable) {
			if ((pAd->PortCfg.bPiggyBackCapable)
			    && (pAd->MlmeAux.APRalinkIe & 0x00000003) == 3) {
				ULONG csr0;

				RTMP_IO_READ32(pAd, PHY_CSR0, &csr0);
				csr0 = csr0 & 0xfff3ffff;	// Mask off bit 18, bit 19
				csr0 = csr0 | BIT32[18];	// b18 to enable piggy-back
				RTMP_IO_WRITE32(pAd, PHY_CSR0, csr0);

				OPSTATUS_SET_FLAG(pAd,
						  fOP_STATUS_PIGGYBACK_INUSED);
				OPSTATUS_SET_FLAG(pAd,
						  fOP_STATUS_AGGREGATION_INUSED);
				DBGPRINT(RT_DEBUG_TRACE,
					 "Turn on Piggy-Back\n");
			} else if (pAd->MlmeAux.APRalinkIe & 0x00000001) {
				OPSTATUS_SET_FLAG(pAd,
						  fOP_STATUS_AGGREGATION_INUSED);
			}
		}
#endif
	}
	DBGPRINT(RT_DEBUG_TRACE, "NDIS_STATUS_MEDIA_CONNECT Event B!\n");

	// Set LED
	RTMPSetLED(pAd, LED_LINK_UP);

	//
	// Enable OFDM TX rate auto fallback to CCK, if need.
	//
	RTMP_IO_READ32(pAd, TXRX_CSR4, &CurTxRxCsr4.word);

	NewTxRxCsr4.word = CurTxRxCsr4.word;

	if ((pAd->PortCfg.Channel <= 14) &&
	    ((pAd->PortCfg.PhyMode == PHY_11B) ||
	     (pAd->PortCfg.PhyMode == PHY_11BG_MIXED) ||
	     (pAd->PortCfg.PhyMode == PHY_11ABG_MIXED))) {
		NewTxRxCsr4.field.OfdmTxFallbacktoCCK = 1;	//Enable OFDM TX rate auto fallback to CCK 1M, 2M
	} else {
		NewTxRxCsr4.field.OfdmTxFallbacktoCCK = 0;	//Disable OFDM TX rate auto fallback to CCK 1M, 2M
	}

	if (NewTxRxCsr4.word != CurTxRxCsr4.word)
		RTMP_IO_WRITE32(pAd, TXRX_CSR4, NewTxRxCsr4.word);

	pAd->Mlme.PeriodicRound = 0;	// re-schedule MlmePeriodicExec()
	pAd->bConfigChanged = FALSE;	// Reset config flag
	pAd->ExtraInfo = GENERAL_LINK_UP;	// Update extra information to link is up

}

/*
	==========================================================================

	Routine	Description:
		Disconnect current BSSID

	Arguments:
		pAd				- Pointer to our adapter
		IsReqFromAP		- Request from AP

	Return Value:
		None

	Note:
		We need more information to know it's this requst from AP.
		If yes! we need to do extra handling, for example, remove the WPA key.
		Otherwise on 4-way handshaking will faied, since the WPA key didn't be
		remove while auto reconnect.
		Disconnect request from AP, it means we will start afresh 4-way handshaking
		on WPA mode.

	==========================================================================
*/
VOID LinkDown(IN PRTMP_ADAPTER pAd, IN BOOLEAN IsReqFromAP)
{
	TXRX_CSR4_STRUC CurTxRxCsr4;

	DBGPRINT(RT_DEBUG_TRACE, "!!! LINK DOWN !!!\n");

	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_AGGREGATION_INUSED);

	if (ADHOC_ON(pAd))	// Adhoc mode link down
	{
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_ADHOC_ON);

//              OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED);
		// clean up previous SCAN result, add current BSS back to table if any
//              BssTableDeleteEntry(&pAd->ScanTab, pAd->PortCfg.Bssid, pAd->PortCfg.Channel);

#ifdef	SINGLE_ADHOC_LINKUP
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED);
		// clean up previous SCAN result, add current BSS back to table if any
		BssTableDeleteEntry(&pAd->ScanTab, pAd->PortCfg.Bssid,
				    pAd->PortCfg.Channel);
#else
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF)) {
			OPSTATUS_CLEAR_FLAG(pAd,
					    fOP_STATUS_MEDIA_STATE_CONNECTED);
			BssTableDeleteEntry(&pAd->ScanTab, pAd->PortCfg.Bssid,
					    pAd->PortCfg.Channel);
		}
#endif
	} else			// Infra structure mode
	{
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_INFRA_ON);
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED);
		// Saved last SSID for linkup comparison
		pAd->PortCfg.LastSsidLen = pAd->PortCfg.SsidLen;
		memcpy(pAd->PortCfg.LastSsid, pAd->PortCfg.Ssid,
		       pAd->PortCfg.LastSsidLen);
		memcpy(pAd->PortCfg.LastBssid, pAd->PortCfg.Bssid, ETH_ALEN);
		if (pAd->MlmeAux.CurrReqIsFromNdis == TRUE) {
			DBGPRINT(RT_DEBUG_TRACE,
				 "NDIS_STATUS_MEDIA_DISCONNECT Event A!\n");
			pAd->MlmeAux.CurrReqIsFromNdis = FALSE;
		} else {
			// Set linkdown timer
			pAd->Mlme.LinkDownTimer.expires = jiffies + 10 * HZ;	// timeout Timer
			add_timer(&pAd->Mlme.LinkDownTimer);
		}

		BssTableDeleteEntry(&pAd->ScanTab, pAd->PortCfg.Bssid,
				    pAd->PortCfg.Channel);

		// restore back to -
		//      1. long slot (20 us) or short slot (9 us) time
		//      2. turn on/off RTS/CTS and/or CTS-to-self protection
		//      3. short preamble
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED);

	}

	//
	// Reset CWMin & CWMax to default value
	// Since we reset the slot time to 0x14(long slot time), so we also need to
	// Reset the flag fOP_STATUS_SHORT_SLOT_INUSED at the same time.
	//
	RTMP_IO_WRITE32(pAd, MAC_CSR9, 0x0704a414);
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_SLOT_INUSED);

	AsicSetSlotTime(pAd, TRUE);	//FALSE);
	AsicSetEdcaParm(pAd, NULL);

	// Set LED
	RTMPSetLED(pAd, LED_LINK_DOWN);

	AsicDisableSync(pAd);
	pAd->Mlme.PeriodicRound = 0;

	// Remove PortCfg Information after link down
	memset(pAd->PortCfg.Bssid, 0, ETH_ALEN);
	//memset(pAd->PortCfg.Ssid, 0, MAX_LEN_OF_SSID);
	//pAd->PortCfg.SsidLen = 0;

	// Reset WPA-PSK state. Only reset when supplicant enabled
	if (pAd->PortCfg.WpaState != SS_NOTUSE) {
		pAd->PortCfg.WpaState = SS_START;
		// Clear Replay counter
		memset(pAd->PortCfg.ReplayCounter, 0, 8);
	}
	//
	// if link down come from AP, we need to remove all WPA keys on WPA mode.
	// otherwise will cause 4-way handshaking failed, since the WPA key not empty.
	//
	if ((IsReqFromAP) && (pAd->PortCfg.AuthMode >= Ndis802_11AuthModeWPA)) {
		// Remove all WPA keys
		RTMPWPARemoveAllKeys(pAd);
	}
	// 802.1x port control
	pAd->PortCfg.PortSecured = WPA_802_1X_PORT_NOT_SECURED;
	pAd->PortCfg.MicErrCnt = 0;

	// Update extra information to link is up
	pAd->ExtraInfo = GENERAL_LINK_DOWN;

	// Clean association information
	memset(&pAd->PortCfg.AssocInfo, 0,
	       sizeof(NDIS_802_11_ASSOCIATION_INFORMATION));
	pAd->PortCfg.AssocInfo.Length =
	    sizeof(NDIS_802_11_ASSOCIATION_INFORMATION);
	pAd->PortCfg.ReqVarIELen = 0;
	pAd->PortCfg.ResVarIELen = 0;

	//
	// Reset RSSI value after link down
	//
	pAd->PortCfg.AvgRssi = 0;
	pAd->PortCfg.AvgRssiX8 = 0;
	pAd->PortCfg.AvgRssi2 = 0;
	pAd->PortCfg.AvgRssi2X8 = 0;

	// Restore MlmeRate
	pAd->PortCfg.MlmeRate = pAd->PortCfg.BasicMlmeRate;
	pAd->PortCfg.RtsRate = pAd->PortCfg.BasicMlmeRate;

	//
	// After link down, reset R17 to LowerBound.
	//
	if (pAd->MlmeAux.Channel <= 14) {
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 17,
					     pAd->BbpTuning.R17LowerBoundG);
	} else {
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 17,
					     pAd->BbpTuning.R17LowerBoundA);
	}

	//
	// After Link down, Set to Collect Rssi-A/Rssi-B mode.
	//
	if ((pAd->RfIcType == RFIC_5325) || (pAd->RfIcType == RFIC_2529)) {
		pAd->Mlme.bTxRateReportPeriod = FALSE;
		RTMP_IO_WRITE32(pAd, TXRX_CSR1, 0x9eb39eb3);
		DBGPRINT(RT_DEBUG_INFO, "Collect Rssi-A/Rssi-B\n");
	}

	//
	// After Link down, reset piggy-back setting in ASIC
	//
	{
		RTMPSetPiggyBack(pAd, NULL, FALSE);

		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_PIGGYBACK_INUSED);
	}

	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MAX_RETRY_ENABLED)) {
		RTMP_IO_READ32(pAd, TXRX_CSR4, &CurTxRxCsr4.word);
		CurTxRxCsr4.field.ShortRetryLimit = 0x07;
		CurTxRxCsr4.field.LongRetryLimit = 0x04;
		RTMP_IO_WRITE32(pAd, TXRX_CSR4, CurTxRxCsr4.word);
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_MAX_RETRY_ENABLED);
	}
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_RTS_PROTECTION_ENABLE);
}

/*
    ==========================================================================
    Description:
        Pre-build a BEACON frame in the shared memory
    ==========================================================================
*/
ULONG MakeIbssBeacon(IN PRTMP_ADAPTER pAd)
{
	UCHAR DsLen = 1, IbssLen = 2;
	UCHAR LocalErpIe[3] = { IE_ERP, 1, 0x04 };
	HEADER_802_11 BcnHdr;
	USHORT CapabilityInfo;
	LARGE_INTEGER FakeTimestamp;
	ULONG FrameLen;
	PTXD_STRUC pTxD = &pAd->BeaconTxD;
	CHAR *pBeaconFrame = pAd->BeaconBuf;
	BOOLEAN Privacy;
	UCHAR SupRate[MAX_LEN_OF_SUPPORTED_RATES];
	UCHAR SupRateLen = 0;
	UCHAR ExtRate[MAX_LEN_OF_SUPPORTED_RATES];
	UCHAR ExtRateLen = 0;

	// 2003-12-10 802.11g WIFI spec disallow OFDM rates in 802.11g ADHOC mode
	//            make sure 1,2,5.5,11 are the firt 4 rates in PortCfg.SupportedRates[] array
	if (((pAd->PortCfg.PhyMode == PHY_11BG_MIXED) ||
	     (pAd->PortCfg.PhyMode == PHY_11ABG_MIXED)) &&
	    (pAd->PortCfg.AdhocMode == 0)) {
		SupRate[0] = 0x82;	// 1 mbps
		SupRate[1] = 0x84;	// 2 mbps
		SupRate[2] = 0x8b;	// 5.5 mbps
		SupRate[3] = 0x96;	// 11 mbps
		SupRateLen = 4;
		ExtRateLen = 0;
	} else if (pAd->PortCfg.AdhocMode == 1)	//Adhoc Mode 1: B/G mixed.
	{
		SupRate[0] = 0x82;	// 1 mbps
		SupRate[1] = 0x84;	// 2 mbps
		SupRate[2] = 0x8b;	// 5.5 mbps
		SupRate[3] = 0x96;	// 11 mbps
		SupRateLen = 4;

		ExtRate[0] = 0x0C;	// 6 mbps, in units of 0.5 Mbps, not set basic rate when connect B only STA
		ExtRate[1] = 0x12;	// 9 mbps, in units of 0.5 Mbps
		ExtRate[2] = 0x18;	// 12 mbps, in units of 0.5 Mbps, not set basic rate when connect B only STA
		ExtRate[3] = 0x24;	// 18 mbps, in units of 0.5 Mbps
		ExtRate[4] = 0x30;	// 24 mbps, in units of 0.5 Mbps, not set basic rate when connect B only STA
		ExtRate[5] = 0x48;	// 36 mbps, in units of 0.5 Mbps
		ExtRate[6] = 0x60;	// 48 mbps, in units of 0.5 Mbps
		ExtRate[7] = 0x6c;	// 54 mbps, in units of 0.5 Mbps
		ExtRateLen = 8;
	} else {
		SupRateLen = pAd->PortCfg.SupRateLen;
		memcpy(SupRate, pAd->PortCfg.SupRate, SupRateLen);
		ExtRateLen = pAd->PortCfg.ExtRateLen;
		memcpy(ExtRate, pAd->PortCfg.ExtRate, ExtRateLen);
	}

	pAd->ActiveCfg.SupRateLen = SupRateLen;
	memcpy(pAd->ActiveCfg.SupRate, SupRate, SupRateLen);
	pAd->ActiveCfg.ExtRateLen = ExtRateLen;
	memcpy(pAd->ActiveCfg.ExtRate, ExtRate, ExtRateLen);

	// compose IBSS beacon frame
	MgtMacHeaderInit(pAd, &BcnHdr, SUBTYPE_BEACON, 0, BROADCAST_ADDR,
			 pAd->PortCfg.Bssid);
	Privacy = (pAd->PortCfg.WepStatus == Ndis802_11Encryption1Enabled)
	    || (pAd->PortCfg.WepStatus == Ndis802_11Encryption2Enabled)
	    || (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled);
	CapabilityInfo =
	    CAP_GENERATE(0, 1, Privacy,
			 (pAd->PortCfg.TxPreamble == Rt802_11PreambleShort), 0);

	MakeOutgoingFrame(pBeaconFrame, &FrameLen,
			  sizeof(HEADER_802_11), &BcnHdr,
			  TIMESTAMP_LEN, &FakeTimestamp,
			  2, &pAd->PortCfg.BeaconPeriod,
			  2, &CapabilityInfo,
			  1, &SsidIe,
			  1, &pAd->PortCfg.SsidLen,
			  pAd->PortCfg.SsidLen, pAd->PortCfg.Ssid,
			  1, &SupRateIe,
			  1, &SupRateLen,
			  SupRateLen, SupRate,
			  1, &DsIe,
			  1, &DsLen,
			  1, &pAd->PortCfg.Channel,
			  1, &IbssIe,
			  1, &IbssLen, 2, &pAd->ActiveCfg.AtimWin, END_OF_ARGS);

	// add ERP_IE and EXT_RAE IE of in 802.11g
	if (ExtRateLen) {
		ULONG tmp;

		MakeOutgoingFrame(pBeaconFrame + FrameLen, &tmp,
				  3, LocalErpIe,
				  1, &ExtRateIe,
				  1, &ExtRateLen,
				  ExtRateLen, ExtRate, END_OF_ARGS);
		FrameLen += tmp;
	}
	// If adhoc secruity is set for WPA-None, append the cipher suite IE
	if (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPANone) {
		ULONG tmp;

		if (pAd->PortCfg.WepStatus == Ndis802_11Encryption2Enabled)	// Tkip
		{
			MakeOutgoingFrame(pBeaconFrame + FrameLen, &tmp,
					  1, &WpaIe,
					  1, &CipherSuiteWpaNoneTkipLen,
					  CipherSuiteWpaNoneTkipLen,
					  &CipherSuiteWpaNoneTkip[0],
					  END_OF_ARGS);
			FrameLen += tmp;
		} else if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled)	// Aes
		{
			MakeOutgoingFrame(pBeaconFrame + FrameLen, &tmp,
					  1, &WpaIe,
					  1, &CipherSuiteWpaNoneAesLen,
					  CipherSuiteWpaNoneAesLen,
					  &CipherSuiteWpaNoneAes[0],
					  END_OF_ARGS);
			FrameLen += tmp;
		}
	}
#ifdef BIG_ENDIAN
	RTMPFrameEndianChange(pAd, pBeaconFrame, DIR_WRITE, FALSE);
#endif

	RTMPWriteTxDescriptor(pAd, pTxD, CIPHER_NONE, 0, 0, FALSE, FALSE, TRUE,
			      SHORT_RETRY, IFS_BACKOFF, pAd->PortCfg.MlmeRate,
			      FrameLen, QID_MGMT,
			      PTYPE_SPECIAL | PSUBTYPE_MGMT);

	DBGPRINT(RT_DEBUG_TRACE, "MakeIbssBeacon (len=%d)\n", FrameLen);
	return FrameLen;
}
