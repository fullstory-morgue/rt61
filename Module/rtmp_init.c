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
 *      Module Name: rtmp_init.c
 *
 *      Abstract:
 *
 *      Revision History:
 *      Who             When            What
 *      --------        -----------     -----------------------------
 *      PaulL           1st  Aug 02     Created
 *      JohnC           20th Aug 04     RT2561/2661 use scatter-gather scheme
 *      GertjanW        21st Jan 06     Baseline code
 *	LuisC (rt2500)  15th Feb 06     Added Yann's patch for radio hw
 *      MarkW (rt2500)  19th Feb 06     Promisc mode support
 * 	OlivierC	7th  May 07     firmware_class support
 * 	OlivierC	8th  May 07	Remove rt61sta.dat file support
 * 	OlivierC	20th May 07	WEP settings persist on ifup
 ***************************************************************************/

#include    "rt_config.h"

UCHAR BIT8[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
ULONG BIT32[] = { 0x00000001, 0x00000002, 0x00000004, 0x00000008,
	0x00000010, 0x00000020, 0x00000040, 0x00000080,
	0x00000100, 0x00000200, 0x00000400, 0x00000800,
	0x00001000, 0x00002000, 0x00004000, 0x00008000,
	0x00010000, 0x00020000, 0x00040000, 0x00080000,
	0x00100000, 0x00200000, 0x00400000, 0x00800000,
	0x01000000, 0x02000000, 0x04000000, 0x08000000,
	0x10000000, 0x20000000, 0x40000000, 0x80000000
};

char *CipherName[] =
    { "none", "wep64", "wep128", "TKIP", "AES", "CKIP64", "CKIP128" };

const unsigned short ccitt_16Table[] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
	0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
	0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
	0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
	0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
	0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
	0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
	0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
	0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
	0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
	0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
	0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
	0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
	0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
	0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
	0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
	0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
	0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
	0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
	0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
	0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
	0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
	0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
	0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
	0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

#define ByteCRC16(v, crc) \
    (unsigned short)((crc << 8) ^  ccitt_16Table[((crc >> 8) ^ (v)) & 255])

//
// BBP register initialization set
//
BBP_REG_PAIR BBPRegTable[] = {
	{3, 0x00},
	{15, 0x30},
	{17, 0x20},
	{21, 0xc8},
	{22, 0x38},
	{23, 0x06},
	{24, 0xfe},
	{25, 0x0a},
	{26, 0x0d},
	{34, 0x12},
	{37, 0x07},
	{39, 0xf8},
	{41, 0x60},
	{53, 0x10},
	{54, 0x18},
	{60, 0x10},
	{61, 0x04},
	{62, 0x04},
	{75, 0xfe},
	{86, 0xfe},
	{88, 0xfe},
	{90, 0x0f},
	{99, 0x00},
	{102, 0x16},
	{107, 0x04},
};

#define	NUM_BBP_REG_PARMS	(sizeof(BBPRegTable) / sizeof(BBP_REG_PAIR))

//
// ASIC register initialization sets
//
RTMP_REG_PAIR MACRegTable[] = {
//      {MAC_CSR11,             0x0000000a}, // 0x302c, power state transition time
//      {TXRX_CSR5,             0x0000000f}, // 0x3054, Basic rate set bitmap
	{TXRX_CSR0, 0x025fb032},	// 0x3040, RX control, default Disable RX
	{TXRX_CSR1, 0x9eaa9eaf},	// 0x3044, BBP 30:Ant-A RSSI, R51:Ant-B RSSI, R42:OFDM rate, R47:CCK SIGNAL
	{TXRX_CSR2, 0x8a8b8c8d},	// 0x3048, CCK TXD BBP registers
	{TXRX_CSR3, 0x00858687},	// 0x304c, OFDM TXD BBP registers
	{TXRX_CSR7, 0x2E31353B},	// 0x305c, ACK/CTS payload consume time for 18/12/9/6 mbps
	{TXRX_CSR8, 0x2a2a2a2c},	// 0x3060, ACK/CTS payload consume time for 54/48/36/24 mbps
	{TXRX_CSR15, 0x0000000f},	// 0x307c, TKIP MIC priority byte "AND" mask
	{MAC_CSR6, 0x00000fff},	// 0x3018, MAX frame length
	{MAC_CSR8, 0x016c030a},	// 0x3020, SIFS time for compensation
	{MAC_CSR10, 0x0000071c},	// 0x3028, ASIC PIN control in various power states
	{MAC_CSR12, 0x00000004},	// 0x3030, power state control, set to AWAKE state
	{MAC_CSR13, 0x0000e000},	// 0x3034, GPIO pin#5 Input as bHwRadio, otherwise as output.
//      {INT_SOURCE_CSR,0xffffffff}, // 0x3068, Clear all pending interrupt source
//      {MAC_CSR14,             0x00001e6}, // 0x3038, default both LEDs off
//      {PHY_CSR2,              0x82188200}, // 0x308c, pre-TX BBP control
//      {TXRX_CSR11,    0x0000e78f}, // 0x306c, AES, mask off more data bit for MIC calculation
//      {TX_DMA_DST_CSR,0x000000aa}, // 0x342c, ASIC TX FIFO to host shared memory mapping
	{SEC_CSR0, 0x00000000},	// 0x30a0, invalidate all shared key entries
	{SEC_CSR1, 0x00000000},	// 0x30a4, reset all shared key algorithm to "none"
	{SEC_CSR5, 0x00000000},	// 0x30b4, reset all shared key algorithm to "none"
	{PHY_CSR1, 0x000023b0},	// 0x3084, BBP Register R/W mode set to "Parallel mode"
	{PHY_CSR5, 0x060a100c},	// 0x00040a06
	{PHY_CSR6, 0x00080606},
	{PHY_CSR7, 0x00000a08},
	{PCI_CFG_CSR, 0x28ca4404},	// 0x3460, PCI configuration set to 0x28ca4404
	{AIFSN_CSR, 0x00002273},
	{CWMIN_CSR, 0x00002344},
	{CWMAX_CSR, 0x000034aa},
	{TEST_MODE_CSR, 0x00000200},	// 0x3484, Count TXOP anyway. by Mark 2005/07/27 for WMM S3T07 issue
	{M2H_CMD_DONE_CSR, 0xffffffff},	// 0x2104, clear M2H_CMD_DONE mailbox
};

#define	NUM_MAC_REG_PARMS	(sizeof(MACRegTable) / sizeof(RTMP_REG_PAIR))

#define FIRMWARE_MAJOR_VERSION	0
#define FIRMWARE_MINOR_VERSION	8
// ===================  End of Tables of FirmareImage   =================

/*
	========================================================================

	Routine Description:
		Allocate RTMP_ADAPTER data block and do some initialization

	Arguments:
		Adapter		Pointer to our adapter

	Return Value:
		NDIS_STATUS_SUCCESS
		NDIS_STATUS_FAILURE

	Note:

	========================================================================
*/
NDIS_STATUS RTMPAllocAdapterBlock(IN PRTMP_ADAPTER pAdapter)
{
	DBGPRINT(RT_DEBUG_TRACE, "--> RTMPAllocAdapterBlock\n");

	do {
		// Init spin locks
		spin_lock_init(&pAdapter->TxRingLock);
		spin_lock_init(&pAdapter->MgmtRingLock);
		spin_lock_init(&pAdapter->RxRingLock);
	} while (FALSE);

#ifdef RX_TASKLET
	// Init tasklet
	tasklet_init(&pAdapter->RxTasklet, RxProcessFrameQueue,
		     (unsigned long) pAdapter);
#endif
	DBGPRINT(RT_DEBUG_TRACE, "<-- RTMPAllocAdapterBlock\n");

	return (NDIS_STATUS_SUCCESS);
}

/*
    ========================================================================

    Routine Description:
        Allocate all DMA releated resources

    Arguments:
        Adapter         Pointer to our adapter

    Return Value:
        None

    Note:

    ========================================================================
*/
NDIS_STATUS RTMPAllocDMAMemory(IN PRTMP_ADAPTER pAdapter)
{
	ULONG RingBasePa;
	PVOID RingBaseVa;
	INT index, num;
	PTXD_STRUC pTxD;
	PRXD_STRUC pRxD;
	PRTMP_TX_RING pTxRing;
	PRTMP_DMABUF pDmaBuf;
	struct sk_buff *pSkb;
	NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

	DBGPRINT(RT_DEBUG_TRACE, "--> RTMPAllocDMAMemory\n");
	do {
		//
		// Allocate all ring descriptors, include TxD, RxD, MgmtD.
		// Although each size is different, to prevent cacheline and alignment
		// issue, I intentional set them all to 64 bytes.
		//
		for (num = 0; num < NUM_OF_TX_RING; num++) {
			ULONG BufBasePaLow;
			PVOID BufBaseVa;

			//
			// Allocate Tx ring descriptor's memory (5 TX rings = 4 ACs + 1 HCCA)
			//
			pAdapter->TxDescRing[num].AllocSize =
			    TX_RING_SIZE * TXD_SIZE;
			pAdapter->TxDescRing[num].AllocVa =
			    pci_alloc_consistent(pAdapter->pPci_Dev,
						 pAdapter->TxDescRing[num].
						 AllocSize,
						 &pAdapter->TxDescRing[num].
						 AllocPa);

			if (pAdapter->TxDescRing[num].AllocVa == NULL) {
				DBGPRINT(RT_DEBUG_ERROR,
					 "Failed to allocate a big buffer\n");
				Status = -ENOMEM;
				break;
			}
			// Zero init this memory block
			memset(pAdapter->TxDescRing[num].AllocVa, 0,
			       pAdapter->TxDescRing[num].AllocSize);

			// Save PA & VA for further operation
			RingBasePa = pAdapter->TxDescRing[num].AllocPa;
			RingBaseVa = pAdapter->TxDescRing[num].AllocVa;

			//
			// Allocate all 1st TXBuf's memory for this TxRing
			//
			pAdapter->TxBufSpace[num].AllocSize =
			    TX_RING_SIZE * TX_DMA_1ST_BUFFER_SIZE;
			pAdapter->TxBufSpace[num].AllocVa =
			    pci_alloc_consistent(pAdapter->pPci_Dev,
						 pAdapter->TxBufSpace[num].
						 AllocSize,
						 &pAdapter->TxBufSpace[num].
						 AllocPa);

			if (pAdapter->TxBufSpace[num].AllocVa == NULL) {
				DBGPRINT(RT_DEBUG_ERROR,
					 "Failed to allocate a big buffer\n");
				Status = -ENOMEM;
				break;
			}
			// Zero init this memory block
			memset(pAdapter->TxBufSpace[num].AllocVa, 0,
			       pAdapter->TxBufSpace[num].AllocSize);

			// Save PA & VA for further operation
			BufBasePaLow = pAdapter->TxBufSpace[num].AllocPa;
			BufBaseVa = pAdapter->TxBufSpace[num].AllocVa;

			//
			// Initialize Tx Ring Descriptor and associated buffer memory
			//
			pTxRing = &pAdapter->TxRing[num];
			for (index = 0; index < TX_RING_SIZE; index++) {
				// Init Tx Ring Size, Va, Pa variables
				pTxRing->Cell[index].AllocSize = TXD_SIZE;
				pTxRing->Cell[index].AllocVa = RingBaseVa;
				pTxRing->Cell[index].AllocPa = RingBasePa;

				// Setup Tx Buffer size & address. only 802.11 header will store in this space
				pDmaBuf = &pTxRing->Cell[index].DmaBuf;
				pDmaBuf->AllocSize = TX_DMA_1ST_BUFFER_SIZE;
				pDmaBuf->AllocVa = BufBaseVa;
				pDmaBuf->AllocPa = BufBasePaLow;

				// link the pre-allocated TxBuf to TXD
				pTxD =
				    (PTXD_STRUC) pTxRing->Cell[index].AllocVa;
				pTxD->BufCount = 1;
				pTxD->BufPhyAddr0 = BufBasePaLow;

#ifdef BIG_ENDIAN
				RTMPDescriptorEndianChange((PUCHAR) pTxD,
							   TYPE_TXD);
#endif
				// advance to next ring descriptor address
				RingBasePa += TXD_SIZE;
				RingBaseVa = (PUCHAR) RingBaseVa + TXD_SIZE;

				// advance to next TxBuf address
				BufBasePaLow += TX_DMA_1ST_BUFFER_SIZE;
				BufBaseVa =
				    (PUCHAR) BufBaseVa + TX_DMA_1ST_BUFFER_SIZE;
			}
			DBGPRINT(RT_DEBUG_TRACE,
				 "TxRing[%d]: total %d entry allocated\n", num,
				 index);
		}

		//
		// Allocate MGMT ring descriptor's memory except Tx ring which allocated eariler
		//
		pAdapter->MgmtDescRing.AllocSize = MGMT_RING_SIZE * TXD_SIZE;
		pAdapter->MgmtDescRing.AllocVa
		    =
		    pci_alloc_consistent(pAdapter->pPci_Dev,
					 pAdapter->MgmtDescRing.AllocSize,
					 &pAdapter->MgmtDescRing.AllocPa);

		if (pAdapter->MgmtDescRing.AllocVa == NULL) {
			DBGPRINT(RT_DEBUG_ERROR,
				 "Failed to allocate a big buffer\n");
			Status = -ENOMEM;
			break;
		}
		// Zero init this memory block
		memset(pAdapter->MgmtDescRing.AllocVa, 0,
		       pAdapter->MgmtDescRing.AllocSize);

		// Save PA & VA for further operation
		RingBasePa = pAdapter->MgmtDescRing.AllocPa;
		RingBaseVa = pAdapter->MgmtDescRing.AllocVa;

		//
		// Initialize MGMT Ring and associated buffer memory
		//
		for (index = 0; index < MGMT_RING_SIZE; index++) {
			// Init MGMT Ring Size, Va, Pa variables
			pAdapter->MgmtRing.Cell[index].AllocSize = TXD_SIZE;
			pAdapter->MgmtRing.Cell[index].AllocVa = RingBaseVa;
			pAdapter->MgmtRing.Cell[index].AllocPa = RingBasePa;

			// Offset to next ring descriptor address
			RingBasePa += TXD_SIZE;
			RingBaseVa = (PUCHAR) RingBaseVa + TXD_SIZE;

			// no pre-allocated buffer required in MgmtRing for scatter-gather case
		}
		DBGPRINT(RT_DEBUG_TRACE,
			 "MGMT Ring: total %d entry allocated\n", index);

		//
		// Allocate RX ring descriptor's memory except Tx ring which allocated eariler
		//
		pAdapter->RxDescRing.AllocSize = RX_RING_SIZE * RXD_SIZE;
		pAdapter->RxDescRing.AllocVa
		    =
		    pci_alloc_consistent(pAdapter->pPci_Dev,
					 pAdapter->RxDescRing.AllocSize,
					 &pAdapter->RxDescRing.AllocPa);

		if (pAdapter->RxDescRing.AllocVa == NULL) {
			DBGPRINT(RT_DEBUG_ERROR,
				 "Failed to allocate a big buffer\n");
			Status = -ENOMEM;
			break;
		}
		// Zero init this memory block
		memset(pAdapter->RxDescRing.AllocVa, 0,
		       pAdapter->RxDescRing.AllocSize);

		// Save PA & VA for further operation
		RingBasePa = pAdapter->RxDescRing.AllocPa;
		RingBaseVa = pAdapter->RxDescRing.AllocVa;

		//
		// Initialize Rx Ring and associated buffer memory
		//
		for (index = 0; index < RX_RING_SIZE; index++) {
			// Init RX Ring Size, Va, Pa variables
			pAdapter->RxRing.Cell[index].AllocSize = RXD_SIZE;
			pAdapter->RxRing.Cell[index].AllocVa = RingBaseVa;
			pAdapter->RxRing.Cell[index].AllocPa = RingBasePa;

			// Offset to next ring descriptor address
			RingBasePa += RXD_SIZE;
			RingBaseVa = (PUCHAR) RingBaseVa + RXD_SIZE;

			// Setup Rx associated Buffer size & allocate share memory
			pDmaBuf = &pAdapter->RxRing.Cell[index].DmaBuf;
			pDmaBuf->AllocSize = RX_DMA_BUFFER_SIZE;

			pSkb = __dev_alloc_skb(pDmaBuf->AllocSize, MEM_ALLOC_FLAG);
			if (pSkb == NULL) {
				Status = -ENOMEM;
				DBGPRINT(RT_DEBUG_ERROR,
					 "- %s: Failed to allocate RxRing skb %d\n",
					 __FUNCTION__, index);
				break;
			}
			RTMP_SET_PACKET_SOURCE(pSkb, PKTSRC_DRIVER);
			pDmaBuf->pSkb = pSkb;
			pDmaBuf->AllocVa = pSkb->data;
			pDmaBuf->AllocPa =
			    pci_map_single(pAdapter->pPci_Dev,
					   (PVOID) pSkb->data,
					   pDmaBuf->AllocSize,
					   PCI_DMA_FROMDEVICE);

			// Error handling
			if (pDmaBuf->AllocVa == NULL) {
				DBGPRINT(RT_DEBUG_ERROR,
					 "- %s: Failed to allocate RxRing buffer %d\n",
					 __FUNCTION__, index);
				Status = -ENOMEM;
				break;
			}
			// Zero init this memory block
			memset(pDmaBuf->AllocVa, 0, pDmaBuf->AllocSize);

			// Write RxD buffer address & allocated buffer length
			pRxD =
			    (PRXD_STRUC) pAdapter->RxRing.Cell[index].AllocVa;
			pRxD->BufPhyAddr = pDmaBuf->AllocPa;

			// Rx owner bit assign to NIC immediately
			pRxD->Owner = DESC_OWN_NIC;

#ifdef BIG_ENDIAN
			RTMPDescriptorEndianChange((PUCHAR) pRxD, TYPE_RXD);
#endif
		}
		DBGPRINT(RT_DEBUG_TRACE, "Rx Ring: total %d entry allocated\n",
			 index);

		/* Back out partially allocated resources. */
		if (Status != NDIS_STATUS_SUCCESS) {
			RTMPFreeDMAMemory(pAdapter);
			KPRINT(KERN_CRIT, "DMA alloc failed. Giving up!\n");
		}
	} while (FALSE);

	DBGPRINT(RT_DEBUG_TRACE, "<-- RTMPAllocDMAMemory (Status=%d)\n", Status);

	return Status;
}

/*
    ========================================================================

    Routine Description:
        Free all DMA memory.

    Arguments:
        Adapter         Pointer to our adapter

    Return Value:
        None

    Note:

    ========================================================================
*/
VOID RTMPFreeDMAMemory(IN PRTMP_ADAPTER pAdapter)
{
	INT index, num;

	DBGPRINT(RT_DEBUG_TRACE, "--> RTMPFreeDMAMemory\n");

	//
	// Free RX Ring related space
	//
	for (index = RX_RING_SIZE - 1; index >= 0; index--) {
		if ((pAdapter->RxRing.Cell[index].DmaBuf.AllocVa)
		    && (pAdapter->RxRing.Cell[index].DmaBuf.pSkb)) {
			pci_unmap_single(pAdapter->pPci_Dev,
					 pAdapter->RxRing.Cell[index].DmaBuf.
					 AllocPa,
					 pAdapter->RxRing.Cell[index].DmaBuf.
					 AllocSize, PCI_DMA_FROMDEVICE);
			RELEASE_NDIS_PACKET(pAdapter,
					    pAdapter->RxRing.Cell[index].DmaBuf.
					    pSkb);
		}
	}
	memset(pAdapter->RxRing.Cell, 0, RX_RING_SIZE * sizeof(RTMP_DMACB));

	if (pAdapter->RxDescRing.AllocVa) {
		pci_free_consistent(pAdapter->pPci_Dev,
				    pAdapter->RxDescRing.AllocSize,
				    pAdapter->RxDescRing.AllocVa,
				    pAdapter->RxDescRing.AllocPa);
	}
	memset(&pAdapter->RxDescRing, 0, sizeof(RTMP_DMABUF));

	//
	// Free MGMT Ring related space
	//
	if (pAdapter->MgmtDescRing.AllocVa) {
		pci_free_consistent(pAdapter->pPci_Dev,
				    pAdapter->MgmtDescRing.AllocSize,
				    pAdapter->MgmtDescRing.AllocVa,
				    pAdapter->MgmtDescRing.AllocPa);
	}
	memset(&pAdapter->MgmtDescRing, 0, sizeof(RTMP_DMABUF));

	//
	// Free TX Ring buffer
	//
	for (num = 0; num < NUM_OF_TX_RING; num++) {
		if (pAdapter->TxBufSpace[num].AllocVa) {
			pci_free_consistent(pAdapter->pPci_Dev,
					    pAdapter->TxBufSpace[num].AllocSize,
					    pAdapter->TxBufSpace[num].AllocVa,
					    pAdapter->TxBufSpace[num].AllocPa);
		}
		memset(&pAdapter->TxBufSpace[num], 0, sizeof(RTMP_DMABUF));

		if (pAdapter->TxDescRing[num].AllocVa) {
			pci_free_consistent(pAdapter->pPci_Dev,
					    pAdapter->TxDescRing[num].AllocSize,
					    pAdapter->TxDescRing[num].AllocVa,
					    pAdapter->TxDescRing[num].AllocPa);
		}
		memset(&pAdapter->TxDescRing[num], 0, sizeof(RTMP_DMABUF));
	}

	DBGPRINT(RT_DEBUG_TRACE, "<-- RTMPFreeDMAMemory\n");
}

// By removing 'inline' directive from the function definitions.
// Then Driverloader is compiled and runs smooth after kernel 2.6.9
#ifdef BIG_ENDIAN
inline VOID
#else
VOID
#endif
NICDisableInterrupt(IN PRTMP_ADAPTER pAdapter)
{
	RTMP_IO_WRITE32(pAdapter, INT_MASK_CSR, 0xffffff7f);	// 0xffffff7f
	RTMP_IO_WRITE32(pAdapter, MCU_INT_MASK_CSR, 0xffffffff);
	RTMP_CLEAR_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_ACTIVE);
}

