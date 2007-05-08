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
 *      Module Name: rtmp_main.c
 *
 *      Abstract:
 *
 *      Revision History:
 *      Who             When            What
 *      --------        -----------     -----------------------------
 *      PaulL           25th Nov 02     Initial version
 *      GertjanW        21st Jan 06     Baseline code
 *      Flavio (rt2400) 23rd Jan 06     Elegant irqreturn_t handling
 *      Flavio (rt2400) 23rd Jan 06     Remove local alloc_netdev
 *      Flavio (rt2400) 23rd Jan 06     Extra debug at init time
 *      Ivo (rt2400)    28th Jan 06     Debug Level Switching
 *      RobinC (rt2500) 30th Jan 06     Support ifpreup scripts
 *      MarkW (rt2500)  3rd  Feb 06     sysfs support for HAL/NetworkMan
 *      Bruno (rt2500)  3rd  Feb 06	Network device name module param
 *      MeelisR(rt2500) 16th Feb 06     PCI management fixes
 *      TorP (rt2500)   19th Feb 06     Power management: Suspend and Resume
 *      MarkW (rt2500)  19th Feb 06     Promisc mode support
 ***************************************************************************/

#include "rt_config.h"
#include <linux/ethtool.h>

// Global variable, debug level flag
// Don't hide this behing debug define. There should be as little difference between debug and no-debug as possible.
int debug = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
module_param(debug, int, 0444);
#else
MODULE_PARM(debug, "i");
#endif
MODULE_PARM_DESC(debug,
		 "Enable level: accepted values: 1 to switch debug on, 0 to switch debug off.");

static char *ifname = NULL;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
module_param(ifname, charp, 0444);
#else
MODULE_PARM(ifname, "s");
#endif
MODULE_PARM_DESC(ifname, "Network device name (default wlan%d)");

static dma_addr_t dma_adapter;

extern const struct iw_handler_def rt61_iw_handler_def;

#ifdef RT2X00DEBUGFS
/*
 * Register layout information.
 */
#define CSR_REG_BASE			0x3000
#define CSR_REG_SIZE			0x04b0
#define EEPROM_BASE			0x0000
#define EEPROM_SIZE			0x0100
#define BBP_SIZE			0x0080

static void rt61pci_read_csr(void *dev, const unsigned long word,
		void *data)
{
	RTMP_ADAPTER *pAd = dev;

	RTMP_IO_READ32(pAd, CSR_REG_BASE + (word * sizeof(u32)), (u32*)data);
}

static void rt61pci_write_csr(void *dev, const unsigned long word,
	void *data)
{
	RTMP_ADAPTER *pAd = dev;

	RTMP_IO_WRITE32(pAd, word, *((u32*)data));
}

static void rt61pci_read_eeprom(void *dev, const unsigned long word,
		void *data)
{
	RTMP_ADAPTER *pAd = dev;

	*((u16*)data) = RTMP_EEPROM_READ16(pAd, word * sizeof(u16));
}

static void rt61pci_write_eeprom(void *dev, const unsigned long word,
	void *data)
{
	/* DANGEROUS, DON'T DO THIS! */
}

static void rt61pci_read_bbp(void *dev, const unsigned long word,
		void *data)
{
	RTMP_ADAPTER *pAd = dev;

	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, (u8)word, ((u8*)data));
}

static void rt61pci_write_bbp(void *dev, const unsigned long word,
	void *data)
{
	RTMP_ADAPTER *pAd = dev;

	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, word, *((u8*)data));
}

static void rt61pci_open_debugfs(RTMP_ADAPTER *pAd)
{
	struct rt2x00debug *debug = &pAd->debug;

	debug->owner 			= THIS_MODULE;
	debug->mod_name			= DRIVER_NAME;
	debug->mod_version		= DRIVER_VERSION;
	debug->reg_csr.read		= rt61pci_read_csr;
	debug->reg_csr.write		= rt61pci_write_csr;
	debug->reg_csr.word_size	= sizeof(u32);
	debug->reg_csr.length		= CSR_REG_SIZE;
	debug->reg_eeprom.read		= rt61pci_read_eeprom;
	debug->reg_eeprom.write		= rt61pci_write_eeprom;
	debug->reg_eeprom.word_size	= sizeof(u16);
	debug->reg_eeprom.length	= EEPROM_SIZE;
	debug->reg_bbp.read		= rt61pci_read_bbp;
	debug->reg_bbp.write		= rt61pci_write_bbp;
	debug->reg_bbp.word_size	= sizeof(u8);
	debug->reg_bbp.length		= BBP_SIZE;
	debug->dev 			= pAd;

	snprintf(debug->intf_name, sizeof(debug->intf_name),
		"%s", pAd->net_dev->name);

	if (rt2x00debug_register(debug))
		printk(KERN_ERR "Failed to register debug handler.\n");
}

