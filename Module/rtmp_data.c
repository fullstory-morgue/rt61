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
 *      Module Name: rtmp_data.c
 *
 *      Abstract:
 *
 *      Revision History:
 *      Who             When            What
 *      --------        -----------     -----------------------------
 *      John            17th Aug 04     Major modification for RT2561/2661
 *      GertjanW        21st Jan 06     Baseline code
 *      MarkW (rt2500)  19th Feb 06     Promisc mode support
 *      MarkW (rt2500)  3rd  Jun 06     Monitor mode through iwconfig
 ***************************************************************************/

#include "rt_config.h"
#include <net/iw_handler.h>

extern UCHAR Phy11BGNextRateUpward[];	// defined in mlme.c

UCHAR SNAP_802_1H[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
UCHAR SNAP_BRIDGE_TUNNEL[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };
UCHAR EAPOL_LLC_SNAP[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e };
UCHAR EAPOL[] = { 0x88, 0x8e };

UCHAR IPX[] = { 0x81, 0x37 };
UCHAR APPLE_TALK[] = { 0x80, 0xf3 };

static UINT _11G_RATES[12] = { 0, 0, 0, 0, 6, 9, 12, 18, 24, 36, 48, 54 };

static UCHAR RateIdToPlcpSignal[12] = {
	0, /* RATE_1 */ 1, /* RATE_2 */ 2, /* RATE_5_5 */ 3,	/* RATE_11 */// see BBP spec
	11, /* RATE_6 */ 15, /* RATE_9 */ 10, /* RATE_12 */ 14,	/* RATE_18 */// see IEEE802.11a-1999 p.14
	9, /* RATE_24 */ 13, /* RATE_36 */ 8, /* RATE_48 */ 12 /* RATE_54 */
};				// see IEEE802.11a-1999 p.14

UCHAR OfdmSignalToRateId[16] = {
	RATE_54, RATE_54, RATE_54, RATE_54,	// OFDM PLCP Signal = 0,  1,  2,  3 respectively
	RATE_54, RATE_54, RATE_54, RATE_54,	// OFDM PLCP Signal = 4,  5,  6,  7 respectively
	RATE_48, RATE_24, RATE_12, RATE_6,	// OFDM PLCP Signal = 8,  9,  10, 11 respectively
	RATE_54, RATE_36, RATE_18, RATE_9,	// OFDM PLCP Signal = 12, 13, 14, 15 respectively
};

UCHAR default_cwmin[] =
    { CW_MIN_IN_BITS, CW_MIN_IN_BITS, CW_MIN_IN_BITS - 1, CW_MIN_IN_BITS - 2 };
UCHAR default_cwmax[] =
    { CW_MAX_IN_BITS, CW_MAX_IN_BITS, CW_MIN_IN_BITS, CW_MIN_IN_BITS - 1 };
UCHAR default_sta_aifsn[] = { 3, 7, 2, 2 };

UCHAR MapUserPriorityToAccessCategory[8] =
    { QID_AC_BE, QID_AC_BK, QID_AC_BK, QID_AC_BE, QID_AC_VI, QID_AC_VI,
QID_AC_VO, QID_AC_VO };

// Macro for rx indication
VOID REPORT_ETHERNET_FRAME_TO_LLC(IN PRTMP_ADAPTER pAd,
				  IN PUCHAR p8023hdr,
				  IN PUCHAR pData,
				  IN ULONG DataSize,
				  IN struct net_device *net_dev)
{
	struct sk_buff *pSkb;

	if ((pSkb =
	     __dev_alloc_skb(DataSize + LENGTH_802_3 + 2,
			     MEM_ALLOC_FLAG)) != NULL) {
		pSkb->dev = net_dev;
		skb_reserve(pSkb, 2);	// 16 byte align the IP header
		memcpy(skb_put(pSkb, LENGTH_802_3), p8023hdr, LENGTH_802_3);
		memcpy(skb_put(pSkb, DataSize), pData, DataSize);
		pSkb->protocol = eth_type_trans(pSkb, net_dev);
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocPa =
		    pci_map_single(pAd->pPci_Dev,
				   pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].
				   DmaBuf.AllocVa,
				   pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].
				   DmaBuf.AllocSize, PCI_DMA_FROMDEVICE);

		netif_rx(pSkb);

		pAd->net_dev->last_rx = jiffies;
		pAd->stats.rx_packets++;

		pAd->Counters8023.GoodReceives++;
	}
}

// Macro for rx indication
VOID REPORT_ETHERNET_FRAME_TO_LLC_WITH_NON_COPY(IN PRTMP_ADAPTER pAd,
						IN PUCHAR p8023hdr,
						IN PUCHAR pData,
						IN ULONG DataSize,
						IN struct net_device *net_dev)
{
#ifdef NONCOPY_RX
//      PRXD_STRUC              pRxD;
	struct sk_buff *pRxSkb =
	    pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.pSkb;
	struct sk_buff *pSkb = NULL;

	// allocate a new one and update RxD
	if ((pSkb =
	     __dev_alloc_skb(RX_DMA_BUFFER_SIZE, MEM_ALLOC_FLAG)) != NULL) {
		pRxSkb->dev = net_dev;
		pRxSkb->data = pData;
		pRxSkb->len = DataSize;
		pRxSkb->tail = pRxSkb->data + pRxSkb->len;
		memcpy(skb_push(pRxSkb, LENGTH_802_3), p8023hdr, LENGTH_802_3);

		pRxSkb->protocol = eth_type_trans(pRxSkb, net_dev);

		netif_rx(pRxSkb);

		pAd->net_dev->last_rx = jiffies;
		pAd->stats.rx_packets++;

		RTMP_SET_PACKET_SOURCE(pSkb, PKTSRC_DRIVER);
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocSize =
		    RX_DMA_BUFFER_SIZE;
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.pSkb = pSkb;
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocVa =
		    pSkb->data;
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocPa =
		    pci_map_single(pAd->pPci_Dev,
				   pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].
				   DmaBuf.AllocVa,
				   pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].
				   DmaBuf.AllocSize, PCI_DMA_FROMDEVICE);

		// Write RxD buffer address & allocated buffer length
		//pRxD = (PRXD_STRUC) pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].AllocVa;
		//pRxD->BufPhyAddr = pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocPa;
	} else {
		RTMP_SET_PACKET_SOURCE(pRxSkb, PKTSRC_DRIVER);
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocSize =
		    RX_DMA_BUFFER_SIZE;
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.pSkb = pRxSkb;
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocVa =
		    pRxSkb->data;
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocPa =
		    pci_map_single(pAd->pPci_Dev,
				   pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].
				   DmaBuf.AllocVa,
				   pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].
				   DmaBuf.AllocSize, PCI_DMA_FROMDEVICE);

		// Write RxD buffer address & allocated buffer length
		//pRxD = (PRXD_STRUC) pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].AllocVa;
		//pRxD->BufPhyAddr = pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocPa;
	}
#else
	struct sk_buff *pSkb;

	if ((pSkb =
	     __dev_alloc_skb(DataSize + LENGTH_802_3 + 2,
			     MEM_ALLOC_FLAG)) != NULL) {
		pSkb->dev = net_dev;
		skb_reserve(pSkb, 2);	// 16 byte align the IP header
		memcpy(skb_put(pSkb, LENGTH_802_3), p8023hdr, LENGTH_802_3);
		memcpy(skb_put(pSkb, DataSize), pData, DataSize);
		pSkb->protocol = eth_type_trans(pSkb, net_dev);

		netif_rx(pSkb);

		pAd->net_dev->last_rx = jiffies;
		pAd->stats.rx_packets++;
	}
#endif
}

// Macro for rx indication
VOID REPORT_AGGREGATE_ETHERNET_FRAME_TO_LLC_WITH_NON_COPY(IN PRTMP_ADAPTER pAd,
							  IN PUCHAR p8023hdr,
							  IN PUCHAR pData,
							  IN ULONG DataSize1,
							  IN ULONG DataSize2,
							  IN struct net_device
							  *net_dev)
{
	struct sk_buff *pRxSkb =
	    pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.pSkb;
	struct sk_buff *pSkb1 = NULL;
	struct sk_buff *pSkb2 = NULL;
	PUCHAR pData2;
	PUCHAR phdr;

	pData2 = pData + DataSize1 + LENGTH_802_3;
	phdr = pData + DataSize1;

	if ((pSkb2 =
	     __dev_alloc_skb(DataSize2 + LENGTH_802_3 + 2,
			     MEM_ALLOC_FLAG)) != NULL) {
		pSkb2->dev = net_dev;
		skb_reserve(pSkb2, 2);	// 16 byte align the IP header
		memcpy(skb_put(pSkb2, LENGTH_802_3), phdr, LENGTH_802_3);
		memcpy(skb_put(pSkb2, DataSize2), pData2, DataSize2);
		pSkb2->protocol = eth_type_trans(pSkb2, net_dev);
	} else {
		return;
	}

	// allocate a new one and update RxD
	if ((pSkb1 =
	     __dev_alloc_skb(RX_DMA_BUFFER_SIZE, MEM_ALLOC_FLAG)) != NULL) {
		pRxSkb->dev = net_dev;
		pRxSkb->data = pData;
		pRxSkb->len = DataSize1;
		pRxSkb->tail = pRxSkb->data + pRxSkb->len;
		memcpy(skb_push(pRxSkb, LENGTH_802_3), p8023hdr, LENGTH_802_3);

		pRxSkb->protocol = eth_type_trans(pRxSkb, net_dev);

		netif_rx(pRxSkb);
		netif_rx(pSkb2);

		pAd->net_dev->last_rx = jiffies;
		pAd->stats.rx_packets += 2;

		RTMP_SET_PACKET_SOURCE(pSkb1, PKTSRC_DRIVER);
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocSize =
		    RX_DMA_BUFFER_SIZE;
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.pSkb = pSkb1;
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocVa =
		    pSkb1->data;
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocPa =
		    pci_map_single(pAd->pPci_Dev,
				   pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].
				   DmaBuf.AllocVa,
				   pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].
				   DmaBuf.AllocSize, PCI_DMA_FROMDEVICE);

		// Write RxD buffer address & allocated buffer length
		//pRxD = (PRXD_STRUC) pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].AllocVa;
		//pRxD->BufPhyAddr = pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocPa;
	} else {
		RTMP_SET_PACKET_SOURCE(pRxSkb, PKTSRC_DRIVER);
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocSize =
		    RX_DMA_BUFFER_SIZE;
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.pSkb = pRxSkb;
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocVa =
		    pRxSkb->data;
		pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocPa =
		    pci_map_single(pAd->pPci_Dev,
				   pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].
				   DmaBuf.AllocVa,
				   pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].
				   DmaBuf.AllocSize, PCI_DMA_FROMDEVICE);
		dev_kfree_skb_any(pSkb2);
		return;
		// Write RxD buffer address & allocated buffer length
		//pRxD = (PRXD_STRUC) pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].AllocVa;
		//pRxD->BufPhyAddr = pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].DmaBuf.AllocPa;
	}
}

// Enqueue this frame to MLME engine
// We need to enqueue the whole frame because MLME need to pass data type
// information from 802.11 header
#define REPORT_MGMT_FRAME_TO_MLME(_pAd, _pFrame, _FrameSize, _Rssi, _PlcpSignal)        \
{                                                                                       \
    ULONG High32TSF, Low32TSF;                                                          \
    RTMP_IO_READ32(_pAd, TXRX_CSR13, &High32TSF);                                       \
    RTMP_IO_READ32(_pAd, TXRX_CSR12, &Low32TSF);                                        \
    MlmeEnqueueForRecv(_pAd, High32TSF, Low32TSF, (UCHAR)_Rssi, _FrameSize, _pFrame, (UCHAR)_PlcpSignal);   \
}

/*
	========================================================================

	Routine	Description:
		Process	RxDone interrupt, running in DPC level

	Arguments:
		pAd	Pointer	to our adapter

	Return Value:
		None

	Note:
		This routine has to	maintain Rx	ring read pointer.
		Need to consider QOS DATA format when converting to 802.3
	========================================================================
*/
VOID RTMPHandleRxDoneInterrupt(IN PRTMP_ADAPTER pAd)
{
	PRXD_STRUC pRxD;
	USHORT Offset = 0;
#ifdef BIG_ENDIAN
	PRXD_STRUC pDestRxD;
	RXD_STRUC RxD;
#endif
	PHEADER_802_11 pHeader;
	PUCHAR pData;
	USHORT DataSize, Msdu2Size;
	PUCHAR pDA, pSA;
	UCHAR Header802_3[14];
	int Count;
	struct net_device *net_dev = pAd->net_dev;
#if SL_IRQSAVE
	ULONG IrqFlags;
#endif

//      DBGPRINT(RT_DEBUG_INFO, "====> RTMPHandleRxDoneInterrupt\n");
	// Make sure Rx ring resource won't be used by other threads

#if SL_IRQSAVE
	spin_lock_irqsave(&pAd->RxRingLock, IrqFlags);
#else
	spin_lock_bh(&pAd->RxRingLock);
#endif

	for (Count = 0; Count < MAX_RX_PROCESS; Count++) {
		// Point to Rx indexed rx ring descriptor
#ifndef BIG_ENDIAN
		// Point to Rx indexed rx ring descriptor
		pRxD =
		    (PRXD_STRUC) pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].
		    AllocVa;
#else
		pDestRxD =
		    (PRXD_STRUC) pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].
		    AllocVa;
		RxD = *pDestRxD;
		pRxD = &RxD;
		RTMPDescriptorEndianChange((PUCHAR) pRxD, TYPE_RXD);
