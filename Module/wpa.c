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
 *      Module Name: wpa.c
 *
 *      Abstract:
 *
 *      Revision History:
 *      Who             When            What
 *      --------        -----------     -----------------------------
 *      JanL            22nd Jul 03     Initial
 *      PaulL           28th Nov 03     Modify for supplicant
 *      GertjanW        21st Jan 06     Baseline code
 ***************************************************************************/

#include "rt_config.h"

UCHAR CipherWpaPskTkip[] = {
	0xDD, 0x16,		// RSN IE
	0x00, 0x50, 0xf2, 0x01,	// oui
	0x01, 0x00,		// Version
	0x00, 0x50, 0xf2, 0x02,	// Multicast
	0x01, 0x00,		// Number of unicast
	0x00, 0x50, 0xf2, 0x02,	// unicast
	0x01, 0x00,		// number of authentication method
	0x00, 0x50, 0xf2, 0x02	// authentication
};
UCHAR CipherWpaPskTkipLen = (sizeof(CipherWpaPskTkip) / sizeof(UCHAR));

UCHAR CipherWpaPskAes[] = {
	0xDD, 0x16,		// RSN IE
	0x00, 0x50, 0xf2, 0x01,	// oui
	0x01, 0x00,		// Version
	0x00, 0x50, 0xf2, 0x04,	// Multicast
	0x01, 0x00,		// Number of unicast
	0x00, 0x50, 0xf2, 0x04,	// unicast
	0x01, 0x00,		// number of authentication method
	0x00, 0x50, 0xf2, 0x02	// authentication
};
UCHAR CipherWpaPskAesLen = (sizeof(CipherWpaPskAes) / sizeof(UCHAR));

extern UCHAR CipherWpa2Template[];
extern UCHAR CipherWpa2TemplateLen;

static UCHAR prf_input[1024];

#define     WPARSNIE    0xdd
#define     WPA2RSNIE   0x30

#define ROUND_UP(__x, __y) \
	(((unsigned long)((__x)+((__y)-1))) & ((unsigned long)~((__y)-1)))

/*
	========================================================================

	Routine Description:
		Classify WPA EAP message type

	Arguments:
		EAPType		Value of EAP message type
		MsgType		Internal Message definition for MLME state machine

	Return Value:
		TRUE		Found appropriate message type
		FALSE		No appropriate message type

	Note:
		All these constants are defined in wpa.h
		For supplicant, there is only EAPOL Key message avaliable

	========================================================================
*/
BOOLEAN WpaMsgTypeSubst(IN UCHAR EAPType, OUT ULONG * MsgType)
{
	switch (EAPType) {
	case EAPPacket:
		*MsgType = MT2_EAPPacket;
		break;
	case EAPOLStart:
		*MsgType = MT2_EAPOLStart;
		break;
	case EAPOLLogoff:
		*MsgType = MT2_EAPOLLogoff;
		break;
	case EAPOLKey:
		*MsgType = MT2_EAPOLKey;
		break;
	case EAPOLASFAlert:
		*MsgType = MT2_EAPOLASFAlert;
		break;
	default:
		DBGPRINT(RT_DEBUG_INFO, "WpaMsgTypeSubst : return FALSE; \n");
		return FALSE;
	}
	return TRUE;
}

/*
	========================================================================

	Routine Description:
		Init WPA MAC header

	Arguments:
		pAd	Pointer	to our adapter

	Return Value:
		None

	Note:

	========================================================================
*/
static VOID WpaMacHeaderInit(IN PRTMP_ADAPTER pAd,
		      IN OUT PHEADER_802_11 pHdr80211,
		      IN UCHAR wep, IN PUCHAR pAddr1)
{
	memset(pHdr80211, 0, sizeof(HEADER_802_11));
	pHdr80211->FC.Type = BTYPE_DATA;
	pHdr80211->FC.ToDs = 1;
	if (wep == 1)
		pHdr80211->FC.Wep = 1;

	//     Addr1: DA, Addr2: BSSID, Addr3: SA
	memcpy(pHdr80211->Addr1, pAddr1, ETH_ALEN);
	memcpy(pHdr80211->Addr2, pAd->CurrentAddress, ETH_ALEN);
	memcpy(pHdr80211->Addr3, pAd->PortCfg.Bssid, ETH_ALEN);
	pHdr80211->Sequence = pAd->Sequence;
}

/*
	========================================================================

	Routine Description:
		SHA1 function

	Arguments:

	Return Value:

	Note:

	========================================================================
*/
static VOID HMAC_SHA1(IN UCHAR * text,
	       IN UINT text_len,
	       IN UCHAR * key, IN UINT key_len, IN UCHAR * digest)
{
	SHA_CTX context;
	UCHAR k_ipad[65];	/* inner padding - key XORd with ipad       */
	UCHAR k_opad[65];	/* outer padding - key XORd with opad       */
	INT i;

	// if key is longer     than 64 bytes reset     it to key=SHA1(key)
	if (key_len > 64) {
		SHA_CTX tctx;
		SHAInit(&tctx);
		SHAUpdate(&tctx, key, key_len);
		SHAFinal(&tctx, key);
		key_len = 20;
	}
	memset(k_ipad, 0, sizeof(k_ipad));
	memset(k_opad, 0, sizeof(k_opad));
	memcpy(k_ipad, key, key_len);
	memcpy(k_opad, key, key_len);

	// XOR key with ipad and opad values
	for (i = 0; i < 64; i++) {
		k_ipad[i] ^= 0x36;
		k_opad[i] ^= 0x5c;
	}

	// perform inner SHA1
	SHAInit(&context);	/* init context for 1st pass */
	SHAUpdate(&context, k_ipad, 64);	/*      start with inner pad */
	SHAUpdate(&context, text, text_len);	/*      then text of datagram */
	SHAFinal(&context, digest);	/* finish up 1st pass */

	//perform outer SHA1
	SHAInit(&context);	/* init context for 2nd pass */
	SHAUpdate(&context, k_opad, 64);	/*      start with outer pad */
	SHAUpdate(&context, digest, 20);	/*      then results of 1st     hash */
	SHAFinal(&context, digest);	/* finish up 2nd pass */
}

/*
	========================================================================

	Routine Description:
		Count TPTK from PMK

	Arguments:

	Return Value:
		Output		Store the TPTK

	Note:

	========================================================================
*/
static VOID WpaCountPTK(IN UCHAR * PMK,
		 IN UCHAR * ANonce,
		 IN UCHAR * AA,
		 IN UCHAR * SNonce,
		 IN UCHAR * SA, OUT UCHAR * output, IN UINT len)
{
	UCHAR concatenation[76];
	UINT CurrPos = 0;
	UCHAR temp[32];
	UCHAR Prefix[] =
	    { 'P', 'a', 'i', 'r', 'w', 'i', 's', 'e', ' ', 'k', 'e', 'y', ' ',
		'e', 'x', 'p', 'a', 'n', 's', 'i', 'o', 'n'
	};

	memset(temp, 0, sizeof(temp));

	// Get smaller address
	if (memcmp(SA, AA, 6) > 0)
		memcpy(concatenation, AA, 6);
	else
		memcpy(concatenation, SA, 6);
	CurrPos += 6;

	// Get larger address
	if (memcmp(SA, AA, 6) > 0)
		memcpy(&concatenation[CurrPos], SA, 6);
	else
		memcpy(&concatenation[CurrPos], AA, 6);
	CurrPos += 6;

	// Get smaller address
	if (memcmp(ANonce, SNonce, 32) > 1)
		memcpy(&concatenation[CurrPos], SNonce, 32);
	else
		memcpy(&concatenation[CurrPos], ANonce, 32);
	CurrPos += 32;

	// Get larger address
	if (memcmp(ANonce, SNonce, 32) > 0)
		memcpy(&concatenation[CurrPos], ANonce, 32);
	else
		memcpy(&concatenation[CurrPos], SNonce, 32);
	CurrPos += 32;

	PRF(PMK, LEN_MASTER_KEY, Prefix, 22, concatenation, 76, output, len);

}

/*
	========================================================================

	Routine Description:
		Misc function to Generate random number

	Arguments:

	Return Value:

	Note:
		802.1i  Annex F.9

	========================================================================
*/
static VOID GenRandom(IN PRTMP_ADAPTER pAd, OUT UCHAR * random)
{
	INT i, curr;
	UCHAR *local, *KeyCounter;
	UCHAR *result;
	unsigned long CurrentTime;
	UCHAR prefix[] =
	    { 'I', 'n', 'i', 't', ' ', 'C', 'o', 'u', 'n', 't', 'e', 'r' };
	UCHAR *mpool;

	mpool = kmalloc(256, MEM_ALLOC_FLAG);
	if (mpool == NULL)
		return;

	// local Len = 80.
	local = (UCHAR *) ROUND_UP(mpool, 4);
	// KeyCounter Len = 32.
	KeyCounter = (UCHAR *) ROUND_UP(local + 80, 4);
	// result Len = 80.
	result = (UCHAR *) ROUND_UP(KeyCounter + 32, 4);

	memset(result, 0, 80);
	memset(local, 0, 80);
	memset(KeyCounter, 0, 32);
	memcpy(local, pAd->CurrentAddress, ETH_ALEN);

	for (i = 0; i < 32; i++) {
		curr = ETH_ALEN;
		CurrentTime = jiffies;
		memcpy(local, pAd->CurrentAddress, ETH_ALEN);
		curr += ETH_ALEN;
		memcpy(&local[curr], &CurrentTime, sizeof(CurrentTime));
		curr += sizeof(CurrentTime);
		memcpy(&local[curr], result, 32);
		curr += 32;
		memcpy(&local[curr], &i, 2);
		curr += 2;
		PRF(KeyCounter, 32, prefix, 12, local, curr, result, 32);
	}
	memcpy(random, result, 32);
	kfree(mpool);
}