static void rt61pci_close_debugfs(RTMP_ADAPTER *pAd)
{
	rt2x00debug_deregister(&pAd->debug);
}
#else /* RT2X00DEBUGFS */
static inline void rt61pci_open_debugfs(RTMP_ADAPTER *pAd){}
static inline void rt61pci_close_debugfs(RTMP_ADAPTER *pAd){}
#endif /* RT2X00DEBUGFS */

// =======================================================================
// Ralink PCI device table, include all supported chipsets
// =======================================================================
static struct pci_device_id rt61_pci_tbl[] __devinitdata = {
	{0x1814, 0x0301, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},	//RT2561S
	{0x1814, 0x0302, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},	//RT2561 V2
	{0x1814, 0x0401, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},	//RT2661
	{0,}			// terminate list
};
int const rt61_pci_tbl_len =
    sizeof(rt61_pci_tbl) / sizeof(struct pci_device_id);


/*
    ========================================================================

    Routine Description:
        Interrupt handler

    Arguments:
        irq                         interrupt line
        dev_instance                Pointer to net_device

    Return Value:
        VOID

    Note:

    ========================================================================
*/
static
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
 irqreturn_t RTMPIsr(IN INT irq, IN VOID * dev_instance, IN struct pt_regs * rgs)
#else
 irqreturn_t RTMPIsr(IN INT irq, IN VOID * dev_instance)
#endif
{
	struct net_device *net_dev = dev_instance;
	PRTMP_ADAPTER pAdapter = net_dev->priv;
	INT_SOURCE_CSR_STRUC IntSource;
	MCU_INT_SOURCE_STRUC McuIntSource;
	int ret = 0;

	DBGPRINT(RT_DEBUG_INFO, "====> RTMPHandleInterrupt\n");

	// 1. Disable interrupt
	if (RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)
	    		&& RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_ACTIVE))
		NICDisableInterrupt(pAdapter);

	// Exit if Reset in progress (won't re-enable interrupts)
	if (RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_RESET_IN_PROGRESS) ||
	    		RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_HALT_IN_PROGRESS)) {
	    ret++;
		goto out;
	}
	
	//
	// Handle interrupt, walking through all bits.
	// Should start from highest priority interrupt.
	// The priority can be adjust by altering processing if statement
	// If required spinlock, each interrupt service routine has to acquire
	// and release itself.
	//

	// Get interrupt source & save it to local variable
	IntSource.word = 0x00000000L;
	RTMP_IO_READ32(pAdapter, INT_SOURCE_CSR, &IntSource.word);
	if (IntSource.word) {
		RTMP_IO_WRITE32(pAdapter, INT_SOURCE_CSR, IntSource.word);	// write 1 to clear
		if (IntSource.field.MgmtDmaDone) {
			RTMPHandleMgmtRingDmaDoneInterrupt(pAdapter);
			ret++;
		}
		if (IntSource.field.RxDone) {
			RTMPHandleRxDoneInterrupt(pAdapter);
			ret++;
		}
		if (IntSource.field.TxDone) {
			RTMPHandleTxDoneInterrupt(pAdapter);
			ret++;
		}
		if (IntSource.word & 0x002f0000) {
			RTMPHandleTxRingDmaDoneInterrupt(pAdapter, IntSource);
			ret++;
		}
	}

	McuIntSource.word = 0x00000000L;
	RTMP_IO_READ32(pAdapter, MCU_INT_SOURCE_CSR, &McuIntSource.word);
	if (McuIntSource.word) {
		RTMP_IO_WRITE32(pAdapter, MCU_INT_SOURCE_CSR, McuIntSource.word);
		if (McuIntSource.word & 0xff) {
			ULONG M2hCmdDoneCsr;
			RTMP_IO_READ32(pAdapter, M2H_CMD_DONE_CSR, &M2hCmdDoneCsr);
			RTMP_IO_WRITE32(pAdapter, M2H_CMD_DONE_CSR, 0xffffffff);
			DBGPRINT(RT_DEBUG_TRACE,
				 "MCU command done - INT bitmap=0x%02x, M2H mbox=0x%08x\n",
				 McuIntSource.word, M2hCmdDoneCsr);
			ret++;
		}
		if (McuIntSource.field.TBTTExpire) {
			RTMPHandleTBTTInterrupt(pAdapter);
			ret++;
		}
		if (McuIntSource.field.Twakeup) {
			RTMPHandleTwakeupInterrupt(pAdapter);
			ret++;
		}
	}

	//
	// Re-enable the interrupt (disabled in RTMPIsr)
	//
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_ACTIVE))
		NICEnableInterrupt(pAdapter);