#endif

		// In case of false alarm or processed at last instance
		if (pRxD->Owner != DESC_OWN_HOST)
			break;

		// start process the received frame. break out this "do {...} while(FALSE)"
		// loop without indication to upper layer or MLME if any error in this
		// received frame.
		do {
			pci_unmap_single(pAd->pPci_Dev,
					 pAd->RxRing.Cell[pAd->RxRing.
							  CurRxIndex].DmaBuf.
					 AllocPa,
					 pAd->RxRing.Cell[pAd->RxRing.
							  CurRxIndex].DmaBuf.
					 AllocSize, PCI_DMA_FROMDEVICE);
			// Point to Rx ring buffer where stores the real data frame
			pData =
			    (PUCHAR) (pAd->RxRing.Cell[pAd->RxRing.CurRxIndex].
				      DmaBuf.AllocVa);
			pHeader = (PHEADER_802_11) pData;

#ifdef BIG_ENDIAN
			RTMPFrameEndianChange(pAd, (PUCHAR) pHeader, DIR_READ,
					      FALSE);
#endif

			if (pAd->PortCfg.BssType == BSS_MONITOR) {
				struct sk_buff *skb;
				wlan_ng_prism2_header *ph;

				if ((skb =
				     __dev_alloc_skb(2048,
						     GFP_DMA | GFP_ATOMIC)) !=
				    NULL) {
					if (pAd->PortCfg.RFMONTX == FALSE) {
						/* Set up the wlan-ng prismheader */
						if (skb_headroom(skb) <
						    sizeof
						    (wlan_ng_prism2_header))
							pskb_expand_head(skb,
									 sizeof
									 (wlan_ng_prism2_header),
									 0,
									 GFP_ATOMIC);

						ph = (wlan_ng_prism2_header *)
						    skb_push(skb,
							     sizeof
							     (wlan_ng_prism2_header));
						memset(ph, 0,
						       sizeof
						       (wlan_ng_prism2_header));

						ph->msgcode =
						    DIDmsg_lnxind_wlansniffrm;
						ph->msglen =
						    sizeof
						    (wlan_ng_prism2_header);
						strcpy(ph->devname,
						       pAd->net_dev->name);

						ph->hosttime.did =
						    DIDmsg_lnxind_wlansniffrm_hosttime;
						ph->hosttime.len = 4;
						ph->hosttime.data = jiffies;

						ph->mactime.did =
						    DIDmsg_lnxind_wlansniffrm_mactime;
						ph->mactime.len = 4;

						ph->channel.did =
						    DIDmsg_lnxind_wlansniffrm_channel;
						ph->channel.len = 4;
						ph->channel.data =
						    pAd->PortCfg.Channel;

						ph->signal.did =
						    DIDmsg_lnxind_wlansniffrm_signal;
						ph->signal.len = 4;
						ph->signal.data =
						    pRxD->PlcpRssi;

						ph->noise.did =
						    DIDmsg_lnxind_wlansniffrm_noise;
						ph->noise.len = 4;
						ph->noise.data =
						    pAd->BbpWriteLatch[17];

						ph->rssi.did =
						    DIDmsg_lnxind_wlansniffrm_rssi;
						ph->rssi.len = 4;
						ph->rssi.data =
						    ph->signal.data -
						    ph->noise.data;

						ph->rate.did =
						    DIDmsg_lnxind_wlansniffrm_rate;
						ph->rate.len = 4;
						if (pRxD->Ofdm == 1) {
							int i;

							for (i = 4; i < 12; i++)
								if (pRxD->
								    PlcpSignal
								    ==
								    RateIdToPlcpSignal
								    [i])
									ph->rate.data = _11G_RATES[i] * 2;
						} else {
							ph->rate.data =
							    pRxD->PlcpSignal /
							    5;
						}

						ph->istx.did =
						    DIDmsg_lnxind_wlansniffrm_istx;
						ph->istx.len = 4;

						ph->frmlen.did =
						    DIDmsg_lnxind_wlansniffrm_frmlen;
						ph->frmlen.len = 4;
						ph->frmlen.data =
						    pRxD->DataByteCnt;
						/* end prismheader setup */
					}

					skb->dev = pAd->net_dev;
					memcpy(skb_put(skb, pRxD->DataByteCnt),
					       pData, pRxD->DataByteCnt);
					skb->mac.raw = skb->data;
					skb->pkt_type = PACKET_OTHERHOST;
					skb->protocol = htons(ETH_P_802_2);
					skb->ip_summed = CHECKSUM_NONE;
					netif_rx(skb);
				}
				break;
			}
			// Increase Total receive byte counter after real data received no mater any error or not
			pAd->RalinkCounters.ReceivedByteCount +=
			    pRxD->DataByteCnt;
			pAd->RalinkCounters.RxCount++;

			// Check for all RxD errors
			if (RTMPCheckRxError(pAd, pHeader, pRxD) !=
			    NDIS_STATUS_SUCCESS) {
				DBGPRINT(RT_DEBUG_INFO,
					 "RxDone - drop error frame (len=%d)\n",
					 pRxD->DataByteCnt);
				pAd->Counters8023.RxErrors++;
				break;	// give up this frame
			}
			// Apply packet filtering rule based on microsoft requirements.
			if (RTMPApplyPacketFilter(pAd, pRxD, pHeader) !=
			    NDIS_STATUS_SUCCESS) {
				DBGPRINT(RT_DEBUG_INFO,
					 "RxDone - frame filtered out (len=%d)\n",
					 pRxD->DataByteCnt);
				pAd->Counters8023.RxErrors++;
				break;	// give up this frame
			}
			// Increase 802.11 counters & general receive counters
			INC_COUNTER64(pAd->WlanCounters.ReceivedFragmentCount);
			pAd->RalinkCounters.OneSecRxOkCnt++;

#ifdef RALINK_ATE
			if ((!pRxD->U2M) && pAd->ate.Mode != ATE_STASTART) {
				CHAR RealRssi;

				RealRssi =
				    ConvertToRssi(pAd, (UCHAR) pRxD->PlcpRssi,
						  RSSI_NO_1);
				pAd->PortCfg.LastRssi =
				    RealRssi + pAd->BbpRssiToDbmDelta;
				pAd->PortCfg.AvgRssiX8 =
				    (pAd->PortCfg.AvgRssiX8 -
				     pAd->PortCfg.AvgRssi) +
				    pAd->PortCfg.LastRssi;
				pAd->PortCfg.AvgRssi =
				    pAd->PortCfg.AvgRssiX8 >> 3;

				if ((pAd->RfIcType == RFIC_5325)
				    || (pAd->RfIcType == RFIC_2529)) {
					pAd->PortCfg.LastRssi2 =
					    ConvertToRssi(pAd,
							  (UCHAR) pRxD->
							  PlcpSignal,
							  RSSI_NO_2) +
					    pAd->BbpRssiToDbmDelta;
				}
			}
#endif				// RALINK_ATE

			// DUPLICATE FRAME CHECK:
			//   Check retry bit. If this bit is on, search the cache with SA & sequence
			//   as index, if matched, discard this frame, otherwise, update cache
			//   This check only apply to unicast data & management frames
			if ((pRxD->U2M) &&
			    (pHeader->FC.Retry) &&
			    (RTMPSearchTupleCache(pAd, pHeader) == TRUE)) {
				DBGPRINT(RT_DEBUG_INFO,
					 "RxDone- drop DUPLICATE frame(len=%d)\n",
					 pRxD->DataByteCnt);
				INC_COUNTER64(pAd->WlanCounters.
					      FrameDuplicateCount);
				break;	// give up this frame
			} else	// Update Tuple Cache
				RTMPUpdateTupleCache(pAd, pHeader);

			if ((pRxD->U2M)
			    || ((pHeader->FC.SubType == SUBTYPE_BEACON)
				&&
				(memcmp
				 (&pAd->PortCfg.Bssid, &pHeader->Addr2,
				  ETH_ALEN) == 0))) {
				if (((pAd->Antenna.field.NumOfAntenna == 0)
				     && (pAd->NicConfig2.field.
					 Enable4AntDiversity == 1))
				    || ((pAd->Antenna.field.NumOfAntenna == 2)
					&& (pAd->Antenna.field.
					    TxDefaultAntenna == 0)
					&& (pAd->Antenna.field.
					    RxDefaultAntenna == 0))) {
					if (pAd->RxAnt.EvaluatePeriod != 2) {
						COLLECT_RX_ANTENNA_AVERAGE_RSSI
						    (pAd,
						     ConvertToRssi(pAd,
								   (UCHAR)
								   pRxD->
								   PlcpRssi,
								   RSSI_NO_1),
						     ConvertToRssi(pAd,
								   (UCHAR)
								   pRxD->
								   PlcpSignal,
								   RSSI_NO_2));
					}
					pAd->PortCfg.NumOfAvgRssiSample++;
				}
			}
#ifdef RALINK_ATE
			if (pAd->ate.Mode != ATE_STASTART) {
				break;
			}
#endif

#if 0
			// TODO: to be removed
			if (pRxD->CipherAlg) {
				int i;
				PUCHAR ptr = (PUCHAR) pHeader;
				DBGPRINT_RAW(RT_DEBUG_TRACE,
					     "%s decrypt with Key#%d Len=%d\n",
					     CipherName[pRxD->CipherAlg],
					     pRxD->KeyIndex, pRxD->DataByteCnt);
				for (i = 0; i < 64; i += 16) {
					DBGPRINT_RAW(RT_DEBUG_TRACE,
						     "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x - %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
						     *ptr, *(ptr + 1),
						     *(ptr + 2), *(ptr + 3),
						     *(ptr + 4), *(ptr + 5),
						     *(ptr + 6), *(ptr + 7),
						     *(ptr + 8), *(ptr + 9),
						     *(ptr + 10), *(ptr + 11),
						     *(ptr + 12), *(ptr + 13),
						     *(ptr + 14), *(ptr + 15));
					ptr += 16;
				}
			}
#endif
			// pData : Pointer skip the     first 24 bytes, 802.11 HEADER
			pData += LENGTH_802_11;
			DataSize = (USHORT) pRxD->DataByteCnt - LENGTH_802_11;

			//
			// CASE I. receive a DATA frame
			//
			if (pHeader->FC.Type == BTYPE_DATA) {
				// before LINK UP, all DATA frames are rejected
				if (!OPSTATUS_TEST_FLAG
				    (pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)) {
					DBGPRINT(RT_DEBUG_TRACE,
						 "RxDone- drop DATA frame before LINK UP(len=%d)\n",
						 pRxD->DataByteCnt);
					// TODO: dis-associate the AP if U2M? class3 error. better no. will cause trouble when reconnecting to same AP
					break;
				}
				// Drop not my BSS frame
				if (pRxD->MyBss == 0)
					break;	// give up this frame

				// Drop NULL (+CF-POLL) (+CF-ACK) data frame
				if ((pHeader->FC.SubType & 0x04) == 0x04) {
					DBGPRINT(RT_DEBUG_TRACE,
						 "RxDone- drop NULL frame(subtype=%d)\n",
						 pHeader->FC.SubType);
					break;	// give up this frame
				}
				// remove the 2 extra QOS CNTL bytes
				if (pHeader->FC.SubType & 0x08) {
					pData += 2;
					DataSize -= 2;
					Offset += 2;
				}
				// remove the 2 extra AGGREGATION bytes
				Msdu2Size = 0;
				if (pHeader->FC.Order) {
					Msdu2Size =
					    *pData + (*(pData + 1) << 8);
					if ((Msdu2Size <= 1536)
					    && (Msdu2Size < DataSize)) {
						pData += 2;
						DataSize -= 2;
					} else
						Msdu2Size = 0;
				}
				// prepare 802.3 header: DA=addr1; SA=addr3 in INFRA mode, DA=addr2 in ADHOC mode
				pDA = pHeader->Addr1;
				if (INFRA_ON(pAd))
					pSA = pHeader->Addr3;
				else
					pSA = pHeader->Addr2;

				if (pHeader->FC.Wep)	// frame received in encrypted format
				{
					if (pRxD->CipherAlg == CIPHER_NONE)	// unsupported cipher suite
						break;	// give up this frame

				} else	// frame received in clear text
				{
					// encryption in-use but receive a non-EAPOL clear text frame, drop it
					if (((pAd->PortCfg.WepStatus ==
					      Ndis802_11Encryption1Enabled)
					     || (pAd->PortCfg.WepStatus ==
						 Ndis802_11Encryption2Enabled)
					     || (pAd->PortCfg.WepStatus ==
						 Ndis802_11Encryption3Enabled))
					    && (pAd->PortCfg.PrivacyFilter ==
						Ndis802_11PrivFilter8021xWEP)
					    &&
					    (memcmp
					     (EAPOL_LLC_SNAP, pData,
					      LENGTH_802_1_H) != 0))
						break;	// give up this frame
				}

				//
				// Case I.1  Process Broadcast & Multicast data frame
				//
				if (pRxD->Bcast || pRxD->Mcast) {
					PUCHAR pRemovedLLCSNAP;

					INC_COUNTER64(pAd->WlanCounters.
						      MulticastReceivedFrameCount);

					// Drop Mcast/Bcast frame with fragment bit on
					if (pHeader->FC.MoreFrag)
						break;	// give up this frame

					// Filter out Bcast frame which AP relayed for us
					if (pHeader->FC.FrDs
					    &&
					    (memcmp
					     (pHeader->Addr3,
					      pAd->CurrentAddress,
					      ETH_ALEN) == 0))
						break;	// give up this frame

					// build 802.3 header and decide if remove the 8-byte LLC/SNAP encapsulation
					CONVERT_TO_802_3(Header802_3, pDA, pSA,
							 pData, DataSize,
							 pRemovedLLCSNAP);
					REPORT_ETHERNET_FRAME_TO_LLC(pAd,
								     Header802_3,
								     pData,
								     DataSize,
								     net_dev);
//                                      DBGPRINT_RAW(RT_DEBUG_TRACE, "!!! report BCAST DATA to LLC (len=%d) !!!\n", DataSize);
				}
				//
				// Case I.2  Process unicast-to-me DATA frame
				//
				else if (pRxD->U2M
					 || pAd->bAcceptPromiscuous == TRUE) {
// TODO: temp removed
//                                      // Send PS-Poll for AP to send next data frame
//                                      if ((pHeader->FC.MoreData) && INFRA_ON(pAd) && (pAd->PortCfg.Psm == PWR_SAVE))
//                                              EnqueuePsPoll(pAd);

					pAd->RalinkCounters.OneSecRxOkDataCnt++;

					// Update Rx data rate for OID query
					if (pAd->Mlme.bTxRateReportPeriod) {
						RECORD_LATEST_RX_DATA_RATE(pAd,
									   pRxD);
					}
//#ifdef WPA_SUPPLICANT_SUPPORT
					if (pAd->PortCfg.WPA_Supplicant == TRUE) {
						// All EAPoL frames have to pass to upper layer (ex. WPA_SUPPLICANT daemon)
						// TBD : process fragmented EAPol frames
						if (memcmp
						    (EAPOL_LLC_SNAP, pData,
						     LENGTH_802_1_H) == 0) {
							PUCHAR pRemovedLLCSNAP;
							int success = 0;

							// In 802.1x mode, if the received frame is EAP-SUCCESS packet, turn on the PortSecured variable
							if (pAd->PortCfg.
							    IEEE8021X == TRUE
							    && (EAP_CODE_SUCCESS
								==
								RTMPCheckWPAframeForEapCode
								(pAd, pData,
								 DataSize,
								 LENGTH_802_1_H)))
							{
								DBGPRINT_RAW
								    (RT_DEBUG_TRACE,
								     "Receive EAP-SUCCESS Packet\n");
								pAd->PortCfg.
								    PortSecured
								    =
								    WPA_802_1X_PORT_SECURED;

								success = 1;
							}
							// build 802.3 header and decide if remove the 8-byte LLC/SNAP encapsulation
							CONVERT_TO_802_3
							    (Header802_3, pDA,
							     pSA, pData,
							     DataSize,
							     pRemovedLLCSNAP);
							REPORT_ETHERNET_FRAME_TO_LLC
							    (pAd, Header802_3,
							     pData, DataSize,
							     net_dev);
							DBGPRINT_RAW
							    (RT_DEBUG_TRACE,
							     "!!! report EAPoL DATA to LLC (len=%d) !!!\n",
							     DataSize);

							if (success) {
								// For static wep mode, need to set wep key to Asic again
								if (pAd->
								    PortCfg.
								    IEEE8021x_required_keys
								    == 0) {
									int idx;

									idx =
									    pAd->
									    PortCfg.
									    DefaultKeyId;
									//for (idx=0; idx < 4; idx++)
									{
										DBGPRINT_RAW
										    (RT_DEBUG_TRACE,
										     "Set WEP key to Asic again =>\n");

										if (pAd->PortCfg.DesireSharedKey[idx].KeyLen != 0) {
											pAd->
											    SharedKey
											    [idx].
											    KeyLen
											    =
											    pAd->
											    PortCfg.
											    DesireSharedKey
											    [idx].
											    KeyLen;
											memcpy
											    (pAd->
											     SharedKey
											     [idx].
											     Key,
											     pAd->
											     PortCfg.
											     DesireSharedKey
											     [idx].
											     Key,
											     pAd->
											     SharedKey
											     [idx].
											     KeyLen);

											pAd->
											    SharedKey
											    [idx].
											    CipherAlg
											    =
											    pAd->
											    PortCfg.
											    DesireSharedKey
											    [idx].
											    CipherAlg;

											AsicAddSharedKeyEntry
											    (pAd,
											     0,
											     (UCHAR)
											     idx,
											     pAd->
											     SharedKey
											     [idx].
											     CipherAlg,
											     pAd->
											     SharedKey
											     [idx].
											     Key,
											     NULL,
											     NULL);
										}
									}
								}
							}

							break;
						}
					} else {
//#else
						// Special DATA frame that has to pass to MLME
						// EAPOL handshaking frames when driver supplicant enabled, pass to MLME for special process
						if ((memcmp
						     (EAPOL_LLC_SNAP, pData,
						      LENGTH_802_1_H) == 0)
						    && (pAd->PortCfg.WpaState !=
							SS_NOTUSE)) {
							DataSize +=
							    LENGTH_802_11 +
							    Offset;
							REPORT_MGMT_FRAME_TO_MLME
							    (pAd, pHeader,
							     DataSize,
							     pRxD->PlcpRssi,
							     pRxD->PlcpSignal);
							DBGPRINT_RAW
							    (RT_DEBUG_TRACE,
							     "!!! report EAPOL DATA to MLME (len=%d) !!!\n",
							     DataSize);
							break;	// end of processing this frame
						}
//#endif
					}

					if (pHeader->Frag == 0)	// First or Only fragment
					{
						PUCHAR pRemovedLLCSNAP;

						CONVERT_TO_802_3(Header802_3,
								 pDA, pSA,
								 pData,
								 DataSize,
								 pRemovedLLCSNAP);
						pAd->FragFrame.Flags &=
						    0xFFFFFFFE;

						// Firt Fragment & LLC/SNAP been removed. Keep the removed LLC/SNAP for later on
						// TKIP MIC verification.
						if (pHeader->FC.MoreFrag
						    && pRemovedLLCSNAP) {
							memcpy(pAd->FragFrame.
							       Header_LLC,
							       pRemovedLLCSNAP,
							       LENGTH_802_1_H);
							//DataSize -= LENGTH_802_1_H;
							//pData += LENGTH_802_1_H;
							pAd->FragFrame.Flags |=
							    0x01;
						}
						// One & The only fragment
						if (pHeader->FC.MoreFrag ==
						    FALSE) {
							if ((pHeader->FC.Order == 1) && (Msdu2Size > 0))	// this is an aggregation
							{
								USHORT
								    Payload1Size,
								    Payload2Size;
#ifndef NONCOPY_RX
								PUCHAR pData2;
#endif

								pAd->
								    RalinkCounters.
								    OneSecRxAggregationCount++;
								Payload1Size =
								    DataSize -
								    Msdu2Size;
								Payload2Size =
								    Msdu2Size -
								    LENGTH_802_3;
#ifndef NONCOPY_RX
								REPORT_ETHERNET_FRAME_TO_LLC
								    (pAd,
								     Header802_3,
								     pData,
								     Payload1Size,
								     net_dev);
								DBGPRINT_RAW
								    (RT_DEBUG_INFO,
								     "!!! report agregated MSDU1 to LLC (len=%d, proto=%02x:%02x) %02x:%02x:%02x:%02x-%02x:%02x:%02x:%02x\n",
								     LENGTH_802_3
								     +
								     Payload1Size,
								     Header802_3
								     [12],
								     Header802_3
								     [13],
								     *pData,
								     *(pData +
								       1),
								     *(pData +
								       2),
								     *(pData +
								       3),
								     *(pData +
								       4),
								     *(pData +
								       5),
								     *(pData +
								       6),
								     *(pData +
								       7));

								pData2 =
								    pData +
								    Payload1Size
								    +
								    LENGTH_802_3;
								REPORT_ETHERNET_FRAME_TO_LLC
								    (pAd,
								     pData +
								     Payload1Size,
								     pData2,
								     Payload2Size,
								     net_dev);
								DBGPRINT_RAW
								    (RT_DEBUG_INFO,
								     "!!! report agregated MSDU2 to LLC (len=%d, proto=%02x:%02x) %02x:%02x:%02x:%02x-%02x:%02x:%02x:%02x\n",
								     LENGTH_802_3
								     +
								     Payload2Size,
								     *(pData2 -
								       2),
								     *(pData2 -
								       1),
								     *pData2,
								     *(pData2 +
								       1),
								     *(pData2 +
								       2),
								     *(pData2 +
								       3),
								     *(pData2 +
								       4),
								     *(pData2 +
								       5),
								     *(pData2 +
								       6),
								     *(pData2 +
								       7));
#else
								REPORT_AGGREGATE_ETHERNET_FRAME_TO_LLC_WITH_NON_COPY
								    (pAd,
								     Header802_3,
								     pData,
								     Payload1Size,
								     Payload2Size,
								     net_dev);
								pRxD->
								    BufPhyAddr =
								    pAd->RxRing.
								    Cell[pAd->
									 RxRing.
									 CurRxIndex].
								    DmaBuf.
								    AllocPa;
#endif
							} else {
#if 0
								REPORT_ETHERNET_FRAME_TO_LLC
								    (pAd,
								     Header802_3,
								     pData,
								     DataSize,
								     net_dev);

#endif

								REPORT_ETHERNET_FRAME_TO_LLC_WITH_NON_COPY
								    (pAd,
								     Header802_3,
								     pData,
								     DataSize,
								     net_dev);
#ifdef NONCOPY_RX
								pRxD->
								    BufPhyAddr =
								    pAd->RxRing.
								    Cell[pAd->
									 RxRing.
									 CurRxIndex].
								    DmaBuf.
								    AllocPa;
#endif

								DBGPRINT_RAW
								    (RT_DEBUG_INFO,
								     "!!! report DATA (no frag) to LLC (len=%d, proto=%02x:%02x) %02x:%02x:%02x:%02x-%02x:%02x:%02x:%02x\n",
								     DataSize,
								     Header802_3
								     [12],
								     Header802_3
								     [13],
								     *pData,
								     *(pData +
								       1),
								     *(pData +
								       2),
								     *(pData +
								       3),
								     *(pData +
								       4),
								     *(pData +
								       5),
								     *(pData +
								       6),
								     *(pData +
								       7));
							}
						}
						// First fragment - record the 802.3 header and frame body
						else {
							memcpy(&pAd->FragFrame.
							       Buffer
							       [LENGTH_802_3],
							       pData, DataSize);
							memcpy(pAd->FragFrame.
							       Header802_3,
							       Header802_3,
							       LENGTH_802_3);
							pAd->FragFrame.RxSize =
							    DataSize;
							pAd->FragFrame.
							    Sequence =
							    pHeader->Sequence;
							pAd->FragFrame.LastFrag = pHeader->Frag;	// Should be 0
						}
					}
					// Middle & End of fragment burst fragments
					else {
						// No LLC-SNAP header in except the first fragment frame

						if ((pHeader->Sequence !=
						     pAd->FragFrame.Sequence)
						    || (pHeader->Frag !=
							(pAd->FragFrame.
							 LastFrag + 1))) {
							// Fragment is not the same sequence or out of fragment number order
							// Clear Fragment frame contents
							memset(&pAd->FragFrame,
							       0,
							       sizeof
							       (FRAGMENT_FRAME));
							break;	// give up this frame
						} else
						    if ((pAd->FragFrame.RxSize +
							 DataSize) >
							MAX_FRAME_SIZE) {
							// Fragment frame is too large, it exeeds the maximum frame size.
							// Clear Fragment frame contents
							memset(&pAd->FragFrame,
							       0,
							       sizeof
							       (FRAGMENT_FRAME));
							break;	// give up this frame
						}
						// concatenate this fragment into the re-assembly buffer
						memcpy(&pAd->FragFrame.
						       Buffer[LENGTH_802_3 +
							      pAd->FragFrame.
							      RxSize], pData,
						       DataSize);
						pAd->FragFrame.RxSize +=
						    DataSize;
						pAd->FragFrame.LastFrag = pHeader->Frag;	// Update fragment number

						// Last fragment
						if (pHeader->FC.MoreFrag ==
						    FALSE) {
							// For TKIP frame, calculate the MIC value
							if (pRxD->CipherAlg ==
							    CIPHER_TKIP) {
								PCIPHER_KEY
								    pWpaKey =
								    &pAd->
								    SharedKey
								    [pRxD->
								     KeyIndex];

								// Minus MIC length
								pAd->FragFrame.
								    RxSize -= 8;

								if (pAd->
								    FragFrame.
								    Flags &
								    0x00000001)
								{
									// originally there's an LLC/SNAP field in the first fragment
									// but been removed in re-assembly buffer. here we have to include
									// this LLC/SNAP field upon calculating TKIP MIC
									// pData = pAd->FragFrame.Header_LLC;
									// Copy LLC data to the position in front of real data for MIC calculation
									memcpy
									    (&pAd->
									     FragFrame.
									     Buffer
									     [LENGTH_802_3
									      -
									      LENGTH_802_1_H],
									     pAd->
									     FragFrame.
									     Header_LLC,
									     LENGTH_802_1_H);
									pData =
									    (PUCHAR)
									    &
									    pAd->
									    FragFrame.
									    Buffer
									    [LENGTH_802_3
									     -
									     LENGTH_802_1_H];
									DataSize
									    =
									    (USHORT)
									    pAd->
									    FragFrame.
									    RxSize
									    +
									    LENGTH_802_1_H;
								} else {
									pData =
									    (PUCHAR)
									    &
									    pAd->
									    FragFrame.
									    Buffer
									    [LENGTH_802_3];
									DataSize
									    =
									    (USHORT)
									    pAd->
									    FragFrame.
									    RxSize;
								}

								if (RTMPTkipCompareMICValue(pAd, pData, pDA, pSA, pWpaKey->RxMic, DataSize) == FALSE) {
									DBGPRINT_RAW
									    (RT_DEBUG_ERROR,
									     "Rx MIC Value error 2\n");
									RTMPReportMicError
									    (pAd,
									     pWpaKey);
									break;	// give up this frame
								}

							}

							pData =
							    &pAd->FragFrame.
							    Buffer
							    [LENGTH_802_3];
							REPORT_ETHERNET_FRAME_TO_LLC
							    (pAd,
							     pAd->FragFrame.
							     Header802_3, pData,
							     pAd->FragFrame.
							     RxSize, net_dev);
//                                                      DBGPRINT_RAW(RT_DEBUG_TRACE, "!!! report DATA (fragmented) to LLC (len=%d) !!!\n", pAd->FragFrame.RxSize);

							// Clear Fragment frame contents
							memset(&pAd->FragFrame,
							       0,
							       sizeof
							       (FRAGMENT_FRAME));
						}
					}
				}

			}
			// CASE II. receive a MGMT frame
			//
			else if (pHeader->FC.Type == BTYPE_MGMT) {
				REPORT_MGMT_FRAME_TO_MLME(pAd, pHeader,
							  pRxD->DataByteCnt,
							  pRxD->PlcpRssi,
							  pRxD->PlcpSignal);
				break;	// end of processing this frame
			}
			// CASE III. receive a CNTL frame
			//
			else if (pHeader->FC.Type == BTYPE_CNTL)
				break;	// give up this frame

			// CASE IV. receive a frame of invalid type
			else
				break;	// give up this frame

		} while (FALSE);	// ************* exit point *********

		// release this RXD for ASIC to use
		pRxD->Owner = DESC_OWN_NIC;