#ifdef BIG_ENDIAN
inline VOID
#else
VOID
#endif
NICEnableInterrupt(IN PRTMP_ADAPTER pAdapter)
{
	//
	// Flag "fOP_STATUS_DOZE" On, means ASIC put to sleep, else means ASIC WakeUp
	// To prevent System hang, we should enalbe the interrupt when
	// ASIC is already Wake Up.
	//
	if (!OPSTATUS_TEST_FLAG(pAdapter, fOP_STATUS_DOZE)) {
		RTMP_IO_WRITE32(pAdapter, INT_MASK_CSR, 0x0000ff10);	// 0x0000ff00
	}
	RTMP_IO_WRITE32(pAdapter, MCU_INT_MASK_CSR, 0x00000000);	// 0x000001ff
	RTMP_SET_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_ACTIVE);
}

/*
	========================================================================

	Routine Description:
		Initialize transmit data structures

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		None

	Note:
		Initialize all transmit releated private buffer, include those define
		in RTMP_ADAPTER structure and all private data structures.

	========================================================================
*/
VOID NICInitTxRxRingAndBacklogQueue(IN PRTMP_ADAPTER pAdapter)
{
	DBGPRINT(RT_DEBUG_TRACE, "<--> NICInitTxRxRingAndBacklogQueue\n");

	// Initialize all transmit related software queues
	skb_queue_head_init(&pAdapter->TxSwQueue[QID_AC_BE]);
	skb_queue_head_init(&pAdapter->TxSwQueue[QID_AC_BK]);
	skb_queue_head_init(&pAdapter->TxSwQueue[QID_AC_VI]);
	skb_queue_head_init(&pAdapter->TxSwQueue[QID_AC_VO]);
	skb_queue_head_init(&pAdapter->TxSwQueue[QID_HCCA]);
#ifdef RX_TASKLET
	skb_queue_head_init(&pAdapter->RxQueue);
#endif

	// Init RX Ring index pointer
	pAdapter->RxRing.CurRxIndex = 0;

	// Init TX rings index pointer
	{
		INT i;
		for (i = 0; i < NUM_OF_TX_RING; i++) {
			pAdapter->TxRing[i].CurTxIndex = 0;
			pAdapter->TxRing[i].NextTxDmaDoneIndex = 0;
		}
	}

	// init MGMT ring index pointer
	pAdapter->MgmtRing.CurTxIndex = 0;
	pAdapter->MgmtRing.NextTxDmaDoneIndex = 0;

	pAdapter->PrivateInfo.TxRingFullCnt = 0;
}