out:

	DBGPRINT(RT_DEBUG_INFO, "<==== RTMPHandleInterrupt (%d handled)\n", ret);
	return IRQ_RETVAL(ret);
}

/*
    ========================================================================

    Routine Description:
        hard_start_xmit handler

    Arguments:
        skb             point to sk_buf which upper layer transmit
        net_dev         point to net_dev
    Return Value:
        None

    Note:

    ========================================================================
*/
static INT RTMPSendPackets(IN struct sk_buff * pSkb, IN struct net_device * net_dev)
{
	UCHAR Index;
	PRTMP_ADAPTER pAdapter = net_dev->priv;

	DBGPRINT(RT_DEBUG_INFO, "===> RTMPSendPackets\n");

#ifdef RALINK_ATE
	if (pAdapter->ate.Mode != ATE_STASTART) {
		dev_kfree_skb(pSkb);
		return 0;
	}
#endif

	if (pAdapter->PortCfg.BssType == BSS_MONITOR
	    && pAdapter->PortCfg.RFMONTX != TRUE) {
		dev_kfree_skb(pSkb);
		return 0;
	}
	// Drop packets if no associations
	if (pAdapter->PortCfg.BssType != BSS_MONITOR &&
	    !INFRA_ON(pAdapter) && !ADHOC_ON(pAdapter)) {
		// Drop send request since there are no physical connection yet
		// Check the association status for infrastructure mode
		// And Mibss for Ad-hoc mode setup
		dev_kfree_skb(pSkb);
		return 0;
	}
	if (RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_RESET_IN_PROGRESS) ||
		   RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_HALT_IN_PROGRESS)) {
		// Drop send request since hardware is in reset state
		dev_kfree_skb(pSkb);
		return 0;
	}

	// initial pSkb->data_len=0, we will use this variable to store data size when fragment(in TKIP)
	// and pSkb->len is actual data len
	pSkb->data_len = pSkb->len;

	// Record that orignal packet source is from protocol layer,so that
	// later on driver knows how to release this skb buffer
	RTMP_SET_PACKET_SOURCE(pSkb, PKTSRC_NDIS);
	pAdapter->RalinkCounters.PendingNdisPacketCount++;
	RTMPSendPacket(pAdapter, pSkb);
	for (Index = 0; Index < 5; Index++)
		RTMPDeQueuePacket(pAdapter, Index);
	return 0;
}


static int rt61_set_mac_address(struct net_device *net_dev, void *addr)
{
	RTMP_ADAPTER *pAd = net_dev->priv;
	struct sockaddr *mac = (struct sockaddr *)addr;
	u32 set_mac;

	if (netif_running(net_dev))
		return -EBUSY;

	if (!is_valid_ether_addr(&mac->sa_data[0]))
		return -EINVAL;

	BUG_ON(net_dev->addr_len != ETH_ALEN);

	memcpy(net_dev->dev_addr, mac->sa_data, ETH_ALEN);
	memcpy(pAd->CurrentAddress, mac->sa_data, ETH_ALEN);
	pAd->bLocalAdminMAC = TRUE;

	memset(&set_mac, 0x00, sizeof(INT));
	set_mac =
	    (net_dev->dev_addr[0]) | (net_dev->dev_addr[1] << 8) | (net_dev->
								    dev_addr[2]
								    << 16) |
	    (net_dev->dev_addr[3] << 24);

	RTMP_IO_WRITE32(pAd, MAC_CSR2, set_mac);

	memset(&set_mac, 0x00, sizeof(INT));
	set_mac = (net_dev->dev_addr[4]) | (net_dev->dev_addr[5] << 8);

	RTMP_IO_WRITE32(pAd, MAC_CSR3, set_mac);

	printk(KERN_INFO
	       "***rt2x00***: Info - Mac address changed to: %02x:%02x:%02x:%02x:%02x:%02x.\n",
	       net_dev->dev_addr[0], net_dev->dev_addr[1], net_dev->dev_addr[2],
	       net_dev->dev_addr[3], net_dev->dev_addr[4],
	       net_dev->dev_addr[5]);

	return 0;
}

#if WIRELESS_EXT >= 12
/*
    ========================================================================

    Routine Description:
        get wireless statistics

    Arguments:
        net_dev                     Pointer to net_device

    Return Value:
        struct iw_statistics

    Note:
        This function will be called when query /proc

    ========================================================================
*/
long rt_abs(long arg)
{
	return (arg < 0) ? -arg : arg;
}
struct iw_statistics *RT61_get_wireless_stats(IN struct net_device *net_dev)
{
	RTMP_ADAPTER *pAd = net_dev->priv;

