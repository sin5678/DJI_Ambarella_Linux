/*----------------------------------------------------------------------------*\
 *  @FileName       :: rpmsg_transfer.h
 *
 *  @Copyright      :: Copyright (C) 2013 DJI Innovations. All rights reserved.
 *
 *                     No part of this file may be reproduced, stored in a
 *                     retrieval system, or transmitted, in any form, or by any
 *                     means, electronic, mechanical, photocopying, recording,
 *                     or otherwise, without the prior consent of DJI Innovations.
 *
 *  @Description    :: Image transfer module MACRO/struct or global function description.
 *
 *  @History        ::
\*----------------------------------------------------------------------------*/
#ifndef _DEFINE_FILE_RPMSG_TRANSFER_H_
#define _DEFINE_FILE_RPMSG_TRANSFER_H_

#define DRIVER_VERSION   "v1.00"
#define DEBUG_DIR_NAME   ("dji")
#define MAX_PROC_NAME_LEN (256)

/* decide wether has 68013 */
#undef  HAS_USB_68013

/* IPC Message feature */
#undef  IPC_WITH_CRC
#define IPC_WITH_TIMESTAMP

#define RPMSG_CHNNUM    3
#define RPMSG_CHNNAME0  "dji_test"
#define RPMSG_CHNNAME1  "dji_usr"
#define RPMSG_CHNNAME2  "dji_drv"
#define RPMSG_CHN_TEST  0

#define DJI_IPC_C0_PID_D0         0x55
#define DJI_IPC_C0_PID_TEST       ((DJI_IPC_C0_PID_D0 << 8) | 0)
#define DJI_IPC_C0_PID_UPDATE     ((DJI_IPC_C0_PID_D0 << 8) | 1)
#define DJI_IPC_C0_PID_IMAGE      ((DJI_IPC_C0_PID_D0 << 8) | 2)
#define DJI_IPC_C0_PID_COMMAND    ((DJI_IPC_C0_PID_D0 << 8) | 3)
#define DJI_IPC_C0_PID_MCCTRL     ((DJI_IPC_C0_PID_D0 << 8) | 4)
#define DJI_IPC_C0_PID_DOWNLOAD   ((DJI_IPC_C0_PID_D0 << 8) | 5)
#define DJI_IPC_C0_PID_RSH        ((DJI_IPC_C0_PID_D0 << 8) | 8)

#define DJI_IPC_C1_PID_D0         0x66
#define DJI_IPC_C1_PID_TEST       ((DJI_IPC_C1_PID_D0 << 8) | 0)
#define DJI_IPC_C1_PID_UPDATE     ((DJI_IPC_C1_PID_D0 << 8) | 1)
#define DJI_IPC_C1_PID_IMAGE      ((DJI_IPC_C1_PID_D0 << 8) | 2)
#define DJI_IPC_C1_PID_COMMAND    ((DJI_IPC_C1_PID_D0 << 8) | 3)
#define DJI_IPC_C1_PID_MCCTRL     ((DJI_IPC_C1_PID_D0 << 8) | 4)
#define DJI_IPC_C1_PID_DOWNLOAD   ((DJI_IPC_C1_PID_D0 << 8) | 5)
#define DJI_IPC_C1_PID_RSH        ((DJI_IPC_C1_PID_D0 << 8) | 8)

#define DJI_IPC_GET_PID_D1(pid)   ((pid) & 0xff)

#define NETLINK_RPMSG_DJI        24
#define RPMSG_NETLINK_MAXPKT   1024 /* netlink max package size */

typedef unsigned char  UINT8;
typedef unsigned short UINT16;
typedef unsigned int   UINT32;

enum
{
    IPC_MSG_TYPE_COMM          = 0,
    IPC_MSG_TYPE_SYN_COMM      = 1,
    IPC_MSG_TYPE_DATA          = 2,
    IPC_MSG_TYPE_SYN_DATA      = 3,
    IPC_MSG_TYPE_SYN_ECHO      = 4,
};

#pragma pack(4)
#define IPC_MSG_COMM_MAGIC  0x7788
#define IPC_MSG_HEADER \
    UINT16 Magic;      /*fixed magic word*/\
    UINT16 SenderId;   /*sender id*/\
    UINT16 ReceiverId; /*receiver id*/\
	UINT16 MsgId;      /*message id*/\
    UINT16 MsgLen;     /*message length(total length, include the length of MSG_HEADER)*/\
    UINT16 MsgSeq;     /*message sequence number*/\
    UINT16 MsgChkSum;  /*message's 16-bit CheckSum(for total message)*/\
    UINT16 _Rscv_; \
    UINT32 TimeStamp;  /*message's Time stamp*/

typedef struct
{
    IPC_MSG_HEADER
    UINT8 Data[0];
} IPC_MSG_COMM_STRU;

typedef struct
{
    IPC_MSG_HEADER
    UINT32 DataAddr;
    UINT32 DataLen;
    UINT32 DataCrc;
    UINT32 DataInfo;
} IPC_MSG_DATA_STRU;

typedef struct
{
    IPC_MSG_HEADER
	UINT32 Ack;
    UINT32 Return;
    UINT32 ResultLen;
    UINT8  Result[0];
} IPC_MSG_ECHO_STRU;

typedef struct
{
    IPC_MSG_HEADER
    UINT16  MType;
    UINT8   Type;
    UINT8   Rscv;
    UINT32  InfoAddr;
} IPC_MSG_MEM_INFO_STRU;
#pragma pack()

typedef struct
{
	UINT32 rx_total;
    UINT32 rx_fail;
    UINT32 tx_total;
    UINT32 tx_fail;
} RPMSG_TRANS_STAT_STRU;


#endif /*#end of _DEFINE_FILE_RPMSG_TRANSFER_H_*/