/*
    ========================================================================

    Routine Description:
        Read initial parameters from EEPROM

    Arguments:
        Adapter                     Pointer to our adapter

    Return Value:
        None

    Note:

    ========================================================================
*/
VOID NICReadEEPROMParameters(IN PRTMP_ADAPTER pAd)
{
	ULONG data;
	USHORT i, value, value2;
	UCHAR TmpPhy;
	EEPROM_TX_PWR_STRUC Power;
	EEPROM_VERSION_STRUC Version;
	EEPROM_ANTENNA_STRUC Antenna;
	EEPROM_LED_STRUC LedSetting;
	USHORT Addr01, Addr23, Addr45;
	MAC_CSR2_STRUC csr2;
	MAC_CSR3_STRUC csr3;

	DBGPRINT(RT_DEBUG_TRACE, "--> NICReadEEPROMParameters\n");

	// Init EEPROM Address Number, before access EEPROM; if 93c46, EEPROMAddressNum=6, else if 93c66, EEPROMAddressNum=8
	RTMP_IO_READ32(pAd, E2PROM_CSR, &data);

	if (data & 0x20)
		pAd->EEPROMAddressNum = 6;	// 93C46
	else
		pAd->EEPROMAddressNum = 8;	// 93C66

	// RT2660 MAC no longer auto load MAC address from E2PROM. Driver has to intialize
	// MAC address registers according to E2PROM setting
	Addr01 = RTMP_EEPROM_READ16(pAd, 0x04);
	Addr23 = RTMP_EEPROM_READ16(pAd, 0x06);
	Addr45 = RTMP_EEPROM_READ16(pAd, 0x08);

	pAd->PermanentAddress[0] = (UCHAR) (Addr01 & 0xff);
	pAd->PermanentAddress[1] = (UCHAR) (Addr01 >> 8);
	pAd->PermanentAddress[2] = (UCHAR) (Addr23 & 0xff);
	pAd->PermanentAddress[3] = (UCHAR) (Addr23 >> 8);
	pAd->PermanentAddress[4] = (UCHAR) (Addr45 & 0xff);
	pAd->PermanentAddress[5] = (UCHAR) (Addr45 >> 8);

	if (pAd->bLocalAdminMAC == FALSE) {
		memcpy(pAd->CurrentAddress, pAd->PermanentAddress,
		       ETH_ALEN);
	}
	csr2.field.Byte0 = pAd->CurrentAddress[0];
	csr2.field.Byte1 = pAd->CurrentAddress[1];
	csr2.field.Byte2 = pAd->CurrentAddress[2];
	csr2.field.Byte3 = pAd->CurrentAddress[3];
	RTMP_IO_WRITE32(pAd, MAC_CSR2, csr2.word);
	csr3.word = 0;
	csr3.field.Byte4 = pAd->CurrentAddress[4];
	csr3.field.Byte5 = pAd->CurrentAddress[5];
	csr3.field.U2MeMask = 0xff;
	RTMP_IO_WRITE32(pAd, MAC_CSR3, csr3.word);

	DBGPRINT(RT_DEBUG_TRACE, "E2PROM: MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
		 pAd->PermanentAddress[0], pAd->PermanentAddress[1],
		 pAd->PermanentAddress[2], pAd->PermanentAddress[3],
		 pAd->PermanentAddress[4], pAd->PermanentAddress[5]);

	// Init the channel number for TX channel power
	// 0. 11b/g
	for (i = 0; i < 14; i++)
		pAd->TxPower[i].Channel = i + 1;
	// 1. UNI 36 - 64
	for (i = 0; i < 8; i++)
		pAd->TxPower[i + 14].Channel = 36 + i * 4;
	// 2. HipperLAN 2 100 - 140
	for (i = 0; i < 11; i++)
		pAd->TxPower[i + 22].Channel = 100 + i * 4;
	// 3. UNI 140 - 165
	for (i = 0; i < 5; i++)
		pAd->TxPower[i + 33].Channel = 149 + i * 4;
	// 34/38/42/46
	for (i = 0; i < 4; i++)
		pAd->TxPower[i + 38].Channel = 34 + i * 4;

	// if E2PROM version mismatch with driver's expectation, then skip
	// all subsequent E2RPOM retieval and set a system error bit to notify GUI
	Version.word = RTMP_EEPROM_READ16(pAd, EEPROM_VERSION_OFFSET);
	pAd->EepromVersion =
	    Version.field.Version + Version.field.FaeReleaseNumber * 256;
	DBGPRINT(RT_DEBUG_TRACE, "E2PROM: Version = %d, FAE release #%d\n",
		 Version.field.Version, Version.field.FaeReleaseNumber);

	if (Version.field.Version > VALID_EEPROM_VERSION) {
		DBGPRINT(RT_DEBUG_ERROR,
			 "E2PROM: WRONG VERSION %d, should be %d\n",
			 Version.field.Version, VALID_EEPROM_VERSION);
		pAd->SystemErrorBitmap |= 0x00000001;

		// hard-code default value when no proper E2PROM installed
		pAd->bAutoTxAgcA = FALSE;
		pAd->bAutoTxAgcG = FALSE;

		// Default the channel power
		for (i = 0; i < MAX_NUM_OF_CHANNELS; i++)
			pAd->TxPower[i].Power = DEFAULT_RF_TX_POWER;

		pAd->RfIcType = RFIC_5225;
		for (i = 0; i < NUM_EEPROM_BBP_PARMS; i++)
			pAd->EEPROMDefaultValue[i] = 0xffff;
		return;
	}
	// Read BBP default value from EEPROM and store to array(EEPROMDefaultValue) in pAd
	for (i = 0; i < NUM_EEPROM_BBP_PARMS; i++) {
		value = RTMP_EEPROM_READ16(pAd, EEPROM_BBP_BASE_OFFSET + i * 2);
		pAd->EEPROMDefaultValue[i] = value;
	}

	// We have to parse NIC configuration 0 at here.
	// If TSSI did not have preloaded value, it should reset the TxAutoAgc to false
	// Therefore, we have to read TxAutoAgc control beforehand.
	// Read Tx AGC control bit
	Antenna.word = pAd->EEPROMDefaultValue[0];
	if (Antenna.field.DynamicTxAgcControl == 1)
		pAd->bAutoTxAgcA = pAd->bAutoTxAgcG = TRUE;
	else
		pAd->bAutoTxAgcA = pAd->bAutoTxAgcG = FALSE;

	//
	// Reset PhyMode if we don't support 802.11a
	//
	if ((pAd->PortCfg.PhyMode == PHY_11ABG_MIXED)
	    || (pAd->PortCfg.PhyMode == PHY_11A)) {
		//
		// Only RFIC_5225 & RFIC_5325 support 802.11a
		//
		if ((Antenna.field.RfIcType != RFIC_5225)
		    && (Antenna.field.RfIcType != RFIC_5325))
			pAd->PortCfg.PhyMode = PHY_11BG_MIXED;
	}

	// Read Tx power value for all channels
	// Value from 1 - 0x7f. Default value is 24.
	// 0. 11b/g, ch1 - ch 14
	// Power value 0xFA (-6) ~ 0x24 (36)
	for (i = 0; i < 7; i++) {
		Power.word =
		    RTMP_EEPROM_READ16(pAd, EEPROM_G_TX_PWR_OFFSET + i * 2);

		if ((Power.field.Byte0 > 36))   //|| (Power.field.Byte0 < -6))
			pAd->TxPower[i * 2].Power = DEFAULT_RF_TX_POWER;
		else
			pAd->TxPower[i * 2].Power = Power.field.Byte0;

		if ((Power.field.Byte1 > 36))   //|| (Power.field.Byte1 < -6))
			pAd->TxPower[i * 2 + 1].Power = DEFAULT_RF_TX_POWER;
		else
			pAd->TxPower[i * 2 + 1].Power = Power.field.Byte1;
	}
	// 1. UNI 36 - 64, HipperLAN 2 100 - 140, UNI 140 - 165
	// Power value 0xFA (-6) ~ 0x24 (36)
	for (i = 0; i < 12; i++) {
		Power.word =
		    RTMP_EEPROM_READ16(pAd, EEPROM_A_TX_PWR_OFFSET + i * 2);

		if ((Power.field.Byte0 > 36))   //|| (Power.field.Byte0 < -6))
			pAd->TxPower[i * 2 + 14].Power = DEFAULT_RF_TX_POWER;
		else
			pAd->TxPower[i * 2 + 14].Power = Power.field.Byte0;

		if ((Power.field.Byte1 > 36))   //|| (Power.field.Byte1 < -6))
			pAd->TxPower[i * 2 + 15].Power = DEFAULT_RF_TX_POWER;
		else
			pAd->TxPower[i * 2 + 15].Power = Power.field.Byte1;
	}

	// for J52, 34/38/42/46
	Power.word = RTMP_EEPROM_READ16(pAd, EEPROM_J52_TX_PWR_OFFSET);
	//
	// Start from 0x7d, skip low byte.
	//
	ASSERT(pAd->TxPower[J52_CHANNEL_START_OFFSET].Channel == 34);
	if ((Power.field.Byte0 > 36))   //|| (Power.field.Byte0 < -6))
		pAd->TxPower[J52_CHANNEL_START_OFFSET].Power =
		    DEFAULT_RF_TX_POWER;
	else
		pAd->TxPower[J52_CHANNEL_START_OFFSET].Power =
		    Power.field.Byte1;

	Power.word = RTMP_EEPROM_READ16(pAd, EEPROM_J52_TX_PWR_OFFSET + 2);
	ASSERT(pAd->TxPower[J52_CHANNEL_START_OFFSET + 1].Channel == 38);
	if ((Power.field.Byte0 > 36))   //|| (Power.field.Byte0 < -6))
		pAd->TxPower[J52_CHANNEL_START_OFFSET + 1].Power =
		    DEFAULT_RF_TX_POWER;
	else
		pAd->TxPower[J52_CHANNEL_START_OFFSET + 1].Power =
		    Power.field.Byte0;

	ASSERT(pAd->TxPower[J52_CHANNEL_START_OFFSET + 2].Channel == 42);
	if ((Power.field.Byte0 > 36))   //|| (Power.field.Byte0 < -6))
		pAd->TxPower[J52_CHANNEL_START_OFFSET + 2].Power =
		    DEFAULT_RF_TX_POWER;
	else
		pAd->TxPower[J52_CHANNEL_START_OFFSET + 2].Power =
		    Power.field.Byte1;

	Power.word = RTMP_EEPROM_READ16(pAd, EEPROM_J52_TX_PWR_OFFSET + 4);
	ASSERT(pAd->TxPower[J52_CHANNEL_START_OFFSET + 3].Channel == 46);
	if ((Power.field.Byte0 > 36))   //|| (Power.field.Byte0 < -6))
		pAd->TxPower[J52_CHANNEL_START_OFFSET + 3].Power =
		    DEFAULT_RF_TX_POWER;
	else
		pAd->TxPower[J52_CHANNEL_START_OFFSET + 3].Power =
		    Power.field.Byte0;

	// Read TSSI reference and TSSI boundary for temperature compensation. This is ugly
	// 0. 11b/g
	Power.word = RTMP_EEPROM_READ16(pAd, 0x54);
	pAd->TssiMinusBoundaryG[4] = Power.field.Byte0;
	pAd->TssiMinusBoundaryG[3] = Power.field.Byte1;
	Power.word = RTMP_EEPROM_READ16(pAd, 0x56);
	pAd->TssiMinusBoundaryG[2] = Power.field.Byte0;
	pAd->TssiMinusBoundaryG[1] = Power.field.Byte1;
	Power.word = RTMP_EEPROM_READ16(pAd, 0x58);
	pAd->TssiPlusBoundaryG[1] = Power.field.Byte0;
	pAd->TssiPlusBoundaryG[2] = Power.field.Byte1;
	Power.word = RTMP_EEPROM_READ16(pAd, 0x5a);
	pAd->TssiPlusBoundaryG[3] = Power.field.Byte0;
	pAd->TssiPlusBoundaryG[4] = Power.field.Byte1;
	Power.word = RTMP_EEPROM_READ16(pAd, 0x5c);
	pAd->TssiRefG = Power.field.Byte0;
	pAd->TxAgcStepG = Power.field.Byte1;
	pAd->TxAgcCompensateG = 0;
	pAd->TssiMinusBoundaryG[0] = pAd->TssiRefG;
	pAd->TssiPlusBoundaryG[0] = pAd->TssiRefG;

	// Disable TxAgc if the based value is not right
	if (pAd->TssiRefG == 0xff)
		pAd->bAutoTxAgcG = FALSE;

	DBGPRINT(RT_DEBUG_TRACE,
		 "E2PROM: G Tssi[-4 .. +4] = %d %d %d %d - %d -%d %d %d %d, step=%d, tuning=%d\n",
		 pAd->TssiMinusBoundaryG[4], pAd->TssiMinusBoundaryG[3],
		 pAd->TssiMinusBoundaryG[2], pAd->TssiMinusBoundaryG[1],
		 pAd->TssiRefG, pAd->TssiPlusBoundaryG[1],
		 pAd->TssiPlusBoundaryG[2], pAd->TssiPlusBoundaryG[3],
		 pAd->TssiPlusBoundaryG[4], pAd->TxAgcStepG,
		 pAd->bAutoTxAgcG);
	// 1. 11a
	Power.word = RTMP_EEPROM_READ16(pAd, 0x90);
	pAd->TssiMinusBoundaryA[4] = Power.field.Byte0;
	pAd->TssiMinusBoundaryA[3] = Power.field.Byte1;
	Power.word = RTMP_EEPROM_READ16(pAd, 0x92);
	pAd->TssiMinusBoundaryA[2] = Power.field.Byte0;
	pAd->TssiMinusBoundaryA[1] = Power.field.Byte1;
	Power.word = RTMP_EEPROM_READ16(pAd, 0x94);
	pAd->TssiPlusBoundaryA[1] = Power.field.Byte0;
	pAd->TssiPlusBoundaryA[2] = Power.field.Byte1;
	Power.word = RTMP_EEPROM_READ16(pAd, 0x96);
	pAd->TssiPlusBoundaryA[3] = Power.field.Byte0;
	pAd->TssiPlusBoundaryA[4] = Power.field.Byte1;
	Power.word = RTMP_EEPROM_READ16(pAd, 0x98);
	pAd->TssiRefA = Power.field.Byte0;
	pAd->TxAgcStepA = Power.field.Byte1;
	pAd->TxAgcCompensateA = 0;
	pAd->TssiMinusBoundaryA[0] = pAd->TssiRefA;
	pAd->TssiPlusBoundaryA[0] = pAd->TssiRefA;

	// Disable TxAgc if the based value is not right
	if (pAd->TssiRefA == 0xff)
		pAd->bAutoTxAgcA = FALSE;

	DBGPRINT(RT_DEBUG_TRACE,
		 "E2PROM: A Tssi[-4 .. +4] = %d %d %d %d - %d -%d %d %d %d, step=%d, tuning=%d\n",
		 pAd->TssiMinusBoundaryA[4], pAd->TssiMinusBoundaryA[3],
		 pAd->TssiMinusBoundaryA[2], pAd->TssiMinusBoundaryA[1],
		 pAd->TssiRefA, pAd->TssiPlusBoundaryA[1],
		 pAd->TssiPlusBoundaryA[2], pAd->TssiPlusBoundaryA[3],
		 pAd->TssiPlusBoundaryA[4], pAd->TxAgcStepA,
		 pAd->bAutoTxAgcA);
	pAd->BbpRssiToDbmDelta = 0x79;

	// Read frequency offset and RF programming sequence setting for RT5225
	value = RTMP_EEPROM_READ16(pAd, EEPROM_FREQ_OFFSET);
	if ((value & 0xFF00) == 0xFF00) {
		pAd->RFProgSeq = 0;
	} else {
		pAd->RFProgSeq = (value & 0x0300) >> 8;	// bit 8,9
	}

	value &= 0x00FF;
	if (value != 0x00FF)
		pAd->RfFreqOffset = (ULONG) value;
	else
		pAd->RfFreqOffset = 0;
	DBGPRINT(RT_DEBUG_TRACE,
		 "E2PROM: RF freq offset=0x%x, RF programming seq=%d\n",
		 pAd->RfFreqOffset, pAd->RFProgSeq);

	//CountryRegion byte offset = 0x25
	value = pAd->EEPROMDefaultValue[2] >> 8;
	value2 = pAd->EEPROMDefaultValue[2] & 0x00FF;
	if ((value <= REGION_MAXIMUM_BG_BAND)
	    && (value2 <= REGION_MAXIMUM_A_BAND)) {
		pAd->PortCfg.CountryRegion = ((UCHAR) value) | 0x80;
		pAd->PortCfg.CountryRegionForABand = ((UCHAR) value2) | 0x80;
		TmpPhy = pAd->PortCfg.PhyMode;
		pAd->PortCfg.PhyMode = 0xff;
		RTMPSetPhyMode(pAd, TmpPhy);
	}
	//
	// Get RSSI Offset on EEPROM 0x9Ah & 0x9Ch.
	// The valid value are (-10 ~ 10)
	//
	value = RTMP_EEPROM_READ16(pAd, EEPROM_RSSI_BG_OFFSET);
	pAd->BGRssiOffset1 = value & 0x00ff;
	pAd->BGRssiOffset2 = (value >> 8);

	// Validate 11b/g RSSI_1 offset.
	if ((pAd->BGRssiOffset1 < -10) || (pAd->BGRssiOffset1 > 10))
		pAd->BGRssiOffset1 = 0;

	// Validate 11b/g RSSI_2 offset.
	if ((pAd->BGRssiOffset2 < -10) || (pAd->BGRssiOffset2 > 10))
		pAd->BGRssiOffset2 = 0;

	value = RTMP_EEPROM_READ16(pAd, EEPROM_RSSI_A_OFFSET);
	pAd->ARssiOffset1 = value & 0x00ff;
	pAd->ARssiOffset2 = (value >> 8);

	// Validate 11a RSSI_1 offset.
	if ((pAd->ARssiOffset1 < -10) || (pAd->ARssiOffset1 > 10))
		pAd->ARssiOffset1 = 0;

	//Validate 11a RSSI_2 offset.
	if ((pAd->ARssiOffset2 < -10) || (pAd->ARssiOffset2 > 10))
		pAd->ARssiOffset2 = 0;

	//
	// Get LED Setting.
	//
	LedSetting.word = RTMP_EEPROM_READ16(pAd, EEPROM_LED_OFFSET);
	if (LedSetting.word == 0xFFFF) {
		//
		// Set it to Default.
		//
		LedSetting.field.PolarityRDY_G = 1;	// Active High.
		LedSetting.field.PolarityRDY_A = 1;	// Active High.
		LedSetting.field.PolarityACT = 1;	// Active High.
		LedSetting.field.PolarityGPIO_0 = 1;	// Active High.
		LedSetting.field.PolarityGPIO_1 = 1;	// Active High.
		LedSetting.field.PolarityGPIO_2 = 1;	// Active High.
		LedSetting.field.PolarityGPIO_3 = 1;	// Active High.
		LedSetting.field.PolarityGPIO_4 = 1;	// Active High.
		LedSetting.field.LedMode = LED_MODE_DEFAULT;
	}
	pAd->LedCntl.word = 0;
	pAd->LedCntl.field.LedMode = LedSetting.field.LedMode;
	pAd->LedCntl.field.PolarityRDY_G = LedSetting.field.PolarityRDY_G;
	pAd->LedCntl.field.PolarityRDY_A = LedSetting.field.PolarityRDY_A;
	pAd->LedCntl.field.PolarityACT = LedSetting.field.PolarityACT;
	pAd->LedCntl.field.PolarityGPIO_0 = LedSetting.field.PolarityGPIO_0;
	pAd->LedCntl.field.PolarityGPIO_1 = LedSetting.field.PolarityGPIO_1;
	pAd->LedCntl.field.PolarityGPIO_2 = LedSetting.field.PolarityGPIO_2;
	pAd->LedCntl.field.PolarityGPIO_3 = LedSetting.field.PolarityGPIO_3;
	pAd->LedCntl.field.PolarityGPIO_4 = LedSetting.field.PolarityGPIO_4;

	value = RTMP_EEPROM_READ16(pAd, EEPROM_TXPOWER_DELTA_OFFSET);
	value = value & 0x00ff;
	if (value != 0xff) {
		pAd->TxPowerDeltaConfig.value = (UCHAR) value;
		if (pAd->TxPowerDeltaConfig.field.DeltaValue > 0x04)
			pAd->TxPowerDeltaConfig.field.DeltaValue = 0x04;
	} else
		pAd->TxPowerDeltaConfig.field.TxPowerEnable = FALSE;

	DBGPRINT(RT_DEBUG_TRACE, "<-- NICReadEEPROMParameters\n");
}