	// TODO: All elements are zero before be implemented

	pAd->iw_stats.status = 0;	// Status - device dependent for now

	pAd->iw_stats.qual.qual = pAd->Mlme.ChannelQuality;	// link quality (%retries, SNR, %missed beacons or better...)
#ifdef RTMP_EMBEDDED
	pAd->iw_stats.qual.level = rt_abs(pAd->PortCfg.LastRssi);	// signal level (dBm)
#else
	pAd->iw_stats.qual.level = abs(pAd->PortCfg.LastRssi);	// signal level (dBm)
#endif
	pAd->iw_stats.qual.level += 256 - pAd->BbpRssiToDbmDelta;

	pAd->iw_stats.qual.noise = (pAd->BbpWriteLatch[17] > pAd->BbpTuning.R17UpperBoundG) ? pAd->BbpTuning.R17UpperBoundG : ((ULONG) pAd->BbpWriteLatch[17]);	// noise level (dBm)
	pAd->iw_stats.qual.noise += 256 - 143;
	pAd->iw_stats.qual.updated = 1;	// Flags to know if updated

	pAd->iw_stats.discard.nwid = 0;	// Rx : Wrong nwid/essid
	pAd->iw_stats.miss.beacon = 0;	// Missed beacons/superframe

	// pAd->iw_stats.discard.code, discard.fragment, discard.retries, discard.misc has counted in other place

	return &pAd->iw_stats;
}
#endif

/*
    ========================================================================

    Routine Description:
        return ethernet statistics counter

    Arguments:
        net_dev                     Pointer to net_device

    Return Value:
        net_device_stats*

    Note:

    ========================================================================
*/
static struct net_device_stats *RT61_get_ether_stats(IN struct net_device *net_dev)
{
	RTMP_ADAPTER *pAd = net_dev->priv;

	DBGPRINT(RT_DEBUG_INFO, "RT61_get_ether_stats --->\n");

	pAd->stats.rx_packets = pAd->WlanCounters.ReceivedFragmentCount.vv.LowPart;	// total packets received
	pAd->stats.tx_packets = pAd->WlanCounters.TransmittedFragmentCount.vv.LowPart;	// total packets transmitted

	pAd->stats.rx_bytes = pAd->RalinkCounters.ReceivedByteCount;	// total bytes received
	pAd->stats.tx_bytes = pAd->RalinkCounters.TransmittedByteCount;	// total bytes transmitted

	pAd->stats.rx_errors = pAd->Counters8023.RxErrors;	// bad packets received
	pAd->stats.tx_errors = pAd->Counters8023.TxErrors;	// packet transmit problems

	pAd->stats.rx_dropped = pAd->Counters8023.RxNoBuffer;	// no space in linux buffers
	pAd->stats.tx_dropped = pAd->WlanCounters.FailedCount.vv.LowPart;	// no space available in linux

	pAd->stats.multicast = pAd->WlanCounters.MulticastReceivedFrameCount.vv.LowPart;	// multicast packets received
	pAd->stats.collisions = pAd->Counters8023.OneCollision + pAd->Counters8023.MoreCollisions;	// Collision packets

	pAd->stats.rx_length_errors = 0;
	pAd->stats.rx_over_errors = pAd->Counters8023.RxNoBuffer;	// receiver ring buff overflow
	pAd->stats.rx_crc_errors = 0;	//pAd->WlanCounters.FCSErrorCount;         // recved pkt with crc error
	pAd->stats.rx_frame_errors = pAd->Counters8023.RcvAlignmentErrors;	// recv'd frame alignment error
	pAd->stats.rx_fifo_errors = pAd->Counters8023.RxNoBuffer;	// recv'r fifo overrun
	pAd->stats.rx_missed_errors = 0;	// receiver missed packet

	// detailed tx_errors
	pAd->stats.tx_aborted_errors = 0;
	pAd->stats.tx_carrier_errors = 0;
	pAd->stats.tx_fifo_errors = 0;
	pAd->stats.tx_heartbeat_errors = 0;
	pAd->stats.tx_window_errors = 0;

	// for cslip etc
	pAd->stats.rx_compressed = 0;
	pAd->stats.tx_compressed = 0;

	return &pAd->stats;
}