#ifdef BIG_ENDIAN
		RTMPDescriptorEndianChange((PUCHAR) pRxD, TYPE_RXD);
		WriteBackToDescriptor((PUCHAR) pDestRxD, (PUCHAR) pRxD, FALSE,
				      TYPE_RXD);
#endif
		// advance to next RX frame
		INC_RING_INDEX(pAd->RxRing.CurRxIndex, RX_RING_SIZE);

	}

	// Make sure to release Rx ring resource
#if SL_IRQSAVE
	spin_unlock_irqrestore(&pAd->RxRingLock, (unsigned long)IrqFlags);
#else
	spin_unlock_bh(&pAd->RxRingLock);
#endif

}

/*
	========================================================================

	Routine	Description:
		Process	TxDone interrupt, running in	DPC	level

	Arguments:
		Adapter		Pointer	to our adapter

	Return Value:
		None

	Note:

	========================================================================
*/
VOID RTMPHandleTxDoneInterrupt(IN PRTMP_ADAPTER pAd)
{
	STA_CSR4_STRUC csr;
	int i;
	UCHAR UpRate;

//      DBGPRINT(RT_DEBUG_INFO, "====> RTMPHandleTxDoneInterrupt\n");
	for (i = 0; i < MAX_TX_DONE_PROCESS; i++) {
		RTMP_IO_READ32(pAd, STA_CSR4, &csr.word);
		if (!csr.field.bValid)
			break;

		pAd->RalinkCounters.OneSecTxDoneCount++;
		switch (csr.field.TxResult) {
		case TX_RESULT_SUCCESS:	// Success with or without retry
			// DBGPRINT(RT_DEBUG_INFO, "TX Success without retry<<<\n");
			INC_COUNTER64(pAd->WlanCounters.
				      TransmittedFragmentCount);
			pAd->Counters8023.GoodTransmits++;

			if ((csr.field.PidType == (PTYPE_SPECIAL >> 6))
			    && (csr.field.PidSubtype == PSUBTYPE_RTS))
				INC_COUNTER64(pAd->WlanCounters.
					      RTSSuccessCount);

			// case 1. no retry
			if (csr.field.RetryCount == 0) {
				// update DRS related counters
				if (csr.field.PidType ==
				    (PTYPE_DATA_REQUIRE_ACK >> 6)) {
					pAd->RalinkCounters.
					    OneSecTxNoRetryOkCount++;
				}
			} else {
				// case 2. one or multiple retry
				INC_COUNTER64(pAd->WlanCounters.RetryCount);
				INC_COUNTER64(pAd->WlanCounters.
					      ACKFailureCount);
				if (csr.field.RetryCount > 1) {
					INC_COUNTER64(pAd->WlanCounters.
						      MultipleRetryCount);
					pAd->Counters8023.MoreCollisions++;
				} else
					pAd->Counters8023.OneCollision++;

				// update DRS related counters
				if (csr.field.PidType ==
				    (PTYPE_NULL_AT_HIGH_RATE >> 6)) {
					// DRS - must be NULL frame retried @ UpRate; downgrade
					//       TxQuality[UpRate] so that not upgrade TX rate
					if (csr.field.PidSubtype == 0)	// operate in STA mode
					{
						UpRate =
						    Phy11BGNextRateUpward[pAd->
									  PortCfg.
									  TxRate];
						pAd->DrsCounters.
						    TxQuality[UpRate] += 2;
						if (pAd->DrsCounters.
						    TxQuality[UpRate] >
						    DRS_TX_QUALITY_WORST_BOUND)
							pAd->DrsCounters.
							    TxQuality[UpRate] =
							    DRS_TX_QUALITY_WORST_BOUND;
					}
				} else if (csr.field.PidType ==
					   (PTYPE_DATA_REQUIRE_ACK >> 6))
					pAd->RalinkCounters.
					    OneSecTxRetryOkCount++;
			}
			break;

		case TX_RESULT_RETRY_FAIL:	// Fail on hitting retry count limit
			DBGPRINT(RT_DEBUG_WARN, "TX Failed (RETRY LIMIT)<<<\n");
			INC_COUNTER64(pAd->WlanCounters.FailedCount);
			INC_COUNTER64(pAd->WlanCounters.ACKFailureCount);
			pAd->Counters8023.TxErrors++;
			if ((csr.field.PidType == (PTYPE_SPECIAL >> 6))
			    && (csr.field.PidSubtype == PSUBTYPE_RTS))
				INC_COUNTER64(pAd->WlanCounters.
					      RTSFailureCount);

			// update DRS related counters
			if (csr.field.PidType == (PTYPE_NULL_AT_HIGH_RATE >> 6)) {
				// DRS - must be NULL frame failed @ UpRate; downgrade
				//       TxQuality[UpRate] so that not upgrade TX rate
				if (csr.field.PidSubtype == 0)	// operate in STA mode
				{
					UpRate =
					    Phy11BGNextRateUpward[pAd->PortCfg.
								  TxRate];
					pAd->DrsCounters.TxQuality[UpRate] =
					    DRS_TX_QUALITY_WORST_BOUND;
				}
			} else if (csr.field.PidType ==
				   (PTYPE_DATA_REQUIRE_ACK >> 6))
				pAd->RalinkCounters.OneSecTxFailCount++;

			break;

		default:
			DBGPRINT(RT_DEBUG_ERROR, "TX Failed (%04x)<<<\n",
				 csr.word);
			INC_COUNTER64(pAd->WlanCounters.FailedCount);
			INC_COUNTER64(pAd->WlanCounters.ACKFailureCount);
			pAd->Counters8023.TxErrors++;
			break;
		}
	}
}

/*
	========================================================================

	Routine	Description:
		Process	TX Rings DMA Done interrupt, running in	DPC	level

	Arguments:
		Adapter		Pointer	to our adapter

	Return Value:
		None

	========================================================================
*/
VOID RTMPHandleTxRingDmaDoneInterrupt(IN PRTMP_ADAPTER pAdapter,
				      IN INT_SOURCE_CSR_STRUC TxRingBitmap)
{
#if SL_IRQSAVE
	ULONG IrqFlags;
#endif
	UCHAR Index;
	// Make sure Tx ring resource won't be used by other threads
#if SL_IRQSAVE
	spin_lock_irqsave(&pAdapter->TxRingLock, IrqFlags);
#else
	spin_lock_bh(&pAdapter->TxRingLock);
#endif

	if (TxRingBitmap.field.Ac0DmaDone)
		RTMPFreeTXDUponTxDmaDone(pAdapter, QID_AC_BE, TX_RING_SIZE);

	if (TxRingBitmap.field.HccaDmaDone)
		RTMPFreeTXDUponTxDmaDone(pAdapter, QID_HCCA, TX_RING_SIZE);

	if (TxRingBitmap.field.Ac3DmaDone)
		RTMPFreeTXDUponTxDmaDone(pAdapter, QID_AC_VO, TX_RING_SIZE);

	if (TxRingBitmap.field.Ac2DmaDone)
		RTMPFreeTXDUponTxDmaDone(pAdapter, QID_AC_VI, TX_RING_SIZE);

	if (TxRingBitmap.field.Ac1DmaDone)
		RTMPFreeTXDUponTxDmaDone(pAdapter, QID_AC_BK, TX_RING_SIZE);

#ifdef RALINK_ATE
	if (pAdapter->ate.Mode == ATE_TXFRAME) {
		if (pAdapter->ate.TxDoneCount < pAdapter->ate.TxCount) {
			PTXD_STRUC pTxD;
			PRTMP_TX_RING pTxRing = &pAdapter->TxRing[QID_AC_BE];
			PRTMP_DMABUF pDmaBuf =
			    &pAdapter->TxRing[QID_AC_BE].Cell[pTxRing->
							      CurTxIndex].
			    DmaBuf;

			pAdapter->ate.TxDoneCount++;

			pTxD =
			    (PTXD_STRUC) pAdapter->TxRing[QID_AC_BE].
			    Cell[pTxRing->CurTxIndex].AllocVa;

			pTxRing->Cell[pTxRing->CurTxIndex].pSkb = pDmaBuf->pSkb;
			pTxRing->Cell[pTxRing->CurTxIndex].pNextSkb = NULL;
			RTMPWriteTxDescriptor(pAdapter, pTxD, CIPHER_NONE, 0, 0,
					      FALSE, FALSE, FALSE, SHORT_RETRY,
					      IFS_BACKOFF, pAdapter->ate.TxRate,
					      pAdapter->ate.TxLength, QID_AC_BE,
					      0);

			INC_RING_INDEX(pTxRing->CurTxIndex, TX_RING_SIZE);

			RTMP_IO_WRITE32(pAdapter, TX_CNTL_CSR, BIT8[QID_AC_BE]);
		} else {
			DBGPRINT(RT_DEBUG_INFO, "ATE TXFRAME completed!\n");
		}
	}
#endif				// RALINK_ATE

	// Make sure to release Tx ring resource
#if SL_IRQSAVE
	spin_unlock_irqrestore(&pAdapter->TxRingLock, (unsigned long)IrqFlags);
#else
	spin_unlock_bh(&pAdapter->TxRingLock);
#endif

	// Dequeue one frame from TxSwQueue[] and process it
//      if ((!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) &&
//              (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_RESET_IN_PROGRESS)) &&
//              (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_HALT_IN_PROGRESS)))
	{
		// let the subroutine select the right priority Tx software queue
		for (Index = 0; Index < 5; Index++) {
			if (!skb_queue_empty(&pAdapter->TxSwQueue[Index]))
				RTMPDeQueuePacket(pAdapter, Index);
		}
	}
}

/*
	========================================================================

	Routine	Description:
		Process	MGMT ring DMA done interrupt, running in DPC level

	Arguments:
		pAdapter		Pointer	to our adapter

	Return Value:
		None

	Note:

	========================================================================
*/
VOID RTMPHandleMgmtRingDmaDoneInterrupt(IN PRTMP_ADAPTER pAdapter)
{
	PTXD_STRUC pTxD;
#ifdef BIG_ENDIAN
	PTXD_STRUC pDestTxD;
	TXD_STRUC TxD;
#endif
	struct sk_buff *pSkb;
	int i;
#if SL_IRQSAVE
	ULONG IrqFlags;
#endif

	DBGPRINT(RT_DEBUG_INFO, "RTMPHandleMgmtRingDmaDoneInterrupt...\n");

#if SL_IRQSAVE
	spin_lock_irqsave(&pAdapter->MgmtRingLock, IrqFlags);
#else
	spin_lock_bh(&pAdapter->MgmtRingLock);
#endif

	for (i = 0; i < MGMT_RING_SIZE; i++) {

#ifndef BIG_ENDIAN
		pTxD =
		    (PTXD_STRUC) (pAdapter->MgmtRing.
				  Cell[pAdapter->MgmtRing.NextTxDmaDoneIndex].
				  AllocVa);
#else
		pDestTxD =
		    (PTXD_STRUC) (pAdapter->MgmtRing.
				  Cell[pAdapter->MgmtRing.NextTxDmaDoneIndex].
				  AllocVa);
		TxD = *pDestTxD;
		pTxD = &TxD;
		RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif				/* BIG_ENDIAN */

		if ((pTxD->Owner == DESC_OWN_NIC)
		    || (pTxD->bWaitingDmaDoneInt == 0))
			break;

		pci_unmap_single(pAdapter->pPci_Dev, pTxD->BufPhyAddr0,
				 pTxD->BufLen0, PCI_DMA_TODEVICE);

		pSkb =
		    pAdapter->MgmtRing.Cell[pAdapter->MgmtRing.
					    NextTxDmaDoneIndex].pSkb;
		if (pSkb) {
			pAdapter->MgmtRing.Cell[pAdapter->MgmtRing.
						NextTxDmaDoneIndex].pSkb = NULL;
			RELEASE_NDIS_PACKET(pAdapter, pSkb);
		}

		pTxD->bWaitingDmaDoneInt = 0;
		INC_RING_INDEX(pAdapter->MgmtRing.NextTxDmaDoneIndex,
			       MGMT_RING_SIZE);
//      pAdapter->RalinkCounters.TransmittedByteCount +=  pTxD->DataByteCnt;

#ifdef BIG_ENDIAN
		RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
		WriteBackToDescriptor((PUCHAR) pDestTxD, (PUCHAR) pTxD, TRUE,
				      TYPE_TXD);
#endif

	}

#if SL_IRQSAVE
	spin_unlock_irqrestore(&pAdapter->MgmtRingLock,
			       (unsigned long)IrqFlags);
#else
	spin_unlock_bh(&pAdapter->MgmtRingLock);
#endif

}

/*
	========================================================================

	Routine	Description:
	Arguments:
		Adapter		Pointer	to our adapter

	========================================================================
*/
VOID RTMPHandleTBTTInterrupt(IN PRTMP_ADAPTER pAd)
{
	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
		DBGPRINT(RT_DEBUG_TRACE, "RTMPHandleTBTTInterrupt...\n");
}

/*
	========================================================================

	Routine	Description:
	Arguments:
		pAd		Pointer	to our adapter

	========================================================================
*/
VOID RTMPHandleTwakeupInterrupt(IN PRTMP_ADAPTER pAd)
{
	DBGPRINT(RT_DEBUG_INFO, "Twakeup Expired... !!!\n");
	AsicForceWakeup(pAd);
}

/*
	========================================================================

	Routine	Description:
		API for MLME to transmit management frame to AP (BSS Mode)
	or station (IBSS Mode)

	Arguments:
		pAdapter	Pointer	to our adapter
		pData		Pointer to the outgoing 802.11 frame
		Length		Size of outgoing management frame

	Return Value:
		NDIS_STATUS_FAILURE
		NDIS_STATUS_PENDING
		NDIS_STATUS_SUCCESS

	Note:

	========================================================================
*/
NDIS_STATUS MiniportMMRequest(IN PRTMP_ADAPTER pAdapter,
			      IN PUCHAR pData, IN UINT Length)
{
	struct sk_buff *pSkb = NULL;
	NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
	UCHAR FreeNum;

	ASSERT(Length <= MGMT_DMA_BUFFER_SIZE);

	do {
#if 0
		// Reset is in progress, stop immediately
		if (RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_RESET_IN_PROGRESS) ||
		    RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_HALT_IN_PROGRESS)) {
			DBGPRINT(RT_DEBUG_WARN,
				 "MiniportMMRequest-Card in abnormal state!!!\n");
			Status = NDIS_STATUS_FAILURE;
			break;
		}
#endif
		// Check Free priority queue
		Status = RTMPFreeTXDRequest(pAdapter, QID_MGMT, 1, &FreeNum);
		if (Status == NDIS_STATUS_SUCCESS) {
			Status =
			    RTMPAllocateNdisPacket(pAdapter, &pSkb, pData,
						   Length);
			if (Status != NDIS_STATUS_SUCCESS) {
				DBGPRINT(RT_DEBUG_WARN,
					 "MiniportMMRequest (error:: can't allocate skb buffer)\n");
				break;
			}

			Status = MlmeHardTransmit(pAdapter, pSkb);
			if (Status != NDIS_STATUS_SUCCESS)
				RELEASE_NDIS_PACKET(pAdapter, pSkb);
		} else {
			pAdapter->RalinkCounters.MgmtRingFullCount++;

			// Kick MGMT ring transmit
			RTMP_IO_WRITE32(pAdapter, TX_CNTL_CSR, 0x00000010);

			DBGPRINT(RT_DEBUG_TRACE,
				 "Not enough space in MgmtRing\n");
		}
	} while (FALSE);

	return Status;
}

/*
	========================================================================

	Routine	Description:
		Copy frame from waiting queue into relative ring buffer and set
	appropriate ASIC register to kick hardware transmit function

	Arguments:
		pAdapter	Pointer	to our adapter
		pBuffer		Pointer to  memory of outgoing frame
		Length		Size of outgoing management frame

	Return Value:
		NDIS_STATUS_FAILURE
		NDIS_STATUS_PENDING
		NDIS_STATUS_SUCCESS

	Note:

	========================================================================
*/
NDIS_STATUS MlmeHardTransmit(IN PRTMP_ADAPTER pAdapter,
			     IN struct sk_buff * pSkb)
{
	PUCHAR pSrcBufVA;
	ULONG SrcBufLen;
	PTXD_STRUC pTxD;
#ifdef BIG_ENDIAN
	PTXD_STRUC pDestTxD;
	TXD_STRUC TxD;
#endif
	PHEADER_802_11 pHeader_802_11;
	BOOLEAN bAckRequired, bInsertTimestamp;
	UCHAR MlmeRate;
#if SL_IRQSAVE
	ULONG IrqFlags;
#endif

	DBGPRINT(RT_DEBUG_INFO, "MlmeHardTransmit\n");
	// only one buffer is used in MGMT packet
	pSrcBufVA = (PVOID) pSkb->data;
	SrcBufLen = pSkb->len;

	// Make sure MGMT ring resource won't be used by other threads
#if SL_IRQSAVE
	spin_lock_irqsave(&pAdapter->MgmtRingLock, IrqFlags);
#else
	spin_lock_bh(&pAdapter->MgmtRingLock);
#endif

#ifndef BIG_ENDIAN
	pTxD =
	    (PTXD_STRUC) pAdapter->MgmtRing.Cell[pAdapter->MgmtRing.CurTxIndex].
	    AllocVa;
#else
	pDestTxD =
	    (PTXD_STRUC) pAdapter->MgmtRing.Cell[pAdapter->MgmtRing.CurTxIndex].
	    AllocVa;
	TxD = *pDestTxD;
	pTxD = &TxD;
	RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif

	if ((pTxD->Owner == DESC_OWN_NIC) || (pTxD->bWaitingDmaDoneInt == 1)) {
		// Descriptor owned by NIC. No descriptor avaliable
		// This should not happen since caller guaranteed.
		// Make sure to release MGMT ring resource
		DBGPRINT(RT_DEBUG_ERROR, "MlmeHardTransmit: MGMT RING full\n");

#if SL_IRQSAVE
		spin_unlock_irqrestore(&pAdapter->MgmtRingLock,
				       (unsigned long)IrqFlags);
#else
		spin_unlock_bh(&pAdapter->MgmtRingLock);
#endif

		return NDIS_STATUS_FAILURE;
	}
	if (pSrcBufVA == NULL) {
		// The buffer shouldn't be NULL
		DBGPRINT(RT_DEBUG_ERROR, "MlmeHardTransmit: buffer NULL\n");

#if SL_IRQSAVE
		spin_unlock_irqrestore(&pAdapter->MgmtRingLock,
				       (unsigned long)IrqFlags);
#else
		spin_unlock_bh(&pAdapter->MgmtRingLock);
#endif

		return NDIS_STATUS_FAILURE;
	}
	// outgoing frame always wakeup PHY to prevent frame lost
	if (OPSTATUS_TEST_FLAG(pAdapter, fOP_STATUS_DOZE))
		AsicForceWakeup(pAdapter);

	pHeader_802_11 = (PHEADER_802_11) pSrcBufVA;

	if (pHeader_802_11->Addr1[0] & 0x01) {
		MlmeRate = pAdapter->PortCfg.BasicMlmeRate;
	} else {
		MlmeRate = pAdapter->PortCfg.MlmeRate;
	}

	// Verify Mlme rate for a / g bands.
	if ((pAdapter->LatchRfRegs.Channel > 14) && (MlmeRate < RATE_6))	// 11A band
		MlmeRate = RATE_6;

	//
	// Should not be hard code to set PwrMgmt to 0 (PWR_ACTIVE)
	// Snice it's been set to 0 while on MgtMacHeaderInit
	// By the way this will cause frame to be send on PWR_SAVE failed.
	//
	// pHeader_802_11->FC.PwrMgmt = 0; // (pAd->PortCfg.Psm == PWR_SAVE);
	//
	bInsertTimestamp = FALSE;
	if (pHeader_802_11->FC.Type == BTYPE_CNTL)	// must be PS-POLL
	{
		bAckRequired = FALSE;
	} else			// BTYPE_MGMT or BTYPE_DATA(must be NULL frame)
	{
		pAdapter->Sequence++;
		pHeader_802_11->Sequence = pAdapter->Sequence;

		if (pHeader_802_11->Addr1[0] & 0x01)	// MULTICAST, BROADCAST
		{
			bAckRequired = FALSE;
			pHeader_802_11->Duration = 0;
		} else {
			bAckRequired = TRUE;
			pHeader_802_11->Duration =
			    RTMPCalcDuration(pAdapter,
					     pAdapter->PortCfg.MlmeRate, 14);
			if (pHeader_802_11->FC.SubType == SUBTYPE_PROBE_RSP) {
				bInsertTimestamp = TRUE;
			}
		}
	}

	// Before radar detection done, mgmt frame can not be sent but probe req
	// Because we need to use probe req to trigger driver to send probe req in passive scan
	if ((pHeader_802_11->FC.SubType != SUBTYPE_PROBE_REQ)
	    && (pAdapter->PortCfg.bIEEE80211H == 1)
	    && (pAdapter->PortCfg.RadarDetect.RDMode != RD_NORMAL_MODE)) {
		DBGPRINT(RT_DEBUG_ERROR,
			 "MlmeHardTransmit --> radar detect not in normal mode !!!\n");
#if SL_IRQSAVE
		spin_unlock_irqrestore(&pAdapter->MgmtRingLock,
				       (unsigned long)IrqFlags);
#else
		spin_unlock_bh(&pAdapter->MgmtRingLock);
#endif
		return (NDIS_STATUS_FAILURE);
	}
#ifdef BIG_ENDIAN
	RTMPFrameEndianChange(pAdapter, (PUCHAR) pHeader_802_11, DIR_WRITE,
			      FALSE);
#endif

	//
	// fill scatter-and-gather buffer list into TXD
	//
	pTxD->BufCount = 1;
	pTxD->BufLen0 = SrcBufLen;
	pTxD->BufPhyAddr0 =
	    pci_map_single(pAdapter->pPci_Dev, (PVOID) pSrcBufVA, SrcBufLen,
			   PCI_DMA_TODEVICE);

#ifdef BIG_ENDIAN
	RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
	*pDestTxD = TxD;
	pTxD = pDestTxD;
#endif

	// Initialize TX Descriptor
	// For inter-frame gap, the number is for this frame and next frame
	// For MLME rate, we will fix as 2Mb to match other vendor's implement
	pAdapter->MgmtRing.Cell[pAdapter->MgmtRing.CurTxIndex].pSkb = pSkb;
	pAdapter->MgmtRing.Cell[pAdapter->MgmtRing.CurTxIndex].pNextSkb = NULL;
	RTMPWriteTxDescriptor(pAdapter, pTxD, CIPHER_NONE, 0, 0, bAckRequired,
			      FALSE, bInsertTimestamp, SHORT_RETRY, IFS_BACKOFF,
			      MlmeRate, SrcBufLen, QID_MGMT,
			      PTYPE_SPECIAL | PSUBTYPE_MGMT);

	DBGPRINT_RAW(RT_DEBUG_INFO, "MLMEHardTransmit use rate %d\n", MlmeRate);

	INC_RING_INDEX(pAdapter->MgmtRing.CurTxIndex, MGMT_RING_SIZE);
	pAdapter->RalinkCounters.KickTxCount++;

	// Kick MGMT ring transmit
	RTMP_IO_WRITE32(pAdapter, TX_CNTL_CSR, 0x00000010);

	// Make sure to release MGMT ring resource
#if SL_IRQSAVE
	spin_unlock_irqrestore(&pAdapter->MgmtRingLock,
			       (unsigned long)IrqFlags);
#else
	spin_unlock_bh(&pAdapter->MgmtRingLock);
#endif

	return NDIS_STATUS_SUCCESS;
}