/*
	========================================================================

	Routine Description:
		Process Pairwise key 4-way handshaking

	Arguments:
		pAd	Pointer	to our adapter
		Elem		Message body

	Return Value:
		None

	Note:

	========================================================================
*/
static VOID WpaPairMsg1Action(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	PHEADER_802_11 pHeader;
	UCHAR *PTK;
	UCHAR *digest;
	PUCHAR pOutBuffer = NULL;
	HEADER_802_11 Header_802_11;
	UCHAR AckRate = RATE_2;
	USHORT AckDuration = 0;
	ULONG FrameLen = 0;
	UCHAR EAPHEAD[8] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e };
	PEAPOL_PACKET pMsg1;
	EAPOL_PACKET Packet;
	UCHAR Mic[16];
	UCHAR QosControl[] = { 0x00, 0x00 };
	UCHAR *mpool;

	DBGPRINT(RT_DEBUG_TRACE, "WpaPairMsg1Action ----->\n");

	mpool = kmalloc(256, MEM_ALLOC_FLAG);
	if (mpool == NULL)
		return;

	// PKT Len = 80.
	PTK = (UCHAR *) ROUND_UP(mpool, 4);
	// digest Len = 80;
	digest = (UCHAR *) ROUND_UP(PTK + 80, 4);

	pHeader = (PHEADER_802_11) Elem->Msg;

	// Save Data Length to pDesc for receiving packet, then put in outgoing frame   Data Len fields.
	if ((pAd->PortCfg.bWmmCapable)
	    && (pHeader->FC.SubType == SUBTYPE_QDATA)) {
		pMsg1 =
		    (PEAPOL_PACKET) & Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H +
						LENGTH_WMMQOS_H];
	} else {
		pMsg1 =
		    (PEAPOL_PACKET) & Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];
	}

	// Process message 1 from authenticator
	// Key must be Pairwise key, already verified at callee.
	// 1. Save Replay counter, it will use to verify message 3 and construct message 2
	memcpy(pAd->PortCfg.ReplayCounter, pMsg1->KeyDesc.ReplayCounter,
	       LEN_KEY_DESC_REPLAY);

	// 2. Save ANonce
	memcpy(pAd->PortCfg.ANonce, pMsg1->KeyDesc.KeyNonce,
	       LEN_KEY_DESC_NONCE);

	// TSNonce <--- SNonce
	// Generate random SNonce
	GenRandom(pAd, pAd->PortCfg.SNonce);

	// TPTK <--- Calc PTK(ANonce, TSNonce)
	WpaCountPTK(pAd->PortCfg.PskKey.Key,
		    pAd->PortCfg.ANonce,
		    pAd->PortCfg.Bssid,
		    pAd->PortCfg.SNonce, pAd->CurrentAddress, PTK, LEN_PTK);

	// Save key to PTK entry
	memcpy(pAd->PortCfg.PTK, PTK, LEN_PTK);

	// =====================================
	// Use Priority Ring & MiniportMMRequest
	// =====================================
	pAd->Sequence = ((pAd->Sequence) + 1) & (MAX_SEQ_NUMBER);
	WpaMacHeaderInit(pAd, &Header_802_11, 0, pAd->PortCfg.Bssid);
	if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED))
	    && (pHeader->FC.SubType == SUBTYPE_QDATA))
		Header_802_11.FC.SubType = SUBTYPE_QDATA;

	// ACK size     is 14 include CRC, and its rate is based on real time information
	AckRate = pAd->PortCfg.ExpectedACKRate[pAd->PortCfg.TxRate];
	AckDuration = RTMPCalcDuration(pAd, AckRate, 14);
	Header_802_11.Duration = pAd->PortCfg.Dsifs + AckDuration;

	// Zero message 2 body
	memset(&Packet, 0, sizeof(Packet));
	Packet.Version = EAPOL_VER;
	Packet.Type = EAPOLKey;
	//
	// Message 2 as  EAPOL-Key(0,1,0,0,0,P,0,SNonce,MIC,RSN IE)
	//
	Packet.KeyDesc.Type = RSN_KEY_DESC;
	// 1. Key descriptor version and appropriate RSN IE
	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled) {
		Packet.KeyDesc.KeyInfo.KeyDescVer = 2;
	} else			// TKIP
	{
		Packet.KeyDesc.KeyInfo.KeyDescVer = 1;
	}
	Packet.KeyDesc.KeyDataLen[1] = pAd->PortCfg.RSN_IELen;
	memcpy(Packet.KeyDesc.KeyData, pAd->PortCfg.RSN_IE,
	       pAd->PortCfg.RSN_IELen);

	// Update packet length after decide Key data payload
	Packet.Len[1] =
	    sizeof(KEY_DESCRIPTER) - MAX_LEN_OF_RSNIE +
	    Packet.KeyDesc.KeyDataLen[1];

	// Update Key length
	Packet.KeyDesc.KeyLength[0] = pMsg1->KeyDesc.KeyLength[0];
	Packet.KeyDesc.KeyLength[1] = pMsg1->KeyDesc.KeyLength[1];
	// 2. Key Type PeerKey
	Packet.KeyDesc.KeyInfo.KeyType = 1;

	// 3. KeyMic field presented
	Packet.KeyDesc.KeyInfo.KeyMic = 1;

	// 4. Fill SNonce
	memcpy(Packet.KeyDesc.KeyNonce, pAd->PortCfg.SNonce,
	       LEN_KEY_DESC_NONCE);

	// 5. Key Replay Count
	memcpy(Packet.KeyDesc.ReplayCounter, pAd->PortCfg.ReplayCounter,
	       LEN_KEY_DESC_REPLAY);

#ifdef BIG_ENDIAN
	// recovery original byte order, before forward Elem to another routine
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &Packet.KeyDesc.KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&Packet.KeyDesc.KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
#endif

	// Send EAPOL(0, 1, 0, 0, 0, K, 0, TSNonce, 0, MIC(TPTK), 0)
	// Out buffer for transmitting message 2
	pOutBuffer = kmalloc(MAX_LEN_OF_MLME_BUFFER, MEM_ALLOC_FLAG);	// allocate memory
	if (pOutBuffer == NULL) {
		kfree(mpool);
		return;
	}

	// Prepare EAPOL frame for MIC calculation
	// Be careful, only EAPOL frame is counted for MIC calculation
	MakeOutgoingFrame(pOutBuffer, &FrameLen,
			  Packet.Len[1] + 4, &Packet, END_OF_ARGS);

	// 6. Prepare and Fill MIC value
	memset(Mic, 0, sizeof(Mic));
	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled) {
		HMAC_SHA1(pOutBuffer, FrameLen, PTK, LEN_EAP_MICK, digest);
		memcpy(Mic, digest, LEN_KEY_DESC_MIC);
	} else {
		INT i;
		DBGPRINT_RAW(RT_DEBUG_INFO, " PMK = ");
		for (i = 0; i < 16; i++)
			DBGPRINT_RAW(RT_DEBUG_INFO, "%2x-",
				     pAd->PortCfg.PskKey.Key[i]);

		DBGPRINT_RAW(RT_DEBUG_INFO, "\n PTK = ");
		for (i = 0; i < 64; i++)
			DBGPRINT_RAW(RT_DEBUG_INFO, "%2x-",
				     pAd->PortCfg.PTK[i]);
		DBGPRINT_RAW(RT_DEBUG_INFO, "\n FrameLen = %d\n", FrameLen);

		hmac_md5(PTK, LEN_EAP_MICK, pOutBuffer, FrameLen, Mic);
	}
	memcpy(Packet.KeyDesc.KeyMic, Mic, LEN_KEY_DESC_MIC);

	FrameLen = 0;

	if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED))
	    && (pHeader->FC.SubType == SUBTYPE_QDATA)) {
		MakeOutgoingFrame(pOutBuffer, &FrameLen, sizeof(HEADER_802_11),
				  &Header_802_11, 2, QosControl,
				  sizeof(EAPHEAD), EAPHEAD, Packet.Len[1] + 4,
				  &Packet, END_OF_ARGS);
	} else {
		MakeOutgoingFrame(pOutBuffer, &FrameLen,
				  sizeof(HEADER_802_11), &Header_802_11,
				  sizeof(EAPHEAD), EAPHEAD,
				  Packet.Len[1] + 4, &Packet, END_OF_ARGS);
	}

	// Send using priority queue
	MiniportMMRequest(pAd, pOutBuffer, FrameLen);
	kfree(pOutBuffer);
	kfree(mpool);

	DBGPRINT(RT_DEBUG_TRACE, "WpaPairMsg1Action <-----\n");
}

static VOID Wpa2PairMsg1Action(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	PHEADER_802_11 pHeader;
	UCHAR PTK[80];
	PUCHAR pOutBuffer = NULL;
	HEADER_802_11 Header_802_11;
	UCHAR AckRate = RATE_2;
	USHORT AckDuration = 0;
	ULONG FrameLen = 0;
	UCHAR EAPHEAD[8] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e };
	PEAPOL_PACKET pMsg1;
	EAPOL_PACKET Packet;
	UCHAR Mic[16];
	UCHAR QosControl[] = { 0x00, 0x00 };

	DBGPRINT(RT_DEBUG_TRACE, "Wpa2PairMsg1Action ----->\n");

	pHeader = (PHEADER_802_11) Elem->Msg;

	// Save Data Length to pDesc for receiving packet, then put in outgoing frame   Data Len fields.
	if ((pAd->PortCfg.bWmmCapable)
	    && (pHeader->FC.SubType == SUBTYPE_QDATA))
		pMsg1 =
		    (PEAPOL_PACKET) & Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H +
						LENGTH_WMMQOS_H];
	else
		pMsg1 =
		    (PEAPOL_PACKET) & Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];

	// Process message 1 from authenticator
	// Key must be Pairwise key, already verified at callee.
	// 1. Save Replay counter, it will use to verify message 3 and construct message 2
	memcpy(pAd->PortCfg.ReplayCounter, pMsg1->KeyDesc.ReplayCounter,
	       LEN_KEY_DESC_REPLAY);

	// 2. Save ANonce
	memcpy(pAd->PortCfg.ANonce, pMsg1->KeyDesc.KeyNonce,
	       LEN_KEY_DESC_NONCE);

	// TSNonce <--- SNonce
	// Generate random SNonce
	GenRandom(pAd, pAd->PortCfg.SNonce);

	if (pMsg1->KeyDesc.KeyDataLen[1] > 0) {
		// cached PMKID
	}
	// TPTK <--- Calc PTK(ANonce, TSNonce)
	WpaCountPTK(pAd->PortCfg.PskKey.Key,
		    pAd->PortCfg.ANonce,
		    pAd->PortCfg.Bssid,
		    pAd->PortCfg.SNonce, pAd->CurrentAddress, PTK, LEN_PTK);

	// Save key to PTK entry
	memcpy(pAd->PortCfg.PTK, PTK, LEN_PTK);

	// =====================================
	// Use Priority Ring & MiniportMMRequest
	// =====================================
	pAd->Sequence = ((pAd->Sequence) + 1) & (MAX_SEQ_NUMBER);
	WpaMacHeaderInit(pAd, &Header_802_11, 0, pAd->PortCfg.Bssid);
	if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED))
	    && (pHeader->FC.SubType == SUBTYPE_QDATA))
		Header_802_11.FC.SubType = SUBTYPE_QDATA;

	// ACK size is 14 include CRC, and its rate is based on real time information
	AckRate = pAd->PortCfg.ExpectedACKRate[pAd->PortCfg.TxRate];
	AckDuration = RTMPCalcDuration(pAd, AckRate, 14);
	Header_802_11.Duration = pAd->PortCfg.Dsifs + AckDuration;

	// Zero message 2 body
	memset(&Packet, 0, sizeof(Packet));
	Packet.Version = EAPOL_VER;
	Packet.Type = EAPOLKey;
	//
	// Message 2 as  EAPOL-Key(0,1,0,0,0,P,0,SNonce,MIC,RSN IE)
	//
	Packet.KeyDesc.Type = WPA2_KEY_DESC;
	// 1. Key descriptor version and appropriate RSN IE
	memcpy(Packet.KeyDesc.KeyData, pAd->PortCfg.RSN_IE,
	       pAd->PortCfg.RSN_IELen);
	Packet.KeyDesc.KeyDataLen[1] = pAd->PortCfg.RSN_IELen;

	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled) {
		Packet.KeyDesc.KeyInfo.KeyDescVer = 2;
	} else			// TKIP
	{
		Packet.KeyDesc.KeyInfo.KeyDescVer = 1;
	}
	// Update packet length after decide Key data payload
	Packet.Len[1] =
	    sizeof(KEY_DESCRIPTER) - MAX_LEN_OF_RSNIE +
	    Packet.KeyDesc.KeyDataLen[1];

	// 2. Key Type PeerKey
	Packet.KeyDesc.KeyInfo.KeyType = 1;

	// 3. KeyMic field presented
	Packet.KeyDesc.KeyInfo.KeyMic = 1;

	// Update Key Length
	Packet.KeyDesc.KeyLength[0] = 0;
	Packet.KeyDesc.KeyLength[1] = pMsg1->KeyDesc.KeyLength[1];

	// 4. Fill SNonce
	memcpy(Packet.KeyDesc.KeyNonce, pAd->PortCfg.SNonce,
	       LEN_KEY_DESC_NONCE);

	// 5. Key Replay Count
	memcpy(Packet.KeyDesc.ReplayCounter, pAd->PortCfg.ReplayCounter,
	       LEN_KEY_DESC_REPLAY);