/*
    ========================================================================

    Routine Description:
        Set to filter multicast list

    Arguments:
        net_dev                     Pointer to net_device

    Return Value:
        VOID

    Note:

    ========================================================================
*/
static VOID RT61_set_rx_mode(IN struct net_device * net_dev)
{
	RTMP_ADAPTER *pAd = net_dev->priv;

	if (net_dev->flags & IFF_PROMISC) {
		pAd->bAcceptPromiscuous = TRUE;
		DBGPRINT(RT_DEBUG_TRACE, "rt61 acknowledge PROMISC on\n");
	} else {
		pAd->bAcceptPromiscuous = FALSE;
		DBGPRINT(RT_DEBUG_TRACE, "rt61 acknowledge PROMISC off\n");
	}
	RTMPWriteTXRXCsr0(pAd, FALSE, TRUE);
}

static INT RT61_open(IN struct net_device * net_dev)
{
	PRTMP_ADAPTER pAd = net_dev->priv;
	INT status = NDIS_STATUS_SUCCESS;
	ULONG MacCsr0;
	UCHAR TmpPhy;
#ifdef RX_TASKLET
	struct sk_buff *skb;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	if (!try_module_get(THIS_MODULE)) {
		DBGPRINT(RT_DEBUG_ERROR, "%s: cannot reserve module\n",
			 __FUNCTION__);
		return -1;
	}
#else
	MOD_INC_USE_COUNT;
#endif

	// Wait for hardware stable
	{
		ULONG MacCsr0, Index = 0;

		do {
			RTMP_IO_READ32(pAd, MAC_CSR0, &MacCsr0);

			if (MacCsr0 != 0)
				break;

			RTMPusecDelay(1000);
		} while (Index++ < 1000);
	}

	RTMPAllocAdapterBlock(pAd);

	// Enable RF Tuning timer
	init_timer(&pAd->RfTuningTimer);
	pAd->RfTuningTimer.data = (unsigned long)pAd;
	pAd->RfTuningTimer.function = &AsicRfTuningExec;

	// 1. Allocate DMA descriptors & buffers
	status = RTMPAllocDMAMemory(pAd);
	if (status != NDIS_STATUS_SUCCESS)
		goto out_module_put;

	//
	// 1.1 Init TX/RX data structures and related parameters
	//
	NICInitTxRxRingAndBacklogQueue(pAd);

	// 2. request interrupt
	// Disable interrupts here which is as soon as possible
	// This statement should never be true. We might consider to remove it later
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_ACTIVE)) {
		NICDisableInterrupt(pAd);
	}

	status =
	    request_irq(pAd->pPci_Dev->irq, &RTMPIsr, SA_SHIRQ, net_dev->name,
			net_dev);
	if (status) {
		goto out_free_dma_memory;
	}
	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE);

	// Load firmware
	status = NICLoadFirmware(pAd);
	if (status != NDIS_STATUS_SUCCESS) {
		printk(KERN_ERR DRIVER_NAME
			": Could not load firmware! (is firmware file installed?)\n");
		goto out_no_firmware;
	}

	// Initialize Asics
	NICInitializeAdapter(pAd);

	// We should read EEPROM for all cases.
	NICReadEEPROMParameters(pAd);

	// hardware initialization after all parameters are acquired from
	// Registry or E2PROM
	TmpPhy = pAd->PortCfg.PhyMode;
	pAd->PortCfg.PhyMode = 0xff;
	RTMPSetPhyMode(pAd, TmpPhy);

	NICInitAsicFromEEPROM(pAd);

	//
	// other settings
	//

	// PCI_ID info
	pAd->VendorDesc =
	    (ULONG) (pAd->pPci_Dev->vendor << 16) + (pAd->pPci_Dev->device);

	// Note: DeviceID = 0x0302 shoud turn off the Aggregation.
	if (pAd->pPci_Dev->device == NIC2561_PCI_DEVICE_ID)
		pAd->PortCfg.bAggregationCapable = FALSE;

	RTMP_IO_READ32(pAd, MAC_CSR0, &MacCsr0);
	if (((MacCsr0 == 0x2561c) || (MacCsr0 == 0x2661d))
	    && (pAd->PortCfg.bAggregationCapable == TRUE)) {
		pAd->PortCfg.bPiggyBackCapable = TRUE;
	} else {
		pAd->PortCfg.bPiggyBackCapable = FALSE;
	}

	// external LNA For 5G has different R17 base
	if (pAd->NicConfig2.field.ExternalLNAForA) {
		pAd->BbpTuning.R17LowerBoundA += 0x10;
		pAd->BbpTuning.R17UpperBoundA += 0x10;
	}
	// external LNA For 2.4G has different R17 base
	if (pAd->NicConfig2.field.ExternalLNAForG) {
		pAd->BbpTuning.R17LowerBoundG += 0x10;
		pAd->BbpTuning.R17UpperBoundG += 0x10;
	}

	net_dev->dev_addr[0] = pAd->CurrentAddress[0];
	net_dev->dev_addr[1] = pAd->CurrentAddress[1];
	net_dev->dev_addr[2] = pAd->CurrentAddress[2];
	net_dev->dev_addr[3] = pAd->CurrentAddress[3];
	net_dev->dev_addr[4] = pAd->CurrentAddress[4];
	net_dev->dev_addr[5] = pAd->CurrentAddress[5];

	// initialize MLME
	status = MlmeInit(pAd);
	if (status != NDIS_STATUS_SUCCESS) {
		goto out_free_irq;
	}
	//
	// Enable Interrupt
	//
	RTMP_IO_WRITE32(pAd, INT_SOURCE_CSR, 0xffffffff);	// clear garbage interrupts
	NICEnableInterrupt(pAd);

	//
	// Now Enable Rx
	//
	RTMP_IO_WRITE32(pAd, RX_CNTL_CSR, 0x00000001);	// enable RX of DMA block
	RTMPWriteTXRXCsr0(pAd, FALSE, TRUE);

	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_START_UP);

	// Start net interface tx /rx
	netif_start_queue(net_dev);

	netif_carrier_on(net_dev);
	netif_wake_queue(net_dev);

	return 0;

      out_no_firmware:
	del_timer_sync(&pAd->RfTuningTimer);
	MlmeHalt(pAd);
