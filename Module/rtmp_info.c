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
 *      Module Name: rtmp_info.c
 *
 *      Abstract:
 *
 *      Revision History:
 *      Who             When            What
 *      --------        -----------     -----------------------------
 *      RoryC           3rd  Jan 03     Created
 *      RoryC           14th Feb 05     Modify to support RT61
 *      GertjanW        21st Jan 06     Baseline code
 *      MarkW (rt2500)  28th Jan 06     Removed debug iwpriv
 *      RobinC (rt2500) 30th Jan 06     Fix for range values
 *      RobinC (rt2500) 30th Jan 06     Support ifpreup scripts
 *      RobinC (rt2500) 30th Jan 06     Link Quality reporting
 *      MarkW (rt2500)  30th Jan 06     iwconfig frequency fix
 *      MarkW (rt2500)  2nd  Feb 06     RSSI reporting for iwlist scanning
 *      MarkW (rt2500)  3rd  Feb 06     if pre-up fix for RaConfig
 *      LuisC (rt2500)  16th Feb 06     fix unknown IOCTL's
 *      MarkW (rt2500)  16th Feb 06     Quality reporting in scan for current
 *      GertjanW        19th Feb 06     Promisc mode support
 *      MarkW (rt2500)  3rd  Jun 06     Monitor mode through iwconfig
 *      RomainB         31st Dec 06     RFMON getter
 * 	OlivierC	20th May 07	Fix WEP keys management
 ***************************************************************************/

#include	"rt_config.h"
#include <net/iw_handler.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,29)
static inline unsigned int jiffies_to_msecs(const unsigned long j)
{
#if HZ <= 1000 && !(1000 % HZ)
	return (1000 / HZ) * j;
#elif HZ > 1000 && !(HZ % 1000)
	return (j + (HZ / 1000) - 1) / (HZ / 1000);
#else
	return (j * 1000) / HZ;
#endif
}

static inline unsigned long msecs_to_jiffies(const unsigned int m)
{
	if (m > jiffies_to_msecs(MAX_JIFFY_OFFSET))
		return MAX_JIFFY_OFFSET;
#if HZ <= 1000 && !(1000 % HZ)
	return (m + (1000 / HZ) - 1) / (1000 / HZ);
#elif HZ > 1000 && !(HZ % 1000)
	return m * (HZ / 1000);
#else
	return (m * HZ + 999) / 1000;
#endif
}

static inline void msleep(unsigned int msecs)
{
	unsigned long timeout = msecs_to_jiffies(msecs) + 1;

	while (timeout) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		timeout = schedule_timeout(timeout);
	}
}
#endif

#ifndef IW_ESSID_MAX_SIZE
/* Maximum size of the ESSID and NICKN strings */
#define IW_ESSID_MAX_SIZE   32
#endif

#define NR_WEP_KEYS 4
#define WEP_SMALL_KEY_LEN (40/8)
#define WEP_LARGE_KEY_LEN (104/8)

extern UCHAR CipherWpa2Template[];
extern UCHAR CipherWpa2TemplateLen;
extern UCHAR CipherWpaPskTkip[];
extern UCHAR CipherWpaPskTkipLen;

struct iw_priv_args privtab[] = {
	{RTPRIV_IOCTL_SET,
	 IW_PRIV_TYPE_CHAR | 1024, 0,
	 "set"},

#ifdef RT61_DBG
	{RTPRIV_IOCTL_BBP,
	 IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | 1024,
	 "bbp"},
	{RTPRIV_IOCTL_MAC,
	 IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | 1024,
	 "mac"},

#ifdef RALINK_ATE
	{RTPRIV_IOCTL_E2P,
	 IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | 1024,
	 "e2p"},
#endif				/* RALINK_ATE */
#endif				/* RT61_DBG */

	{RTPRIV_IOCTL_STATISTICS,
	 IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | 1024,
	 "stat"},
	{RTPRIV_IOCTL_SET_RFMONTX,
	 IW_PRIV_TYPE_INT | 2, 0,
	 "rfmontx"},
	{RTPRIV_IOCTL_GET_RFMONTX,
	 0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 "get_rfmontx"},
	{RTPRIV_IOCTL_GSITESURVEY,
	 IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | 1024,
	 "get_site_survey"},
	{RTPRIV_IOCTL_GETRAAPCFG,
	 IW_PRIV_TYPE_CHAR | 1024, 0,
	 "get_RaAP_Cfg"},
};

static INT Set_DriverVersion_Proc(IN PRTMP_ADAPTER pAd, IN PUCHAR arg)
{
	DBGPRINT(RT_DEBUG_TRACE, "Driver version-%s\n", DRIVER_VERSION);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set Country Region.
        This command will not work, if the field of CountryRegion in eeprom is programmed.
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_CountryRegion_Proc(IN PRTMP_ADAPTER pAd, IN PUCHAR arg)
{
	ULONG region;

	region = simple_strtol(arg, 0, 10);

	// Country can be set only when EEPROM not programmed
	if (pAd->PortCfg.CountryRegion & 0x80) {
		DBGPRINT(RT_DEBUG_ERROR,
			 "Set_CountryRegion_Proc::parameter of CountryRegion in eeprom is programmed \n");
		return FALSE;
	}

	if (region <= REGION_MAXIMUM_BG_BAND) {
		pAd->PortCfg.CountryRegion = (UCHAR) region;
	} else {
		DBGPRINT(RT_DEBUG_ERROR,
			 "Set_CountryRegion_Proc::parameters out of range\n");
		return FALSE;
	}

	// if set country region, driver needs to be reset
	BuildChannelList(pAd);

	DBGPRINT(RT_DEBUG_TRACE, "Set_CountryRegion_Proc::(CountryRegion=%d)\n",
		 pAd->PortCfg.CountryRegion);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set Country Region for A band.
        This command will not work, if the field of CountryRegion in eeprom is programmed.
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_CountryRegionABand_Proc(IN PRTMP_ADAPTER pAd, IN PUCHAR arg)
{
	ULONG region;

	region = simple_strtol(arg, 0, 10);

	// Country can be set only when EEPROM not programmed
	if (pAd->PortCfg.CountryRegionForABand & 0x80) {
		DBGPRINT(RT_DEBUG_ERROR,
			 "Set_CountryRegionABand_Proc::parameter of CountryRegion in eeprom is programmed \n");
		return FALSE;
	}

	if (region <= REGION_MAXIMUM_A_BAND) {
		pAd->PortCfg.CountryRegionForABand = (UCHAR) region;
	} else {
		DBGPRINT(RT_DEBUG_ERROR,
			 "Set_CountryRegionABand_Proc::parameters out of range\n");
		return FALSE;
	}

	// if set country region, driver needs to be reset
	BuildChannelList(pAd);

	DBGPRINT(RT_DEBUG_TRACE,
		 "Set_CountryRegionABand_Proc::(CountryRegion=%d)\n",
		 pAd->PortCfg.CountryRegionForABand);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set SSID
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_SSID_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	NDIS_802_11_SSID Ssid, *pSsid = NULL;
	BOOLEAN StateMachineTouched = FALSE;
	int success = TRUE;

	printk
	    ("'iwpriv <dev> set SSID' is deprecated, please use 'iwconfg <dev> essid' instead\n");

	/* Protect against oops if net is down, this will not work with if-preup use iwconfig properly */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE))
		return FALSE;

	if (strlen(arg) <= MAX_LEN_OF_SSID) {

		memset(&Ssid, 0, MAX_LEN_OF_SSID);
		if (strlen(arg) != 0) {
			memcpy(Ssid.Ssid, arg, strlen(arg));
			Ssid.SsidLength = strlen(arg);
		} else		//ANY ssid
		{
			Ssid.SsidLength = 0;
			memcpy(Ssid.Ssid, "", 0);
		}
		pSsid = &Ssid;

		if (pAdapter->Mlme.CntlMachine.CurrState != CNTL_IDLE) {
			MlmeRestartStateMachine(pAdapter);
			DBGPRINT(RT_DEBUG_TRACE,
				 "!!! MLME busy, reset MLME state machine !!!\n");
		}
		// tell CNTL state machine to call NdisMSetInformationComplete() after completing
		// this request, because this request is initiated by NDIS.
		pAdapter->MlmeAux.CurrReqIsFromNdis = FALSE;

		pAdapter->bConfigChanged = TRUE;

		MlmeEnqueue(pAdapter,
			    MLME_CNTL_STATE_MACHINE,
			    OID_802_11_SSID,
			    sizeof(NDIS_802_11_SSID), (VOID *) pSsid);

		StateMachineTouched = TRUE;
		DBGPRINT(RT_DEBUG_TRACE, "Set_SSID_Proc::(Len=%d,Ssid=%s)\n",
			 Ssid.SsidLength, Ssid.Ssid);
	} else
		success = FALSE;

	if (StateMachineTouched)	// Upper layer sent a MLME-related operations
		MlmeHandler(pAdapter);

	return success;
}

/*
    ==========================================================================
    Description:
        Set Wireless Mode
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_WirelessMode_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	ULONG WirelessMode;
	int success = TRUE;

	WirelessMode = simple_strtol(arg, 0, 10);

	if ((WirelessMode == PHY_11BG_MIXED) || (WirelessMode == PHY_11B) ||
	    (WirelessMode == PHY_11A) || (WirelessMode == PHY_11ABG_MIXED) ||
	    (WirelessMode == PHY_11G)) {
		// protect no A-band support
		if ((pAdapter->RfIcType != RFIC_5225)
		    && (pAdapter->RfIcType != RFIC_5325)) {
			if ((WirelessMode == PHY_11A)
			    || (WirelessMode == PHY_11ABG_MIXED)) {
				DBGPRINT(RT_DEBUG_ERROR,
					 "!!!!! Not support A band in RfIcType= %d\n",
					 pAdapter->RfIcType);
				return FALSE;
			}
		}

		RTMPSetPhyMode(pAdapter, WirelessMode);

		// Set AdhocMode rates
		if (pAdapter->PortCfg.BssType == BSS_ADHOC) {
			if (WirelessMode == PHY_11B)
				pAdapter->PortCfg.AdhocMode = 0;
			else if ((WirelessMode == PHY_11BG_MIXED)
				 || (WirelessMode == PHY_11ABG_MIXED))
				pAdapter->PortCfg.AdhocMode = 1;
			else if ((WirelessMode == PHY_11A)
				 || (WirelessMode == PHY_11G))
				pAdapter->PortCfg.AdhocMode = 2;

			MlmeUpdateTxRates(pAdapter, FALSE);
			MakeIbssBeacon(pAdapter);	// re-build BEACON frame
			AsicEnableIbssSync(pAdapter);	// copy to on-chip memory
		}

		DBGPRINT(RT_DEBUG_TRACE, "Set_WirelessMode_Proc::(=%d)\n",
			 WirelessMode);

		return success;
	} else {
		DBGPRINT(RT_DEBUG_ERROR,
			 "Set_WirelessMode_Proc::parameters out of range\n");
		return FALSE;
	}
}

/*
    ==========================================================================
    Description:
        Set TxRate
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_TxRate_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	ULONG TxRate;
	int success = TRUE;

	TxRate = simple_strtol(arg, 0, 10);

	if ((pAdapter->PortCfg.PhyMode == PHY_11B && TxRate <= 4) ||
	    ((pAdapter->PortCfg.PhyMode == PHY_11BG_MIXED
	      || pAdapter->PortCfg.PhyMode == PHY_11ABG_MIXED) && TxRate <= 12)
	    ||
	    ((pAdapter->PortCfg.PhyMode == PHY_11A
	      || pAdapter->PortCfg.PhyMode == PHY_11G) && (TxRate == 0
							   || (TxRate > 4
							       && TxRate <=
							       12)))) {
		if (TxRate == 0)
			RTMPSetDesiredRates(pAdapter, -1);
		else
			RTMPSetDesiredRates(pAdapter,
					    (LONG) (RateIdToMbps[TxRate - 1] *
						    1000000));

		DBGPRINT(RT_DEBUG_TRACE, "Set_TxRate_Proc::(TxRate=%d)\n",
			 TxRate);

		return success;
	} else {
		DBGPRINT(RT_DEBUG_ERROR,
			 "Set_TxRate_Proc::parameters out of range\n");
		return FALSE;	//Invalid argument
	}

}

/*
    ==========================================================================
    Description:
        Set Channel
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_Channel_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	int success = TRUE;
	UCHAR Channel;

	Channel = (UCHAR) simple_strtol(arg, 0, 10);

	if (ChannelSanity(pAdapter, Channel) == TRUE) {
		pAdapter->PortCfg.Channel = Channel;

		if (pAdapter->PortCfg.BssType == BSS_ADHOC)
			pAdapter->PortCfg.AdHocChannel = Channel;

		DBGPRINT(RT_DEBUG_TRACE, "Set_Channel_Proc::(Channel=%d)\n",
			 Channel);
	} else
		success = FALSE;

	return success;
}

/*
    ==========================================================================
    Description:
        Set 11B/11G Protection
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_BGProtection_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	switch (simple_strtol(arg, 0, 10)) {
	case 0:		//AUTO
		pAdapter->PortCfg.UseBGProtection = 0;
		break;
	case 1:		//Always On
		pAdapter->PortCfg.UseBGProtection = 1;
		break;
	case 2:		//Always OFF
		pAdapter->PortCfg.UseBGProtection = 2;
		break;
	default:		//Invalid argument
		return FALSE;
	}
	DBGPRINT(RT_DEBUG_TRACE, "Set_BGProtection_Proc::(BGProtection=%d)\n",
		 pAdapter->PortCfg.UseBGProtection);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set TxPreamble
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_TxPreamble_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	RT_802_11_PREAMBLE Preamble;

	Preamble = simple_strtol(arg, 0, 10);
	switch (Preamble) {
	case Rt802_11PreambleShort:
		pAdapter->PortCfg.TxPreamble = Preamble;
		MlmeSetTxPreamble(pAdapter, Rt802_11PreambleShort);
		break;
	case Rt802_11PreambleLong:
	case Rt802_11PreambleAuto:
		// if user wants AUTO, initialize to LONG here, then change according to AP's
		// capability upon association.
		pAdapter->PortCfg.TxPreamble = Preamble;
		MlmeSetTxPreamble(pAdapter, Rt802_11PreambleLong);
		break;
	default:		//Invalid argument
		return FALSE;
	}

	DBGPRINT(RT_DEBUG_TRACE, "Set_TxPreamble_Proc::(TxPreamble=%d)\n",
		 Preamble);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set RTS Threshold
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_RTSThreshold_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	NDIS_802_11_RTS_THRESHOLD RtsThresh;

	printk
	    ("'iwpriv <dev> set RTSThreshold' is deprecated, please use 'iwconfg <dev> rts' instead\n");

	RtsThresh = simple_strtol(arg, 0, 10);

	if ((RtsThresh > 0) && (RtsThresh <= MAX_RTS_THRESHOLD))
		pAdapter->PortCfg.RtsThreshold = (USHORT) RtsThresh;
	else if (RtsThresh == 0)
		pAdapter->PortCfg.RtsThreshold = MAX_RTS_THRESHOLD;
	else
		return FALSE;

	DBGPRINT(RT_DEBUG_TRACE, "Set_RTSThreshold_Proc::(RTSThreshold=%d)\n",
		 pAdapter->PortCfg.RtsThreshold);
	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set Fragment Threshold
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_FragThreshold_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	NDIS_802_11_FRAGMENTATION_THRESHOLD FragThresh;

	printk
	    ("'iwpriv <dev> set FragThreshold' is deprecated, please use 'iwconfg <dev> frag' instead\n");

	FragThresh = simple_strtol(arg, 0, 10);

	if ((FragThresh >= MIN_FRAG_THRESHOLD)
	    && (FragThresh <= MAX_FRAG_THRESHOLD))
		pAdapter->PortCfg.FragmentThreshold = (USHORT) FragThresh;
	else if (FragThresh == 0)
		pAdapter->PortCfg.FragmentThreshold = MAX_FRAG_THRESHOLD;
	else
		return FALSE;	//Invalid argument

	if (pAdapter->PortCfg.FragmentThreshold == MAX_FRAG_THRESHOLD)
		pAdapter->PortCfg.bFragmentZeroDisable = TRUE;
	else
		pAdapter->PortCfg.bFragmentZeroDisable = FALSE;

	DBGPRINT(RT_DEBUG_TRACE, "Set_FragThreshold_Proc::(FragThreshold=%d)\n",
		 FragThresh);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set TxBurst
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_TxBurst_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	ULONG TxBurst;

	TxBurst = simple_strtol(arg, 0, 10);

	if (TxBurst == 1)
		pAdapter->PortCfg.bEnableTxBurst = TRUE;
	else if (TxBurst == 0)
		pAdapter->PortCfg.bEnableTxBurst = FALSE;
	else
		return FALSE;	//Invalid argument

	DBGPRINT(RT_DEBUG_TRACE, "Set_TxBurst_Proc::(TxBurst=%d)\n",
		 pAdapter->PortCfg.bEnableTxBurst);

	return TRUE;
}

#ifdef AGGREGATION_SUPPORT
/*
    ==========================================================================
    Description:
        Set TxBurst
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_PktAggregate_Proc(IN PRTMP_ADAPTER pAd, IN PUCHAR arg)
{
	ULONG aggre;

	aggre = simple_strtol(arg, 0, 10);

	if (aggre == 1)
		pAd->PortCfg.bAggregationCapable = TRUE;
	else if (aggre == 0)
		pAd->PortCfg.bAggregationCapable = FALSE;
	else
		return FALSE;	//Invalid argument

	DBGPRINT(RT_DEBUG_TRACE, "Set_PktAggregate_Proc::(AGGRE=%d)\n",
		 pAd->PortCfg.bAggregationCapable);

	return TRUE;
}
#endif

/*
    ==========================================================================
    Description:
        Set TurboRate Enable or Disable
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_TurboRate_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	ULONG TurboRate;

	TurboRate = simple_strtol(arg, 0, 10);

	if (TurboRate == 1)
		pAdapter->PortCfg.EnableTurboRate = TRUE;
	else if (TurboRate == 0)
		pAdapter->PortCfg.EnableTurboRate = FALSE;
	else
		return FALSE;	//Invalid argument

	DBGPRINT(RT_DEBUG_TRACE, "Set_TurboRate_Proc::(TurboRate=%d)\n",
		 pAdapter->PortCfg.EnableTurboRate);

	return TRUE;
}

#ifdef WMM_SUPPORT
/*
    ==========================================================================
    Description:
        Set WmmCapable Enable or Disable
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_WmmCapable_Proc(IN PRTMP_ADAPTER pAd, IN PUCHAR arg)
{
	BOOLEAN bWmmCapable;

	bWmmCapable = simple_strtol(arg, 0, 10);

	if (bWmmCapable == 1)
		pAd->PortCfg.bWmmCapable = TRUE;
	else if (bWmmCapable == 0)
		pAd->PortCfg.bWmmCapable = FALSE;
	else
		return FALSE;	//Invalid argument

	DBGPRINT(RT_DEBUG_TRACE,
		 "IF(ra%d) Set_WmmCapable_Proc::(bWmmCapable=%d)\n",
		 pAd->IoctlIF, pAd->PortCfg.bWmmCapable);

	return TRUE;
}

/*
    ==========================================================================
    Description:
	Only use in WIFI APSD-STAUT.
	It's configured by "pgen test program".
    Return:
	TRUE if all parameters are OK, FALSE otherwise.
	Usage in the file of pgen.c :
		Windows client:         ./raoid.exe pm var
		Linux client:           iwpriv ra0 set APSDPsm=var
		where var= {0, 1}
    ==========================================================================
*/
static INT Set_APSDPsm_Proc(IN PRTMP_ADAPTER pAd, IN PUCHAR arg)
{
	BOOLEAN APSDPsmBit;

	if (pAd->PortCfg.bAPSDCapable == FALSE)
		return FALSE;   // do nothing!

	APSDPsmBit = simple_strtol(arg, 0, 10);
	if ((APSDPsmBit == 0) || (APSDPsmBit == 1)) {
		// Driver needs to notify AP when PSM changes
		pAd->PortCfg.bAPSDForcePowerSave = APSDPsmBit;
		if (pAd->PortCfg.bAPSDForcePowerSave != pAd->PortCfg.Psm) {
			MlmeSetPsmBit(pAd, pAd->PortCfg.bAPSDForcePowerSave);
			RTMPSendNullFrame(pAd, pAd->PortCfg.TxRate, TRUE);
		}

		DBGPRINT(RT_DEBUG_TRACE,
			 "IF(ra%d) Set_APSDPsm_Proc::(bAPSDForcePowerSave=%d)\n",
			 pAd->IoctlIF, pAd->PortCfg.bAPSDForcePowerSave);

		return TRUE;
	} else
		return FALSE;   //Invalid argument
}
#endif				/* WMM_SUPPORT */