/*
    ========================================================================

    Routine Description:
        Set default value from EEPROM

    Arguments:
        Adapter                     Pointer to our adapter

    Return Value:
        None

    Note:

    ========================================================================
*/
VOID NICInitAsicFromEEPROM(IN PRTMP_ADAPTER pAd)
{
	ULONG data;
	USHORT i;
	EEPROM_ANTENNA_STRUC Antenna;
	EEPROM_NIC_CONFIG2_STRUC NicConfig2;

	DBGPRINT(RT_DEBUG_TRACE, "--> NICInitAsicFromEEPROM\n");

	for (i = 3; i < NUM_EEPROM_BBP_PARMS; i++) {
		UCHAR BbpRegIdx, BbpValue;

		if ((pAd->EEPROMDefaultValue[i] != 0xFFFF)
		    && (pAd->EEPROMDefaultValue[i] != 0)) {
			BbpRegIdx = (UCHAR) (pAd->EEPROMDefaultValue[i] >> 8);
			BbpValue = (UCHAR) (pAd->EEPROMDefaultValue[i] & 0xff);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BbpRegIdx, BbpValue);
		}
	}

	Antenna.word = pAd->EEPROMDefaultValue[0];

	if (Antenna.word == 0xFFFF) {
		Antenna.word = 0;
		Antenna.field.RfIcType = RFIC_5225;
		Antenna.field.HardwareRadioControl = 0;	// no hardware control
		Antenna.field.DynamicTxAgcControl = 0;
		Antenna.field.RxDefaultAntenna = 2;	// Ant-B
		Antenna.field.TxDefaultAntenna = 2;	// Ant-B
		Antenna.field.NumOfAntenna = 2;
		DBGPRINT(RT_DEBUG_TRACE,
			 "E2PROM, Antenna parameter error, hard code as 0x%04x\n",
			 Antenna.word);
	}

	pAd->RfIcType = (UCHAR) Antenna.field.RfIcType;
	printk("RT61: RfIcType= %d\n", pAd->RfIcType);

	// Save the antenna for future use
	pAd->Antenna.word = Antenna.word;

	// Read Hardware controlled Radio state enable bit
	if (0 && Antenna.field.HardwareRadioControl == 1) {
		pAd->PortCfg.bHardwareRadio = TRUE;

		// Read GPIO pin5 as Hardware controlled radio state
		RTMP_IO_READ32(pAd, MAC_CSR13, &data);
		if ((data & 0x20) == 0) {
			pAd->PortCfg.bHardwareRadio = FALSE;
			pAd->PortCfg.bRadio = FALSE;
			RTMP_IO_WRITE32(pAd, MAC_CSR10, 0x00001818);
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);
		}
	} else
		pAd->PortCfg.bHardwareRadio = FALSE;

	if (pAd->PortCfg.bRadio == FALSE) {
		RTMPSetLED(pAd, LED_RADIO_OFF);
	} else {
		RTMPSetLED(pAd, LED_RADIO_ON);
	}

	NicConfig2.word = pAd->EEPROMDefaultValue[1];
	if (NicConfig2.word == 0xffff) {
		NicConfig2.word = 0;
	}
	// Save the antenna for future use
	pAd->NicConfig2.word = NicConfig2.word;

	//
	// Since BBP has been progamed, to make sure BBP setting will be
	// upate inside of AsicAntennaSelect, so reset to UNKNOWN_BAND!!
	//
	pAd->PortCfg.BandState = UNKNOWN_BAND;
	DBGPRINT(RT_DEBUG_TRACE,
		 "Use Hw Radio Control Pin=%d; if used Pin=%d;\n",
		 pAd->PortCfg.bHardwareRadio, pAd->PortCfg.bHardwareRadio);

	DBGPRINT(RT_DEBUG_TRACE, "RFIC=%d, LED mode=%d\n", pAd->RfIcType,
		 pAd->LedCntl.field.LedMode);

	DBGPRINT(RT_DEBUG_TRACE, "<-- NICInitAsicFromEEPROM\n");
}