// should be called only when -
// 1. MEADIA_CONNECTED
// 2. AGGREGATION_IN_USED
// 3. Fragmentation not in used
// 4. either no previous frame (pPrevAddr1=NULL) .OR. previoud frame is aggregatible
BOOLEAN TxFrameIsAggregatible(IN PRTMP_ADAPTER pAd,
			      IN PUCHAR pPrevAddr1, IN PUCHAR p8023hdr)
{
	// can't aggregate EAPOL (802.1x) frame
	if ((p8023hdr[12] == 0x88) && (p8023hdr[13] == 0x8e))
		return FALSE;

	// can't aggregate multicast/broadcast frame
	if (p8023hdr[0] & 0x01)
		return FALSE;

	if (INFRA_ON(pAd))	// must be unicast to AP
		return TRUE;
	else if ((pPrevAddr1 == NULL) || (memcmp(pPrevAddr1, p8023hdr, ETH_ALEN) == 0))	// unicast to same STA
		return TRUE;
	else
		return FALSE;
}

// NOTE: we do have an assumption here, that Byte0 and Byte1 always reasid at the same
//       scatter gather buffer
NDIS_STATUS Sniff2BytesFromNdisBuffer(IN struct sk_buff * pFirstSkb,
				      IN UCHAR DesiredOffset,
				      OUT PUCHAR pByte0, OUT PUCHAR pByte1)
{
	PUCHAR pBufferVA;
	ULONG BufferLen, AccumulateBufferLen, BufferBeginOffset;

	pBufferVA = (PVOID) pFirstSkb->data;
	BufferLen = pFirstSkb->len;
	BufferBeginOffset = 0;
	AccumulateBufferLen = BufferLen;

	*pByte0 = *(PUCHAR) (pBufferVA + DesiredOffset - BufferBeginOffset);
	*pByte1 = *(PUCHAR) (pBufferVA + DesiredOffset - BufferBeginOffset + 1);
	return NDIS_STATUS_SUCCESS;
}

/*
	========================================================================

	Routine	Description:
		This routine classifies outgoing frames into several AC (Access
		Category) and enqueue them into corresponding s/w waiting queues.

	Arguments:
		pAd	Pointer	to our adapter
		pPacket		Pointer to send packet

	Return Value:
		None

	Note:

	========================================================================
*/
NDIS_STATUS RTMPSendPacket(IN PRTMP_ADAPTER pAd, IN struct sk_buff * pSkb)
{
	PUCHAR pSrcBufVA;
	UINT AllowFragSize;
	UCHAR NumberOfFrag;
	UCHAR RTSRequired;
	UCHAR QueIdx, UserPriority;
#if SL_IRQSAVE
	ULONG IrqFlags;
#endif

	DBGPRINT(RT_DEBUG_INFO, "====> RTMPSendPacket\n");

	// Prepare packet information structure for buffer descriptor
	pSrcBufVA = (PVOID) pSkb->data;

	if (pSrcBufVA == NULL) {
		// Resourece is low, system did not allocate virtual address
		// return NDIS_STATUS_FAILURE directly to upper layer
		RELEASE_NDIS_PACKET(pAd, pSkb);
		return NDIS_STATUS_FAILURE;
	}
	// STEP 1. Decide number of fragments required to deliver this MSDU.
	//     The estimation here is not very accurate because difficult to
	//     take encryption overhead into consideration here. The result
	//     "NumberOfFrag" is then just used to pre-check if enough free
	//     TXD are available to hold this MSDU.

	if (*pSrcBufVA & 0x01)	// fragmentation not allowed on multicast & broadcast
		NumberOfFrag = 1;
	else if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_AGGREGATION_INUSED))
		NumberOfFrag = 1;	// Aggregation overwhelms fragmentation
	else {
		// The calculated "NumberOfFrag" is a rough estimation because of various
		// encryption/encapsulation overhead not taken into consideration. This number is just
		// used to make sure enough free TXD are available before fragmentation takes place.
		// In case the actual required number of fragments of an skb buffer
		// excceeds "NumberOfFrag"caculated here and not enough free TXD available, the
		// last fragment (i.e. last MPDU) will be dropped in RTMPHardTransmit() due to out of
		// resource, and the skb buffer will be indicated NDIS_STATUS_FAILURE. This should
		// rarely happen and the penalty is just like a TX RETRY fail. Affordable.

		AllowFragSize =
		    (pAd->PortCfg.FragmentThreshold) - LENGTH_802_11 -
		    LENGTH_CRC;
		NumberOfFrag =
		    ((pSkb->len - LENGTH_802_3 +
		      LENGTH_802_1_H) / AllowFragSize) + 1;
	}
	// Save fragment number to Ndis packet reserved field
	RTMP_SET_PACKET_FRAGMENTS(pSkb, NumberOfFrag);

	// STEP 2. Check the requirement of RTS:
	//     If multiple fragment required, RTS is required only for the first fragment
	//     if the fragment size large than RTS threshold

	if (NumberOfFrag > 1)
		RTSRequired =
		    (pAd->PortCfg.FragmentThreshold >
		     pAd->PortCfg.RtsThreshold) ? 1 : 0;
	else
		RTSRequired = (pSkb->len > pAd->PortCfg.RtsThreshold) ? 1 : 0;

#if 0
	// RTS/CTS may also be required in order to protect OFDM frame
	if ((pAd->PortCfg.TxRate >= RATE_FIRST_OFDM_RATE) && OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED))	// use the same condition "fOP_STATUS_BG_PROTECTION_INUSED" in func. this and RTMPSendRTSFrame()
		RTSRequired = 1;
#endif
	// Save RTS requirement to Ndis packet reserved field
	RTMP_SET_PACKET_RTS(pSkb, RTSRequired);
	RTMP_SET_PACKET_TXRATE(pSkb, pAd->PortCfg.TxRate);

	//
	// STEP 3. Traffic classification. outcome = <UserPriority, QueIdx>
	//
	UserPriority = 0;
	QueIdx = QID_AC_BE;
	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED)) {
		USHORT Protocol;
		UCHAR LlcSnapLen = 0, Byte0, Byte1;
		do {
			// get Ethernet protocol field
			Protocol =
			    (USHORT) ((pSrcBufVA[12] << 8) + pSrcBufVA[13]);
			if (Protocol <= 1500) {
				// get Ethernet protocol field from LLC/SNAP
				if (Sniff2BytesFromNdisBuffer
				    (pSkb, LENGTH_802_3 + 6, &Byte0,
				     &Byte1) != NDIS_STATUS_SUCCESS)
					break;

				Protocol = (USHORT) ((Byte0 << 8) + Byte1);
				LlcSnapLen = 8;
			}
			// always AC_BE for non-IP packet
			if (Protocol != 0x0800)
				break;

			// get IP header
			if (Sniff2BytesFromNdisBuffer
			    (pSkb, LENGTH_802_3 + LlcSnapLen, &Byte0,
			     &Byte1) != NDIS_STATUS_SUCCESS)
				break;

			// return AC_BE if packet is not IPv4
			if ((Byte0 & 0xf0) != 0x40)
				break;

			UserPriority = (Byte1 & 0xe0) >> 5;
			QueIdx = MapUserPriorityToAccessCategory[UserPriority];

			// TODO: have to check ACM bit. apply TSPEC if ACM is ON
			// TODO: downgrade UP & QueIdx before passing ACM
			if (pAd->PortCfg.APEdcaParm.bACM[QueIdx]) {
				UserPriority = 0;
				QueIdx = QID_AC_BE;
			}
		} while (FALSE);
	}

	RTMP_SET_PACKET_UP(pSkb, UserPriority);

#if SL_IRQSAVE
	spin_lock_irqsave(&pAd->TxSwQueueLock, IrqFlags);
#else
	spin_lock_bh(&pAd->TxSwQueueLock);
#endif

	pAd->RalinkCounters.OneSecOsTxCount[QueIdx]++;

//      if(pAd->TxSwQueue[QueIdx].Number<=100)
	skb_queue_tail(&pAd->TxSwQueue[QueIdx], pSkb);

#if SL_IRQSAVE
	spin_unlock_irqrestore(&pAd->TxSwQueueLock, (unsigned long)IrqFlags);
#else
	spin_unlock_bh(&pAd->TxSwQueueLock);
#endif

	pAd->RalinkCounters.OneSecOsTxCount[QueIdx]++;	// TODO: for debug only. to be removed
	return NDIS_STATUS_SUCCESS;
}

/*
	========================================================================

	Routine	Description:
		This subroutine will scan through releative ring descriptor to find
		out avaliable free ring descriptor and compare with request size.

	Arguments:
		pAdapter	Pointer	to our adapter
		QueIdx	    Selected TX Ring

	Return Value:
		NDIS_STATUS_FAILURE		Not enough free descriptor
		NDIS_STATUS_SUCCESS		Enough free descriptor

	Note:

	========================================================================
*/
NDIS_STATUS RTMPFreeTXDRequest(IN PRTMP_ADAPTER pAdapter,
			       IN UCHAR QueIdx,
			       IN UCHAR NumberRequired, IN PUCHAR FreeNumberIs)
{
	INT FreeNumber = 0;
	PTXD_STRUC pTxD;
#ifdef BIG_ENDIAN
	PTXD_STRUC pDestTxD;
	TXD_STRUC TxD;
#endif
	PRTMP_TX_RING pTxRing;
	PRTMP_MGMT_RING pMgmtRing;
	NDIS_STATUS Status = NDIS_STATUS_FAILURE;

	switch (QueIdx) {
	case QID_AC_BK:
	case QID_AC_BE:
	case QID_AC_VI:
	case QID_AC_VO:
	case QID_HCCA:
// TODO: do we need to wrap the code by SpinLock?
		pTxRing = &pAdapter->TxRing[QueIdx];
#ifndef BIG_ENDIAN
		pTxD = (PTXD_STRUC) pTxRing->Cell[pTxRing->CurTxIndex].AllocVa;
#else
		pDestTxD =
		    (PTXD_STRUC) pTxRing->Cell[pTxRing->CurTxIndex].AllocVa;
		TxD = *pDestTxD;
		pTxD = &TxD;
		RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif

		// ring full waiting for DmaDoneInt() to release skb buffer
		if (pTxD->bWaitingDmaDoneInt == 1)
			FreeNumber = 0;
		else {
			FreeNumber =
			    TX_RING_SIZE + pTxRing->NextTxDmaDoneIndex -
			    pTxRing->CurTxIndex;
			if (FreeNumber > TX_RING_SIZE)
				FreeNumber -= TX_RING_SIZE;
		}

		if (FreeNumber >= NumberRequired)
			Status = NDIS_STATUS_SUCCESS;
		break;

	case QID_MGMT:
		pMgmtRing = &pAdapter->MgmtRing;
#ifndef BIG_ENDIAN
		pTxD =
		    (PTXD_STRUC) pMgmtRing->Cell[pMgmtRing->CurTxIndex].AllocVa;
#else
		pDestTxD =
		    (PTXD_STRUC) pMgmtRing->Cell[pMgmtRing->CurTxIndex].AllocVa;
		TxD = *pDestTxD;
		pTxD = &TxD;
		RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif

		if (pTxD->bWaitingDmaDoneInt == 1)
			FreeNumber = 0;
		else {
			FreeNumber =
			    MGMT_RING_SIZE + pMgmtRing->NextTxDmaDoneIndex -
			    pMgmtRing->CurTxIndex;
			if (FreeNumber > MGMT_RING_SIZE)
				FreeNumber -= MGMT_RING_SIZE;
		}

		if (FreeNumber >= NumberRequired)
			Status = NDIS_STATUS_SUCCESS;
		break;

	default:
		break;
	}

	*FreeNumberIs = (UCHAR) FreeNumber;
	return (Status);

}

VOID RTMPSendNullFrame(IN PRTMP_ADAPTER pAd,
		       IN PVOID pBuffer, IN ULONG Length, IN UCHAR TxRate)
{
	PUCHAR pDest;
	PTXD_STRUC pTxD;
#ifdef BIG_ENDIAN
	PTXD_STRUC pDestTxD;
	TXD_STRUC TxD;
#endif
	PRTMP_TX_RING pTxRing = &pAd->TxRing[QID_AC_BE];
	PHEADER_802_11 pHeader_802_11;
	UCHAR PID;
#if SL_IRQSAVE
	ULONG IrqFlags;
#endif

	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS) ||
	    RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))
		return;

	// WPA 802.1x secured port control
	if (((pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPA) ||
	     (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPAPSK)
//#ifdef WPA_SUPPLICANT_SUPPORT
	     || (pAd->PortCfg.IEEE8021X == TRUE)
//#endif
	    ) && (pAd->PortCfg.PortSecured == WPA_802_1X_PORT_NOT_SECURED)) {
		return;
	}
	// outgoing frame always wakeup PHY to prevent frame lost
	//if (pAd->PortCfg.Psm == PWR_SAVE)
	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
		AsicForceWakeup(pAd);

	// Make sure Tx ring resource won't be used by other threads
#if SL_IRQSAVE
	spin_lock_irqsave(&pAd->TxRingLock, IrqFlags);
#else
	spin_lock_bh(&pAd->TxRingLock);
#endif

	// Get the Tx Ring descriptor & Dma Buffer address
	pDest = (PUCHAR) pTxRing->Cell[pTxRing->CurTxIndex].DmaBuf.AllocVa;
#ifndef BIG_ENDIAN
	pTxD = (PTXD_STRUC) pTxRing->Cell[pTxRing->CurTxIndex].AllocVa;
#else
	pDestTxD = (PTXD_STRUC) pTxRing->Cell[pTxRing->CurTxIndex].AllocVa;
	TxD = *pDestTxD;
	pTxD = &TxD;
	RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif

	if ((pTxD->Owner == DESC_OWN_NIC) || (pTxD->bWaitingDmaDoneInt == 1)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 "RTMPSendNullFrame: Ring Full. Give up TX NULL\n");
		pAd->RalinkCounters.TxRingErrCount++;

#if SL_IRQSAVE
		spin_unlock_irqrestore(&pAd->TxRingLock,
				       (unsigned long)IrqFlags);
#else
		spin_unlock_bh(&pAd->TxRingLock);
#endif

		return;
	}

	DBGPRINT(RT_DEBUG_TRACE, "SYNC - send NULL Frame @%d Mbps...\n",
		 RateIdToMbps[TxRate]);
	memcpy(pDest, pBuffer, Length);

	pHeader_802_11 = (PHEADER_802_11) pDest;
	pHeader_802_11->FC.PwrMgmt = (pAd->PortCfg.Psm == PWR_SAVE);
	pHeader_802_11->Duration =
	    pAd->PortCfg.Dsifs + RTMPCalcDuration(pAd, TxRate, 14);

	pAd->Sequence++;
	pHeader_802_11->Sequence = pAd->Sequence;

	if (TxRate > pAd->PortCfg.TxRate)
		PID = PTYPE_NULL_AT_HIGH_RATE;
	else
		PID = PTYPE_DATA_REQUIRE_ACK;

#ifdef BIG_ENDIAN
	RTMPFrameEndianChange(pAd, (PUCHAR) pHeader_802_11, DIR_WRITE, FALSE);
#endif

	pTxD->BufLen0 = Length;
	pTxD->BufCount = 1;

#ifdef BIG_ENDIAN
	RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
	*pDestTxD = TxD;
	pTxD = pDestTxD;
#endif

	pTxRing->Cell[pTxRing->CurTxIndex].pSkb = NULL;
	pTxRing->Cell[pTxRing->CurTxIndex].pNextSkb = NULL;
	RTMPWriteTxDescriptor(pAd, pTxD, CIPHER_NONE, 0, 0, TRUE, FALSE, FALSE,
			      LONG_RETRY, IFS_BACKOFF, TxRate, Length,
			      QID_AC_BE, PID);

	INC_RING_INDEX(pTxRing->CurTxIndex, TX_RING_SIZE);
	pAd->RalinkCounters.KickTxCount++;

	// Kick Tx Control Register at the end of all ring buffer preparation
	RTMP_IO_WRITE32(pAd, TX_CNTL_CSR, BIT8[QID_AC_BE]);	// use AC_BE

#if SL_IRQSAVE
	spin_unlock_irqrestore(&pAd->TxRingLock, (unsigned long)IrqFlags);
#else
	spin_unlock_bh(&pAd->TxRingLock);
#endif

}

