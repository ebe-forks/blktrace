/*
 * blktrace output analysis: generate a timeline & gather statistics
 *
 * Copyright (C) 2006 Alan D. Brunelle <Alan.Brunelle@hp.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <stdio.h>
#include <unistd.h>
#include "globals.h"

#define INC_STAT(dip, fld) 						\
	do { 								\
		(dip)->stats. fld ++;					\
		(dip)->all_stats. fld ++;				\
	} while (0)

#define DEC_STAT(dip, fld) 						\
	do { 								\
		(dip)->stats. fld --;					\
		(dip)->all_stats. fld --;				\
	} while (0)

#define ADD_STAT(dip, fld, val) 					\
	 do { 								\
		 __u64 __v = (val);					\
		(dip)->stats. fld += __v;				\
		(dip)->all_stats. fld += __v;				\
	} while (0)

#define SUB_STAT(dip, fld, val) 					\
	 do { 								\
		 __u64 __v = (val);					\
		(dip)->stats. fld -= __v;				\
		(dip)->all_stats. fld -= __v;				\
	} while (0)

__u64 last_start, iostat_last_stamp;
__u64 iostat_interval = 1;
char *iostat_name = NULL;
FILE *iostat_ofp = NULL;

void dump_hdr(void)
{
	fprintf(iostat_ofp, "Device:       rrqm/s   wrqm/s     r/s     w/s    "
			    "rsec/s    wsec/s     rkB/s     wkB/s "
			    "avgrq-sz avgqu-sz   await   svctm  %%util   Stamp\n");
}

void im2d2c_func(struct io *c_iop, struct io *im_iop)
{
	ADD_STAT(c_iop->dip, wait, c_iop->t.time - im_iop->t.time);
}

void iostat_init(void)
{
	last_start = (__u64)-1;
	if (iostat_ofp)
		dump_hdr();
}

void update_tot_qusz(struct d_info *dip, double now)
{
	dip->stats.tot_qusz += ((now - dip->stats.last_qu_change) *
						dip->stats.cur_qusz);
	dip->all_stats.tot_qusz += ((now - dip->all_stats.last_qu_change) *
						dip->all_stats.cur_qusz);

	dip->stats.last_qu_change = dip->all_stats.last_qu_change = now;
}

void update_idle_time(struct d_info *dip, double now, int force)
{
	if (dip->stats.cur_dev == 0 || force) {
		dip->stats.idle_time += (now - dip->stats.last_dev_change);
		dip->all_stats.idle_time += 
				       (now - dip->all_stats.last_dev_change);
	}
	dip->stats.last_dev_change = dip->all_stats.last_dev_change = now;
}

void __dump_stats(__u64 stamp, int all, struct d_info *dip)
{
	char hdr[16];
	struct stats *sp;
	double dt, nios, avgrq_sz, p_util, nrqm, await, svctm;
	double now = TO_SEC(stamp);

	if (all) {
		dt = (double)stamp / 1.0e9;
		sp = &dip->all_stats;
	}
	else {
		dt = (double)(stamp-last_start) / 1.0e9;
		sp = &dip->stats;
	}

	nios = (double)(sp->ios[0] + sp->ios[1]);
	nrqm = (double)(sp->rqm[0] + sp->rqm[1]);
	update_idle_time(dip, now, 1);
	update_tot_qusz(dip, now);

	if (nios > 0.0) {
		avgrq_sz = (double)(sp->sec[0] + sp->sec[1]) / nios;
		svctm = TO_MSEC(sp->svctm) / nios;
	}
	else
		avgrq_sz = svctm = 0.0;

	await = ((nios + nrqm) > 0.0) ? TO_MSEC(sp->wait) / (nios+nrqm) : 0.0;
	p_util = (sp->idle_time <= dt) ? 100.0 * (1.0 - (sp->idle_time / dt)) :
	                                 0.0;

	/*
	 * For AWAIT: nios should be the same as number of inserts
	 * and we add in nrqm (number of merges), which should give
	 * us the total number of IOs sent to the block IO layer.
	 */
	fprintf(iostat_ofp, "%-11s ", make_dev_hdr(hdr, 11, dip));
	fprintf(iostat_ofp, "%8.2lf ", (double)sp->rqm[1] / dt);
	fprintf(iostat_ofp, "%8.2lf ", (double)sp->rqm[0] / dt);
	fprintf(iostat_ofp, "%7.2lf ", (double)sp->ios[1] / dt);
	fprintf(iostat_ofp, "%7.2lf ", (double)sp->ios[0] / dt);
	fprintf(iostat_ofp, "%9.2lf ", (double)sp->sec[1] / dt);
	fprintf(iostat_ofp, "%9.2lf ", (double)sp->sec[0] / dt);
	fprintf(iostat_ofp, "%9.2lf ", (double)(sp->sec[1] / 2) / dt);
	fprintf(iostat_ofp, "%9.2lf ", (double)(sp->sec[0] / 2) / dt);
	fprintf(iostat_ofp, "%8.2lf ", avgrq_sz);
	fprintf(iostat_ofp, "%8.2lf ", (double)sp->tot_qusz / dt);
	fprintf(iostat_ofp, "%7.2lf ", await);
	fprintf(iostat_ofp, "%7.2lf ", svctm);
	fprintf(iostat_ofp, "%6.2lf", p_util);
	if (all)
		fprintf(iostat_ofp, "%8s\n", "TOTAL");
	else {
		fprintf(iostat_ofp, "%8.2lf\n", TO_SEC(stamp));
		sp->rqm[0] = sp->rqm[1] = 0;
		sp->ios[0] = sp->ios[1] = 0;
		sp->sec[0] = sp->sec[1] = 0;
		sp->wait = sp->svctm = 0;

		sp->tot_qusz = sp->idle_time = 0.0;
	}
}