#ifdef BIG_ENDIAN
	// recovery original byte order, before forward Elem to another routine
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &Packet.KeyDesc.KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&Packet.KeyDesc.KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
#endif

	// Send EAPOL(0, 1, 0, 0, 0, K, 0, TSNonce, 0, MIC(TPTK), 0)
	// Out buffer for transmitting message 2
	pOutBuffer = kmalloc(MAX_LEN_OF_MLME_BUFFER, MEM_ALLOC_FLAG);	// allocate memory
	if (pOutBuffer == NULL)
		return;

	// Prepare EAPOL frame for MIC calculation
	// Be careful, only EAPOL frame is counted for MIC calculation

	MakeOutgoingFrame(pOutBuffer, &FrameLen,
			  Packet.Len[1] + 4, &Packet, END_OF_ARGS);

	// 6. Prepare and Fill MIC value
	memset(Mic, 0, sizeof(Mic));
	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled) {
		// AES
		UCHAR digest[32];	// SHA1_DIGEST_SIZE =20

		HMAC_SHA1(pOutBuffer, FrameLen, PTK, LEN_EAP_MICK, digest);
		memcpy(Mic, digest, LEN_KEY_DESC_MIC);
	} else {
		hmac_md5(PTK, LEN_EAP_MICK, pOutBuffer, FrameLen, Mic);
	}
	memcpy(Packet.KeyDesc.KeyMic, Mic, LEN_KEY_DESC_MIC);

	FrameLen = 0;
	// Make  Transmitting frame
	if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED))
	    && (pHeader->FC.SubType == SUBTYPE_QDATA)) {
		MakeOutgoingFrame(pOutBuffer, &FrameLen, sizeof(HEADER_802_11),
				  &Header_802_11, 2, QosControl,
				  sizeof(EAPHEAD), EAPHEAD, Packet.Len[1] + 4,
				  &Packet, END_OF_ARGS);
	} else {
		MakeOutgoingFrame(pOutBuffer, &FrameLen,
				  sizeof(HEADER_802_11), &Header_802_11,
				  sizeof(EAPHEAD), EAPHEAD,
				  Packet.Len[1] + 4, &Packet, END_OF_ARGS);
	}

	// Send using priority queue
	MiniportMMRequest(pAd, pOutBuffer, FrameLen);
	kfree(pOutBuffer);

	DBGPRINT(RT_DEBUG_TRACE, "Wpa2PairMsg1Action <-----\n");

}

/*
	========================================================================

	Routine Description:
		Process Pairwise key 4-way handshaking

	Arguments:
		pAd	Pointer	to our adapter
		Elem		Message body

	Return Value:
		None

	Note:

	========================================================================
*/
static VOID WpaPairMsg3Action(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	PHEADER_802_11 pHeader;
	PUCHAR pOutBuffer = NULL;
	HEADER_802_11 Header_802_11;
	UCHAR AckRate = RATE_2;
	USHORT AckDuration = 0;
	ULONG FrameLen = 0;
	UCHAR EAPHEAD[8] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e };
	EAPOL_PACKET Packet;
	PEAPOL_PACKET pMsg3;
	UCHAR Mic[16], OldMic[16];
	PNDIS_802_11_KEY pPeerKey;
	UCHAR QosControl[] = { 0x00, 0x00 };

	DBGPRINT(RT_DEBUG_TRACE, "WpaPairMsg3Action ----->\n");

	pHeader = (PHEADER_802_11) Elem->Msg;

	// Process message 3 frame.
	if ((pAd->PortCfg.bWmmCapable)
	    && (pHeader->FC.SubType == SUBTYPE_QDATA))
		pMsg3 =
		    (PEAPOL_PACKET) & Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H +
						LENGTH_WMMQOS_H];
	else
		pMsg3 =
		    (PEAPOL_PACKET) & Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];

#ifdef BIG_ENDIAN
	// recovery original byte order, before forward Elem to another routine
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &pMsg3->KeyDesc.KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&pMsg3->KeyDesc.KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
#endif

	// 1. Verify RSN IE & cipher type match
	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled
	    && (pMsg3->KeyDesc.KeyInfo.KeyDescVer != 2)) {
		return;
	} else if (pAd->PortCfg.WepStatus == Ndis802_11Encryption2Enabled
		   && (pMsg3->KeyDesc.KeyInfo.KeyDescVer != 1)) {
		return;
	}
#ifdef BIG_ENDIAN
	// recovery original byte order, before forward Elem to another routine
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &pMsg3->KeyDesc.KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&pMsg3->KeyDesc.KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
#endif

	// 2. Check MIC value
	// Save the MIC and replace with zero
	memcpy(OldMic, pMsg3->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);
	memset(pMsg3->KeyDesc.KeyMic, 0, LEN_KEY_DESC_MIC);
	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled) {
		// AES
		UCHAR digest[32];	// SHA1_DIGEST_SIZE =20

		HMAC_SHA1((PUCHAR) pMsg3, pMsg3->Len[1] + 4, pAd->PortCfg.PTK,
			  LEN_EAP_MICK, digest);
		memcpy(Mic, digest, LEN_KEY_DESC_MIC);
	} else {
		hmac_md5(pAd->PortCfg.PTK, LEN_EAP_MICK, (PUCHAR) pMsg3,
			 pMsg3->Len[1] + 4, Mic);
	}

	if (memcmp(OldMic, Mic, LEN_KEY_DESC_MIC) != 0) {
		DBGPRINT(RT_DEBUG_ERROR,
			 " MIC Different in msg 3 of 4-way handshake!!!!!!!!!! \n");
		return;
	} else
		DBGPRINT(RT_DEBUG_TRACE,
			 " MIC VALID in msg 3 of 4-way handshake!!!!!!!!!! \n");

	// 3. Check Replay Counter, it has to be larger than last one. No need to be exact one larger
	if (memcmp
	    (pMsg3->KeyDesc.ReplayCounter, pAd->PortCfg.ReplayCounter,
	     LEN_KEY_DESC_REPLAY) <= 0)
		return;

	// Update new replay counter
	memcpy(pAd->PortCfg.ReplayCounter, pMsg3->KeyDesc.ReplayCounter,
	       LEN_KEY_DESC_REPLAY);

	// 4. Double check ANonce
	if (memcmp
	    (pAd->PortCfg.ANonce, pMsg3->KeyDesc.KeyNonce,
	     LEN_KEY_DESC_NONCE) != 0)
		return;

	// 5. Construct Message 4
	// =====================================
	// Use Priority Ring & MiniportMMRequest
	// =====================================
	pAd->Sequence = ((pAd->Sequence) + 1) & (MAX_SEQ_NUMBER);
	WpaMacHeaderInit(pAd, &Header_802_11, 0, pAd->PortCfg.Bssid);
	if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED))
	    && (pHeader->FC.SubType == SUBTYPE_QDATA))
		Header_802_11.FC.SubType = SUBTYPE_QDATA;

	// ACK size     is 14 include CRC, and its rate is based on real time information
	AckRate = pAd->PortCfg.ExpectedACKRate[pAd->PortCfg.TxRate];
	AckDuration = RTMPCalcDuration(pAd, AckRate, 14);
	Header_802_11.Duration = pAd->PortCfg.Dsifs + AckDuration;

	// Zero message 4 body
	memset(&Packet, 0, sizeof(Packet));
	Packet.Version = EAPOL_VER;
	Packet.Type = EAPOLKey;
	Packet.Len[1] = sizeof(KEY_DESCRIPTER) - MAX_LEN_OF_RSNIE;	// No data field

	//
	// Message 4 as  EAPOL-Key(0,1,0,0,0,P,0,0,MIC,0)
	//
	Packet.KeyDesc.Type = RSN_KEY_DESC;

#ifdef BIG_ENDIAN
	// recovery original byte order, before forward Elem to another routine
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &pMsg3->KeyDesc.KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&pMsg3->KeyDesc.KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
#endif

	// Key descriptor version and appropriate RSN IE
	Packet.KeyDesc.KeyInfo.KeyDescVer = pMsg3->KeyDesc.KeyInfo.KeyDescVer;

	// Update Key Length
	Packet.KeyDesc.KeyLength[0] = pMsg3->KeyDesc.KeyLength[0];
	Packet.KeyDesc.KeyLength[1] = pMsg3->KeyDesc.KeyLength[1];

	// Key Type PeerKey
	Packet.KeyDesc.KeyInfo.KeyType = 1;

	// KeyMic field presented
	Packet.KeyDesc.KeyInfo.KeyMic = 1;

	// In Msg3,  KeyInfo.secure =0 if Group Key HS to come. 1 if no group key HS
	// Station sends Msg4  KeyInfo.secure should be the same as that in Msg.3
	Packet.KeyDesc.KeyInfo.Secure = pMsg3->KeyDesc.KeyInfo.Secure;

	// Key Replay count
	memcpy(Packet.KeyDesc.ReplayCounter, pMsg3->KeyDesc.ReplayCounter,
	       LEN_KEY_DESC_REPLAY);

#ifdef BIG_ENDIAN
	// recovery original byte order, before forward Elem to another routine
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &Packet.KeyDesc.KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&Packet.KeyDesc.KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
#endif

	// Out buffer for transmitting message 4
	pOutBuffer = kmalloc(MAX_LEN_OF_MLME_BUFFER, MEM_ALLOC_FLAG);	// allocate memory
	if (pOutBuffer == NULL)
		return;

	// Prepare EAPOL frame for MIC calculation
	// Be careful, only EAPOL frame is counted for MIC calculation
	MakeOutgoingFrame(pOutBuffer, &FrameLen,
			  Packet.Len[1] + 4, &Packet, END_OF_ARGS);

	// Prepare and Fill MIC value
	memset(Mic, 0, sizeof(Mic));
	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled) {
		// AES
		UCHAR digest[32];	// SHA1_DIGEST_SIZE =20

		HMAC_SHA1(pOutBuffer, FrameLen, pAd->PortCfg.PTK, LEN_EAP_MICK,
			  digest);
		memcpy(Mic, digest, LEN_KEY_DESC_MIC);
	} else {
		hmac_md5(pAd->PortCfg.PTK, LEN_EAP_MICK, pOutBuffer, FrameLen,
			 Mic);
	}
	memcpy(Packet.KeyDesc.KeyMic, Mic, LEN_KEY_DESC_MIC);

	FrameLen = 0;
	// Make  Transmitting frame
	if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED))
	    && (pHeader->FC.SubType == SUBTYPE_QDATA)) {

		MakeOutgoingFrame(pOutBuffer, &FrameLen,
				  sizeof(HEADER_802_11), &Header_802_11,
				  2, QosControl,
				  sizeof(EAPHEAD), EAPHEAD,
				  Packet.Len[1] + 4, &Packet, END_OF_ARGS);
	} else {
		MakeOutgoingFrame(pOutBuffer, &FrameLen,
				  sizeof(HEADER_802_11), &Header_802_11,
				  sizeof(EAPHEAD), EAPHEAD,
				  Packet.Len[1] + 4, &Packet, END_OF_ARGS);
	}

	// 7. Update PTK
	pPeerKey = kmalloc(MAX_LEN_OF_MLME_BUFFER, MEM_ALLOC_FLAG);	// allocate memory
	if (pPeerKey == NULL) {
		kfree(pOutBuffer);
		return;
	}

	memset(pPeerKey, 0, sizeof(NDIS_802_11_KEY) + LEN_EAP_KEY);
	pPeerKey->Length = sizeof(NDIS_802_11_KEY) + LEN_EAP_KEY;
	pPeerKey->KeyIndex = 0xe0000000;
	pPeerKey->KeyLength =
	    pMsg3->KeyDesc.KeyLength[0] * 256 + pMsg3->KeyDesc.KeyLength[1];

	memcpy(pPeerKey->BSSID, pAd->PortCfg.Bssid, ETH_ALEN);
	memcpy(&pPeerKey->KeyRSC, pMsg3->KeyDesc.KeyRsc, LEN_KEY_DESC_RSC);
	memcpy(pPeerKey->KeyMaterial, &pAd->PortCfg.PTK[32], LEN_EAP_KEY);
	// Call Add peer key function
	RTMPWPAAddKeyProc(pAd, pPeerKey);
	kfree(pPeerKey);

	// 6. Send Message 4 to authenticator
	// Send using priority queue
	MiniportMMRequest(pAd, pOutBuffer, FrameLen);
	kfree(pOutBuffer);

	DBGPRINT(RT_DEBUG_TRACE, "WpaPairMsg3Action <-----\n");
}