VOID RTMPSendRTSFrame(IN PRTMP_ADAPTER pAd,
		      IN PUCHAR pDA,
		      IN unsigned int NextMpduSize,
		      IN UCHAR TxRate, IN USHORT CtsDuration, IN UCHAR QueIdx)
{
	PRTS_FRAME pRtsFrame;
	PTXD_STRUC pTxD;
#ifdef BIG_ENDIAN
	PTXD_STRUC pDestTxD;
	TXD_STRUC TxD;
#endif
	PUCHAR pBuf;
	PRTMP_TX_RING pTxRing = &pAd->TxRing[QueIdx];
#if SL_IRQSAVE
	ULONG IrqFlags;
#endif

	// Make sure Tx ring resource won't be used by other threads
#if SL_IRQSAVE
	spin_lock_irqsave(&pAd->TxRingLock, IrqFlags);
#else
	spin_lock_bh(&pAd->TxRingLock);
#endif

	pRtsFrame =
	    (PRTS_FRAME) pTxRing->Cell[pTxRing->CurTxIndex].DmaBuf.AllocVa;
	pBuf = (PUCHAR) pRtsFrame;
#ifndef BIG_ENDIAN
	pTxD = (PTXD_STRUC) pTxRing->Cell[pTxRing->CurTxIndex].AllocVa;
#else
	pDestTxD = (PTXD_STRUC) pTxRing->Cell[pTxRing->CurTxIndex].AllocVa;
	TxD = *pDestTxD;
	pTxD = &TxD;
	RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif

	if ((pTxD->Owner == DESC_OWN_NIC) || (pTxD->bWaitingDmaDoneInt == 1)) {
		// Descriptor owned by NIC. No descriptor avaliable
		// This should not happen since caller guaranteed.
		DBGPRINT(RT_DEBUG_ERROR, "RTMPSendRTSFrame: TX RING full\n");
		pAd->RalinkCounters.TxRingErrCount++;

#if SL_IRQSAVE
		spin_unlock_irqrestore(&pAd->TxRingLock,
				       (unsigned long)IrqFlags);
#else
		spin_unlock_bh(&pAd->TxRingLock);
#endif
		return;
	}

	memset(pRtsFrame, 0, sizeof(RTS_FRAME));
	pRtsFrame->FC.Type = BTYPE_CNTL;
	// CTS-to-self's duration = SIFS + MPDU
	pRtsFrame->Duration =
	    pAd->PortCfg.Dsifs + RTMPCalcDuration(pAd, pAd->PortCfg.TxRate,
						  NextMpduSize);

	// Write Tx descriptor
	// Don't kick tx start until all frames are prepared
	// RTS has to set more fragment bit for fragment burst
	// RTS did not encrypt
	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED)) {
		DBGPRINT(RT_DEBUG_INFO, "Making CTS-to-self Frame\n");
		pRtsFrame->FC.SubType = SUBTYPE_CTS;
		memcpy(pRtsFrame->Addr1, pAd->CurrentAddress, ETH_ALEN);
#ifdef BIG_ENDIAN
		RTMPFrameEndianChange(pAd, (PUCHAR) pRtsFrame, DIR_WRITE,
				      FALSE);
		RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
		*pDestTxD = TxD;
		pTxD = pDestTxD;
#endif
		pTxRing->Cell[pTxRing->CurTxIndex].pSkb = NULL;
		pTxRing->Cell[pTxRing->CurTxIndex].pNextSkb = NULL;
		RTMPWriteTxDescriptor(pAd, pTxD, CIPHER_NONE, 0, 0, FALSE, TRUE,
				      FALSE, SHORT_RETRY, IFS_BACKOFF,
				      /*pAd->PortCfg.RtsRate */ RATE_11, 10,
				      QueIdx,
				      PTYPE_SPECIAL | PSUBTYPE_OTHER_CNTL);
#if 0
		//
		// To continue fetch next MPDU, turn on this Burst mode
		//
		pTxD->Burst = TRUE;
#endif
	} else {
		if ((*pDA) & 0x01) {
			// should not use RTS/CTS to protect MCAST frame since no one will reply CTS
#if SL_IRQSAVE
			spin_unlock_irqrestore(&pAd->TxRingLock,
					       (unsigned long)IrqFlags);
#else
			spin_unlock_bh(&pAd->TxRingLock);
#endif
			return;
		}
		DBGPRINT(RT_DEBUG_INFO, "Making RTS Frame\n");
		pRtsFrame->FC.SubType = SUBTYPE_RTS;
		memcpy(pRtsFrame->Addr1, pDA, ETH_ALEN);
		memcpy(pRtsFrame->Addr2, pAd->CurrentAddress, ETH_ALEN);
		// RTS's duration need to include and extra (SIFS + CTS) time
		pRtsFrame->Duration += (pAd->PortCfg.Dsifs + CtsDuration);
#ifdef BIG_ENDIAN
		RTMPFrameEndianChange(pAd, (PUCHAR) pRtsFrame, DIR_WRITE,
				      FALSE);
		RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
		*pDestTxD = TxD;
		pTxD = pDestTxD;
#endif
		pTxRing->Cell[pTxRing->CurTxIndex].pSkb = NULL;
		pTxRing->Cell[pTxRing->CurTxIndex].pNextSkb = NULL;
		RTMPWriteTxDescriptor(pAd, pTxD, CIPHER_NONE, 0, 0, TRUE, TRUE,
				      FALSE, SHORT_RETRY, IFS_BACKOFF,
				      pAd->PortCfg.RtsRate, sizeof(RTS_FRAME),
				      QueIdx, PTYPE_SPECIAL | PSUBTYPE_RTS);
	}

	INC_RING_INDEX(pAd->TxRing[QueIdx].CurTxIndex, TX_RING_SIZE);
	pAd->RalinkCounters.KickTxCount++;

	// leave the KICK action until the protected MPDU is ready
#if SL_IRQSAVE
	spin_unlock_irqrestore(&pAd->TxRingLock, (unsigned long)IrqFlags);
#else
	spin_unlock_bh(&pAd->TxRingLock);
#endif

}

/*
	========================================================================

	Routine	Description:
		Copy frame from waiting queue into relative ring buffer and set
	appropriate ASIC register to kick hardware encryption before really
	sent out to air.

	Arguments:
		pAd		Pointer	to our adapter
		PNDIS_PACKET	Pointer to outgoing Ndis frame
		NumberOfFrag	Number of fragment required

	Return Value:
		None

	Note:

	========================================================================
*/
#ifdef BIG_ENDIAN
static inline
#endif
 NDIS_STATUS RTMPHardTransmit(IN PRTMP_ADAPTER pAd,
			      IN struct sk_buff *pSkb, IN UCHAR QueIdx)
{
	PUCHAR pSrcBufVA;
	PUCHAR pDestBufVA, pDest;
	UINT SrcBytesCopied;
	UINT FreeMpduSize, MpduSize = 0;
	ULONG SrcBufLen, NextPacketBufLen = 0;
	UCHAR SrcBufIdx;
	ULONG SrcBufPA;
	INT SrcRemainingBytes;
	UCHAR FrameGap;
	HEADER_802_11 Header_802_11;
	PHEADER_802_11 pHeader80211;
	PUCHAR pExtraLlcSnapEncap;	// NULL: no extra LLC/SNAP is required
	UCHAR CipherAlg;
	PTXD_STRUC pTxD;
#ifdef BIG_ENDIAN
	PTXD_STRUC pDestTxD;
	TXD_STRUC TxD;
#endif
	BOOLEAN bEAPOLFrame;
	BOOLEAN bAckRequired;
	ULONG Iv16, Iv32;
	UCHAR RetryMode = SHORT_RETRY;
	USHORT AckDuration = 0;
	USHORT EncryptionOverhead = 0;
	UCHAR KeyIdx;
	PCIPHER_KEY pKey = NULL;
	UCHAR PID;
	PRTMP_TX_RING pTxRing = &pAd->TxRing[QueIdx];
	PRTMP_SCATTER_GATHER_LIST pSGList;
	RTMP_SCATTER_GATHER_LIST LocalSGList;
	struct sk_buff *pNextSkb;
	BOOLEAN bClonePacket;
	UCHAR MpduRequired, RtsRequired, UserPriority;
	UCHAR TxRate;
//  UCHAR           WdsIdx;
	USHORT Protocol;
	// To indicate cipher used for this packet
	NDIS_802_11_ENCRYPTION_STATUS Cipher;
#if SL_IRQSAVE
	ULONG IrqFlags;
#endif

	BOOLEAN bRTS_CTSFrame = FALSE;

	MpduRequired = RTMP_GET_PACKET_FRAGMENTS(pSkb);
	RtsRequired = RTMP_GET_PACKET_RTS(pSkb);
	UserPriority = RTMP_GET_PACKET_UP(pSkb);
	TxRate = RTMP_GET_PACKET_TXRATE(pSkb);
//  WdsIdx       = RTMP_GET_PACKET_WDS(pSkb);

	if (pAd->PortCfg.BssType == BSS_MONITOR && pAd->PortCfg.RFMONTX == TRUE) {
#if SL_IRQSAVE
		spin_lock_irqsave(&pAd->TxRingLock, IrqFlags);
#else
		spin_lock_bh(&pAd->TxRingLock);
#endif

#ifndef BIG_ENDIAN
		pTxD = (PTXD_STRUC) pTxRing->Cell[pTxRing->CurTxIndex].AllocVa;
#else
		pDestTxD =
		    (PTXD_STRUC) pTxRing->Cell[pTxRing->CurTxIndex].AllocVa;
		TxD = *pDestTxD;
		pTxD = &TxD;
		RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif

		if ((pTxD->Owner == DESC_OWN_NIC)
		    || (pTxD->bWaitingDmaDoneInt == 1)) {
			// Descriptor owned by NIC. No descriptor avaliable
			// This should not happen since caller guaranteed.
			DBGPRINT(RT_DEBUG_ERROR,
				 "RTMPHardTransmit: TX RING full\n");
			pAd->RalinkCounters.TxRingErrCount++;

#if SL_IRQSAVE
			spin_unlock_irqrestore(&pAd->TxRingLock,
					       (unsigned long)IrqFlags);
#else
			spin_unlock_bh(&pAd->TxRingLock);
#endif

			RELEASE_NDIS_PACKET(pAd, pSkb);
			return (NDIS_STATUS_RESOURCES);
		}
		pTxD->BufCount = 1;
		pTxD->BufLen0 = pSkb->len;
		pTxD->BufPhyAddr0 =
		    pci_map_single(pAd->pPci_Dev, (PVOID) pSkb->data, pSkb->len,
				   PCI_DMA_TODEVICE);
#ifdef BIG_ENDIAN
		RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
		*pDestTxD = TxD;
		pTxD = pDestTxD;
#endif

		MlmeSetPsmBit(pAd, PWR_ACTIVE);
		pTxRing->Cell[pTxRing->CurTxIndex].pSkb = pSkb;
		pTxRing->Cell[pTxRing->CurTxIndex].pNextSkb = NULL;
		RTMPWriteTxDescriptor(pAd, pTxD, CIPHER_NONE, 0, 0, FALSE,
				      FALSE, FALSE, SHORT_RETRY, IFS_BACKOFF,
				      pAd->PortCfg.TxRate, pSkb->len, QueIdx,
				      PTYPE_SPECIAL | PSUBTYPE_DATA_NO_ACK);
		INC_RING_INDEX(pTxRing->CurTxIndex, TX_RING_SIZE);
		pAd->RalinkCounters.KickTxCount++;

#if SL_IRQSAVE
		spin_unlock_irqrestore(&pAd->TxRingLock,
				       (unsigned long)IrqFlags);
#else
		spin_unlock_bh(&pAd->TxRingLock);
#endif

		goto kick_ring;
	}

	if ((pAd->PortCfg.bIEEE80211H == 1)
	    && (pAd->PortCfg.RadarDetect.RDMode != RD_NORMAL_MODE)) {
		DBGPRINT(RT_DEBUG_INFO,
			 "RTMPHardTransmit --> radar detect not in normal mode !!!\n");
		RELEASE_NDIS_PACKET(pAd, pSkb);
		return (NDIS_STATUS_FAILURE);
	}
	DBGPRINT(RT_DEBUG_INFO, "RTMPHardTransmit(RTS=%d, Frag=%d)\n",
		 RtsRequired, MpduRequired);
	//
	// Prepare packet information structure which will be query for buffer descriptor
	//
	pSrcBufVA = (PVOID) pSkb->data;
	SrcBufLen = pSkb->len;

	if (SrcBufLen < 14) {
		DBGPRINT(RT_DEBUG_ERROR,
			 "RTMPHardTransmit --> Skb buffer error !!!\n");
		RELEASE_NDIS_PACKET(pAd, pSkb);
		return (NDIS_STATUS_FAILURE);
	}
	//
	// If DHCP datagram or ARP datagram , we need to send it as Low rates.
	//
	if (pAd->PortCfg.Channel <= 14) {
		//
		// Case 802.11 b/g
		// basic channel means that we can use CCKM's low rate as RATE_1.
		//
		if ((TxRate != RATE_1) && RTMPCheckDHCPFrame(pAd, pSkb))
			TxRate = RATE_1;
	} else {
		//
		// Case 802.11a
		// We must used OFDM's low rate as RATE_6, note RATE_1 is not allow
		// Only OFDM support on Channel > 14
		//
		if ((TxRate != RATE_6) && RTMPCheckDHCPFrame(pAd, pSkb))
			TxRate = RATE_6;
	}

	if (TxRate < pAd->PortCfg.MinTxRate)
		TxRate = pAd->PortCfg.MinTxRate;

	bClonePacket = FALSE;	// cloning is not necessary
	pNextSkb = NULL;	// no aggregation is required

#ifdef AGGREGATION_SUPPORT
	// check if aggregation applicable on this MSDU and the next one. If applicable
	// make sure both MSDUs use internally created Skb buffer so that both has
	// only one scatter-gather buffer
	// NOTE: 1.) aggregation not applicable when CKIP inused. because it's difficult for driver
	//       to calculate CMIC on the aggregated MSDU
	//       2.) used Aggregation if packet didn't be fragment, that means MpduRequired must be 1.
	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_AGGREGATION_INUSED) &&
	    (!skb_queue_empty(&pAd->TxSwQueue[QueIdx])) &&
	    (TxRate >= RATE_6) &&
	    (MpduRequired == 1) &&
	    TxFrameIsAggregatible(pAd, NULL, pSrcBufVA)) {
		PUCHAR pNextPacketBufVA;

		DBGPRINT(RT_DEBUG_INFO,
			 "AGGRE: 1st MSDU aggregatible(len=%d)\n", pSkb->len);

		pNextSkb = skb_dequeue(&pAd->TxSwQueue[QueIdx]);

		pNextPacketBufVA = (PVOID) pNextSkb->data;
		NextPacketBufLen = pNextSkb->len;

		if (TxFrameIsAggregatible(pAd, pSrcBufVA, pNextPacketBufVA)) {
			// need to clone pNextSkb to assure it uses only 1 scatter buffer
			DBGPRINT(RT_DEBUG_INFO,
				 "AGGRE: 2nd MSDU aggregatible(len=%d)\n",
				 NextPacketBufLen);
		} else {
			// can't aggregate. put next packe back to TxSwQueue
			skb_queue_head(&pAd->TxSwQueue[QueIdx], pNextSkb);
			pNextSkb = NULL;
		}
	}
#else
	pNextSkb = NULL;
#endif

	// Decide if Packet Cloning is required -
	// 1. when fragmentation && TKIP is inused, we have to clone it into a single buffer
	//    NDIS_PACKET so that driver can pre-cluculate and append TKIP MIC at tail of the
	//    source packet before hardware fragmentation can be performed
	// 2. if too many physical buffers in scatter-gather list (>4), we have to clone it into
	//    a single buffer NDIS_PACKET before furthur processing
	if ((RTMP_GET_PACKET_SOURCE(pSkb) == PKTSRC_NDIS)
	    && (bClonePacket == FALSE)) {
		if ((MpduRequired > 1)
		    && (pAd->PortCfg.WepStatus == Ndis802_11Encryption2Enabled))
			bClonePacket = TRUE;
		else {
			if (skb_shinfo(pSkb)->nr_frags != 0) {
				DBGPRINT(RT_DEBUG_WARN, "\nnr_frags=%d\n\n",
					 skb_shinfo(pSkb)->nr_frags);
			}
			if (skb_shinfo(pSkb)->nr_frags == 0) {
				LocalSGList.NumberOfElements = 1;
				LocalSGList.Elements[0].Length = SrcBufLen;
				LocalSGList.Elements[0].Address = pSkb->data;
				LocalSGList.Elements[1].Length = 0;
				LocalSGList.Elements[1].Address = NULL;
			} else {
				DBGPRINT(RT_DEBUG_WARN,
					 "Scatter-gather packet (nr_frags=%d)\n\n\n",
					 skb_shinfo(pSkb)->nr_frags);
				bClonePacket = TRUE;
			}
		}
	}
	// Clone then release the original skb buffer
	if (bClonePacket) {
		struct sk_buff *pOutSkb;
		NDIS_STATUS Status;

		Status = RTMPCloneNdisPacket(pAd, pSkb, &pOutSkb);
		RELEASE_NDIS_PACKET(pAd, pSkb);
		if (Status != NDIS_STATUS_SUCCESS)
			return Status;

		// Use the new cloned packet from now on
		pSkb = pOutSkb;
		RTMP_SET_PACKET_UP(pSkb, UserPriority);

		pSrcBufVA = (PVOID) pSkb->data;
		SrcBufLen = pSkb->len;
	}
	// use local scatter-gather structure for internally created skb buffer
	// Can't use NDIS_PER_PACKET_INFO() to get scatter gather list
	//if (RTMP_GET_PACKET_SOURCE(pSkb) != PKTSRC_NDIS)
	{
		LocalSGList.NumberOfElements = 1;
		LocalSGList.Elements[0].Length = SrcBufLen;
		LocalSGList.Elements[0].Address = pSkb->data;
		LocalSGList.Elements[1].Length = 0;
		LocalSGList.Elements[1].Address = NULL;

		// link next MSDU into scatter list, if any
		if (pNextSkb) {
			LocalSGList.NumberOfElements = 2;
			LocalSGList.Elements[0].Length = SrcBufLen;
			LocalSGList.Elements[0].Address = pSkb->data;
			LocalSGList.Elements[1].Length = NextPacketBufLen;
			LocalSGList.Elements[1].Address = pNextSkb->data;
			pSkb->len += NextPacketBufLen;
		}
	}
	pSGList = &LocalSGList;

	// ------------------------------------------
	// STEP 0.1 Add 802.1x protocol check.
	// ------------------------------------------

	// For non-WPA network, 802.1x message should not encrypt even privacy is on.
	if (memcmp(EAPOL, pSrcBufVA + 12, 2) == 0) {
		bEAPOLFrame = TRUE;
		if (pAd->PortCfg.MicErrCnt >= 2)
			pAd->PortCfg.MicErrCnt++;
	} else
		bEAPOLFrame = FALSE;

	//
	// WPA 802.1x secured port control - drop all non-802.1x frame before port secured
	//
	if (((pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPA) ||
	     (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPAPSK) ||
	     (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPA2) ||
	     (pAd->PortCfg.AuthMode == Ndis802_11AuthModeWPA2PSK)
//#ifdef WPA_SUPPLICANT_SUPPORT
	     || (pAd->PortCfg.IEEE8021X == TRUE)
//#endif
	    ) &&
	    ((pAd->PortCfg.PortSecured == WPA_802_1X_PORT_NOT_SECURED)
	     || (pAd->PortCfg.MicErrCnt >= 2)) && (bEAPOLFrame == FALSE)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 "RTMPHardTransmit --> Drop packet before port secured !!!\n");
		RELEASE_NDIS_PACKET(pAd, pSkb);
		return (NDIS_STATUS_FAILURE);
	}

	if (*pSrcBufVA & 0x01)	// Multicast or Broadcast
	{
		bAckRequired = FALSE;
		PID = PTYPE_SPECIAL | PSUBTYPE_DATA_NO_ACK;
		INC_COUNTER64(pAd->WlanCounters.MulticastTransmittedFrameCount);
		Cipher = pAd->PortCfg.GroupCipher;	// Cipher for Multicast or Broadcast
	} else {
		bAckRequired = TRUE;
		PID = PTYPE_DATA_REQUIRE_ACK;
		Cipher = pAd->PortCfg.PairCipher;	// Cipher for Unicast
	}

	// -------------------------------------------
	// STEP 0.2. some early parsing
	// -------------------------------------------

	// 1. traditional TX burst
	if (pAd->PortCfg.bEnableTxBurst && (pAd->Sequence & 0x7))
		FrameGap = IFS_SIFS;
	// 2. frame belonging to AC that has non-zero TXOP
	else if (pAd->PortCfg.APEdcaParm.bValid
		 && pAd->PortCfg.APEdcaParm.Txop[QueIdx])
		FrameGap = IFS_SIFS;
	// 3. otherwise, always BACKOFF before transmission
	else
		FrameGap = IFS_BACKOFF;	// Default frame gap mode

	Protocol = *(pSrcBufVA + 12) * 256 + *(pSrcBufVA + 13);
	// if orginal Ethernet frame contains no LLC/SNAP, then an extra LLC/SNAP encap is required
	if (Protocol > 1500) {
		pExtraLlcSnapEncap = SNAP_802_1H;
		if ((memcmp(IPX, pSrcBufVA + 12, 2) == 0) ||
		    (memcmp(APPLE_TALK, pSrcBufVA + 12, 2) == 0)) {
			pExtraLlcSnapEncap = SNAP_BRIDGE_TUNNEL;
		}
	} else
		pExtraLlcSnapEncap = NULL;

	// ------------------------------------------------------------------
	// STEP 1. WAKE UP PHY
	//      outgoing frame always wakeup PHY to prevent frame lost and
	//      turn off PSM bit to improve performance
	// ------------------------------------------------------------------
	// not to change PSM bit, just send this frame out?
	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
		AsicForceWakeup(pAd);
#if 1
	if (pAd->PortCfg.Psm == PWR_SAVE)
		MlmeSetPsmBit(pAd, PWR_ACTIVE);
