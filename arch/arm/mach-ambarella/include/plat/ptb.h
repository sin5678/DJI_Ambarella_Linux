/*
 * arch/arm/plat-ambarella/include/plat/ptb.h
 *
 * Author: Anthony Ginger <hfjiang@ambarella.com>
 *
 * Copyright (C) 2004-2010, Ambarella, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef __PLAT_AMBARELLA_PTB_H
#define __PLAT_AMBARELLA_PTB_H

/* ==========================================================================*/
#ifndef CONFIG_PLAT_AMBARELLA_AMBALINK
#define BOOT_DEV_NAND		0x1
#define BOOT_DEV_NOR		0x2
#define BOOT_DEV_SM		0x3
#define BOOT_DEV_ONENAND	0x4
#define BOOT_DEV_SNOR		0x5
#else
#define BOOT_DEV_NAND		0x0
#define BOOT_DEV_SM		0x1
#define BOOT_DEV_NOR		0x2
#define BOOT_DEV_ONENAND	0x3
#define BOOT_DEV_SNOR		0x4
#endif

#define PART_DEV_AUTO		(0x00)
#define PART_DEV_NAND		(0x01)
#define PART_DEV_EMMC		(0x02)
#define PART_DEV_SNOR		(0x04)

#define FW_MODEL_NAME_SIZE	128

#define PART_MAX_WITH_RSV	32

#ifndef CONFIG_PLAT_AMBARELLA_AMBALINK
#define PART_MAX		20
#else
#if ((CHIP_REV == S2E) || (CHIP_REV == S3))
#define PART_MAX		13
#else
#define PART_MAX		15
#endif
#endif

#define CMDLINE_PART_MAX	8

#define HAS_IMG_PARTS		15
#define HAS_NO_IMG_PARTS	1
#define TOTAL_FW_PARTS		(HAS_IMG_PARTS + HAS_NO_IMG_PARTS)

/* ==========================================================================*/
#ifndef __ASSEMBLER__

typedef struct flpart_s
{
	u32	crc32;		/**< CRC32 checksum of image */
	u32	ver_num;	/**< Version number */
	u32	ver_date;	/**< Version date */
	u32	img_len;	/**< Lengh of image in the partition */
	u32	mem_addr;	/**< Starting address to copy to RAM */
	u32	flag;		/**< Special properties of this partition */
	u32	magic;		/**< Magic number */
} __attribute__((packed)) flpart_t;

#define FLDEV_CMD_LINE_SIZE	1024

#ifndef CONFIG_PLAT_AMBARELLA_AMBALINK
typedef struct netdev_s
{
	/* This section contains networking related settings */
	u8	mac[6];		/**< MAC address*/
	u32	ip;		/**< Boot loader's LAN IP */
	u32	mask;		/**< Boot loader's LAN mask */
	u32	gw;		/**< Boot loader's LAN gateway */
} __attribute__((packed)) netdev_t;
#else
typedef struct netdev_s
{
	/* This section contains networking related settings */
	u8	mac[6];		/**< MAC address*/
	u8	rsv[2];
	u32	ip;		/**< Boot loader's LAN IP */
	u32	mask;		/**< Boot loader's LAN mask */
	u32	gw;		/**< Boot loader's LAN gateway */
} __attribute__((packed)) netdev_t;
#endif

#ifndef CONFIG_PLAT_AMBARELLA_AMBALINK
typedef struct fldev_s
{
	char	sn[32];		/**< Serial number */
	u8	usbdl_mode;	/**< USB download mode */
	u8	auto_boot;	/**< Automatic boot */
	u8	rsv[2];
	u32	splash_id;
	char	cmdline[FLDEV_CMD_LINE_SIZE];

	/* This section contains networking related settings */
	netdev_t eth[2];
	netdev_t wifi[2];
	netdev_t usb_eth[2];

	/* This section contains update by network  related settings */
	u32	auto_dl;	/**< Automatic download? */
	u32	tftpd;		/**< Boot loader's TFTP server */
	u32	pri_addr;	/**< RTOS download address */
	char	pri_file[32];	/**< RTOS file name */
	u32	pri_comp;	/**< RTOS compressed? */
	u32	dtb_addr;	/**< DTB download address */
	char	dtb_file[32];	/**< DTB file name */
	u32	rmd_addr;	/**< Ramdisk download address */
	char	rmd_file[32];	/**< Ramdisk file name */
	u32	rmd_comp;	/**< Ramdisk compressed? */
	u32	dsp_addr;	/**< DSP download address */
	char	dsp_file[32];	/**< DSP file name */
	u32	dsp_comp;	/**< DSP compressed? */

	u32	magic;		/**< Magic number */
} __attribute__((packed)) fldev_t;
#else
typedef struct fldev_s
{
	char	sn[32];		/**< Serial number */
	u8	usbdl_mode;	/**< USB download mode */
	u8	auto_boot;	/**< Automatic boot */
	u8	rsv[2];
	char	cmdline[FLDEV_CMD_LINE_SIZE];	/**< Boot command line options */
	u32	splash_id;

	/* This section contains networking related settings */
	netdev_t wifi[2];

	u32	magic;		/**< Magic number */
} __attribute__((packed)) fldev_t;