// Unify all delay routine by using udelay
VOID RTMPusecDelay(IN ULONG usec)
{
	ULONG i;

	for (i = 0; i < (usec / 50); i++)
		udelay(50);

	if (usec % 50)
		udelay(usec % 50);
}


static VOID NICInitializeAsic(IN PRTMP_ADAPTER pAdapter)
{
	ULONG Index, Counter;
	UCHAR Value = 0xff;
	ULONG MacCsr12;

	DBGPRINT(RT_DEBUG_TRACE, "--> NICInitializeAsic\n");

	// Initialize MAC register to default value
	for (Index = 0; Index < NUM_MAC_REG_PARMS; Index++) {
		RTMP_IO_WRITE32(pAdapter, MACRegTable[Index].Register,
				MACRegTable[Index].Value);
	}

	// Set Host ready before kicking Rx
//      RTMP_IO_WRITE32(pAdapter, MAC_CSR1, 0x1); // reset MAC state machine, requested by Kevin 2003-2-11
	RTMP_IO_WRITE32(pAdapter, MAC_CSR1, 0x3);
	RTMP_IO_WRITE32(pAdapter, MAC_CSR1, 0x0);

	//
	// Before program BBP, we need to wait BBP/RF get wake up.
	//
	Index = 0;
	do {
		RTMP_IO_READ32(pAdapter, MAC_CSR12, &MacCsr12);

		if (MacCsr12 & 0x08)
			break;

		RTMPusecDelay(1000);
	} while (Index++ < 1000);

	// Read BBP register, make sure BBP is up and running before write new data
	Index = 0;
	do {
		RTMP_BBP_IO_READ8_BY_REG_ID(pAdapter, BBP_R0, &Value);
		DBGPRINT(RT_DEBUG_TRACE, "BBP version = %d\n", Value);
	} while ((++Index < 100) && ((Value == 0xff) || (Value == 0x00)));
	//ASSERT(Index < 20);   //this will cause BSOD on Check-build driver

	// Initialize BBP register to default value
	for (Index = 0; Index < NUM_BBP_REG_PARMS; Index++) {
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAdapter,
					     BBPRegTable[Index].Register,
					     BBPRegTable[Index].Value);
	}

	// Add radio off control
	if (pAdapter->PortCfg.bRadio == FALSE) {
		RTMP_IO_WRITE32(pAdapter, MAC_CSR10, 0x00001818);
		RTMP_SET_FLAG(pAdapter, fRTMP_ADAPTER_RADIO_OFF);
	}
	// Kick Rx
	//RTMP_IO_WRITE32(pAdapter, RX_CNTL_CSR, 0x00000001);     // enable RX of DMA block

	// This delay is needed when ATE(RXFRAME) turn on
	RTMPusecDelay(10);

	RTMPWriteTXRXCsr0(pAdapter, FALSE, TRUE);

	// Clear raw counters
	RTMP_IO_READ32(pAdapter, STA_CSR0, &Counter);
	RTMP_IO_READ32(pAdapter, STA_CSR1, &Counter);
	RTMP_IO_READ32(pAdapter, STA_CSR2, &Counter);

	// assert HOST ready bit
//  RTMP_IO_WRITE32(pAdapter, MAC_CSR1, 0x0); // 2004-09-14 asked by Mark
//  RTMP_IO_WRITE32(pAdapter, MAC_CSR1, 0x4);

	RTMP_IO_WRITE32(pAdapter, MAC_CSR1, 0x4);

	OPSTATUS_CLEAR_FLAG(pAdapter, fOP_STATUS_MEDIA_STATE_CONNECTED);

	DBGPRINT(RT_DEBUG_TRACE, "<-- NICInitializeAsic\n");
}

NDIS_STATUS NICInitializeAdapter(IN PRTMP_ADAPTER pAdapter)
{
	TX_RING_CSR0_STRUC TxCsr0;
	TX_RING_CSR1_STRUC TxCsr1;
	RX_RING_CSR_STRUC RxCsr;
	ULONG Value;
	NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

	DBGPRINT(RT_DEBUG_TRACE, "--> NICInitializeAdapter\n");

	//
	// write all shared Ring's base address into ASIC
	//

	// Write AC_BK base address register
	Value = pAdapter->TxRing[QID_AC_BK].Cell[0].AllocPa;
	RTMP_IO_WRITE32(pAdapter, AC1_BASE_CSR, Value);

	// Write AC_BE base address register
	Value = pAdapter->TxRing[QID_AC_BE].Cell[0].AllocPa;
	RTMP_IO_WRITE32(pAdapter, AC0_BASE_CSR, Value);

	// Write AC_VI base address register
	Value = pAdapter->TxRing[QID_AC_VI].Cell[0].AllocPa;
	RTMP_IO_WRITE32(pAdapter, AC2_BASE_CSR, Value);

	// Write AC_VO base address register
	Value = pAdapter->TxRing[QID_AC_VO].Cell[0].AllocPa;
	RTMP_IO_WRITE32(pAdapter, AC3_BASE_CSR, Value);

	// Write HCCA base address register
	//  Value = pci_map_single(pAdapter->pPci_Dev, pAdapter->TxRing[QID_HCCA].Cell[0].AllocVa,
	//                         pAdapter->TxRing[QID_HCCA].Cell[0].AllocSize, PCI_DMA_TODEVICE);
	//  RTMP_IO_WRITE32(pAdapter, HCCA_BASE_CSR, Value);

	// Write MGMT_BASE_CSR register
	Value = pAdapter->MgmtRing.Cell[0].AllocPa;
	RTMP_IO_WRITE32(pAdapter, MGMT_BASE_CSR, Value);

	// Write RX_BASE_CSR register
	Value = pAdapter->RxRing.Cell[0].AllocPa;
	RTMP_IO_WRITE32(pAdapter, RX_BASE_CSR, Value);

	//
	// set each Ring's SIZE and DESCRIPTOR size into ASIC
	//

	// Write TX_RING_CSR0 register
	TxCsr0.word = 0;
	TxCsr0.field.Ac0Total = TX_RING_SIZE;
	TxCsr0.field.Ac1Total = TX_RING_SIZE;
	TxCsr0.field.Ac2Total = TX_RING_SIZE;
	TxCsr0.field.Ac3Total = TX_RING_SIZE;
	RTMP_IO_WRITE32(pAdapter, TX_RING_CSR0, TxCsr0.word);

	// Write TX_RING_CSR1 register
	TxCsr1.word = 0;
	TxCsr1.field.TxdSize = TXD_SIZE / 4;
	TxCsr1.field.HccaTotal = TX_RING_SIZE;
	TxCsr1.field.MgmtTotal = MGMT_RING_SIZE;
	RTMP_IO_WRITE32(pAdapter, TX_RING_CSR1, TxCsr1.word);

	// Write RX_RING_CSR register
	RxCsr.word = 0;
	RxCsr.field.RxdSize = RXD_SIZE / 4;
	RxCsr.field.RxRingTotal = RX_RING_SIZE;
	RxCsr.field.RxdWritebackSize = 4;
	RTMP_IO_WRITE32(pAdapter, RX_RING_CSR, RxCsr.word);

	//
	// Load shared memeory configuration to ASIC DMA block
	//

	// 0x342c, ASIC TX FIFO to host shared memory mapping
	RTMP_IO_WRITE32(pAdapter, TX_DMA_DST_CSR, 0x000000aa);

	// ASIC load all TX ring's base address
	RTMP_IO_WRITE32(pAdapter, LOAD_TX_RING_CSR, 0x1f);

	// ASIC load RX ring's base address. still disable RX
	RTMP_IO_WRITE32(pAdapter, RX_CNTL_CSR, 0x00000002);

	// Initialze ASIC for TX & Rx operation
	NICInitializeAsic(pAdapter);

	DBGPRINT(RT_DEBUG_TRACE, "<-- NICInitializeAdapter\n");
	return Status;
}

/*
    ========================================================================

    Routine Description:
        Reset NIC Asics

    Arguments:
        Adapter                     Pointer to our adapter

    Return Value:
        None

    Note:
        Reset NIC to initial state AS IS system boot up time.

    ========================================================================
*/
VOID NICIssueReset(IN PRTMP_ADAPTER pAdapter)
{
	MAC_CSR2_STRUC StaMacReg0;
	MAC_CSR3_STRUC StaMacReg1;

	DBGPRINT(RT_DEBUG_TRACE, "--> NICIssueReset\n");

	// Abort Tx, prevent ASIC from writing to Host memory
	RTMP_IO_WRITE32(pAdapter, TX_CNTL_CSR, 0x001f0000);

	RTMPWriteTXRXCsr0(pAdapter, TRUE, FALSE);

	if (pAdapter->bLocalAdminMAC) {
		// Write Back Permanent MAC address to CSR3 & CSR4
		StaMacReg0.field.Byte0 = pAdapter->PermanentAddress[0];
		StaMacReg0.field.Byte1 = pAdapter->PermanentAddress[1];
		StaMacReg0.field.Byte2 = pAdapter->PermanentAddress[2];
		StaMacReg0.field.Byte3 = pAdapter->PermanentAddress[3];
		StaMacReg1.word = 0;
		StaMacReg1.field.Byte4 = pAdapter->PermanentAddress[4];
		StaMacReg1.field.Byte5 = pAdapter->PermanentAddress[5];
		StaMacReg1.field.U2MeMask = 0xff;
		RTMP_IO_WRITE32(pAdapter, MAC_CSR2, StaMacReg0.word);
		RTMP_IO_WRITE32(pAdapter, MAC_CSR3, StaMacReg1.word);
	}
	// Issue reset and clear from reset state
	RTMP_IO_WRITE32(pAdapter, MAC_CSR1, 0x03);	// 2004-09-17 change from 0x01
	RTMP_IO_WRITE32(pAdapter, MAC_CSR1, 0x00);

	DBGPRINT(RT_DEBUG_TRACE, "<-- NICIssueReset\n");
}