/*
    ==========================================================================
    Description:
        Set Short Slot Time Enable or Disable
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
#if 0
static INT Set_ShortSlot_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	ULONG ShortSlot;

	ShortSlot = simple_strtol(arg, 0, 10);

	if (ShortSlot == 1)
		pAdapter->PortCfg.UseShortSlotTime = TRUE;
	else if (ShortSlot == 0)
		pAdapter->PortCfg.UseShortSlotTime = FALSE;
	else
		return FALSE;	//Invalid argument

	DBGPRINT(RT_DEBUG_TRACE, "Set_ShortSlot_Proc::(ShortSlot=%d)\n",
		 pAdapter->PortCfg.UseShortSlotTime);

	return TRUE;
}
#endif

/*
    ==========================================================================
    Description:
        Set IEEE80211H.
        This parameter is 1 when needs radar detection, otherwise 0
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_IEEE80211H_Proc(IN PRTMP_ADAPTER pAd, IN PUCHAR arg)
{
	ULONG ieee80211h;

	ieee80211h = simple_strtol(arg, 0, 10);

	if (ieee80211h == 1)
		pAd->PortCfg.bIEEE80211H = TRUE;
	else if (ieee80211h == 0)
		pAd->PortCfg.bIEEE80211H = FALSE;
	else
		return FALSE;	//Invalid argument

	DBGPRINT(RT_DEBUG_TRACE, "Set_IEEE80211H_Proc::(IEEE80211H=%d)\n",
		 pAd->PortCfg.bIEEE80211H);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set Network Type(Infrastructure/Adhoc mode)
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_NetworkType_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	printk
	    ("'iwpriv <dev> set NetworkType' is deprecated, please use 'iwconfg <dev> mode' instead\n");

	if (strcmp(arg, "Adhoc") == 0)
		pAdapter->PortCfg.BssType = BSS_ADHOC;
	else			//Default Infrastructure mode
		pAdapter->PortCfg.BssType = BSS_INFRA;

	// Reset Ralink supplicant to not use, it will be set to start when UI set PMK key
	pAdapter->PortCfg.WpaState = SS_NOTUSE;

	DBGPRINT(RT_DEBUG_TRACE, "Set_NetworkType_Proc::(NetworkType=%d)\n",
		 pAdapter->PortCfg.BssType);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set Authentication mode
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_AuthMode_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	if ((strcmp(arg, "WEPAUTO") == 0) || (strcmp(arg, "wepauto") == 0))
		pAdapter->PortCfg.AuthMode = Ndis802_11AuthModeAutoSwitch;
	else if ((strcmp(arg, "OPEN") == 0) || (strcmp(arg, "open") == 0))
		pAdapter->PortCfg.AuthMode = Ndis802_11AuthModeOpen;
	else if ((strcmp(arg, "SHARED") == 0) || (strcmp(arg, "shared") == 0))
		pAdapter->PortCfg.AuthMode = Ndis802_11AuthModeShared;
	else if ((strcmp(arg, "WPAPSK") == 0) || (strcmp(arg, "wpapsk") == 0))
		pAdapter->PortCfg.AuthMode = Ndis802_11AuthModeWPAPSK;
	else if ((strcmp(arg, "WPANONE") == 0) || (strcmp(arg, "wpanone") == 0))
		pAdapter->PortCfg.AuthMode = Ndis802_11AuthModeWPANone;
	else if ((strcmp(arg, "WPA2PSK") == 0) || (strcmp(arg, "wpa2psk") == 0))
		pAdapter->PortCfg.AuthMode = Ndis802_11AuthModeWPA2PSK;
	else if ((strcmp(arg, "WPA") == 0) || (strcmp(arg, "wpa") == 0))
		pAdapter->PortCfg.AuthMode = Ndis802_11AuthModeWPA;
	else
		return FALSE;

	pAdapter->PortCfg.PortSecured = WPA_802_1X_PORT_NOT_SECURED;

	DBGPRINT(RT_DEBUG_TRACE, "Set_AuthMode_Proc::(AuthMode=%d)\n",
		 pAdapter->PortCfg.AuthMode);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set Encryption Type
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_EncrypType_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	if ((strcmp(arg, "NONE") == 0) || (strcmp(arg, "none") == 0)) {
		if (pAdapter->PortCfg.AuthMode >= Ndis802_11AuthModeWPA)
			return TRUE;	// do nothing

		pAdapter->PortCfg.WepStatus = Ndis802_11WEPDisabled;
		pAdapter->PortCfg.PairCipher = Ndis802_11WEPDisabled;
		pAdapter->PortCfg.GroupCipher = Ndis802_11WEPDisabled;
	} else if ((strcmp(arg, "WEP") == 0) || (strcmp(arg, "wep") == 0)) {
		if (pAdapter->PortCfg.AuthMode >= Ndis802_11AuthModeWPA)
			return TRUE;	// do nothing

		pAdapter->PortCfg.WepStatus = Ndis802_11WEPEnabled;
		pAdapter->PortCfg.PairCipher = Ndis802_11WEPEnabled;
		pAdapter->PortCfg.GroupCipher = Ndis802_11WEPEnabled;
	} else if ((strcmp(arg, "TKIP") == 0) || (strcmp(arg, "tkip") == 0)) {
		if (pAdapter->PortCfg.AuthMode < Ndis802_11AuthModeWPA)
			return TRUE;	// do nothing

		pAdapter->PortCfg.WepStatus = Ndis802_11Encryption2Enabled;
		pAdapter->PortCfg.PairCipher = Ndis802_11Encryption2Enabled;
		pAdapter->PortCfg.GroupCipher = Ndis802_11Encryption2Enabled;
	} else if ((strcmp(arg, "AES") == 0) || (strcmp(arg, "aes") == 0)) {
		if (pAdapter->PortCfg.AuthMode < Ndis802_11AuthModeWPA)
			return TRUE;	// do nothing

		pAdapter->PortCfg.WepStatus = Ndis802_11Encryption3Enabled;
		pAdapter->PortCfg.PairCipher = Ndis802_11Encryption3Enabled;
		pAdapter->PortCfg.GroupCipher = Ndis802_11Encryption3Enabled;
	} else
		return FALSE;

	RTMPMakeRSNIE(pAdapter, pAdapter->PortCfg.GroupCipher);

	DBGPRINT(RT_DEBUG_TRACE, "Set_EncrypType_Proc::(EncrypType=%d)\n",
		 pAdapter->PortCfg.WepStatus);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set Default Key ID
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_DefaultKeyID_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	ULONG KeyIdx;

	printk
	    ("'iwpriv <dev> set DefaultKeyID' is deprecated, please use 'iwconfg <dev> key' instead\n");

	KeyIdx = simple_strtol(arg, 0, 10);
	if ((KeyIdx >= 1) && (KeyIdx <= 4))
		pAdapter->PortCfg.DefaultKeyId = (UCHAR) (KeyIdx - 1);
	else
		return FALSE;	//Invalid argument

	DBGPRINT(RT_DEBUG_TRACE, "Set_DefaultKeyID_Proc::(DefaultKeyID=%d)\n",
		 pAdapter->PortCfg.DefaultKeyId);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set WEP KEY1
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_Key1_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	int KeyLen;
	int i;
	UCHAR CipherAlg = CIPHER_WEP64;

	printk
	    ("'iwpriv <dev> set Key1' is deprecated, please use 'iwconfg <dev> key [2] ' instead\n");

	if (pAdapter->PortCfg.WepStatus != Ndis802_11WEPEnabled ||
	    pAdapter->PortCfg.DefaultKeyId != 0)
		return TRUE;	// do nothing

	KeyLen = strlen(arg);

	switch (KeyLen) {
	case 5:		//wep 40 Ascii type
		pAdapter->SharedKey[0].KeyLen = KeyLen;
		memcpy(pAdapter->SharedKey[0].Key, arg, KeyLen);
		CipherAlg = CIPHER_WEP64;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key1_Proc::(Key1=%s and type=%s)\n", arg,
			 "Ascii");
		break;
	case 10:		//wep 40 Hex type
		for (i = 0; i < KeyLen; i++) {
			if (!isxdigit(*(arg + i)))
				return FALSE;	//Not Hex value;
		}
		pAdapter->SharedKey[0].KeyLen = KeyLen / 2;
		AtoH(arg, pAdapter->SharedKey[0].Key, KeyLen / 2);
		CipherAlg = CIPHER_WEP64;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key1_Proc::(Key1=%s and type=%s)\n", arg, "Hex");
		break;
	case 13:		//wep 104 Ascii type
		pAdapter->SharedKey[0].KeyLen = KeyLen;
		memcpy(pAdapter->SharedKey[0].Key, arg, KeyLen);
		CipherAlg = CIPHER_WEP128;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key1_Proc::(Key1=%s and type=%s)\n", arg,
			 "Ascii");
		break;
	case 26:		//wep 104 Hex type
		for (i = 0; i < KeyLen; i++) {
			if (!isxdigit(*(arg + i)))
				return FALSE;	//Not Hex value;
		}
		pAdapter->SharedKey[0].KeyLen = KeyLen / 2;
		AtoH(arg, pAdapter->SharedKey[0].Key, KeyLen / 2);
		CipherAlg = CIPHER_WEP128;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key1_Proc::(Key1=%s and type=%s)\n", arg, "Hex");
		break;
	default:		//Invalid argument
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key1_Proc::Invalid argument (=%s)\n", arg);
		return FALSE;
	}

	pAdapter->SharedKey[0].CipherAlg = CipherAlg;

	// Set keys (into ASIC)
	if (pAdapter->PortCfg.AuthMode >= Ndis802_11AuthModeWPA) ;	// not support
	else			// Old WEP stuff
	{
		AsicAddSharedKeyEntry(pAdapter,
				      0,
				      0,
				      pAdapter->SharedKey[0].CipherAlg,
				      pAdapter->SharedKey[0].Key, NULL, NULL);
	}

	return TRUE;
}

/*
    ==========================================================================

    Description:
        Set WEP KEY2
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_Key2_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	int KeyLen;
	int i;
	UCHAR CipherAlg = CIPHER_WEP64;

	printk
	    ("'iwpriv <dev> set Key2' is deprecated, please use 'iwconfg <dev> key [2] ' instead\n");

	if (pAdapter->PortCfg.WepStatus != Ndis802_11WEPEnabled ||
	    pAdapter->PortCfg.DefaultKeyId != 1)
		return TRUE;	// do nothing

	KeyLen = strlen(arg);

	switch (KeyLen) {
	case 5:		//wep 40 Ascii type
		pAdapter->SharedKey[1].KeyLen = KeyLen;
		memcpy(pAdapter->SharedKey[1].Key, arg, KeyLen);
		CipherAlg = CIPHER_WEP64;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key2_Proc::(Key2=%s and type=%s)\n", arg,
			 "Ascii");
		break;
	case 10:		//wep 40 Hex type
		for (i = 0; i < KeyLen; i++) {
			if (!isxdigit(*(arg + i)))
				return FALSE;	//Not Hex value;
		}
		pAdapter->SharedKey[1].KeyLen = KeyLen / 2;
		AtoH(arg, pAdapter->SharedKey[1].Key, KeyLen / 2);
		CipherAlg = CIPHER_WEP64;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key2_Proc::(Key2=%s and type=%s)\n", arg, "Hex");
		break;
	case 13:		//wep 104 Ascii type
		pAdapter->SharedKey[1].KeyLen = KeyLen;
		memcpy(pAdapter->SharedKey[1].Key, arg, KeyLen);
		CipherAlg = CIPHER_WEP128;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key2_Proc::(Key2=%s and type=%s)\n", arg,
			 "Ascii");
		break;
	case 26:		//wep 104 Hex type
		for (i = 0; i < KeyLen; i++) {
			if (!isxdigit(*(arg + i)))
				return FALSE;	//Not Hex value;
		}
		pAdapter->SharedKey[1].KeyLen = KeyLen / 2;
		AtoH(arg, pAdapter->SharedKey[1].Key, KeyLen / 2);
		CipherAlg = CIPHER_WEP128;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key2_Proc::(Key2=%s and type=%s)\n", arg, "Hex");
		break;
	default:		//Invalid argument
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key2_Proc::Invalid argument (=%s)\n", arg);
		return FALSE;
	}
	pAdapter->SharedKey[1].CipherAlg = CipherAlg;

	// Set keys (into ASIC)
	if (pAdapter->PortCfg.AuthMode >= Ndis802_11AuthModeWPA) ;	// not support
	else			// Old WEP stuff
	{
		AsicAddSharedKeyEntry(pAdapter,
				      0,
				      1,
				      pAdapter->SharedKey[1].CipherAlg,
				      pAdapter->SharedKey[1].Key, NULL, NULL);
	}

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set WEP KEY3
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_Key3_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	int KeyLen;
	int i;
	UCHAR CipherAlg = CIPHER_WEP64;

	printk
	    ("'iwpriv <dev> set Key3' is deprecated, please use 'iwconfg <dev> key [2] ' instead\n");

	if (pAdapter->PortCfg.WepStatus != Ndis802_11WEPEnabled ||
	    pAdapter->PortCfg.DefaultKeyId != 2)
		return TRUE;	// do nothing

	KeyLen = strlen(arg);

	switch (KeyLen) {
	case 5:		//wep 40 Ascii type
		pAdapter->SharedKey[2].KeyLen = KeyLen;
		memcpy(pAdapter->SharedKey[2].Key, arg, KeyLen);
		CipherAlg = CIPHER_WEP64;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key3_Proc::(Key3=%s and type=%s)\n", arg,
			 "Ascii");
		break;
	case 10:		//wep 40 Hex type
		for (i = 0; i < KeyLen; i++) {
			if (!isxdigit(*(arg + i)))
				return FALSE;	//Not Hex value;
		}
		pAdapter->SharedKey[2].KeyLen = KeyLen / 2;
		AtoH(arg, pAdapter->SharedKey[2].Key, KeyLen / 2);
		CipherAlg = CIPHER_WEP64;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key3_Proc::(Key3=%s and type=%s)\n", arg, "Hex");
		break;
	case 13:		//wep 104 Ascii type
		pAdapter->SharedKey[2].KeyLen = KeyLen;
		memcpy(pAdapter->SharedKey[2].Key, arg, KeyLen);
		CipherAlg = CIPHER_WEP128;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key3_Proc::(Key3=%s and type=%s)\n", arg,
			 "Ascii");
		break;
	case 26:		//wep 104 Hex type
		for (i = 0; i < KeyLen; i++) {
			if (!isxdigit(*(arg + i)))
				return FALSE;	//Not Hex value;
		}
		pAdapter->SharedKey[2].KeyLen = KeyLen / 2;
		AtoH(arg, pAdapter->SharedKey[2].Key, KeyLen / 2);
		CipherAlg = CIPHER_WEP128;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key3_Proc::(Key3=%s and type=%s)\n", arg, "Hex");
		break;
	default:		//Invalid argument
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key3_Proc::Invalid argument (=%s)\n", arg);
		return FALSE;
	}
	pAdapter->SharedKey[2].CipherAlg = CipherAlg;

	// Set keys (into ASIC)
	if (pAdapter->PortCfg.AuthMode >= Ndis802_11AuthModeWPA) ;	// not support
	else			// Old WEP stuff
	{
		AsicAddSharedKeyEntry(pAdapter,
				      0,
				      2,
				      pAdapter->SharedKey[2].CipherAlg,
				      pAdapter->SharedKey[2].Key, NULL, NULL);
	}

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set WEP KEY4
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_Key4_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	int KeyLen;
	int i;
	UCHAR CipherAlg = CIPHER_WEP64;

	printk
	    ("'iwpriv <dev> set Key4' is deprecated, please use 'iwconfg <dev> key [2] ' instead\n");

	if (pAdapter->PortCfg.WepStatus != Ndis802_11WEPEnabled ||
	    pAdapter->PortCfg.DefaultKeyId != 3)
		return TRUE;	// do nothing

	KeyLen = strlen(arg);

	switch (KeyLen) {
	case 5:		//wep 40 Ascii type
		pAdapter->SharedKey[3].KeyLen = KeyLen;
		memcpy(pAdapter->SharedKey[3].Key, arg, KeyLen);
		CipherAlg = CIPHER_WEP64;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key4_Proc::(Key4=%s and type=%s)\n", arg,
			 "Ascii");
		break;
	case 10:		//wep 40 Hex type
		for (i = 0; i < KeyLen; i++) {
			if (!isxdigit(*(arg + i)))
				return FALSE;	//Not Hex value;
		}
		pAdapter->SharedKey[3].KeyLen = KeyLen / 2;
		AtoH(arg, pAdapter->SharedKey[3].Key, KeyLen / 2);
		CipherAlg = CIPHER_WEP64;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key4_Proc::(Key4=%s and type=%s)\n", arg, "Hex");
		break;
	case 13:		//wep 104 Ascii type
		pAdapter->SharedKey[3].KeyLen = KeyLen;
		memcpy(pAdapter->SharedKey[3].Key, arg, KeyLen);
		CipherAlg = CIPHER_WEP128;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key4_Proc::(Key4=%s and type=%s)\n", arg,
			 "Ascii");
		break;
	case 26:		//wep 104 Hex type
		for (i = 0; i < KeyLen; i++) {
			if (!isxdigit(*(arg + i)))
				return FALSE;	//Not Hex value;
		}
		pAdapter->SharedKey[3].KeyLen = KeyLen / 2;
		AtoH(arg, pAdapter->SharedKey[3].Key, KeyLen / 2);
		CipherAlg = CIPHER_WEP128;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key4_Proc::(Key4=%s and type=%s)\n", arg, "Hex");
		break;
	default:		//Invalid argument
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set_Key4_Proc::Invalid argument (=%s)\n", arg);
		return FALSE;
	}
	pAdapter->SharedKey[3].CipherAlg = CipherAlg;

	// Set keys (into ASIC)
	if (pAdapter->PortCfg.AuthMode >= Ndis802_11AuthModeWPA) ;	// not support
	else			// Old WEP stuff
	{
		AsicAddSharedKeyEntry(pAdapter,
				      0,
				      3,
				      pAdapter->SharedKey[3].CipherAlg,
				      pAdapter->SharedKey[3].Key, NULL, NULL);
	}

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set WPA PSK key
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_WPAPSK_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	UCHAR keyMaterial[40];
	INT Status;

	if ((pAdapter->PortCfg.AuthMode != Ndis802_11AuthModeWPAPSK) &&
	    (pAdapter->PortCfg.AuthMode != Ndis802_11AuthModeWPA2PSK) &&
	    (pAdapter->PortCfg.AuthMode != Ndis802_11AuthModeWPANone))
		return TRUE;	// do nothing

	DBGPRINT(RT_DEBUG_TRACE, "Set_WPAPSK_Proc::(WPAPSK=%s)\n", arg);

	arg[strlen(arg)] = '\0';

	memset(keyMaterial, 0, 40);

	if ((strlen(arg) < 8) || (strlen(arg) > 64)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set failed!!(WPAPSK=%s), WPAPSK key-string required 8 ~ 64 characters \n",
			 arg);
		return FALSE;
	}

	if (strlen(arg) == 64) {
		AtoH(arg, keyMaterial, 32);
		memcpy(pAdapter->PortCfg.PskKey.Key, keyMaterial, 32);

	} else {
		PasswordHash((char *)arg, pAdapter->MlmeAux.Ssid,
			     pAdapter->MlmeAux.SsidLen, keyMaterial);
		memcpy(pAdapter->PortCfg.PskKey.Key, keyMaterial, 32);
	}

	RTMPMakeRSNIE(pAdapter, pAdapter->PortCfg.GroupCipher);

	if (pAdapter->PortCfg.BssType == BSS_ADHOC &&
	    pAdapter->PortCfg.AuthMode == Ndis802_11AuthModeWPANone) {
		Status = RTMPWPANoneAddKeyProc(pAdapter, &keyMaterial[0]);

		if (Status != NDIS_STATUS_SUCCESS)
			return FALSE;

		pAdapter->PortCfg.WpaState = SS_NOTUSE;
	} else			// Use RaConfig as PSK agent.
	{
		// Start STA supplicant state machine
		pAdapter->PortCfg.WpaState = SS_START;
	}

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Reset statistics counter

    Arguments:
        pAdapter            Pointer to our adapter
        arg

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_ResetStatCounter_Proc(IN PRTMP_ADAPTER pAd, IN PUCHAR arg)
{
	DBGPRINT(RT_DEBUG_TRACE, "==>Set_ResetStatCounter_Proc\n");

	// add the most up-to-date h/w raw counters into software counters
	NICUpdateRawCounters(pAd);

	memset(&pAd->WlanCounters, 0, sizeof(COUNTER_802_11));
	memset(&pAd->Counters8023, 0, sizeof(COUNTER_802_3));
	memset(&pAd->RalinkCounters, 0, sizeof(COUNTER_RALINK));

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set Power Saving mode
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
static INT Set_PSMode_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	if (pAdapter->PortCfg.BssType == BSS_INFRA) {
		if ((strcmp(arg, "MAX_PSP") == 0)
		    || (strcmp(arg, "max_psp") == 0)) {
			// do NOT turn on PSM bit here, wait until MlmeCheckForPsmChange()
			// to exclude certain situations.
			//     MlmeSetPsmBit(pAdapter, PWR_SAVE);
			if (pAdapter->PortCfg.bWindowsACCAMEnable == FALSE)
				pAdapter->PortCfg.WindowsPowerMode =
				    Ndis802_11PowerModeMAX_PSP;
			pAdapter->PortCfg.WindowsBatteryPowerMode =
			    Ndis802_11PowerModeMAX_PSP;
			OPSTATUS_SET_FLAG(pAdapter, fOP_STATUS_RECEIVE_DTIM);
			pAdapter->PortCfg.DefaultListenCount = 5;

		} else if ((strcmp(arg, "Fast_PSP") == 0)
			   || (strcmp(arg, "fast_psp") == 0)
			   || (strcmp(arg, "FAST_PSP") == 0)) {
			// do NOT turn on PSM bit here, wait until MlmeCheckForPsmChange()
			// to exclude certain situations.
			//     MlmeSetPsmBit(pAdapter, PWR_SAVE);
			OPSTATUS_SET_FLAG(pAdapter, fOP_STATUS_RECEIVE_DTIM);
			if (pAdapter->PortCfg.bWindowsACCAMEnable == FALSE)
				pAdapter->PortCfg.WindowsPowerMode =
				    Ndis802_11PowerModeFast_PSP;
			pAdapter->PortCfg.WindowsBatteryPowerMode =
			    Ndis802_11PowerModeFast_PSP;
			pAdapter->PortCfg.DefaultListenCount = 3;
		} else {
			//Default Ndis802_11PowerModeCAM
			// clear PSM bit immediately
			MlmeSetPsmBit(pAdapter, PWR_ACTIVE);
			OPSTATUS_SET_FLAG(pAdapter, fOP_STATUS_RECEIVE_DTIM);
			if (pAdapter->PortCfg.bWindowsACCAMEnable == FALSE)
				pAdapter->PortCfg.WindowsPowerMode =
				    Ndis802_11PowerModeCAM;
			pAdapter->PortCfg.WindowsBatteryPowerMode =
			    Ndis802_11PowerModeCAM;
		}

		DBGPRINT(RT_DEBUG_TRACE, "Set_PSMode_Proc::(PSMode=%d)\n",
			 pAdapter->PortCfg.WindowsPowerMode);
	} else
		return FALSE;

	return TRUE;
}

static struct {
	CHAR *name;
	 INT(*set_proc) (PRTMP_ADAPTER pAdapter, PUCHAR arg);
} *PRTMP_PRIVATE_SET_PROC, RTMP_PRIVATE_SUPPORT_PROC[] = {
	{
	"DriverVersion", Set_DriverVersion_Proc}, {
	"CountryRegion", Set_CountryRegion_Proc}, {
	"CountryRegionABand", Set_CountryRegionABand_Proc}, {
	"SSID", Set_SSID_Proc}, {
	"WirelessMode", Set_WirelessMode_Proc}, {
	"TxRate", Set_TxRate_Proc}, {
	"Channel", Set_Channel_Proc}, {
	"BGProtection", Set_BGProtection_Proc}, {
	"TxPreamble", Set_TxPreamble_Proc}, {
	"RTSThreshold", Set_RTSThreshold_Proc}, {
	"FragThreshold", Set_FragThreshold_Proc}, {
	"TxBurst", Set_TxBurst_Proc},
#ifdef AGGREGATION_SUPPORT
	{
	"PktAggregate", Set_PktAggregate_Proc},
#endif
	{
	"TurboRate", Set_TurboRate_Proc},
#ifdef WMM_SUPPORT
	{
	"WmmCapable", Set_WmmCapable_Proc}, {
	"APSDPsm", Set_APSDPsm_Proc},
#endif
//      {"ShortSlot", Set_ShortSlot_Proc},
	{
	"IEEE80211H", Set_IEEE80211H_Proc}, {
	"NetworkType", Set_NetworkType_Proc}, {
	"AuthMode", Set_AuthMode_Proc}, {
	"EncrypType", Set_EncrypType_Proc}, {
	"DefaultKeyID", Set_DefaultKeyID_Proc}, {
	"Key1", Set_Key1_Proc}, {
	"Key2", Set_Key2_Proc}, {
	"Key3", Set_Key3_Proc}, {
	"Key4", Set_Key4_Proc}, {
	"WPAPSK", Set_WPAPSK_Proc}, {
	"ResetCounter", Set_ResetStatCounter_Proc}, {
	"PSMode", Set_PSMode_Proc},
#ifdef RALINK_ATE
	{
	"ATE", Set_ATE_Proc},	// set ATE Mode to: STOP, TXCONT, TXCARR, TXFRAME, RXFRAME
	{
	"ATEDA", Set_ATE_DA_Proc},	// set ATE TxFrames ADDR1, DA
	{
	"ATESA", Set_ATE_SA_Proc},	// set ATE TxFrames ADDR2, SA
	{
	"ATEBSSID", Set_ATE_BSSID_Proc},	// set ATE TxFrames ADDR3, BSSID
	{
	"ATECHANNEL", Set_ATE_CHANNEL_Proc},	// set ATE Channel
	{
	"ATETXPOW", Set_ATE_TX_POWER_Proc},	// set ATE TxPower
	{
	"ATETXFREQOFFSET", Set_ATE_TX_FREQOFFSET_Proc},	//set ATE RF frequency offset
	{
	"ATETXLEN", Set_ATE_TX_LENGTH_Proc},	// set ATE TxLength
	{
	"ATETXCNT", Set_ATE_TX_COUNT_Proc},	// set ATE TxCount
	{
	"ATETXRATE", Set_ATE_TX_RATE_Proc},	// set ATE TxRate
#endif				// RALINK_ATE
	{
	NULL,}
};

static char *rtstrchr(const char *s, int c)
{
	for (; *s != (char)c; ++s)
		if (*s == '\0')
			return NULL;
	return (char *)s;
}

/**
 * rstrtok - Split a string into tokens
 * @s: The string to be searched
 * @ct: The characters to search for
 * * WARNING: strtok is deprecated, use strsep instead. However strsep is not compatible with old architecture.
 */
char *__rstrtok;
char *rstrtok(char *s, const char *ct)
{
	char *sbegin, *send;

	sbegin = s ? s : __rstrtok;
	if (!sbegin) {
		return NULL;
	}

	sbegin += strspn(sbegin, ct);
	if (*sbegin == '\0') {
		__rstrtok = NULL;
		return (NULL);
	}

	send = strpbrk(sbegin, ct);
	if (send && *send != '\0')
		*send++ = '\0';

	__rstrtok = send;

	return (sbegin);
}

/*
This is required for LinEX2004/kernel2.6.7 to provide iwlist scanning function
*/

static int rt_ioctl_giwname(struct net_device *dev,
		 struct iw_request_info *info, char *name, char *extra)
{
	strncpy(name, "RT61 Wireless", IFNAMSIZ);
	return 0;
}

static int rt_ioctl_siwfreq(struct net_device *dev,
		     struct iw_request_info *info,
		     struct iw_freq *freq, char *extra)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;
	int chan = -1;

	if (freq->e > 1)
		return -EINVAL;

	if ((freq->e == 0) && (freq->m <= 1000))
		chan = freq->m;	// Setting by channel number
	else
		MAP_KHZ_TO_CHANNEL_ID((freq->m / 100), chan);	// Setting by frequency - search the table , like 2.412G, 2.422G,
	pAdapter->PortCfg.Channel = chan;
	DBGPRINT(RT_DEBUG_TRACE,
		 "==>rt_ioctl_siwfreq::SIOCSIWFREQ[cmd=0x%x] (Channel=%d)\n",
		 SIOCSIWFREQ, pAdapter->PortCfg.Channel);

	if (RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)
	    && pAdapter->PortCfg.BssType == BSS_MONITOR) {
		pAdapter->PortCfg.Channel = chan;
		AsicSwitchChannel(pAdapter, pAdapter->PortCfg.Channel);
		AsicLockChannel(pAdapter, pAdapter->PortCfg.Channel);
	}

	return 0;
}

static int rt_ioctl_giwfreq(struct net_device *dev,
		     struct iw_request_info *info,
		     struct iw_freq *freq, char *extra)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;
	DBGPRINT(RT_DEBUG_TRACE, "==>rt_ioctl_giwfreq  %d\n",
		 pAdapter->PortCfg.Channel);

	MAP_CHANNEL_ID_TO_KHZ(pAdapter->PortCfg.Channel, freq->m);
	// MW: Alter the multiplier so iwconfig reports GhZ
	freq->e = 3;
	freq->i = 0;
	return 0;
}

static int rt_ioctl_siwmode(struct net_device *dev,
		     struct iw_request_info *info, __u32 * mode, char *extra)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;

	switch (*mode) {
	case IW_MODE_ADHOC:
		if (pAdapter->PortCfg.BssType != BSS_ADHOC) {
			// Config has changed
			pAdapter->bConfigChanged = TRUE;
		}
		pAdapter->PortCfg.BssType = BSS_ADHOC;
		DBGPRINT(RT_DEBUG_TRACE,
			 "===>rt_ioctl_siwmode::SIOCSIWMODE (AD-HOC)\n");
		break;
	case IW_MODE_INFRA:
		if (pAdapter->PortCfg.BssType != BSS_INFRA) {
			// Config has changed
			pAdapter->bConfigChanged = TRUE;
		}
		pAdapter->PortCfg.BssType = BSS_INFRA;
		DBGPRINT(RT_DEBUG_TRACE,
			 "===>rt_ioctl_siwmode::SIOCSIWMODE (INFRA)\n");
		break;
	case IW_MODE_MONITOR:
		if (pAdapter->PortCfg.BssType != BSS_MONITOR) {
			// Config has changed
			pAdapter->bConfigChanged = TRUE;
		}
		pAdapter->PortCfg.BssType = BSS_MONITOR;
		DBGPRINT(RT_DEBUG_TRACE,
			 "===>rt_ioctl_siwmode::SIOCSIWMODE (MONITOR)\n");
		break;
	default:
		DBGPRINT(RT_DEBUG_TRACE,
			 "===>rt_ioctl_siwmode::SIOCSIWMODE (unknown)\n");
		return -ENOSYS;
	}

	if (pAdapter->bConfigChanged == TRUE) {
		if (pAdapter->PortCfg.BssType == BSS_MONITOR) {
			if (pAdapter->PortCfg.RFMONTX == TRUE)
				pAdapter->net_dev->type = ARPHRD_IEEE80211;
			else
				pAdapter->net_dev->type =
				    ARPHRD_IEEE80211_PRISM;
		} else
			pAdapter->net_dev->type = ARPHRD_ETHER;
		RTMPWriteTXRXCsr0(pAdapter, FALSE, TRUE);
	}
	// Reset Ralink supplicant to not use, it will be set to start when UI set PMK key
	pAdapter->PortCfg.WpaState = SS_NOTUSE;

	return 0;
}

static int rt_ioctl_giwmode(struct net_device *dev,
		     struct iw_request_info *info, __u32 * mode, char *extra)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;

	if (pAdapter->PortCfg.BssType == BSS_ADHOC)
		*mode = IW_MODE_ADHOC;
	else if (pAdapter->PortCfg.BssType == BSS_INFRA)
		*mode = IW_MODE_INFRA;
	else if (pAdapter->PortCfg.BssType == BSS_MONITOR)
		*mode = IW_MODE_MONITOR;
	else
		*mode = IW_MODE_AUTO;

	DBGPRINT(RT_DEBUG_TRACE, "==>rt_ioctl_giwmode\n");
	return 0;
}

#if 0
static int rt_ioctl_siwsens(struct net_device *dev,
		     struct iw_request_info *info, char *name, char *extra)
{
	return 0;
}

static int rt_ioctl_giwsens(struct net_device *dev,
		     struct iw_request_info *info, char *name, char *extra)
{
	return 0;
}
#endif

static int rt_ioctl_giwrange(struct net_device *dev,
		      struct iw_request_info *info,
		      struct iw_point *data, char *extra)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;
	struct iw_range *range = (struct iw_range *)extra;
	u16 val;
	int i, chan;

	DBGPRINT(RT_DEBUG_TRACE, "===>rt_ioctl_giwrange\n");
	data->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	range->txpower_capa = IW_TXPOW_DBM;

	if (INFRA_ON(pAdapter) || ADHOC_ON(pAdapter)) {
		range->min_pmp = 1 * 1024;
		range->max_pmp = 65535 * 1024;
		range->min_pmt = 1 * 1024;
		range->max_pmt = 1000 * 1024;
		range->pmp_flags = IW_POWER_PERIOD;
		range->pmt_flags = IW_POWER_TIMEOUT;
		range->pm_capa = IW_POWER_PERIOD | IW_POWER_TIMEOUT |
		    IW_POWER_UNICAST_R | IW_POWER_ALL_R;
	}

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 14;

	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT;
	range->min_retry = 0;
	range->max_retry = 255;

	val = 0;
	for (i = 0; i < 14; i++) {
		chan = pAdapter->ChannelList[val].Channel;
		if (chan != 0) {
			range->freq[val].i = chan;
			MAP_CHANNEL_ID_TO_KHZ(range->freq[val].i,
					      range->freq[val].m);
			range->freq[val].m *= 100;
			range->freq[val].e = 1;
			val++;
		}
	}

	range->num_frequency = val;
	range->num_channels = val;

	val = 0;
	for (i = 0; i < pAdapter->PortCfg.SupRateLen; i++) {
		range->bitrate[i] =
		    1000000 * (pAdapter->PortCfg.SupRate[i] & 0x7f) / 2;
		val++;
		if (val == IW_MAX_BITRATES)
			break;
	}
	range->num_bitrates = val;

	range->max_qual.qual = 100;	/* % sig quality */
	range->max_qual.level = 0;	/* dB */
	range->max_qual.noise = 0;	/* dB */

	/* What would be suitable values for "average/typical" qual? */
	range->avg_qual.qual = 20;
	range->avg_qual.level = -60;
	range->avg_qual.noise = -95;
	range->sensitivity = 3;

	range->max_encoding_tokens = NR_WEP_KEYS;
	range->num_encoding_sizes = 2;
	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;

#if 0
	over2 = 0;
	len = prism2_get_datarates(dev, rates);
	range->num_bitrates = 0;
	for (i = 0; i < len; i++) {
		if (range->num_bitrates < IW_MAX_BITRATES) {
			range->bitrate[range->num_bitrates] = rates[i] * 500000;
			range->num_bitrates++;
		}
		if (rates[i] == 0x0b || rates[i] == 0x16)
			over2 = 1;
	}
	/* estimated maximum TCP throughput values (bps) */
	range->throughput = over2 ? 5500000 : 1500000;
#endif
	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	return 0;
}

static int rt_ioctl_giwap(struct net_device *dev,
		   struct iw_request_info *info,
		   struct sockaddr *ap_addr, char *extra)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;

	if (INFRA_ON(pAdapter) || ADHOC_ON(pAdapter)) {
		ap_addr->sa_family = ARPHRD_ETHER;
		memcpy(ap_addr->sa_data, &pAdapter->PortCfg.Bssid, ETH_ALEN);
	} else {
		DBGPRINT(RT_DEBUG_TRACE, "IOCTL::SIOCGIWAP(=EMPTY)\n");
		return -ENOTCONN;
	}

	return 0;
}

/*
 * Units are in db above the noise floor. That means the
 * rssi values reported in the tx/rx descriptors in the
 * driver are the SNR expressed in db.
 *
 * If you assume that the noise floor is -95, which is an
 * excellent assumption 99.5 % of the time, then you can
 * derive the absolute signal level (i.e. -95 + rssi).
 * There are some other slight factors to take into account
 * depending on whether the rssi measurement is from 11b,
 * 11g, or 11a.   These differences are at most 2db and
 * can be documented.
 *
 * NB: various calculations are based on the orinoco/wavelan
 *     drivers for compatibility
 */
static void set_quality(PRTMP_ADAPTER pAdapter,
			struct iw_quality *iq, u_int rssi)
{
	u32 ChannelQuality, NorRssi;

	// Normalize Rssi
	if (rssi > 0x50)
		NorRssi = 100;
	else if (rssi < 0x20)
		NorRssi = 0;
	else
		NorRssi = (rssi - 0x20) * 2;

	// ChannelQuality = W1*RSSI + W2*TxPRR + W3*RxPER    (RSSI 0..100), (TxPER 100..0), (RxPER 100..0)
	ChannelQuality = (RSSI_WEIGHTING * NorRssi +
			  TX_WEIGHTING * (100 - 0) +
			  RX_WEIGHTING * (100 - 0)) / 100;

	if (ChannelQuality >= 100)
		ChannelQuality = 100;

	iq->qual = ChannelQuality;

#ifdef RTMP_EMBEDDED
	iq->level = rt_abs(rssi);	// signal level (dBm)
#else
	iq->level = abs(rssi);	// signal level (dBm)
#endif
	iq->level += 256 - pAdapter->BbpRssiToDbmDelta;
	iq->noise = (pAdapter->BbpWriteLatch[17] > pAdapter->BbpTuning.R17UpperBoundG) ? pAdapter->BbpTuning.R17UpperBoundG : ((ULONG) pAdapter->BbpWriteLatch[17]);	// noise level (dBm)
	iq->noise += 256 - 143;
	iq->updated = pAdapter->iw_stats.qual.updated;
}

static int rt_ioctl_iwaplist(struct net_device *dev,
		      struct iw_request_info *info,
		      struct iw_point *data, char *extra)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;

	struct sockaddr addr[IW_MAX_AP];
	struct iw_quality qual[IW_MAX_AP];
	int i;

	for (i = 0; i < IW_MAX_AP; i++) {
		if (i >= pAdapter->ScanTab.BssNr)
			break;
		addr[i].sa_family = ARPHRD_ETHER;
		memcpy(addr[i].sa_data, &pAdapter->ScanTab.BssEntry[i].Bssid,
		       ETH_ALEN);
		set_quality(pAdapter, &qual[i],
			    pAdapter->ScanTab.BssEntry[i].Rssi);
	}
	data->length = i;
	memcpy(extra, &addr, i * sizeof(addr[0]));
	data->flags = 1;	/* signal quality present (sort of) */
	memcpy(extra + i * sizeof(addr[0]), &qual, i * sizeof(qual[i]));

	return 0;
}

#ifdef SIOCGIWSCAN
static int rt_ioctl_siwscan(struct net_device *dev,
		     struct iw_request_info *info,
		     struct iw_point *data, char *extra)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;
	unsigned long Now;
	int Status = NDIS_STATUS_SUCCESS;

	//check if the interface is down
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE) &&
	    !RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_START_UP)) {
		DBGPRINT(RT_DEBUG_TRACE, "INFO::Network is down!\n");
		return -ENETDOWN;
	}
	//BOOLEAN               StateMachineTouched = FALSE;
	if (RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
		return 0;
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_MLME_INITIALIZED))
		return 0;
	do {
		Now = jiffies;

		if ((OPSTATUS_TEST_FLAG
		     (pAdapter, fOP_STATUS_MEDIA_STATE_CONNECTED))
		    && ((pAdapter->PortCfg.AuthMode == Ndis802_11AuthModeWPA)
			|| (pAdapter->PortCfg.AuthMode ==
			    Ndis802_11AuthModeWPAPSK)
#if WPA_SUPPLICANT_SUPPORT
			|| (pAdapter->PortCfg.IEEE8021X == TRUE)
#endif
		    )
		    && (pAdapter->PortCfg.PortSecured ==
			WPA_802_1X_PORT_NOT_SECURED)
		    ) {
			DBGPRINT(RT_DEBUG_TRACE,
				 "!!! Link UP, Port Not Secured! ignore this set::OID_802_11_BSSID_LIST_SCAN\n");
			Status = NDIS_STATUS_SUCCESS;
			break;
		}

		if (pAdapter->Mlme.CntlMachine.CurrState != CNTL_IDLE) {
			MlmeRestartStateMachine(pAdapter);
			DBGPRINT(RT_DEBUG_TRACE,
				 "!!! MLME busy, reset MLME state machine !!!\n");
		}
		// tell CNTL state machine to call NdisMSetInformationComplete() after completing
		// this request, because this request is initiated by NDIS.
		pAdapter->MlmeAux.CurrReqIsFromNdis = FALSE;
		// Reset allowed scan retries
		pAdapter->PortCfg.ScanCnt = 0;
		pAdapter->PortCfg.LastScanTime = Now;

		MlmeEnqueue(pAdapter,
			    MLME_CNTL_STATE_MACHINE,
			    OID_802_11_BSSID_LIST_SCAN, 0, NULL);

		Status = NDIS_STATUS_SUCCESS;
		//StateMachineTouched = TRUE;
		MlmeHandler(pAdapter);
	} while (0);
	return 0;
}

#define MAX_CUSTOM_LEN 64

static int rt_ioctl_giwscan(struct net_device *dev,
		 struct iw_request_info *info,
		 struct iw_point *data, char *extra)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;
	int i = 2, j;
	char *current_ev = extra, *previous_ev = extra;
	char *end_buf = extra + IW_SCAN_MAX_DATA;
	char *current_val;
	struct iw_event iwe;

#if 0	// support bit rate, extended rate, quality and last beacon timing
	//-------------------------------------------
	char custom[MAX_CUSTOM_LEN];
	char *p;
	char SupRateLen, ExtRateLen;
	char rate, max_rate;
	int k;
	//-------------------------------------------
