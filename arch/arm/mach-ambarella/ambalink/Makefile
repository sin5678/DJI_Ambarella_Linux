#
# arch/arm/plat-ambarella/ambalink/Makefile
#
# Author: Anthony Ginger <hfjiang@ambarella.com>
#
# Copyright (C) 2004-2011, Ambarella, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
#

obj-y                                           += init.o
obj-y                                           += pm.o
obj-$(CONFIG_AMBALINK_LOCK)	 		+= aipc_slock.o
obj-$(CONFIG_AMBALINK_LOCK) 			+= aipc_mutex.o
obj-$(CONFIG_AMBALINK_RPC)	 		+= aipc_nl.o
obj-$(CONFIG_AMBALINK_RPC) 			+= aipc_router.o
obj-y 						+= rpmsg_rpc.o
obj-y 						+= rpmsg_rfs.o
obj-y                      			+= ambafs/
obj-y                         			+= rpmsg_clk.o
obj-$(CONFIG_RPMSG_SD)                          += rpmsg_sd.o
obj-y				                += rpmsg_linkctrl.o
obj-$(CONFIG_RPMSG_VFFS)			+= rpmsg_vffs.o