/*
    ========================================================================

    Routine Description:
        Check ASIC registers and find any reason the system might hang

    Arguments:
        Adapter                     Pointer to our adapter

    Return Value:
        None

    Note:

    ========================================================================
*/
#if 0
static BOOLEAN NICCheckForHang(IN PRTMP_ADAPTER pAd)
{
	return (FALSE);
}
#endif
/*
	========================================================================

	Routine Description:
		Read statistical counters from hardware registers and record them
		in software variables for later on query

	Arguments:
		pAdapter					Pointer to our adapter

	Return Value:
		None

	========================================================================
*/
VOID NICUpdateRawCounters(IN PRTMP_ADAPTER pAdapter)
{
	STA_CSR0_STRUC StaCsr0;
	STA_CSR1_STRUC StaCsr1;
	STA_CSR2_STRUC StaCsr2;
	STA_CSR3_STRUC StaCsr3;
	ULONG OldValue;

	RTMP_IO_READ32(pAdapter, STA_CSR0, &StaCsr0.word);

	// Update RX PLCP error counter
	pAdapter->PrivateInfo.PhyRxErrCnt += StaCsr0.field.PlcpErr;

	// Update FCS counters
	OldValue = pAdapter->WlanCounters.FCSErrorCount.vv.LowPart;
	pAdapter->WlanCounters.FCSErrorCount.vv.LowPart += (StaCsr0.field.CrcErr);	// >> 7);
	if (pAdapter->WlanCounters.FCSErrorCount.vv.LowPart < OldValue)
		pAdapter->WlanCounters.FCSErrorCount.vv.HighPart++;

	// Add FCS error count to private counters
	pAdapter->RalinkCounters.OneSecRxFcsErrCnt += StaCsr0.field.CrcErr;
	OldValue = pAdapter->RalinkCounters.RealFcsErrCount.vv.LowPart;
	pAdapter->RalinkCounters.RealFcsErrCount.vv.LowPart +=
	    StaCsr0.field.CrcErr;
	if (pAdapter->RalinkCounters.RealFcsErrCount.vv.LowPart < OldValue)
		pAdapter->RalinkCounters.RealFcsErrCount.vv.HighPart++;

	// Update False CCA counter
	RTMP_IO_READ32(pAdapter, STA_CSR1, &StaCsr1.word);
	pAdapter->RalinkCounters.OneSecFalseCCACnt += StaCsr1.field.FalseCca;
	DBGPRINT(RT_DEBUG_INFO, "OneSecFalseCCACnt = %d\n",
		 pAdapter->RalinkCounters.OneSecFalseCCACnt);

	// Update RX Overflow counter
	RTMP_IO_READ32(pAdapter, STA_CSR2, &StaCsr2.word);
	pAdapter->Counters8023.RxNoBuffer +=
	    (StaCsr2.field.RxOverflowCount + StaCsr2.field.RxFifoOverflowCount);

	// Update BEACON sent count
	RTMP_IO_READ32(pAdapter, STA_CSR3, &StaCsr3.word);
	pAdapter->RalinkCounters.OneSecBeaconSentCnt +=
	    StaCsr3.field.TxBeaconCount;
}

/*
    ========================================================================

    Routine Description:
        Reset NIC from error

    Arguments:
        Adapter                     Pointer to our adapter

    Return Value:
        None

    Note:
        Reset NIC from error state

    ========================================================================
*/
VOID NICResetFromError(IN PRTMP_ADAPTER pAdapter)
{
	// Reset BBP (according to alex, reset ASIC will force reset BBP
	// Therefore, skip the reset BBP
	// RTMP_IO_WRITE32(pAdapter, MAC_CSR1, 0x2);
	// Release BBP reset
	// RTMP_IO_WRITE32(pAdapter, MAC_CSR1, 0x0);

	RTMP_IO_WRITE32(pAdapter, MAC_CSR1, 0x1);
	// Remove ASIC from reset state
	RTMP_IO_WRITE32(pAdapter, MAC_CSR1, 0x0);

	// Init send data structures and related parameters
	NICInitTxRxRingAndBacklogQueue(pAdapter);

	NICInitializeAdapter(pAdapter);
	NICInitAsicFromEEPROM(pAdapter);

	// Switch to current channel, since during reset process, the connection should remains on.
	AsicSwitchChannel(pAdapter, pAdapter->PortCfg.Channel);
	AsicLockChannel(pAdapter, pAdapter->PortCfg.Channel);
}