#endif

	for (i = 0; i < pAdapter->ChannelListNum; i++) {
		if (!RTMP_TEST_FLAG
		    (pAdapter, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
			goto report_scan;
		msleep(MAX_CHANNEL_TIME);
	}
	return -EBUSY;

      report_scan:
	for (i = 0; i < pAdapter->ScanTab.BssNr; i++) {
		if (current_ev >= end_buf)
			break;

		//MAC address
		//================================
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWAP;
		iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
		memcpy(iwe.u.ap_addr.sa_data,
		       &pAdapter->ScanTab.BssEntry[i].Bssid, ETH_ALEN);

		previous_ev = current_ev;
		current_ev =
		    iwe_stream_add_event(current_ev, end_buf, &iwe,
					 IW_EV_ADDR_LEN);
		if (current_ev == previous_ev)
			break;

		//ESSID
		//================================
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWESSID;
		iwe.u.data.length = pAdapter->ScanTab.BssEntry[i].SsidLen;
		iwe.u.data.flags = 1;

		previous_ev = current_ev;
		current_ev =
		    iwe_stream_add_point(current_ev, end_buf, &iwe,
					 pAdapter->ScanTab.BssEntry[i].Ssid);
		if (current_ev == previous_ev)
			break;

		//Network Type
		//================================
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWMODE;
		if (pAdapter->ScanTab.BssEntry[i].BssType == Ndis802_11IBSS) {
			iwe.u.mode = IW_MODE_ADHOC;
		} else if (pAdapter->ScanTab.BssEntry[i].BssType ==
			   Ndis802_11Infrastructure) {
			iwe.u.mode = IW_MODE_INFRA;
		} else {
			iwe.u.mode = IW_MODE_AUTO;
		}
		iwe.len = IW_EV_UINT_LEN;

		previous_ev = current_ev;
		current_ev =
		    iwe_stream_add_event(current_ev, end_buf, &iwe,
					 IW_EV_UINT_LEN);
		if (current_ev == previous_ev)
			break;

		//Channel and Frequency
		//================================
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWFREQ;
		if (INFRA_ON(pAdapter) || ADHOC_ON(pAdapter))
			iwe.u.freq.m = pAdapter->ScanTab.BssEntry[i].Channel;
		else
			iwe.u.freq.m = pAdapter->ScanTab.BssEntry[i].Channel;
		iwe.u.freq.e = 0;
		iwe.u.freq.i = 0;

		previous_ev = current_ev;
		current_ev =
		    iwe_stream_add_event(current_ev, end_buf, &iwe,
					 IW_EV_FREQ_LEN);
		if (current_ev == previous_ev)
			break;

		//Encyption key
		//================================
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWENCODE;
		if (CAP_IS_PRIVACY_ON
		    (pAdapter->ScanTab.BssEntry[i].CapabilityInfo))
			iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
		else
			iwe.u.data.flags = IW_ENCODE_DISABLED;

		previous_ev = current_ev;
		current_ev =
		    iwe_stream_add_point(current_ev, end_buf, &iwe,
					 (char *)pAdapter->SharedKey[(iwe.u.data.flags & IW_ENCODE_INDEX) - 1].Key);
		if (current_ev == previous_ev)
			break;

#if 1				// support bit rate
		//Bit Rate
		//================================
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWRATE;
		current_val = current_ev + IW_EV_LCP_LEN;
		//for (j = 0; j < pAdapter->ScanTab.BssEntry[i].RatesLen;j++)
		for (j = 0; j < 1; j++) {
			iwe.u.bitrate.value =
			    RateIdToMbps[pAdapter->ScanTab.BssEntry[i].
					 SupRate[i] / 2] * 1000000;
			iwe.u.bitrate.disabled = 0;
			current_val = iwe_stream_add_value(current_ev,
							   current_val, end_buf,
							   &iwe,
							   IW_EV_PARAM_LEN);

			if ((current_val - current_ev) > IW_EV_LCP_LEN)
				current_ev = current_val;
			else
				break;

		}
#else	// support bit rate, extended rate, quality and last beacon timing
		// max. of displays used IW_SCAN_MAX_DATA are about 22~24 cells
		//Bit Rate
		//================================
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWRATE;
		current_val = current_ev + IW_EV_LCP_LEN;

		SupRateLen = pAdapter->ScanTab.BssEntry[i].SupRateLen;
		ExtRateLen = pAdapter->ScanTab.BssEntry[i].ExtRateLen;

		max_rate = 0;
		p = custom;
		p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
			      " Rates (Mb/s): ");
		for (k = 0, j = 0; k < SupRateLen;) {
			if (j < ExtRateLen &&
			    ((pAdapter->ScanTab.BssEntry[i].ExtRate[j] & 0x7F) <
			     (pAdapter->ScanTab.BssEntry[i].SupRate[k] & 0x7F)))
			{
				rate =
				    pAdapter->ScanTab.BssEntry[i].
				    ExtRate[j++] & 0x7F;
			} else {
				rate =
				    pAdapter->ScanTab.BssEntry[i].
				    SupRate[k++] & 0x7F;
			}

			if (rate > max_rate)
				max_rate = rate;
			p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
				      "%d%s ", rate >> 1,
				      (rate & 1) ? ".5" : "");
		}

		for (; j < ExtRateLen; j++) {
			rate = pAdapter->ScanTab.BssEntry[i].ExtRate[j] & 0x7F;
			p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
				      "%d%s ", rate >> 1,
				      (rate & 1) ? ".5" : "");
			if (rate > max_rate)
				max_rate = rate;
		}
		iwe.u.bitrate.value = max_rate * 500000;
		iwe.u.bitrate.disabled = 0;
		current_val = iwe_stream_add_value(current_ev,
						   current_val, end_buf, &iwe,
						   IW_EV_PARAM_LEN);
		if ((current_val - current_ev) > IW_EV_LCP_LEN)
			current_ev = current_val;
		else
			break;

		//Extended Rate
		//================================
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		iwe.u.data.length = p - custom;
		if (iwe.u.data.length) {
			previous_ev = current_ev;
			current_ev =
			    iwe_stream_add_point(current_ev, end_buf, &iwe,
						 custom);
			if (current_ev == previous_ev)
				break;
		}
		//Quality Statistics
		//================================
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVQUAL;
		set_quality(pAdapter, &iwe.u.qual,
			    pAdapter->ScanTab.BssEntry[i].Rssi);

		previous_ev = current_ev;
		current_ev =
		    iwe_stream_add_event(current_ev, end_buf, &iwe,
					 IW_EV_QUAL_LEN);
		if (current_ev == previous_ev)
			break;

		//Age to display seconds since last beacon
		//================================
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		p = custom;
		p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
			      " Last beacon: %lums ago",
			      (jiffies -
			       pAdapter->ScanTab.BssEntry[i].LastBeaconRxTime) /
			      (HZ / 100));
		iwe.u.data.length = p - custom;
		if (iwe.u.data.length) {
			previous_ev = current_ev;
			current_ev =
			    iwe_stream_add_point(current_ev, end_buf, &iwe,
						 custom);
			if (current_ev == previous_ev)
				break;
#endif

		//================================
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVQUAL;
		if (memcmp
		    (&pAdapter->ScanTab.BssEntry[i].Bssid,
		     &pAdapter->PortCfg.Bssid, ETH_ALEN) == 0)
			iwe.u.qual.qual = pAdapter->Mlme.ChannelQuality;
		else
			iwe.u.qual.qual = 0;
		iwe.u.qual.level =
		    pAdapter->ScanTab.BssEntry[i].Rssi - RSSI_TO_DBM_OFFSET;
		iwe.u.qual.noise = 0;
		current_ev =
		    iwe_stream_add_event(current_ev, end_buf, &iwe,
					 IW_EV_QUAL_LEN);
		//================================
		memset(&iwe, 0, sizeof(iwe));
	}
	data->length = current_ev - extra;
	DBGPRINT(RT_DEBUG_TRACE, "===>rt_ioctl_giwscan. %d(%d) BSS returned\n",
		 i, pAdapter->ScanTab.BssNr);
	return 0;
}
#endif

static int rt_ioctl_siwessid(struct net_device *dev,
		      struct iw_request_info *info,
		      struct iw_point *data, char *essid)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;
	NDIS_802_11_SSID Ssid;
	memset(&Ssid, 0x00, sizeof(NDIS_802_11_SSID));

	if (data->flags && data->length) {
		// Associate with the specific ESSID provided:
		//   # iwconfig [interface] essid <ESSID>
		u8 len;
		if (data->length > IW_ESSID_MAX_SIZE)
			return -E2BIG;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
		len = data->length - 1;		//minus null character.
#else
		len = data->length;
#endif
		memcpy(Ssid.Ssid, essid, len);
		Ssid.SsidLength = len;
	} else {
		// Associate with any ESSID:
		//   # iwconfig [interface] essid any
		//   # iwconfig [interface] essid ""
		// Reset to infra/open/none (Ssid already zeroed)
		pAdapter->PortCfg.BssType = BSS_INFRA;
		pAdapter->PortCfg.AuthMode = Ndis802_11AuthModeOpen;
		pAdapter->PortCfg.WepStatus = Ndis802_11EncryptionDisabled;
	}
	// Update desired settings
	memcpy(pAdapter->PortCfg.Ssid, Ssid.Ssid, Ssid.SsidLength);
	pAdapter->PortCfg.SsidLen = Ssid.SsidLength;

	DBGPRINT(RT_DEBUG_TRACE,
		 "===>rt_ioctl_siwessid:: (Ssid.SsidLength = %d, %s)\n",
		 Ssid.SsidLength, Ssid.Ssid);

	// If interface is down don't run mlme
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE))
		return 0;

	if (pAdapter->Mlme.CntlMachine.CurrState != CNTL_IDLE) {
		MlmeRestartStateMachine(pAdapter);
		DBGPRINT(RT_DEBUG_TRACE,
			 "!!! MLME busy, reset MLME state machine !!!\n");
	}
	// tell CNTL state machine to call NdisMSetInformationComplete() after completing
	// this request, because this request is initiated by NDIS.
	// To prevent some kernel thread is very low priority ...so essid copy immediately for later wpapsk counting.
	if ((pAdapter->PortCfg.AuthMode >= Ndis802_11AuthModeWPA)
	    && (pAdapter->PortCfg.AuthMode != Ndis802_11AuthModeWPANone))
		memcpy(pAdapter->MlmeAux.Ssid, Ssid.Ssid, Ssid.SsidLength);
	pAdapter->MlmeAux.CurrReqIsFromNdis = FALSE;
	MlmeEnqueue(pAdapter, MLME_CNTL_STATE_MACHINE, OID_802_11_SSID,
			sizeof(NDIS_802_11_SSID), (VOID *) &Ssid);
	MlmeHandler(pAdapter);
	return 0;
}

static int rt_ioctl_giwessid(struct net_device *dev,
		      struct iw_request_info *info,
		      struct iw_point *data, char *essid)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;

	data->flags = 1;	/* active */
	data->length = pAdapter->PortCfg.SsidLen;
	memcpy(essid, pAdapter->PortCfg.Ssid, pAdapter->PortCfg.SsidLen);
	pAdapter->PortCfg.Ssid[pAdapter->PortCfg.SsidLen] = '\0';
	DBGPRINT(RT_DEBUG_TRACE,
		 "===>rt_ioctl_giwessid:: (Len=%d, ssid=%s...)\n",
		 pAdapter->PortCfg.SsidLen, pAdapter->PortCfg.Ssid);

	return 0;

}

static int rt_ioctl_siwnickn(struct net_device *dev,
		      struct iw_request_info *info,
		      struct iw_point *data, char *nickname)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;

	if (data->length > IW_ESSID_MAX_SIZE)
		return -EINVAL;

	memset(pAdapter->nickn, 0, IW_ESSID_MAX_SIZE);
	memcpy(pAdapter->nickn, nickname, data->length);

	return 0;
}

static int rt_ioctl_giwnickn(struct net_device *dev,
		      struct iw_request_info *info,
		      struct iw_point *data, char *nickname)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;

	if (data->length > strlen(pAdapter->nickn) + 1)
		data->length = strlen(pAdapter->nickn) + 1;
	if (data->length > 0) {
		memcpy(nickname, pAdapter->nickn, data->length - 1);
		nickname[data->length - 1] = '\0';
	}
	return 0;
}

static int rt_ioctl_siwrts(struct net_device *dev,
		    struct iw_request_info *info,
		    struct iw_param *rts, char *extra)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;
	u16 val;

	if (rts->disabled)
		val = MAX_RTS_THRESHOLD;
	else if (rts->value < 0 || rts->value > MAX_RTS_THRESHOLD)
		return -EINVAL;
	else if (rts->value == 0)
		val = MAX_RTS_THRESHOLD;
	else
		val = rts->value;

	if (val != pAdapter->PortCfg.RtsThreshold)
		pAdapter->PortCfg.RtsThreshold = val;

	return 0;
}

static int rt_ioctl_giwrts(struct net_device *dev,
		    struct iw_request_info *info,
		    struct iw_param *rts, char *extra)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;

	rts->value = pAdapter->PortCfg.RtsThreshold;
	rts->disabled = (rts->value == MAX_RTS_THRESHOLD);
	rts->fixed = 1;

	return 0;
}

static int rt_ioctl_siwfrag(struct net_device *dev,
		     struct iw_request_info *info,
		     struct iw_param *rts, char *extra)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;
	u16 val;

	if (rts->disabled)
		val = MAX_FRAG_THRESHOLD;
	else if (rts->value >= MIN_FRAG_THRESHOLD
		 || rts->value <= MAX_FRAG_THRESHOLD)
		val = rts->value;
	else if (rts->value == 0)
		val = MAX_FRAG_THRESHOLD;
	else
		return -EINVAL;

	pAdapter->PortCfg.FragmentThreshold = val;

	if (pAdapter->PortCfg.FragmentThreshold == MAX_FRAG_THRESHOLD)
		pAdapter->PortCfg.bFragmentZeroDisable = TRUE;
	else
		pAdapter->PortCfg.bFragmentZeroDisable = FALSE;

	return 0;
}

static int rt_ioctl_giwfrag(struct net_device *dev,
		     struct iw_request_info *info,
		     struct iw_param *rts, char *extra)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;

	rts->value = pAdapter->PortCfg.FragmentThreshold;
	rts->disabled = (rts->value >= MAX_FRAG_THRESHOLD);
	rts->fixed = 1;

	return 0;
}

static int rt_ioctl_siwencode(struct net_device *dev,
		       struct iw_request_info *info,
		       struct iw_point *erq, char *keybuf)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER) dev->priv;
	int idx;
	BOOLEAN updateAsic = FALSE;

	// Set Authentication Mode
	if (erq->flags & IW_ENCODE_RESTRICTED)
		pAd->PortCfg.AuthMode = Ndis802_11AuthModeShared;
	else
		pAd->PortCfg.AuthMode = Ndis802_11AuthModeOpen;

	// Set WEP encryption on/off
	if (erq->flags & IW_ENCODE_DISABLED) {
		pAd->PortCfg.PairCipher = Ndis802_11WEPDisabled;
		pAd->PortCfg.GroupCipher = Ndis802_11WEPDisabled;
		pAd->PortCfg.WepStatus = Ndis802_11WEPDisabled;
		// Can't have encrypted Auth Mode if WEP is disabled
		pAd->PortCfg.AuthMode = Ndis802_11AuthModeOpen;
	} else {
		pAd->PortCfg.PairCipher = Ndis802_11WEPEnabled;
		pAd->PortCfg.GroupCipher = Ndis802_11WEPEnabled;
		pAd->PortCfg.WepStatus = Ndis802_11WEPEnabled;
	}

	// Get key index (if any)
	idx = (erq->flags & IW_ENCODE_INDEX);

	if (erq->flags & IW_ENCODE_NOKEY && !(erq->flags & IW_ENCODE_DISABLED)) {
		// No key provided: set active index
		if ((idx < 1) || (idx > NR_WEP_KEYS))
			return -EINVAL;
		idx--;
		pAd->PortCfg.DefaultKeyId = idx;
		updateAsic = TRUE;
	} else {
		// Key provided: set key material
		CIPHER_KEY *cypher;
		u16 len = erq->length;
		if (len > WEP_LARGE_KEY_LEN)
			len = WEP_LARGE_KEY_LEN;
		if ((idx < 0) || (idx > NR_WEP_KEYS))
			return -EINVAL;
		// No index provided? Use current
		idx = idx == 0 ? pAd->PortCfg.DefaultKeyId : idx - 1;
		// Set current key? Update Asic
		if (idx == pAd->PortCfg.DefaultKeyId)
			updateAsic = TRUE;
		cypher = &pAd->PortCfg.DesireSharedKey[idx];
		// Parse key
		memset(&cypher->Key, 0, MAX_LEN_OF_KEY);
		memcpy(&cypher->Key, keybuf, len);
		cypher->KeyLen = len <= WEP_SMALL_KEY_LEN ? WEP_SMALL_KEY_LEN :
							WEP_LARGE_KEY_LEN;
		cypher->CipherAlg = len == WEP_SMALL_KEY_LEN ? CIPHER_WEP64 :
							CIPHER_WEP128;
	}

	// Encryption on and current key changed? Update ASIC
	if (updateAsic && !(erq->flags & IW_ENCODE_DISABLED)) {
		CIPHER_KEY *cipher = &pAd->PortCfg.DesireSharedKey[idx];
		memcpy(&pAd->SharedKey[idx].Key, &cipher->Key, cipher->KeyLen);
		pAd->SharedKey[idx].KeyLen = cipher->KeyLen;
		pAd->SharedKey[idx].CipherAlg = cipher->CipherAlg;
		AsicAddSharedKeyEntry(pAd, 0, (UCHAR) idx,
					pAd->SharedKey[idx].CipherAlg,
					pAd->SharedKey[idx].Key,
					NULL, NULL);
	}
	DBGPRINT(RT_DEBUG_TRACE, "==>rt_ioctl_siwencode::AuthMode=%x\n",
		 pAd->PortCfg.AuthMode);
	DBGPRINT(RT_DEBUG_TRACE,
		 "==>rt_ioctl_siwencode::DefaultKeyId=%x, KeyLen = %d\n",
		 pAd->PortCfg.DefaultKeyId,
		 pAd->SharedKey[pAd->PortCfg.DefaultKeyId].KeyLen);
	DBGPRINT(RT_DEBUG_TRACE, "==>rt_ioctl_siwencode::WepStatus=%x\n",
		 pAd->PortCfg.WepStatus);
	return 0;
}

static int rt_ioctl_giwencode(struct net_device *dev,
		   struct iw_request_info *info,
		   struct iw_point *erq, char *key)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;
	int kid;

	kid = erq->flags & IW_ENCODE_INDEX;
	DBGPRINT(RT_DEBUG_TRACE, "===>rt_ioctl_giwencode %d\n",
		 erq->flags & IW_ENCODE_INDEX);

	if (pAdapter->PortCfg.WepStatus == Ndis802_11WEPDisabled) {
		erq->length = 0;
		erq->flags = IW_ENCODE_DISABLED;
	} else if ((kid > 0) && (kid <= 4)) {
		// copy wep key
		erq->flags = kid;	/* NB: base 1 */
		if (erq->length > pAdapter->SharedKey[kid - 1].KeyLen)
			erq->length = pAdapter->SharedKey[kid - 1].KeyLen;
		memcpy(key, pAdapter->SharedKey[kid - 1].Key, erq->length);
		//if ((kid == pAdapter->PortCfg.DefaultKeyId))
		//erq->flags |= IW_ENCODE_ENABLED;      /* XXX */
		if (pAdapter->PortCfg.AuthMode == Ndis802_11AuthModeShared)
			erq->flags |= IW_ENCODE_RESTRICTED;	/* XXX */
		else
			erq->flags |= IW_ENCODE_OPEN;	/* XXX */

	} else if (kid == 0) {
		if (pAdapter->PortCfg.AuthMode == Ndis802_11AuthModeShared)
			erq->flags |= IW_ENCODE_RESTRICTED;	/* XXX */
		else
			erq->flags |= IW_ENCODE_OPEN;	/* XXX */
		erq->length =
		    pAdapter->SharedKey[pAdapter->PortCfg.DefaultKeyId].KeyLen;
		memcpy(key,
		       pAdapter->SharedKey[pAdapter->PortCfg.DefaultKeyId].Key,
		       erq->length);
		// copy default key ID
		if (pAdapter->PortCfg.AuthMode == Ndis802_11AuthModeShared)
			erq->flags |= IW_ENCODE_RESTRICTED;	/* XXX */
		else
			erq->flags |= IW_ENCODE_OPEN;	/* XXX */
		erq->flags = pAdapter->PortCfg.DefaultKeyId + 1;	/* NB: base 1 */
		erq->flags |= IW_ENCODE_ENABLED;	/* XXX */
	}

	return 0;
}

static int rt_ioctl_setparam(struct net_device *dev, struct iw_request_info *info,
		  void *w, char *extra)
{
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;
	char *this_char = extra;
	char *value;
	int Status = 0;

	//check if the interface is down
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, "INFO::Network is down!\n");
		return -ENETDOWN;
	}

	if (!*this_char)
		return Status;

	if ((value = rtstrchr(this_char, '=')) != NULL)
		*value++ = 0;

	if (!value)
		return Status;

	// reject setting nothing besides ANY ssid(ssidLen=0)
	if (!*value && (strcmp(this_char, "SSID") != 0))
		return Status;

	for (PRTMP_PRIVATE_SET_PROC = RTMP_PRIVATE_SUPPORT_PROC;
	     PRTMP_PRIVATE_SET_PROC->name; PRTMP_PRIVATE_SET_PROC++) {
		if (strcmp(this_char, PRTMP_PRIVATE_SET_PROC->name) == 0) {
			if (!PRTMP_PRIVATE_SET_PROC->set_proc(pAdapter, value)) {	//FALSE:Set private failed then return Invalid argument
				Status = -EINVAL;
			}
			break;	//Exit for loop.
		}
	}

	if (PRTMP_PRIVATE_SET_PROC->name == NULL) {	//Not found argument
		Status = -EINVAL;
		DBGPRINT(RT_DEBUG_TRACE,
			 "===>rt_ioctl_setparam:: (iwpriv) Not Support Set Command [%s=%s]\n",
			 this_char, value);
	}

	return Status;
}

static const iw_handler rt_handler[] = {
	(iw_handler) NULL,	/* SIOCSIWCOMMIT */
	(iw_handler) rt_ioctl_giwname,	/* SIOCGIWNAME  1 */
	(iw_handler) NULL,	/* SIOCSIWNWID */
	(iw_handler) NULL,	/* SIOCGIWNWID */
	(iw_handler) rt_ioctl_siwfreq,	/* SIOCSIWFREQ */
	(iw_handler) rt_ioctl_giwfreq,	/* SIOCGIWFREQ  5 */
	(iw_handler) rt_ioctl_siwmode,	/* SIOCSIWMODE */
	(iw_handler) rt_ioctl_giwmode,	/* SIOCGIWMODE */
	(iw_handler) NULL,	/* SIOCSIWSENS */
	(iw_handler) NULL,	/* SIOCGIWSENS */
	(iw_handler) NULL /* not used */ ,	/* SIOCSIWRANGE */
	(iw_handler) rt_ioctl_giwrange,	/* SIOCGIWRANGE 11 */
	(iw_handler) NULL /* not used */ ,	/* SIOCSIWPRIV */
	(iw_handler) NULL /* kernel code */ ,	/* SIOCGIWPRIV */
	(iw_handler) NULL /* not used */ ,	/* SIOCSIWSTATS */
	(iw_handler) NULL /* kernel code */ ,	/* SIOCGIWSTATS f */
	(iw_handler) NULL,	/* SIOCSIWSPY */
	(iw_handler) NULL,	/* SIOCGIWSPY */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* SIOCSIWAP */
	(iw_handler) rt_ioctl_giwap,	/* SIOCGIWAP    0x15 */
	(iw_handler) NULL,	/* -- hole --   0x16 */
	(iw_handler) rt_ioctl_iwaplist,	/* SIOCGIWAPLIST */
#ifdef SIOCGIWSCAN
	(iw_handler) rt_ioctl_siwscan,	/* SIOCSIWSCAN  0x18 */
	(iw_handler) rt_ioctl_giwscan,	/* SIOCGIWSCAN */
#else
	(iw_handler) NULL,	/* SIOCSIWSCAN */
	(iw_handler) NULL,	/* SIOCGIWSCAN */
#endif				/* SIOCGIWSCAN */
	(iw_handler) rt_ioctl_siwessid,	/* SIOCSIWESSID */
	(iw_handler) rt_ioctl_giwessid,	/* SIOCGIWESSID */
	(iw_handler) rt_ioctl_siwnickn,	/* SIOCSIWNICKN */
	(iw_handler) rt_ioctl_giwnickn,	/* SIOCGIWNICKN 1d */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* SIOCSIWRATE  20 */
	(iw_handler) NULL,	/* SIOCGIWRATE */
	(iw_handler) rt_ioctl_siwrts,	/* SIOCSIWRTS */
	(iw_handler) rt_ioctl_giwrts,	/* SIOCGIWRTS */
	(iw_handler) rt_ioctl_siwfrag,	/* SIOCSIWFRAG */
	(iw_handler) rt_ioctl_giwfrag,	/* SIOCGIWFRAG  25 */
	(iw_handler) NULL,	/* SIOCSIWTXPOW */
	(iw_handler) NULL,	/* SIOCGIWTXPOW */
	(iw_handler) NULL,	/* SIOCSIWRETRY */
	(iw_handler) NULL,	/* SIOCGIWRETRY  29 */
	(iw_handler) rt_ioctl_siwencode,	/* SIOCSIWENCODE 2a */
	(iw_handler) rt_ioctl_giwencode,	/* SIOCGIWENCODE 2b */
	(iw_handler) NULL,	/* SIOCSIWPOWER  2c */
	(iw_handler) NULL,	/* SIOCGIWPOWER  2d */
};

static const iw_handler rt_priv_handlers[] = {
	(iw_handler) rt_ioctl_setparam,	/* SIOCWFIRSTPRIV+2 */
	(iw_handler) NULL,
};

const struct iw_handler_def rt61_iw_handler_def = {
#define	N(a)	(sizeof (a) / sizeof (a[0]))
	.standard = (iw_handler *) rt_handler,
	.num_standard = sizeof(rt_handler) / sizeof(iw_handler),
	.private = (iw_handler *) rt_priv_handlers,
	.num_private = N(rt_priv_handlers),
	.private_args = (struct iw_priv_args *)privtab,
	.num_private_args = N(privtab),
#if WIRELESS_EXT > 15
//      .spy_offset     = offsetof(struct hostap_interface, spy_data),
#endif				/* WIRELESS_EXT > 15 */
#if WIRELESS_EXT > 16
	.get_wireless_stats = &RT61_get_wireless_stats,
#endif
};