/*
    ========================================================================

    Routine Description:
        Misc function to decrypt AES body

    Arguments:

    Return Value:

    Note:
        This function references to RFC 3394 for aes key unwrap algorithm.
    ========================================================================
*/
static VOID AES_GTK_KEY_UNWRAP(IN UCHAR * key,
			OUT UCHAR * plaintext,
			IN UCHAR c_len, IN UCHAR * ciphertext)
{
	UCHAR *A, *BIN, *BOUT;
	UCHAR xor;
	INT i, j;
	aes_context aesctx;
	UCHAR *R;
	INT num_blocks = c_len / 8;	// unit:64bits
	UCHAR *mpool;

	mpool = kmalloc(1024, MEM_ALLOC_FLAG);
	if (mpool == NULL)
		return;

	//A len = 8.
	A = (UCHAR *) ROUND_UP(mpool, 4);
	// BIN len = 16.
	BIN = (UCHAR *) ROUND_UP(A + 8, 4);
	// BOUT len = 16.
	BOUT = (UCHAR *) ROUND_UP(BIN + 16, 4);
	// R len = 512
	R = (UCHAR *) ROUND_UP(BOUT + 16, 4);

	// Initialize
	memcpy(A, ciphertext, 8);
	//Input plaintext
	for (i = 0; i < (c_len - 8); i++) {
		R[i] = ciphertext[i + 8];
	}

	aes_set_key(&aesctx, key, 128);

	for (j = 5; j >= 0; j--) {
		for (i = (num_blocks - 1); i > 0; i--) {
			xor = (num_blocks - 1) * j + i;
			memcpy(BIN, A, 8);
			BIN[7] = A[7] ^ xor;
			memcpy(&BIN[8], &R[(i - 1) * 8], 8);
			aes_decrypt(&aesctx, BIN, BOUT);
			memcpy(A, &BOUT[0], 8);
			memcpy(&R[(i - 1) * 8], &BOUT[8], 8);
		}
	}

	// OUTPUT
	for (i = 0; i < c_len; i++) {
		plaintext[i] = R[i];
	}

	DBGPRINT_RAW(RT_DEBUG_TRACE, "plaintext = \n");
	for (i = 0; i < (num_blocks * 8); i++) {
		DBGPRINT_RAW(RT_DEBUG_TRACE, "%2x ", plaintext[i]);
		if (i % 16 == 15)
			DBGPRINT_RAW(RT_DEBUG_TRACE, "\n ");
	}
	DBGPRINT_RAW(RT_DEBUG_TRACE, "\n  \n");

	kfree(mpool);
}

/*
    ========================================================================

    Routine Description:
    Parse KEYDATA field.  KEYDATA[] May contain 2 RSN IE and optionally GTK.
    GTK  is encaptulated in KDE format at  p.83 802.11i D10

    Arguments:

    Return Value:

    Note:
        802.11i D10

    ========================================================================
*/
static VOID ParseKeyData(IN PRTMP_ADAPTER pAd, IN PUCHAR pKeyData, IN UCHAR KeyDataLen)
{
	PKDE_ENCAP pKDE = NULL;
	PNDIS_802_11_KEY pGroupKey = NULL;
	PUCHAR pMyKeyData = pKeyData;
	UCHAR KeyDataLength = KeyDataLen;
	UCHAR GTKLEN;
	INT i;
	ULONG Idx;

	PUCHAR pVIE = NULL;
	UCHAR Len;
	PEID_STRUCT pEid;

	if ((Idx = BssTableSearch(&pAd->ScanTab, pAd->PortCfg.Bssid,
				  pAd->PortCfg.Channel)) == BSS_NOT_FOUND) {
		DBGPRINT(RT_DEBUG_ERROR, "%s, Can't find BSS\n", __FUNCTION__);
		return;
	}

	pVIE = pAd->ScanTab.BssEntry[Idx].VarIEs;
	Len = (UCHAR) pAd->ScanTab.BssEntry[Idx].VarIELen;
	while (Len > 0) {
		pEid = (PEID_STRUCT) pVIE;
		if (pEid->Eid != IE_RSN) {
			pVIE += (pEid->Len + 2);
			Len -= (pEid->Len + 2);
			continue;
		}

		if (memcmp(pKeyData, pEid->Octet, pEid->Len) != 0) {
			DBGPRINT(RT_DEBUG_ERROR, " RSN IE mismatched !!!!!!!!!! \n");
		} else {
			DBGPRINT(RT_DEBUG_TRACE, " RSN IE matched !!!!!!!!!! \n");
		}

		Len -= (pEid->Len + 2);
		break;
	}

	DBGPRINT(RT_DEBUG_ERROR, "KeyDataLen = %d  \n", KeyDataLen);

/*
====================================================================
======================================================================
*/
	if ((*pKeyData == WPARSNIE) && (*(pKeyData + 1) != 0)
	    && (KeyDataLength >= (2 + *(pKeyData + 1)))) {
		pMyKeyData = pKeyData + *(pKeyData + 1) + 2;
		KeyDataLength -= (2 + *(pKeyData + 1));
		DBGPRINT_RAW(RT_DEBUG_TRACE,
			     "WPA RSN IE length %d contained in Msg3 = \n",
			     (2 + *(pKeyData + 1)));
	}
	if ((*pMyKeyData == WPA2RSNIE) && (*(pMyKeyData + 1) != 0)
	    && (KeyDataLength >= (2 + *(pMyKeyData + 1)))) {
		pMyKeyData += (*(pMyKeyData + 1) + 2);
		KeyDataLength -= (2 + *(pMyKeyData + 1));
		DBGPRINT_RAW(RT_DEBUG_TRACE,
			     "WPA2 RSN IE length %d contained in Msg3 = \n",
			     (2 + *(pMyKeyData + 1)));
	}

	DBGPRINT_RAW(RT_DEBUG_TRACE, "KeyDataLength %d   \n", KeyDataLength);

	pKDE = (PKDE_ENCAP) pMyKeyData;
	if ((KeyDataLength >= 8) && (KeyDataLength <= sizeof(KDE_ENCAP))) {
		DBGPRINT_RAW(RT_DEBUG_TRACE, "pKDE = \n");
		DBGPRINT_RAW(RT_DEBUG_TRACE, "pKDE->Type %x:", pKDE->Type);
		DBGPRINT_RAW(RT_DEBUG_TRACE, "pKDE->Len 0x%x:", pKDE->Len);
		DBGPRINT_RAW(RT_DEBUG_TRACE, "pKDE->OUI %x %x %x :",
			     pKDE->OUI[0], pKDE->OUI[1], pKDE->OUI[2]);
		DBGPRINT_RAW(RT_DEBUG_TRACE, "\n");
	}

	if (pKDE->GTKEncap.Kid == 0) {
		DBGPRINT_RAW(RT_DEBUG_ERROR, "GTK Key index zero , error\n");
		return;
	}

	GTKLEN = pKDE->Len - 6;

	DBGPRINT_RAW(RT_DEBUG_TRACE, "GTK Key[%d] len=%d ", pKDE->GTKEncap.Kid,
		     GTKLEN);
	for (i = 0; i < GTKLEN; i++) {
		DBGPRINT_RAW(RT_DEBUG_TRACE, "%02x:", pKDE->GTKEncap.GTK[i]);
	}
	DBGPRINT_RAW(RT_DEBUG_TRACE, "\n");

	// Update GTK
	pGroupKey = kmalloc(MAX_LEN_OF_MLME_BUFFER, MEM_ALLOC_FLAG);	// allocate memory
	if (pGroupKey == NULL)
		return;

	memset(pGroupKey, 0, sizeof(NDIS_802_11_KEY) + LEN_EAP_KEY);
	pGroupKey->Length = sizeof(NDIS_802_11_KEY) + LEN_EAP_KEY;
	pGroupKey->KeyIndex = 0x20000000 | pKDE->GTKEncap.Kid;
	pGroupKey->KeyLength = GTKLEN;

	memcpy(pGroupKey->BSSID, pAd->PortCfg.Bssid, ETH_ALEN);
	memcpy(pGroupKey->KeyMaterial, pKDE->GTKEncap.GTK, 32);

	// Call Add peer key function
	RTMPWPAAddKeyProc(pAd, pGroupKey);

	kfree(pGroupKey);

}


static VOID WPAMake8023Hdr(IN PRTMP_ADAPTER pAd, IN PCHAR pDAddr, IN OUT PCHAR pHdr)
{
	// Addr1: DA, Addr2: BSSID, Addr3: SA
	memcpy(pHdr, pDAddr, ETH_ALEN);
	memcpy(&pHdr[ETH_ALEN], pAd->CurrentAddress, ETH_ALEN);
	pHdr[2 * ETH_ALEN] = 0x88;
	pHdr[2 * ETH_ALEN + 1] = 0x8e;

}

