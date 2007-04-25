###########################################################################
# RT2400/RT2500 SourceForge Project - http://rt2x00.serialmonkey.com      #
#                                                                         #
#   This program is free software; you can redistribute it and/or modify  #
#   it under the terms of the GNU General Public License as published by  #
#   the Free Software Foundation; either version 2 of the License, or     #
#   (at your option) any later version.                                   #
#                                                                         #
#   This program is distributed in the hope that it will be useful,       #
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        #
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
#   GNU General Public License for more details.                          #
#                                                                         #
#   You should have received a copy of the GNU General Public License     #
#   along with this program; if not, write to the                         #
#   Free Software Foundation, Inc.,                                       #
#   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             #
#                                                                         #
#   Licensed under the GNU GPL                                            #
#   Original code supplied under license from RaLink Inc, 2004.           #
###########################################################################

###########################################################################
#      Module Name: Makefile
#
#      Abstract: Makefile for rt61 kernel module
#
#      Revision History:
#      Who             When             What
#      --------        -----------      -----------------------------
#      MarkW (rt2500)  21st Jan 06      Rewrite of Makefile
#      Amir S (rt2500) 2nd  Feb 06      Update for gmake compat
#      MarkW (rt2500)  3rd  Feb 06      Fixed permissions on directory
#      MichaIL(rt2500) 13th Feb 06      Module installation fixes
#      MarkW (rt2500)  13th Feb 06      Allow install dir override
###########################################################################



all: module
MODULE_NAME := rt61

ifeq ($(PATCHLEVEL),)
 PATCHLEVEL := $(shell uname -r | cut -d. -f2)
endif

RESMAN_CORE_OBJS := rtmp_main.o
RESMAN_GLUE_OBJS := mlme.o connect.o sync.o assoc.o auth.o auth_rsp.o rtmp_data.o rtmp_init.o sanity.o rtmp_wep.o wpa.o md5.o rtmp_tkip.o rtmp_info.o eeprom.o

$(MODULE_NAME)-objs := $(RESMAN_CORE_OBJS) $(RESMAN_GLUE_OBJS)

ifdef TOPDIR
obj-m += $(MODULE_NAME).o
endif

EXTRA_CFLAGS += -I$(src)
EXTRA_CFLAGS += -DAGGREGATION_SUPPORT -DWMM_SUPPORT
#EXTRA_CFLAGS += -DNO_RX_TASKLET
#EXTRA_CFLAGS += -DRALINK_ATE
#EXTRA_CFLAGS += -DSINGLE_ADHOC_LINKUP

ifdef KERNDIR
 KERNEL_SOURCES := $(KERNDIR)
else
 KERNEL_SOURCES := /lib/modules/$(shell uname -r)/build
endif

ifdef MODDIR
 MODULE_ROOT := $(MODDIR)
else
 MODULE_ROOT := /lib/modules/$(shell uname -r)/extra
endif

ifdef KERNOUT
 KERNEL_OUTPUT := KBUILD_OUTPUT=$(KERNOUT)
else
 KERNEL_OUTPUT :=
endif

src ?= .
obj ?= .

ifeq ($(PATCHLEVEL), 4)
 MODULE_OBJECT := $(MODULE_NAME).o
 MODULE_CONF := /etc/modules.conf
else
 MODULE_OBJECT := $(MODULE_NAME).ko
 MODULE_CONF := /etc/modprobe.conf
endif

ifeq ($(PATCHLEVEL), 4)
$(obj)/$(MODULE_NAME).o: $($(MODULE_NAME)-objs)
	$(LD) $(EXTRA_LDFLAGS) -r -o $@ $($(MODULE_NAME)-objs)
endif

KBUILD_PARAMS := -C $(KERNEL_SOURCES) SUBDIRS=$(PWD) $(KERNEL_OUTPUT)

module:
	@$(MAKE) $(KBUILD_PARAMS) modules; \
	if ! [ -f $(MODULE_OBJECT) ]; then \
		echo "$(MODULE_OBJECT) failed to build!"; \
		exit 1; \
	fi

debug:
	@$(MAKE) $(KBUILD_PARAMS) 'EXTRA_CFLAGS=$(EXTRA_CFLAGS) -DRT61_DBG' modules; \
	if ! [ -f $(MODULE_OBJECT) ]; then \
		echo "$(MODULE_OBJECT) failed to build!"; \
		exit 1; \
	fi

debugfs:
	@$(MAKE) $(KBUILD_PARAMS) 'EXTRA_CFLAGS=$(EXTRA_CFLAGS) -DRT61_DBG -DRT2X00DEBUGFS' modules; \
	if ! [ -f $(MODULE_OBJECT) ]; then \
	echo "$(MODULE_OBJECT) failed to build!"; \
	exit 1; \
	fi

clean:
	@rm -f $(RESMAN_GLUE_OBJS) $(RESMAN_CORE_OBJS) .*.{cmd,flags}
	@rm -f $(MODULE_NAME).{o,ko,mod.{o,c}} built-in.o $(VERSION_HEADER) *~
	@rm -fr .tmp_versions Module.symvers

modules_install:
	@if ! [ -f $(MODULE_OBJECT) ]; then \
		$(MAKE) module; \
	fi
ifeq ($(PATCHLEVEL),4)
	@echo "install '$(MODULE_OBJECT)' to $(MODULE_ROOT)"
	install -m 755 -o 0 -g 0 -d $(MODULE_ROOT)
	install -m 644 -o 0 -g 0 $(MODULE_OBJECT) $(MODULE_ROOT)
	/sbin/depmod -a
else
	@echo "2.6 module install"
	make $(KBUILD_PARAMS) modules_install
	/sbin/depmod -a
endif

install: modules_install
	@if ! grep -q 'ra0' $(MODULE_CONF) ; then \
		echo "append 'alias ra0 rt61' to $(MODULE_CONF)"; \
		echo "alias ra0 rt61" >> $(MODULE_CONF) ; \
	fi

install-fedora:
	if ! [ -f $(MODULE_OBJECT) ]; then \
		module; \
	fi
	@echo "install '$(MODULE_OBJECT)' to $(MODULE_ROOT)"
	install -m 755 -o 0 -g 0 -d $(MODULE_ROOT)
	install -m 644 -o 0 -g 0 $(MODULE_OBJECT) $(MODULE_ROOT)
	/sbin/depmod -a

	@if ! grep -q 'wlan0' /etc/modprobe.conf ; then \
		echo "append 'alias wlan0 rt61' to /etc/modprobe.conf"; \
		echo "alias wlan0 rt61" >> /etc/modprobe.conf ; \
		echo "options rt61 ifname=wlan%d" >> /etc/modprobe.conf ; \
	fi

ifeq ($(PATCHLEVEL), 4)
include $(KERNEL_SOURCES)/Rules.make
endif