#endif

	// -----------------------------------------------------------------
	// STEP 2. MAKE A COMMON 802.11 HEADER SHARED BY ENTIRE FRAGMENT BURST.
	// -----------------------------------------------------------------

	pAd->Sequence++;
	MAKE_802_11_HEADER(pAd, Header_802_11, pSrcBufVA, pAd->Sequence);

	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED))
		Header_802_11.FC.SubType = SUBTYPE_QDATA;

	// --------------------------------------------------------
	// STEP 3. FIND ENCRYPT KEY AND DECIDE CIPHER ALGORITHM
	//      Find the WPA key, either Group or Pairwise Key
	//      LEAP + TKIP also use WPA key.
	// --------------------------------------------------------
	// Decide WEP bit and cipher suite to be used. Same cipher suite should be used for whole fragment burst
	// In Cisco CCX 2.0 Leap Authentication
	//         WepStatus is Ndis802_11Encryption1Enabled but the key will use PairwiseKey
	//         Instead of the SharedKey, SharedKey Length may be Zero.

	KeyIdx = 0xff;
	if (bEAPOLFrame) {
		ASSERT(pAd->SharedKey[0].CipherAlg <= CIPHER_CKIP128);
		if ((pAd->SharedKey[0].CipherAlg) && (pAd->SharedKey[0].KeyLen)) {
			CipherAlg = pAd->SharedKey[0].CipherAlg;
			KeyIdx = 0;
		}
	} else if (pAd->PortCfg.WepStatus == Ndis802_11Encryption1Enabled) {
		// standard WEP64 or WEP128
		KeyIdx = pAd->PortCfg.DefaultKeyId;
	} else if ((pAd->PortCfg.WepStatus == Ndis802_11Encryption2Enabled) ||
		   (pAd->PortCfg.WepStatus == Ndis802_11Encryption3Enabled)) {
		if (Header_802_11.Addr1[0] & 0x01)	// multicast
			KeyIdx = pAd->PortCfg.DefaultKeyId;
		else if (pAd->SharedKey[0].KeyLen)
			KeyIdx = 0;
		else
			KeyIdx = pAd->PortCfg.DefaultKeyId;
	}

	if (KeyIdx == 0xff)
		CipherAlg = CIPHER_NONE;
	else if (pAd->SharedKey[KeyIdx].KeyLen == 0)
		CipherAlg = CIPHER_NONE;
	else {
		Header_802_11.FC.Wep = 1;
		CipherAlg = pAd->SharedKey[KeyIdx].CipherAlg;
		pKey = &pAd->SharedKey[KeyIdx];
	}

	DBGPRINT(RT_DEBUG_INFO,
		 "RTMPHardTransmit(bEAP=%d) - %s key#%d, KeyLen=%d\n",
		 bEAPOLFrame, CipherName[CipherAlg], KeyIdx,
		 pAd->SharedKey[KeyIdx].KeyLen);

	// STEP 3.1 if TKIP is used and fragmentation is required. Driver has to
	//          append TKIP MIC at tail of the scatter buffer (This must be the
	//          ONLY scatter buffer in the skb buffer).
	//          MAC ASIC will only perform IV/EIV/ICV insertion but no TKIP MIC
	if ((MpduRequired > 1) && (CipherAlg == CIPHER_TKIP)) {
		ASSERT(pSGList->NumberOfElements == 1);
		RTMPCalculateMICValue(pAd, pSkb, pExtraLlcSnapEncap, pKey);
		memcpy(pSrcBufVA + SrcBufLen, pAd->PrivateInfo.Tx.MIC, 8);
		SrcBufLen += 8;
		pSkb->len += 8;
		CipherAlg = CIPHER_TKIP_NO_MIC;
	}

	// ----------------------------------------------------------------
	// STEP 4. Make RTS frame or CTS-to-self frame if required
	// ----------------------------------------------------------------

	//
	// calcuate the overhead bytes that encryption algorithm may add. This
	// affects the calculate of "duration" field
	//
	if ((CipherAlg == CIPHER_WEP64) || (CipherAlg == CIPHER_WEP128))
		EncryptionOverhead = 8;	//WEP: IV[4] + ICV[4];
	else if (CipherAlg == CIPHER_TKIP_NO_MIC)
		EncryptionOverhead = 12;	//TKIP: IV[4] + EIV[4] + ICV[4], MIC will be added to TotalPacketLength
	else if (CipherAlg == CIPHER_TKIP)
		EncryptionOverhead = 20;	//TKIP: IV[4] + EIV[4] + ICV[4] + MIC[8]
	else if (CipherAlg == CIPHER_AES)
		EncryptionOverhead = 16;	// AES: IV[4] + EIV[4] + MIC[8]
	else
		EncryptionOverhead = 0;

	// decide how much time an ACK/CTS frame will consume in the air
	AckDuration =
	    RTMPCalcDuration(pAd, pAd->PortCfg.ExpectedACKRate[TxRate], 14);

	if (RtsRequired) {
		unsigned int NextMpduSize;

		// If fragment required, MPDU size is maximum fragment size
		// Else, MPDU size should be frame with 802.11 header & CRC
		if (MpduRequired > 1)
			NextMpduSize = pAd->PortCfg.FragmentThreshold;
		else {
			NextMpduSize =
			    pSkb->len + LENGTH_802_11 + LENGTH_CRC -
			    LENGTH_802_3;
			if (pExtraLlcSnapEncap)
				NextMpduSize += LENGTH_802_1_H;
		}

		RTMPSendRTSFrame(pAd,
				 Header_802_11.Addr1,
				 NextMpduSize + EncryptionOverhead,
				 pAd->PortCfg.RtsRate, AckDuration, QueIdx);

		// RTS/CTS-protected frame should use LONG_RETRY (=4) and SIFS
		RetryMode = LONG_RETRY;
		FrameGap = IFS_SIFS;
		bRTS_CTSFrame = TRUE;
	}
	// --------------------------------------------------------
	// STEP 5. START MAKING MPDU(s)
	//      Start Copy Ndis Packet into Ring buffer.
	//      For frame required more than one ring buffer (fragment), all ring buffers
	//      have to be filled before kicking start tx bit.
	//      Make sure TX ring resource won't be used by other threads
	// --------------------------------------------------------

	SrcBufIdx = 0;
	SrcBufPA =
	    pci_map_single(pAd->pPci_Dev, pSGList->Elements[0].Address,
			   SrcBufLen, PCI_DMA_TODEVICE);
	SrcBufPA += LENGTH_802_3;	// skip 802.3 header
	SrcBufLen -= LENGTH_802_3;	// skip 802.3 header
	SrcRemainingBytes = pSkb->len - LENGTH_802_3;

#if SL_IRQSAVE
	spin_lock_irqsave(&pAd->TxRingLock, IrqFlags);
#else
	spin_lock_bh(&pAd->TxRingLock);
#endif

	do {
		//
		// STEP 5.1 ACQUIRE TXD
		//
		pDestBufVA =
		    (PUCHAR) pTxRing->Cell[pTxRing->CurTxIndex].DmaBuf.AllocVa;
#ifndef BIG_ENDIAN
		pTxD = (PTXD_STRUC) pTxRing->Cell[pTxRing->CurTxIndex].AllocVa;
#else
		pDestTxD =
		    (PTXD_STRUC) pTxRing->Cell[pTxRing->CurTxIndex].AllocVa;
		TxD = *pDestTxD;
		pTxD = &TxD;
		RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif

		if ((pTxD->Owner == DESC_OWN_NIC)
		    || (pTxD->bWaitingDmaDoneInt == 1)) {
			// Descriptor owned by NIC. No descriptor avaliable
			// This should not happen since caller guaranteed.
			DBGPRINT(RT_DEBUG_ERROR,
				 "RTMPHardTransmit: TX RING full\n");
			pAd->RalinkCounters.TxRingErrCount++;

#if SL_IRQSAVE
			spin_unlock_irqrestore(&pAd->TxRingLock,
					       (unsigned long)IrqFlags);
#else
			spin_unlock_bh(&pAd->TxRingLock);
#endif

			RELEASE_NDIS_PACKET(pAd, pSkb);
			if (pNextSkb) {
				RELEASE_NDIS_PACKET(pAd, pNextSkb);
			}
			return (NDIS_STATUS_RESOURCES);
		}
		//
		// STEP 5.2 PUT IVOFFSET, IV, EIV INTO TXD
		//
		pTxD->IvOffset = LENGTH_802_11;
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED))
			pTxD->IvOffset += 2;	// add QOS CONTROL bytes

		if ((CipherAlg == CIPHER_WEP64) || (CipherAlg == CIPHER_WEP128)) {
			PUCHAR pTmp;
			pTmp = (PUCHAR) & pTxD->Iv;
			*pTmp = RandomByte(pAd);
			*(pTmp + 1) = RandomByte(pAd);
			*(pTmp + 2) = RandomByte(pAd);
			*(pTmp + 3) = (KeyIdx << 6);
		} else if ((CipherAlg == CIPHER_TKIP)
			   || (CipherAlg == CIPHER_TKIP_NO_MIC)) {
			RTMPInitTkipEngine(pAd, pKey->Key, KeyIdx,	// This might cause problem when using peer key
					   Header_802_11.Addr2,
					   pKey->TxMic,
					   pKey->TxTsc, &Iv16, &Iv32);

			memcpy(&pTxD->Iv, &Iv16, 4);	// Copy IV
			memcpy(&pTxD->Eiv, &Iv32, 4);	// Copy EIV
			INC_TX_TSC(pKey->TxTsc);	// Increase TxTsc for next transmission
		} else if (CipherAlg == CIPHER_AES) {
			PUCHAR pTmp;
			pTmp = (PUCHAR) & Iv16;
			*pTmp = pKey->TxTsc[0];
			*(pTmp + 1) = pKey->TxTsc[1];
			*(pTmp + 2) = 0;
			*(pTmp + 3) = (pAd->PortCfg.DefaultKeyId << 6) | 0x20;
			memcpy(&Iv32, &pKey->TxTsc[2], 4);

			memcpy(&pTxD->Iv, &Iv16, 4);	// Copy IV
			memcpy(&pTxD->Eiv, &Iv32, 4);	// Copy EIV
			INC_TX_TSC(pKey->TxTsc);	// Increase TxTsc for next transmission
		}
		//
		// STEP 5.3 COPY 802.11 HEADER INTO 1ST DMA BUFFER
		//
		pDest = pDestBufVA;
		memcpy(pDest, &Header_802_11, sizeof(Header_802_11));
		pDest += sizeof(Header_802_11);

		//
		// Fragmentation is not allowed on multicast & broadcast
		// So, we need to used the MAX_FRAG_THRESHOLD instead of pAd->PortCfg.FragmentThreshold
		// otherwise if pSkb->len > pAd->PortCfg.FragmentThreshold then
		// packet will be fragment on multicast & broadcast.
		//
		if (*pSrcBufVA & 0x01) {
			FreeMpduSize =
			    MAX_FRAG_THRESHOLD - sizeof(Header_802_11) -
			    LENGTH_CRC;
		} else {
			FreeMpduSize =
			    pAd->PortCfg.FragmentThreshold -
			    sizeof(Header_802_11) - LENGTH_CRC;
		}

		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED)) {
			// copy QOS CONTROL bytes
			*pDest =
			    (UserPriority & 0x0f) | pAd->PortCfg.
			    AckPolicy[QueIdx];
			*(pDest + 1) = 0;
			pDest += 2;
			FreeMpduSize -= 2;
			if (pAd->PortCfg.AckPolicy[QueIdx] != NORMAL_ACK) {
				bAckRequired = FALSE;
				PID = PTYPE_SPECIAL | PSUBTYPE_DATA_NO_ACK;
			}
		}
		// if aggregation, put the 2nd MSDU length(extra 2-byte field) after QOS_CONTROL. little endian
		if (pNextSkb) {
			*pDest = (UCHAR) NextPacketBufLen & 0xff;
			*(pDest + 1) = (UCHAR) (NextPacketBufLen >> 8);
			pDest += 2;
			FreeMpduSize = MAX_AGGREGATION_SIZE;
			((PHEADER_802_11) pDestBufVA)->FC.Order = 1;	// steal "order" bit to mark "aggregation"
		}
		//
		// STEP 5.4 COPY LLC/SNAP, CKIP MIC INTO 1ST DMA BUFFER ONLY WHEN THIS
		//          MPDU IS THE 1ST OR ONLY FRAGMENT
		//
		if (Header_802_11.Frag == 0) {
			if (pExtraLlcSnapEncap) {
				// Insert LLC-SNAP encapsulation
				memcpy(pDest, pExtraLlcSnapEncap, 6);
				pDest += 6;
				memcpy(pDest, pSrcBufVA + 12, 2);
				pDest += 2;
				FreeMpduSize -= LENGTH_802_1_H;
			}
		}
		// TX buf0 size fixed here
		pTxD->BufLen0 = (ULONG) (pDest - pDestBufVA);
		pTxD->BufCount = 1;

		//
		// STEP 5.5 TRAVERSE scatter-gather list TO BUILD THE MPDU PAYLOAD
		//
		MpduSize = pTxD->BufLen0;
		SrcBytesCopied = 0;
		do {
			if (SrcBufLen == 0) {
				// do nothing. skip to next scatter-gather buffer
			} else if (SrcBufLen <= FreeMpduSize) {
				// scatter-gather buffer still fit into current MPDU
				switch (pTxD->BufCount) {
				case 1:
					pTxD->BufPhyAddr1 = SrcBufPA;
					pTxD->BufLen1 = SrcBufLen;
					pTxD->BufCount++;
					break;
				case 2:
					pTxD->BufPhyAddr2 = SrcBufPA;
					pTxD->BufLen2 = SrcBufLen;
					pTxD->BufCount++;
					break;
				case 3:
					pTxD->BufPhyAddr3 = SrcBufPA;
					pTxD->BufLen3 = SrcBufLen;
					pTxD->BufCount++;
					break;
				case 4:
					pTxD->BufPhyAddr4 = SrcBufPA;
					pTxD->BufLen4 = SrcBufLen;
					pTxD->BufCount++;
					break;
				default:	// should never happen
					break;
				}
				SrcBytesCopied += SrcBufLen;
				pDest += SrcBufLen;
				FreeMpduSize -= SrcBufLen;
				MpduSize += SrcBufLen;
				SrcBufPA += SrcBufLen;
				SrcBufLen = 0;
			} else {
				// scatter-gather buffer exceed current MPDU. leave some of the buffer to next MPDU
				switch (pTxD->BufCount) {
				case 1:
					pTxD->BufPhyAddr1 = SrcBufPA;
					pTxD->BufLen1 = FreeMpduSize;
					pTxD->BufCount++;
					break;
				case 2:
					pTxD->BufPhyAddr2 = SrcBufPA;
					pTxD->BufLen2 = FreeMpduSize;
					pTxD->BufCount++;
					break;
				case 3:
					pTxD->BufPhyAddr3 = SrcBufPA;
					pTxD->BufLen3 = FreeMpduSize;
					pTxD->BufCount++;
					break;
				case 4:
					pTxD->BufPhyAddr4 = SrcBufPA;
					pTxD->BufLen4 = FreeMpduSize;
					pTxD->BufCount++;
					break;
				default:	// should never happen
					break;
				}
				SrcBytesCopied += FreeMpduSize;
				pDest += FreeMpduSize;
				MpduSize += FreeMpduSize;
				SrcBufPA += FreeMpduSize;
				SrcBufLen -= FreeMpduSize;

				// a complete MPDU is built. break out to write TXD of this MPDU
				break;
			}

			// advance to next scatter-gather BUFFER
			SrcBufIdx++;
			if (SrcBufIdx < pSGList->NumberOfElements) {
				SrcBufPA =
				    pci_map_single(pAd->pPci_Dev,
						   pSGList->Elements[SrcBufIdx].
						   Address,
						   pSGList->Elements[SrcBufIdx].
						   Length, PCI_DMA_TODEVICE);
				SrcBufLen = pSGList->Elements[SrcBufIdx].Length;
			} else
				SrcBufLen = 0;

			if (SrcBufLen == 0)
				break;

		} while (TRUE);	// End of copying payload

		// remaining size of the skb buffer payload
		SrcRemainingBytes -= SrcBytesCopied;

		//
		// STEP 5.6 MODIFY MORE_FRAGMENT BIT & DURATION FIELD. WRITE TXD
		//
		pHeader80211 = (PHEADER_802_11) pDestBufVA;
		if (SrcRemainingBytes > 0)	// more fragment is required
		{
			UINT NextMpduSize;
			NextMpduSize =
			    min((UINT) SrcRemainingBytes,
				(UINT) pAd->PortCfg.FragmentThreshold);
			pHeader80211->FC.MoreFrag = 1;
			pHeader80211->Duration = 3 * pAd->PortCfg.Dsifs
			    + 2 * AckDuration
			    + RTMPCalcDuration(pAd, TxRate,
					       NextMpduSize +
					       EncryptionOverhead);
#ifdef BIG_ENDIAN
			RTMPFrameEndianChange(pAd, (PUCHAR) pHeader80211,
					      DIR_WRITE, FALSE);
			RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
			*pDestTxD = TxD;
			pTxD = pDestTxD;
#endif
#if 1
			if (bRTS_CTSFrame) {
				// After RTS/CTS frame, data frame should use SIFS time.
				// To patch this code, add the following code.
				// Recommended by Jerry 2005/07/25 for WiFi testing with Proxim AP
				pTxD->Cwmin = 0;
				pTxD->Cwmax = 0;
				pTxD->Aifsn = 1;
				pTxD->IFS = IFS_BACKOFF;
			}
#endif

			pTxRing->Cell[pTxRing->CurTxIndex].pSkb = NULL;
			pTxRing->Cell[pTxRing->CurTxIndex].pNextSkb = NULL;
			RTMPWriteTxDescriptor(pAd, pTxD, CipherAlg, 0, KeyIdx,
					      bAckRequired, TRUE, FALSE,
					      RetryMode, FrameGap, TxRate,
					      MpduSize, QueIdx, PID);

			FrameGap = IFS_SIFS;	// use SIFS for all subsequent fragments
			Header_802_11.Frag++;	// increase Frag #
#if 0
			//
			// On fragment case, turn on this bit to tell ASIC to continue fetch the fragment data.
			// that belongs to the same MSDU.
			//
			pTxD->Burst = TRUE;
#endif
		} else		// this is the last or only fragment
		{
			pHeader80211->FC.MoreFrag = 0;
			if (pHeader80211->Addr1[0] & 0x01)	// multicast/broadcast
				pHeader80211->Duration = 0;
			else
				pHeader80211->Duration =
				    pAd->PortCfg.Dsifs + AckDuration;

			if ((bEAPOLFrame) && (TxRate > RATE_6))
				TxRate = RATE_6;
#ifdef BIG_ENDIAN
			RTMPFrameEndianChange(pAd, (PUCHAR) pHeader80211,
					      DIR_WRITE, FALSE);
			RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
			*pDestTxD = TxD;
			pTxD = pDestTxD;
#endif
#if 1
			if (bRTS_CTSFrame) {
				// After RTS/CTS frame, data frame should use SIFS time.
				// To patch this code, add the following code.
				// Recommended by Jerry 2005/07/25 for WiFi testing with Proxim AP

				pTxD->Cwmin = 0;
				pTxD->Cwmax = 0;
				pTxD->Aifsn = 1;
				pTxD->IFS = IFS_BACKOFF;
			}
#endif
			pTxRing->Cell[pTxRing->CurTxIndex].pSkb = pSkb;
			pTxRing->Cell[pTxRing->CurTxIndex].pNextSkb = pNextSkb;

			RTMPWriteTxDescriptor(pAd, pTxD, CipherAlg, 0, KeyIdx,
					      bAckRequired, FALSE, FALSE,
					      RetryMode, FrameGap, TxRate,
					      MpduSize, QueIdx, PID);
		}

		INC_RING_INDEX(pTxRing->CurTxIndex, TX_RING_SIZE);
		pAd->RalinkCounters.KickTxCount++;

	} while (SrcRemainingBytes > 0);

	// Make sure to release Tx ring resource
#if SL_IRQSAVE
	spin_unlock_irqrestore(&pAd->TxRingLock, (unsigned long)IrqFlags);
#else
	spin_unlock_bh(&pAd->TxRingLock);