INT RTMPSetInformation(IN PRTMP_ADAPTER pAdapter,
		       IN OUT struct ifreq *rq, IN INT cmd)
{
	struct iwreq *wrq = (struct iwreq *)rq;
	NDIS_802_11_SSID Ssid, *pSsid = NULL;
	NDIS_802_11_MAC_ADDRESS Bssid;
	RT_802_11_PHY_MODE PhyMode;
	RT_802_11_STA_CONFIG StaConfig;
	NDIS_802_11_RATES aryRates;
	RT_802_11_PREAMBLE Preamble;
	NDIS_802_11_WEP_STATUS WepStatus;
	NDIS_802_11_AUTHENTICATION_MODE AuthMode;
	NDIS_802_11_NETWORK_INFRASTRUCTURE BssType;
	NDIS_802_11_RTS_THRESHOLD RtsThresh;
	NDIS_802_11_FRAGMENTATION_THRESHOLD FragThresh;
	NDIS_802_11_POWER_MODE PowerMode;
	NDIS_802_11_TX_POWER_LEVEL TxPowerLevel;
	PNDIS_802_11_KEY pKey = NULL;
	PNDIS_802_11_REMOVE_KEY pRemoveKey = NULL;
	NDIS_802_11_CONFIGURATION Config, *pConfig = NULL;
	NDIS_802_11_NETWORK_TYPE NetType;
	unsigned long Now;
	ULONG KeyIdx;
	INT Status = NDIS_STATUS_SUCCESS;
	ULONG AntDiv;
	ULONG PowerTemp;
	BOOLEAN RadioState;
	BOOLEAN StateMachineTouched = FALSE;
#if WPA_SUPPLICANT_SUPPORT
	PNDIS_802_11_WEP pWepKey = NULL;
	PNDIS_802_11_PMKID pPmkId = NULL;
	BOOLEAN IEEE8021xState;
	BOOLEAN IEEE8021x_required_keys;
	BOOLEAN wpa_supplicant_enable;
#endif
//    USHORT                              TxTotalCnt;

	switch (cmd & 0x7FFF) {
	case RT_OID_802_11_COUNTRY_REGION:
		if (wrq->u.data.length < sizeof(UCHAR))
			Status = -EINVAL;
		else if (!(pAdapter->PortCfg.CountryRegion & 0x80) && !(pAdapter->PortCfg.CountryRegionForABand & 0x80))	// Only avaliable when EEPROM not programming
		{
			ULONG Country;
			UCHAR TmpPhy;

			if (copy_from_user
			    (&Country, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				pAdapter->PortCfg.CountryRegion =
				    (UCHAR) (Country & 0x000000FF);
				pAdapter->PortCfg.CountryRegionForABand =
				    (UCHAR) ((Country >> 8) & 0x000000FF);
				TmpPhy = pAdapter->PortCfg.PhyMode;
				pAdapter->PortCfg.PhyMode = 0xff;
				// Build all corresponding channel information
				RTMPSetPhyMode(pAdapter, TmpPhy);
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::RT_OID_802_11_COUNTRY_REGION (A:%d  B/G:%d)\n",
					 pAdapter->PortCfg.
					 CountryRegionForABand,
					 pAdapter->PortCfg.CountryRegion);
			}
		}
		break;
	case OID_802_11_BSSID_LIST_SCAN:
		Now = jiffies;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set::OID_802_11_BSSID_LIST_SCAN, TxCnt = %d \n",
			 pAdapter->RalinkCounters.LastOneSecTotalTxCount);

		if (pAdapter->RalinkCounters.LastOneSecTotalTxCount > 100) {
			DBGPRINT(RT_DEBUG_TRACE,
				 "!!! Link UP, ignore this set::OID_802_11_BSSID_LIST_SCAN\n");
			//Status = NDIS_STATUS_SUCCESS;
			pAdapter->PortCfg.ScanCnt = 99;	// Prevent auto scan triggered by this OID
			break;
		}

		if ((OPSTATUS_TEST_FLAG
		     (pAdapter, fOP_STATUS_MEDIA_STATE_CONNECTED))
		    && ((pAdapter->PortCfg.AuthMode == Ndis802_11AuthModeWPA)
			|| (pAdapter->PortCfg.AuthMode ==
			    Ndis802_11AuthModeWPAPSK)
			|| (pAdapter->PortCfg.AuthMode ==
			    Ndis802_11AuthModeWPA2)
			|| (pAdapter->PortCfg.AuthMode ==
			    Ndis802_11AuthModeWPA2PSK)
#if WPA_SUPPLICANT_SUPPORT
			|| (pAdapter->PortCfg.IEEE8021X == TRUE)
#endif
		    ) &&
		    (pAdapter->PortCfg.PortSecured ==
		     WPA_802_1X_PORT_NOT_SECURED)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 "!!! Link UP, Port Not Secured! ignore this set::OID_802_11_BSSID_LIST_SCAN\n");
			Status = NDIS_STATUS_SUCCESS;
			pAdapter->PortCfg.ScanCnt = 99;	// Prevent auto scan triggered by this OID
			break;
		}

		if (pAdapter->Mlme.CntlMachine.CurrState != CNTL_IDLE) {
			MlmeRestartStateMachine(pAdapter);
			DBGPRINT(RT_DEBUG_TRACE,
				 "!!! MLME busy, reset MLME state machine !!!\n");
		}
		// tell CNTL state machine to call NdisMSetInformationComplete() after completing
		// this request, because this request is initiated by NDIS.
		pAdapter->MlmeAux.CurrReqIsFromNdis = FALSE;
		// Reset allowed scan retries
		pAdapter->PortCfg.ScanCnt = 0;
		// reset SSID to null
		if (pSsid->SsidLength == 0) {
			memcpy(pSsid->Ssid, "", 0);
		}
		pAdapter->bConfigChanged = TRUE;
		pAdapter->PortCfg.LastScanTime = Now;

		MlmeEnqueue(pAdapter,
			    MLME_CNTL_STATE_MACHINE,
			    OID_802_11_BSSID_LIST_SCAN, 0, NULL);

		Status = NDIS_STATUS_SUCCESS;
		StateMachineTouched = TRUE;
		break;
	case OID_802_11_SSID:
		if (wrq->u.data.length != sizeof(NDIS_802_11_SSID))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&Ssid, wrq->u.data.pointer, wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				pSsid = &Ssid;

				if (pSsid->SsidLength > MAX_LEN_OF_SSID)
					Status = -EINVAL;
				else {
					if (pAdapter->Mlme.CntlMachine.
					    CurrState != CNTL_IDLE) {
						MlmeRestartStateMachine
						    (pAdapter);
						DBGPRINT(RT_DEBUG_TRACE,
							 "!!! MLME busy, reset MLME state machine !!!\n");
					}
					// tell CNTL state machine to call NdisMSetInformationComplete() after completing
					// this request, because this request is initiated by NDIS.
					pAdapter->MlmeAux.CurrReqIsFromNdis =
					    FALSE;

					// Reset allowed scan retries
					pAdapter->PortCfg.ScanCnt = 0;

					MlmeEnqueue(pAdapter,
						    MLME_CNTL_STATE_MACHINE,
						    OID_802_11_SSID,
						    sizeof(NDIS_802_11_SSID),
						    (VOID *) pSsid);
					Status = NDIS_STATUS_SUCCESS;
					StateMachineTouched = TRUE;

					DBGPRINT(RT_DEBUG_TRACE,
						 "Set::OID_802_11_SSID (Len=%d,Ssid=%s)\n",
						 pSsid->SsidLength,
						 pSsid->Ssid);
				}
			}
		}
		break;
	case OID_802_11_BSSID:
		if (wrq->u.data.length != sizeof(NDIS_802_11_MAC_ADDRESS))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&Bssid, wrq->u.data.pointer, wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				if (pAdapter->Mlme.CntlMachine.CurrState !=
				    CNTL_IDLE) {
					MlmeRestartStateMachine(pAdapter);
					DBGPRINT(RT_DEBUG_TRACE,
						 "!!! MLME busy, reset MLME state machine !!!\n");
				}
				// tell CNTL state machine to call NdisMSetInformationComplete() after completing
				// this request, because this request is initiated by NDIS.
				pAdapter->MlmeAux.CurrReqIsFromNdis = FALSE;

				// Reset allowed scan retries
				pAdapter->PortCfg.ScanCnt = 0;

				MlmeEnqueue(pAdapter, MLME_CNTL_STATE_MACHINE,
					    OID_802_11_BSSID,
					    sizeof(NDIS_802_11_MAC_ADDRESS),
					    (VOID *) & Bssid);
				Status = NDIS_STATUS_SUCCESS;
				StateMachineTouched = TRUE;
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_BSSID %02x:%02x:%02x:%02x:%02x:%02x\n",
					 Bssid[0], Bssid[1], Bssid[2], Bssid[3],
					 Bssid[4], Bssid[5]);
			}
		}
		break;
	case RT_OID_802_11_RADIO:
		if (wrq->u.data.length != sizeof(BOOLEAN))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&RadioState, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::RT_OID_802_11_RADIO (=%d)\n",
					 RadioState);
				if (pAdapter->PortCfg.bSwRadio != RadioState) {
					pAdapter->PortCfg.bSwRadio = RadioState;
					if (pAdapter->PortCfg.bRadio !=
					    (pAdapter->PortCfg.bHwRadio
					     && pAdapter->PortCfg.bSwRadio)) {
						pAdapter->PortCfg.bRadio =
						    (pAdapter->PortCfg.bHwRadio
						     && pAdapter->PortCfg.
						     bSwRadio);
						if (pAdapter->PortCfg.bRadio ==
						    TRUE) {
							MlmeRadioOn(pAdapter);
							// Update extra information
							pAdapter->ExtraInfo =
							    EXTRA_INFO_CLEAR;
						} else {
							MlmeRadioOff(pAdapter);
							// Update extra information
							pAdapter->ExtraInfo =
							    SW_RADIO_OFF;
						}
					}
				}
			}
		}
		break;
	case RT_OID_802_11_PHY_MODE:
		if (wrq->u.data.length != sizeof(RT_802_11_PHY_MODE))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&PhyMode, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				RTMPSetPhyMode(pAdapter, PhyMode);
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::RT_OID_802_11_PHY_MODE (=%d)\n",
					 PhyMode);
			}
		}
		break;
	case RT_OID_802_11_STA_CONFIG:
		if (wrq->u.data.length != sizeof(RT_802_11_STA_CONFIG))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&StaConfig, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				pAdapter->PortCfg.bEnableTxBurst =
				    StaConfig.EnableTxBurst;
				pAdapter->PortCfg.EnableTurboRate = 0;	//StaConfig.EnableTurboRate;
				pAdapter->PortCfg.UseBGProtection =
				    StaConfig.UseBGProtection;
				pAdapter->PortCfg.UseShortSlotTime = 1;	// 2003-10-30 always SHORT SLOT capable
				if (pAdapter->PortCfg.AdhocMode !=
				    StaConfig.AdhocMode) {
					// allow dynamic change of "USE OFDM rate or not" in ADHOC mode
					// if setting changed, need to reset current TX rate as well as BEACON frame format
					pAdapter->PortCfg.AdhocMode =
					    StaConfig.AdhocMode;
					if (pAdapter->PortCfg.BssType ==
					    BSS_ADHOC) {
						MlmeUpdateTxRates(pAdapter,
								  FALSE);
						MakeIbssBeacon(pAdapter);	// re-build BEACON frame
						AsicEnableIbssSync(pAdapter);	// copy to on-chip memory
					}
				}
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::RT_OID_802_11_SET_STA_CONFIG (Burst=%d, Protection=%d,ShortSlot=%d, Adhoc=%d\n",
					 pAdapter->PortCfg.bEnableTxBurst,
//                                        pAdapter->PortCfg.EnableTurboRate,
					 pAdapter->PortCfg.UseBGProtection,
					 pAdapter->PortCfg.UseShortSlotTime,
					 pAdapter->PortCfg.AdhocMode);
			}
		}
		break;
	case OID_802_11_DESIRED_RATES:
		if (wrq->u.data.length != sizeof(NDIS_802_11_RATES))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&aryRates, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				memset(pAdapter->PortCfg.DesireRate, 0,
				       MAX_LEN_OF_SUPPORTED_RATES);
				memcpy(pAdapter->PortCfg.DesireRate, &aryRates,
				       sizeof(NDIS_802_11_RATES));
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_DESIRED_RATES (%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x)\n",
					 pAdapter->PortCfg.DesireRate[0],
					 pAdapter->PortCfg.DesireRate[1],
					 pAdapter->PortCfg.DesireRate[2],
					 pAdapter->PortCfg.DesireRate[3],
					 pAdapter->PortCfg.DesireRate[4],
					 pAdapter->PortCfg.DesireRate[5],
					 pAdapter->PortCfg.DesireRate[6],
					 pAdapter->PortCfg.DesireRate[7]);
				// Changing DesiredRate may affect the MAX TX rate we used to TX frames out
				MlmeUpdateTxRates(pAdapter, FALSE);
			}
		}
		break;
	case RT_OID_802_11_PREAMBLE:
		if (wrq->u.data.length != sizeof(RT_802_11_PREAMBLE))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&Preamble, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				if (Preamble == Rt802_11PreambleShort) {
					pAdapter->PortCfg.TxPreamble = Preamble;
					MlmeSetTxPreamble(pAdapter,
							  Rt802_11PreambleShort);
				} else if ((Preamble == Rt802_11PreambleLong)
					   || (Preamble ==
					       Rt802_11PreambleAuto)) {
					// if user wants AUTO, initialize to LONG here, then change according to AP's
					// capability upon association.
					pAdapter->PortCfg.TxPreamble = Preamble;
					MlmeSetTxPreamble(pAdapter,
							  Rt802_11PreambleLong);
				} else {
					Status = EINVAL;
					break;
				}
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::RT_OID_802_11_PREAMBLE (=%d)\n",
					 Preamble);
			}
		}
		break;
	case OID_802_11_WEP_STATUS:
		if (wrq->u.data.length != sizeof(NDIS_802_11_WEP_STATUS))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&WepStatus, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				// Since TKIP, AES, WEP are all supported. It should not have any invalid setting
				if (WepStatus <= Ndis802_11Encryption3KeyAbsent) {
					if (pAdapter->PortCfg.WepStatus !=
					    WepStatus) {
						// Config has changed
						pAdapter->bConfigChanged = TRUE;
					}
					pAdapter->PortCfg.WepStatus = WepStatus;
					pAdapter->PortCfg.PairCipher =
					    WepStatus;
					pAdapter->PortCfg.GroupCipher =
					    WepStatus;
				} else {
					Status = -EINVAL;
					break;
				}
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_WEP_STATUS (=%d)\n",
					 WepStatus);
			}
		}
		break;
	case OID_802_11_AUTHENTICATION_MODE:
		if (wrq->u.data.length !=
		    sizeof(NDIS_802_11_AUTHENTICATION_MODE))

			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&AuthMode, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				if (AuthMode > Ndis802_11AuthModeMax) {
					Status = -EINVAL;
					break;
				} else {
					if (pAdapter->PortCfg.AuthMode !=
					    AuthMode) {
						// Config has changed
						pAdapter->bConfigChanged = TRUE;
					}
					pAdapter->PortCfg.AuthMode = AuthMode;
				}
				pAdapter->PortCfg.PortSecured =
				    WPA_802_1X_PORT_NOT_SECURED;
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_AUTHENTICATION_MODE (=%d) \n",
					 pAdapter->PortCfg.AuthMode);
			}
		}
		break;
	case OID_802_11_INFRASTRUCTURE_MODE:
		if (wrq->u.data.length !=
		    sizeof(NDIS_802_11_NETWORK_INFRASTRUCTURE))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&BssType, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				if (BssType == Ndis802_11IBSS) {
					if (pAdapter->PortCfg.BssType !=
					    BSS_ADHOC) {
						if (INFRA_ON(pAdapter)) {
							// Set the AutoReconnectSsid to prevent it reconnect to old SSID
							// Since calling this indicate user don't want to connect to that SSID anymore.
							pAdapter->MlmeAux.
							    AutoReconnectSsidLen
							    = 32;
							memset(pAdapter->
							       MlmeAux.
							       AutoReconnectSsid,
							       0,
							       pAdapter->
							       MlmeAux.
							       AutoReconnectSsidLen);
							LinkDown(pAdapter,
								 FALSE);
							// First cancel linkdown timer
							del_timer_sync
							    (&pAdapter->Mlme.
							     LinkDownTimer);
							DBGPRINT(RT_DEBUG_TRACE,
								 "NDIS_STATUS_MEDIA_DISCONNECT Event BB!\n");
						}
					}
					pAdapter->PortCfg.BssType = BSS_ADHOC;
					DBGPRINT(RT_DEBUG_TRACE,
						 "Set::OID_802_11_INFRASTRUCTURE_MODE (AD-HOC)\n");
				} else if (BssType == Ndis802_11Infrastructure) {
					if (pAdapter->PortCfg.BssType !=
					    BSS_INFRA) {
						if (ADHOC_ON(pAdapter)) {
							// Set the AutoReconnectSsid to prevent it reconnect to old SSID
							// Since calling this indicate user don't want to connect to that SSID anymore.
							pAdapter->MlmeAux.
							    AutoReconnectSsidLen
							    = 32;
							memset(pAdapter->
							       MlmeAux.
							       AutoReconnectSsid,
							       0,
							       pAdapter->
							       MlmeAux.
							       AutoReconnectSsidLen);
							LinkDown(pAdapter,
								 FALSE);
						}
					}
					pAdapter->PortCfg.BssType = BSS_INFRA;
					DBGPRINT(RT_DEBUG_TRACE,
						 "Set::OID_802_11_INFRASTRUCTURE_MODE (INFRA)\n");
				} else {
					Status = -EINVAL;
					DBGPRINT(RT_DEBUG_TRACE,
						 "Set::OID_802_11_INFRASTRUCTURE_MODE (unknown)\n");
				}
				// Reset Ralink supplicant to not use, it will be set to start when UI set PMK key
				pAdapter->PortCfg.WpaState = SS_NOTUSE;
			}
		}
		break;
	case OID_802_11_REMOVE_WEP:
		DBGPRINT(RT_DEBUG_TRACE, "Set::OID_802_11_REMOVE_WEP\n");
		if (wrq->u.data.length != sizeof(NDIS_802_11_KEY_INDEX)) {
			Status = -EINVAL;
		} else {
			KeyIdx = *(NDIS_802_11_KEY_INDEX *) wrq->u.data.pointer;

			if (KeyIdx & 0x80000000) {
				// Should never set default bit when remove key
				Status = -EINVAL;
			} else {
				KeyIdx = KeyIdx & 0x0fffffff;
				if (KeyIdx >= 4) {
					Status = -EINVAL;
				} else {
					pAdapter->SharedKey[KeyIdx].KeyLen = 0;
					pAdapter->SharedKey[KeyIdx].CipherAlg =
					    CIPHER_NONE;
					AsicRemoveSharedKeyEntry(pAdapter, 0,
								 (UCHAR)
								 KeyIdx);
				}
			}
		}
		break;
	case RT_OID_802_11_RESET_COUNTERS:
		memset(&pAdapter->WlanCounters, 0, sizeof(COUNTER_802_11));
		memset(&pAdapter->Counters8023, 0, sizeof(COUNTER_802_3));
		memset(&pAdapter->RalinkCounters, 0, sizeof(COUNTER_RALINK));
		pAdapter->Counters8023.RxNoBuffer = 0;
		pAdapter->Counters8023.GoodReceives = 0;
		pAdapter->Counters8023.RxNoBuffer = 0;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set::RT_OID_802_11_RESET_COUNTERS \n");
		break;
	case OID_802_11_RTS_THRESHOLD:
		if (wrq->u.data.length != sizeof(NDIS_802_11_RTS_THRESHOLD))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&RtsThresh, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				if (RtsThresh > MAX_RTS_THRESHOLD)
					Status = -EINVAL;
				else
					pAdapter->PortCfg.RtsThreshold =
					    (USHORT) RtsThresh;
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_RTS_THRESHOLD (=%d)\n",
					 RtsThresh);
			}
		}
		break;
	case OID_802_11_FRAGMENTATION_THRESHOLD:
		if (wrq->u.data.length !=
		    sizeof(NDIS_802_11_FRAGMENTATION_THRESHOLD))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&FragThresh, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				pAdapter->PortCfg.bFragmentZeroDisable = FALSE;
				if (FragThresh > MAX_FRAG_THRESHOLD
				    || FragThresh < MIN_FRAG_THRESHOLD) {
					if (FragThresh == 0) {
						pAdapter->PortCfg.
						    FragmentThreshold =
						    MAX_FRAG_THRESHOLD;
						pAdapter->PortCfg.
						    bFragmentZeroDisable = TRUE;
					} else
						Status = -EINVAL;
				} else {
					pAdapter->PortCfg.FragmentThreshold =
					    (USHORT) FragThresh;
				}
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_FRAGMENTATION_THRESHOLD (=%d) \n",
					 FragThresh);
			}
		}
		break;
	case OID_802_11_POWER_MODE:
		if (wrq->u.data.length != sizeof(NDIS_802_11_POWER_MODE))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&PowerMode, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				// save user's policy here, but not change PortCfg.Psm immediately
				if (PowerMode == Ndis802_11PowerModeCAM) {
					// clear PSM bit immediately
					MlmeSetPsmBit(pAdapter, PWR_ACTIVE);

					OPSTATUS_SET_FLAG(pAdapter,
							  fOP_STATUS_RECEIVE_DTIM);
					if (pAdapter->PortCfg.
					    bWindowsACCAMEnable == FALSE)
						pAdapter->PortCfg.
						    WindowsPowerMode =
						    PowerMode;
					pAdapter->PortCfg.
					    WindowsBatteryPowerMode = PowerMode;
				} else if (PowerMode ==
					   Ndis802_11PowerModeMAX_PSP) {
					// do NOT turn on PSM bit here, wait until MlmeCheckForPsmChange()
					// to exclude certain situations.
					//     MlmeSetPsmBit(pAdapter, PWR_SAVE);
					if (pAdapter->PortCfg.
					    bWindowsACCAMEnable == FALSE)
						pAdapter->PortCfg.
						    WindowsPowerMode =
						    PowerMode;
					pAdapter->PortCfg.
					    WindowsBatteryPowerMode = PowerMode;
					OPSTATUS_SET_FLAG(pAdapter,
							  fOP_STATUS_RECEIVE_DTIM);
					pAdapter->PortCfg.DefaultListenCount =
					    5;
				} else if (PowerMode ==
					   Ndis802_11PowerModeFast_PSP) {
					// do NOT turn on PSM bit here, wait until MlmeCheckForPsmChange()
					// to exclude certain situations.
					//     MlmeSetPsmBit(pAdapter, PWR_SAVE);
					OPSTATUS_SET_FLAG(pAdapter,
							  fOP_STATUS_RECEIVE_DTIM);
					if (pAdapter->PortCfg.
					    bWindowsACCAMEnable == FALSE)
						pAdapter->PortCfg.
						    WindowsPowerMode =
						    PowerMode;
					pAdapter->PortCfg.
					    WindowsBatteryPowerMode = PowerMode;
					pAdapter->PortCfg.DefaultListenCount =
					    3;
				} else {
					Status = -EINVAL;
				}
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_POWER_MODE (=%d)\n",
					 PowerMode);
			}
		}
		break;
	case OID_802_11_TX_POWER_LEVEL:
		if (wrq->u.data.length != sizeof(NDIS_802_11_TX_POWER_LEVEL))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&TxPowerLevel, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				if (TxPowerLevel > MAX_TX_POWER_LEVEL)
					Status = -EINVAL;
				else
					pAdapter->PortCfg.TxPower =
					    (UCHAR) TxPowerLevel;
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_TX_POWER_LEVEL (=%d) \n",
					 TxPowerLevel);
			}
		}
		break;
	case RT_OID_802_11_TX_POWER_LEVEL_1:
		if (wrq->u.data.length < sizeof(ULONG))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&PowerTemp, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				if (PowerTemp > 100)
					PowerTemp = 0xffffffff;	// AUTO
				pAdapter->PortCfg.TxPowerDefault = PowerTemp;	//keep current setting.

				// Only update TxPowerPercentage if the value is smaller than current AP setting
				// TODO: 2005-03-08 john removed the following line.
				// if (pAdapter->PortCfg.TxPowerDefault < pAdapter->PortCfg.TxPowerPercentage)
				pAdapter->PortCfg.TxPowerPercentage =
				    pAdapter->PortCfg.TxPowerDefault;
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::RT_OID_802_11_TX_POWER_LEVEL_1 (=%d)\n",
					 pAdapter->PortCfg.TxPowerPercentage);
			}
		}
		break;
	case OID_802_11_NETWORK_TYPE_IN_USE:
		if (wrq->u.data.length != sizeof(NDIS_802_11_NETWORK_TYPE))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&NetType, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				if (NetType == Ndis802_11DS)
					RTMPSetPhyMode(pAdapter, PHY_11B);
				else if (NetType == Ndis802_11OFDM24)
					RTMPSetPhyMode(pAdapter,
						       PHY_11BG_MIXED);
				else if (NetType == Ndis802_11OFDM5)
					RTMPSetPhyMode(pAdapter, PHY_11A);
				else
					Status = -EINVAL;
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_NETWORK_TYPE_IN_USE (=%d)\n",
					 NetType);
			}
		}
		break;
	case OID_802_11_RX_ANTENNA_SELECTED:
		if (wrq->u.data.length != sizeof(NDIS_802_11_ANTENNA))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&AntDiv, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				if (AntDiv == 0)
					pAdapter->Antenna.field.RxDefaultAntenna = 1;	// ant-A
				else if (AntDiv == 1)
					pAdapter->Antenna.field.RxDefaultAntenna = 2;	// ant-B
				else
					pAdapter->Antenna.field.RxDefaultAntenna = 0;	// diversity

				pAdapter->PortCfg.BandState = UNKNOWN_BAND;
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_RX_ANTENNA_SELECTED (=%d)\n",
					 AntDiv);
				AsicAntennaSelect(pAdapter,
						  pAdapter->LatchRfRegs.
						  Channel);
			}
		}
		break;
	case OID_802_11_TX_ANTENNA_SELECTED:
		if (wrq->u.data.length != sizeof(NDIS_802_11_ANTENNA))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&AntDiv, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				if (AntDiv == 0)
					pAdapter->Antenna.field.TxDefaultAntenna = 1;	// ant-A
				else if (AntDiv == 1)
					pAdapter->Antenna.field.TxDefaultAntenna = 2;	// ant-B
				else
					pAdapter->Antenna.field.TxDefaultAntenna = 0;	// diversity

				pAdapter->PortCfg.BandState = UNKNOWN_BAND;
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_RX_ANTENNA_SELECTED (=%d)\n",
					 AntDiv);
				AsicAntennaSelect(pAdapter,
						  pAdapter->LatchRfRegs.
						  Channel);
			}
		}
		break;
		// For WPA PSK PMK key
	case RT_OID_802_11_ADD_WPA:
		pKey = kmalloc(wrq->u.data.length, MEM_ALLOC_FLAG);
		if (pKey == NULL) {
			Status = -ENOMEM;
			break;
		}

		if (copy_from_user
		    (pKey, wrq->u.data.pointer, wrq->u.data.length)) {
			Status = -EFAULT;
		} else {
			if (pKey->Length != wrq->u.data.length) {
				Status = -EINVAL;
			} else {
				if ((pAdapter->PortCfg.AuthMode !=
				     Ndis802_11AuthModeWPAPSK)
				    && (pAdapter->PortCfg.AuthMode !=
					Ndis802_11AuthModeWPA2PSK)
				    && (pAdapter->PortCfg.AuthMode !=
					Ndis802_11AuthModeWPANone)) {
					Status = -EOPNOTSUPP;
				} else if ((pAdapter->PortCfg.AuthMode == Ndis802_11AuthModeWPAPSK) || (pAdapter->PortCfg.AuthMode == Ndis802_11AuthModeWPA2PSK))	// Only for WPA PSK mode
				{
					pAdapter->PortCfg.PskKey.KeyLen =
					    (UCHAR) pKey->KeyLength;
					memcpy(pAdapter->PortCfg.PskKey.Key,
					       &pKey->KeyMaterial,
					       pKey->KeyLength);
					// Use RaConfig as PSK agent.
					// Start STA supplicant state machine
					pAdapter->PortCfg.WpaState = SS_START;
					DBGPRINT(RT_DEBUG_TRACE,
						 "Set::RT_OID_802_11_ADD_WPA (id=0x%x, Len=%d-byte)\n",
						 pKey->KeyIndex,
						 pKey->KeyLength);
				} else {
					memcpy(pKey->BSSID,
					       pAdapter->PortCfg.Bssid, 6);
					RTMPWPAAddKeyProc(pAdapter, pKey);
					pAdapter->PortCfg.WpaState = SS_NOTUSE;
					DBGPRINT(RT_DEBUG_TRACE,
						 "Set::RT_OID_802_11_ADD_WPA (id=0x%x, Len=%d-byte)\n",
						 pKey->KeyIndex,
						 pKey->KeyLength);
				}
			}
		}
		kfree(pKey);
		break;
	case OID_802_11_REMOVE_KEY:
		pRemoveKey = kmalloc(wrq->u.data.length, MEM_ALLOC_FLAG);
		if (pRemoveKey == NULL) {
			Status = -ENOMEM;
			break;
		}

		if (copy_from_user
		    (pRemoveKey, wrq->u.data.pointer, wrq->u.data.length)) {
			Status = -EFAULT;
		} else {
			if (pRemoveKey->Length != wrq->u.data.length) {
				Status = -EINVAL;
			} else {
				if (pAdapter->PortCfg.AuthMode >=
				    Ndis802_11AuthModeWPA) {
					RTMPWPARemoveKeyProc(pAdapter,
							     pRemoveKey);
					DBGPRINT(RT_DEBUG_TRACE,
						 "Set::OID_802_11_REMOVE_KEY, Remove WPA Key!!\n");
				} else {
					KeyIdx = pRemoveKey->KeyIndex;

					if (KeyIdx & 0x80000000) {
						// Should never set default bit when remove key
						Status = -EINVAL;
						DBGPRINT(RT_DEBUG_TRACE,
							 "Set::OID_802_11_REMOVE_KEY, Failed!!(Should never set default bit when remove key)\n");
					} else {
						KeyIdx = KeyIdx & 0x0fffffff;
						if (KeyIdx > 3) {
							Status = -EINVAL;
							DBGPRINT(RT_DEBUG_TRACE,
								 "Set::OID_802_11_REMOVE_KEY, Failed!!(KeyId[%d] out of range)\n",
								 KeyIdx);
						} else {
							pAdapter->
							    SharedKey[KeyIdx].
							    KeyLen = 0;
							pAdapter->
							    SharedKey[KeyIdx].
							    CipherAlg =
							    CIPHER_NONE;
							AsicRemoveSharedKeyEntry
							    (pAdapter, 0,
							     (UCHAR) KeyIdx);
							DBGPRINT(RT_DEBUG_TRACE,
								 "Set::OID_802_11_REMOVE_KEY (id=0x%x, Len=%d-byte)\n",
								 pRemoveKey->
								 KeyIndex,
								 pRemoveKey->
								 Length);
						}
					}
				}
			}
		}
		kfree(pRemoveKey);
		break;
		// New for WPA
	case OID_802_11_ADD_KEY:
		pKey = kmalloc(wrq->u.data.length, MEM_ALLOC_FLAG);
		if (pKey == NULL) {
			Status = -ENOMEM;
			break;
		}
		if (copy_from_user
		    (pKey, wrq->u.data.pointer, wrq->u.data.length)) {
			Status = -EFAULT;
		} else {
			if (pKey->Length != wrq->u.data.length) {
				Status = -EINVAL;
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_ADD_KEY, Failed!!\n");
			} else {
				if (pAdapter->PortCfg.AuthMode >=
				    Ndis802_11AuthModeWPA) {
					// Probably PortCfg.Bssid reset to zero as linkdown,
					// Set pKey.BSSID to Broadcast bssid in order to ensure AsicAddSharedKeyEntry done
					if (pAdapter->PortCfg.AuthMode ==
					    Ndis802_11AuthModeWPANone) {
						memcpy(pKey->BSSID,
						       BROADCAST_ADDR, 6);
					}

					RTMPWPAAddKeyProc(pAdapter, pKey);
				} else	// Old WEP stuff
				{
					RTMPWPAWepKeySanity(pAdapter, pKey);
#if 0
					KeyIdx = pKey->KeyIndex & 0x0fffffff;

					// it is a shared key
					if (KeyIdx > 4)
						Status = -EINVAL;
					else {
						UCHAR CipherAlg;

						pAdapter->SharedKey[KeyIdx].
						    KeyLen =
						    (UCHAR) pKey->KeyLength;
						memcpy(pAdapter->
						       SharedKey[KeyIdx].Key,
						       &pKey->KeyMaterial,
						       pKey->KeyLength);

						if (pKey->KeyLength == 5)
							CipherAlg =
							    CIPHER_WEP64;
						else
							CipherAlg =
							    CIPHER_WEP128;

						pAdapter->SharedKey[KeyIdx].
						    CipherAlg = CipherAlg;

						if (pKey->KeyIndex & 0x80000000) {
							// Default key for tx (shared key)
							pAdapter->PortCfg.
							    DefaultKeyId =
							    (UCHAR) KeyIdx;
						}
						AsicAddSharedKeyEntry(pAdapter,
								      0,
								      (UCHAR)
								      KeyIdx,
								      CipherAlg,
								      pAdapter->
								      SharedKey
								      [KeyIdx].
								      Key, NULL,
								      NULL);
					}
#endif
				}
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_ADD_KEY (id=0x%x, Len=%d-byte)\n",
					 pKey->KeyIndex, pKey->KeyLength);
			}
		}
		kfree(pKey);
		break;
#if WPA_SUPPLICANT_SUPPORT
	case OID_802_11_SET_IEEE8021X:
		if (wrq->u.data.length != sizeof(BOOLEAN))
			Status = -EINVAL;
		else {
			Status =
			    copy_from_user(&IEEE8021xState, wrq->u.data.pointer,
					   wrq->u.data.length);
			pAdapter->PortCfg.IEEE8021X = IEEE8021xState;

			// set WEP key to ASIC for static wep mode
			if (pAdapter->PortCfg.IEEE8021X == FALSE
			    && pAdapter->PortCfg.AuthMode <
			    Ndis802_11AuthModeWPA) {
				int idx;

				idx = pAdapter->PortCfg.DefaultKeyId;
				//for (idx=0; idx < 4; idx++)
				{
					DBGPRINT_RAW(RT_DEBUG_TRACE,
						     "Set WEP key to Asic for static wep mode =>\n");

					if (pAdapter->PortCfg.
					    DesireSharedKey[idx].KeyLen != 0) {
						pAdapter->SharedKey[idx].
						    KeyLen =
						    pAdapter->PortCfg.
						    DesireSharedKey[idx].KeyLen;
						memcpy(pAdapter->SharedKey[idx].
						       Key,
						       pAdapter->PortCfg.
						       DesireSharedKey[idx].Key,
						       pAdapter->SharedKey[idx].
						       KeyLen);

						pAdapter->SharedKey[idx].
						    CipherAlg =
						    pAdapter->PortCfg.
						    DesireSharedKey[idx].
						    CipherAlg;

						AsicAddSharedKeyEntry(pAdapter,
								      0,
								      (UCHAR)
								      idx,
								      pAdapter->
								      SharedKey
								      [idx].
								      CipherAlg,
								      pAdapter->
								      SharedKey
								      [idx].Key,
								      NULL,
								      NULL);
					}
				}
			}

			DBGPRINT(RT_DEBUG_TRACE,
				 "Set::OID_802_11_SET_IEEE8021X (=%d)\n",
				 pAdapter->PortCfg.IEEE8021X);
		}
		break;
	case OID_802_11_SET_IEEE8021X_REQUIRE_KEY:
		if (wrq->u.data.length != sizeof(BOOLEAN))
			Status = -EINVAL;
		else {
			Status =
			    copy_from_user(&IEEE8021x_required_keys,
					   wrq->u.data.pointer,
					   wrq->u.data.length);
			pAdapter->PortCfg.IEEE8021x_required_keys =
			    IEEE8021x_required_keys;
			DBGPRINT(RT_DEBUG_TRACE,
				 "Set::OID_802_11_SET_IEEE8021X_REQUIRE_KEY (%d)\n",
				 pAdapter->PortCfg.IEEE8021x_required_keys);
		}
		break;
		// For WPA_SUPPLICANT to set dynamic wep key
	case OID_802_11_ADD_WEP:
		pWepKey = kmalloc(wrq->u.data.length, MEM_ALLOC_FLAG);

		if (pWepKey == NULL) {
			Status = -ENOMEM;
			DBGPRINT(RT_DEBUG_TRACE,
				 "Set::OID_802_11_ADD_WEP, Failed!!\n");
			break;
		}
		Status =
		    copy_from_user(pWepKey, wrq->u.data.pointer,
				   wrq->u.data.length);
		if (pWepKey->Length != wrq->u.data.length) {
			Status = -EINVAL;
			DBGPRINT(RT_DEBUG_TRACE,
				 "Set::OID_802_11_ADD_WEP, Failed (length mismatch)!!\n");
		} else {
			KeyIdx = pWepKey->KeyIndex & 0x0fffffff;

			// KeyIdx must be smaller than 4
			if (KeyIdx > 4) {
				Status = -EINVAL;
				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_ADD_WEP, Failed (KeyIdx must be smaller than 4)!!\n");
			} else {
				// After receiving eap-success in 802.1x mode, PortSecured will be TRUE.
				// At this moment, wpa_supplicant will set dynamic wep key to driver.
				// Otherwise, driver only records it, not set to Asic.
				if (pAdapter->PortCfg.PortSecured ==
				    WPA_802_1X_PORT_SECURED) {
					UCHAR CipherAlg;

					pAdapter->SharedKey[KeyIdx].KeyLen =
					    (UCHAR) pWepKey->KeyLength;
					memcpy(pAdapter->SharedKey[KeyIdx].Key,
					       &pWepKey->KeyMaterial,
					       pWepKey->KeyLength);

					if (pWepKey->KeyLength == 5)
						CipherAlg = CIPHER_WEP64;
					else
						CipherAlg = CIPHER_WEP128;

					pAdapter->SharedKey[KeyIdx].CipherAlg =
					    CipherAlg;

					if (pWepKey->KeyIndex & 0x80000000) {
						// Default key for tx (shared key)
						pAdapter->PortCfg.DefaultKeyId =
						    (UCHAR) KeyIdx;
					}

					AsicAddSharedKeyEntry(pAdapter, 0,
							      (UCHAR) KeyIdx,
							      CipherAlg,
							      pAdapter->
							      SharedKey[KeyIdx].
							      Key, NULL, NULL);

					DBGPRINT(RT_DEBUG_TRACE,
						 "Set::OID_802_11_ADD_WEP (id=0x%x, Len=%d-byte), Port Secured\n",
						 pWepKey->KeyIndex,
						 pWepKey->KeyLength);

				} else	// PortSecured is NOT SECURED
				{
					UCHAR CipherAlg;

					pAdapter->PortCfg.
					    DesireSharedKey[KeyIdx].KeyLen =
					    (UCHAR) pWepKey->KeyLength;
					memcpy(pAdapter->PortCfg.
					       DesireSharedKey[KeyIdx].Key,
					       &pWepKey->KeyMaterial,
					       pWepKey->KeyLength);

					if (pWepKey->KeyLength == 5)
						CipherAlg = CIPHER_WEP64;
					else
						CipherAlg = CIPHER_WEP128;

					pAdapter->PortCfg.
					    DesireSharedKey[KeyIdx].CipherAlg =
					    CipherAlg;

					if (pWepKey->KeyIndex & 0x80000000) {
						// Default key for tx (shared key)
						pAdapter->PortCfg.DefaultKeyId =
						    (UCHAR) KeyIdx;
					}

					DBGPRINT(RT_DEBUG_TRACE,
						 "Set::OID_802_11_ADD_WEP (id=0x%x, Len=%d-byte), Port Not Secured\n",
						 pWepKey->KeyIndex,
						 pWepKey->KeyLength);
				}
			}
		}
		kfree(pWepKey);
		break;