/*
    ========================================================================
    Routine Description:
       Send all EAP frames to wireless station.
       These frames don't come from normal SendPackets routine, but are EAPPacket, EAPOL,

    Arguments:
        pRxD        Pointer to the Rx descriptor

    Return Value:
    None
    ========================================================================
*/
static VOID RTMPToWirelessSta(IN PRTMP_ADAPTER pAd, IN PUCHAR pFrame, IN UINT FrameLen)
{
	struct sk_buff *skb;
	NDIS_STATUS Status;
	UCHAR Index;

	do {
		// 1. build a NDIS packet and call RTMPSendPacket();
		//    be careful about how/when to release this internal allocated NDIS PACKET buffer
		if ((skb =
		     __dev_alloc_skb(FrameLen + 2,
				     GFP_DMA | GFP_ATOMIC)) != NULL) {
			skb->len = FrameLen;
			skb->data_len = FrameLen;
			memcpy((skb->data), pFrame, FrameLen);
		} else {
			break;
		}

		RTMP_SET_PACKET_SOURCE(skb, PKTSRC_DRIVER);

		// 2. send out the packet
		Status = RTMPSendPacket(pAd, skb);
		if (Status == NDIS_STATUS_SUCCESS) {
			// Dequeue one frame from TxSwQueue0..3 queue and process it
			// There are three place calling dequeue for TX ring.
			// 1. Here, right after queueing the frame.
			// 2. At the end of TxRingTxDone service routine.
			// 3. Upon NDIS call RTMPSendPackets
			if ((!RTMP_TEST_FLAG
			     (pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
			    &&
			    (!RTMP_TEST_FLAG
			     (pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS))) {
				for (Index = 0; Index < 5; Index++) {
					if (!skb_queue_empty(&pAd->TxSwQueue[Index]))
						RTMPDeQueuePacket(pAd, Index);
				}
			}
		} else		// free this packet space
		{
			dev_kfree_skb_irq(skb);
		}

	} while (FALSE);

}


static VOID Wpa2PairMsg3Action(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	PHEADER_802_11 pHeader;
	PUCHAR pOutBuffer = NULL;
	HEADER_802_11 Header_802_11;
	UCHAR AckRate = RATE_2;
	USHORT AckDuration = 0;
	ULONG FrameLen = 0;
	UCHAR EAPHEAD[8] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e };
	EAPOL_PACKET Packet;
	PEAPOL_PACKET pMsg3;
	UCHAR *Mic, *OldMic;
	PNDIS_802_11_KEY pPeerKey;
	UCHAR *KEYDATA, *Key;
	UCHAR QosControl[] = { 0x00, 0x00 };
	UCHAR *mpool;

	mpool = kmalloc(640, MEM_ALLOC_FLAG);
	if (mpool == NULL)
		return;

	// Mic Len = 16.
	Mic = (UCHAR *) ROUND_UP(mpool, 4);
	// OldMic Len = 16.
	OldMic = (UCHAR *) ROUND_UP(Mic + 16, 4);
	// KEYDATA Len = 512.
	KEYDATA = (UCHAR *) ROUND_UP(OldMic + 16, 4);
	// Key Len = 32.
	Key = (UCHAR *) ROUND_UP(KEYDATA + 512, 8);

	DBGPRINT(RT_DEBUG_TRACE, "Wpa2PairMsg3Action ----->\n");

	pHeader = (PHEADER_802_11) Elem->Msg;

	// Process message 3 frame.
	if ((pAd->PortCfg.bWmmCapable)
	    && (pHeader->FC.SubType == SUBTYPE_QDATA))
		pMsg3 =
		    (PEAPOL_PACKET) & Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H +
						LENGTH_WMMQOS_H];
	else
		pMsg3 =
		    (PEAPOL_PACKET) & Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];

#ifdef BIG_ENDIAN
	// recovery original byte order, before forward Elem to another routine
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &pMsg3->KeyDesc.KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&pMsg3->KeyDesc.KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
#endif

	// 1. Verify RSN IE & cipher type match
	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled
	    && (pMsg3->KeyDesc.KeyInfo.KeyDescVer != 2)) {
		kfree(mpool);
		return;
	} else if (pAd->PortCfg.WepStatus == Ndis802_11Encryption2Enabled
		   && (pMsg3->KeyDesc.KeyInfo.KeyDescVer != 1)) {
		kfree(mpool);
		return;
	}
#ifdef BIG_ENDIAN
	// recovery original byte order, before forward Elem to another routine
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &pMsg3->KeyDesc.KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&pMsg3->KeyDesc.KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
#endif

	// 2. Check MIC value
	// Save the MIC and replace with zero
	memcpy(OldMic, pMsg3->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);
	memset(pMsg3->KeyDesc.KeyMic, 0, LEN_KEY_DESC_MIC);
	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled) {
		// AES
		UCHAR digest[32];	// SHA1_DIGEST_SIZE =20

		HMAC_SHA1((PUCHAR) pMsg3, pMsg3->Len[1] + 4, pAd->PortCfg.PTK,
			  LEN_EAP_MICK, digest);
		memcpy(Mic, digest, LEN_KEY_DESC_MIC);
	} else {
		hmac_md5(pAd->PortCfg.PTK, LEN_EAP_MICK, (PUCHAR) pMsg3,
			 pMsg3->Len[1] + 4, Mic);
	}

	if (memcmp(OldMic, Mic, LEN_KEY_DESC_MIC) != 0) {
		DBGPRINT(RT_DEBUG_ERROR,
			 " MIC Different in msg 3 of 4-way handshake!!!!!!!!!! \n");
		kfree(mpool);
		return;
	} else
		DBGPRINT(RT_DEBUG_TRACE,
			 " MIC VALID in msg 3 of 4-way handshake!!!!!!!!!! \n");

	// 3. Check Replay Counter, it has to be larger than last one. No need to be exact one larger
	if (memcmp(pMsg3->KeyDesc.ReplayCounter, pAd->PortCfg.ReplayCounter,
	           LEN_KEY_DESC_REPLAY) <= 0) {
		kfree(mpool);
		return;
	}

	// Update new replay counter
	memcpy(pAd->PortCfg.ReplayCounter, pMsg3->KeyDesc.ReplayCounter,
	       LEN_KEY_DESC_REPLAY);

	// 4. Double check ANonce
	if (memcmp(pAd->PortCfg.ANonce, pMsg3->KeyDesc.KeyNonce, LEN_KEY_DESC_NONCE) != 0) {
		kfree(mpool);
		return;
	}

	// Obtain GTK
	// 5. Decrypt GTK from Key Data
	DBGPRINT_RAW(RT_DEBUG_TRACE, "EKD = %d\n",
		     pMsg3->KeyDesc.KeyInfo.EKD_DL);
	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled) {
		// Decrypt AES GTK
		AES_GTK_KEY_UNWRAP(&pAd->PortCfg.PTK[16], KEYDATA,
				   pMsg3->KeyDesc.KeyDataLen[1],
				   pMsg3->KeyDesc.KeyData);

		ParseKeyData(pAd, KEYDATA, pMsg3->KeyDesc.KeyDataLen[1]);
	} else			// TKIP
	{
		INT i;
		// Decrypt TKIP GTK
		// Construct 32 bytes RC4 Key
		memcpy(Key, pMsg3->KeyDesc.KeyIv, 16);
		memcpy(&Key[16], &pAd->PortCfg.PTK[16], 16);
		ARCFOUR_INIT(&pAd->PrivateInfo.WEPCONTEXT, Key, 32);
		//discard first 256 bytes
		for (i = 0; i < 256; i++)
			ARCFOUR_BYTE(&pAd->PrivateInfo.WEPCONTEXT);
		// Decrypt GTK. Becareful, there is no ICV to check the result is correct or not
		ARCFOUR_DECRYPT(&pAd->PrivateInfo.WEPCONTEXT, KEYDATA,
				pMsg3->KeyDesc.KeyData,
				pMsg3->KeyDesc.KeyDataLen[1]);

		DBGPRINT_RAW(RT_DEBUG_TRACE, "KEYDATA = \n");
		for (i = 0; i < 100; i++) {
			DBGPRINT_RAW(RT_DEBUG_TRACE, "%2x ", KEYDATA[i]);
			if (i % 16 == 15)
				DBGPRINT_RAW(RT_DEBUG_TRACE, "\n ");
		}
		DBGPRINT_RAW(RT_DEBUG_TRACE, "\n  \n");

		ParseKeyData(pAd, KEYDATA, pMsg3->KeyDesc.KeyDataLen[1]);

	}

	// 6. Construct Message 4
	// =====================================
	// Use Priority Ring & MiniportMMRequest
	// =====================================
	pAd->Sequence = ((pAd->Sequence) + 1) & (MAX_SEQ_NUMBER);
	WpaMacHeaderInit(pAd, &Header_802_11, 0, pAd->PortCfg.Bssid);
	if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED))
	    && (pHeader->FC.SubType == SUBTYPE_QDATA))
		Header_802_11.FC.SubType = SUBTYPE_QDATA;

	// ACK size is 14 include CRC, and its rate is based on real time information
	AckRate = pAd->PortCfg.ExpectedACKRate[pAd->PortCfg.TxRate];
	AckDuration = RTMPCalcDuration(pAd, AckRate, 14);
	Header_802_11.Duration = pAd->PortCfg.Dsifs + AckDuration;

	// Zero message 4 body
	memset(&Packet, 0, sizeof(Packet));
	Packet.Version = EAPOL_VER;
	Packet.Type = EAPOLKey;
	Packet.Len[1] = sizeof(KEY_DESCRIPTER) - MAX_LEN_OF_RSNIE;	// No data field

	//
	// Message 4 as  EAPOL-Key(0,1,0,0,0,P,0,0,MIC,0)
	//
	Packet.KeyDesc.Type = RSN_KEY_DESC;

#ifdef BIG_ENDIAN
	// recovery original byte order, before forward Elem to another routine
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &pMsg3->KeyDesc.KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&pMsg3->KeyDesc.KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
#endif

	// Key descriptor version and appropriate RSN IE
	Packet.KeyDesc.KeyInfo.KeyDescVer = pMsg3->KeyDesc.KeyInfo.KeyDescVer;

	// Update Key Length
	Packet.KeyDesc.KeyLength[0] = pMsg3->KeyDesc.KeyLength[0];
	Packet.KeyDesc.KeyLength[1] = pMsg3->KeyDesc.KeyLength[1];

	// Key Type PeerKey
	Packet.KeyDesc.KeyInfo.KeyType = 1;

	// KeyMic field presented
	Packet.KeyDesc.KeyInfo.KeyMic = 1;
	Packet.KeyDesc.KeyInfo.Secure = 1;

	// Key Replay count
	memcpy(Packet.KeyDesc.ReplayCounter, pMsg3->KeyDesc.ReplayCounter,
	       LEN_KEY_DESC_REPLAY);