#endif

      kick_ring:
	// -------------------------------------------------
	// STEP 6. KICK TX AND EXIT
	// -------------------------------------------------
	if (pAd->PortCfg.bWmmCapable) {
		//
		// Kick all TX rings, QID_AC_VO, QID_AC_VI, QID_AC_BE, QID_AC_BK, to pass WMM testing.
		//
		RTMP_IO_WRITE32(pAd, TX_CNTL_CSR, 0x0000000f);
	} else {
		if (QueIdx <= QID_AC_VO) {
			RTMP_IO_WRITE32(pAd, TX_CNTL_CSR, BIT8[QueIdx]);
		}
	}

	// Check for EAPOL frame sent after MIC countermeasures
	if (pAd->PortCfg.MicErrCnt >= 3) {
		MLME_DISASSOC_REQ_STRUCT DisassocReq;

		// disassoc from current AP first
		printk
		    ("<0>MLME - disassociate with current AP after sending second continuous EAPOL frame\n");
		DBGPRINT(RT_DEBUG_TRACE,
			 "MLME - disassociate with current AP after sending second continuous EAPOL frame\n");
		DisassocParmFill(pAd, &DisassocReq, pAd->PortCfg.Bssid,
				 REASON_MIC_FAILURE);
		MlmeEnqueue(pAd, ASSOC_STATE_MACHINE, MT2_MLME_DISASSOC_REQ,
			    sizeof(MLME_DISASSOC_REQ_STRUCT), &DisassocReq);

		pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_DISASSOC;
		pAd->PortCfg.bBlockAssoc = TRUE;
		printk("<0>bBlockAssoc = %d\n", pAd->PortCfg.bBlockAssoc);

	}
	// the skb buffer will be released at DMA done interrupt service routine
	return (NDIS_STATUS_SUCCESS);
}

/*
	========================================================================

	Routine	Description:
		To do the enqueue operation and extract the first item of waiting
		list. If a number of available shared memory segments could meet
		the request of extracted item, the extracted item will be fragmented
		into shared memory segments.

	Arguments:
		pAdapter	Pointer	to our adapter
		pQueue		Pointer to Waiting Queue

	Return Value:
		None

	Note:

	========================================================================
*/
VOID RTMPDeQueuePacket(IN PRTMP_ADAPTER pAdapter, IN UCHAR QueueIdx)
{
	struct sk_buff *pSkb;
	UCHAR MpduRequired;
	NDIS_STATUS Status;
	UCHAR Count = 0;
	struct sk_buff_head *pQueue;
	//ULONG                 Number;
	UCHAR QueIdx, FreeNumber;
#if SL_IRQSAVE
	ULONG IrqFlags;
#endif

// Reset is in progress, stop immediately
	if ((RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) ||
	    (RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_RADIO_OFF)) ||
	    (RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_RESET_IN_PROGRESS)) ||
	    (RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_HALT_IN_PROGRESS)))
		return;

	// Make sure SendTxWait queue resource won't be used by other threads
#if SL_IRQSAVE
	spin_lock_irqsave(&pAdapter->TxSwQueueLock, IrqFlags);
#else
	spin_lock_bh(&pAdapter->TxSwQueueLock);
#endif
	QueIdx = QueueIdx;
	pQueue = &pAdapter->TxSwQueue[QueIdx];

	while (!skb_queue_empty(pQueue) && (Count < MAX_TX_PROCESS)) {
		// Dequeue the first entry from head of queue list
		pSkb = skb_dequeue(pQueue);
		if (!pSkb)
			break;

		// RTS or CTS-to-self for B/G protection mode has been set already.
		// There is no need to re-do it here.
		// Total fragment required = number of fragment + RST if required
		MpduRequired =
		    RTMP_GET_PACKET_FRAGMENTS(pSkb) + RTMP_GET_PACKET_RTS(pSkb);

		if (RTMPFreeTXDRequest
		    (pAdapter, QueIdx, MpduRequired,
		     &FreeNumber) == NDIS_STATUS_SUCCESS) {
			if (FreeNumber != TX_RING_SIZE) {
				skb_queue_head(pQueue, pSkb);
				break;
			}
			// 2004-10-12 if HardTransmit returns SUCCESS, then the skb buffer should be
			//            released in later on DMADoneIsr. If HardTransmit returns FAIL, the
			//            the skb buffer should already be released inside RTMPHardTransmit
			Status = RTMPHardTransmit(pAdapter, pSkb, QueIdx);

			if (Status != NDIS_STATUS_SUCCESS)
				break;
		} else {
			skb_queue_head(pQueue, pSkb);
			pAdapter->PrivateInfo.TxRingFullCnt++;
			DBGPRINT(RT_DEBUG_INFO,
				 "RTMPDequeuePacket --> Not enough free TxD (CurTxIndex=%d, NextTxDmaDoneIndex=%d)!!!\n",
				 pAdapter->TxRing[QueIdx].CurTxIndex,
				 pAdapter->TxRing[QueIdx].NextTxDmaDoneIndex);
			break;
		}
		Count++;
	}

	// Release TxSwQueue resources

#if SL_IRQSAVE
	spin_unlock_irqrestore(&pAdapter->TxSwQueueLock,
			       (unsigned long)IrqFlags);
#else
	spin_unlock_bh(&pAdapter->TxSwQueueLock);
#endif

}

/*
	========================================================================

	Routine	Description:
		Calculates the duration which is required to transmit out frames
	with given size and specified rate.

	Arguments:
		pAdapter		Pointer	to our adapter
		Rate			Transmit rate
		Size			Frame size in units of byte

	Return Value:
		Duration number in units of usec

	Note:

	========================================================================
*/
USHORT RTMPCalcDuration(IN PRTMP_ADAPTER pAdapter, IN UCHAR Rate, IN ULONG Size)
{
	ULONG Duration = 0;

	if (Rate < RATE_FIRST_OFDM_RATE)	// CCK
	{
		if ((Rate > RATE_1)
		    &&
		    (OPSTATUS_TEST_FLAG
		     (pAdapter, fOP_STATUS_SHORT_PREAMBLE_INUSED)))
			Duration = 96;	// 72+24 preamble+plcp
		else
			Duration = 192;	// 144+48 preamble+plcp

		Duration += (USHORT) ((Size << 4) / RateIdTo500Kbps[Rate]);
		if ((Size << 4) % RateIdTo500Kbps[Rate])
			Duration++;
	} else			// OFDM rates
	{
		Duration = 20 + 6;	// 16+4 preamble+plcp + Signal Extension
		Duration +=
		    4 * (USHORT) ((11 + Size * 4) / RateIdTo500Kbps[Rate]);
		if ((11 + Size * 4) % RateIdTo500Kbps[Rate])
			Duration += 4;
	}

	return (USHORT) Duration;

}

/*
	========================================================================

	Routine	Description:
		Calculates the duration which is required to transmit out frames
	with given size and specified rate.

	Arguments:
		pTxD		Pointer to transmit descriptor
		Ack			Setting for Ack requirement bit
		Fragment	Setting for Fragment bit
		RetryMode	Setting for retry mode
		Ifs			Setting for IFS gap
		Rate		Setting for transmit rate
		Service		Setting for service
		Length		Frame length
		TxPreamble  Short or Long preamble when using CCK rates
		QueIdx - 0-3, according to 802.11e/d4.4 June/2003

	Return Value:
		None

	========================================================================
*/
VOID RTMPWriteTxDescriptor(IN PRTMP_ADAPTER pAd,
			   IN PTXD_STRUC pSourceTxD,
			   IN UCHAR CipherAlg,
			   IN UCHAR KeyTable,
			   IN UCHAR KeyIdx,
			   IN BOOLEAN Ack,
			   IN BOOLEAN Fragment,
			   IN BOOLEAN InsTimestamp,
			   IN UCHAR RetryMode,
			   IN UCHAR Ifs,
			   IN UINT Rate,
			   IN ULONG Length, IN UCHAR QueIdx, IN UCHAR PID)
{
	UINT Residual;

	PTXD_STRUC pTxD;

#ifndef BIG_ENDIAN
	pTxD = pSourceTxD;
#else
	TXD_STRUC TxD;

	TxD = *pSourceTxD;
	pTxD = &TxD;
	RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif

	DBGPRINT(RT_DEBUG_INFO,
		 "WriteTxD: scatter #%d = %d + %d + %d ... = %d\n",
		 pTxD->BufCount, pTxD->BufLen0, pTxD->BufLen1, pTxD->BufLen2,
		 Length);

	pTxD->HostQId = QueIdx;
	pTxD->MoreFrag = Fragment;
	pTxD->ACK = Ack;
	pTxD->Timestamp = InsTimestamp;
	pTxD->RetryMd = RetryMode;
	pTxD->Ofdm = (Rate < RATE_FIRST_OFDM_RATE) ? 0 : 1;
	pTxD->IFS = Ifs;
	pTxD->PktId = PID;
	pTxD->Drop = 1;		// 1:valid, 0:drop
	pTxD->HwSeq = (QueIdx == QID_MGMT) ? 1 : 0;
	pTxD->BbpTxPower = DEFAULT_BBP_TX_POWER;	// TODO: to be modified
	pTxD->DataByteCnt = Length;

	// fill encryption related information, if required
	pTxD->CipherAlg = CipherAlg;
	if (CipherAlg != CIPHER_NONE) {
		pTxD->KeyTable = KeyTable;
		pTxD->KeyIndex = KeyIdx;
		pTxD->TkipMic = 1;
	}
	// In TKIP+fragmentation. TKIP MIC is already appended by driver. MAC needs not generate MIC
	if (CipherAlg == CIPHER_TKIP_NO_MIC) {
		pTxD->CipherAlg = CIPHER_TKIP;
		pTxD->TkipMic = 0;	// tell MAC need not insert TKIP MIC
	}

	if ((pAd->PortCfg.APEdcaParm.bValid) && (QueIdx <= QID_AC_VO)) {
		pTxD->Cwmin = pAd->PortCfg.APEdcaParm.Cwmin[QueIdx];
		pTxD->Cwmax = pAd->PortCfg.APEdcaParm.Cwmax[QueIdx];
		pTxD->Aifsn = pAd->PortCfg.APEdcaParm.Aifsn[QueIdx];
	} else {
		pTxD->Cwmin = CW_MIN_IN_BITS;
		pTxD->Cwmax = CW_MAX_IN_BITS;
		pTxD->Aifsn = 2;
	}

	// fill up PLCP SIGNAL field
	pTxD->PlcpSignal = RateIdToPlcpSignal[Rate];
	if (((Rate == RATE_2) || (Rate == RATE_5_5) || (Rate == RATE_11)) &&
	    (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED))) {
		pTxD->PlcpSignal |= 0x0008;
	}
	// fill up PLCP SERVICE field, not used for OFDM rates
	pTxD->PlcpService = 4;	// Service;

	// file up PLCP LENGTH_LOW and LENGTH_HIGH fields
	Length += LENGTH_CRC;	// CRC length
	switch (CipherAlg) {
	case CIPHER_WEP64:
		Length += 8;
		break;		// IV + ICV
	case CIPHER_WEP128:
		Length += 8;
		break;		// IV + ICV
	case CIPHER_TKIP:
		Length += 20;
		break;		// IV + EIV + MIC + ICV
	case CIPHER_AES:
		Length += 16;
		break;		// IV + EIV + MIC
	case CIPHER_CKIP64:
		Length += 8;
		break;		// IV + CMIC + ICV, but CMIC already inserted by driver
	case CIPHER_CKIP128:
		Length += 8;
		break;		// IV + CMIC + ICV, but CMIC already inserted by driver
	case CIPHER_TKIP_NO_MIC:
		Length += 12;
		break;		// IV + EIV + ICV
	default:
		break;
	}

	if (Rate < RATE_FIRST_OFDM_RATE)	// 11b - RATE_1, RATE_2, RATE_5_5, RATE_11
	{
		if ((Rate == RATE_1) || (Rate == RATE_2)) {
			Length = Length * 8 / (Rate + 1);
		} else {
			Residual =
			    ((Length * 16) % (11 * (1 + Rate - RATE_5_5)));
			Length = Length * 16 / (11 * (1 + Rate - RATE_5_5));
			if (Residual != 0) {
				Length++;
			}
			if ((Residual <= (3 * (1 + Rate - RATE_5_5)))
			    && (Residual != 0)) {
				if (Rate == RATE_11)	// Only 11Mbps require length extension bit
					pTxD->PlcpService |= 0x80;	// 11b's PLCP Length extension bit
			}
		}

		pTxD->PlcpLengthHigh = Length >> 8;	// 256;
		pTxD->PlcpLengthLow = Length % 256;
	} else			// OFDM - RATE_6, RATE_9, RATE_12, RATE_18, RATE_24, RATE_36, RATE_48, RATE_54
	{
		pTxD->PlcpLengthHigh = Length >> 6;	// 64;  // high 6-bit of total byte count
		pTxD->PlcpLengthLow = Length % 64;	// low 6-bit of total byte count
	}

	pTxD->bWaitingDmaDoneInt = 1;
	pTxD->Owner = DESC_OWN_NIC;	// change OWNER bit at the last

#ifdef BIG_ENDIAN
	RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
	WriteBackToDescriptor((PUCHAR) pSourceTxD, (PUCHAR) pTxD, FALSE,
			      TYPE_TXD);
#endif
}

/*
	========================================================================

	Routine	Description:
		Search tuple cache for receive duplicate frame from unicast frames.

	Arguments:
		pAdapter		Pointer	to our adapter
		pHeader			802.11 header of receiving frame

	Return Value:
		TRUE			found matched tuple cache
		FALSE			no matched found

	Note:

	========================================================================
*/
BOOLEAN RTMPSearchTupleCache(IN PRTMP_ADAPTER pAdapter,
			     IN PHEADER_802_11 pHeader)
{
	INT Index;

	for (Index = 0; Index < MAX_CLIENT; Index++) {
		if (pAdapter->TupleCache[Index].Valid == FALSE)
			continue;

		if ((memcmp
		     (pAdapter->TupleCache[Index].MacAddress, pHeader->Addr2,
		      ETH_ALEN) == 0)
		    && (pAdapter->TupleCache[Index].Sequence ==
			pHeader->Sequence)
		    && (pAdapter->TupleCache[Index].Frag == pHeader->Frag)) {
//                      DBGPRINT(RT_DEBUG_TRACE,"DUPCHECK - duplicate frame hit entry %d\n", Index);
			return (TRUE);
		}
	}
	return (FALSE);
}

/*
	========================================================================

	Routine	Description:
		Update tuple cache for new received unicast frames.

	Arguments:
		pAdapter		Pointer	to our adapter
		pHeader			802.11 header of receiving frame

	Return Value:
		None

	Note:

	========================================================================
*/
VOID RTMPUpdateTupleCache(IN PRTMP_ADAPTER pAdapter, IN PHEADER_802_11 pHeader)
{
	UCHAR Index;

	for (Index = 0; Index < MAX_CLIENT; Index++) {
		if (pAdapter->TupleCache[Index].Valid == FALSE) {
			// Add new entry
			memcpy(pAdapter->TupleCache[Index].MacAddress,
			       pHeader->Addr2, ETH_ALEN);
			pAdapter->TupleCache[Index].Sequence =
			    pHeader->Sequence;
			pAdapter->TupleCache[Index].Frag = pHeader->Frag;
			pAdapter->TupleCache[Index].Valid = TRUE;
			pAdapter->TupleCacheLastUpdateIndex = Index;
			DBGPRINT(RT_DEBUG_INFO,
				 "DUPCHECK - Add Entry %d, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
				 Index,
				 pAdapter->TupleCache[Index].MacAddress[0],
				 pAdapter->TupleCache[Index].MacAddress[1],
				 pAdapter->TupleCache[Index].MacAddress[2],
				 pAdapter->TupleCache[Index].MacAddress[3],
				 pAdapter->TupleCache[Index].MacAddress[4],
				 pAdapter->TupleCache[Index].MacAddress[5]);
			return;
		} else
		    if (memcmp
			(pAdapter->TupleCache[Index].MacAddress, pHeader->Addr2,
			 ETH_ALEN) == 0) {
			// Update old entry
			pAdapter->TupleCache[Index].Sequence =
			    pHeader->Sequence;
			pAdapter->TupleCache[Index].Frag = pHeader->Frag;
			return;
		}
	}

	// tuple cache full, replace the first inserted one (even though it may not be
	// least referenced one)
	if (Index == MAX_CLIENT) {
		pAdapter->TupleCacheLastUpdateIndex++;
		if (pAdapter->TupleCacheLastUpdateIndex >= MAX_CLIENT)
			pAdapter->TupleCacheLastUpdateIndex = 0;
		Index = pAdapter->TupleCacheLastUpdateIndex;

		// replace with new entry
		memcpy(pAdapter->TupleCache[Index].MacAddress, pHeader->Addr2,
		       ETH_ALEN);
		pAdapter->TupleCache[Index].Sequence = pHeader->Sequence;
		pAdapter->TupleCache[Index].Frag = pHeader->Frag;
		pAdapter->TupleCache[Index].Valid = TRUE;
		DBGPRINT(RT_DEBUG_INFO,
			 "DUPCHECK - replace Entry %d, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
			 Index, pAdapter->TupleCache[Index].MacAddress[0],
			 pAdapter->TupleCache[Index].MacAddress[1],
			 pAdapter->TupleCache[Index].MacAddress[2],
			 pAdapter->TupleCache[Index].MacAddress[3],
			 pAdapter->TupleCache[Index].MacAddress[4],
			 pAdapter->TupleCache[Index].MacAddress[5]);
	}
}

/*
	========================================================================

	Routine	Description:
		Suspend MSDU transmission

	Arguments:
		pAdapter		Pointer	to our adapter

	Return Value:
		None

	Note:

	========================================================================
*/
VOID RTMPSuspendMsduTransmission(IN PRTMP_ADAPTER pAd)
{
	DBGPRINT(RT_DEBUG_TRACE, "SCANNING, suspend MSDU transmission ...\n");

	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, 17, &pAd->BbpTuning.R17CurrentValue);

	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS);
	RTMP_IO_WRITE32(pAd, TX_CNTL_CSR, 0x000f0000);	// abort all TX rings
}

/*
	========================================================================

	Routine	Description:
		Resume MSDU transmission

	Arguments:
		pAdapter		Pointer	to our adapter

	Return Value:
		None

	Note:

	========================================================================
*/
VOID RTMPResumeMsduTransmission(IN PRTMP_ADAPTER pAd)
{
	DBGPRINT(RT_DEBUG_TRACE, "SCAN done, resume MSDU transmission ...\n");

	//
	// After finish BSS_SCAN_IN_PROGRESS, we need to restore Current R17 value
	//
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 17, pAd->BbpTuning.R17CurrentValue);

	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS);
	RTMPDeQueuePacket(pAd, QID_AC_BE);
	RTMP_IO_WRITE32(pAd, TX_CNTL_CSR, 0x0000000f);	// kick all TX rings
}

/*
	========================================================================

	Routine	Description:
		Check Rx descriptor, return NDIS_STATUS_FAILURE if any error dound

	Arguments:
		pRxD		Pointer	to the Rx descriptor

	Return Value:
		NDIS_STATUS_SUCCESS		No err
		NDIS_STATUS_FAILURE		Error

	Note:

	========================================================================
*/
NDIS_STATUS RTMPCheckRxError(IN PRTMP_ADAPTER pAd,
			     IN PHEADER_802_11 pHeader, IN PRXD_STRUC pRxD)
{
	PCIPHER_KEY pWpaKey;

	// Drop ToDs promiscous frame, it is opened due to CCX 2 channel load statistics
	if (pHeader->FC.ToDs)
		return (NDIS_STATUS_FAILURE);

	// Drop not U2M frames, cant's drop here because we will drop beacon in this case
	// I am kind of doubting the U2M bit operation
	// if (pRxD->U2M == 0)
	//      return(NDIS_STATUS_FAILURE);

	// drop decyption fail frame
	if (pRxD->CipherErr) {
		UINT i;
		PUCHAR ptr = (PUCHAR) pHeader;
		DBGPRINT(RT_DEBUG_TRACE,
			 "ERROR: CRC ok but CipherErr %d (len = %d, Mcast=%d, Cipher=%s, KeyId=%d)\n",
			 pRxD->CipherErr, pRxD->DataByteCnt,
			 pRxD->Mcast | pRxD->Bcast, CipherName[pRxD->CipherAlg],
			 pRxD->KeyIndex);
#if 1
		for (i = 0; i < 64; i += 16) {
			DBGPRINT_RAW(RT_DEBUG_TRACE,
				     "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x - %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
				     *ptr, *(ptr + 1), *(ptr + 2), *(ptr + 3),
				     *(ptr + 4), *(ptr + 5), *(ptr + 6),
				     *(ptr + 7), *(ptr + 8), *(ptr + 9),
				     *(ptr + 10), *(ptr + 11), *(ptr + 12),
				     *(ptr + 13), *(ptr + 14), *(ptr + 15));
			ptr += 16;
		}
#endif

		//
		// MIC Error
		//
		if (pRxD->CipherErr == 2) {
			pWpaKey = &pAd->SharedKey[pRxD->KeyIndex];
			RTMPReportMicError(pAd, pWpaKey);
			DBGPRINT(RT_DEBUG_ERROR, "Rx MIC Value error\n");
		}

		return (NDIS_STATUS_FAILURE);
	}

	return (NDIS_STATUS_SUCCESS);
}