#endif

#ifndef CONFIG_PLAT_AMBARELLA_AMBALINK
#define PTB_SIZE		4096
#define PTB_PAD_SIZE		\
	(PTB_SIZE - PART_MAX_WITH_RSV * sizeof(flpart_t) - sizeof(fldev_t))

typedef struct flpart_table_s
{
	flpart_t	part[PART_MAX_WITH_RSV];/** Partitions */
	/* ------------------------------------------ */
	fldev_t		dev;			/**< Device properties */
	u8		rsv[PTB_PAD_SIZE];	/**< Padding to 2048 bytes */
} __attribute__((packed)) flpart_table_t;
#else
#define PTB_SIZE		4096
#define PTB_PAD_SIZE		\
	(PTB_SIZE - PART_MAX_WITH_RSV * sizeof(flpart_t) - sizeof(fldev_t))

typedef struct flpart_table_s
{
	flpart_t	part[PART_MAX_WITH_RSV];/** Partitions */
	/* ------------------------------------------ */
	fldev_t		dev;			/**< Device properties */
	u8		rsv[PTB_PAD_SIZE];	/**< Padding to 2048 bytes */
} __attribute__((packed)) flpart_table_t;
#endif

/**
 * The meta data table is a region in flash after partition table.
 * The data need by dual boot are stored.
 */
/* For first version of flpart_meta_t */
#define PTB_META_MAGIC		0x33219fbd
/* For second version of flpart_meta_t */
#define PTB_META_MAGIC2		0x4432a0ce
#define PTB_META_MAGIC3		0x42405891

#ifndef CONFIG_PLAT_AMBARELLA_AMBALINK
#define PART_NAME_LEN		8
#define PTB_META_ACTURAL_LEN	((sizeof(u32) * 2 + PART_NAME_LEN + \
				sizeof(u32)) * PART_MAX + sizeof(u32) + \
				sizeof(u32) + FW_MODEL_NAME_SIZE)
#else
#define PART_NAME_LEN		32
#define PTB_META_ACTURAL_LEN            \
    ((sizeof(u32) * 2 + PART_NAME_LEN + sizeof(u32)) * PART_MAX + \
     sizeof(u32) + sizeof(u32) + FW_MODEL_NAME_SIZE)
#endif
#define PTB_META_SIZE		2048
#define PTB_META_PAD_SIZE	(PTB_META_SIZE - PTB_META_ACTURAL_LEN)
#ifndef CONFIG_PLAT_AMBARELLA_AMBALINK
typedef struct flpart_meta_s
{
	struct {
		u32	sblk;
		u32	nblk;
		char	name[PART_NAME_LEN];
	} part_info[PART_MAX];
	u32		magic;
	u32		part_dev[PART_MAX];
	u8		model_name[FW_MODEL_NAME_SIZE];
	u8 		rsv[PTB_META_PAD_SIZE];
	/* This meta crc32 doesn't include itself. */
	/* It's only calc data before this field.  */
	u32 	crc32;
} __attribute__((packed)) flpart_meta_t;
#else
#if ((CHIP_REV == S2E) || (CHIP_REV == S3))