#ifdef RX_TASKLET
	// Stop tasklet 
	tasklet_kill(&pAd->RxTasklet);
	while (TRUE) {
		skb = skb_dequeue(&pAd->RxQueue);
		if (!skb)
			break;
		dev_kfree_skb_irq(skb);
	}
#endif
	NICIssueReset(pAd);
	free_irq(net_dev->irq, net_dev);
	RTMPFreeDMAMemory(pAd);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	module_put(THIS_MODULE);
#else
	MOD_DEC_USE_COUNT;
#endif
	return status;

      out_free_irq:
	free_irq(net_dev->irq, net_dev);
      out_free_dma_memory:
	RTMPFreeDMAMemory(pAd);
      out_module_put:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	module_put(THIS_MODULE);
#else
	MOD_DEC_USE_COUNT;
#endif

	return status;
}

//
// Close driver function
//
static INT RT61_close(IN struct net_device *net_dev)
{
	RTMP_ADAPTER *pAd = net_dev->priv;
	// LONG            ioaddr = net_dev->base_addr;
#ifdef RX_TASKLET
	struct sk_buff *skb;
#endif

	DBGPRINT(RT_DEBUG_TRACE, "===> RT61_close\n");

	LinkDown(pAd, FALSE);

	// Stop Mlme state machine
	del_timer_sync(&pAd->RfTuningTimer);
	MlmeHalt(pAd);

#ifdef RX_TASKLET
	// Stop tasklet 
	tasklet_kill(&pAd->RxTasklet);
	while (TRUE) {
		skb = skb_dequeue(&pAd->RxQueue);
		if (!skb)
			break;
		dev_kfree_skb_irq(skb);
	}

#endif
	netif_stop_queue(net_dev);
	netif_carrier_off(net_dev);

	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_ACTIVE)) {
		NICDisableInterrupt(pAd);
	}
	// Disable Rx, register value supposed will remain after reset
	NICIssueReset(pAd);

	// Free IRQ
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		// Deregister interrupt function
		free_irq(net_dev->irq, net_dev);
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE);
	}
	// Free Ring buffers
	RTMPFreeDMAMemory(pAd);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	module_put(THIS_MODULE);
#else
	MOD_DEC_USE_COUNT;
#endif

	return 0;
}