/*
	========================================================================

	Routine	Description:
		Apply packet filter policy, return NDIS_STATUS_FAILURE if this frame
		should be dropped.

	Arguments:
		pAd		Pointer	to our adapter
		pRxD			Pointer	to the Rx descriptor
		pHeader			Pointer to the 802.11 frame header

	Return Value:
		NDIS_STATUS_SUCCESS		Accept frame
		NDIS_STATUS_FAILURE		Drop Frame

	Note:
		Maganement frame should bypass this filtering rule.

	========================================================================
*/
NDIS_STATUS RTMPApplyPacketFilter(IN PRTMP_ADAPTER pAd,
				  IN PRXD_STRUC pRxD, IN PHEADER_802_11 pHeader)
{
	UCHAR i;

	// 0. Management frame should bypass all these filtering rules.
	if (pHeader->FC.Type == BTYPE_MGMT)
		return (NDIS_STATUS_SUCCESS);

	// 0.1  Drop all Rx frames if MIC countermeasures kicks in
	if (pAd->PortCfg.MicErrCnt >= 2) {
		DBGPRINT(RT_DEBUG_TRACE, "Rx dropped by MIC countermeasure\n");
		return (NDIS_STATUS_FAILURE);
	}
	// 1. Drop unicast to me packet if NDIS_PACKET_TYPE_DIRECTED is FALSE
	if (pRxD->U2M) {
		if (pAd->bAcceptDirect == FALSE) {
			DBGPRINT(RT_DEBUG_TRACE,
				 "Rx U2M dropped by RX_FILTER\n");
			return (NDIS_STATUS_FAILURE);
		}
	}
	// 2. Drop broadcast packet if NDIS_PACKET_TYPE_BROADCAST is FALSE
	else if (pRxD->Bcast) {
		if (pAd->bAcceptBroadcast == FALSE) {
			DBGPRINT(RT_DEBUG_TRACE,
				 "Rx BCAST dropped by RX_FILTER\n");
			return (NDIS_STATUS_FAILURE);
		}
	}
	// 3. Drop (non-Broadcast) multicast packet if NDIS_PACKET_TYPE_ALL_MULTICAST is false
	//    and NDIS_PACKET_TYPE_MULTICAST is false.
	//    If NDIS_PACKET_TYPE_MULTICAST is true, but NDIS_PACKET_TYPE_ALL_MULTICAST is false.
	//    We have to deal with multicast table lookup & drop not matched packets.
	else if (pRxD->Mcast) {
		if (pAd->bAcceptAllMulticast == FALSE) {
			if (pAd->bAcceptMulticast == FALSE) {
				DBGPRINT(RT_DEBUG_INFO,
					 "Rx MCAST dropped by RX_FILTER\n");
				return (NDIS_STATUS_FAILURE);
			} else {
				// Selected accept multicast packet based on multicast table
				for (i = 0; i < pAd->NumberOfMcastAddresses;
				     i++) {
					if (memcmp
					    (pHeader->Addr1, pAd->McastTable[i],
					     ETH_ALEN) == 0)
						break;	// Matched
				}

				// Not matched
				if (i == pAd->NumberOfMcastAddresses) {
					DBGPRINT(RT_DEBUG_INFO,
						 "Rx MCAST %02x:%02x:%02x:%02x:%02x:%02x dropped by RX_FILTER\n",
						 pHeader->Addr1[0],
						 pHeader->Addr1[1],
						 pHeader->Addr1[2],
						 pHeader->Addr1[3],
						 pHeader->Addr1[4],
						 pHeader->Addr1[5]);
					return (NDIS_STATUS_FAILURE);
				} else {
					DBGPRINT(RT_DEBUG_INFO,
						 "Accept multicast %02x:%02x:%02x:%02x:%02x:%02x\n",
						 pHeader->Addr1[0],
						 pHeader->Addr1[1],
						 pHeader->Addr1[2],
						 pHeader->Addr1[3],
						 pHeader->Addr1[4],
						 pHeader->Addr1[5]);
				}
			}
		}
	}
	// 4. Not U2M, not Mcast, not Bcast, must be unicast to other DA.
	//    Since we did not implement promiscuous mode, just drop this kind of packet for now.
	else if (pAd->bAcceptPromiscuous == FALSE)
		return (NDIS_STATUS_FAILURE);

	return (NDIS_STATUS_SUCCESS);
}

/*
	========================================================================

	Routine	Description:
		Check and fine the packet waiting in SW queue with highest priority

	Arguments:
		pAdapter	Pointer	to our adapter

	Return Value:
		pQueue		Pointer to Waiting Queue

	Note:

	========================================================================
*/
struct sk_buff_head *RTMPCheckTxSwQueue(IN PRTMP_ADAPTER pAdapter,
					OUT PUCHAR pQueIdx)
{
	if (!skb_queue_empty(&pAdapter->TxSwQueue[QID_AC_VO])) {
		*pQueIdx = QID_AC_VO;
		return (&pAdapter->TxSwQueue[QID_AC_VO]);
	} else if (!skb_queue_empty(&pAdapter->TxSwQueue[QID_AC_VI])) {
		*pQueIdx = QID_AC_VI;
		return (&pAdapter->TxSwQueue[QID_AC_VI]);
	} else if (!skb_queue_empty(&pAdapter->TxSwQueue[QID_AC_BE])) {
		*pQueIdx = QID_AC_BE;
		return (&pAdapter->TxSwQueue[QID_AC_BE]);
	} else if (!skb_queue_empty(&pAdapter->TxSwQueue[QID_AC_BK])) {
		*pQueIdx = QID_AC_BK;
		return (&pAdapter->TxSwQueue[QID_AC_BK]);
	} else if (!skb_queue_empty(&pAdapter->TxSwQueue[QID_HCCA])) {
		*pQueIdx = QID_HCCA;
		return (&pAdapter->TxSwQueue[QID_HCCA]);
	}
	// No packet pending in Tx Sw queue
	*pQueIdx = QID_AC_BK;
	return (NULL);
}

//#ifdef WPA_SUPPLICANT_SUPPORT
static void ralink_michael_mic_failure(struct net_device *dev,
				       PCIPHER_KEY pWpaKey)
{
	union iwreq_data wrqu;
	char buf[128];

	/* TODO: needed parameters: count, keyid, key type, TSC */

	//Check for Group or Pairwise MIC error
	if (pWpaKey->Type == PAIRWISE_KEY)
		sprintf(buf, "MLME-MICHAELMICFAILURE.indication unicast");
	else
		sprintf(buf, "MLME-MICHAELMICFAILURE.indication broadcast");
	memset(&wrqu, 0, sizeof(wrqu));
	wrqu.data.length = strlen(buf);
	//send mic error event to wpa_supplicant
	wireless_send_event(dev, IWEVCUSTOM, &wrqu, buf);
}

//#endif

/*
	========================================================================

	Routine	Description:
		Process MIC error indication and record MIC error timer.

	Arguments:
		pAd		Pointer	to our adapter
		pWpaKey			Pointer	to the WPA key structure

	Return Value:
		None

	Note:

	========================================================================
*/
VOID RTMPReportMicError(IN PRTMP_ADAPTER pAd, IN PCIPHER_KEY pWpaKey)
{
	unsigned long Now;
	struct {
		NDIS_802_11_STATUS_INDICATION Status;
		NDIS_802_11_AUTHENTICATION_REQUEST Request;
	} Report;

//#ifdef WPA_SUPPLICANT_SUPPORT
	if (pAd->PortCfg.WPA_Supplicant == TRUE) {
		//report mic error to wpa_supplicant
		ralink_michael_mic_failure(pAd->net_dev, pWpaKey);
	}
//#endif

	// 0. Set Status to indicate auth error
	Report.Status.StatusType = Ndis802_11StatusType_Authentication;

	// 1. Check for Group or Pairwise MIC error
	if (pWpaKey->Type == PAIRWISE_KEY)
		Report.Request.Flags = NDIS_802_11_AUTH_REQUEST_PAIRWISE_ERROR;
	else
		Report.Request.Flags = NDIS_802_11_AUTH_REQUEST_GROUP_ERROR;

	// 2. Copy AP MAC address
	memcpy(Report.Request.Bssid, pWpaKey->BssId, ETH_ALEN);

	// 3. Calculate length
	Report.Request.Length = sizeof(NDIS_802_11_AUTHENTICATION_REQUEST);

	// 4. Record Last MIC error time and count
	Now = jiffies;
	if (pAd->PortCfg.MicErrCnt == 0) {
		pAd->PortCfg.MicErrCnt++;
		pAd->PortCfg.LastMicErrorTime = Now;
	} else if (pAd->PortCfg.MicErrCnt == 1) {
		if ((pAd->PortCfg.LastMicErrorTime + (60 * 1000)) < Now) {
			// Update Last MIC error time, this did not violate two MIC errors within 60 seconds
			pAd->PortCfg.LastMicErrorTime = Now;
		} else {
			pAd->PortCfg.LastMicErrorTime = Now;
			// Violate MIC error counts, MIC countermeasures kicks in
			pAd->PortCfg.MicErrCnt++;
			// We shall block all reception
			// We shall clean all Tx ring and disassoicate from AP after next EAPOL frame
			RTMPRingCleanUp(pAd, QID_AC_BK);
			RTMPRingCleanUp(pAd, QID_AC_BE);
			RTMPRingCleanUp(pAd, QID_AC_VI);
			RTMPRingCleanUp(pAd, QID_AC_VO);
			RTMPRingCleanUp(pAd, QID_HCCA);
		}
	} else {
		// MIC error count >= 2
		// This should not happen
		;
	}
}

// clone an input skb buffer to another one.
// NOTE: internally created NDIS_PACKET should be destroyed by RTMPFreeNdisPacket
NDIS_STATUS RTMPCloneNdisPacket(IN PRTMP_ADAPTER pAdapter,
				IN struct sk_buff *pInSkb,
				OUT struct sk_buff **ppOutSkb)
{
	if ((*ppOutSkb = skb_clone(pInSkb, MEM_ALLOC_FLAG)) == NULL)
		return NDIS_STATUS_FAILURE;

	RTMP_SET_PACKET_SOURCE(*ppOutSkb, PKTSRC_DRIVER);

	return NDIS_STATUS_SUCCESS;
}

// the allocated Skb buffer must be freed via RTMPFreeNdisPacket()
NDIS_STATUS RTMPAllocateNdisPacket(IN PRTMP_ADAPTER pAdapter,
				   OUT struct sk_buff ** ppSkb,
				   IN PUCHAR pData, IN UINT DataLen)
{
	// 1. Allocate Skb buffer
	*ppSkb = __dev_alloc_skb(DataLen + 2, MEM_ALLOC_FLAG);
	if (*ppSkb == NULL)
		return NDIS_STATUS_RESOURCES;

	// 2. Set packet flag
	skb_reserve(*ppSkb, 2);	// 16 byte align the IP header
	RTMP_SET_PACKET_SOURCE(*ppSkb, PKTSRC_DRIVER);

	// 3. clone the frame content
	memcpy(skb_put(*ppSkb, DataLen), pData, DataLen);

	return NDIS_STATUS_SUCCESS;
}

VOID DBGPRINT_TX_RING(IN PRTMP_ADAPTER pAdapter, IN UCHAR QueIdx)
{
	ULONG PbfCsr = 0;
	ULONG Ac0Base = 0;
	ULONG Ac0CurAddr = 0;
	ULONG Ac0HwIdx, Ac0SwIdx, AC0DmaDoneIdx;
	PTXD_STRUC pTxD;
#ifdef	BIG_ENDIAN
	PTXD_STRUC pDestTxD;
	TXD_STRUC TxD;
#endif
	int i;

	RTMP_IO_READ32(pAdapter, PBF_QUEUE_CSR, &PbfCsr);
	switch (QueIdx) {
	case QID_AC_BE:
		RTMP_IO_READ32(pAdapter, AC0_BASE_CSR, &Ac0Base);
		RTMP_IO_READ32(pAdapter, AC0_TXPTR_CSR, &Ac0CurAddr);
		break;
	case QID_AC_BK:
		RTMP_IO_READ32(pAdapter, AC1_BASE_CSR, &Ac0Base);
		RTMP_IO_READ32(pAdapter, AC1_TXPTR_CSR, &Ac0CurAddr);
		break;
	case QID_AC_VI:
		RTMP_IO_READ32(pAdapter, AC2_BASE_CSR, &Ac0Base);
		RTMP_IO_READ32(pAdapter, AC2_TXPTR_CSR, &Ac0CurAddr);
		break;
	case QID_AC_VO:
		RTMP_IO_READ32(pAdapter, AC3_BASE_CSR, &Ac0Base);
		RTMP_IO_READ32(pAdapter, AC3_TXPTR_CSR, &Ac0CurAddr);
		break;
	default:
		DBGPRINT(RT_DEBUG_ERROR,
			 "DBGPRINT_TX_RING(Ring %d) not supported\n", QueIdx);
		return;
	}
	Ac0HwIdx = (Ac0CurAddr - Ac0Base) / TXD_SIZE;
	Ac0SwIdx = pAdapter->TxRing[QueIdx].CurTxIndex;
	AC0DmaDoneIdx = pAdapter->TxRing[QueIdx].NextTxDmaDoneIndex;

	printk("TxRing%d PBF=%08x, HwIdx=%d, SwIdx[%d]=", QueIdx, PbfCsr,
	       Ac0HwIdx, Ac0SwIdx);
	for (i = 0; i < 4; i++) {
#ifndef BIG_ENDIAN
		pTxD =
		    (PTXD_STRUC) pAdapter->TxRing[QueIdx].Cell[Ac0SwIdx].
		    AllocVa;
#else
		pDestTxD =
		    (PTXD_STRUC) pAdapter->TxRing[QueIdx].Cell[Ac0SwIdx].
		    AllocVa;
		TxD = *pDestTxD;
		pTxD = &TxD;
		RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif

		printk("%d%d.", pTxD->Owner, pTxD->bWaitingDmaDoneInt);
		INC_RING_INDEX(Ac0SwIdx, TX_RING_SIZE);
	}
	printk("DmaIdx[%d]=", AC0DmaDoneIdx);
	for (i = 0; i < 4; i++) {
#ifndef BIG_ENDIAN
		pTxD =
		    (PTXD_STRUC) pAdapter->TxRing[QueIdx].Cell[AC0DmaDoneIdx].
		    AllocVa;
#else
		pDestTxD =
		    (PTXD_STRUC) pAdapter->TxRing[QueIdx].Cell[AC0DmaDoneIdx].
		    AllocVa;
		TxD = *pDestTxD;
		pTxD = &TxD;
		RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif

		printk("%d%d.", pTxD->Owner, pTxD->bWaitingDmaDoneInt);
		INC_RING_INDEX(AC0DmaDoneIdx, TX_RING_SIZE);
	}
	DBGPRINT(RT_DEBUG_TRACE, "p-NDIS=%d\n",
		 pAdapter->RalinkCounters.PendingNdisPacketCount);
}

VOID RTMPFreeTXDUponTxDmaDone(IN PRTMP_ADAPTER pAd,
			      IN UCHAR QueIdx, IN UINT RingSize)
{
	PRTMP_TX_RING pTxRing;
	PTXD_STRUC pTxD;
#ifdef	BIG_ENDIAN
	PTXD_STRUC pDestTxD;
	TXD_STRUC TxD;
#endif
	struct sk_buff **pSkb;
	struct sk_buff **pNextSkb;
	int i;

	ASSERT(QueIdx < NUM_OF_TX_RING);
	pTxRing = &pAd->TxRing[QueIdx];

	for (i = 0; i < MAX_DMA_DONE_PROCESS; i++) {
#ifndef BIG_ENDIAN
		pTxD =
		    (PTXD_STRUC) (pTxRing->Cell[pTxRing->NextTxDmaDoneIndex].
				  AllocVa);
#else
		pDestTxD =
		    (PTXD_STRUC) (pTxRing->Cell[pTxRing->NextTxDmaDoneIndex].
				  AllocVa);
		TxD = *pDestTxD;
		pTxD = &TxD;
		RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif

		pSkb = &pTxRing->Cell[pTxRing->NextTxDmaDoneIndex].pSkb;
		pNextSkb = &pTxRing->Cell[pTxRing->NextTxDmaDoneIndex].pNextSkb;
		if (pTxD->Owner == DESC_OWN_NIC)
			break;
		if (pTxD->bWaitingDmaDoneInt == 0) {
			if (pTxRing->NextTxDmaDoneIndex != pTxRing->CurTxIndex) {
				// should never reach here!!!!
				DBGPRINT_TX_RING(pAd, QueIdx);
				// dump TXD at NextTxDmaDoneIndex, check sequence#, check Airopeek if
				// the frame been transmitted out or not.
				DBGPRINT(RT_DEBUG_ERROR,
					 "pNdisPacket[%d]=0x%08lx\n",
					 pTxRing->NextTxDmaDoneIndex,
					 (unsigned long)*pSkb);
				INC_RING_INDEX(pTxRing->NextTxDmaDoneIndex,
					       RingSize);
				continue;
			} else
				break;
		}

		pTxD->bWaitingDmaDoneInt = 0;
		INC_RING_INDEX(pTxRing->NextTxDmaDoneIndex, RingSize);
		pAd->RalinkCounters.TransmittedByteCount += pTxD->DataByteCnt;
		pAd->RalinkCounters.OneSecDmaDoneCount[QueIdx]++;

#ifdef RALINK_ATE
		if (pAd->ate.Mode == ATE_TXFRAME) {
#ifdef BIG_ENDIAN
			RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
			*pDestTxD = TxD;
#endif
			continue;
		}
#endif

		if (*pSkb) {
			pci_unmap_single(pAd->pPci_Dev, pTxD->BufPhyAddr1,
					 pTxD->BufLen1, PCI_DMA_TODEVICE);
			RELEASE_NDIS_PACKET(pAd, *pSkb);
			*pSkb = NULL;
		}

		if (*pNextSkb) {
			pci_unmap_single(pAd->pPci_Dev, pTxD->BufPhyAddr2,
					 pTxD->BufLen2, PCI_DMA_TODEVICE);
			RELEASE_NDIS_PACKET(pAd, *pNextSkb);
			*pNextSkb = 0;
		}
#ifdef BIG_ENDIAN
		RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
		*pDestTxD = TxD;
#endif
	}
}

/*
	========================================================================

	Routine	Description:
		Check the out going frame, if this is an DHCP or ARP datagram
	will be duplicate another frame at low data rate transmit.

	Arguments:
		pAd			Pointer	to our adapter
		pSkb		Pointer to outgoing skb buffer

	Return Value:
		TRUE		To be transmitted at Low data rate transmit. (1Mbps/6Mbps)
		FALSE		Do nothing.

	Note:

		MAC header + IP Header + UDP Header
		  14 Bytes    20 Bytes

		UDP Header
		00|01|02|03|04|05|06|07|08|09|10|11|12|13|14|15|
						Source Port
		16|17|18|19|20|21|22|23|24|25|26|27|28|29|30|31|
					Destination Port

		port 0x43 means Bootstrap Protocol, server.
		Port 0x44 means Bootstrap Protocol, client.

	========================================================================
*/
BOOLEAN RTMPCheckDHCPFrame(IN PRTMP_ADAPTER pAd, IN struct sk_buff *pSkb)
{
	PUCHAR pSrc;
	ULONG SrcLen = 0;

	pSrc = (PVOID) pSkb->data;
	SrcLen = pSkb->len;

	// Check ARP packet
	if (SrcLen >= 13) {
		if ((pSrc[12] == 0x08) && (pSrc[13] == 0x06)) {
			DBGPRINT(RT_DEBUG_INFO,
				 "RTMPCheckDHCPFrame - ARP packet\n");
			return TRUE;
		}
	}
	// Check foe DHCP & BOOTP protocol
	if (SrcLen >= 37) {
		if ((pSrc[35] == 0x43) && (pSrc[37] == 0x44)) {
			DBGPRINT(RT_DEBUG_INFO,
				 "RTMPCheckDHCPFrame - DHCP packet\n");
			return TRUE;
		}
	}

	return FALSE;
}

VOID RTMPCckBbpTuning(IN PRTMP_ADAPTER pAd, IN UINT TxRate)
{
	CHAR Bbp94 = 0xFF;

	//
	// Do nothing if TxPowerEnable == FALSE
	//
	if (pAd->TxPowerDeltaConfig.field.TxPowerEnable == FALSE)
		return;

	if ((TxRate < RATE_FIRST_OFDM_RATE) && (pAd->BbpForCCK == FALSE)) {
		Bbp94 = pAd->Bbp94;

		if (pAd->TxPowerDeltaConfig.field.Type == 1) {
			Bbp94 += pAd->TxPowerDeltaConfig.field.DeltaValue;
		} else {
			Bbp94 -= pAd->TxPowerDeltaConfig.field.DeltaValue;
		}
		pAd->BbpForCCK = TRUE;
	} else if ((TxRate >= RATE_FIRST_OFDM_RATE) && (pAd->BbpForCCK == TRUE)) {
		Bbp94 = pAd->Bbp94;
		pAd->BbpForCCK = FALSE;
	}

	if ((Bbp94 >= 0) && (Bbp94 <= 0x0C)) {
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R94, Bbp94);
	}
}
