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
 *      Module Name: rt_config.h
 *
 *      Abstract:
 *
 *      Revision History:
 *      Who             When            What
 *      --------        -----------     -----------------------------
 *      RoryC           10th Feb 05     Porting from RT2500
 *      GertjanW        21st Jan 06     Baseline code
 ***************************************************************************/

#ifndef __RT_CONFIG_H__
#define __RT_CONFIG_H__

#define PROFILE_PATH                "/etc/Wireless/RT61STA/rt61sta.dat"
#define NIC_DEVICE_NAME             "RT61STA"
// Super mode, RT2561S: super(high throughput, aggregiation, piggy back), RT2561T: Package type(TQFP)
#define RT2561_IMAGE_FILE_NAME      "/etc/Wireless/RT61STA/rt2561.bin"
#define RT2561S_IMAGE_FILE_NAME     "/etc/Wireless/RT61STA/rt2561s.bin"
#define RT2661_IMAGE_FILE_NAME      "/etc/Wireless/RT61STA/rt2661.bin"
#define RALINK_PASSPHRASE           "Ralink"
#define DRIVER_NAME                 "rt61"
#define DRIVER_VERSION              "1.1.0 CVS"
#define DRIVER_RELDATE              "CVS"

// Query from UI
#define DRV_MAJORVERSION            1
#define DRV_MINORVERSION            1
#define DRV_SUBVERSION	            0
#define DRV_TESTVERSION	            0
#define DRV_YEAR                    2006
#define DRV_MONTH                   6
#define DRV_DAY	                    18

/* Operational parameters that are set at compile time. */
#if !defined(__OPTIMIZE__)  ||  !defined(__KERNEL__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error  You must compile this driver with "-O".
#endif

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>

#include <linux/init.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/wireless.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/if_arp.h>
#include <linux/ctype.h>

#if LINUX_VERSION_CODE >= 0x20407
#include <linux/mii.h>
#endif

// load firmware
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#include <asm/uaccess.h>

#define NONCOPY_RX

#ifndef ULONG
#define CHAR            signed char
#define INT             int
#define SHORT           int
#define UINT            u32
#define ULONG           u32
#define USHORT          u16
#define UCHAR           u8

#define uint32			u32
#define uint8			u8

#define BOOLEAN         u8
//#define LARGE_INTEGER s64
#define VOID            void
#define LONG            int
#define LONGLONG        s64
#define ULONGLONG       u64
typedef VOID *PVOID;
typedef CHAR *PCHAR;
typedef UCHAR *PUCHAR;
typedef USHORT *PUSHORT;
typedef LONG *PLONG;
typedef ULONG *PULONG;

typedef union _LARGE_INTEGER {
	struct {
		ULONG LowPart;
		LONG HighPart;
	} vv;
	struct {
		ULONG LowPart;
		LONG HighPart;
	} u;
	s64 QuadPart;
} LARGE_INTEGER;

#endif

#define IN
#define OUT

#define TRUE        1
#define FALSE       0

#define NDIS_STATUS                             INT
#define NDIS_STATUS_SUCCESS                     0x00
#define NDIS_STATUS_FAILURE                     0x01
#define NDIS_STATUS_RESOURCES                   0x03

#ifdef __BIG_ENDIAN
#warning Compiling for big endian machine.
#define BIG_ENDIAN TRUE
#endif				/* __BIG_ENDIAN */

#include    "rtmp_type.h"
#include    "rtmp_def.h"
#include    "rt2661.h"
#include	"rt2x00debug.h"
#include    "rtmp.h"
#include    "mlme.h"
#include    "oid.h"
#include    "wpa.h"
#include    "md5.h"

#define	NIC2661_PCI_DEVICE_ID	    0x0401
#define NIC2561Turbo_PCI_DEVICE_ID  0x0301
#define NIC2561_PCI_DEVICE_ID	    0x0302
#define	NIC_PCI_VENDOR_ID		    0x1814

#define MEM_ALLOC_FLAG      (GFP_DMA | GFP_ATOMIC)

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) ((a)*65536+(b)*256+(c))
#endif

#if 1
#define RT_READ_PROFILE		// Driver reads RaConfig profile parameters from rt61sta.dat
#endif

#define SL_IRQSAVE          1	// 0: use spin_lock_bh/spin_unlock_bh pair,
			       // 1: use spin_lock_irqsave/spin_unlock_irqrestore pair

#endif				// __RT_CONFIG_H__