/*
	========================================================================

	Routine Description:
		Load 8051 firmware RT2561.BIN file into MAC ASIC

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		NDIS_STATUS_SUCCESS         firmware image load ok
		NDIS_STATUS_FAILURE         image not found

	========================================================================
*/
NDIS_STATUS NICLoadFirmware(IN PRTMP_ADAPTER pAd)
{
	const struct firmware *fw_entry;
	char *fw_name;
	size_t size;
	PUCHAR ptr;
	USHORT crc;
	INT i;
	ULONG MacReg;
	NDIS_STATUS Status = NDIS_STATUS_FAILURE;
	struct pci_dev *dev = pAd->pPci_Dev;

	DBGPRINT(RT_DEBUG_TRACE, "===> NICLoadFirmware\n");

	pAd->FirmwareVersion = (FIRMWARE_MAJOR_VERSION << 8)
				+ FIRMWARE_MINOR_VERSION;  //default version.

	// Select chipset firmware
	if (pAd->pPci_Dev->device == NIC2561_PCI_DEVICE_ID)
		fw_name = RT2561_IMAGE_FILE_NAME;
	else if (pAd->pPci_Dev->device == NIC2561Turbo_PCI_DEVICE_ID)
		fw_name = RT2561S_IMAGE_FILE_NAME;
	else if (pAd->pPci_Dev->device == NIC2661_PCI_DEVICE_ID)
		fw_name = RT2661_IMAGE_FILE_NAME;
	else {
		DBGPRINT(RT_DEBUG_ERROR,
			 "NICLoadFirmware: wrong DeviceID = 0x%04x, can't find firmware\n",
			 pAd->pPci_Dev->device);
		pAd->SystemErrorBitmap |= 0x00000002;
		return NDIS_STATUS_FAILURE;
	}

	// hold 8051 in reset state
	RTMP_IO_WRITE32(pAd, MCU_CNTL_CSR, 0x02);
	RTMP_IO_WRITE32(pAd, M2H_CMD_DONE_CSR, 0xffffffff);	// clear all CmdToken
	RTMP_IO_WRITE32(pAd, H2M_MAILBOX_CSR, 0x00000000);	// MBOX owned by HOST
	RTMP_IO_WRITE32(pAd, HOST_CMD_CSR, 0x00000000);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	if (request_firmware(&fw_entry, fw_name, (const char *)&dev->slot_name)) {
#else
	if (request_firmware(&fw_entry, fw_name, &dev->dev)) {
#endif
		DBGPRINT(RT_DEBUG_ERROR, "Failed to load Firmware.\n");
		return NDIS_STATUS_FAILURE;
	}

	if (fw_entry->size != MAX_FIRMWARE_IMAGE_SIZE) {
		DBGPRINT(RT_DEBUG_ERROR,
			 "NICLoadFirmware: error file length (=%d) in BIN file\n",
			 fw_entry->size);
		goto error;
	}

	// Check firmware CRC
	ptr = fw_entry->data;
	size = MAX_FIRMWARE_IMAGE_SIZE - 2;
	crc = 0;
	for (i = 0; i < size; i++)
		crc = ByteCRC16(*ptr++, crc);
	crc = ByteCRC16(0x00, crc);
	crc = ByteCRC16(0x00, crc);

	if ((fw_entry->data[size] != (UCHAR) (crc >> 8))
			|| (fw_entry->data[size + 1] != (UCHAR) crc)) {
		DBGPRINT(RT_DEBUG_ERROR,
			"NICLoadFirmware: CRC = 0x%02x 0x%02x error, should be 0x%02x 0x%02x\n",
			fw_entry->data[size], fw_entry->data[size + 1],
			(UCHAR) (crc >> 8), (UCHAR) crc);
		goto error;
	}

	// Check firmware version number
	if (pAd->FirmwareVersion > ((fw_entry->data[size - 2] << 8) +
						fw_entry->data[size - 1])) {
		DBGPRINT(RT_DEBUG_ERROR,
			 "NICLoadFirmware: Ver=%d.%d, local Ver=%d.%d, used FirmwareImage table instead\n",
			 fw_entry->data[size - 2], fw_entry->data[size - 1],
			 FIRMWARE_MAJOR_VERSION, FIRMWARE_MINOR_VERSION);
		goto error;
	}
	pAd->FirmwareVersion = (fw_entry->data[size - 2] << 8) +
	    			fw_entry->data[size - 1];
	DBGPRINT(RT_DEBUG_TRACE,
		 "NICLoadFirmware OK: CRC = 0x%04x ver=%d.%d\n",
		 crc, fw_entry->data[size - 2], fw_entry->data[size - 1]);

	// Copy firmware to NIC
	// select 8051 program bank; write entire firmware image
	RTMP_IO_WRITE32(pAd, MCU_CNTL_CSR, 0x03);
	for (i = 0; i < MAX_FIRMWARE_IMAGE_SIZE; i++)
		RTMP_IO_WRITE8(pAd, FIRMWARE_IMAGE_BASE + i, fw_entry->data[i]);
	// de-select 8051 program bank
	RTMP_IO_WRITE32(pAd, MCU_CNTL_CSR, 0x02);

	// 8051 get out of RESET state
	RTMP_IO_WRITE32(pAd, MCU_CNTL_CSR, 0x00);

	// Wait for stable hardware
	i = 0;
	do {
		RTMP_IO_READ32(pAd, MCU_CNTL_CSR, &MacReg);
		if (MacReg & 0x04) {
			RTMPusecDelay(1000);
			break;
		}
		RTMPusecDelay(1000);
	} while (i++ < 1000);

	if (i >= 1000) {
		DBGPRINT(RT_DEBUG_ERROR,
				 "NICLoadFirmware: MCU is not ready\n\n\n");
		goto error;
	}

	// Reset LED
	RTMPSetLED(pAd, LED_LINK_DOWN);
	// Firmware loaded successfully
	Status = NDIS_STATUS_SUCCESS;

error:
	release_firmware(fw_entry);
	DBGPRINT(RT_DEBUG_TRACE, "<=== NICLoadFirmware (status=%d)\n", Status);
	return Status;
}

/*
    ========================================================================

    Routine Description:
        Find key section for Get key parameter.

    Arguments:
        buffer                      Pointer to the buffer to start find the key section
        section                     the key of the secion to be find

    Return Value:
        NULL                        Fail
        Others                      Success
    ========================================================================
*/
#if 0
static PUCHAR RTMPFindSection(IN PCHAR buffer)
{
	CHAR temp_buf[255];
	PUCHAR ptr;

	strcpy(temp_buf, "[");	/*  and the opening bracket [  */
	strcat(temp_buf, "Default");
	strcat(temp_buf, "]");

	if ((ptr = strstr(buffer, temp_buf)) != NULL)
		return (ptr + strlen("\n"));
	else
		return NULL;
}

/*
    ========================================================================

    Routine Description:
        Get key parameter.

    Arguments:
        key                         Pointer to key string
        dest                        Pointer to destination
        destsize                    The datasize of the destination
        buffer                      Pointer to the buffer to start find the key

    Return Value:
        TRUE                        Success
        FALSE                       Fail

    Note:
        This routine get the value with the matched key (case case-sensitive)
    ========================================================================
*/
static INT RTMPGetKeyParameter(IN PCHAR key,
			OUT PCHAR dest, IN INT destsize, IN PCHAR buffer)
{
	CHAR temp_buf1[600];
	CHAR temp_buf2[600];
	CHAR *start_ptr;
	CHAR *end_ptr;
	CHAR *ptr;
	CHAR *offset = 0;
	INT len;

	//find section
	if ((offset = RTMPFindSection(buffer)) == NULL)
		return (FALSE);

	strcpy(temp_buf1, "\n");
	strcat(temp_buf1, key);
	strcat(temp_buf1, "=");

	//search key
	if ((start_ptr = strstr(offset, temp_buf1)) == NULL)
		return FALSE;

	start_ptr += strlen("\n");

	if ((end_ptr = strstr(start_ptr, "\n")) == NULL)
		end_ptr = start_ptr + strlen(start_ptr);

	if (end_ptr < start_ptr)
		return FALSE;

	memcpy(temp_buf2, start_ptr, end_ptr - start_ptr);
	temp_buf2[end_ptr - start_ptr] = '\0';
	len = strlen(temp_buf2);
	strcpy(temp_buf1, temp_buf2);
	if ((start_ptr = strstr(temp_buf1, "=")) == NULL)
		return FALSE;

	strcpy(temp_buf2, start_ptr + 1);
	ptr = temp_buf2;
	//trim space or tab
	while (*ptr != 0x00) {
		if ((*ptr == ' ') || (*ptr == '\t'))
			ptr++;
		else
			break;
	}

	len = strlen(ptr);
	memset(dest, 0x00, destsize);
	strncpy(dest, ptr, len >= destsize ? destsize : len);

	return TRUE;
}
#endif
/*
    ========================================================================

    Routine Description:
        Reset NIC Asics

    Arguments:
        Adapter                     Pointer to our adapter

    Return Value:
        None

    Note:
        Reset NIC to initial state AS IS system boot up time.

    ========================================================================
*/
VOID RTMPRingCleanUp(IN PRTMP_ADAPTER pAdapter, IN UCHAR RingType)
{
	PTXD_STRUC pTxD;
	PRXD_STRUC pRxD;
#ifdef	BIG_ENDIAN
	PTXD_STRUC pDestTxD;
	TXD_STRUC TxD;
	PRXD_STRUC pDestRxD;
	RXD_STRUC RxD;
#endif
#if SL_IRQSAVE
	unsigned long IrqFlags;
#endif

	struct sk_buff *pSkb;
	INT i;
	PRTMP_TX_RING pTxRing;

	DBGPRINT(RT_DEBUG_TRACE, "RTMPRingCleanUp(RingIdx=%d, p-NDIS=%d)\n",
		 RingType, pAdapter->RalinkCounters.PendingNdisPacketCount);

	switch (RingType) {
	case QID_AC_BK:
	case QID_AC_BE:
	case QID_AC_VI:
	case QID_AC_VO:
	case QID_HCCA:
#if SL_IRQSAVE
		spin_lock_irqsave(&pAdapter->TxRingLock, IrqFlags);
#else
		spin_lock_bh(&pAdapter->TxRingLock);
#endif

		pTxRing = &pAdapter->TxRing[RingType];
		// We have to clean all descriptors in case some error happened with reset
		for (i = 0; i < TX_RING_SIZE; i++)	// We have to scan all TX ring
		{
#ifndef BIG_ENDIAN
			pTxD = (PTXD_STRUC) pTxRing->Cell[i].AllocVa;
#else
			pDestTxD = (PTXD_STRUC) pTxRing->Cell[i].AllocVa;
			TxD = *pDestTxD;
			pTxD = &TxD;
			RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif

			// release scatter-and-gather NDIS_PACKET
			if (pTxRing->Cell[i].pSkb) {
				RELEASE_NDIS_PACKET(pAdapter,
						    pTxRing->Cell[i].pSkb);
				pTxRing->Cell[i].pSkb = 0;
				DBGPRINT(RT_DEBUG_TRACE,
					 "Release 1 skb buffer at TxRing%d[%d]=%d.%d\n",
					 RingType, i, pTxD->Owner,
					 pTxD->bWaitingDmaDoneInt);
			}
			// release scatter-and-gather NDIS_PACKET
			if (pTxRing->Cell[i].pNextSkb) {
				RELEASE_NDIS_PACKET(pAdapter,
						    pTxRing->Cell[i].pNextSkb);
				pTxRing->Cell[i].pNextSkb = 0;
				DBGPRINT(RT_DEBUG_TRACE,
					 "Release 1 skb buffer at TxRing%d[%d]\n",
					 RingType, i);
			}

			pTxD->Owner = DESC_OWN_HOST;
			pTxD->bWaitingDmaDoneInt = 0;
#ifdef BIG_ENDIAN
			RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
			WriteBackToDescriptor((PUCHAR) pDestTxD, (PUCHAR) pTxD,
					      FALSE, TYPE_TXD);
#endif
		}
		pTxRing->CurTxIndex = 0;
		pTxRing->NextTxDmaDoneIndex = 0;
#if SL_IRQSAVE
		spin_unlock_irqrestore(&pAdapter->TxRingLock,
				       IrqFlags);
#else
		spin_unlock_bh(&pAdapter->TxRingLock);
#endif

		while (TRUE) {
			pSkb = skb_dequeue(&pAdapter->TxSwQueue[RingType]);
			if (!pSkb)
				break;
			RELEASE_NDIS_PACKET(pAdapter, pSkb);
			DBGPRINT(RT_DEBUG_TRACE,
				 "Release 1 skb buffer from s/w backlog queue\n");
		}
		break;

	case QID_MGMT:
		// We have to clean all descriptors in case some error happened with reset
#if SL_IRQSAVE
		spin_lock_irqsave(&pAdapter->MgmtRingLock, IrqFlags);
#else
		spin_lock_bh(&pAdapter->MgmtRingLock);
#endif
		for (i = 0; i < MGMT_RING_SIZE; i++) {
#ifndef BIG_ENDIAN
			pTxD = (PTXD_STRUC) pAdapter->MgmtRing.Cell[i].AllocVa;
#else
			pDestTxD =
			    (PTXD_STRUC) pAdapter->MgmtRing.Cell[i].AllocVa;
			TxD = *pDestTxD;
			pTxD = &TxD;
			RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
#endif

			pTxD->Owner = DESC_OWN_HOST;
			pTxD->bWaitingDmaDoneInt = 0;

			pci_unmap_single(pAdapter->pPci_Dev, pTxD->BufPhyAddr0,
					 pTxD->BufLen0, PCI_DMA_TODEVICE);

			// rlease scatter-and-gather NDIS_PACKET
			if (pAdapter->MgmtRing.Cell[i].pSkb) {
				RELEASE_NDIS_PACKET(pAdapter,
						    pAdapter->MgmtRing.Cell[i].
						    pSkb);
				pAdapter->MgmtRing.Cell[i].pSkb = 0;
			}
#ifdef BIG_ENDIAN
			RTMPDescriptorEndianChange((PUCHAR) pTxD, TYPE_TXD);
			WriteBackToDescriptor((PUCHAR) pDestTxD, (PUCHAR) pTxD,
					      FALSE, TYPE_TXD);
#endif
		}
		pAdapter->MgmtRing.CurTxIndex = 0;
		pAdapter->MgmtRing.NextTxDmaDoneIndex = 0;
#if SL_IRQSAVE
		spin_unlock_irqrestore(&pAdapter->MgmtRingLock,
				       IrqFlags);
#else
		spin_unlock_bh(&pAdapter->MgmtRingLock);
#endif
		pAdapter->RalinkCounters.MgmtRingFullCount = 0;
		break;

	case QID_RX:
		// We have to clean all descriptors in case some error happened with reset
#if SL_IRQSAVE
		spin_lock_irqsave(&pAdapter->RxRingLock, IrqFlags);
#else
		spin_lock_bh(&pAdapter->RxRingLock);
#endif
		for (i = 0; i < RX_RING_SIZE; i++) {
#ifndef BIG_ENDIAN
			pRxD = (PRXD_STRUC) pAdapter->RxRing.Cell[i].AllocVa;
#else
			pDestRxD =
			    (PRXD_STRUC) pAdapter->RxRing.Cell[i].AllocVa;
			RxD = *pDestRxD;
			pRxD = &RxD;
			RTMPDescriptorEndianChange((PUCHAR) pRxD, TYPE_RXD);
#endif
			pRxD->Owner = DESC_OWN_NIC;
#ifdef BIG_ENDIAN
			RTMPDescriptorEndianChange((PUCHAR) pRxD, TYPE_RXD);
			WriteBackToDescriptor((PUCHAR) pDestRxD, (PUCHAR) pRxD,
					      FALSE, TYPE_RXD);
#endif
		}
		pAdapter->RxRing.CurRxIndex = 0;
#if SL_IRQSAVE
		spin_unlock_irqrestore(&pAdapter->RxRingLock,
				       IrqFlags);
#else
		spin_unlock_bh(&pAdapter->RxRingLock);
#endif
		break;

	default:
		break;

	}
}

/*
	========================================================================

	Routine Description:
		Initialize port configuration structure

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		None

	Note:

	========================================================================
*/
VOID PortCfgInit(IN PRTMP_ADAPTER pAd)
{
	UINT i;

	DBGPRINT(RT_DEBUG_TRACE, "--> PortCfgInit\n");

	for (i = 0; i < SHARE_KEY_NUM; i++) {
		pAd->SharedKey[i].KeyLen = 0;
		pAd->SharedKey[i].CipherAlg = CIPHER_NONE;
	}

	pAd->Antenna.field.TxDefaultAntenna = 2;	// Ant-B
	pAd->Antenna.field.RxDefaultAntenna = 2;	// Ant-B
	pAd->Antenna.field.NumOfAntenna = 2;

	pAd->LedCntl.field.LedMode = LED_MODE_DEFAULT;
	pAd->LedIndicatorStregth = 0;

	pAd->bAutoTxAgcA = FALSE;	// Default is OFF
	pAd->bAutoTxAgcG = FALSE;	// Default is OFF
	pAd->RfIcType = RFIC_5225;
	pAd->PortCfg.PhyMode = 0xff;	// unknown
	pAd->PortCfg.BandState = UNKNOWN_BAND;

	pAd->bAcceptDirect = TRUE;
	pAd->bAcceptMulticast = FALSE;
	pAd->bAcceptBroadcast = TRUE;
	pAd->bAcceptAllMulticast = TRUE;

	pAd->bLocalAdminMAC = FALSE;	//TRUE;

	pAd->PortCfg.Dsifs = 10;	// in units of usec
	pAd->PortCfg.PrivacyFilter = Ndis802_11PrivFilterAcceptAll;
	pAd->PortCfg.TxPower = 100;	//mW
	pAd->PortCfg.TxPowerPercentage = 0xffffffff;	// AUTO
	pAd->PortCfg.TxPowerDefault = 0xffffffff;	// AUTO
	pAd->PortCfg.TxPreamble = Rt802_11PreambleAuto;	// use Long preamble on TX by defaut
	pAd->PortCfg.RtsThreshold = 2347;
	pAd->PortCfg.FragmentThreshold = 2346;
	pAd->PortCfg.dBmToRoam = 75;
	pAd->PortCfg.UseBGProtection = 0;	// 0: AUTO
	pAd->PortCfg.bEnableTxBurst = 0;
	pAd->PortCfg.UseShortSlotTime = TRUE;	//default set short slot time

	pAd->PortCfg.RFMONTX = FALSE;

	// PHY specification
	pAd->PortCfg.PhyMode = PHY_11BG_MIXED;
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED);	// CCK use LONG preamble

	pAd->PortCfg.Psm = PWR_ACTIVE;
	pAd->PortCfg.BeaconPeriod = 100;	// in mSec

	pAd->PortCfg.bAPSDCapable = FALSE;
	pAd->PortCfg.bAPSDForcePowerSave = FALSE;
	pAd->PortCfg.MaxSPLength = 0;	// QAP may deliver all buffered MSDUs and MMPDUs

	// Patch for Ndtest
	pAd->PortCfg.ScanCnt = 0;

	pAd->PortCfg.bIEEE80211H = FALSE;
	pAd->PortCfg.RadarDetect.RDMode = RD_NORMAL_MODE;

	pAd->PortCfg.AuthMode = Ndis802_11AuthModeOpen;
	pAd->PortCfg.WepStatus = Ndis802_11EncryptionDisabled;
	pAd->PortCfg.PairCipher = Ndis802_11EncryptionDisabled;
	pAd->PortCfg.GroupCipher = Ndis802_11EncryptionDisabled;
	pAd->PortCfg.bMixCipher = FALSE;
	pAd->PortCfg.DefaultKeyId = 0;

	// 802.1x port control
	pAd->PortCfg.PortSecured = WPA_802_1X_PORT_NOT_SECURED;
	pAd->PortCfg.LastMicErrorTime = 0;
	pAd->PortCfg.MicErrCnt = 0;
	pAd->PortCfg.bBlockAssoc = FALSE;
	pAd->PortCfg.WpaState = SS_NOTUSE;	// Handle by microsoft unless RaConfig changed it.

	pAd->PortCfg.RssiTrigger = 0;
	pAd->PortCfg.LastRssi = 0;
	pAd->PortCfg.LastRssi2 = 0;
	pAd->PortCfg.AvgRssi = 0;
	pAd->PortCfg.AvgRssiX8 = 0;
	pAd->PortCfg.AvgRssi2 = 0;
	pAd->PortCfg.AvgRssi2X8 = 0;
	pAd->PortCfg.RssiTriggerMode = RSSI_TRIGGERED_UPON_BELOW_THRESHOLD;
	pAd->PortCfg.AtimWin = 0;
	pAd->PortCfg.DefaultListenCount = 3;	//default listen count;
	pAd->PortCfg.BssType = BSS_INFRA;	// BSS_INFRA or BSS_ADHOC

	// global variables mXXXX used in MAC protocol state machines
	OPSTATUS_SET_FLAG(pAd, fOP_STATUS_RECEIVE_DTIM);
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_ADHOC_ON);
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_INFRA_ON);

	// user desired power mode
	pAd->PortCfg.WindowsPowerMode = Ndis802_11PowerModeCAM;
	pAd->PortCfg.WindowsBatteryPowerMode = Ndis802_11PowerModeCAM;
	pAd->PortCfg.bWindowsACCAMEnable = FALSE;

	init_timer(&pAd->PortCfg.QuickResponeForRateUpTimer);
	pAd->PortCfg.QuickResponeForRateUpTimer.data = (unsigned long)pAd;
	pAd->PortCfg.QuickResponeForRateUpTimer.function =
	    &StaQuickResponeForRateUpExec;
	pAd->PortCfg.QuickResponeForRateUpTimerRunning = FALSE;

	pAd->PortCfg.bHwRadio = TRUE;	// Default Hardware Radio status is On
	pAd->PortCfg.bSwRadio = TRUE;	// Default Software Radio status is On
	pAd->PortCfg.bRadio = TRUE;	// bHwRadio && bSwRadio
	pAd->PortCfg.bHardwareRadio = FALSE;	// Default is OFF
	pAd->PortCfg.bShowHiddenSSID = FALSE;	// Default no show

	// Nitro mode control
	pAd->PortCfg.bAutoReconnect = TRUE;

	// Save the init time as last scan time, the system should do scan after 2 seconds.
	// This patch is for driver wake up from standby mode, system will do scan right away.
	pAd->PortCfg.LastScanTime = 0;

	// Default for extra information is not valid
	pAd->ExtraInfo = EXTRA_INFO_CLEAR;

	// Default Config change flag
	pAd->bConfigChanged = FALSE;

	// dynamic BBP R17:sensibity tuning to overcome background noise
	pAd->BbpTuning.bEnable = TRUE;
	pAd->BbpTuning.R17LowerBoundG = 0x20;	// for best RX sensibility
	pAd->BbpTuning.R17UpperBoundG = 0x40;	// for best RX noise isolation to prevent false CCA
	pAd->BbpTuning.R17LowerBoundA = 0x28;	// for best RX sensibility
	pAd->BbpTuning.R17UpperBoundA = 0x48;	// for best RX noise isolation to prevent false CCA
	pAd->BbpTuning.FalseCcaLowerThreshold = 100;
	pAd->BbpTuning.FalseCcaUpperThreshold = 512;
	pAd->BbpTuning.R17Delta = 1;

	pAd->Bbp94 = BBPR94_DEFAULT;
	pAd->BbpForCCK = FALSE;