#ifdef BIG_ENDIAN
	// recovery original byte order, before forward Elem to another routine
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &Packet.KeyDesc.KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&Packet.KeyDesc.KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
#endif

	// Out buffer for transmitting message 4
	pOutBuffer = kmalloc(MAX_LEN_OF_MLME_BUFFER, MEM_ALLOC_FLAG);	// allocate memory
	if (pOutBuffer == NULL) {
		kfree(mpool);
		return;
	}

	// Prepare EAPOL frame for MIC calculation
	// Be careful, only EAPOL frame is counted for MIC calculation
	MakeOutgoingFrame(pOutBuffer, &FrameLen,
			  Packet.Len[1] + 4, &Packet, END_OF_ARGS);

	// Prepare and Fill MIC value
	memset(Mic, 0, sizeof(Mic));
	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled) {
		// AES
		UCHAR digest[32];	// SHA1_DIGEST_SIZE =20

		HMAC_SHA1(pOutBuffer, FrameLen, pAd->PortCfg.PTK, LEN_EAP_MICK,
			  digest);
		memcpy(Mic, digest, LEN_KEY_DESC_MIC);
	} else {
		hmac_md5(pAd->PortCfg.PTK, LEN_EAP_MICK, pOutBuffer, FrameLen,
			 Mic);
	}
	memcpy(Packet.KeyDesc.KeyMic, Mic, LEN_KEY_DESC_MIC);

	FrameLen = 0;

	// Make  Transmitting frame
	if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED))
	    && (pHeader->FC.SubType == SUBTYPE_QDATA)) {

		MakeOutgoingFrame(pOutBuffer, &FrameLen,
				  sizeof(HEADER_802_11), &Header_802_11,
				  2, QosControl,
				  sizeof(EAPHEAD), EAPHEAD,
				  Packet.Len[1] + 4, &Packet, END_OF_ARGS);
	} else {
		MakeOutgoingFrame(pOutBuffer, &FrameLen,
				  sizeof(HEADER_802_11), &Header_802_11,
				  sizeof(EAPHEAD), EAPHEAD,
				  Packet.Len[1] + 4, &Packet, END_OF_ARGS);
	}

	// 7. Update PTK
	pPeerKey = kmalloc(MAX_LEN_OF_MLME_BUFFER, MEM_ALLOC_FLAG);	// allocate memory
	if (pPeerKey == NULL) {
		kfree(pOutBuffer);
		kfree(mpool);
		return;
	}

	memset(pPeerKey, 0, sizeof(NDIS_802_11_KEY) + LEN_EAP_KEY);
	pPeerKey->Length = sizeof(NDIS_802_11_KEY) + LEN_EAP_KEY;
	pPeerKey->KeyIndex = 0xe0000000;
	pPeerKey->KeyLength =
	    pMsg3->KeyDesc.KeyLength[0] * 256 + pMsg3->KeyDesc.KeyLength[1];

	memcpy(pPeerKey->BSSID, pAd->PortCfg.Bssid, ETH_ALEN);
	memcpy(&pPeerKey->KeyRSC, pMsg3->KeyDesc.KeyRsc, LEN_KEY_DESC_RSC);
	memcpy(pPeerKey->KeyMaterial, &pAd->PortCfg.PTK[32], 32);
	// Call Add peer key function
	RTMPWPAAddKeyProc(pAd, pPeerKey);
	kfree(pPeerKey);

	// 6. Send Message 4 to authenticator
	// Send using priority queue
	MiniportMMRequest(pAd, pOutBuffer, FrameLen);
	kfree(pOutBuffer);
	kfree(mpool);

	DBGPRINT(RT_DEBUG_ERROR, "Wpa2PairMsg3Action <-----\n");

}

/*
	========================================================================

	Routine Description:
		Process Group key 2-way handshaking

	Arguments:
		pAd	Pointer	to our adapter
		Elem		Message body

	Return Value:
		None

	Note:

	========================================================================
*/
static VOID WpaGroupMsg1Action(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	PHEADER_802_11 pHeader;
	PUCHAR pOutBuffer = NULL;
	UCHAR Header802_3[14];
	ULONG FrameLen = 0;
	UCHAR EAPHEAD[8] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e };
	EAPOL_PACKET Packet;
	PEAPOL_PACKET pGroup;
	UCHAR *mpool, *MSG, *KEYDATA;
	UCHAR Mic[16], OldMic[16];
	UCHAR GTK[32], Key[32];
	PNDIS_802_11_KEY pGroupKey = NULL;
	ULONG fEqMem;

	pHeader = (PHEADER_802_11) Elem->Msg;

	mpool = kmalloc(2570, MEM_ALLOC_FLAG);	// allocate memory
	if (mpool == NULL)
		return;

	// MSG Len = 2048.
	MSG = (UCHAR *) ROUND_UP(mpool, 4);
	// KEYDATA Len = 512.
	KEYDATA = (UCHAR *) ROUND_UP(MSG + 2048, 4);

	if ((pAd->PortCfg.bWmmCapable)
	    && (pHeader->FC.SubType == SUBTYPE_QDATA))
		fEqMem =
		    (ULONG) (memcmp
			     (&Elem->
			      Msg[LENGTH_802_11 + LENGTH_802_1_H +
				  LENGTH_WMMQOS_H], EAPHEAD,
			      LENGTH_802_1_H) == 0);
	else
		fEqMem =
		    (ULONG) (memcmp
			     (&Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H],
			      EAPHEAD, LENGTH_802_1_H) == 0);

	if (fEqMem) {
		DBGPRINT(RT_DEBUG_TRACE, "WpaGroupMsg1Action ----->MsgLen=%d\n",
			 Elem->MsgLen);
		memcpy(MSG, Elem->Msg, LENGTH_802_11);
		memcpy(&MSG[LENGTH_802_11],
		       &Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H],
		       (Elem->MsgLen));
	} else {
		DBGPRINT(RT_DEBUG_TRACE, "WpaGroupMsg1Action ----->\n");
		memcpy(MSG, Elem->Msg, Elem->MsgLen);
	}

	// Process Group message 1 frame.
	if ((pAd->PortCfg.bWmmCapable)
	    && (pHeader->FC.SubType == SUBTYPE_QDATA))
		pGroup =
		    (PEAPOL_PACKET) & MSG[LENGTH_802_11 + LENGTH_802_1_H +
					  LENGTH_WMMQOS_H];
	else
		pGroup = (PEAPOL_PACKET) & MSG[LENGTH_802_11 + LENGTH_802_1_H];

#ifdef BIG_ENDIAN
	// recovery original byte order, before forward Elem to another routine
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &pGroup->KeyDesc.KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&pGroup->KeyDesc.KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
#endif

	// 0. Verify RSN IE & cipher type match
	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled
	    && (pGroup->KeyDesc.KeyInfo.KeyDescVer != 2)) {
		kfree(mpool);
		return;
	} else if (pAd->PortCfg.WepStatus == Ndis802_11Encryption2Enabled
		   && (pGroup->KeyDesc.KeyInfo.KeyDescVer != 1)) {
		kfree(mpool);
		return;
	}
	// 1. Verify Replay counter
	//    Check Replay Counter, it has to be larger than last one. No need to be exact one larger
	if (memcmp
	    (pGroup->KeyDesc.ReplayCounter, pAd->PortCfg.ReplayCounter,
	     LEN_KEY_DESC_REPLAY) <= 0) {
		if ((!pAd->PortCfg.bWmmCapable)
		    && (pHeader->FC.SubType == SUBTYPE_QDATA)) {
			kfree(mpool);
			return;
		}
	}
#ifdef BIG_ENDIAN
	// recovery original byte order, before forward Elem to another routine
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &pGroup->KeyDesc.KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&pGroup->KeyDesc.KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
#endif

	// Update new replay counter
	memcpy(pAd->PortCfg.ReplayCounter, pGroup->KeyDesc.ReplayCounter,
	       LEN_KEY_DESC_REPLAY);

	// 2. Verify MIC is valid
	// Save the MIC and replace with zero
	memcpy(OldMic, pGroup->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);
	memset(pGroup->KeyDesc.KeyMic, 0, LEN_KEY_DESC_MIC);

	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled) {
		// AES
		UCHAR digest[32];	// SHA1_DIGEST_SIZE =20

		HMAC_SHA1((PUCHAR) pGroup, pGroup->Len[1] + 4, pAd->PortCfg.PTK,
			  LEN_EAP_MICK, digest);
		memcpy(Mic, digest, LEN_KEY_DESC_MIC);
	} else {
		hmac_md5(pAd->PortCfg.PTK, LEN_EAP_MICK, (PUCHAR) pGroup,
			 pGroup->Len[1] + 4, Mic);
	}

	if (memcmp(OldMic, Mic, LEN_KEY_DESC_MIC) != 0) {
		DBGPRINT(RT_DEBUG_ERROR,
			 " MIC Different in group msg 1 of 2-way handshake!!!!!!!!!! \n");
		kfree(mpool);
		return;
	} else
		DBGPRINT(RT_DEBUG_TRACE,
			 " MIC VALID in group msg 1 of 2-way handshake!!!!!!!!!! \n");

#ifdef BIG_ENDIAN
	// recovery original byte order, before forward Elem to another routine
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &pGroup->KeyDesc.KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&pGroup->KeyDesc.KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
#endif

	// 3. Decrypt GTK from Key Data
	DBGPRINT(RT_DEBUG_TRACE,
		 " Install = %d!!!!EKD_DL = %d!!!!!KeyIndex = %d! \n",
		 pGroup->KeyDesc.KeyInfo.Install,
		 pGroup->KeyDesc.KeyInfo.EKD_DL,
		 pGroup->KeyDesc.KeyInfo.KeyIndex);

	pGroupKey = kmalloc(MAX_LEN_OF_MLME_BUFFER, MEM_ALLOC_FLAG);	// allocate memory
	if (pGroupKey == NULL) {
		kfree(mpool);
		return;
	}

	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled) {
		INT i;
		// Decrypt AES GTK
		memcpy(KEYDATA, pGroup->KeyDesc.KeyData, 32);

		//if (pGroup->KeyDesc.KeyInfo.EKD_DL == 1)
		AES_GTK_KEY_UNWRAP(&pAd->PortCfg.PTK[16], KEYDATA,
				   pGroup->KeyDesc.KeyDataLen[1],
				   pGroup->KeyDesc.KeyData);

		// Update GTK
		memset(pGroupKey, 0, sizeof(NDIS_802_11_KEY) + LEN_EAP_KEY);
		pGroupKey->Length = sizeof(NDIS_802_11_KEY) + LEN_EAP_KEY;
		pGroupKey->KeyIndex =
		    0x20000000 | pGroup->KeyDesc.KeyInfo.KeyIndex;
		pGroupKey->KeyLength =
		    pGroup->KeyDesc.KeyLength[0] * 256 +
		    pGroup->KeyDesc.KeyLength[1];

		memcpy(pGroupKey->BSSID, pAd->PortCfg.Bssid, ETH_ALEN);
		memcpy(pGroupKey->KeyMaterial, KEYDATA, 32);
		memcpy(GTK, KEYDATA, 32);

		DBGPRINT_RAW(RT_DEBUG_TRACE, "GTK = \n");
		for (i = 0; i < 32; i++) {
			DBGPRINT_RAW(RT_DEBUG_TRACE, "%02x ",
				     pGroup->KeyDesc.KeyData[i]);
			if (i % 16 == 15)
				DBGPRINT_RAW(RT_DEBUG_TRACE, "\n ");
		}
		DBGPRINT_RAW(RT_DEBUG_TRACE, "\n  \n");

		// Call Add peer key function
		RTMPWPAAddKeyProc(pAd, pGroupKey);
	} else			// TKIP
	{
		INT i;

		// Decrypt TKIP GTK
		// Construct 32 bytes RC4 Key
		memcpy(Key, pGroup->KeyDesc.KeyIv, 16);
		memcpy(&Key[16], &pAd->PortCfg.PTK[16], 16);
		ARCFOUR_INIT(&pAd->PrivateInfo.WEPCONTEXT, Key, 32);
		//discard first 256 bytes
		for (i = 0; i < 256; i++)
			ARCFOUR_BYTE(&pAd->PrivateInfo.WEPCONTEXT);
		// Decrypt GTK. Becareful, there is no ICV to check the result is correct or not
		ARCFOUR_DECRYPT(&pAd->PrivateInfo.WEPCONTEXT, GTK,
				pGroup->KeyDesc.KeyData, 32);

		DBGPRINT_RAW(RT_DEBUG_TRACE, "GTK = \n");
		for (i = 0; i < 32; i++) {
			DBGPRINT_RAW(RT_DEBUG_TRACE, "%2x ", GTK[i]);
			if (i % 16 == 15)
				DBGPRINT_RAW(RT_DEBUG_TRACE, "\n ");
		}
		DBGPRINT_RAW(RT_DEBUG_TRACE, "\n  \n");

		//RTMPWPAAddKeyProc(pAd, pGroupKey);
		//ParseKeyData(pAd, KEYDATA, pGroup->KeyDesc.KeyDataLen[1]);

	}

	// 4. Construct Group Message 2
	pAd->Sequence = ((pAd->Sequence) + 1) & (MAX_SEQ_NUMBER);

	WPAMake8023Hdr(pAd, pAd->PortCfg.Bssid, Header802_3);

	// Zero Group message 1 body
	memset(&Packet, 0, sizeof(Packet));
	Packet.Version = EAPOL_VER;
	Packet.Type = EAPOLKey;
	Packet.Len[1] = sizeof(KEY_DESCRIPTER) - MAX_LEN_OF_RSNIE;	// No data field

	//
	// Group Message 2 as  EAPOL-Key(1,0,0,0,G,0,0,MIC,0)
	//
	Packet.KeyDesc.Type = RSN_KEY_DESC;

	// Key descriptor version and appropriate RSN IE
	Packet.KeyDesc.KeyInfo.KeyDescVer = pGroup->KeyDesc.KeyInfo.KeyDescVer;

	// Update Key Length and Key Index
	Packet.KeyDesc.KeyInfo.KeyIndex = pGroup->KeyDesc.KeyInfo.KeyIndex;
	Packet.KeyDesc.KeyLength[0] = pGroup->KeyDesc.KeyLength[0];
	Packet.KeyDesc.KeyLength[1] = pGroup->KeyDesc.KeyLength[1];

	// Key Type Group key
	Packet.KeyDesc.KeyInfo.KeyType = 0;

	// KeyMic field presented
	Packet.KeyDesc.KeyInfo.KeyMic = 1;

	// Secure bit
	if (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPAPSK)
		Packet.KeyDesc.KeyInfo.Secure = 1;

	// Key Replay count
	memcpy(Packet.KeyDesc.ReplayCounter, pGroup->KeyDesc.ReplayCounter,
	       LEN_KEY_DESC_REPLAY);