//
// PCI device probe & initialization function
//
static INT __devinit RT61_probe(IN struct pci_dev * pPci_Dev,
			 IN const struct pci_device_id * ent)
{
	struct net_device *net_dev;
	RTMP_ADAPTER *pAd;
	CHAR *print_name;
	INT chip_id = (INT) ent->driver_data;
	void __iomem *csr_addr;
	INT Status = -ENODEV;
	INT i;

	printk("%s %s %s http://rt2x00.serialmonkey.com\n",
	       KERN_INFO DRIVER_NAME, DRIVER_VERSION, DRIVER_RELDATE);

	print_name = pPci_Dev ? pci_name(pPci_Dev) : "rt61";

	// alloc_etherdev() will set net_dev->name
	net_dev = alloc_etherdev(0);
	if (net_dev == NULL) {
		DBGPRINT(RT_DEBUG_TRACE, "init_ethernet failed\n");
		goto err_out;
	}

	SET_MODULE_OWNER(net_dev);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
	SET_NETDEV_DEV(net_dev, &(pPci_Dev->dev));
#endif

	if (pci_request_regions(pPci_Dev, print_name))
		goto err_out_free_netdev;

	for (i = 0; i < rt61_pci_tbl_len; i++) {
		if (pPci_Dev->vendor == rt61_pci_tbl[i].vendor &&
		    pPci_Dev->device == rt61_pci_tbl[i].device) {
			printk("RT61: Vendor = 0x%04x, Product = 0x%04x \n",
			       pPci_Dev->vendor, pPci_Dev->device);
			break;
		}
	}
	if (i == rt61_pci_tbl_len) {
		printk("Device PID/VID not matching!!!\n");
		goto err_out_free_netdev;
	}
	// Interrupt IRQ number
	net_dev->irq = pPci_Dev->irq;

	// map physical address to virtual address for accessing register
	csr_addr =
	    ioremap(pci_resource_start(pPci_Dev, 0),
		    pci_resource_len(pPci_Dev, 0));
	if (!csr_addr) {
		DBGPRINT(RT_DEBUG_ERROR,
			 "ioremap failed for device %s, region 0x%X @ 0x%X\n",
			 print_name, (ULONG) pci_resource_len(pPci_Dev, 0),
			 (ULONG) pci_resource_start(pPci_Dev, 0));
		goto err_out_free_res;
	}

	net_dev->priv =
	    pci_alloc_consistent(pPci_Dev, sizeof(RTMP_ADAPTER), &dma_adapter);

	// Zero init RTMP_ADAPTER
	memset(net_dev->priv, 0, sizeof(RTMP_ADAPTER));

	// Save CSR virtual address and irq to device structure
	net_dev->base_addr = (unsigned long)csr_addr;
	pAd = net_dev->priv;
	pAd->CSRBaseAddress = csr_addr;
	pAd->net_dev = net_dev;

	// Set DMA master
	pci_set_master(pPci_Dev);

	pAd->chip_id = chip_id;
	pAd->pPci_Dev = pPci_Dev;

	// The chip-specific entries in the device structure.
	net_dev->open = RT61_open;
	net_dev->hard_start_xmit = RTMPSendPackets;
	net_dev->stop = RT61_close;
	net_dev->get_stats = RT61_get_ether_stats;

#if WIRELESS_EXT >= 12
#if WIRELESS_EXT < 17
	net_dev->get_wireless_stats = RT61_get_wireless_stats;
#endif
	net_dev->wireless_handlers =
	    (struct iw_handler_def *)&rt61_iw_handler_def;
#endif

	net_dev->set_multicast_list = RT61_set_rx_mode;
	net_dev->do_ioctl = RT61_ioctl;
	net_dev->set_mac_address = rt61_set_mac_address;

	// register_netdev() will call dev_alloc_name() for us
	// TODO: Remove the following line to keep the default eth%d name
	if (ifname == NULL)
		strcpy(net_dev->name, "wlan%d");
	else
		strncpy(net_dev->name, ifname, IFNAMSIZ);

	// Register this device
	Status = register_netdev(net_dev);
	if (Status)
		goto err_out_unmap;

	DBGPRINT(RT_DEBUG_TRACE, "%s: at 0x%x, VA 0x%lx, IRQ %d. \n",
		 net_dev->name, (ULONG) pci_resource_start(pPci_Dev, 0),
		 (unsigned long)csr_addr, pPci_Dev->irq);

	// Set driver data
	pci_set_drvdata(pPci_Dev, net_dev);

	// moved to here by GertjanW (RobinC) so if-preup can work
	// When driver now loads it is loaded up with "factory" defaults
	// All this occurs while the net iface is down
	// iwconfig can then be used to configure card BEFORE
	// ifconfig ra0 up is applied.
	// Note the rt61sta.dat file will still overwrite settings
	// but it is useful for the settings iwconfig doesn't let you at
	PortCfgInit(pAd);

	Status = MlmeQueueInit(&pAd->Mlme.Queue);
	if (Status != NDIS_STATUS_SUCCESS)
		goto err_out_unmap;

	// Build channel list for default physical mode
	BuildChannelList(pAd);

	rt61pci_open_debugfs(pAd);

	return 0;

      err_out_unmap:
	iounmap(csr_addr);
      err_out_free_res:
	pci_release_regions(pPci_Dev);

      err_out_free_netdev:
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	kfree(net_dev);
#else
	free_netdev(net_dev);
#endif

      err_out:
	return Status;
}


