/*
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <mach/init.h>
#include <plat/remoteproc.h>
#include <plat/remoteproc_cfg.h>

extern AMBA_RPMSG_PROFILE_s *svq_profile, *rvq_profile;
extern AMBA_RPMSG_STATISTIC_s *rpmsg_stat;

/* The counter is decreasing, so start will large than end normally.*/
static unsigned int calc_timer_diff(unsigned int start, unsigned int end){
	unsigned int diff;
	if(end <= start) {
		diff = start - end;
	}
	else{
		diff = 0xFFFFFFFF - end + 1 + start;
	}
	return diff;
}

unsigned int to_get_svq_buf_profile(void)
{
	unsigned int to_get_buf = 0;
#if RPMSG_DEBUG
	unsigned int diff;
	to_get_buf = amba_readl(PROFILE_TIMER);
	/* calculate rpmsg injection rate */
	if( rpmsg_stat->LxLastInjectTime != 0 ){
		diff = calc_timer_diff(rpmsg_stat->LxLastInjectTime, to_get_buf);
	}
	else{
		diff = 0;
	}
	rpmsg_stat->LxTotalInjectTime += diff;
	rpmsg_stat->LxLastInjectTime = to_get_buf;
#endif
	return to_get_buf;
}

void get_svq_buf_done_profile(unsigned int to_get_buf, int idx)
{
#if RPMSG_DEBUG
	unsigned int get_buf;
#endif
	if(idx < 0) {
		return;
	}

#if RPMSG_DEBUG
	get_buf = amba_readl(PROFILE_TIMER);
	svq_profile[idx].ToGetSvqBuffer = to_get_buf;
	svq_profile[idx].GetSvqBuffer = get_buf;
//	printk("idx %d to_get_svq_buf %u\n", idx, svq_profile[idx].ToGetSvqBuffer);
#endif
}

void lnx_response_profile(unsigned int to_get_buf, int idx)
{
#if RPMSG_DEBUG
	unsigned int diff;
#endif
	if(idx < 0) {
		return;
	}
#if RPMSG_DEBUG

	diff = calc_timer_diff(rvq_profile[idx].SvqToSendInterrupt, to_get_buf);

	rpmsg_stat->LxResponseTime += diff;
	if(diff > rpmsg_stat->MaxLxResponseTime){
		rpmsg_stat->MaxLxResponseTime = diff;
	}
#endif
}

unsigned int finish_rpmsg_profile(unsigned int to_get_buf, unsigned int to_recv_data, int idx)
{
	unsigned int recv_data_done, ret = 0;
#if RPMSG_DEBUG
	unsigned int diff;

	recv_data_done = amba_readl(PROFILE_TIMER);
	diff = calc_timer_diff(to_recv_data, recv_data_done);
	rpmsg_stat->LxRecvCallBackTime += diff;
	if(diff > rpmsg_stat->MaxLxRecvCBTime){
		rpmsg_stat->MaxLxRecvCBTime = diff;
	}
	if(diff < rpmsg_stat->MinLxRecvCBTime){
		rpmsg_stat->MinLxRecvCBTime = diff;
	}

	diff = calc_timer_diff(rvq_profile[idx].ToGetSvqBuffer, rvq_profile[idx].SvqToSendInterrupt);
	rpmsg_stat->TxSendRpmsgTime += diff;

	diff = calc_timer_diff(to_get_buf, to_recv_data);
	rpmsg_stat->LxRecvRpmsgTime += diff;

	diff = calc_timer_diff(rvq_profile[idx].ToGetSvqBuffer, to_recv_data);
	rpmsg_stat->TxToLxRpmsgTime += diff;
	if(diff > rpmsg_stat->MaxTxToLxRpmsgTime){
		rpmsg_stat->MaxTxToLxRpmsgTime = diff;
	}
	if(diff < rpmsg_stat->MinTxToLxRpmsgTime){
		rpmsg_stat->MinTxToLxRpmsgTime = diff;
	}
	ret = amba_readl(PROFILE_TIMER);
#endif
	return ret;
}