#ifdef BIG_ENDIAN
	// recovery original byte order, before forward Elem to another routine
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &Packet.KeyDesc.KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&Packet.KeyDesc.KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
#endif

	// Out buffer for transmitting group message 2
	pOutBuffer = kmalloc(MAX_LEN_OF_MLME_BUFFER, MEM_ALLOC_FLAG);	// allocate memory
	if (pOutBuffer == NULL) {
		kfree(pGroupKey);
		kfree(mpool);
		return;
	}
	// Prepare EAPOL frame for MIC calculation
	// Be careful, only EAPOL frame is counted for MIC calculation
	MakeOutgoingFrame(pOutBuffer, &FrameLen,
			  Packet.Len[1] + 4, &Packet, END_OF_ARGS);

	// Prepare and Fill MIC value
	memset(Mic, 0, sizeof(Mic));
	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled) {
		// AES
		UCHAR digest[32];	// SHA1_DIGEST_SIZE =20

		HMAC_SHA1(pOutBuffer, FrameLen, pAd->PortCfg.PTK, LEN_EAP_MICK,
			  digest);
		memcpy(Mic, digest, LEN_KEY_DESC_MIC);
	} else {
		INT i;
		DBGPRINT_RAW(RT_DEBUG_INFO, "PTK = ");
		for (i = 0; i < 64; i++)
			DBGPRINT_RAW(RT_DEBUG_INFO, "%2x-",
				     pAd->PortCfg.PTK[i]);
		DBGPRINT_RAW(RT_DEBUG_INFO, "\n FrameLen = %d\n", FrameLen);

		hmac_md5(pAd->PortCfg.PTK, LEN_EAP_MICK, pOutBuffer, FrameLen,
			 Mic);
	}
	memcpy(Packet.KeyDesc.KeyMic, Mic, LEN_KEY_DESC_MIC);

	MakeOutgoingFrame(pOutBuffer, &FrameLen, LENGTH_802_3, &Header802_3,
//                      sizeof(EAPHEAD),  EAPHEAD,
			  Packet.Len[1] + 4, &Packet, END_OF_ARGS);

	// 5. Copy frame to Tx ring and prepare for encryption
	RTMPToWirelessSta(pAd, pOutBuffer, FrameLen);

	// 6 Free allocated memory
	kfree(pOutBuffer);

	// 7. Update GTK
	memset(pGroupKey, 0, sizeof(NDIS_802_11_KEY) + LEN_EAP_KEY);
	pGroupKey->Length = sizeof(NDIS_802_11_KEY) + LEN_EAP_KEY;
	pGroupKey->KeyIndex = 0x20000000 | pGroup->KeyDesc.KeyInfo.KeyIndex;
	pGroupKey->KeyLength =
	    pGroup->KeyDesc.KeyLength[0] * 256 + pGroup->KeyDesc.KeyLength[1];

	memcpy(pGroupKey->BSSID, pAd->PortCfg.Bssid, ETH_ALEN);
	memcpy(pGroupKey->KeyMaterial, GTK, LEN_EAP_KEY);
	// Call Add peer key function
	RTMPWPAAddKeyProc(pAd, pGroupKey);
	kfree(pGroupKey);
	kfree(mpool);

	DBGPRINT(RT_DEBUG_TRACE, "WpaGroupMsg1Action <-----\n");
}

/*
	==========================================================================
	Description:
		This is	state machine function.
		When receiving EAPOL packets which is  for 802.1x key management.
		Use	both in	WPA, and WPAPSK	case.
		In this	function, further dispatch to different	functions according	to the received	packet.	 3 categories are :
		  1.  normal 4-way pairwisekey and 2-way groupkey handshake
		  2.  MIC error	(Countermeasures attack)  report packet	from STA.
		  3.  Request for pairwise/group key update	from STA
	Return:
	==========================================================================
*/
static VOID WpaEAPOLKeyAction(IN PRTMP_ADAPTER pAd, IN MLME_QUEUE_ELEM * Elem)
{
	INT MsgType = EAPOL_MSG_INVALID;
	PKEY_DESCRIPTER pKeyDesc;
	PHEADER_802_11 pHeader;	//red
	UCHAR ZeroReplay[LEN_KEY_DESC_REPLAY];
	UCHAR EapolVr;

	DBGPRINT(RT_DEBUG_TRACE, "-----> WpaEAPOLKeyAction\n");

	pHeader = (PHEADER_802_11) Elem->Msg;

	// Get 802.11 header first
	if ((pAd->PortCfg.bWmmCapable)
	    && (pHeader->FC.SubType == SUBTYPE_QDATA))
		pKeyDesc =
		    (PKEY_DESCRIPTER) & Elem->
		    Msg[(LENGTH_802_11 + LENGTH_802_1_H + LENGTH_WMMQOS_H +
			 LENGTH_EAPOL_H)];
	else
		pKeyDesc =
		    (PKEY_DESCRIPTER) & Elem->
		    Msg[(LENGTH_802_11 + LENGTH_802_1_H + LENGTH_EAPOL_H)];

#ifdef BIG_ENDIAN
	// pMsg->KeyDesc.KeyInfo and pKeyDesc->KeyInfo both point to the same addr.
	// Thus, it only needs swap once.
	{
		USHORT tmpKeyinfo;

		memcpy(&tmpKeyinfo, &pKeyDesc->KeyInfo, sizeof(USHORT));
		tmpKeyinfo = SWAP16(tmpKeyinfo);
		memcpy(&pKeyDesc->KeyInfo, &tmpKeyinfo, sizeof(USHORT));
	}
//          *(USHORT *)((UCHAR *)pKeyDesc+1) = SWAP16(*(USHORT *)((UCHAR *)pKeyDesc+1));
#endif

	// Sanity check, this should only happen in WPA(2)-PSK mode
	// 0. Debug print all bit information
	DBGPRINT(RT_DEBUG_INFO, "KeyInfo Key Description Version %d\n",
		 pKeyDesc->KeyInfo.KeyDescVer);
	DBGPRINT(RT_DEBUG_INFO, "KeyInfo Key Type %d\n",
		 pKeyDesc->KeyInfo.KeyType);
	DBGPRINT(RT_DEBUG_INFO, "KeyInfo Key Index %d\n",
		 pKeyDesc->KeyInfo.KeyIndex);
	DBGPRINT(RT_DEBUG_INFO, "KeyInfo Install %d\n",
		 pKeyDesc->KeyInfo.Install);
	DBGPRINT(RT_DEBUG_INFO, "KeyInfo Key Ack %d\n",
		 pKeyDesc->KeyInfo.KeyAck);
	DBGPRINT(RT_DEBUG_INFO, "KeyInfo Key MIC %d\n",
		 pKeyDesc->KeyInfo.KeyMic);
	DBGPRINT(RT_DEBUG_INFO, "KeyInfo Secure %d\n",
		 pKeyDesc->KeyInfo.Secure);
	DBGPRINT(RT_DEBUG_INFO, "KeyInfo Error %d\n", pKeyDesc->KeyInfo.Error);
	DBGPRINT(RT_DEBUG_INFO, "KeyInfo Request %d\n",
		 pKeyDesc->KeyInfo.Request);
	DBGPRINT(RT_DEBUG_INFO, "KeyInfo EKD_DL %d\n",
		 pKeyDesc->KeyInfo.EKD_DL);

	// 1. Check EAPOL frame version and type
	if ((pAd->PortCfg.bWmmCapable)
	    && (pHeader->FC.SubType == SUBTYPE_QDATA))
		EapolVr =
		    (UCHAR) Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H +
				      LENGTH_WMMQOS_H];
	else
		EapolVr = (UCHAR) Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H];

	if (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPAPSK) {

		if ((EapolVr != EAPOL_VER) || (pKeyDesc->Type != WPA1_KEY_DESC)) {
			DBGPRINT(RT_DEBUG_ERROR,
				 "Key descripter does not match with WPA1 rule \n");
			return;
		}
	} else if (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPA2PSK) {
		if (pKeyDesc->Type != WPA2_KEY_DESC)	//some APs would send EapolVr = 2
		{
			DBGPRINT(RT_DEBUG_ERROR,
				 "Key descripter does not match with WPA2 rule \n");
			return;
		}
	}
	// First validate replay counter, only accept message with larger replay counter
	// Let equal pass, some AP start with all zero replay counter
	memset(ZeroReplay, 0, LEN_KEY_DESC_REPLAY);

	if ((!pAd->PortCfg.bWmmCapable)
	    && (pHeader->FC.SubType == SUBTYPE_QDATA))
		if ((memcmp
		     (pKeyDesc->ReplayCounter, pAd->PortCfg.ReplayCounter,
		      LEN_KEY_DESC_REPLAY) <= 0)
		    &&
		    (memcmp
		     (pKeyDesc->ReplayCounter, ZeroReplay,
		      LEN_KEY_DESC_REPLAY) != 0)) {
			DBGPRINT(RT_DEBUG_ERROR,
				 "   ReplayCounter not match   \n");
			return;
		}