void iostat_dump_stats(__u64 stamp, int all)
{
	struct d_info *dip;

	if (all)
		dump_hdr();
	if (devices == NULL) {
		struct list_head *p;

		__list_for_each(p, &all_devs) {
			dip = list_entry(p, struct d_info, all_head);
			__dump_stats(stamp, all, dip);
		}
	}
	else {
		int i;
		unsigned int mjr, mnr;
		char *p = devices;

		while (p && ((i = sscanf(p, "%u,%u", &mjr, &mnr)) == 2)) {
			dip = __dip_find((__u32)((mjr << MINORBITS) | mnr));
			__dump_stats(stamp, all, dip);

			p = strchr(p, ';');
			if (p) p++;
		}
	}
	if (!all && iostat_ofp)
		fprintf(iostat_ofp, "\n");
}

void iostat_check_time(__u64 stamp)
{
	if (iostat_ofp) {
		if (last_start == (__u64)-1)
			last_start = stamp;
		else if ((stamp - last_start) >= iostat_interval) {
			iostat_dump_stats(stamp, 0);
			last_start = stamp;
		}

		iostat_last_stamp = stamp;
	}
}

void iostat_insert(struct io *iop)
{
	update_tot_qusz(iop->dip, TO_SEC(iop->t.time));
	INC_STAT(iop->dip, cur_qusz);
}

void iostat_merge(struct io *iop)
{
	INC_STAT(iop->dip, rqm[IOP_RW(iop)]);
}

void iostat_issue(struct io *iop)
{
	int rw = IOP_RW(iop);
	struct d_info *dip = iop->dip;
	double now = TO_SEC(iop->t.time);

	INC_STAT(dip, ios[rw]);
	ADD_STAT(dip, sec[rw], iop->t.bytes >> 9);

	update_idle_time(dip, now, 0);
	INC_STAT(dip, cur_dev);
}

void iostat_unissue(struct io *iop)
{
	int rw = IOP_RW(iop);
	struct d_info *dip = iop->dip;

	DEC_STAT(dip, ios[rw]);
	SUB_STAT(dip, sec[rw], iop->t.bytes >> 9);
	DEC_STAT(dip, cur_dev);
}

void iostat_complete(struct io *d_iop, struct io *c_iop)
{
	double now = TO_SEC(c_iop->t.time);
	struct d_info *dip = d_iop->dip;

	dip_foreach(d_iop, IOP_I, im2d2c_func, 0);
	dip_foreach(d_iop, IOP_M, im2d2c_func, 0);

	update_tot_qusz(dip, now);
	DEC_STAT(dip, cur_qusz);

	update_idle_time(dip, now, 0);
	DEC_STAT(dip, cur_dev);

	ADD_STAT(dip, svctm, c_iop->t.time - d_iop->t.time);
}