static INT __devinit RT61_init_one(IN struct pci_dev *pPci_Dev,
				   IN const struct pci_device_id *ent)
{
	INT rc;

	DBGPRINT(RT_DEBUG_TRACE, "===> RT61_init_one\n");

	// wake up and enable device
	if (pci_enable_device(pPci_Dev))
		rc = -EIO;
	else {
		rc = RT61_probe(pPci_Dev, ent);
		if (rc)
			pci_disable_device(pPci_Dev);
	}

	DBGPRINT(RT_DEBUG_TRACE, "<=== RT61_init_one\n");
	return rc;
}


//
// Remove driver function
//
static VOID __devexit RT61_remove_one(IN struct pci_dev *pPci_Dev)
{
	struct net_device *net_dev = pci_get_drvdata(pPci_Dev);
	RTMP_ADAPTER *pAd = net_dev->priv;

	DBGPRINT(RT_DEBUG_TRACE, "===> RT61_remove_one\n");

	rt61pci_close_debugfs(pAd);
	// Unregister network device
	unregister_netdev(net_dev);

	// Unmap CSR base address
	iounmap((void *)(net_dev->base_addr));

	pci_free_consistent(pAd->pPci_Dev, sizeof(RTMP_ADAPTER), pAd,
			    dma_adapter);

	// release memory regions
	pci_release_regions(pPci_Dev);

	// disable the device
	pci_disable_device(pPci_Dev);

	// Free pre-allocated net_device memory
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	kfree(net_dev);
#else
	free_netdev(net_dev);
#endif
}

//
// prepare for software suspend
//
#ifdef CONFIG_PM
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14))
#ifndef pm_message_t
#define pm_message_t u32
#endif
#endif				/* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)) */

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,9))
static u32 suspend_buffer[16];
#define rt2x00_save_state(__pci)        pci_save_state(__pci, suspend_buffer)
#define rt2x00_restore_state(__pci)     pci_restore_state(__pci, suspend_buffer)
#else				/* (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,8)) */
#define rt2x00_save_state(__pci)        pci_save_state(__pci)
#define rt2x00_restore_state(__pci)     pci_restore_state(__pci)
#endif				/* (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,8)) */

static int rt61_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14))
	printk(KERN_NOTICE "%s: got suspend request (state %d)\n", dev->name,
	       state);
#else
	printk(KERN_NOTICE "%s: got suspend request (event %d)\n", dev->name,
	       state.event);
#endif				/* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)) */

	rt2x00_save_state(pdev);

	NICDisableInterrupt(pAdapter);
	MlmeHalt(pAdapter);

	netif_stop_queue(dev);
	netif_device_detach(dev);

	return 0;
}

//
// reactivate after software suspend
//
static int rt61_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	PRTMP_ADAPTER pAdapter = (PRTMP_ADAPTER) dev->priv;
	int status;

	pci_enable_device(pdev);

	printk(KERN_NOTICE "%s: got resume request\n", dev->name);

	rt2x00_restore_state(pdev);

	NICInitTxRxRingAndBacklogQueue(pAdapter);
	status = MlmeInit(pAdapter);
	NICInitializeAdapter(pAdapter);
	NICEnableInterrupt(pAdapter);

	netif_device_attach(dev);
	netif_start_queue(dev);

	return 0;
}
#endif				/* CONFIG_PM */

// =======================================================================
// Our PCI driver structure
// =======================================================================
static struct pci_driver rt61_driver = {
      name:"rt61",
      id_table:rt61_pci_tbl,
      probe:RT61_init_one,
#ifdef CONFIG_PM
      suspend:rt61_suspend,
      resume:rt61_resume,
#endif
#if LINUX_VERSION_CODE >= 0x20412 || BIG_ENDIAN == TRUE || RTMP_EMBEDDED == TRUE
      remove:__devexit_p(RT61_remove_one),
#else
      remove:__devexit(RT61_remove_one),
#endif
};

// =======================================================================
// LOAD / UNLOAD sections
// =======================================================================
//
// Driver module load function
//
static INT __init rt61_init_module(VOID)
{
	return pci_module_init(&rt61_driver);
}

//
// Driver module unload function
//
static VOID __exit rt61_cleanup_module(VOID)
{
	pci_unregister_driver(&rt61_driver);
}

/*************************************************************************/
// Following information will be show when you run 'modinfo'
// *** If you have a solution for the bug in current version of driver, please mail to me.
// Otherwise post to forum in ralinktech's web site(www.ralinktech.com) and let all users help you. ***

MODULE_AUTHOR("http://rt2x00.serialmonkey.com");
MODULE_DESCRIPTION("Ralink RT61 802.11abg WLAN Driver " DRIVER_VERSION " "
		   DRIVER_RELDATE);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, rt61_pci_tbl);

module_init(rt61_init_module);
module_exit(rt61_cleanup_module);
/*************************************************************************/