/*
====================================================================
        WPAPSK2     WPAPSK2         WPAPSK2     WPAPSK2
======================================================================
*/
	if (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPA2PSK) {
		if ((pKeyDesc->KeyInfo.KeyType == 1) &&
		    (pKeyDesc->KeyInfo.EKD_DL == 0) &&
		    (pKeyDesc->KeyInfo.KeyAck == 1) &&
		    (pKeyDesc->KeyInfo.KeyMic == 0) &&
		    (pKeyDesc->KeyInfo.Secure == 0) &&
		    (pKeyDesc->KeyInfo.Error == 0) &&
		    (pKeyDesc->KeyInfo.Request == 0)) {
			MsgType = EAPOL_PAIR_MSG_1;
			DBGPRINT(RT_DEBUG_ERROR,
				 "Receive EAPOL Key Pairwise Message 1\n");
		} else if ((pKeyDesc->KeyInfo.KeyType == 1)
			   && (pKeyDesc->KeyInfo.EKD_DL == 1)
			   && (pKeyDesc->KeyInfo.KeyAck == 1)
			   && (pKeyDesc->KeyInfo.KeyMic == 1)
			   && (pKeyDesc->KeyInfo.Secure == 1)
			   && (pKeyDesc->KeyInfo.Error == 0)
			   && (pKeyDesc->KeyInfo.Request == 0)) {
			MsgType = EAPOL_PAIR_MSG_3;
			DBGPRINT(RT_DEBUG_ERROR,
				 "Receive EAPOL Key Pairwise Message 3\n");
		} else if ((pKeyDesc->KeyInfo.KeyType == 0)
			   && (pKeyDesc->KeyInfo.EKD_DL == 1)
			   && (pKeyDesc->KeyInfo.KeyAck == 1)
			   && (pKeyDesc->KeyInfo.KeyMic == 1)
			   && (pKeyDesc->KeyInfo.Secure == 1)
			   && (pKeyDesc->KeyInfo.Error == 0)
			   && (pKeyDesc->KeyInfo.Request == 0)) {
			MsgType = EAPOL_GROUP_MSG_1;
			DBGPRINT(RT_DEBUG_ERROR,
				 "Receive EAPOL Key Group Message 1\n");
		}
#ifdef BIG_ENDIAN
		// recovery original byte order, before forward Elem to another routine
		{
			USHORT tmpKeyinfo;

			memcpy(&tmpKeyinfo, &pKeyDesc->KeyInfo, sizeof(USHORT));
			tmpKeyinfo = SWAP16(tmpKeyinfo);
			memcpy(&pKeyDesc->KeyInfo, &tmpKeyinfo, sizeof(USHORT));
		}
#endif

		// We will assume link is up (assoc suceess and port not secured).
		// All state has to be able to process message from previous state
		switch (pAd->PortCfg.WpaState) {
		case SS_START:
			if (MsgType == EAPOL_PAIR_MSG_1) {
				Wpa2PairMsg1Action(pAd, Elem);
				pAd->PortCfg.WpaState = SS_WAIT_MSG_3;
			}
			break;

		case SS_WAIT_MSG_3:
			if (MsgType == EAPOL_PAIR_MSG_1) {
				Wpa2PairMsg1Action(pAd, Elem);
				pAd->PortCfg.WpaState = SS_WAIT_MSG_3;
			} else if (MsgType == EAPOL_PAIR_MSG_3) {
				Wpa2PairMsg3Action(pAd, Elem);
				pAd->PortCfg.WpaState = SS_WAIT_GROUP;
			}
			break;

		case SS_WAIT_GROUP:	// When doing group key exchange
		case SS_FINISH:	// This happened when update group key
			if (MsgType == EAPOL_PAIR_MSG_1) {
				Wpa2PairMsg1Action(pAd, Elem);
				pAd->PortCfg.WpaState = SS_WAIT_MSG_3;
				// Reset port secured variable
				pAd->PortCfg.PortSecured =
				    WPA_802_1X_PORT_NOT_SECURED;
			} else if (MsgType == EAPOL_PAIR_MSG_3) {
				Wpa2PairMsg3Action(pAd, Elem);
				pAd->PortCfg.WpaState = SS_WAIT_GROUP;
				// Reset port secured variable
				pAd->PortCfg.PortSecured =
				    WPA_802_1X_PORT_NOT_SECURED;
			} else if (MsgType == EAPOL_GROUP_MSG_1) {
				WpaGroupMsg1Action(pAd, Elem);
				pAd->PortCfg.WpaState = SS_FINISH;
			}
			break;

		default:
			break;
		}
	}
///*
//====================================================================
//          WPAPSK          WPAPSK          WPAPSK          WPAPSK
//======================================================================
//*/
	// Classify message Type, either pairwise message 1, 3, or group message 1 for supplicant
	else if (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPAPSK) {
		if ((pKeyDesc->KeyInfo.KeyType == 1) &&
		    (pKeyDesc->KeyInfo.KeyIndex == 0) &&
		    (pKeyDesc->KeyInfo.KeyAck == 1) &&
		    (pKeyDesc->KeyInfo.KeyMic == 0) &&
		    (pKeyDesc->KeyInfo.Secure == 0) &&
		    (pKeyDesc->KeyInfo.Error == 0) &&
		    (pKeyDesc->KeyInfo.Request == 0)) {
			MsgType = EAPOL_PAIR_MSG_1;
			DBGPRINT(RT_DEBUG_TRACE,
				 "Receive EAPOL Key Pairwise Message 1\n");
		} else if ((pKeyDesc->KeyInfo.KeyType == 1)
			   && (pKeyDesc->KeyInfo.KeyIndex == 0)
			   && (pKeyDesc->KeyInfo.KeyAck == 1)
			   && (pKeyDesc->KeyInfo.KeyMic == 1)
			   && (pKeyDesc->KeyInfo.Secure == 0)
			   && (pKeyDesc->KeyInfo.Error == 0)
			   && (pKeyDesc->KeyInfo.Request == 0)) {
			MsgType = EAPOL_PAIR_MSG_3;
			DBGPRINT(RT_DEBUG_TRACE,
				 "Receive EAPOL Key Pairwise Message 3\n");
		} else if ((pKeyDesc->KeyInfo.KeyType == 0)
			   && (pKeyDesc->KeyInfo.KeyIndex != 0)
			   && (pKeyDesc->KeyInfo.KeyAck == 1)
			   && (pKeyDesc->KeyInfo.KeyMic == 1)
			   && (pKeyDesc->KeyInfo.Secure == 1)
			   && (pKeyDesc->KeyInfo.Error == 0)
			   && (pKeyDesc->KeyInfo.Request == 0)) {
			MsgType = EAPOL_GROUP_MSG_1;
			DBGPRINT(RT_DEBUG_TRACE,
				 "Receive EAPOL Key Group Message 1\n");
		}
#ifdef BIG_ENDIAN
		// recovery original byte order, before forward Elem to another routine
		{
			USHORT tmpKeyinfo;

			memcpy(&tmpKeyinfo, &pKeyDesc->KeyInfo, sizeof(USHORT));
			tmpKeyinfo = SWAP16(tmpKeyinfo);
			memcpy(&pKeyDesc->KeyInfo, &tmpKeyinfo, sizeof(USHORT));
		}
#endif

		// We will assume link is up (assoc suceess and port not secured).
		// All state has to be able to process message from previous state
		switch (pAd->PortCfg.WpaState) {
		case SS_START:
			if (MsgType == EAPOL_PAIR_MSG_1) {
				WpaPairMsg1Action(pAd, Elem);
				pAd->PortCfg.WpaState = SS_WAIT_MSG_3;
			}
			break;

		case SS_WAIT_MSG_3:
			if (MsgType == EAPOL_PAIR_MSG_1) {
				WpaPairMsg1Action(pAd, Elem);
				pAd->PortCfg.WpaState = SS_WAIT_MSG_3;
			} else if (MsgType == EAPOL_PAIR_MSG_3) {
				WpaPairMsg3Action(pAd, Elem);
				pAd->PortCfg.WpaState = SS_WAIT_GROUP;
			}
			break;

		case SS_WAIT_GROUP:	// When doing group key exchange
		case SS_FINISH:	// This happened when update group key
			if (MsgType == EAPOL_PAIR_MSG_1) {
				WpaPairMsg1Action(pAd, Elem);
				pAd->PortCfg.WpaState = SS_WAIT_MSG_3;
				// Reset port secured variable
				pAd->PortCfg.PortSecured =
				    WPA_802_1X_PORT_NOT_SECURED;
			} else if (MsgType == EAPOL_PAIR_MSG_3) {
				WpaPairMsg3Action(pAd, Elem);
				pAd->PortCfg.WpaState = SS_WAIT_GROUP;
				// Reset port secured variable
				pAd->PortCfg.PortSecured =
				    WPA_802_1X_PORT_NOT_SECURED;
			} else if (MsgType == EAPOL_GROUP_MSG_1) {
				WpaGroupMsg1Action(pAd, Elem);
				pAd->PortCfg.WpaState = SS_FINISH;
			}
			break;

		default:
			break;
		}
	}

	DBGPRINT(RT_DEBUG_TRACE, "<----- WpaEAPOLKeyAction\n");
}

/*
	==========================================================================
	Description:
		association	state machine init,	including state	transition and timer init
	Parameters:
		S -	pointer	to the association state machine
	==========================================================================
 */
VOID WpaPskStateMachineInit(IN PRTMP_ADAPTER pAd,
			    IN STATE_MACHINE * S,
			    OUT STATE_MACHINE_FUNC Trans[])
{
	StateMachineInit(S, (STATE_MACHINE_FUNC *) Trans, MAX_WPA_PSK_STATE,
			 MAX_WPA_PSK_MSG, (STATE_MACHINE_FUNC) Drop,
			 WPA_PSK_IDLE, WPA_MACHINE_BASE);
	StateMachineSetAction(S, WPA_PSK_IDLE, MT2_EAPOLKey,
			      (STATE_MACHINE_FUNC) WpaEAPOLKeyAction);
}

/*
	========================================================================

	Routine Description:
		PRF function

	Arguments:

	Return Value:

	Note:
		802.1i	Annex F.9

	========================================================================
*/
VOID PRF(IN UCHAR * key,
	 IN INT key_len,
	 IN UCHAR * prefix,
	 IN INT prefix_len,
	 IN UCHAR * data, IN INT data_len, OUT UCHAR * output, IN INT len)
{
	INT i;
	UCHAR *input;
	INT currentindex = 0;
	INT total_len;
	BOOLEAN bfree = TRUE;

	input = kmalloc(1024, MEM_ALLOC_FLAG); // allocate memory
	if (input == NULL) {
		input = prf_input;
		bfree = FALSE;
	}

	memcpy(input, prefix, prefix_len);
	input[prefix_len] = 0;
	memcpy(&input[prefix_len + 1], data, data_len);
	total_len = prefix_len + 1 + data_len;
	input[total_len] = 0;
	total_len++;
	for (i = 0; i < (len + 19) / 20; i++) {
		HMAC_SHA1(input, total_len, key, key_len,
			  &output[currentindex]);
		currentindex += 20;
		input[total_len - 1]++;
	}

	if (bfree == TRUE)
		kfree(input);
}

//#if WPA_SUPPLICANT_SUPPORT

// If the received frame is EAP-Packet ,find out its EAP-Code (Request(0x01), Response(0x02), Success(0x03), Failure(0x04)).
INT RTMPCheckWPAframeForEapCode(IN PRTMP_ADAPTER pAd,
				IN PUCHAR pFrame,
				IN ULONG FrameLen, IN ULONG OffSet)
{

	PUCHAR pData;
	INT result = 0;

	if (FrameLen < OffSet + LENGTH_EAPOL_H + LENGTH_EAP_H)
		return result;

	pData = pFrame + OffSet;	// skip offset bytes

	if (*(pData + 1) == EAPPacket)	// 802.1x header - Packet Type
	{
		result = *(pData + 4);	// EAP header - Code
	}

	return result;
}

//#endif