#endif
	case OID_802_11_CONFIGURATION:
		if (wrq->u.data.length != sizeof(NDIS_802_11_CONFIGURATION))
			Status = -EINVAL;
		else {
			if (copy_from_user
			    (&Config, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				pConfig = &Config;

				if ((pConfig->BeaconPeriod >= 20)
				    && (pConfig->BeaconPeriod <= 400))
					pAdapter->PortCfg.BeaconPeriod =
					    (USHORT) pConfig->BeaconPeriod;

				pAdapter->PortCfg.AtimWin =
				    (USHORT) pConfig->ATIMWindow;
				MAP_KHZ_TO_CHANNEL_ID(pConfig->DSConfig,
						      pAdapter->PortCfg.
						      Channel);
				//
				// Save the channel on MlmeAux for CntlOidRTBssidProc used.
				//
				pAdapter->MlmeAux.Channel =
				    pAdapter->PortCfg.Channel;

				DBGPRINT(RT_DEBUG_TRACE,
					 "Set::OID_802_11_CONFIGURATION (BeacnPeriod=%d,AtimW=%d,Ch=%d)\n",
					 pConfig->BeaconPeriod,
					 pConfig->ATIMWindow,
					 pAdapter->PortCfg.Channel);
				// Config has changed
				pAdapter->bConfigChanged = TRUE;
			}
		}
		break;
	case OID_802_11_DISASSOCIATE:
		// Set to immediately send the media disconnect event
		pAdapter->MlmeAux.CurrReqIsFromNdis = TRUE;

		DBGPRINT(RT_DEBUG_TRACE, "Set::OID_802_11_DISASSOCIATE \n");

		if (INFRA_ON(pAdapter)) {
			if (pAdapter->Mlme.CntlMachine.CurrState != CNTL_IDLE) {
				MlmeRestartStateMachine(pAdapter);
				DBGPRINT(RT_DEBUG_TRACE,
					 "!!! MLME busy, reset MLME state machine !!!\n");
			}

			MlmeEnqueue(pAdapter,
				    MLME_CNTL_STATE_MACHINE,
				    OID_802_11_DISASSOCIATE, 0, NULL);

			StateMachineTouched = TRUE;
		}
		break;
#if WPA_SUPPLICANT_SUPPORT
	case OID_SET_COUNTERMEASURES:
		if (wrq->u.param.value) {
			pAdapter->PortCfg.bBlockAssoc = TRUE;
			DBGPRINT(RT_DEBUG_TRACE,
				 "Set::OID_SET_COUNTERMEASURES bBlockAssoc=TRUE\n");
		} else {
			// WPA MIC error should block association attempt for 60 seconds
			pAdapter->PortCfg.bBlockAssoc = FALSE;
			DBGPRINT(RT_DEBUG_TRACE,
				 "Set::OID_SET_COUNTERMEASURES bBlockAssoc=FALSE \n");
		}
		break;
	case OID_802_11_PMKID:
		pPmkId = kmalloc(wrq->u.data.length, MEM_ALLOC_FLAG);

		if (pPmkId == NULL) {
			Status = -ENOMEM;
			break;
		}
		Status =
		    copy_from_user(pPmkId, wrq->u.data.pointer,
				   wrq->u.data.length);

		DBGPRINT(RT_DEBUG_OFF, "Set::OID_802_11_PMKID \n");

		// check the PMKID information
		if (pPmkId->BSSIDInfoCount == 0)
			break;
		else {
			PBSSID_INFO pBssIdInfo;
			ULONG BssIdx;
			ULONG CachedIdx;

			for (BssIdx = 0; BssIdx < pPmkId->BSSIDInfoCount;
			     BssIdx++) {
				// point to the indexed BSSID_INFO structure
				pBssIdInfo =
				    (PBSSID_INFO) ((PUCHAR) pPmkId +
						   2 * sizeof(ULONG) +
						   BssIdx * sizeof(BSSID_INFO));
				// Find the entry in the saved data base.
				for (CachedIdx = 0;
				     CachedIdx < pAdapter->PortCfg.SavedPMKNum;
				     CachedIdx++) {
					// compare the BSSID
					if (memcmp
					    (pBssIdInfo->BSSID,
					     pAdapter->PortCfg.
					     SavedPMK[CachedIdx].BSSID,
					     sizeof(NDIS_802_11_MAC_ADDRESS)) ==
					    0)
						break;
				}

				// Found, replace it
				if (CachedIdx < PMKID_NO) {
					DBGPRINT(RT_DEBUG_OFF,
						 "Update OID_802_11_PMKID, idx = %d\n",
						 CachedIdx);
					memcpy(&pAdapter->PortCfg.
					       SavedPMK[CachedIdx], pBssIdInfo,
					       sizeof(BSSID_INFO));
					pAdapter->PortCfg.SavedPMKNum++;
				}
				// Not found, replace the last one
				else {
					// Randomly replace one
					CachedIdx =
					    (pBssIdInfo->BSSID[5] % PMKID_NO);
					DBGPRINT(RT_DEBUG_OFF,
						 "Update OID_802_11_PMKID, idx = %d\n",
						 CachedIdx);
					memcpy(&pAdapter->PortCfg.
					       SavedPMK[CachedIdx], pBssIdInfo,
					       sizeof(BSSID_INFO));
				}
			}
		}
		break;
	case RT_OID_WPA_SUPPLICANT_SUPPORT:
		if (wrq->u.data.length != sizeof(BOOLEAN))
			Status = -EINVAL;
		else {
			Status =
			    copy_from_user(&wpa_supplicant_enable,
					   wrq->u.data.pointer,
					   wrq->u.data.length);
			pAdapter->PortCfg.WPA_Supplicant =
			    wpa_supplicant_enable;
			DBGPRINT(RT_DEBUG_TRACE,
				 "Set::RT_OID_WPA_SUPPLICANT_SUPPORT (=%d)\n",
				 pAdapter->PortCfg.WPA_Supplicant);
		}
		break;
#endif

	default:
		DBGPRINT(RT_DEBUG_TRACE,
			 "Set::unknown IOCTL's subcmd = 0x%08x\n", cmd);
		Status = -EOPNOTSUPP;
		break;
	}
	return Status;
}