/*---------------------------------------------------------------------------*\
 * definitions of User partition ID
\*---------------------------------------------------------------------------*/
typedef enum _AMBA_USER_PARTITION_ID_e_ {
    AMBA_USER_PARTITION_VENDOR_DATA = 0,        /* Vendor Specific Data */

    AMBA_USER_PARTITION_SYS_SOFTWARE,           /* System Software */
    AMBA_USER_PARTITION_DSP_uCODE,              /* DSP uCode (ROM Region) */
    AMBA_USER_PARTITION_SYS_DATA,               /* System Data (ROM Region) */

    AMBA_USER_PARTITION_LINUX_KERNEL,           /* Linux Kernel */
    AMBA_USER_PARTITION_LINUX_ROOT_FS,          /* Linux Root File System */
    AMBA_USER_PARTITION_LINUX_HIBERNATION_IMG,  /* Linux Hibernation Image */

    AMBA_USER_PARTITION_VIDEO_REC_INDEX,        /* Video Recording Index */
    AMBA_USER_PARTITION_CALIBRATION_DATA,       /* Calibration Data */

    AMBA_USER_PARTITION_USER_SETTING,           /* User Settings */
    AMBA_USER_PARTITION_FAT_DRIVE_A,            /* Internal Storage FAT Drive 'A' */
    AMBA_USER_PARTITION_FAT_DRIVE_B,            /* Internal Storage FAT Drive 'B' */

    AMBA_USER_PARTITION_RESERVED,               /* Reserved User Partition */

    AMBA_NUM_USER_PARTITION                     /* Total Number of User Partitions */
} AMBA_USER_PARTITION_ID_e;

/*---------------------------------------------------------------------------*\
 * Partition entry structure
\*---------------------------------------------------------------------------*/
typedef struct _AMBA_PARTITION_ENTRY_s_ {
	u8   name[32];		/* Partition Name */
	u32  Attribute;		/* Attribute of the Partition */
	u32  ByteCount;		/* number of Bytes allocated for the Partition */
	u32  nblk;		/* number of Blocks for the Partition  */
	u32  sblk;		/* start Block Address */
	u32  RamLoadAddr;		/* Load address of this partiontion in RAM */

	u32  ActualByteSize;	/* actual size in Bytes */
	u32  ProgramStatus;		/* the Status of after programming the Partition */

	u32  ImageCRC32;		/* CRC32 of the Image inside the Partition */
} AMBA_PARTITION_ENTRY_s;

/*---------------------------------------------------------------------------*\
 * definitions of partal load information
\*---------------------------------------------------------------------------*/
#define PLOAD_REGION_NUM                6
typedef struct _AMBA_PLOAD_PARTITION_s_ {
        u32  RegionRoStart[PLOAD_REGION_NUM];
        u32  RegionRwStart[PLOAD_REGION_NUM];
        u32  RegionRoSize [PLOAD_REGION_NUM];
        u32  RegionRwSize [PLOAD_REGION_NUM];
        u32  LinkerStubStart;
        u32  LinkerStubSize;
        u32  DspBufStart;
        u32  DspBufSize;
} AMBA_PLOAD_PARTITION_s;

/*---------------------------------------------------------------------------*\
 * User Partition table
\*---------------------------------------------------------------------------*/
typedef struct flpart_meta_s_ {
	u32	Version;		/* the version of the Partition Table */
	u32	magic;			/* Bootloader Magic Code: set - Load SysSW; clear - FwUpdater */
	AMBA_PARTITION_ENTRY_s  part_info[AMBA_NUM_USER_PARTITION];

	int	NAND_BlkAddrPrimaryBBT;	/* NAND Block address of Primary BBT(Bad Block Table) */
	int	NAND_BlkAddrMirrorBBT;	/* NAND Block address of Mirror BBT(Bad Block Table) */

        AMBA_PLOAD_PARTITION_s PloadInfo;   /* Pload region info */

	u32	crc32;			/* CRC32 of User Partition Table */
} __attribute__((packed)) flpart_meta_t;
#else
typedef struct flpart_meta_s
{
	struct {
		u32	sblk;
		u32	nblk;
		char	name[PART_NAME_LEN];
	} part_info[PART_MAX];

	u32	magic;				/**< Magic number */
	u32	part_dev[PART_MAX];
	u8	model_name[FW_MODEL_NAME_SIZE];

#ifndef CONFIG_PLAT_AMBARELLA_AMBALINK
	struct {
		u32	sblk;
		u32	nblk;
	} sm_stg[2];
#endif
	/* This meta crc32 doesn't include itself. */
	/* It's only calc data before this field.  */
	u32 	crc32;

	u8 	rsv[PTB_META_PAD_SIZE];
} __attribute__((packed)) flpart_meta_t;
#endif
#endif

#endif /* __ASSEMBLER__ */

/* ==========================================================================*/

#endif