//#if WPA_SUPPLICANT_SUPPORT
	pAd->PortCfg.IEEE8021X = 0;
	pAd->PortCfg.IEEE8021x_required_keys = 0;
	pAd->PortCfg.WPA_Supplicant = FALSE;
//#endif

#ifdef RALINK_ATE
	memset(&pAd->ate, 0, sizeof(ATE_INFO));
	pAd->ate.Mode = ATE_STASTART;
	pAd->ate.TxCount = TX_RING_SIZE;
	pAd->ate.TxLength = 1024;
	pAd->ate.TxRate = RATE_11;
	pAd->ate.Channel = 1;
	pAd->ate.Addr1[0] = 0x00;
	pAd->ate.Addr1[1] = 0x11;
	pAd->ate.Addr1[2] = 0x22;
	pAd->ate.Addr1[3] = 0xAA;
	pAd->ate.Addr1[4] = 0xBB;
	pAd->ate.Addr1[5] = 0xCC;
	memcpy(pAd->ate.Addr2, pAd->ate.Addr1, ETH_ALEN);
	memcpy(pAd->ate.Addr3, pAd->ate.Addr1, ETH_ALEN);
#endif				//RALINK_ATE

	DBGPRINT(RT_DEBUG_TRACE, "<-- PortCfgInit\n");

}

static inline UCHAR BtoH(IN CHAR ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');	// Handle numerals
	if (ch >= 'A' && ch <= 'F')
		return (ch - 'A' + 0xA);	// Handle capitol hex digits
	if (ch >= 'a' && ch <= 'f')
		return (ch - 'a' + 0xA);	// Handle small hex digits
	return (255);
}

//
//  PURPOSE:  Converts ascii string to network order hex
//
//  PARAMETERS:
//    src    - pointer to input ascii string
//    dest   - pointer to output hex
//    destlen - size of dest
//
//  COMMENTS:
//
//    2 ascii bytes make a hex byte so must put 1st ascii byte of pair
//    into upper nibble and 2nd ascii byte of pair into lower nibble.
//
VOID AtoH(IN CHAR * src, OUT UCHAR * dest, IN INT destlen)
{
	CHAR *srcptr;
	PUCHAR destTemp;

	srcptr = src;
	destTemp = (PUCHAR) dest;

	while (destlen--) {
		*destTemp = BtoH(*srcptr++) << 4;	// Put 1st ascii byte in upper nibble.
		*destTemp += BtoH(*srcptr++);	// Add 2nd ascii byte to above.
		destTemp++;
	}
}

/*
	========================================================================

	Routine Description:
		Set LED Status

	Arguments:
		pAd						Pointer to our adapter
		Status					LED Status

	Return Value:
		None

	Note:

	========================================================================
*/
VOID RTMPSetLED(IN PRTMP_ADAPTER pAd, IN UCHAR Status)
{
	UCHAR HighByte = 0;
	UCHAR LowByte = 0;

	switch (Status) {
	case LED_LINK_DOWN:
		pAd->LedCntl.field.LinkGStatus = 0;
		pAd->LedCntl.field.LinkAStatus = 0;

		HighByte = pAd->LedCntl.word >> 8;
		LowByte = pAd->LedCntl.word & 0xFF;
		AsicSendCommandToMcu(pAd, 0x50, 0xff, LowByte, HighByte);
		pAd->LedIndicatorStregth = 0;
		break;
	case LED_LINK_UP:
		if (pAd->PortCfg.Channel <= 14) {
			// 11 G mode
			pAd->LedCntl.field.LinkGStatus = 1;
			pAd->LedCntl.field.LinkAStatus = 0;
		} else {
			//11 A mode
			pAd->LedCntl.field.LinkGStatus = 0;
			pAd->LedCntl.field.LinkAStatus = 1;
		}

		HighByte = pAd->LedCntl.word >> 8;
		LowByte = pAd->LedCntl.word & 0xFF;
		AsicSendCommandToMcu(pAd, 0x50, 0xff, LowByte, HighByte);
		break;
	case LED_RADIO_ON:
		pAd->LedCntl.field.RadioStatus = 1;
		HighByte = pAd->LedCntl.word >> 8;
		LowByte = pAd->LedCntl.word & 0xFF;
		AsicSendCommandToMcu(pAd, 0x50, 0xff, LowByte, HighByte);
		break;
	case LED_HALT:
		//Same as Radio Off.
	case LED_RADIO_OFF:
		pAd->LedCntl.field.RadioStatus = 0;
		pAd->LedCntl.field.LinkGStatus = 0;
		pAd->LedCntl.field.LinkAStatus = 0;
		HighByte = pAd->LedCntl.word >> 8;
		LowByte = pAd->LedCntl.word & 0xFF;
		AsicSendCommandToMcu(pAd, 0x50, 0xff, LowByte, HighByte);
		break;
	default:
		DBGPRINT(RT_DEBUG_WARN, "RTMPSetLED::Unknown Status %d\n",
			 Status);
		break;
	}

	DBGPRINT(RT_DEBUG_INFO,
		 "RTMPSetLED::Mode=%d,HighByte=0x%02x,LowByte=0x%02x\n",
		 pAd->LedCntl.field.LedMode, HighByte, LowByte);
}

/*
	========================================================================

	Routine Description:
		Set LED Signal Stregth

	Arguments:
		pAd						Pointer to our adapter
		Dbm						Signal Stregth

	Return Value:
		None

	Note:
		Can be run on any IRQL level.

		According to Microsoft Zero Config Wireless Signal Stregth definition as belows.
		<= -90  No Signal
		<= -81  Very Low
		<= -71  Low
		<= -67  Good
		<= -57  Very Good
		 > -57  Excellent
	========================================================================
*/
VOID RTMPSetSignalLED(IN PRTMP_ADAPTER pAd, IN NDIS_802_11_RSSI Dbm)
{
	UCHAR nLed = 0;

	//
	// if not Signal Stregth, then do nothing.
	//
	if (pAd->LedCntl.field.LedMode != LED_MODE_SIGNAL_STREGTH) {
		return;
	}

	if (Dbm <= -90)
		nLed = 0;
	else if (Dbm <= -81)
		nLed = 1;
	else if (Dbm <= -71)
		nLed = 2;
	else if (Dbm <= -67)
		nLed = 3;
	else if (Dbm <= -57)
		nLed = 4;
	else
		nLed = 5;

	//
	// Update Signal Stregth to firmware if changed.
	//
	if (pAd->LedIndicatorStregth != nLed) {
		AsicSendCommandToMcu(pAd, 0x52, 0xff, nLed, 0);
		pAd->LedIndicatorStregth = nLed;
	}
}