INT RTMPQueryInformation(IN PRTMP_ADAPTER pAdapter,
			 IN OUT struct ifreq * rq, IN INT cmd)
{
	struct iwreq *wrq = (struct iwreq *)rq;
	NDIS_802_11_BSSID_LIST_EX *pBssidList = NULL;
	PNDIS_WLAN_BSSID_EX pBss;
	NDIS_802_11_SSID Ssid;
	NDIS_802_11_CONFIGURATION Configuration;
	RT_802_11_LINK_STATUS LinkStatus;
	RT_802_11_STA_CONFIG StaConfig;
	NDIS_802_11_STATISTICS Statistics;
	NDIS_802_11_RTS_THRESHOLD RtsThresh;
	NDIS_802_11_FRAGMENTATION_THRESHOLD FragThresh;
	NDIS_802_11_POWER_MODE PowerMode;
	NDIS_802_11_NETWORK_INFRASTRUCTURE BssType;
	RT_802_11_PREAMBLE PreamType;
	NDIS_802_11_AUTHENTICATION_MODE AuthMode;
	NDIS_802_11_WEP_STATUS WepStatus;
	RT_VERSION_INFO DriverVersionInfo;
	NDIS_MEDIA_STATE MediaState;
	ULONG BssBufSize;
	ULONG BssLen;
	ULONG ulInfo = 0;
	PUCHAR pBuf = NULL;
	PUCHAR pPtr;
	INT Status = NDIS_STATUS_SUCCESS;
	UCHAR Padding;
	UINT i;
	BOOLEAN RadioState;
	ULONG NetworkTypeList[4];
	UINT we_version_compiled;

	switch (cmd) {
	case RT_OID_DEVICE_NAME:
		DBGPRINT(RT_DEBUG_INFO, "Query::RT_OID_DEVICE_NAME\n");
		if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
			// TODO: Any client apps that use this need to be reworked so they are okay when the module is loaded but the interface is down. Until then ....
			// Interface is down, so pretend we don't exist.
			Status = -EFAULT;
		} else {
			wrq->u.data.length = sizeof(NIC_DEVICE_NAME);
			if (copy_to_user
			    (wrq->u.data.pointer, NIC_DEVICE_NAME,
			     wrq->u.data.length))
				Status = -EFAULT;
		}
		break;
	case RT_OID_VERSION_INFO:
		DBGPRINT(RT_DEBUG_INFO, "Query::RT_OID_VERSION_INFO \n");
		DriverVersionInfo.DriverVersionW = DRV_MAJORVERSION;
		DriverVersionInfo.DriverVersionX = DRV_MINORVERSION;
		DriverVersionInfo.DriverVersionY = DRV_SUBVERSION;
		DriverVersionInfo.DriverVersionZ = DRV_TESTVERSION;
		DriverVersionInfo.DriverBuildYear = DRV_YEAR;
		DriverVersionInfo.DriverBuildMonth = DRV_MONTH;
		DriverVersionInfo.DriverBuildDay = DRV_DAY;
		wrq->u.data.length = sizeof(RT_VERSION_INFO);
		if (copy_to_user
		    (wrq->u.data.pointer, &DriverVersionInfo,
		     wrq->u.data.length))
			Status = -EFAULT;
		break;
	case OID_802_11_BSSID_LIST:
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::OID_802_11_BSSID_LIST (%d BSS returned)\n",
			 pAdapter->ScanTab.BssNr);
		// Claculate total buffer size required
		BssBufSize = sizeof(ULONG);

		for (i = 0; i < pAdapter->ScanTab.BssNr; i++) {
			// Align pointer to 4 bytes boundary.
			Padding =
			    4 -
			    (pAdapter->ScanTab.BssEntry[i].VarIELen & 0x0003);
			if (Padding == 4)
				Padding = 0;
			BssBufSize +=
			    (sizeof(NDIS_WLAN_BSSID_EX) - 1 +
			     sizeof(NDIS_802_11_FIXED_IEs) +
			     pAdapter->ScanTab.BssEntry[i].VarIELen + Padding);
		}

		// For safety issue, we add 256 bytes just in case
		BssBufSize += 256;
		// Allocate the same size as passed from higher layer
		pBuf = kmalloc(BssBufSize, MEM_ALLOC_FLAG);
		if (pBuf == NULL) {
			Status = -ENOMEM;
			break;
		}
		// Init 802_11_BSSID_LIST_EX structure
		memset(pBuf, 0, BssBufSize);
		pBssidList = (PNDIS_802_11_BSSID_LIST_EX) pBuf;
		pBssidList->NumberOfItems = pAdapter->ScanTab.BssNr;

		// Calculate total buffer length
		BssLen = 4;	// Consist of NumberOfItems
		// Point to start of NDIS_WLAN_BSSID_EX
		// pPtr = pBuf + sizeof(ULONG);
		pPtr = (PUCHAR) & pBssidList->Bssid[0];
		for (i = 0; i < pAdapter->ScanTab.BssNr; i++) {
			pBss = (PNDIS_WLAN_BSSID_EX) pPtr;
			memcpy(&pBss->MacAddress,
			       &pAdapter->ScanTab.BssEntry[i].Bssid, ETH_ALEN);
			if ((pAdapter->ScanTab.BssEntry[i].Hidden == 1)
			    && (pAdapter->PortCfg.bShowHiddenSSID == FALSE)) {
				pBss->Ssid.SsidLength = 0;
			} else {
				pBss->Ssid.SsidLength =
				    pAdapter->ScanTab.BssEntry[i].SsidLen;
				memcpy(pBss->Ssid.Ssid,
				       pAdapter->ScanTab.BssEntry[i].Ssid,
				       pAdapter->ScanTab.BssEntry[i].SsidLen);
			}
			pBss->Privacy = pAdapter->ScanTab.BssEntry[i].Privacy;
			pBss->Rssi =
			    pAdapter->ScanTab.BssEntry[i].Rssi -
			    pAdapter->BbpRssiToDbmDelta;
			pBss->NetworkTypeInUse =
			    NetworkTypeInUseSanity(&pAdapter->ScanTab.
						   BssEntry[i]);
			pBss->Configuration.Length =
			    sizeof(NDIS_802_11_CONFIGURATION);
			pBss->Configuration.BeaconPeriod =
			    pAdapter->ScanTab.BssEntry[i].BeaconPeriod;
			pBss->Configuration.ATIMWindow =
			    pAdapter->ScanTab.BssEntry[i].AtimWin;

			MAP_CHANNEL_ID_TO_KHZ(pAdapter->ScanTab.BssEntry[i].
					      Channel,
					      pBss->Configuration.DSConfig);

			if (pAdapter->ScanTab.BssEntry[i].BssType == BSS_INFRA)
				pBss->InfrastructureMode =
				    Ndis802_11Infrastructure;
			else
				pBss->InfrastructureMode = Ndis802_11IBSS;

			memcpy(pBss->SupportedRates,
			       pAdapter->ScanTab.BssEntry[i].SupRate,
			       pAdapter->ScanTab.BssEntry[i].SupRateLen);
// TODO: 2004-09-13 john -  should we copy ExtRate into this array? if not, some APs annouced all 8 11g rates
// in ExtRateIE which may be mis-treated as 802.11b AP by ZeroConfig
			memcpy(pBss->SupportedRates +
			       pAdapter->ScanTab.BssEntry[i].SupRateLen,
			       pAdapter->ScanTab.BssEntry[i].ExtRate,
			       pAdapter->ScanTab.BssEntry[i].ExtRateLen);

			DBGPRINT(RT_DEBUG_TRACE,
				 "BSS#%d - %s, Ch %d = %d Khz, Sup+Ext rate# = %d\n",
				 i, pBss->Ssid.Ssid,
				 pAdapter->ScanTab.BssEntry[i].Channel,
				 pBss->Configuration.DSConfig,
				 pAdapter->ScanTab.BssEntry[i].SupRateLen +
				 pAdapter->ScanTab.BssEntry[i].ExtRateLen);

			if (pAdapter->ScanTab.BssEntry[i].VarIELen == 0) {
				pBss->IELength = sizeof(NDIS_802_11_FIXED_IEs);
				memcpy(pBss->IEs,
				       &pAdapter->ScanTab.BssEntry[i].FixIEs,
				       sizeof(NDIS_802_11_FIXED_IEs));
				pPtr =
				    pPtr + sizeof(NDIS_WLAN_BSSID_EX) - 1 +
				    sizeof(NDIS_802_11_FIXED_IEs);
			} else {
				pBss->IELength =
				    sizeof(NDIS_802_11_FIXED_IEs) +
				    pAdapter->ScanTab.BssEntry[i].VarIELen;
				pPtr =
				    pPtr + sizeof(NDIS_WLAN_BSSID_EX) - 1 +
				    sizeof(NDIS_802_11_FIXED_IEs);
				memcpy(pBss->IEs,
				       &pAdapter->ScanTab.BssEntry[i].FixIEs,
				       sizeof(NDIS_802_11_FIXED_IEs));
				memcpy(pPtr,
				       pAdapter->ScanTab.BssEntry[i].VarIEs,
				       pAdapter->ScanTab.BssEntry[i].VarIELen);
				pPtr += pAdapter->ScanTab.BssEntry[i].VarIELen;
			}
			// Align pointer to 4 bytes boundary.
			Padding =
			    4 -
			    (pAdapter->ScanTab.BssEntry[i].VarIELen & 0x0003);
			if (Padding == 4)
				Padding = 0;
			pPtr += Padding;
			pBss->Length =
			    sizeof(NDIS_WLAN_BSSID_EX) - 1 +
			    sizeof(NDIS_802_11_FIXED_IEs) +
			    pAdapter->ScanTab.BssEntry[i].VarIELen + Padding;
			BssLen += pBss->Length;
		}
		wrq->u.data.length = BssLen;
		if (copy_to_user
		    (wrq->u.data.pointer, pBssidList, wrq->u.data.length))
			Status = -EFAULT;
		kfree(pBssidList);
		break;
	case OID_802_3_CURRENT_ADDRESS:
		wrq->u.data.length = ETH_ALEN;
		if (copy_to_user
		    (wrq->u.data.pointer, &pAdapter->CurrentAddress,
		     wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_INFO, "Query::OID_802_3_CURRENT_ADDRESS \n");
		break;
	case OID_GEN_MEDIA_CONNECT_STATUS:
		DBGPRINT(RT_DEBUG_INFO,
			 "Query::OID_GEN_MEDIA_CONNECT_STATUS \n");

		if (OPSTATUS_TEST_FLAG
		    (pAdapter, fOP_STATUS_MEDIA_STATE_CONNECTED))
			MediaState = NdisMediaStateConnected;
		else
			MediaState = NdisMediaStateDisconnected;

		wrq->u.data.length = sizeof(NDIS_MEDIA_STATE);
		if (copy_to_user
		    (wrq->u.data.pointer, &MediaState, wrq->u.data.length))
			Status = -EFAULT;
		break;
	case OID_802_11_BSSID:
		if (INFRA_ON(pAdapter) || ADHOC_ON(pAdapter)) {
			if (copy_to_user
			    (wrq->u.data.pointer, &pAdapter->PortCfg.Bssid,
			     sizeof(NDIS_802_11_MAC_ADDRESS)))
				Status = -EFAULT;

			DBGPRINT(RT_DEBUG_INFO,
				 "IOCTL::SIOCGIWAP(=%02x:%02x:%02x:%02x:%02x:%02x)\n",
				 pAdapter->PortCfg.Bssid[0],
				 pAdapter->PortCfg.Bssid[1],
				 pAdapter->PortCfg.Bssid[2],
				 pAdapter->PortCfg.Bssid[3],
				 pAdapter->PortCfg.Bssid[4],
				 pAdapter->PortCfg.Bssid[5]);

		} else {
			DBGPRINT(RT_DEBUG_TRACE,
				 "Query::OID_802_11_BSSID(=EMPTY)\n");
			Status = -ENOTCONN;
		}
		break;
	case OID_802_11_SSID:
		Ssid.SsidLength = pAdapter->PortCfg.SsidLen;
		memset(Ssid.Ssid, 0, MAX_LEN_OF_SSID);
		memcpy(Ssid.Ssid, pAdapter->PortCfg.Ssid, Ssid.SsidLength);
		wrq->u.data.length = sizeof(NDIS_802_11_SSID);
		if (copy_to_user
		    (wrq->u.data.pointer, &Ssid, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::OID_802_11_SSID (Len=%d, ssid=%s)\n",
			 Ssid.SsidLength, Ssid.Ssid);
		break;
	case RT_OID_802_11_QUERY_LINK_STATUS:
		LinkStatus.CurrTxRate = RateIdTo500Kbps[pAdapter->PortCfg.TxRate];	// unit : 500 kbps
		LinkStatus.ChannelQuality = pAdapter->Mlme.ChannelQuality;
		LinkStatus.RxByteCount =
		    pAdapter->RalinkCounters.ReceivedByteCount;
		LinkStatus.TxByteCount =
		    pAdapter->RalinkCounters.TransmittedByteCount;
		wrq->u.data.length = sizeof(RT_802_11_LINK_STATUS);
		if (copy_to_user
		    (wrq->u.data.pointer, &LinkStatus, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::RT_OID_802_11_QUERY_LINK_STATUS\n");
		break;
	case OID_802_11_CONFIGURATION:
		Configuration.Length = sizeof(NDIS_802_11_CONFIGURATION);
		Configuration.BeaconPeriod = pAdapter->PortCfg.BeaconPeriod;
		Configuration.ATIMWindow = pAdapter->PortCfg.AtimWin;
		MAP_CHANNEL_ID_TO_KHZ(pAdapter->PortCfg.Channel,
				      Configuration.DSConfig);
		wrq->u.data.length = sizeof(NDIS_802_11_CONFIGURATION);
		if (copy_to_user
		    (wrq->u.data.pointer, &Configuration, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::OID_802_11_CONFIGURATION(BeaconPeriod=%d,AtimW=%d,Channel=%d) \n",
			 Configuration.BeaconPeriod, Configuration.ATIMWindow,
			 pAdapter->PortCfg.Channel);
		break;
	case OID_802_11_RSSI_TRIGGER:
		ulInfo =
		    pAdapter->PortCfg.LastRssi - pAdapter->BbpRssiToDbmDelta;
		wrq->u.data.length = sizeof(ulInfo);
		if (copy_to_user
		    (wrq->u.data.pointer, &ulInfo, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::OID_802_11_RSSI_TRIGGER(=%d)\n", ulInfo);
		break;
	case RT_OID_802_11_RSSI_2:
		if ((pAdapter->RfIcType == RFIC_5325)
		    || (pAdapter->RfIcType == RFIC_2529)) {
			ulInfo =
			    pAdapter->PortCfg.LastRssi2 -
			    pAdapter->BbpRssiToDbmDelta;
			wrq->u.data.length = sizeof(ulInfo);
			if (copy_to_user
			    (wrq->u.data.pointer, &ulInfo, wrq->u.data.length))
				Status = -EFAULT;
			DBGPRINT(RT_DEBUG_TRACE,
				 "Query::RT_OID_802_11_RSSI_2(=%d)\n", ulInfo);
		} else
			Status = -EOPNOTSUPP;
		break;
	case OID_802_11_STATISTICS:
		DBGPRINT(RT_DEBUG_TRACE, "Query::OID_802_11_STATISTICS \n");

		// add the most up-to-date h/w raw counters into software counters
		NICUpdateRawCounters(pAdapter);

		// Sanity check for calculation of sucessful count
		if (pAdapter->WlanCounters.TransmittedFragmentCount.QuadPart <
		    pAdapter->WlanCounters.RetryCount.QuadPart)
			pAdapter->WlanCounters.TransmittedFragmentCount.
			    QuadPart =
			    pAdapter->WlanCounters.RetryCount.QuadPart;

		Statistics.TransmittedFragmentCount.QuadPart =
		    pAdapter->WlanCounters.TransmittedFragmentCount.QuadPart;
		Statistics.MulticastTransmittedFrameCount.QuadPart =
		    pAdapter->WlanCounters.MulticastTransmittedFrameCount.
		    QuadPart;
		Statistics.FailedCount.QuadPart =
		    pAdapter->WlanCounters.FailedCount.QuadPart;
		Statistics.RetryCount.QuadPart =
		    pAdapter->WlanCounters.RetryCount.QuadPart;
		Statistics.MultipleRetryCount.QuadPart =
		    pAdapter->WlanCounters.MultipleRetryCount.QuadPart;
		Statistics.RTSSuccessCount.QuadPart =
		    pAdapter->WlanCounters.RTSSuccessCount.QuadPart;
		Statistics.RTSFailureCount.QuadPart =
		    pAdapter->WlanCounters.RTSFailureCount.QuadPart;
		Statistics.ACKFailureCount.QuadPart =
		    pAdapter->WlanCounters.ACKFailureCount.QuadPart;
		Statistics.FrameDuplicateCount.QuadPart =
		    pAdapter->WlanCounters.FrameDuplicateCount.QuadPart;
		Statistics.ReceivedFragmentCount.QuadPart =
		    pAdapter->WlanCounters.ReceivedFragmentCount.QuadPart;
		Statistics.MulticastReceivedFrameCount.QuadPart =
		    pAdapter->WlanCounters.MulticastReceivedFrameCount.QuadPart;
#ifdef RT61_DBG
		Statistics.FCSErrorCount =
		    pAdapter->RalinkCounters.RealFcsErrCount;
#else
		Statistics.FCSErrorCount.QuadPart =
		    pAdapter->WlanCounters.FCSErrorCount.QuadPart;
		Statistics.FrameDuplicateCount.vv.LowPart =
		    pAdapter->WlanCounters.FrameDuplicateCount.vv.LowPart / 100;
#endif
		wrq->u.data.length = sizeof(NDIS_802_11_STATISTICS);
		if (copy_to_user
		    (wrq->u.data.pointer, &Statistics, wrq->u.data.length))
			Status = -EFAULT;
		break;
	case OID_GEN_RCV_OK:
		DBGPRINT(RT_DEBUG_INFO, "Query::OID_GEN_RCV_OK \n");
		ulInfo = pAdapter->Counters8023.GoodReceives;
		wrq->u.data.length = sizeof(ulInfo);
		if (copy_to_user
		    (wrq->u.data.pointer, &ulInfo, wrq->u.data.length))
			Status = -EFAULT;
		break;
	case OID_GEN_RCV_NO_BUFFER:
		DBGPRINT(RT_DEBUG_INFO, "Query::OID_GEN_RCV_NO_BUFFER \n");
		ulInfo = pAdapter->Counters8023.RxNoBuffer;
		wrq->u.data.length = sizeof(ulInfo);
		if (copy_to_user
		    (wrq->u.data.pointer, &ulInfo, wrq->u.data.length))
			Status = -EFAULT;
		break;
	case RT_OID_802_11_PHY_MODE:
		ulInfo = (ULONG) pAdapter->PortCfg.PhyMode;
		wrq->u.data.length = sizeof(ulInfo);
		if (copy_to_user
		    (wrq->u.data.pointer, &ulInfo, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::RT_OID_802_11_PHY_MODE (=%d)\n", ulInfo);
		break;
	case RT_OID_802_11_STA_CONFIG:
		DBGPRINT(RT_DEBUG_TRACE, "Query::RT_OID_802_11_STA_CONFIG\n");
		StaConfig.EnableTxBurst = pAdapter->PortCfg.bEnableTxBurst;
		StaConfig.EnableTurboRate = pAdapter->PortCfg.EnableTurboRate;
		StaConfig.UseBGProtection = pAdapter->PortCfg.UseBGProtection;
		StaConfig.UseShortSlotTime = pAdapter->PortCfg.UseShortSlotTime;
		StaConfig.AdhocMode = pAdapter->PortCfg.AdhocMode;
		StaConfig.HwRadioStatus =
		    (pAdapter->PortCfg.bHwRadio == TRUE) ? 1 : 0;
		StaConfig.Rsv1 = 0;
		StaConfig.SystemErrorBitmap = pAdapter->SystemErrorBitmap;
		wrq->u.data.length = sizeof(RT_802_11_STA_CONFIG);
		if (copy_to_user
		    (wrq->u.data.pointer, &StaConfig, wrq->u.data.length))
			Status = -EFAULT;
		break;
	case OID_802_11_RTS_THRESHOLD:
		RtsThresh = pAdapter->PortCfg.RtsThreshold;
		wrq->u.data.length = sizeof(RtsThresh);
		if (copy_to_user
		    (wrq->u.data.pointer, &RtsThresh, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::OID_802_11_RTS_THRESHOLD(=%d)\n", RtsThresh);
		break;
	case OID_802_11_FRAGMENTATION_THRESHOLD:
		FragThresh = pAdapter->PortCfg.FragmentThreshold;
		if (pAdapter->PortCfg.bFragmentZeroDisable == TRUE)
			FragThresh = 0;
		wrq->u.data.length = sizeof(FragThresh);
		if (copy_to_user
		    (wrq->u.data.pointer, &FragThresh, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::OID_802_11_FRAGMENTATION_THRESHOLD(=%d)\n",
			 FragThresh);
		break;
	case OID_802_11_POWER_MODE:
		PowerMode = pAdapter->PortCfg.WindowsPowerMode;
		wrq->u.data.length = sizeof(PowerMode);
		if (copy_to_user
		    (wrq->u.data.pointer, &PowerMode, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE, "Query::OID_802_11_POWER_MODE(=%d)\n",
			 PowerMode);
		break;
	case RT_OID_802_11_RADIO:
		RadioState = (BOOLEAN) pAdapter->PortCfg.bSwRadio;
		wrq->u.data.length = sizeof(RadioState);
		if (copy_to_user
		    (wrq->u.data.pointer, &RadioState, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::RT_OID_802_11_QUERY_RADIO (=%d)\n",
			 RadioState);
		break;
	case OID_802_11_INFRASTRUCTURE_MODE:
		if (ADHOC_ON(pAdapter))
			BssType = Ndis802_11IBSS;
		else if (INFRA_ON(pAdapter))
			BssType = Ndis802_11Infrastructure;
		else
			BssType = Ndis802_11AutoUnknown;

		wrq->u.data.length = sizeof(BssType);
		if (copy_to_user
		    (wrq->u.data.pointer, &BssType, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::OID_802_11_INFRASTRUCTURE_MODE(=%d)\n",
			 BssType);
		break;
	case RT_OID_802_11_PREAMBLE:
		PreamType = pAdapter->PortCfg.TxPreamble;
		wrq->u.data.length = sizeof(PreamType);
		if (copy_to_user
		    (wrq->u.data.pointer, &PreamType, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE, "Query::RT_OID_802_11_PREAMBLE(=%d)\n",
			 PreamType);
		break;
	case OID_802_11_AUTHENTICATION_MODE:
		AuthMode = pAdapter->PortCfg.AuthMode;
		wrq->u.data.length = sizeof(AuthMode);
		if (copy_to_user
		    (wrq->u.data.pointer, &AuthMode, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::OID_802_11_AUTHENTICATION_MODE(=%d)\n",
			 AuthMode);
		break;
	case OID_802_11_WEP_STATUS:
		WepStatus = pAdapter->PortCfg.WepStatus;
		wrq->u.data.length = sizeof(WepStatus);
		if (copy_to_user
		    (wrq->u.data.pointer, &WepStatus, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE, "Query::OID_802_11_WEP_STATUS(=%d)\n",
			 WepStatus);
		break;
	case OID_802_11_TX_POWER_LEVEL:
		wrq->u.data.length = sizeof(ULONG);
		if (copy_to_user
		    (wrq->u.data.pointer, &pAdapter->PortCfg.TxPower,
		     wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::OID_802_11_TX_POWER_LEVEL %x\n",
			 pAdapter->PortCfg.TxPower);
		break;
	case RT_OID_802_11_TX_POWER_LEVEL_1:
		wrq->u.data.length = sizeof(ULONG);
		if (copy_to_user
		    (wrq->u.data.pointer, &pAdapter->PortCfg.TxPowerPercentage,
		     wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::RT_OID_802_11_TX_POWER_LEVEL_1 (=%d)\n",
			 pAdapter->PortCfg.TxPowerPercentage);
		break;
	case OID_802_11_NETWORK_TYPES_SUPPORTED:
		if ((pAdapter->RfIcType == RFIC_5225)
		    || (pAdapter->RfIcType == RFIC_5325)) {
			NetworkTypeList[0] = 3;	// NumberOfItems = 3
			NetworkTypeList[1] = Ndis802_11DS;	// NetworkType[1] = 11b
			NetworkTypeList[2] = Ndis802_11OFDM24;	// NetworkType[2] = 11g
			NetworkTypeList[3] = Ndis802_11OFDM5;	// NetworkType[3] = 11a
			wrq->u.data.length = 16;
		} else {
			NetworkTypeList[0] = 2;	// NumberOfItems = 2
			NetworkTypeList[1] = Ndis802_11DS;	// NetworkType[1] = 11b
			NetworkTypeList[2] = Ndis802_11OFDM24;	// NetworkType[2] = 11g
			wrq->u.data.length = 12;
		}
		if (copy_to_user
		    (wrq->u.data.pointer, &NetworkTypeList[0],
		     wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::OID_802_11_NETWORK_TYPES_SUPPORTED\n");
		break;
	case OID_802_11_NETWORK_TYPE_IN_USE:
		wrq->u.data.length = sizeof(ULONG);
		if (pAdapter->PortCfg.PhyMode == PHY_11A)
			ulInfo = Ndis802_11OFDM5;
		else if ((pAdapter->PortCfg.PhyMode == PHY_11BG_MIXED)
			 || (pAdapter->PortCfg.PhyMode == PHY_11G))
			ulInfo = Ndis802_11OFDM24;
		else
			ulInfo = Ndis802_11DS;
		if (copy_to_user
		    (wrq->u.data.pointer, &ulInfo, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_INFO,
			 "Query::OID_802_11_NETWORK_TYPE_IN_USE(=%d)\n",
			 ulInfo);
		break;
	case RT_OID_802_11_QUERY_LAST_RX_RATE:
		ulInfo = (ULONG) pAdapter->LastRxRate;
		wrq->u.data.length = sizeof(ulInfo);
		if (copy_to_user
		    (wrq->u.data.pointer, &ulInfo, wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::RT_OID_802_11_QUERY_LAST_RX_RATE (=%d)\n",
			 ulInfo);
		break;
	case RT_OID_802_11_QUERY_EEPROM_VERSION:
		wrq->u.data.length = sizeof(ULONG);
		if (copy_to_user
		    (wrq->u.data.pointer, &pAdapter->EepromVersion,
		     wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_INFO,
			 "Query::RT_OID_802_11_QUERY_EEPROM_VERSION (=%d)\n",
			 pAdapter->EepromVersion);
		break;
	case RT_OID_802_11_QUERY_FIRMWARE_VERSION:
		wrq->u.data.length = sizeof(ULONG);
		if (copy_to_user
		    (wrq->u.data.pointer, &pAdapter->FirmwareVersion,
		     wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_INFO,
			 "Query::RT_OID_802_11_QUERY_FIRMWARE_VERSION (=%d)\n",
			 pAdapter->FirmwareVersion);
		break;
	case RT_OID_802_11_QUERY_NOISE_LEVEL:
		// TODO: how to measure NOISE LEVEL now?????
		wrq->u.data.length = sizeof(UCHAR);
		if (copy_to_user
		    (wrq->u.data.pointer, &pAdapter->BbpWriteLatch[17],
		     wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::RT_OID_802_11_QUERY_NOISE_LEVEL (=%d)\n",
			 pAdapter->BbpWriteLatch[17]);
		break;
	case RT_OID_802_11_EXTRA_INFO:
		wrq->u.data.length = sizeof(ULONG);
		if (copy_to_user
		    (wrq->u.data.pointer, &pAdapter->ExtraInfo,
		     wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::RT_OID_802_11_EXTRA_INFO (=%d)\n",
			 pAdapter->ExtraInfo);
		break;
	case RT_OID_802_11_QUERY_PIDVID:
		wrq->u.data.length = sizeof(ULONG);
		if (copy_to_user
		    (wrq->u.data.pointer, &pAdapter->VendorDesc,
		     wrq->u.data.length))
			Status = -EFAULT;
		DBGPRINT(RT_DEBUG_INFO,
			 "Query::RT_OID_802_11_QUERY_PIDVID (=%d)\n",
			 pAdapter->VendorDesc);
		break;
	case RT_OID_WE_VERSION_COMPILED:
		wrq->u.data.length = sizeof(UINT);
		we_version_compiled = WIRELESS_EXT;
		Status =
		    copy_to_user(wrq->u.data.pointer, &we_version_compiled,
				 wrq->u.data.length);
		DBGPRINT(RT_DEBUG_INFO,
			 "Query::RT_OID_WE_VERSION_COMPILED (=%d)\n",
			 we_version_compiled);
		break;

	default:
		DBGPRINT(RT_DEBUG_TRACE,
			 "Query::unknown IOCTL's subcmd = 0x%08x\n", cmd);
		Status = -EOPNOTSUPP;
		break;
	}
	return Status;
}

INT RT61_ioctl(IN struct net_device * net_dev,
	       IN OUT struct ifreq * rq, IN INT cmd)
{
	RTMP_ADAPTER *pAd = net_dev->priv;
	struct iwreq *wrq = (struct iwreq *)rq;
	NDIS_802_11_MAC_ADDRESS Bssid;
	BOOLEAN StateMachineTouched = FALSE;
	INT Status = NDIS_STATUS_SUCCESS;
	USHORT subcmd;

	switch (cmd) {
	case SIOCGIFHWADDR:
		DBGPRINT(RT_DEBUG_TRACE, "IOCTL::SIOCGIFHWADDR\n");
		strcpy(wrq->u.name, pAd->PortCfg.Bssid);
		break;
	case SIOCSIWNWID:	// set network id (the cell)
	case SIOCGIWNWID:	// get network id
		Status = -EOPNOTSUPP;
		break;
	case SIOCGIWRATE:	//get default bit rate (bps)
		wrq->u.bitrate.value =
		    RateIdTo500Kbps[pAd->PortCfg.TxRate] * 500000;
		wrq->u.bitrate.disabled = 0;
		break;
	case SIOCSIWRATE:	//set default bit rate (bps)
		RTMPSetDesiredRates(pAd, wrq->u.bitrate.value);
		break;
	case SIOCSIWAP:	//set access point MAC addresses
		memcpy(&Bssid, &wrq->u.ap_addr.sa_data,
			sizeof(NDIS_802_11_MAC_ADDRESS));

		if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE))
			break;

		if (pAd->Mlme.CntlMachine.CurrState != CNTL_IDLE) {
			MlmeRestartStateMachine(pAd);
			DBGPRINT(RT_DEBUG_TRACE,
				 "!!! MLME busy, reset MLME state machine !!!\n");
		}
		// tell CNTL state machine to call NdisMSetInformationComplete() after completing
		// this request, because this request is initiated by NDIS.
		pAd->MlmeAux.CurrReqIsFromNdis = FALSE;

		MlmeEnqueue(pAd,
			    MLME_CNTL_STATE_MACHINE,
			    OID_802_11_BSSID,
			    sizeof(NDIS_802_11_MAC_ADDRESS), (VOID *) & Bssid);
		Status = NDIS_STATUS_SUCCESS;
		StateMachineTouched = TRUE;
		DBGPRINT(RT_DEBUG_TRACE,
			 "IOCTL::SIOCSIWAP %02x:%02x:%02x:%02x:%02x:%02x\n",
			 Bssid[0], Bssid[1], Bssid[2], Bssid[3], Bssid[4],
			 Bssid[5]);
		break;
	case SIOCGIWSENS:	//get sensitivity (dBm)
	case SIOCSIWSENS:	//set sensitivity (dBm)
	case SIOCGIWPOWER:	//get Power Management settings
	case SIOCSIWPOWER:	//set Power Management settings
	case SIOCGIWTXPOW:	//get transmit power (dBm)
	case SIOCSIWTXPOW:	//set transmit power (dBm)
	case SIOCGIWRETRY:	//get retry limits and lifetime
	case SIOCSIWRETRY:	//set retry limits and lifetime
	case SIOCETHTOOL:	//ethtool specific IOCTL (FIXME!, minimal support should not be difficult)
	case 0x00008947:	//mrtg related IOCTL (ignored for now)
		Status = -EOPNOTSUPP;
		break;
	case RT_PRIV_IOCTL:
		subcmd = wrq->u.data.flags;
		if (subcmd & OID_GET_SET_TOGGLE)
			Status = RTMPSetInformation(pAd, rq, subcmd);
		else
			Status = RTMPQueryInformation(pAd, rq, subcmd);
		break;
	case SIOCGIWPRIV:
		if (wrq->u.data.pointer) {
			if (!access_ok
			    (VERIFY_WRITE, wrq->u.data.pointer,
			     sizeof(privtab)))
				break;
			wrq->u.data.length =
			    sizeof(privtab) / sizeof(privtab[0]);
			if (copy_to_user
			    (wrq->u.data.pointer, privtab, sizeof(privtab)))
				Status = -EFAULT;
		}
		break;
	case RTPRIV_IOCTL_SET:
		{
			CHAR *this_char;
			CHAR *value;

			if (!access_ok
			    (VERIFY_READ, wrq->u.data.pointer,
			     wrq->u.data.length))
				break;

			this_char = kmalloc(wrq->u.data.length, MEM_ALLOC_FLAG);
			if (this_char == NULL) {
				Status = -ENOMEM;
				break;
			}

			if (copy_from_user
			    (this_char, wrq->u.data.pointer,
			     wrq->u.data.length)) {
				Status = -EFAULT;
			} else {
				if (!*this_char)
					break;

				if ((value = rtstrchr(this_char, '=')) != NULL)
					*value++ = 0;

				if (!value)
					break;

				// reject setting nothing besides ANY ssid(ssidLen=0)
				if (!*value && (strcmp(this_char, "SSID") != 0))
					break;

				for (PRTMP_PRIVATE_SET_PROC =
				     RTMP_PRIVATE_SUPPORT_PROC;
				     PRTMP_PRIVATE_SET_PROC->name;
				     PRTMP_PRIVATE_SET_PROC++) {
					if (!strcmp
					    (this_char,
					     PRTMP_PRIVATE_SET_PROC->name)) {
						if (!PRTMP_PRIVATE_SET_PROC->
						    set_proc(pAd, value)) {
							//FALSE:Set private failed then return Invalid argument
							Status = -EINVAL;
						}
						break;	//Exit for loop.
					}
				}

				if (PRTMP_PRIVATE_SET_PROC->name == NULL) {	//Not found argument
					Status = -EINVAL;
					DBGPRINT(RT_DEBUG_TRACE,
						 "IOCTL::(iwpriv) Command not Support [%s=%s]\n",
						 this_char, value);
					break;
				}
			}
			kfree(this_char);
		}
		break;
	case RTPRIV_IOCTL_GSITESURVEY:
		RTMPIoctlGetSiteSurvey(pAd, wrq);
		break;
	case RTPRIV_IOCTL_STATISTICS:
		RTMPIoctlStatistics(pAd, wrq);
		break;
#ifdef RT61_DBG
	case RTPRIV_IOCTL_BBP:
		RTMPIoctlBBP(pAd, wrq);
		break;
	case RTPRIV_IOCTL_MAC:
		RTMPIoctlMAC(pAd, wrq);
		break;
#ifdef RALINK_ATE
	case RTPRIV_IOCTL_E2P:
		RTMPIoctlE2PROM(pAd, wrq);
		break;
#endif				// RALINK_ATE
#endif
	case RTPRIV_IOCTL_SET_RFMONTX:
		RTMPIoctlSetRFMONTX(pAd, wrq);
		break;
	case RTPRIV_IOCTL_GET_RFMONTX:
		RTMPIoctlGetRFMONTX(pAd, wrq);
		break;
	case RTPRIV_IOCTL_GETRAAPCFG:
		RTMPIoctlGetRaAPCfg(pAd, wrq);
		break;
	default:
		DBGPRINT(RT_DEBUG_ERROR,
			 "IOCTL::unknown IOCTL's cmd = 0x%08x\n", cmd);
		Status = -EOPNOTSUPP;
		break;
	}

	if (StateMachineTouched)	// Upper layer sent a MLME-related operations
		MlmeHandler(pAd);

	return Status;
}

/*
	========================================================================

	Routine Description:
		Add WPA key process

	Arguments:
		pAd						Pointer to our adapter
		pBuf				    Pointer to the where the key stored

	Return Value:
		NDIS_SUCCESS		    Add key successfully

	========================================================================
*/
NDIS_STATUS RTMPWPAAddKeyProc(IN PRTMP_ADAPTER pAd, IN PVOID pBuf)
{
	PNDIS_802_11_KEY pKey;
	ULONG KeyIdx;
//      NDIS_STATUS                     Status;

	PUCHAR pTxMic, pRxMic;
	BOOLEAN bTxKey;		// Set the key as transmit key
	BOOLEAN bPairwise;	// Indicate the key is pairwise key
	BOOLEAN bKeyRSC;	// indicate the receive  SC set by KeyRSC value.
	// Otherwise, it will set by the NIC.
	BOOLEAN bAuthenticator;	// indicate key is set by authenticator.

	pKey = (PNDIS_802_11_KEY) pBuf;
	KeyIdx = pKey->KeyIndex & 0xff;
	// Bit 31 of Add-key, Tx Key
	bTxKey = (pKey->KeyIndex & 0x80000000) ? TRUE : FALSE;
	// Bit 30 of Add-key PairwiseKey
	bPairwise = (pKey->KeyIndex & 0x40000000) ? TRUE : FALSE;
	// Bit 29 of Add-key KeyRSC
	bKeyRSC = (pKey->KeyIndex & 0x20000000) ? TRUE : FALSE;
	// Bit 28 of Add-key Authenticator
	bAuthenticator = (pKey->KeyIndex & 0x10000000) ? TRUE : FALSE;

	//
	// Check Group / Pairwise Key
	//
	if (bPairwise)		// Pairwise Key
	{
		// 1. KeyIdx must be 0, otherwise, return NDIS_STATUS_FAILURE
		if (KeyIdx != 0)
			return (NDIS_STATUS_FAILURE);

		// 2. Check bTx, it must be true, otherwise, return NDIS_STATUS_FAILURE
		if (bTxKey == FALSE)
			return (NDIS_STATUS_FAILURE);

		// 3. If BSSID is all 0xff, return NDIS_STATUS_FAILURE
		if (memcmp(pKey->BSSID, BROADCAST_ADDR, ETH_ALEN) == 0)
			return (NDIS_STATUS_FAILURE);

		// 3.1 Check Pairwise key length for TKIP key. For AES, it's always 128 bits
		//if ((pAd->PortCfg.WepStatus == Ndis802_11Encryption2Enabled) && (pKey->KeyLength != LEN_TKIP_KEY))
		if ((pAd->PortCfg.PairCipher == Ndis802_11Encryption2Enabled)
		    && (pKey->KeyLength != LEN_TKIP_KEY))
			return (NDIS_STATUS_FAILURE);

		pAd->SharedKey[KeyIdx].Type = PAIRWISE_KEY;

		if (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPA2) {
			// Send media specific event to start PMKID caching
			RTMPIndicateWPA2Status(pAd);
		}
	} else			// Group Key
	{
		// 1. Check BSSID, if not current BSSID or Bcast, return NDIS_STATUS_FAILURE
		if ((memcmp(pKey->BSSID, BROADCAST_ADDR, ETH_ALEN) != 0) &&
		    (memcmp(pKey->BSSID, pAd->PortCfg.Bssid, ETH_ALEN) != 0))
			return (NDIS_STATUS_FAILURE);

		// 2. Check Key index for supported Group Key
		if (KeyIdx >= GROUP_KEY_NO)	//|| KeyIdx == 0)
			return (NDIS_STATUS_FAILURE);

		// 3. Set as default Tx Key if bTxKey is TRUE
		if (bTxKey == TRUE)
			pAd->PortCfg.DefaultKeyId = (UCHAR) KeyIdx;

		pAd->SharedKey[KeyIdx].Type = GROUP_KEY;
	}

	// 4. Select RxMic / TxMic based on Supp / Authenticator
	if (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPANone) {
		// for WPA-None Tx, Rx MIC is the same
		pTxMic = (PUCHAR) (&pKey->KeyMaterial) + 16;
		pRxMic = pTxMic;
	} else if (bAuthenticator == TRUE) {
		pTxMic = (PUCHAR) (&pKey->KeyMaterial) + 16;
		pRxMic = (PUCHAR) (&pKey->KeyMaterial) + 24;
	} else {
		pRxMic = (PUCHAR) (&pKey->KeyMaterial) + 16;
		pTxMic = (PUCHAR) (&pKey->KeyMaterial) + 24;
	}

	// 5. Check RxTsc
	if (bKeyRSC == TRUE)
		memcpy(pAd->SharedKey[KeyIdx].RxTsc, &pKey->KeyRSC, 6);
	else
		memset(pAd->SharedKey[KeyIdx].RxTsc, 0, 6);

	// 6. Copy information into Pairwise Key structure.
	// pKey->KeyLength will include TxMic and RxMic, therefore, we use 16 bytes hardcoded.
	pAd->SharedKey[KeyIdx].KeyLen = (UCHAR) pKey->KeyLength;
	memcpy(pAd->SharedKey[KeyIdx].Key, &pKey->KeyMaterial, 16);

	if (pKey->KeyLength == LEN_TKIP_KEY) {
		// Only Key lenth equal to TKIP key have these
		memcpy(pAd->SharedKey[KeyIdx].RxMic, pRxMic, 8);
		memcpy(pAd->SharedKey[KeyIdx].TxMic, pTxMic, 8);
	}
	memcpy(pAd->SharedKey[KeyIdx].BssId, pKey->BSSID, ETH_ALEN);

	// Init TxTsc to one based on WiFi WPA specs
	pAd->SharedKey[KeyIdx].TxTsc[0] = 1;
	pAd->SharedKey[KeyIdx].TxTsc[1] = 0;
	pAd->SharedKey[KeyIdx].TxTsc[2] = 0;
	pAd->SharedKey[KeyIdx].TxTsc[3] = 0;
	pAd->SharedKey[KeyIdx].TxTsc[4] = 0;
	pAd->SharedKey[KeyIdx].TxTsc[5] = 0;

	if (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled)
		pAd->SharedKey[KeyIdx].CipherAlg = CIPHER_AES;
	else if (pAd->PortCfg.WepStatus == Ndis802_11Encryption2Enabled)
		pAd->SharedKey[KeyIdx].CipherAlg = CIPHER_TKIP;
	else if (pAd->PortCfg.WepStatus == Ndis802_11Encryption1Enabled) {
		if (pAd->SharedKey[KeyIdx].KeyLen == 5)
			pAd->SharedKey[KeyIdx].CipherAlg = CIPHER_WEP64;
		else if (pAd->SharedKey[KeyIdx].KeyLen == 13)
			pAd->SharedKey[KeyIdx].CipherAlg = CIPHER_WEP128;
		else
			pAd->SharedKey[KeyIdx].CipherAlg = CIPHER_NONE;
	} else
		pAd->SharedKey[KeyIdx].CipherAlg = CIPHER_NONE;

	if ((pAd->PortCfg.AuthMode >= Ndis802_11AuthModeWPA)
	    && (pAd->PortCfg.AuthMode != Ndis802_11AuthModeWPANone)) {
		//
		// Support WPA-Both , Update Group Key Cipher.
		//
		if (!bPairwise) {
			if (pAd->PortCfg.GroupCipher ==
			    Ndis802_11Encryption3Enabled)
				pAd->SharedKey[KeyIdx].CipherAlg = CIPHER_AES;
			else if (pAd->PortCfg.GroupCipher ==
				 Ndis802_11Encryption2Enabled)
				pAd->SharedKey[KeyIdx].CipherAlg = CIPHER_TKIP;
		}
	}

	DBGPRINT(RT_DEBUG_TRACE, "pAd->SharedKey[%d].CipherAlg = %d\n", KeyIdx,
		 pAd->SharedKey[KeyIdx].CipherAlg);

#if 0
	DBGPRINT_RAW(RT_DEBUG_TRACE, "%s Key #%d",
		     CipherName[pAd->SharedKey[KeyIdx].CipherAlg], KeyIdx);
	for (i = 0; i < 16; i++) {
		DBGPRINT_RAW(RT_DEBUG_TRACE, "%02x:",
			     pAd->SharedKey[KeyIdx].Key[i]);
	}
	DBGPRINT_RAW(RT_DEBUG_TRACE, "\n     Rx MIC Key = ");
	for (i = 0; i < 8; i++) {
		DBGPRINT_RAW(RT_DEBUG_TRACE, "%02x:",
			     pAd->SharedKey[KeyIdx].RxMic[i]);
	}
	DBGPRINT_RAW(RT_DEBUG_TRACE, "\n     Tx MIC Key = ");
	for (i = 0; i < 8; i++) {
		DBGPRINT_RAW(RT_DEBUG_TRACE, "%02x:",
			     pAd->SharedKey[KeyIdx].TxMic[i]);
	}
	DBGPRINT_RAW(RT_DEBUG_TRACE, "\n     RxTSC = ");
	for (i = 0; i < 6; i++) {
		DBGPRINT_RAW(RT_DEBUG_TRACE, "%02x:",
			     pAd->SharedKey[KeyIdx].RxTsc[i]);
	}
	DBGPRINT_RAW(RT_DEBUG_TRACE,
		     "\n     BSSID:%02x:%02x:%02x:%02x:%02x:%02x \n",
		     pKey->BSSID[0], pKey->BSSID[1], pKey->BSSID[2],
		     pKey->BSSID[3], pKey->BSSID[4], pKey->BSSID[5]);
#endif
	AsicAddSharedKeyEntry(pAd,
			      0,
			      (UCHAR) KeyIdx,
			      pAd->SharedKey[KeyIdx].CipherAlg,
			      pAd->SharedKey[KeyIdx].Key,
			      pAd->SharedKey[KeyIdx].TxMic,
			      pAd->SharedKey[KeyIdx].RxMic);

	if (pAd->SharedKey[KeyIdx].Type == GROUP_KEY) {
		// 802.1x port control
		pAd->PortCfg.PortSecured = WPA_802_1X_PORT_SECURED;
	}

	return (NDIS_STATUS_SUCCESS);
}

/*
	========================================================================

	Routine Description:
		Remove WPA Key process

	Arguments:
		pAd						Pointer to our adapter
		pBuf				    Pointer to the where the key stored

	Return Value:
		NDIS_SUCCESS		    Add key successfully

	========================================================================
*/
NDIS_STATUS RTMPWPARemoveKeyProc(IN PRTMP_ADAPTER pAd, IN PVOID pBuf)
{
	PNDIS_802_11_REMOVE_KEY pKey;
	ULONG KeyIdx;
	NDIS_STATUS Status = NDIS_STATUS_FAILURE;
	BOOLEAN bTxKey;		// Set the key as transmit key
	BOOLEAN bPairwise;	// Indicate the key is pairwise key
	BOOLEAN bKeyRSC;	// indicate the receive  SC set by KeyRSC value.
	// Otherwise, it will set by the NIC.
	BOOLEAN bAuthenticator;	// indicate key is set by authenticator.
	INT i;

	DBGPRINT(RT_DEBUG_TRACE, "---> RTMPWPARemoveKeyProc\n");

	pKey = (PNDIS_802_11_REMOVE_KEY) pBuf;
	KeyIdx = pKey->KeyIndex & 0xff;
	// Bit 31 of Add-key, Tx Key
	bTxKey = (pKey->KeyIndex & 0x80000000) ? TRUE : FALSE;
	// Bit 30 of Add-key PairwiseKey
	bPairwise = (pKey->KeyIndex & 0x40000000) ? TRUE : FALSE;
	// Bit 29 of Add-key KeyRSC
	bKeyRSC = (pKey->KeyIndex & 0x20000000) ? TRUE : FALSE;
	// Bit 28 of Add-key Authenticator
	bAuthenticator = (pKey->KeyIndex & 0x10000000) ? TRUE : FALSE;

	// 1. If bTx is TRUE, return failure information
	if (bTxKey == TRUE)
		return (NDIS_STATUS_FAILURE);

	// 2. Check Pairwise Key
	if (bPairwise) {
		// a. If BSSID is broadcast, remove all pairwise keys.
		// b. If not broadcast, remove the pairwise specified by BSSID
		for (i = 0; i < SHARE_KEY_NUM; i++) {
			if (memcmp
			    (pAd->SharedKey[i].BssId, pKey->BSSID,
			     ETH_ALEN) == 0) {
				DBGPRINT(RT_DEBUG_TRACE,
					 "RTMPWPARemoveKeyProc(KeyIdx=%d)\n",
					 i);
				pAd->SharedKey[i].KeyLen = 0;
				pAd->SharedKey[i].CipherAlg = CIPHER_NONE;
				AsicRemoveSharedKeyEntry(pAd, 0, (UCHAR) i);
				Status = NDIS_STATUS_SUCCESS;
				break;
			}
		}
	}
	// 3. Group Key
	else {
		// a. If BSSID is broadcast, remove all group keys indexed
		// b. If BSSID matched, delete the group key indexed.
		DBGPRINT(RT_DEBUG_TRACE, "RTMPWPARemoveKeyProc(KeyIdx=%d)\n",
			 KeyIdx);
		pAd->SharedKey[KeyIdx].KeyLen = 0;
		pAd->SharedKey[KeyIdx].CipherAlg = CIPHER_NONE;
		AsicRemoveSharedKeyEntry(pAd, 0, (UCHAR) KeyIdx);
		Status = NDIS_STATUS_SUCCESS;
	}

	return (Status);
}

/*
	========================================================================

	Routine Description:
		Construct and indicate WPA2 Media Specific Status

	Arguments:
		pAd	Pointer to our adapter

	Return Value:
		None

    Note:

	========================================================================
*/
VOID RTMPIndicateWPA2Status(IN PRTMP_ADAPTER pAd)
{
	struct {
		NDIS_802_11_STATUS_TYPE Status;
		NDIS_802_11_PMKID_CANDIDATE_LIST List;
	} Candidate;

//#if WPA_SUPPLICANT_SUPPORT
	//union iwreq_data wrqu;
	//NDIS_802_11_PMKID_CANDIDATE_LIST pmkid_list;
//#endif

	Candidate.Status = Ndis802_11StatusType_PMKID_CandidateList;
	Candidate.List.Version = 1;
	// This might need to be fixed to accomadate with current saved PKMIDs
	Candidate.List.NumCandidates = 1;
	memcpy(&Candidate.List.CandidateList[0].BSSID, pAd->PortCfg.Bssid, 6);
	Candidate.List.CandidateList[0].Flags = 0;
//      NdisMIndicateStatus(pAd->AdapterHandle, NDIS_STATUS_MEDIA_SPECIFIC_INDICATION, &Candidate, sizeof(Candidate));
//#if WPA_SUPPLICANT_SUPPORT
	//pmkid_list.Version =1;
	//pmkid_list.NumCandidates = 1;
	//memcpy(&pmkid_list.CandidateList[0].BSSID, pAd->PortCfg.Bssid, 6);
	//pmkid_list.CandidateList[0].Flags = 0;
	//memset(&wrqu, 0, sizeof(wrqu));
	//wrqu.data.flags = RT_PMKIDCAND_FLAG;
	//wrqu.data.length = sizeof(pmkid_list);
	//wireless_send_event(pAd->net_dev, IWEVCUSTOM, &wrqu, (char *) &pmkid_list);
//#endif
	DBGPRINT(RT_DEBUG_TRACE, "RTMPIndicateWPA2Status\n");
}

/*
	========================================================================

	Routine Description:
		Remove All WPA Keys

	Arguments:
		pAd						Pointer to our adapter

	Return Value:
		None

	========================================================================
*/
VOID RTMPWPARemoveAllKeys(IN PRTMP_ADAPTER pAd)
{
	INT i;

	DBGPRINT(RT_DEBUG_TRACE,
		 "RTMPWPARemoveAllKeys(AuthMode=%d, WepStatus=%d)\n",
		 pAd->PortCfg.AuthMode, pAd->PortCfg.WepStatus);

	// Link up. And it will be replaced if user changed it.
	if (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPANone)
		return;

	for (i = 0; i < SHARE_KEY_NUM; i++) {
//      if ((pAd->SharedKey[i].CipherAlg == CIPHER_WEP64)  ||
//          (pAd->SharedKey[i].CipherAlg == CIPHER_WEP128) ||
//          (pAd->SharedKey[i].CipherAlg == CIPHER_CKIP64) ||
//          (pAd->SharedKey[i].CipherAlg == CIPHER_CKIP128))
//          continue;

		DBGPRINT(RT_DEBUG_TRACE, "remove %s key #%d\n",
			 CipherName[pAd->SharedKey[i].CipherAlg], i);
		pAd->SharedKey[i].CipherAlg = CIPHER_NONE;
		pAd->SharedKey[i].KeyLen = 0;
		AsicRemoveSharedKeyEntry(pAd, 0, (UCHAR) i);
	}

}

/*
	========================================================================
	Routine Description:
		Change NIC PHY mode. Re-association may be necessary. possible settings
		include - PHY_11B, PHY_11BG_MIXED, PHY_11A, and PHY_11ABG_MIXED

	Arguments:
		pAd - Pointer to our adapter
        phymode  -

	========================================================================
*/
VOID RTMPSetPhyMode(IN PRTMP_ADAPTER pAd, IN ULONG phymode)
{
	INT i;

	DBGPRINT(RT_DEBUG_TRACE, "RTMPSetPhyMode(=%d)\n", phymode);
	// the selected phymode must be supported by the RF IC encoded in E2PROM
//  if (pAd->RfIcType == RFIC_5225)
//      phymode = PHY_11ABG_MIXED;

	// if no change, do nothing
	if (pAd->PortCfg.PhyMode == phymode)
		return;

	pAd->PortCfg.PhyMode = (UCHAR) phymode;
	BuildChannelList(pAd);

	// sanity check user setting in Registry
	for (i = 0; i < pAd->ChannelListNum; i++) {
		if (pAd->PortCfg.Channel == pAd->ChannelList[i].Channel)
			break;
	}
	if (i == pAd->ChannelListNum)
		pAd->PortCfg.Channel = FirstChannel(pAd);

	memset(pAd->PortCfg.SupRate, 0, MAX_LEN_OF_SUPPORTED_RATES);
	memset(pAd->PortCfg.ExtRate, 0, MAX_LEN_OF_SUPPORTED_RATES);
	memset(pAd->PortCfg.DesireRate, 0, MAX_LEN_OF_SUPPORTED_RATES);
	switch (phymode) {
	case PHY_11B:
		pAd->PortCfg.SupRate[0] = 0x82;	// 1 mbps, in units of 0.5 Mbps, basic rate
		pAd->PortCfg.SupRate[1] = 0x84;	// 2 mbps, in units of 0.5 Mbps, basic rate
		pAd->PortCfg.SupRate[2] = 0x8B;	// 5.5 mbps, in units of 0.5 Mbps, basic rate
		pAd->PortCfg.SupRate[3] = 0x96;	// 11 mbps, in units of 0.5 Mbps, basic rate
		pAd->PortCfg.SupRateLen = 4;
		pAd->PortCfg.ExtRateLen = 0;
		pAd->PortCfg.DesireRate[0] = 2;	// 1 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[1] = 4;	// 2 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[2] = 11;	// 5.5 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[3] = 22;	// 11 mbps, in units of 0.5 Mbps
		break;

	case PHY_11G:
	case PHY_11BG_MIXED:
	case PHY_11ABG_MIXED:
		pAd->PortCfg.SupRate[0] = 0x82;	// 1 mbps, in units of 0.5 Mbps, basic rate
		pAd->PortCfg.SupRate[1] = 0x84;	// 2 mbps, in units of 0.5 Mbps, basic rate
		pAd->PortCfg.SupRate[2] = 0x8B;	// 5.5 mbps, in units of 0.5 Mbps, basic rate
		pAd->PortCfg.SupRate[3] = 0x96;	// 11 mbps, in units of 0.5 Mbps, basic rate
		pAd->PortCfg.SupRate[4] = 0x12;	// 9 mbps, in units of 0.5 Mbps
		pAd->PortCfg.SupRate[5] = 0x24;	// 18 mbps, in units of 0.5 Mbps
		pAd->PortCfg.SupRate[6] = 0x48;	// 36 mbps, in units of 0.5 Mbps
		pAd->PortCfg.SupRate[7] = 0x6c;	// 54 mbps, in units of 0.5 Mbps
		pAd->PortCfg.SupRateLen = 8;
		pAd->PortCfg.ExtRate[0] = 0x8C;	// 6 mbps, in units of 0.5 Mbps, basic rate
		pAd->PortCfg.ExtRate[1] = 0x98;	// 12 mbps, in units of 0.5 Mbps, basic rate
		pAd->PortCfg.ExtRate[2] = 0xb0;	// 24 mbps, in units of 0.5 Mbps, basic rate
		pAd->PortCfg.ExtRate[3] = 0x60;	// 48 mbps, in units of 0.5 Mbps
		pAd->PortCfg.ExtRateLen = 4;
		pAd->PortCfg.DesireRate[0] = 2;	// 1 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[1] = 4;	// 2 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[2] = 11;	// 5.5 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[3] = 22;	// 11 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[4] = 12;	// 6 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[5] = 18;	// 9 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[6] = 24;	// 12 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[7] = 36;	// 18 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[8] = 48;	// 24 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[9] = 72;	// 36 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[10] = 96;	// 48 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[11] = 108;	// 54 mbps, in units of 0.5 Mbps
		break;

	case PHY_11A:
		pAd->PortCfg.SupRate[0] = 0x8C;	// 6 mbps, in units of 0.5 Mbps, basic rate
		pAd->PortCfg.SupRate[1] = 0x12;	// 9 mbps, in units of 0.5 Mbps
		pAd->PortCfg.SupRate[2] = 0x98;	// 12 mbps, in units of 0.5 Mbps, basic rate
		pAd->PortCfg.SupRate[3] = 0x24;	// 18 mbps, in units of 0.5 Mbps
		pAd->PortCfg.SupRate[4] = 0xb0;	// 24 mbps, in units of 0.5 Mbps, basic rate
		pAd->PortCfg.SupRate[5] = 0x48;	// 36 mbps, in units of 0.5 Mbps
		pAd->PortCfg.SupRate[6] = 0x60;	// 48 mbps, in units of 0.5 Mbps
		pAd->PortCfg.SupRate[7] = 0x6c;	// 54 mbps, in units of 0.5 Mbps
		pAd->PortCfg.SupRateLen = 8;
		pAd->PortCfg.ExtRateLen = 0;
		pAd->PortCfg.DesireRate[0] = 12;	// 6 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[1] = 18;	// 9 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[2] = 24;	// 12 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[3] = 36;	// 18 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[4] = 48;	// 24 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[5] = 72;	// 36 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[6] = 96;	// 48 mbps, in units of 0.5 Mbps
		pAd->PortCfg.DesireRate[7] = 108;	// 54 mbps, in units of 0.5 Mbps
		break;

	default:
		break;
	}

	MlmeUpdateTxRates(pAd, FALSE);
	AsicSetSlotTime(pAd, TRUE);	//FALSE);

	pAd->PortCfg.BandState = UNKNOWN_BAND;
//    MakeIbssBeacon(pAd);    // supported rates may change
}

VOID RTMPSetDesiredRates(IN PRTMP_ADAPTER pAdapter, IN LONG Rates)
{
	NDIS_802_11_RATES aryRates;

	memset(&aryRates, 0x00, sizeof(NDIS_802_11_RATES));
	switch (pAdapter->PortCfg.PhyMode) {
	case PHY_11A:		// A only
		switch (Rates) {
		case 6000000:	//6M
			aryRates[0] = 0x0c;	// 6M
			break;
		case 9000000:	//9M
			aryRates[0] = 0x12;	// 9M
			break;
		case 12000000:	//12M
			aryRates[0] = 0x18;	// 12M
			break;
		case 18000000:	//18M
			aryRates[0] = 0x24;	// 18M
			break;
		case 24000000:	//24M
			aryRates[0] = 0x30;	// 24M
			break;
		case 36000000:	//36M
			aryRates[0] = 0x48;	// 36M
			break;
		case 48000000:	//48M
			aryRates[0] = 0x60;	// 48M
			break;
		case 54000000:	//54M
			aryRates[0] = 0x6c;	// 54M
			break;
		case -1:	//Auto
		default:
			aryRates[0] = 0x6c;	// 54Mbps
			aryRates[1] = 0x60;	// 48Mbps
			aryRates[2] = 0x48;	// 36Mbps
			aryRates[3] = 0x30;	// 24Mbps
			aryRates[4] = 0x24;	// 18M
			aryRates[5] = 0x18;	// 12M
			aryRates[6] = 0x12;	// 9M
			aryRates[7] = 0x0c;	// 6M
			break;
		}
		break;
	case PHY_11BG_MIXED:	// B/G Mixed
	case PHY_11B:		// B only
	case PHY_11ABG_MIXED:	// A/B/G Mixed
	default:
		switch (Rates) {
		case 1000000:	//1M
			aryRates[0] = 0x02;
			break;
		case 2000000:	//2M
			aryRates[0] = 0x04;
			break;
		case 5000000:	//5.5M
			aryRates[0] = 0x0b;	// 5.5M
			break;
		case 11000000:	//11M
			aryRates[0] = 0x16;	// 11M
			break;
		case 6000000:	//6M
			aryRates[0] = 0x0c;	// 6M
			break;
		case 9000000:	//9M
			aryRates[0] = 0x12;	// 9M
			break;
		case 12000000:	//12M
			aryRates[0] = 0x18;	// 12M
			break;
		case 18000000:	//18M
			aryRates[0] = 0x24;	// 18M
			break;
		case 24000000:	//24M
			aryRates[0] = 0x30;	// 24M
			break;
		case 36000000:	//36M
			aryRates[0] = 0x48;	// 36M
			break;
		case 48000000:	//48M
			aryRates[0] = 0x60;	// 48M
			break;
		case 54000000:	//54M
			aryRates[0] = 0x6c;	// 54M
			break;
		case -1:	//Auto
		default:
			if (pAdapter->PortCfg.PhyMode == PHY_11B) {	//B Only
				aryRates[0] = 0x16;	// 11Mbps
				aryRates[1] = 0x0b;	// 5.5Mbps
				aryRates[2] = 0x04;	// 2Mbps
				aryRates[3] = 0x02;	// 1Mbps
			} else {	//(B/G) Mixed or (A/B/G) Mixed
				aryRates[0] = 0x6c;	// 54Mbps
				aryRates[1] = 0x60;	// 48Mbps
				aryRates[2] = 0x48;	// 36Mbps
				aryRates[3] = 0x30;	// 24Mbps
				aryRates[4] = 0x16;	// 11Mbps
				aryRates[5] = 0x0b;	// 5.5Mbps
				aryRates[6] = 0x04;	// 2Mbps
				aryRates[7] = 0x02;	// 1Mbps
			}
			break;
		}
		break;
	}

	memset(pAdapter->PortCfg.DesireRate, 0, MAX_LEN_OF_SUPPORTED_RATES);
	memcpy(pAdapter->PortCfg.DesireRate, &aryRates,
	       sizeof(NDIS_802_11_RATES));
	DBGPRINT(RT_DEBUG_TRACE,
		 " RTMPSetDesiredRates (%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x)\n",
		 pAdapter->PortCfg.DesireRate[0],
		 pAdapter->PortCfg.DesireRate[1],
		 pAdapter->PortCfg.DesireRate[2],
		 pAdapter->PortCfg.DesireRate[3],
		 pAdapter->PortCfg.DesireRate[4],
		 pAdapter->PortCfg.DesireRate[5],
		 pAdapter->PortCfg.DesireRate[6],
		 pAdapter->PortCfg.DesireRate[7]);
	// Changing DesiredRate may affect the MAX TX rate we used to TX frames out
	MlmeUpdateTxRates(pAdapter, FALSE);
}


#ifdef RT61_DBG
/*
    ==========================================================================
    Description:
        Read / Write BBP
    Arguments:
        pAdapter                    Pointer to our adapter
        wrq                         Pointer to the ioctl argument

    Return Value:
        None

    Note:
        Usage:
               1.) iwpriv ra0 bbp               ==> read all BBP
               2.) iwpriv ra0 bbp 1             ==> read BBP where RegID=1
               3.) iwpriv ra0 bbp 1=10		    ==> write BBP R1=0x10
    ==========================================================================
*/
VOID RTMPIoctlBBP(IN PRTMP_ADAPTER pAdapter, IN struct iwreq * wrq)
{
	CHAR *this_char;
	CHAR *value;
	UCHAR regBBP = 0;
	CHAR *msg = NULL;
	CHAR *arg = NULL;
	LONG bbpId;
	LONG bbpValue;
	BOOLEAN bIsPrintAllBBP = FALSE;
	INT Status;

	msg = kmalloc(2048, MEM_ALLOC_FLAG);
	arg = kmalloc(255, MEM_ALLOC_FLAG);
	if (!msg || !arg) {
		goto out;
	}

	DBGPRINT(RT_DEBUG_INFO, "==>RTMPIoctlBBP\n");

	memset(msg, 0x00, 2048);
	memset(arg, 0x00, 255);
	if (wrq->u.data.length > 1)	//No parameters.
	{
		if (copy_from_user
		    (arg, wrq->u.data.pointer,
		     (wrq->u.data.length > 255) ? 255 : wrq->u.data.length)) {
			goto out;
		}
		sprintf(msg, "\n");

		//Parsing Read or Write
		this_char = arg;
		DBGPRINT(RT_DEBUG_INFO, "this_char=%s\n", this_char);
		if (!*this_char)
			goto next;

		if ((value = rtstrchr(this_char, '=')) != NULL)
			*value++ = 0;

		if (!value || !*value) {	//Read
			DBGPRINT(RT_DEBUG_INFO, "this_char=%s, value=%s\n",
				 this_char, value);
			if (sscanf(this_char, "%d", &(bbpId)) == 1) {
				if (bbpId <= 108) {
					RTMP_BBP_IO_READ8_BY_REG_ID(pAdapter,
								    bbpId,
								    &regBBP);
					sprintf(msg + strlen(msg),
						"R%02d[0x%02X]:%02X  ", bbpId,
						bbpId * 2, regBBP);
					DBGPRINT(RT_DEBUG_INFO, "msg=%s\n",
						 msg);
				} else {	//Invalid parametes, so default printk all bbp
					bIsPrintAllBBP = TRUE;
					goto next;
				}
			} else {	//Invalid parametes, so default printk all bbp
				bIsPrintAllBBP = TRUE;
				goto next;
			}
		} else {	//Write
			DBGPRINT(RT_DEBUG_INFO, "this_char=%s, value=%s\n",
				 this_char, value);
			if ((sscanf(this_char, "%d", &(bbpId)) == 1)
			    && (sscanf(value, "%x", &(bbpValue)) == 1)) {
				DBGPRINT(RT_DEBUG_INFO,
					 "bbpID=%02d, value=0x%x\n", bbpId,
					 bbpValue);
				if (bbpId <= 108) {
					RTMP_BBP_IO_WRITE8_BY_REG_ID(pAdapter,
								     (UCHAR)
								     bbpId,
								     (UCHAR)
								     bbpValue);
					//Read it back for showing
					RTMP_BBP_IO_READ8_BY_REG_ID(pAdapter,
								    bbpId,
								    &regBBP);
					sprintf(msg + strlen(msg),
						"R%02d[0x%02X]:%02X\n", bbpId,
						bbpId * 2, regBBP);
					DBGPRINT(RT_DEBUG_INFO, "msg=%s\n",
						 msg);
				} else {	//Invalid parametes, so default printk all bbp
					bIsPrintAllBBP = TRUE;
					goto next;
				}
			} else {	//Invalid parametes, so default printk all bbp
				bIsPrintAllBBP = TRUE;
				goto next;
			}
		}
	} else
		bIsPrintAllBBP = TRUE;

      next:
	if (bIsPrintAllBBP) {
		memset(msg, 0x00, 2048);
		sprintf(msg, "\n");
		for (bbpId = 0; bbpId <= 108; bbpId++) {
			RTMP_BBP_IO_READ8_BY_REG_ID(pAdapter, bbpId, &regBBP);
			sprintf(msg + strlen(msg), "R%02d[0x%02X]:%02X    ",
				bbpId, bbpId * 2, regBBP);
			if (bbpId % 5 == 4)
				sprintf(msg + strlen(msg), "\n");
		}
		// Copy the information into the user buffer
		DBGPRINT(RT_DEBUG_TRACE, "strlen(msg)=%d\n", strlen(msg));
		wrq->u.data.length = strlen(msg);
		if (copy_to_user(wrq->u.data.pointer, msg, wrq->u.data.length)) {
			DBGPRINT(RT_DEBUG_ERROR,
				 "RTMPIoctlBBP - copy to user failure\n");
			Status = -EFAULT;
		}
	} else {
		DBGPRINT(RT_DEBUG_INFO, "copy to user [msg=%s]\n", msg);
		// Copy the information into the user buffer
		DBGPRINT(RT_DEBUG_INFO, "strlen(msg) =%d\n", strlen(msg));
		wrq->u.data.length = strlen(msg);
		if (copy_to_user(wrq->u.data.pointer, msg, wrq->u.data.length)) {
			DBGPRINT(RT_DEBUG_ERROR,
				 "RTMPIoctlBBP - copy to user failure\n");
			Status = -EFAULT;
		}
	}

	DBGPRINT(RT_DEBUG_TRACE, "<==RTMPIoctlBBP\n\n");

      out:
	kfree(msg);
	kfree(arg);
}

/*
    ==========================================================================
    Description:
        Read / Write MAC
    Arguments:
        pAdapter                    Pointer to our adapter
        wrq                         Pointer to the ioctl argument

    Return Value:
        None

    Note:
        Usage:
               1.) iwpriv ra0 mac 0        ==> read MAC where Addr=0x0
               2.) iwpriv ra0 mac 0=12     ==> write MAC where Addr=0x0, value=12
    ==========================================================================
*/
VOID RTMPIoctlMAC(IN PRTMP_ADAPTER pAdapter, IN struct iwreq *wrq)
{
	CHAR *this_char;
	CHAR *value;
	INT j = 0, k = 0;
	CHAR *msg = NULL;
	CHAR *arg = NULL;
	ULONG macAddr = 0;
	UCHAR temp[16], temp2[16];
	ULONG macValue;
	INT Status;

	msg = kmalloc(1024, MEM_ALLOC_FLAG);
	arg = kmalloc(255, MEM_ALLOC_FLAG);
	if (!msg || !arg) {
		goto out;
	}

	DBGPRINT(RT_DEBUG_INFO, "==>RTMPIoctlMAC\n");

	memset(msg, 0x00, 1024);
	memset(arg, 0x00, 255);
	if (wrq->u.data.length > 1)	//No parameters.
	{
		if (copy_from_user
		    (arg, wrq->u.data.pointer,
		     (wrq->u.data.length > 255) ? 255 : wrq->u.data.length)) {
			goto out;
		}
		sprintf(msg, "\n");

		//Parsing Read or Write
		this_char = arg;
		DBGPRINT(RT_DEBUG_INFO, "this_char=%s\n", this_char);
		if (!*this_char)
			goto next;

		if ((value = rtstrchr(this_char, '=')) != NULL)
			*value++ = 0;

		if (!value || !*value) {	//Read
			DBGPRINT(RT_DEBUG_INFO,
				 "Read: this_char=%s, strlen=%d\n", this_char,
				 strlen(this_char));

			// Sanity check
			if (strlen(this_char) > 4)
				goto next;

			j = strlen(this_char);
			while (j-- > 0) {
				if (this_char[j] > 'f' || this_char[j] < '0')
					return;
			}

			// Mac Addr
			k = j = strlen(this_char);
			while (j-- > 0) {
				this_char[4 - k + j] = this_char[j];
			}

			while (k < 4)
				this_char[3 - k++] = '0';
			this_char[4] = '\0';

			if (strlen(this_char) == 4) {
				AtoH(this_char, temp, 4);
				macAddr = *temp * 256 + temp[1];
				if (macAddr < 0xFFFF) {
					RTMP_IO_READ32(pAdapter, macAddr,
						       &macValue);
					DBGPRINT(RT_DEBUG_TRACE,
						 "MacAddr=%x, MacValue=%x\n",
						 macAddr, macValue);
					sprintf(msg + strlen(msg),
						"[0x%08X]:%08X  ", macAddr,
						macValue);
					DBGPRINT(RT_DEBUG_INFO, "msg=%s\n",
						 msg);
				} else {	//Invalid parametes, so default printk all bbp
					goto next;
				}
			}
		} else {	//Write
			DBGPRINT(RT_DEBUG_INFO,
				 "Write: this_char=%s, strlen(value)=%d, value=%s\n",
				 this_char, strlen(value), value);
			memcpy(&temp2, value, strlen(value));
			temp2[strlen(value)] = '\0';

			// Sanity check
			if ((strlen(this_char) > 4) || strlen(temp2) > 8)
				goto next;

			j = strlen(this_char);
			while (j-- > 0) {
				if (this_char[j] > 'f' || this_char[j] < '0')
					return;
			}

			j = strlen(temp2);
			while (j-- > 0) {
				if (temp2[j] > 'f' || temp2[j] < '0')
					return;
			}

			//MAC Addr
			k = j = strlen(this_char);
			while (j-- > 0) {
				this_char[4 - k + j] = this_char[j];
			}

			while (k < 4)
				this_char[3 - k++] = '0';
			this_char[4] = '\0';

			//MAC value
			k = j = strlen(temp2);
			while (j-- > 0) {
				temp2[8 - k + j] = temp2[j];
			}

			while (k < 8)
				temp2[7 - k++] = '0';
			temp2[8] = '\0';

			{
				AtoH(this_char, temp, 4);
				macAddr = *temp * 256 + temp[1];

				AtoH(temp2, temp, 8);
				macValue =
				    *temp * 256 * 256 * 256 +
				    temp[1] * 256 * 256 + temp[2] * 256 +
				    temp[3];

				// debug mode
				if (macAddr == (HW_DEBUG_SETTING_BASE + 4)) {
					// 0x2bf4: byte0 non-zero: enable R17 tuning, 0: disable R17 tuning
					if (macValue & 0x000000ff) {
						pAdapter->BbpTuning.bEnable =
						    TRUE;
						DBGPRINT(RT_DEBUG_TRACE,
							 "turn on R17 tuning\n");
					} else {
						UCHAR R17;
						pAdapter->BbpTuning.bEnable =
						    FALSE;
						if (pAdapter->PortCfg.Channel >
						    14)
							R17 =
							    pAdapter->BbpTuning.
							    R17LowerBoundA;
						else
							R17 =
							    pAdapter->BbpTuning.
							    R17LowerBoundG;
						RTMP_BBP_IO_WRITE8_BY_REG_ID
						    (pAdapter, 17, R17);
						DBGPRINT(RT_DEBUG_TRACE,
							 "turn off R17 tuning, restore to 0x%02x\n",
							 R17);
					}
					return;
				}

				DBGPRINT(RT_DEBUG_TRACE,
					 "MacAddr=%02x, MacValue=0x%x\n",
					 macAddr, macValue);

				RTMP_IO_WRITE32(pAdapter, macAddr, macValue);
				sprintf(msg + strlen(msg), "[0x%08X]:%08X  ",
					macAddr, macValue);
				DBGPRINT(RT_DEBUG_INFO, "msg=%s\n", msg);
			}
		}
	}
      next:
	if (strlen(msg) == 1)
		sprintf(msg + strlen(msg), "===>Error command format!");
	DBGPRINT(RT_DEBUG_INFO, "copy to user [msg=%s]\n", msg);
	// Copy the information into the user buffer
	DBGPRINT(RT_DEBUG_INFO, "strlen(msg) =%d\n", strlen(msg));
	wrq->u.data.length = strlen(msg);
	if (copy_to_user(wrq->u.data.pointer, msg, wrq->u.data.length)) {
		DBGPRINT(RT_DEBUG_ERROR,
			 "RTMPIoctlMAC - copy to user failure.\n");
		Status = -EFAULT;
	}

	DBGPRINT(RT_DEBUG_TRACE, "<==RTMPIoctlMAC\n\n");

      out:
	kfree(msg);
	kfree(arg);
}

#ifdef RALINK_ATE
/*
    ==========================================================================
    Description:
        Read / Write E2PROM
    Arguments:
        pAdapter                    Pointer to our adapter
        wrq                         Pointer to the ioctl argument

    Return Value:
        None

    Note:
        Usage:
               1.) iwpriv ra0 e2p 0     	==> read E2PROM where Addr=0x0
               2.) iwpriv ra0 e2p 0=1234    ==> write E2PROM where Addr=0x0, value=1234
    ==========================================================================
*/
VOID RTMPIoctlE2PROM(IN PRTMP_ADAPTER pAdapter, IN struct iwreq *wrq)
{
	CHAR *this_char;
	CHAR *value;
	INT j = 0, k = 0;
	CHAR *msg = NULL;
	CHAR *arg = NULL;
	USHORT eepAddr = 0;
	UCHAR temp[16], temp2[16];
	USHORT eepValue;
	int Status;

	msg = kmalloc(1024, MEM_ALLOC_FLAG);
	arg = kmalloc(255, MEM_ALLOC_FLAG);
	if (!msg || !arg) {
		goto out;
	}

	DBGPRINT(RT_DEBUG_INFO, "==>RTMPIoctlE2PROM\n");
	memset(msg, 0x00, 1024);
	memset(arg, 0x00, 255);
	if (wrq->u.data.length > 1)	//No parameters.
	{
		if (copy_from_user
		    (arg, wrq->u.data.pointer,
		     (wrq->u.data.length > 255) ? 255 : wrq->u.data.length)) {
			goto out;
		}

		sprintf(msg, "\n");

		//Parsing Read or Write
		this_char = arg;
		DBGPRINT(RT_DEBUG_INFO, "this_char=%s\n", this_char);

		if (!*this_char)
			goto next;

		if ((value = rtstrchr(this_char, '=')) != NULL)
			*value++ = 0;

		if (!value || !*value) {	//Read
			DBGPRINT(RT_DEBUG_INFO,
				 "Read: this_char=%s, strlen=%d\n", this_char,
				 strlen(this_char));

			// Sanity check
			if (strlen(this_char) > 4)
				goto next;

			j = strlen(this_char);
			while (j-- > 0) {
				if (this_char[j] > 'f' || this_char[j] < '0')
					goto out;
			}

			// E2PROM addr
			k = j = strlen(this_char);
			while (j-- > 0) {
				this_char[4 - k + j] = this_char[j];
			}

			while (k < 4)
				this_char[3 - k++] = '0';
			this_char[4] = '\0';

			if (strlen(this_char) == 4) {
				AtoH(this_char, temp, 4);
				eepAddr = *temp * 256 + temp[1];
				if (eepAddr < 0xFFFF) {
					eepValue =
					    RTMP_EEPROM_READ16(pAdapter,
							       eepAddr);
					DBGPRINT(RT_DEBUG_INFO,
						 "eepAddr=%x, eepValue=0x%x\n",
						 eepAddr, eepValue);
					sprintf(msg + strlen(msg),
						"[0x%04X]:0x%X  ", eepAddr,
						eepValue);
					DBGPRINT(RT_DEBUG_INFO, "msg=%s\n",
						 msg);
				} else {	//Invalid parametes, so default printk all bbp
					goto next;
				}
			}
		} else {	//Write
			DBGPRINT(RT_DEBUG_INFO,
				 "Write: this_char=%s, strlen(value)=%d, value=%s\n",
				 this_char, strlen(value), value);
			memcpy(&temp2, value, strlen(value));
			temp2[strlen(value)] = '\0';

			// Sanity check
			if ((strlen(this_char) > 4) || strlen(temp2) > 8)
				goto next;

			j = strlen(this_char);
			while (j-- > 0) {
				if (this_char[j] > 'f' || this_char[j] < '0')
					goto out;
			}
			j = strlen(temp2);
			while (j-- > 0) {
				if (temp2[j] > 'f' || temp2[j] < '0')
					goto out;
			}

			//MAC Addr
			k = j = strlen(this_char);
			while (j-- > 0) {
				this_char[4 - k + j] = this_char[j];
			}

			while (k < 4)
				this_char[3 - k++] = '0';
			this_char[4] = '\0';

			//MAC value
			k = j = strlen(temp2);
			while (j-- > 0) {
				temp2[4 - k + j] = temp2[j];
			}

			while (k < 4)
				temp2[3 - k++] = '0';
			temp2[4] = '\0';

			AtoH(this_char, temp, 4);
			eepAddr = *temp * 256 + temp[1];

			AtoH(temp2, temp, 4);
			eepValue = *temp * 256 + temp[1];

			DBGPRINT(RT_DEBUG_INFO, "eepAddr=%02x, eepValue=0x%x\n",
				 eepAddr, eepValue);

			RTMP_EEPROM_WRITE16(pAdapter, eepAddr, eepValue);
			sprintf(msg + strlen(msg), "[0x%02X]:%02X  ", eepAddr,
				eepValue);
			DBGPRINT(RT_DEBUG_INFO, "msg=%s\n", msg);
		}
	}
      next:
	if (strlen(msg) == 1)
		sprintf(msg + strlen(msg), "===>Error command format!");

	// Copy the information into the user buffer
	DBGPRINT(RT_DEBUG_INFO, "copy to user [msg=%s]\n", msg);
	wrq->u.data.length = strlen(msg);
	if (copy_to_user(wrq->u.data.pointer, msg, wrq->u.data.length))
		Status = -EFAULT;

	DBGPRINT(RT_DEBUG_TRACE, "<==RTMPIoctlE2PROM\n");
      out:
	kfree(msg);
	kfree(arg);
}
#endif				//RALINK_ATE
#endif				//#ifdef RT61_DBG

/*
    ==========================================================================
    Description:
        Read statistics counter
Arguments:
    pAdapter                    Pointer to our adapter
    wrq                         Pointer to the ioctl argument

    Return Value:
        None

    Note:
        Usage:
               1.) iwpriv ra0 stat 0     	==> Read statistics counter
    ==========================================================================
*/
VOID RTMPIoctlStatistics(IN PRTMP_ADAPTER pAd, IN struct iwreq *wrq)
{
	INT Status;
	char *msg;

	msg = (CHAR *) kmalloc(sizeof(CHAR) * (2048), MEM_ALLOC_FLAG);
	if (msg == NULL) {
		return;
	}

	memset(msg, 0x00, 1600);
	sprintf(msg, "\n");

	sprintf(msg + strlen(msg), "Tx success                      = %d\n",
		(ULONG) pAd->WlanCounters.TransmittedFragmentCount.QuadPart);
	sprintf(msg + strlen(msg), "Tx success without retry        = %d\n",
		(ULONG) pAd->WlanCounters.TransmittedFragmentCount.QuadPart -
		(ULONG) pAd->WlanCounters.RetryCount.QuadPart);
	sprintf(msg + strlen(msg), "Tx success after retry          = %d\n",
		(ULONG) pAd->WlanCounters.RetryCount.QuadPart);
	sprintf(msg + strlen(msg), "Tx fail to Rcv ACK after retry  = %d\n",
		(ULONG) pAd->WlanCounters.FailedCount.QuadPart);
	sprintf(msg + strlen(msg), "RTS Success Rcv CTS             = %d\n",
		(ULONG) pAd->WlanCounters.RTSSuccessCount.QuadPart);
	sprintf(msg + strlen(msg), "RTS Fail Rcv CTS                = %d\n",
		(ULONG) pAd->WlanCounters.RTSFailureCount.QuadPart);

	sprintf(msg + strlen(msg), "Rx success                      = %d\n",
		(ULONG) pAd->WlanCounters.ReceivedFragmentCount.QuadPart);
	sprintf(msg + strlen(msg), "Rx with CRC                     = %d\n",
		(ULONG) pAd->WlanCounters.FCSErrorCount.QuadPart);
	sprintf(msg + strlen(msg), "Rx drop due to out of resource  = %d\n",
		(ULONG) pAd->Counters8023.RxNoBuffer);
	sprintf(msg + strlen(msg), "Rx duplicate frame              = %d\n",
		(ULONG) pAd->WlanCounters.FrameDuplicateCount.QuadPart);

	sprintf(msg + strlen(msg), "False CCA (one second)          = %d\n",
		(ULONG) pAd->RalinkCounters.OneSecFalseCCACnt);
	sprintf(msg + strlen(msg), "RSSI-A                          = %d\n",
		(LONG) (pAd->PortCfg.LastRssi - pAd->BbpRssiToDbmDelta));

	if ((pAd->RfIcType == RFIC_5325) || (pAd->RfIcType == RFIC_2529))
		sprintf(msg + strlen(msg), "RSSI-B                          = %d\n\n",
			(LONG) (pAd->PortCfg.LastRssi2 - pAd->BbpRssiToDbmDelta));

	// Copy the information into the user buffer
	DBGPRINT(RT_DEBUG_INFO, "copy to user [msg=%s]\n", msg);
	wrq->u.data.length = strlen(msg);
	if (copy_to_user(wrq->u.data.pointer, msg, wrq->u.data.length))
		Status = -EFAULT;

	kfree(msg);
	DBGPRINT(RT_DEBUG_TRACE, "<==RTMPIoctlStatistics\n");
}

INT RTMPIoctlSetRFMONTX(IN PRTMP_ADAPTER pAd, IN struct iwreq *wrq)
{
	char *pvalue;
	char value;

	if (wrq->u.data.length) {
		// An argument is supplied --> Set the state.
		pvalue = wrq->u.data.pointer;
		value = *pvalue;

		if (value == 1) {
			pAd->PortCfg.RFMONTX = TRUE;
			pAd->net_dev->type = ARPHRD_IEEE80211;
		} else if (!value) {
			pAd->PortCfg.RFMONTX = FALSE;
			if (pAd->PortCfg.BssType == BSS_MONITOR)
				pAd->net_dev->type = ARPHRD_IEEE80211_PRISM;
			else
				pAd->net_dev->type = ARPHRD_ETHER;
		} else
			return -EINVAL;
	}

	return 0;
}


INT RTMPIoctlGetRFMONTX(IN PRTMP_ADAPTER pAd, OUT struct iwreq *wrq)
{
    *(int *) wrq->u.name = pAd->PortCfg.RFMONTX == TRUE ? 1 : 0;

    return 0;
}

/*
	==========================================================================
	Description:
		Parse encryption type
	Arguments:
		pAdapter                    Pointer to our adapter
		wrq                         Pointer to the ioctl argument

	Return Value:
		None

	Note:
	==========================================================================
*/
static CHAR *GetEncryptType(CHAR enc, CHAR encBitMode)
{
	/*** parse TKIP/AES mixed mode ***/
	if (encBitMode == (TKIPBIT | CCMPBIT))
		return "TKIP, AES";

	if (enc == Ndis802_11WEPDisabled)
		return "NONE";
	if (enc == Ndis802_11WEPEnabled)
		return "WEP";
	if (enc == Ndis802_11Encryption2Enabled)
		return "TKIP";
	if (enc == Ndis802_11Encryption3Enabled)
		return "AES";
	else
		return "UNKNOW";
}

static CHAR *GetAuthMode(CHAR auth, CHAR authBitMode)
{
	/*** parse WPA1/WPA2 mixed mode ***/
	if (authBitMode == (WPA1PSKAKMBIT | WPA2PSKAKMBIT))
		return "WPA(2)-PSK";
	if (authBitMode == (WPA1AKMBIT | WPA2AKMBIT))
		return "WPA(2)";

	/*** parse SSN/SSNPSK mixed mode ***/
	if (authBitMode == (WPA1AKMBIT | WPA1PSKAKMBIT))
		return "WPA/WPA-PSK";
	if (authBitMode == (WPA2AKMBIT | WPA2PSKAKMBIT))
		return "WPA2/WPA2-PSK";
	if (authBitMode == (WPA1AKMBIT | WPA1PSKAKMBIT | WPA2AKMBIT | WPA2PSKAKMBIT))
		return "WPA(2)/WPA(2)-PSK";

	if (auth == Ndis802_11AuthModeOpen)
		return "OPEN";
	if (auth == Ndis802_11AuthModeShared)
		return "SHARED";
	if (auth == Ndis802_11AuthModeWPA)
		return "WPA";
	if (auth == Ndis802_11AuthModeWPAPSK)
		return "WPA-PSK";
	if (auth == Ndis802_11AuthModeWPANone)
		return "WPANONE";
	if (auth == Ndis802_11AuthModeWPA2)
		return "WPA2";
	if (auth == Ndis802_11AuthModeWPA2PSK)
		return "WPA2-PSK";
	else
		return "UNKNOW";
}

/*
    ==========================================================================
    Description:
        Get site survey results
	Arguments:
	    pAdapter                    Pointer to our adapter
	    wrq                         Pointer to the ioctl argument

    Return Value:
        None

    Note:
        Usage:
        		1.) UI needs to wait 4 seconds after issue a site survey command
        		2.) iwpriv ra0 get_site_survey
        		3.) UI needs to prepare at least 4096bytes to get the results
    ==========================================================================
*/
#define	LINE_LEN	(8+8+36+20+12+20+12)	// Channel+RSSI+SSID+Bssid+WepStatus+AuthMode+NetworkType
VOID RTMPIoctlGetSiteSurvey(IN PRTMP_ADAPTER pAdapter, IN struct iwreq * wrq)
{
	CHAR *msg;
	INT i = 0;
	INT Status;
	CHAR strSsid[MAX_LEN_OF_SSID + 1];	// ssid with null terminator

	msg =
	    (CHAR *) kmalloc(sizeof(CHAR) * ((MAX_LEN_OF_BSS_TABLE) * LINE_LEN),
			     MEM_ALLOC_FLAG);
	if (msg == NULL) {
		return;
	}

	memset(msg, 0, (MAX_LEN_OF_BSS_TABLE) * LINE_LEN);
	sprintf(msg, "%s", "\n");
	sprintf(msg + strlen(msg), "%-8s%-8s%-36s%-20s%-12s%-20s%-12s\n",
		"Channel", "RSSI", "SSID", "BSSID", "Enc", "Auth",
		"NetworkType");
	for (i = 0; i < MAX_LEN_OF_BSS_TABLE; i++) {
		if (pAdapter->ScanTab.BssEntry[i].Channel == 0)
			break;

		if ((strlen(msg) + LINE_LEN) >=
		    ((MAX_LEN_OF_BSS_TABLE) * LINE_LEN))
			break;

		sprintf(msg + strlen(msg), "%-8d",
			pAdapter->ScanTab.BssEntry[i].Channel);
		sprintf(msg + strlen(msg), "%-8d",
			pAdapter->ScanTab.BssEntry[i].Rssi -
			pAdapter->BbpRssiToDbmDelta);
		memcpy(strSsid, pAdapter->ScanTab.BssEntry[i].Ssid,
		       pAdapter->ScanTab.BssEntry[i].SsidLen);
		strSsid[pAdapter->ScanTab.BssEntry[i].SsidLen] = '\0';
		sprintf(msg + strlen(msg), "%-36s",
			pAdapter->ScanTab.BssEntry[i].Ssid);
		sprintf(msg + strlen(msg), "%02x:%02x:%02x:%02x:%02x:%02x   ",
			pAdapter->ScanTab.BssEntry[i].Bssid[0],
			pAdapter->ScanTab.BssEntry[i].Bssid[1],
			pAdapter->ScanTab.BssEntry[i].Bssid[2],
			pAdapter->ScanTab.BssEntry[i].Bssid[3],
			pAdapter->ScanTab.BssEntry[i].Bssid[4],
			pAdapter->ScanTab.BssEntry[i].Bssid[5]);
		sprintf(msg + strlen(msg), "%-12s",
			GetEncryptType(pAdapter->ScanTab.BssEntry[i].WepStatus,
			    	       pAdapter->ScanTab.BssEntry[i].PairCipherBitMode));
		sprintf(msg + strlen(msg), "%-20s",
			GetAuthMode(pAdapter->ScanTab.BssEntry[i].AuthMode,
			    	    pAdapter->ScanTab.BssEntry[i].AuthBitMode));
		if (pAdapter->ScanTab.BssEntry[i].BssType == BSS_ADHOC)
			sprintf(msg + strlen(msg), "%-12s\n", "Adhoc");
		else
			sprintf(msg + strlen(msg), "%-12s\n", "Infra");
	}

	wrq->u.data.length = strlen(msg);
	if (copy_to_user(wrq->u.data.pointer, msg, wrq->u.data.length))
		Status = -EFAULT;
	kfree(msg);
}

VOID RTMPMakeRSNIE(IN PRTMP_ADAPTER pAdapter, IN UCHAR GroupCipher)
{

	if (pAdapter->PortCfg.AuthMode == Ndis802_11AuthModeWPA2PSK) {
		memcpy(pAdapter->PortCfg.RSN_IE, CipherWpa2Template,
		       CipherWpa2TemplateLen);
		// Modify Group cipher
		pAdapter->PortCfg.RSN_IE[7] =
		    ((GroupCipher == Ndis802_11Encryption2Enabled) ? 0x2 : 0x4);
		// Modify Pairwise cipher
		pAdapter->PortCfg.RSN_IE[13] =
		    ((pAdapter->PortCfg.PairCipher ==
		      Ndis802_11Encryption2Enabled) ? 0x2 : 0x4);
		// Modify AKM
		pAdapter->PortCfg.RSN_IE[19] =
		    ((pAdapter->PortCfg.AuthMode ==
		      Ndis802_11AuthModeWPA2) ? 0x1 : 0x2);

		DBGPRINT_RAW(RT_DEBUG_TRACE, "WPA2PSK GroupC=%d, PairC=%d\n",
			     pAdapter->PortCfg.GroupCipher,
			     pAdapter->PortCfg.PairCipher);

		pAdapter->PortCfg.RSN_IELen = CipherWpa2TemplateLen;
	} else if (pAdapter->PortCfg.AuthMode == Ndis802_11AuthModeWPAPSK) {
		memcpy(pAdapter->PortCfg.RSN_IE, CipherWpaPskTkip,
		       CipherWpaPskTkipLen);
		// Modify Group cipher
		pAdapter->PortCfg.RSN_IE[11] =
		    ((GroupCipher == Ndis802_11Encryption2Enabled) ? 0x2 : 0x4);
		// Modify Pairwise cipher
		pAdapter->PortCfg.RSN_IE[17] =
		    ((pAdapter->PortCfg.PairCipher ==
		      Ndis802_11Encryption2Enabled) ? 0x2 : 0x4);
		// Modify AKM
		pAdapter->PortCfg.RSN_IE[23] =
		    ((pAdapter->PortCfg.AuthMode ==
		      Ndis802_11AuthModeWPA) ? 0x1 : 0x2);

		DBGPRINT_RAW(RT_DEBUG_TRACE,
			     "WPAPSK GroupC = %d, PairC=%d ,  \n ",
			     pAdapter->PortCfg.GroupCipher,
			     pAdapter->PortCfg.PairCipher);

		pAdapter->PortCfg.RSN_IELen = CipherWpaPskTkipLen;

	} else
		pAdapter->PortCfg.RSN_IELen = 0;

}

NDIS_STATUS RTMPWPANoneAddKeyProc(IN PRTMP_ADAPTER pAd, IN PVOID pBuf)
{
	PNDIS_802_11_KEY pKey;
	ULONG KeyLen;
	ULONG BufLen;
	NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
	int i;

	KeyLen = 32;
	BufLen = sizeof(NDIS_802_11_KEY) + KeyLen - 1;

	pKey = kmalloc(BufLen, MEM_ALLOC_FLAG);
	if (pKey == NULL) {
		DBGPRINT(RT_DEBUG_TRACE,
			 "RTMPWPANoneAddKeyProc() allocate memory failed \n");
		Status = NDIS_STATUS_FAILURE;
		return Status;
	}

	memset(pKey, 0, BufLen);
	pKey->Length = BufLen;
	pKey->KeyIndex = 0x80000000;
	pKey->KeyLength = KeyLen;
	for (i = 0; i < 6; i++)
		pKey->BSSID[i] = 0xff;

	memcpy(pKey->KeyMaterial, pBuf, 32);
	RTMPWPAAddKeyProc(pAd, pKey);
	kfree(pKey);

	return Status;
}

#ifdef RALINK_ATE
UCHAR TemplateFrame[24] = { 0x08, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xAA, 0xBB, 0x12, 0x34, 0x56, 0x00, 0x11, 0x22, 0xAA, 0xBB, 0xCC, 0x00, 0x00 };	// 802.11 MAC Header, Type:Data, Length:24bytes

/*
    ==========================================================================
    Description:
        Set ATE operation mode to
        0. APSTOP  = Stop AP Mode
        1. APSTART = Start AP Mode
        2. TXCONT  = Continuous Transmit
        3. TXCARR  = Transmit Carrier
        4. TXFRAME = Transmit Frames
        5. RXFRAME = Receive Frames
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/

#ifdef BIG_ENDIAN
static inline INT set_ate_proc_inline(
#else
INT Set_ATE_Proc(
#endif
			IN PRTMP_ADAPTER pAd, IN PUCHAR arg)
{
	UCHAR BbpData;
	ULONG MacData;
	PTXD_STRUC pTxD;
	PUCHAR pDest;
	UINT i, j, atemode;

#ifdef	BIG_ENDIAN
	PTXD_STRUC pDestTxD;
	TXD_STRUC TxD;
#endif

	ATEAsicSwitchChannel(pAd, pAd->ate.Channel);
	AsicLockChannel(pAd, pAd->ate.Channel);
	RTMPusecDelay(5000);

	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, 63, &BbpData);
	RTMP_IO_READ32(pAd, PHY_CSR1, &MacData);

	BbpData = 0;
	MacData &= 0xFFFFFFFE;

	if (!strcmp(arg, "STASTOP")) {
		DBGPRINT(RT_DEBUG_TRACE, "ATE: STASTOP\n");

		atemode = pAd->ate.Mode;
		pAd->ate.Mode = ATE_STASTOP;
		pAd->ate.TxDoneCount = pAd->ate.TxCount;

		// we should free some resource which allocate when ATE_TXFRAME
		if (atemode == ATE_TXFRAME) {
			RTMP_IO_WRITE32(pAd, TX_CNTL_CSR, 0x000f0000);	// abort all TX rings
			RTMPusecDelay(5000);

			for (i = 0; i < TX_RING_SIZE; i++) {
				PRTMP_DMABUF pDmaBuf =
				    &pAd->TxRing[QID_AC_BE].Cell[i].DmaBuf;
				pTxD =
				    (PTXD_STRUC) pAd->TxRing[QID_AC_BE].Cell[i].
				    AllocVa;
#ifndef BIG_ENDIAN
				pTxD =
				    (PTXD_STRUC) pAd->TxRing[QID_AC_BE].Cell[i].
				    AllocVa;
#else
				pDestTxD =
				    (PTXD_STRUC) pAd->TxRing[QID_AC_BE].Cell[i].
				    AllocVa;
				TxD = *pDestTxD;
				pTxD = &TxD;
				RTMPDescriptorEndianChange((PUCHAR) pTxD,
							   TYPE_TXD);
#endif
				pTxD->Owner = DESC_OWN_NIC;
#ifdef BIG_ENDIAN
				RTMPDescriptorEndianChange((PUCHAR) pTxD,
							   TYPE_TXD);
				WriteBackToDescriptor((PUCHAR) pDestTxD,
						      (PUCHAR) pTxD, FALSE,
						      TYPE_TXD);
#endif
				if (pDmaBuf->pSkb) {
					pci_unmap_single(pAd->pPci_Dev,
							 pTxD->BufPhyAddr1,
							 pTxD->BufLen1,
							 PCI_DMA_TODEVICE);
					RELEASE_NDIS_PACKET(pAd, pDmaBuf->pSkb);
					pDmaBuf->pSkb = NULL;
				}
			}
		}

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 63, BbpData);
		RTMP_IO_WRITE32(pAd, PHY_CSR1, MacData);

		LinkDown(pAd, FALSE);
		AsicEnableBssSync(pAd);

		netif_stop_queue(pAd->net_dev);
		RTMPStationStop(pAd);
		RTMPWriteTXRXCsr0(pAd, TRUE, FALSE);
	} else if (!strcmp(arg, "STASTART")) {
		DBGPRINT(RT_DEBUG_TRACE, "ATE: STASTART\n");

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 63, BbpData);
		RTMP_IO_WRITE32(pAd, PHY_CSR1, MacData);

		pAd->ate.Mode = ATE_STASTART;

		RTMPWriteTXRXCsr0(pAd, FALSE, FALSE);
		netif_start_queue(pAd->net_dev);

		RTMPStationStart(pAd);
	} else if (!strcmp(arg, "TXCONT"))	// Continuous Tx
	{
		DBGPRINT(RT_DEBUG_TRACE, "ATE: TXCONT\n");

		pAd->ate.Mode = ATE_TXCONT;

		BbpData |= 0x80;
		MacData |= 0x00000001;

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 63, BbpData);
		RTMP_IO_WRITE32(pAd, PHY_CSR1, MacData);

		RTMPWriteTXRXCsr0(pAd, TRUE, FALSE);
	} else if (!strcmp(arg, "TXCARR"))	// Tx Carrier
	{
		DBGPRINT(RT_DEBUG_TRACE, "ATE: TXCARR\n");
		pAd->ate.Mode = ATE_TXCARR;

		BbpData |= 0x40;
		MacData |= 0x00000001;

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 63, BbpData);
		RTMP_IO_WRITE32(pAd, PHY_CSR1, MacData);

		RTMPWriteTXRXCsr0(pAd, TRUE, FALSE);
	} else if (!strcmp(arg, "TXFRAME"))	// Tx Frames
	{
		DBGPRINT(RT_DEBUG_TRACE, "ATE: TXFRAME(Count=%d)\n",
			 pAd->ate.TxCount);
		pAd->ate.Mode = ATE_TXFRAME;

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 63, BbpData);
		RTMP_IO_WRITE32(pAd, PHY_CSR1, MacData);

		pAd->ate.TxDoneCount = 0;

		for (i = 0; (i < TX_RING_SIZE) && (i < pAd->ate.TxCount); i++) {
			struct sk_buff *pSkb;
			PRTMP_TX_RING pTxRing = &pAd->TxRing[QID_AC_BE];
			PRTMP_DMABUF pDmaBuf =
			    &pAd->TxRing[QID_AC_BE].Cell[pTxRing->CurTxIndex].
			    DmaBuf;
			PUCHAR pDestBufVA =
			    (PUCHAR) pTxRing->Cell[pTxRing->CurTxIndex].DmaBuf.
			    AllocVa;

			if (!pDmaBuf->pSkb) {
				pSkb =
				    __dev_alloc_skb(LOCAL_TXBUF_SIZE,
						    MEM_ALLOC_FLAG);
				RTMP_SET_PACKET_SOURCE(pSkb, PKTSRC_DRIVER);

				pDmaBuf->pSkb = pSkb;
			} else
				pSkb = pDmaBuf->pSkb;

#ifndef BIG_ENDIAN
			pTxD =
			    (PTXD_STRUC) pAd->TxRing[QID_AC_BE].Cell[pTxRing->
								     CurTxIndex].
			    AllocVa;
#else
			pDestTxD =
			    (PTXD_STRUC) pAd->TxRing[QID_AC_BE].Cell[pTxRing->
								     CurTxIndex].
			    AllocVa;
			TxD = *pDestTxD;
			pTxD = &TxD;
			RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif
			pDest = (PUCHAR) pSkb->data;
			pSkb->len = pAd->ate.TxLength - LENGTH_802_11;

			// Prepare frame payload
			memcpy(pDestBufVA, &TemplateFrame, LENGTH_802_11);
			memcpy(&pDestBufVA[4], pAd->ate.Addr1, ETH_ALEN);
			memcpy(&pDestBufVA[10], pAd->ate.Addr2, ETH_ALEN);
			memcpy(&pDestBufVA[16], pAd->ate.Addr3, ETH_ALEN);

			pTxD->BufLen0 = LENGTH_802_11;
			pTxD->BufLen1 = pAd->ate.TxLength - LENGTH_802_11;
			pTxD->BufPhyAddr1 =
			    pci_map_single(pAd->pPci_Dev, pSkb->data, pSkb->len,
					   PCI_DMA_TODEVICE);
			pTxD->BufCount = 2;

			for (j = 0; j < (pAd->ate.TxLength - LENGTH_802_11);
			     j++)
				pDest[j] = 0xA5;

#ifdef BIG_ENDIAN
			RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
			*pDestTxD = TxD;
			pTxD = pDestTxD;
#endif
			pTxRing->Cell[pTxRing->CurTxIndex].pSkb = pSkb;
			pTxRing->Cell[pTxRing->CurTxIndex].pNextSkb = NULL;
			RTMPWriteTxDescriptor(pAd, pTxD, CIPHER_NONE, 0, 0,
					      FALSE, FALSE, FALSE, SHORT_RETRY,
					      IFS_BACKOFF, pAd->ate.TxRate,
					      pAd->ate.TxLength, QID_AC_BE, 0,
					      FALSE, FALSE, FALSE);

			INC_RING_INDEX(pTxRing->CurTxIndex, TX_RING_SIZE);
		}
		pAd->ate.TxDoneCount += i;

		RTMP_IO_WRITE32(pAd, TX_CNTL_CSR, BIT8[QID_AC_BE]);

		RTMPWriteTXRXCsr0(pAd, TRUE, FALSE);
	} else if (!strcmp(arg, "RXFRAME"))	// Rx Frames
	{
		DBGPRINT(RT_DEBUG_TRACE, "ATE: RXFRAME\n");

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 63, BbpData);
		RTMP_IO_WRITE32(pAd, PHY_CSR1, MacData);

		pAd->ate.Mode = ATE_RXFRAME;

		RTMP_IO_WRITE32(pAd, TX_CNTL_CSR, 0x000f0000);	// abort all TX rings
		RTMPWriteTXRXCsr0(pAd, FALSE, FALSE);
	} else {
		DBGPRINT(RT_DEBUG_TRACE, "ATE: Invalid arg!\n");
		return FALSE;
	}
	RTMPusecDelay(5000);

	return TRUE;
}

#ifdef BIG_ENDIAN
INT Set_ATE_Proc(IN PRTMP_ADAPTER pAd, IN PUCHAR arg)
{

	return set_ate_proc_inline(pAd, arg);
}
#endif

/*
    ==========================================================================
    Description:
        Set ATE ADDR1=DA for TxFrames
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT Set_ATE_DA_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	CHAR *value;
	INT i;

	if (strlen(arg) != 17)	//Mac address acceptable format 01:02:03:04:05:06 length 17
		return FALSE;

	for (i = 0, value = rstrtok(arg, ":"); value;
	     value = rstrtok(NULL, ":")) {
		if ((strlen(value) != 2) || (!isxdigit(*value))
		    || (!isxdigit(*(value + 1))))
			return FALSE;	//Invalid

		AtoH(value, &pAdapter->ate.Addr1[i++], 2);
	}

	if (i != 6)
		return FALSE;	//Invalid

	DBGPRINT(RT_DEBUG_TRACE,
		 "Set_ATE_DA_Proc (DA = %2X:%2X:%2X:%2X:%2X:%2X)\n",
		 pAdapter->ate.Addr1[0], pAdapter->ate.Addr1[1],
		 pAdapter->ate.Addr1[2], pAdapter->ate.Addr1[3],
		 pAdapter->ate.Addr1[4], pAdapter->ate.Addr1[5]);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE ADDR2=SA for TxFrames
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT Set_ATE_SA_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	CHAR *value;
	INT i;

	if (strlen(arg) != 17)	//Mac address acceptable format 01:02:03:04:05:06 length 17
		return FALSE;

	for (i = 0, value = rstrtok(arg, ":"); value;
	     value = rstrtok(NULL, ":")) {
		if ((strlen(value) != 2) || (!isxdigit(*value))
		    || (!isxdigit(*(value + 1))))
			return FALSE;	//Invalid

		AtoH(value, &pAdapter->ate.Addr2[i++], 2);
	}

	if (i != 6)
		return FALSE;	//Invalid

	DBGPRINT(RT_DEBUG_TRACE,
		 "Set_ATE_SA_Proc (DA = %2X:%2X:%2X:%2X:%2X:%2X)\n",
		 pAdapter->ate.Addr2[0], pAdapter->ate.Addr2[1],
		 pAdapter->ate.Addr2[2], pAdapter->ate.Addr2[3],
		 pAdapter->ate.Addr2[4], pAdapter->ate.Addr2[5]);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE ADDR3=BSSID for TxFrames
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT Set_ATE_BSSID_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	CHAR *value;
	INT i;

	if (strlen(arg) != 17)	//Mac address acceptable format 01:02:03:04:05:06 length 17
		return FALSE;

	for (i = 0, value = rstrtok(arg, ":"); value;
	     value = rstrtok(NULL, ":")) {
		if ((strlen(value) != 2) || (!isxdigit(*value))
		    || (!isxdigit(*(value + 1))))
			return FALSE;	//Invalid

		AtoH(value, &pAdapter->ate.Addr3[i++], 2);
	}

	if (i != 6)
		return FALSE;	//Invalid

	DBGPRINT(RT_DEBUG_TRACE,
		 "Set_ATE_BSSID_Proc (DA = %2X:%2X:%2X:%2X:%2X:%2X)\n",
		 pAdapter->ate.Addr3[0], pAdapter->ate.Addr3[1],
		 pAdapter->ate.Addr3[2], pAdapter->ate.Addr3[3],
		 pAdapter->ate.Addr3[4], pAdapter->ate.Addr3[5]);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Channel
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT Set_ATE_CHANNEL_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	pAdapter->ate.Channel = simple_strtol(arg, 0, 10);

	if ((pAdapter->ate.Channel < 1) || (pAdapter->ate.Channel > 200))	// to allow A band channel
	{
		pAdapter->ate.Channel = 1;
		DBGPRINT(RT_DEBUG_ERROR,
			 "Set_ATE_CHANNEL_Proc::Out of range\n");
		return FALSE;
	}

	DBGPRINT(RT_DEBUG_TRACE, "Set_ATE_CHANNEL_Proc (ATE Channel = %d)\n",
		 pAdapter->ate.Channel);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Tx Power
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT Set_ATE_TX_POWER_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	ULONG R3;
	UCHAR Bbp94 = 0;

	pAdapter->ate.TxPower = simple_strtol(arg, 0, 10);

	if ((pAdapter->ate.TxPower > 36) || (pAdapter->ate.TxPower < -6)) {
		DBGPRINT(RT_DEBUG_ERROR,
			 "Set_ATE_TX_POWER_Proc::Out of range (Value=%d)\n",
			 pAdapter->ate.TxPower);
		return FALSE;
	}

	if (pAdapter->ate.TxPower > 31) {
		//
		// R3 can't large than 36 (0x24), 31 ~ 36 used by BBP 94
		//
		R3 = 31;
		if (pAdapter->ate.TxPower <= 36)
			Bbp94 =
			    BBPR94_DEFAULT + (UCHAR) (pAdapter->ate.TxPower -
						      31);
	} else if (pAdapter->ate.TxPower < 0) {
		//
		// R3 can't less than 0, -1 ~ -6 used by BBP 94
		//
		R3 = 0;
		if (pAdapter->ate.TxPower >= -6)
			Bbp94 = BBPR94_DEFAULT + pAdapter->ate.TxPower;
	} else {
		// 0 ~ 31
		R3 = (ULONG) pAdapter->ate.TxPower;
		Bbp94 = BBPR94_DEFAULT;
	}

	DBGPRINT(RT_DEBUG_TRACE,
		 "Set_ATE_TX_POWER_Proc (TxPower=%d, R3=%d, BBP_R94=%d)\n",
		 pAdapter->ate.TxPower, R3, Bbp94);

	R3 = R3 << 9;           // shift TX power control to correct RF register bit position
	R3 |= (pAdapter->LatchRfRegs.R3 & 0xffffc1ff);
	pAdapter->LatchRfRegs.R3 = R3;

	RTMP_RF_IO_WRITE32(pAdapter, pAdapter->LatchRfRegs.R1);
	RTMP_RF_IO_WRITE32(pAdapter, pAdapter->LatchRfRegs.R2);
	RTMP_RF_IO_WRITE32(pAdapter, pAdapter->LatchRfRegs.R3);
	RTMP_RF_IO_WRITE32(pAdapter, pAdapter->LatchRfRegs.R4);

	if (Bbp94 > 0)
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAdapter, BBP_R94, Bbp94);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE RF frequence offset

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT Set_ATE_TX_FREQOFFSET_Proc(IN PRTMP_ADAPTER pAd, IN PUCHAR arg)
{
	ULONG R4;

	pAd->ate.RFFreqOffset = simple_strtol(arg, 0, 10);

	if (pAd->ate.RFFreqOffset >= 64) {
		DBGPRINT(RT_DEBUG_ERROR,
			 "Set_ATE_TX_FREQOFFSET_Proc::Out of range\n");
		return FALSE;
	}

	R4 = pAd->ate.RFFreqOffset << 12;	// shift TX power control to correct RF register bit position
	R4 |= (pAd->LatchRfRegs.R4 & (~0x0003f000));
	pAd->LatchRfRegs.R4 = R4;

	RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R1);
	RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R2);
	RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R4);
	RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R4);

	DBGPRINT(RT_DEBUG_TRACE,
		 "Set_ATE_TX_FREQOFFSET_Proc (RFFreqOffset = %d)\n",
		 pAd->ate.RFFreqOffset);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Tx Length
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT Set_ATE_TX_LENGTH_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	pAdapter->ate.TxLength = simple_strtol(arg, 0, 10);

	if ((pAdapter->ate.TxLength < 24) || (pAdapter->ate.TxLength > 1500)) {
		pAdapter->ate.TxLength = 1500;
		DBGPRINT(RT_DEBUG_ERROR,
			 "Set_ATE_TX_LENGTH_Proc::Out of range\n");
		return FALSE;
	}

	DBGPRINT(RT_DEBUG_TRACE, "Set_ATE_TX_LENGTH_Proc (TxLength = %d)\n",
		 pAdapter->ate.TxLength);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Tx Count
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT Set_ATE_TX_COUNT_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	pAdapter->ate.TxCount = simple_strtol(arg, 0, 10);

	DBGPRINT(RT_DEBUG_TRACE, "Set_ATE_TX_COUNT_Proc (TxCount = %d)\n",
		 pAdapter->ate.TxCount);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set ATE Tx Rate
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT Set_ATE_TX_RATE_Proc(IN PRTMP_ADAPTER pAdapter, IN PUCHAR arg)
{
	pAdapter->ate.TxRate = simple_strtol(arg, 0, 10);

	if (pAdapter->ate.TxRate > RATE_54) {
		pAdapter->ate.TxRate = RATE_11;
		DBGPRINT(RT_DEBUG_ERROR,
			 "Set_ATE_TX_RATE_Proc::Out of range\n");
		return FALSE;
	}

	DBGPRINT(RT_DEBUG_TRACE, "Set_ATE_TX_RATE_Proc (TxRate = %d)\n",
		 pAdapter->ate.TxRate);

	return TRUE;
}

VOID RTMPStationStop(IN PRTMP_ADAPTER pAd)
{
	DBGPRINT(RT_DEBUG_TRACE, "==> RTMPStationStop\n");

	del_timer_sync(&pAd->MlmeAux.AssocTimer);
	del_timer_sync(&pAd->MlmeAux.ReassocTimer);
	del_timer_sync(&pAd->MlmeAux.DisassocTimer);
	del_timer_sync(&pAd->MlmeAux.AuthTimer);
	del_timer_sync(&pAd->MlmeAux.BeaconTimer);
	del_timer_sync(&pAd->MlmeAux.ScanTimer);
	del_timer_sync(&pAd->Mlme.PeriodicTimer);
	del_timer_sync(&pAd->RfTuningTimer);

	DBGPRINT(RT_DEBUG_TRACE, "<== RTMPStationStop\n");
}

VOID RTMPStationStart(IN PRTMP_ADAPTER pAd)
{
	DBGPRINT(RT_DEBUG_TRACE, "==> RTMPStationStart\n");

	pAd->Mlme.PeriodicTimer.expires = jiffies + MLME_TASK_EXEC_INTV;
	add_timer(&pAd->Mlme.PeriodicTimer);
	DBGPRINT(RT_DEBUG_TRACE, "<== RTMPStationStart\n");
}

/*
    ==========================================================================
    Description:
    ==========================================================================
 */
VOID ATEAsicSwitchChannel(IN PRTMP_ADAPTER pAd, IN UCHAR Channel)
{
	ULONG R3, R4;
	UCHAR index, BbpReg, Bbp94 = 0;
	RTMP_RF_REGS *RFRegTable;

	// Select antenna
	AsicAntennaSelect(pAd, Channel);

	R3 = pAd->ate.TxPower << 9;	// shift TX power control to correct RF R3 bit position

	if (pAd->RFProgSeq == 0)
		RFRegTable = RF5225RegTable;
	else
		RFRegTable = RF5225RegTable_1;

	switch (pAd->RfIcType) {
	case RFIC_5225:
	case RFIC_5325:
	case RFIC_2527:
	case RFIC_2529:
		for (index = 0; index < NUM_OF_5225_CHNL; index++) {
			if (Channel == RFRegTable[index].Channel) {
				R3 = R3 | (RFRegTable[index].R3 & 0xffffc1ff);	// set TX power
				R4 = (RFRegTable[index].
				      R4 & (~0x0003f000)) | (pAd->
							     RfFreqOffset <<
							     12);

				// Update variables
				pAd->LatchRfRegs.Channel = Channel;
				pAd->LatchRfRegs.R1 = RFRegTable[index].R1;
				pAd->LatchRfRegs.R2 = RFRegTable[index].R2;
				pAd->LatchRfRegs.R3 = R3;
				pAd->LatchRfRegs.R4 = R4;

				// Set RF value
				RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R1);
				RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R2);
				RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R3);
				RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R4);
				break;
			}
		}
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BbpReg);
		if ((pAd->RfIcType == RFIC_5225)
		    || (pAd->RfIcType == RFIC_2527))
			BbpReg &= 0xFE;	// b0=0 for none smart mode
		else
			BbpReg |= 0x01;	// b0=1 for smart mode
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BbpReg);

		if (Bbp94 > 0)
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R94, Bbp94);
		break;

	default:
		break;
	}

	//
	// On 11A, We should delay and wait RF/BBP to be stable
	// and the appropriate time should be 1000 micro seconds
	//
	if (Channel > 14)
		RTMPusecDelay(1000);

	DBGPRINT(RT_DEBUG_TRACE,
		 "ATEAsicSwitchChannel(RF=%d, Pwr=%d) to #%d, R1=0x%08x, R2=0x%08x, R3=0x%08x, R4=0x%08x\n",
		 pAd->RfIcType, (R3 & 0x00003e00) >> 9, Channel,
		 pAd->LatchRfRegs.R1, pAd->LatchRfRegs.R2, pAd->LatchRfRegs.R3,
		 pAd->LatchRfRegs.R4);
}
#endif				// RALINK_ATE

VOID RTMPIoctlGetRaAPCfg(IN PRTMP_ADAPTER pAdapter, IN struct iwreq *wrq)
{
	pAdapter->PortCfg.bGetAPConfig = TRUE;
	EnqueueProbeRequest(pAdapter);
	DBGPRINT(RT_DEBUG_TRACE, "RTMPIoctlGetRaAPCfg::(bGetAPConfig=%d)\n",
		 pAdapter->PortCfg.bGetAPConfig);
	return;
}
