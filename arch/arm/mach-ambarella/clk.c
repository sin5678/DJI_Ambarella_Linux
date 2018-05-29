/*
 * arch/arm/plat-ambarella/generic/clk.c
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/clk.h>
#ifdef CONFIG_RPMSG_CLK
#include <linux/aipc/ipc_mutex.h>
#endif

#include <asm/uaccess.h>

#include <mach/hardware.h>
#include <plat/clk.h>

/* ==========================================================================*/
static LIST_HEAD(ambarella_all_clocks);

#ifdef CONFIG_RPMSG_CLK
DEFINE_MUTEX(ambarella_clock_mutex);

extern int rpmsg_clk_set(void *data, unsigned long rate);
extern int rpmsg_clk_get(void *data);
#else
DEFINE_SPINLOCK(ambarella_clock_lock);
#endif
/* ==========================================================================*/
static unsigned int ambarella_clk_ref_freq = REF_CLK_FREQ;

const struct pll_table_s ambarella_rct_pll_table[AMBARELLA_RCT_PLL_TABLE_SIZE] =
{
	{    16601562500000,	  0,	 0,	 3,	15 },
	{    16732282936573,	  0,	 0,	 3,	15 },
	{    16865080222486,	  0,	 2,	15,	11 },
	{    17000000923872,	  0,	 2,	15,	11 },
	{    17137097194791,	  0,	 1,	 8,	13 },
	{    17276423051953,	  0,	 1,	 8,	13 },
	{    17418032512068,	  0,	 1,	 8,	13 },
	{    17561983317137,	  0,	 2,	12,	13 },
	{    17708333209157,	  0,	 2,	12,	13 },
	{    17847768962383,	  0,	 0,	 3,	14 },
	{    17989417538047,	  0,	 0,	 3,	14 },
	{    18133332952857,	  0,	 0,	 4,	11 },
	{    18279569223523,	  0,	 0,	 4,	11 },
	{    18428184092045,	  0,	 0,	 5,	 9 },
	{    18579235300422,	  0,	 0,	 5,	 9 },
	{    18732782453299,	  0,	 2,	15,	10 },
	{    18973214551806,	  0,	 1,	 6,	15 },
	{    19122609868646,	  0,	 1,	 6,	15 },
	{    19274376332760,	  0,	 0,	 3,	13 },
	{    19428571686149,	  0,	 2,	10,	14 },
	{    19585253670812,	  0,	 2,	10,	14 },
	{    19744483754039,	  0,	 0,	 4,	10 },
	{    19906323403120,	  0,	 0,	 4,	10 },
	{    20070837810636,	  0,	 0,	 4,	10 },
	{    20238095894456,	  0,	 1,	 8,	11 },
	{    20432692021132,	  0,	 0,	 6,	 7 },
	{    20593579858541,	  0,	 3,	12,	15 },
	{    20757021382451,	  0,	 0,	 3,	12 },
	{    20923076197505,	  0,	 2,	10,	13 },
	{    21091811358929,	  0,	 2,	10,	13 },
	{    21263290196657,	  0,	 2,	 9,	14 },
	{    21437577903271,	  0,	 2,	 9,	14 },
	{    21614748984575,	  0,	 2,	 9,	14 },
	{    21794872358441,	  0,	 1,	 6,	13 },
	{    22135416045785,	  0,	 0,	 2,	15 },
	{    22309711202979,	  0,	 4,	15,	14 },
	{    22486772388220,	  0,	 4,	15,	14 },
	{    22666666656733,	  0,	 0,	 3,	11 },
	{    22849462926388,	  0,	 0,	 3,	11 },
	{    23035230115056,	  0,	 2,	 9,	13 },
	{    23224044591188,	  0,	 2,	 9,	13 },
	{    23415977135301,	  0,	 2,	15,	 8 },
	{    23611111566424,	  0,	 3,	12,	13 },
	{    23809524253011,	  0,	 0,	 2,	14 },
	{    24147726595402,	  0,	 3,	10,	15 },
	{    24337867274880,	  0,	 3,	10,	15 },
	{    24531025439501,	  0,	 1,	 8,	 9 },
	{    24727271869779,	  0,	 1,	 8,	 9 },
	{    24926686659455,	  0,	 0,	 3,	10 },
	{    25129342451692,	  0,	 0,	 3,	10 },
	{    25335321202874,	  0,	 4,	13,	14 },
	{    25544703006744,	  0,	 4,	13,	14 },
	{    25757575407624,	  0,	 0,	 2,	13 },
	{    25974025949836,	  0,	 1,	 6,	11 },
	{    26194144040346,	  0,	 4,	15,	12 },
	{    26562500745058,	  0,	 1,	 4,	15 },
	{    26771653443575,	  0,	 2,	 7,	14 },
	{    26984127238393,	  0,	 2,	 7,	14 },
	{    27200000360608,	  0,	 2,	 9,	11 },
	{    27419354766607,	  0,	 4,	12,	14 },
	{    27642276138067,	  0,	 0,	 2,	12 },
	{    27868852019310,	  0,	 0,	 2,	12 },
	{    28099173679948,	  0,	 3,	10,	13 },
	{    28333334252238,	  0,	 4,	15,	11 },
	{    28571428731084,	  0,	 0,	 4,	 7 },
	{    28813559561968,	  0,	 2,	 7,	13 },
	{    29059829190373,	  0,	 6,	15,	15 },
	{    29513888061047,	  0,	 4,	12,	13 },
	{    29746280983090,	  0,	 4,	11,	14 },
	{    29982363805175,	  0,	 2,	 9,	10 },
	{    30222222208977,	  0,	 0,	 2,	11 },
	{    30465949326754,	  0,	 2,	 6,	14 },
	{    30713640153408,	  0,	 1,	 4,	13 },
	{    30965391546488,	  0,	 6,	14,	15 },
	{    31221304088831,	  0,	 0,	 3,	 8 },
	{    31481482088566,	  0,	 0,	 3,	 8 },
	{    31746033579111,	  0,	 1,	 6,	 9 },
	{    32015066593885,	  0,	 4,	11,	13 },
	{    32288700342177,	  0,	 4,	10,	14 },
	{    32567050307989,	  0,	 4,	10,	14 },
	{    32850243151188,	  0,	 2,	 6,	13 },
	{    33203125000000,	  0,	 0,	 1,	15 },
	{    33464565873146,	  0,	 0,	 1,	15 },
	{    33730160444975,	  0,	 6,	15,	13 },
	{    34000001847744,	  0,	 2,	 7,	11 },
	{    34274194389582,	  0,	 3,	 8,	13 },
	{    34552846103907,	  0,	 4,	11,	12 },
	{    34836065024136,	  0,	 4,	11,	12 },
	{    35123966634274,	  0,	 4,	10,	13 },
	{    35416666418314,	  0,	 5,	12,	13 },
	{    35714287310839,	  0,	 0,	 1,	14 },
	{    36016948521137,	  0,	 6,	12,	15 },
	{    36324787884951,	  0,	 1,	 4,	11 },
	{    36637932062149,	  0,	 6,	15,	12 },
	{    36956522613764,	  0,	 0,	 2,	 9 },
	{    37280701100826,	  0,	 2,	 7,	10 },
	{    37610620260239,	  0,	 2,	 7,	10 },
	{    37946429103613,	  0,	 4,	10,	12 },
	{    38245219737291,	  0,	 3,	 6,	15 },
	{    38548752665520,	  0,	 0,	 1,	13 },
	{    38857143372297,	  0,	 6,	11,	15 },
	{    39170507341623,	  0,	 4,	15,	 8 },
	{    39488967508078,	  0,	 4,	 8,	14 },
	{    39812646806240,	  0,	 6,	15,	11 },
	{    40141675621271,	  0,	 8,	15,	14 },
	{    40476191788912,	  0,	 3,	 8,	11 },
	{    40816325694323,	  0,	 1,	 6,	 7 },
	{    41162226349115,	  0,	 7,	12,	15 },
	{    41514042764902,	  0,	 6,	12,	13 },
	{    41871920228004,	  0,	 5,	10,	13 },
	{    42236026376486,	  0,	 6,	10,	15 },
	{    42606517672539,	  0,	 4,	 8,	13 },
	{    42983565479517,	  0,	 2,	 4,	14 },
	{    43367348611355,	  0,	 8,	15,	13 },
	{    43758042156696,	  0,	 6,	15,	10 },
	{    44270832091570,	  0,	 1,	 2,	15 },
	{    44619422405958,	  0,	 4,	 7,	14 },
	{    44973544776440,	  0,	 6,	11,	13 },
	{    45333333313465,	  0,	 0,	 1,	11 },
	{    45698925852776,	  0,	10,	15,	15 },
	{    46070460230112,	  0,	 2,	 4,	13 },
	{    46448089182377,	  0,	 4,	 8,	12 },
	{    46831954270601,	  0,	 2,	 7,	 8 },
	{    47222223132849,	  0,	 7,	12,	13 },
	{    47619048506021,	  0,	 0,	 2,	 7 },
	{    48022598028183,	  0,	 4,	 7,	13 },
	{    48433046787977,	  0,	 7,	10,	15 },
	{    48850573599339,	  0,	10,	14,	15 },
	{    49275361001492,	  0,	 3,	 8,	 9 },
	{    49707602709532,	  0,	 5,	10,	11 },
	{    50147492438555,	  0,	 0,	 1,	10 },
	{    50595238804817,	  0,	 4,	 8,	11 },
	{    51051050424576,	  0,	 4,	 6,	14 },
	{    51515150815248,	  0,	 1,	 2,	13 },
	{    51987767219543,	  0,	 3,	 6,	11 },
	{    52469134330750,	  0,	10,	13,	15 },
	{    53125001490116,	  0,	 6,	10,	12 },
	{    53543306887150,	  0,	 2,	 3,	14 },
	{    53968254476786,	  0,	 6,	 9,	13 },
	{    54400000721216,	  0,	 2,	 4,	11 },
	{    54838709533215,	  0,	 4,	 6,	13 },
	{    55284552276134,	  0,	 0,	 1,	 9 },
	{    55737704038620,	  0,	 0,	 1,	 9 },
	{    56198347359896,	  0,	 8,	15,	10 },
	{    56666668504477,	  0,	 4,	 7,	11 },
	{    57142857462168,	  0,	 1,	 4,	 7 },
	{    57627119123936,	  0,	 2,	 3,	13 },
	{    58119658380747,	  0,	12,	15,	14 },
	{    58620691299438,	  0,	 8,	10,	14 },
	{    59130433946848,	  0,	 9,	12,	13 },
	{    59649121016264,	  0,	 4,	 5,	14 },
	{    60176990926266,	  0,	 2,	 4,	10 },
	{    60714285820723,	  0,	 1,	 2,	11 },
	{    61261262744665,	  0,	 2,	 6,	 7 },
	{    61818182468414,	  0,	12,	13,	15 },
	{    62385320663452,	  0,	 0,	 1,	 8 },
	{    62962964177132,	  0,	 8,	10,	13 },
	{    63551403582096,	  0,	 3,	 6,	 9 },
	{    64150944352150,	  0,	 4,	 5,	13 },
	{    64761906862258,	  0,	 6,	 8,	12 },
	{    65384618937969,	  0,	10,	11,	14 },
	{    65891474485397,	  0,	 5,	 6,	13 },
	{    66406250000000,	  0,	12,	13,	14 },
	{    66929131746292,	  0,	14,	15,	14 },
	{    67460320889950,	  0,	 6,	 7,	13 },
	{    68000003695488,	  0,	 2,	 3,	11 },
	{    68548388779163,	  0,	 7,	 8,	13 },
	{    69105692207813,	  0,	 8,	 9,	13 },
	{    69672130048274,	  0,	 4,	 5,	12 },
	{    70247933268547,	  0,	 8,	15,	 8 },
	{    70833332836628,	 16,	 0,	15,	15 },
	{    71428574621677,	  0,	 0,	 0,	14 },
	{    72033897042274,	  0,	14,	15,	13 },
	{    72649575769901,	  0,	 3,	 4,	11 },
	{    73275864124298,	  0,	10,	 9,	15 },
	{    73913045227528,	  0,	12,	15,	11 },
	{    74561402201653,	  0,	 8,	10,	11 },
	{    75221240520477,	  0,	 2,	 3,	10 },
	{    75892858207226,	 16,	 0,	15,	14 },
	{    76576575636864,	  0,	14,	13,	14 },
	{    77272728085518,	  0,	12,	11,	14 },
	{    77981650829315,	  0,	 5,	 6,	11 },
	{    78703701496124,	  0,	12,	10,	15 },
	{    79439252614975,	  0,	 4,	 6,	 9 },
	{    80188676714897,	  0,	 8,	 7,	14 },
	{    80952383577824,	 16,	 0,	13,	15 },
	{    81730768084526,	 16,	 0,	15,	13 },
	{    82524269819260,	  0,	14,	12,	14 },
	{    83333335816860,	  0,	 0,	 0,	12 },
	{    84158413112164,	  0,	11,	10,	13 },
	{    85000000894070,	 16,	 0,	13,	14 },
	{    85858583450317,	  0,	10,	15,	 8 },
	{    86734697222710,	 16,	 0,	13,	14 },
	{    87628863751888,	  0,	 6,	 7,	10 },
	{    88541664183140,	 16,	 0,	15,	12 },
	{    89238844811916,	  0,	 4,	 3,	14 },
	{    89947089552879,	  0,	 8,	 9,	10 },
	{    90666666626930,	 18,	 0,	13,	15 },
	{    91397851705551,	 18,	 0,	15,	13 },
	{    92140920460224,	  0,	 5,	 4,	13 },
	{    92896178364754,	  0,	12,	 9,	14 },
	{    93663908541203,	  0,	 2,	 3,	 8 },
	{    94444446265697,	 16,	 0,	11,	15 },
	{    95238097012043,	  0,	 1,	 2,	 7 },
	{    96045196056366,	  0,	 4,	 3,	13 },
	{    96866093575954,	 18,	 0,	13,	14 },
	{    97701147198677,	  1,	10,	14,	15 },
	{    98550722002983,	  0,	12,	10,	12 },
	{    99415205419064,	  0,	11,	10,	11 },
	{   100294984877110,	  0,	 0,	 0,	10 },
	{   101190477609634,	 16,	 0,	11,	14 },
	{   102102100849152,	  0,	 4,	 6,	 7 },
	{   103030301630497,	 16,	 0,	10,	15 },
	{   103975534439087,	  0,	 7,	 6,	11 },
	{   104938268661499,	  0,	14,	10,	13 },
	{   105919003486633,	  0,	 6,	 5,	11 },
	{   106918238103390,	  0,	 2,	 1,	14 },
	{   107936508953571,	 18,	 0,	15,	11 },
	{   108974359929562,	 16,	 0,	11,	13 },
	{   110032364726067,	  0,	10,	 9,	10 },
	{   111111111938953,	  0,	 0,	 0,	 9 },
	{   112211219966412,	  0,	10,	 6,	14 },
	{   113333337008953,	 16,	 0,	 9,	15 },
	{   114478111267090,	  0,	10,	 7,	12 },
	{   115646257996559,	  0,	13,	10,	11 },
	{   116838485002518,	  0,	 8,	 6,	11 },
	{   118055552244186,	 16,	 0,	11,	12 },
	{   119298242032528,	  2,	 6,	15,	11 },
	{   120567373931408,	  2,	 8,	15,	14 },
	{   121863797307014,	 18,	 0,	11,	13 },
	{   123188406229019,	  0,	 7,	 4,	13 },
	{   124542124569416,	  1,	13,	14,	15 },
	{   125925928354263,	 16,	 0,	 8,	15 },
	{   127340823411942,	  0,	 6,	 4,	11 },
	{   128787875175475,	 16,	 0,	10,	12 },
	{   130268201231956,	  4,	 4,	15,	12 },
	{   131782948970795,	  0,	11,	 6,	13 },
	{   132812500000000,	 16,	 1,	15,	16 },
	{   133858263492584,	  0,	14,	 7,	14 },
	{   134920641779900,	 16,	 0,	 8,	14 },
	{   136000007390976,	 22,	 0,	12,	13 },
	{   137096777558327,	 22,	 0,	11,	14 },
	{   138211384415627,	 28,	 0,	13,	15 },
	{   139344260096549,	 22,	 0,	10,	15 },
	{   140495866537094,	 17,	 1,	15,	16 },
	{   141666665673256,	 16,	 1,	15,	15 },
	{   142857149243355,	  0,	 0,	 0,	 7 },
	{   144067794084549,	 18,	 0,	10,	12 },
	{   145299151539803,	 16,	 0,	 8,	13 },
	{   146551728248596,	  0,	10,	 4,	15 },
	{   147826090455055,	 18,	 1,	15,	16 },
	{   149122804403305,	 17,	 1,	15,	15 },
	{   150442481040955,	 16,	 1,	14,	15 },
	{   151785716414452,	 16,	 1,	15,	14 },
	{   153153151273727,	  0,	14,	 6,	14 },
	{   154545456171036,	 16,	 0,	 9,	11 },
	{   155963301658630,	19,	 1,	15,	 16},
	{   157407402992249,	18,	 1,	15,	 15},
	{   158878505229950,	17,	 1,	14,	 15},
	{   160377353429794,	17,	 1,	15,	 14},
	{   161904767155647,	16,	 1,	14,	 14},
	{   163461536169052,	16,	 1,	15,	 13},
	{   165048539638519,	36,	 0,	15,	14 },
	{   166666671633720,	19,	 1,	15,	 15},
	{   168316826224327,	18,	 1,	14,	 15},
	{   170000001788139,	18,	 1,	15,	 14},
	{   171717166900635,	17,	 1,	14,	 14},
	{   173469394445419,	16,	 1,	14,	 13},
	{   175257727503777,	20,	 1,	15,	 15},
	{   177083328366280,	16,	 1,	15,	 12},
	{   178947374224663,	19,	 1,	15,	 14},
	{   180851057171822,	18,	 1,	14,	 14},
	{   182795703411102,	18,	 1,	15,	 13},
	{   184782609343529,	17,	 1,	14,	 13},
	{   186813190579414,	17,	 1,	15,	 12},
	{   188888892531395,	16,	 1,	14,	 12},
	{   191011235117912,	19,	 1,	15,	 13},
	{   193181812763214,	16,	 1,	15,	 11},
	{   195402294397354,	18,	 1,	14,	 13},
	{   197674423456192,	18,	 1,	15,	 12},
	{   200000002980232,	17,	 1,	14,	 12},
	{   202380955219269,	16,	 1,	13,	 12},
	{   204819276928902,	16,	 1,	14,	 11},
	{   207317069172859,	19,	 1,	15,	 12},
	{   209876537322998,	18,	 1,	14,	 12},
	{   212500005960464,	16,	 1,	15,	 10},
	{   215189874172211,	18,	 1,	15,	 11},
	{   217948719859123,	17,	 1,	14,	 11},
	{   220779225230217,	16,	 1,	13,	 11},
	{   223684206604958,	17,	 1,	15,	 10},
	{   226666674017906,	16,	 1,	14,	 10},
	{   229729726910591,	18,	 1,	14,	 11},
	{   232876718044281,	17,	 1,	13,	 11},
	{   236111104488373,	16,	 1,	15,	  9},
	{   239436626434326,	17,	 1,	14,	 10},
	{   242857143282890,	16,	 1,	13,	 10},
	{   246376812458038,	18,	 1,	13,	 11},
	{   250000000000000,	16,	 1,	14,	  9},
	{   253731340169907,	18,	 1,	14,	 10},
	{   255639106035233,	17,	 1,	13,	 10},
	{   257575750350951,	16,	 1,	11,	 11},
	{   261538475751877,	16,	 1,	12,	 10},
	{   263565897941589,	18,	 1,	15,	  9},
	{   265625000000000,	16,	 1,	15,	  8},
	{   267716526985168,	20,	 1,	12,	 12},
	{   269841283559798,	16,	 1,	13,	  9},
	{   272000014781952,	17,	 1,	11,	 11},
	{   274193555116653,	21,	 1,	15,	 10},
	{   276422768831253,	19,	 1,	15,	  9},
	{   278688520193099,	20,	 1,	14,	 10},
	{   280991733074188,	17,	 1,	15,	  8},
	{   283333331346512,	16,	 1,	14,	  8},
	{   285714298486710,	17,	 1,	13,	  9},
	{   288135588169098,	18,	 1,	11,	 11},
	{   290598303079605,	16,	 1,	12,	  9},
	{   293103456497192,	21,	 1,	14,	 10},
	{   295652180910110,	18,	 1,	15,	  8},
	{   298245608806610,	17,	 1,	14,	  8},
	{   300884962081909,	18,	 1,	13,	  9},
	{   303571432828903,	16,	 1,	15,	  7},
	{   306306302547455,	17,	 1,	12,	  9},
	{   309090912342072,	20,	 1,	14,	  9},
	{   311926603317261,	19,	 1,	15,	  8},
	{   314814805984497,	18,	 1,	14,	  8},
	{   317757010459900,	19,	 1,	13,	  9},
	{   320754706859589,	17,	 1,	15,	  7},
	{   323809534311295,	16,	 1,	14,	  7},
	{   326923072338104,	16,	 1,	12,	  8},
	{   330097079277039,	19,	 1,	10,	 11},
	{   333333343267441,	19,	 1,	14,	  8},
	{   336633652448654,	21,	 1,	12,	 10},
	{   340000003576279,	18,	 1,	15,	  7},
	{   343434333801270,	17,	 1,	14,	  7},
	{   346938788890839,	16,	 1,	13,	  7},
	{   350515455007553,	20,	 1,	14,	  8},
	{   354166656732559,	16,	 1,	15,	  6},
	{   357894748449326,	19,	 1,	15,	  7},
	{   361702114343643,	18,	 1,	14,	  7},
	{   365591406822205,	17,	 1,	13,	  7},
	{   369565218687057,	19,	 1,	11,	  9},
	{   373626381158829,	17,	 1,	15,	  6},
	{   377777785062790,	16,	 1,	14,	  6},
	{   382022470235825,	19,	 1,	14,	  7},
	{   386363625526428,	18,	 1,	13,	  7},
	{   390804588794708,	21,	 1,	15,	  7},
	{   395348846912384,	18,	 1,	15,	  6},
	{   400000005960464,	17,	 1,	14,	  6},
	{   404761910438538,	16,	 1,	13,	  6},
	{   409638553857803,	19,	 1,	13,	  7},
	{   414634138345718,	19,	 1,	15,	  6},
	{   419753074645996,	18,	 1,	14,	  6},
	{   425000011920929,	16,	 1,	15,	  5},
	{   430379748344421,	17,	 1,	13,	  6},
	{   435897439718246,	16,	 1,	12,	  6},
	{   441558450460434,	19,	 1,	14,	  6},
	{   447368413209915,	17,	 1,	15,	  5},
	{   453333348035812,	16,	 1,	14,	  5},
	{   459459453821182,	17,	 1,	12,	  6},
	{   465753436088562,	20,	 1,	14,	  6},
	{   472222208976746,	18,	 1,	15,	  5},
	{   478873252868652,	17,	 1,	14,	  5},
	{   485714286565781,	16,	 1,	13,	  5},
	{   492753624916077,	18,	 1,	10,	  7},
	{   500000000000000,	19,	 1,	15,	  5},
	{   507462680339813,	18,	 1,	14,	  5},
	{   515151500701903,	17,	 1,	13,	  5},
	{   523076951503754,	16,	 1,	12,	  5},
	{   531250000000000,	16,	 1,	15,	  4},
	{   539682567119597,	18,	 1,	13,	  5},
	{   548387110233307,	21,	 1,	15,	  5},
	{   552631556987762,	 78,	 0,	10,	 13},
	{   557377040386199,	17,	 1,	12,	  5},
	{   566666662693024,	16,	 1,	14,	  4},
	{   571428596973419,	19,	 1,	13,	  5},
	{   576271176338196,	18,	 1,	10,	  6},
	{   580645143985748,	18,	 1,	12,	  5},
	{   586206912994385,	21,	 1,	14,	  5},
	{   591549277305603,	23,	 1,	 8,	  9},
	{   596491217613220,	17,	 1,	14,	  4},
	{   607142865657806,	16,	 1,	13,	  4},
	{   612903237342834,	19,	 1,	12,	  5},
	{   618181824684143,	16,	 1,	10,	  5},
	{   622950792312622,	19,	 1,	15,	  4},
	{   629629611968994,	18,	 1,	14,	  4},
	{   634920656681061,	20,	 1,	10,	  6},
	{   641509413719177,	17,	 1,	13,	  4},
	{   647058844566345,	 28,	 4,	15,	 14},
	{   653846144676208,	16,	 1,	12,	  4},
	{   666666686534882,	19,	 1,	14,	  4},
	{   680000007152557,	18,	 1,	13,	  4},
	{   688524603843689,	21,	 1,	15,	  4},
	{   693877577781677,	17,	 1,	12,	  4},
	{   701754391193390,	20,	 1,	14,	  4},
	{   708333313465118,	16,	 1,	15,	  3},
	{   716981112957001,	19,	 1,	13,	  4},
	{   723404228687286,	17,	 1,	 9,	  5},
	{   730769217014313,	18,	 1,	12,	  4},
	{   739130437374115,	23,	 1,	12,	  5},
	{   745098054409027,	20,	 1,	13,	  4},
	{   755555570125580,	16,	 1,	14,	  3},
	{   765957474708557,	19,	 1,	12,	  4},
	{   772727251052856,	16,	 1,	10,	  4},
	{   782608687877655,	21,	 1,	13,	  4},
	{   790697693824768,	18,	 1,	15,	  3},
	{   800000011920929,	17,	 1,	14,	  3},
	{   809523820877075,	16,	 1,	13,	  3},
	{   818181812763214,	17,	 1,	10,	  4},
	{   829268276691437,	19,	 1,	15,	  3},
	{   837209284305573,	20,	 1,	 9,	  5},
	{   850000023841858,	18,	 1,	14,	  3},
	{   857142865657806,	17,	 1,	13,	  3},
	{   863636374473572,	18,	 1,	10,	  4},
	{   871794879436493,	16,	 1,	12,	  3},
	{   883720934391022,	19,	 1,	14,	  3},
	{   894736826419830,	17,	 1,	 9,	  4},
	{   904761910438538,	18,	 1,	13,	  3},
	{   918918907642365,	17,	 1,	12,	  3},
	{   926829278469086,	20,	 1,	14,	  3},
	{   936170220375061,	22,	 1,	 6,	  7},
	{   944444417953491,	16,	 1,	11,	  3},
	{   952380955219269,	19,	 1,	13,	  3},
	{   959999978542328,	23,	 1,	 9,	  5},
	{   971428573131561,	18,	 1,	12,	  3},
	{   978723406791687,	23,	 1,	 6,	  7},
	{  1000000000000000,	17,	 1,	11,	  3},
	{  1022222280502318,	22,	 1,	14,	  3},
	{  1030303001403809,	16,	 1,	10,	  3},
	{  1043478250503540,	 30,	 6,	15,	13 },
	{  1052631616592407,	21,	 1,	13,	  3},
	{  1062500000000000,	16,	 1,	15,	  2},
	{  1076923131942749,	20,	 1,	12,	  3},
	{  1085714340209961,	23,	 1,	10,	  4},
	{  1096774220466614,	17,	 1,	10,	  3},
	{  1105263113975524,	 78,	 1,	10,	13 },
	{  1117647051811218,	19,	 1,	11,	  3},
	{  1133333325386047,	16,	 1,	14,	  2},
	{  1142857193946838,	23,	 1,	13,	  3},
	{  1151515126228333,	18,	 1,	10,	  3},
	{  1161290287971497,	20,	 1,	11,	  3},
	{  1172413825988770,	22,	 1,	12,	  3},
	{  1187500000000000,	18,	 1,	15,	  2},
	{  1200000047683716,	17,	 1,	14,	  2},
	{  1214285731315613,	16,	 1,	13,	  2},
	{  1225806474685669,	23,	 1,	12,	  3},
	{  1241379261016846,	19,	 1,	15,	  2},
	{  1259259223937988,	18,	 1,	14,	  2},
	{  1272727251052856,	20,	 1,	10,	  3},
	{  1285714268684387,	17,	 1,	13,	  2},
	{  1297297239303588,	 22,	10,	12,	15 },
	{  1307692289352417,	16,	 1,	12,	  2},
	{  1333333373069763,	19,	 1,	14,	  2},
	{  1360000014305115,	18,	 1,	13,	  2},
	{  1371428608894348,	23,	 1,	 6,	  5},
	{  1384615421295166,	17,	 1,	12,	  2},
	{  1399999976158142,	20,	 1,	14,	  2},
	{  1416666626930237,	16,	 1,	11,	  2},
	{  1428571462631226,	19,	 1,	13,	  2},
	{  1440000057220459,	17,	 1,	 4,	  5},
	{  1461538434028625,	18,	 1,	12,	  2},
	{  1478260874748230,	19,	 1,	 8,	  3},
	{  1500000000000000,	17,	 1,	11,	  2},
	{  1519999980926514,	18,	 1,	 4,	  5},
	{  1533333301544189,	22,	 1,	14,	  2},
	{  1545454502105713,	16,	 1,	10,	  2},
	{  1565217375755310,	21,	 1,	13,	  2},
	{  1583333373069763,	18,	 1,	11,	  2},
	{  1600000023841858,	23,	 1,	14,	  2},
	{  1619047641754150,	20,	 1,	12,	  2},
	{  1636363625526428,	17,	 1,	10,	  2},
	{  1652173876762390,	 36,	 4,	 7,	14 },
	{  1666666626930237,	19,	 1,	11,	  2},
	{  1679999947547913,	21,	 1,	12,	  2},
	{  1700000047683716,	16,	 1,	 9,	  2},
	{  1714285731315613,	23,	 1,	13,	  2},
	{  1727272748947144,	18,	 1,	10,	  2},
	{  1750000000000000,	20,	 1,	11,	  2},
	{  1769230723381042,	22,	 1,	12,	  2},
	{  1789473652839661,	17,	 1,	 9,	  2},
	{  1809523820877075,	19,	 1,	10,	  2},
	{  1826086997985839,	21,	 1,	11,	  2},
	{  1840000033378601,	23,	 1,	12,	  2},
	{  1888888835906982,	16,	 1,	 8,	  2},
	{  1904761910438538,	20,	 1,	10,	  2},
	{  1919999957084656,	23,	 1,	 4,	  5},
	{  2000000000000000,	17,	 1,	 8,	  2},
	{  2086956501007080,	 36,	10,	12,	15 },
	{  2105263233184814,	20,	 1,	 9,	  2},
	{  2125000000000000,	16,	 1,	15,	  1},
	{  2190476179122924,	23,	 1,	10,	  2},
	{  2210526227951049,	21,	 1,	 9,	  2},
	{  2235294103622437,	19,	 1,	 8,	  2},
	{  2266666650772095,	16,	 1,	14,	  1},
	{  2285714387893677,	23,	 1,	 6,	  3},
	{  2315789461135864,	22,	 1,	 9,	  2},
	{  2333333253860474,	20,	 1,	 8,	  2},
	{  2352941274642944,	 18,	12,	 6,	15 },
	{  2375000000000000,	18,	 1,	15,	  1},
	{  2400000095367432,	17,	 1,	14,	  1},
	{  2428571462631226,	16,	 1,	13,	  1},
	{  2470588207244873,	 82,	 4,	11,	14 },
	{  2500000000000000,	19,	 1,	15,	  1},
	{  2533333301544189,	18,	 1,	14,	  1},
	{  2571428537368774,	17,	 1,	13,	  1},
	{  2615384578704834,	16,	 1,	12,	  1},
	{  2666666746139526,	19,	 1,	14,	  1},
	{  2714285612106323,	18,	 1,	13,	  1},
	{  2769230842590332,	17,	 1,	12,	  1},
	{  2799999952316284,	20,	 1,	14,	  1},
	{  2833333253860474,	16,	 1,	11,	  1},
	{  2857142925262451,	19,	 1,	13,	  1},
	{  2923076868057251,	18,	 1,	12,	  1},
	{  3000000000000000,	17,	 1,	11,	  1},
	{  3066666603088379,	22,	 1,	14,	  1},
	{  3090909004211426,	16,	 1,	10,	  1},
	{  3142857074737549,	21,	 1,	13,	  1},
	{  3166666746139526,	18,	 1,	11,	  1},
	{  3200000047683716,	23,	 1,	14,	  1},
	{  3230769157409668,	20,	 1,	12,	  1},
	{  3272727251052856,	17,	 1,	10,	  1},
	{  3333333253860474,	19,	 1,	11,	  1},
	{  3400000095367432,	16,	 1,	 9,	  1},
	{  3428571462631226,	23,	 1,	13,	  1},
	{  3454545497894287,	18,	 1,	10,	  1},
	{  3500000000000000,	20,	 1,	11,	  1},
	{  3538461446762085,	22,	 1,	12,	  1},
	{  3599999904632568,	17,	 1,	 9,	  1},
	{  3636363744735718,	19,	 1,	10,	  1},
	{  3666666746139526,	21,	 1,	11,	  1},
	{  3777777671813965,	16,	 1,	 8,	  1},
	{  3818181753158569,	20,	 1,	10,	  1},
	{  4000000000000000,	17,	 1,	 8,	  1},
	{  4199999809265136,	20,	 1,	 9,	  1},
	{  4250000000000000,	16,	 1,	 7,	  1},
	{  4363636493682861,	23,	 1,	10,	  1},
	{  4400000095367431,	21,	 1,	 9,	  1},
	{  4444444656372070,	19,	 1,	 8,	  1},
	{  4500000000000000,	17,	 1,	 7,	  1},
	{  4599999904632568,	22,	 1,	 9,	  1},
	{  4666666507720947,	20,	 1,	 8,	  1},
	{  4750000000000000,	18,	 1,	 7,	  1},
	{  4800000190734863,	23,	 1,	 9,	  1},
	{  4857142925262451,	16,	 1,	 6,	  1},
	{  5000000000000000,	19,	 1,	 7,	  1},
	{  5142857074737549,	17,	 1,	 6,	  1},
	{  5250000000000000,	20,	 1,	 7,	  1},
	{  5333333492279053,	23,	 1,	 8,	  1},
	{  5428571224212646,	18,	 1,	 6,	  1},
	{  5500000000000000,	21,	 1,	 7,	  1},
	{  5666666507720947,	16,	 1,	 5,	  1},
	{  5714285850524902,	19,	 1,	 6,	  1},
	{  6000000000000000,	17,	 1,	 5,	  1},
	{  6285714149475098,	21,	 1,	 6,	  1},
	{  6333333492279053,	18,	 1,	 5,	  1},
	{  6571428775787354,	22,	 1,	 6,	  1},
	{  6666666507720947,	19,	 1,	 5,	  1},
	{  6800000190734863,	16,	 1,	 4,	  1},
	{  6857142925262451,	23,	 1,	 6,	  1},
	{  7000000000000000,	20,	 1,	 5,	  1},
	{  7199999809265137,	17,	 1,	 4,	  1},
	{  7333333492279053,	21,	 1,	 5,	  1},
	{  7599999904632568,	18,	 1,	 4,	  1},
	{  7666666507720947,	22,	 1,	 5,	  1},
	{  8000000000000000,	19,	 1,	 4,	  1},
	{  8399999618530273,	20,	 1,	 4,	  1},
	{  8500000000000000,	16,	 1,	 3,	  1},
	{  8800000190734863,	21,	 1,	 4,	  1},
	{  9000000000000000,	17,	 1,	 3,	  1},
	{  9199999809265136,	22,	 1,	 4,	  1},
	{  9500000000000000,	18,	 1,	 3,	  1},
	{  9600000381469726,	23,	 1,	 4,	  1},
	{ 10000000000000000,	19,	 1,	 3,	  1},
	{ 10500000000000000,	20,	 1,	 3,	  1},
	{ 11000000000000000,	21,	 1,	 3,	  1},
	{ 11333333015441894,	16,	 1,	 2,	  1},
	{ 11500000000000000,	22,	 1,	 3,	  1},
	{ 12000000000000000,	17,	 1,	 2,	  1},
	{ 12666666984558106,	18,	 1,	 2,	  1},
	{ 13333333015441894,	19,	 1,	 2,	  1},
	{ 14000000000000000,	20,	 1,	 2,	  1},
	{ 14666666984558106,	21,	 1,	 2,	  1},
	{ 15333333015441894,	22,	 1,	 2,	  1},
	{ 16000000000000000,	23,	 1,	 2,	  1},
	{ 17000000000000000,	16,	 1,	 1,	  1},
	{ 18000000000000000,	17,	 1,	 1,	  1},
	{ 19000000000000000,	18,	 1,	 1,	  1},
	{ 20000000000000000,	19,	 1,	 1,	  1},
	{ 21000000000000000,	20,	 1,	 1,	  1},
	{ 22000000000000000,	21,	 1,	 1,	  1},
	{ 23000000000000000,	22,	 1,	 1,	  1},
	{ 24000000000000000,	23,	 1,	 1,	  1},
};
EXPORT_SYMBOL(ambarella_rct_pll_table);

u32 ambarella_rct_find_pll_table_index(unsigned long rate, u32 pre_scaler,
	const struct pll_table_s *p_table, u32 table_size)
{
	u64 divident;
	u64 divider;
	u32 start;
	u32 middle;
	u32 end;
	u32 index_limit;
	u64 diff = 0;
	u64 diff_low = 0xFFFFFFFFFFFFFFFF;
	u64 diff_high = 0xFFFFFFFFFFFFFFFF;

	pr_debug("pre_scaler = [0x%08X]\n", pre_scaler);

	divident = rate;
	divident *= pre_scaler;
	divident *= (1000 * 1000 * 1000);
	divider = (ambarella_clk_ref_freq / (1000 * 1000));
	AMBCLK_DO_DIV(divident, divider);

	index_limit = (table_size - 1);
	start = 0;
	end = index_limit;
	middle = table_size / 2;
	while (p_table[middle].multiplier != divident) {
		if (p_table[middle].multiplier < divident) {
			start = middle;
		} else {
			end = middle;
		}
		middle = (start + end) / 2;
		if (middle == start || middle == end) {
			break;
		}
	}
	if ((middle > 0) && ((middle + 1) <= index_limit)) {
		if (p_table[middle - 1].multiplier < divident) {
			diff_low = (divident -
				p_table[middle - 1].multiplier);
		} else {
			diff_low = (p_table[middle - 1].multiplier -
				divident);
		}
		if (p_table[middle].multiplier < divident) {
			diff = (divident - p_table[middle].multiplier);
		} else {
			diff = (p_table[middle].multiplier - divident);
		}
		if (p_table[middle + 1].multiplier < divident) {
			diff_high = (divident -
				p_table[middle + 1].multiplier);
		} else {
			diff_high = (p_table[middle + 1].multiplier -
				divident);
		}
		pr_debug("multiplier[%u] = [%llu]\n", (middle - 1),
			p_table[middle - 1].multiplier);
		pr_debug("multiplier[%u] = [%llu]\n", (middle),
			p_table[middle].multiplier);
		pr_debug("multiplier[%u] = [%llu]\n", (middle + 1),
			p_table[middle + 1].multiplier);
	} else if ((middle == 0) && ((middle + 1) <= index_limit)) {
		if (p_table[middle].multiplier < divident) {
			diff = (divident - p_table[middle].multiplier);
		} else {
			diff = (p_table[middle].multiplier - divident);
		}
		if (p_table[middle + 1].multiplier < divident) {
			diff_high = (divident -
				p_table[middle + 1].multiplier);
		} else {
			diff_high = (p_table[middle + 1].multiplier -
				divident);
		}
		pr_debug("multiplier[%u] = [%llu]\n", (middle),
			p_table[middle].multiplier);
		pr_debug("multiplier[%u] = [%llu]\n", (middle + 1),
			p_table[middle + 1].multiplier);
	} else if ((middle > 0) && ((middle + 1) > index_limit)) {
		if (p_table[middle - 1].multiplier < divident) {
			diff_low = (divident -
				p_table[middle - 1].multiplier);
		} else {
			diff_low = (p_table[middle - 1].multiplier -
				divident);
		}
		if (p_table[middle].multiplier < divident) {
			diff = (divident - p_table[middle].multiplier);
		} else {
			diff = (p_table[middle].multiplier - divident);
		}
		pr_debug("multiplier[%u] = [%llu]\n", (middle - 1),
			p_table[middle - 1].multiplier);
		pr_debug("multiplier[%u] = [%llu]\n", (middle),
			p_table[middle].multiplier);
	}
	pr_debug("diff_low = [%llu]\n", diff_low);
	pr_debug("diff = [%llu]\n", diff);
	pr_debug("diff_high = [%llu]\n", diff_high);
	if (diff_low < diff) {
		if (middle) {
			middle--;
		}
	}
	if (diff_high < diff) {
		middle++;
		if (middle > index_limit) {
			middle = index_limit;
		}
	}
	pr_debug("middle = [%u]\n", middle);

	return middle;
}
EXPORT_SYMBOL(ambarella_rct_find_pll_table_index);

/* ==========================================================================*/
unsigned long ambarella_rct_clk_get_rate(struct clk *c)
{
	union ctrl_reg_u ctrl_reg;
	union frac_reg_u frac_reg;
	u32 pre_scaler_reg;
	u32 post_scaler_reg;
	u32 pll_int;
	u32 sdiv;
	u32 sout;
	u64 divident;
	u64 divider;
	u64 frac;

#if defined(CONFIG_RPMSG_CLK)
	c->rate = rpmsg_clk_get(c);

	if (c->rate != 0) {
		return c->rate;
	}
#endif

	if (c->ctrl_reg != PLL_REG_UNAVAILABLE) {
		ctrl_reg.w = amba_rct_readl(c->ctrl_reg);
		if ((ctrl_reg.s.power_down == 1) ||
			(ctrl_reg.s.halt_vco == 1)) {
			c->rate = 0;
			goto ambarella_rct_clk_get_rate_exit;
		}
	} else {
		ctrl_reg.w = 0;
	}
	if (c->frac_reg != PLL_REG_UNAVAILABLE) {
		frac_reg.w = amba_rct_readl(c->frac_reg);
	} else {
		frac_reg.w = 0;
	}
	if (c->pres_reg != PLL_REG_UNAVAILABLE) {
		pre_scaler_reg = amba_rct_readl(c->pres_reg);
		if (c->extra_scaler == 1) {
			pre_scaler_reg >>= 4;
			pre_scaler_reg++;
		}
	} else {
		pre_scaler_reg = 1;
	}
	if (c->post_reg != PLL_REG_UNAVAILABLE) {
		post_scaler_reg = amba_rct_readl(c->post_reg);
		if (c->extra_scaler == 1) {
			post_scaler_reg >>= 4;
			post_scaler_reg++;
		}
	} else {
		post_scaler_reg = 1;
	}

	if (ctrl_reg.s.bypass || ctrl_reg.s.force_bypass) {
		pll_int = ambarella_clk_ref_freq;
		pll_int /= pre_scaler_reg;
		pll_int /= post_scaler_reg;
		c->rate = pll_int;
		return c->rate;
	}

	pll_int = ctrl_reg.s.intp;
	sdiv = ctrl_reg.s.sdiv;
	sout = ctrl_reg.s.sout;

	divident = (u64)ambarella_clk_ref_freq;
	divident *= (u64)(pll_int + 1);
	divident *= (u64)(sdiv + 1);
	divider = (pre_scaler_reg * (sout + 1) * post_scaler_reg);
	if (c->divider) {
		divider *= c->divider;
	}
	if (ctrl_reg.s.frac_mode) {
		if (frac_reg.s.nega) {
			/* Negative */
			frac = (0x80000000 - frac_reg.s.frac);
			frac = (ambarella_clk_ref_freq * frac * (sdiv + 1));
			frac >>= 32;
			divident = divident - frac;
		} else {
			/* Positive */
			frac = frac_reg.s.frac;
			frac = (ambarella_clk_ref_freq * frac * (sdiv + 1));
			frac >>= 32;
			divident = divident + frac;
		}
	}
	if (divider == 0) {
		c->rate = 0;
		return c->rate;
	}
	AMBCLK_DO_DIV(divident, divider);
	c->rate = divident;

ambarella_rct_clk_get_rate_exit:
	return c->rate;
}
EXPORT_SYMBOL(ambarella_rct_clk_get_rate);

int ambarella_rct_clk_set_rate(struct clk *c, unsigned long rate)
{
	int ret_val = -1;
	u32 pre_scaler;
	u32 post_scaler;
	u32 ctrl2;
	u32 ctrl3;
	u64 divident;
	u64 divider;
	u32 middle;
	union ctrl_reg_u ctrl_reg;
	union frac_reg_u frac_reg;
	u64 diff;

#if defined(CONFIG_RPMSG_CLK)
	rpmsg_clk_set(c, rate);

	c->rate = rpmsg_clk_get(c);

	if (c->rate != 0) {
		return 0;
	}
#endif

	if (c->divider) {
		rate *= c->divider;
	}

	if (!rate) {
		ret_val = -1;
		goto ambarella_rct_clk_set_rate_exit;
	}

	if ((c->ctrl_reg == PLL_REG_UNAVAILABLE) &&
		(c->frac_reg == PLL_REG_UNAVAILABLE) &&
		(c->ctrl2_reg == PLL_REG_UNAVAILABLE) &&
		(c->ctrl3_reg == PLL_REG_UNAVAILABLE) &&
		(c->pres_reg == PLL_REG_UNAVAILABLE) &&
		(c->post_reg != PLL_REG_UNAVAILABLE) && c->max_divider) {
		divider = (ambarella_clk_ref_freq + (rate >> 1) - 1) / rate;
		if (!divider) {
			ret_val = -1;
			goto ambarella_rct_clk_set_rate_exit;
		}
		if (divider > c->max_divider) {
			divider = c->max_divider;
		}
		post_scaler = divider;
		if (c->extra_scaler == 0) {
			amba_rct_writel(c->post_reg, post_scaler);
		} else if (c->extra_scaler == 1) {
			post_scaler--;
			if (post_scaler > 15){
				printk("post_scaler can't bigger than 15\r\n");
				goto ambarella_rct_clk_set_rate_exit;
			}
			post_scaler <<= 4;
			amba_rct_writel_en(c->post_reg, post_scaler);
		}
		ret_val = 0;
		goto ambarella_rct_clk_set_rate_exit;
	}

	if ((c->ctrl_reg != PLL_REG_UNAVAILABLE) &&
		(c->post_reg != PLL_REG_UNAVAILABLE)) {
		if (c->pres_reg != PLL_REG_UNAVAILABLE) {
			pre_scaler = amba_rct_readl(c->pres_reg);
			if (c->extra_scaler == 1) {
				pre_scaler >>= 4;
				pre_scaler++;
			}
		} else {
			pre_scaler = 1;
		}

		middle = ambarella_rct_find_pll_table_index(rate, pre_scaler,
			ambarella_rct_pll_table, AMBARELLA_RCT_PLL_TABLE_SIZE);

		ctrl_reg.w = amba_rct_readl(c->ctrl_reg);
		ctrl_reg.s.intp = ambarella_rct_pll_table[middle].intp;
		ctrl_reg.s.sdiv = ambarella_rct_pll_table[middle].sdiv;
		ctrl_reg.s.sout = ambarella_rct_pll_table[middle].sout;
		ctrl_reg.s.bypass = 0;
		ctrl_reg.s.frac_mode = 0;
		ctrl_reg.s.force_reset = 0;
		ctrl_reg.s.power_down = 0;
		ctrl_reg.s.halt_vco = 0;
		ctrl_reg.s.tristate = 0;
		ctrl_reg.s.force_lock = 1;
		ctrl_reg.s.force_bypass = 0;
		ctrl_reg.s.write_enable = 0;
		amba_rct_writel_en(c->ctrl_reg, ctrl_reg.w);

		post_scaler = ambarella_rct_pll_table[middle].post;
		if (c->extra_scaler == 0) {
			amba_rct_writel(c->post_reg, post_scaler);
		} else if (c->extra_scaler == 1) {
			post_scaler--;
			post_scaler <<= 4;
			amba_rct_writel_en(c->post_reg, post_scaler);
		}

		if (c->frac_mode) {
			c->rate = ambarella_rct_clk_get_rate(c);
			if (c->rate < rate) {
				diff = rate - c->rate;
			} else {
				diff = c->rate - rate;
			}
			divident = (diff * pre_scaler *
				(ambarella_rct_pll_table[middle].sout + 1) *
				ambarella_rct_pll_table[middle].post);
			divident = divident << 32;
			divider = ((u64)ambarella_clk_ref_freq *
				(ambarella_rct_pll_table[middle].sdiv + 1));
			AMBCLK_DO_DIV_ROUND(divident, divider);
			if (c->rate <= rate) {
				frac_reg.s.nega	= 0;
				frac_reg.s.frac	= divident;
			} else {
				frac_reg.s.nega	= 1;
				frac_reg.s.frac	= 0x80000000 - divident;
			}
			amba_rct_writel(c->frac_reg, frac_reg.w);

			ctrl_reg.w = amba_rct_readl(c->ctrl_reg);
			if (diff) {
				ctrl_reg.s.frac_mode = 1;
			} else {
				ctrl_reg.s.frac_mode = 0;
			}
			ctrl_reg.s.force_lock = 1;
			ctrl_reg.s.write_enable = 1;
			amba_rct_writel(c->ctrl_reg, ctrl_reg.w);

			ctrl_reg.s.write_enable	= 0;
			amba_rct_writel(c->ctrl_reg, ctrl_reg.w);
		}
		if (ctrl_reg.s.frac_mode) {
			ctrl2 = 0x3f770000;
			ctrl3 = 0x00069300;
		} else {
			ctrl2 = 0x3f770000;
			ctrl3 = 0x00068300;
		}

		if (c->ctrl2_reg != PLL_REG_UNAVAILABLE) {
			amba_rct_writel(c->ctrl2_reg, ctrl2);
		}
		if (c->ctrl3_reg != PLL_REG_UNAVAILABLE) {
			amba_rct_writel(c->ctrl3_reg, ctrl3);
		}
	}
	ret_val = 0;

ambarella_rct_clk_set_rate_exit:
	c->rate = ambarella_rct_clk_get_rate(c);
	return ret_val;
}
EXPORT_SYMBOL(ambarella_rct_clk_set_rate);

int ambarella_rct_clk_enable(struct clk *c)
{
#if defined(CONFIG_RPMSG_CLK)
#else
	union ctrl_reg_u ctrl_reg;

	if (c->ctrl_reg == PLL_REG_UNAVAILABLE) {
		return -1;
	}

	ctrl_reg.w = amba_rct_readl(c->ctrl_reg);
	ctrl_reg.s.power_down = 0;
	ctrl_reg.s.halt_vco = 0;
	ctrl_reg.s.write_enable = 1;
	amba_rct_writel(c->ctrl_reg, ctrl_reg.w);

	ctrl_reg.s.write_enable	= 0;
	amba_rct_writel(c->ctrl_reg, ctrl_reg.w);

	c->rate = ambarella_rct_clk_get_rate(c);
#endif
	return 0;
}
EXPORT_SYMBOL(ambarella_rct_clk_enable);

int ambarella_rct_clk_disable(struct clk *c)
{
#if defined(CONFIG_RPMSG_CLK)
#else
	union ctrl_reg_u ctrl_reg;

	if (c->ctrl_reg == PLL_REG_UNAVAILABLE) {
		return -1;
	}

	ctrl_reg.w = amba_rct_readl(c->ctrl_reg);
	ctrl_reg.s.power_down = 1;
	ctrl_reg.s.halt_vco = 1;
	ctrl_reg.s.write_enable = 1;
	amba_rct_writel(c->ctrl_reg, ctrl_reg.w);

	ctrl_reg.s.write_enable	= 0;
	amba_rct_writel(c->ctrl_reg, ctrl_reg.w);

	c->rate = ambarella_rct_clk_get_rate(c);
#endif

	return 0;
}
EXPORT_SYMBOL(ambarella_rct_clk_disable);

struct clk_ops ambarella_rct_pll_ops = {
	.enable		= ambarella_rct_clk_enable,
	.disable	= ambarella_rct_clk_disable,
	.get_rate	= ambarella_rct_clk_get_rate,
	.round_rate	= NULL,
	.set_rate	= ambarella_rct_clk_set_rate,
	.set_parent	= NULL,
};
EXPORT_SYMBOL(ambarella_rct_pll_ops);

/* ==========================================================================*/
unsigned long ambarella_rct_scaler_get_rate(struct clk *c)
{
	u32 divider;

#if defined(CONFIG_RPMSG_CLK)
	c->rate = rpmsg_clk_get(c);

	if (c->rate != 0) {
		return c->rate;
	}
#endif

	if (!c->parent || !c->parent->ops || !c->parent->ops->get_rate) {
		return 0;
	}

	if (c->divider) {
		c->rate = c->parent->ops->get_rate(c->parent) / c->divider;
		return c->rate;
	}

	if (c->post_reg != PLL_REG_UNAVAILABLE) {
		divider = amba_rct_readl(c->post_reg);
		if (c->extra_scaler == 1) {
			divider >>= 4;
			divider++;
		}
		if (divider) {
			c->rate = c->parent->ops->get_rate(c->parent) / divider;
			return c->rate;
		}
	}

	return 0;
}
EXPORT_SYMBOL(ambarella_rct_scaler_get_rate);

int ambarella_rct_scaler_set_rate(struct clk *c, unsigned long rate)
{
	u32 divider;
	u32 post_scaler;

#if defined(CONFIG_RPMSG_CLK)
	rpmsg_clk_set(c, rate);

	c->rate = rpmsg_clk_get(c);

	if (c->rate != 0) {
		return 0;
	}
#endif

	if (!c->parent || !c->parent->ops || !c->parent->ops->get_rate) {
		return -1;
	}
	if (c->post_reg == PLL_REG_UNAVAILABLE) {
		return -1;
	}
	if (!c->max_divider) {
		return -1;
	}
	if (c->divider) {
		rate *= c->divider;
	}
	if (!rate) {
		return -1;
	}

	divider = ((c->parent->ops->get_rate(c->parent) + rate - 1) / rate);
	if (!divider) {
		return -1;
	}
	if (divider > c->max_divider) {
		divider = c->max_divider;
	}

	post_scaler = divider;
	if (c->extra_scaler == 0) {
		amba_rct_writel(c->post_reg, post_scaler);
	} else if (c->extra_scaler == 1) {
		post_scaler--;
		post_scaler <<= 4;
		amba_rct_writel_en(c->post_reg, post_scaler);
	}

	c->rate = ambarella_rct_scaler_get_rate(c);

	return 0;
}
EXPORT_SYMBOL(ambarella_rct_scaler_set_rate);

struct clk_ops ambarella_rct_scaler_ops = {
	.enable		= NULL,
	.disable	= NULL,
	.get_rate	= ambarella_rct_scaler_get_rate,
	.round_rate	= NULL,
	.set_rate	= ambarella_rct_scaler_set_rate,
	.set_parent	= NULL,
};
EXPORT_SYMBOL(ambarella_rct_scaler_ops);

/* ==========================================================================*/
struct clk *clk_get_sys(const char *dev_id, const char *con_id)
{
	struct clk *p;
	struct clk *clk = ERR_PTR(-ENOENT);

#ifdef CONFIG_RPMSG_CLK
	mutex_lock(&ambarella_clock_mutex);
#else
	spin_lock(&ambarella_clock_lock);
#endif
	list_for_each_entry(p, &ambarella_all_clocks, list) {
		if (dev_id && (strcmp(p->name, dev_id) == 0)) {
			clk = p;
			break;
		}
		if (con_id && (strcmp(p->name, con_id) == 0)) {
			clk = p;
			break;
		}
	}
#ifdef CONFIG_RPMSG_CLK
	mutex_unlock(&ambarella_clock_mutex);
#else
	spin_unlock(&ambarella_clock_lock);
#endif

	return clk;
}
EXPORT_SYMBOL(clk_get_sys);

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p;
	struct clk *clk = ERR_PTR(-ENOENT);

	if (id == NULL) {
		return clk;
	}

#ifdef CONFIG_RPMSG_CLK
	mutex_lock(&ambarella_clock_mutex);
#else
	spin_lock(&ambarella_clock_lock);
#endif
	list_for_each_entry(p, &ambarella_all_clocks, list) {
		if (strcmp(p->name, id) == 0) {
			clk = p;
			break;
		}
	}
#ifdef CONFIG_RPMSG_CLK
	mutex_unlock(&ambarella_clock_mutex);
#else
	spin_unlock(&ambarella_clock_lock);
#endif

	return clk;
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);

int clk_enable(struct clk *clk)
{
	if (IS_ERR(clk) || (clk == NULL)) {
		return -EINVAL;
	}

	clk_enable(clk->parent);

#ifdef CONFIG_RPMSG_CLK
	mutex_lock(&ambarella_clock_mutex);
#else
	spin_lock(&ambarella_clock_lock);
#endif
	if (clk->ops && clk->ops->enable) {
		(clk->ops->enable)(clk);
	}
#ifdef CONFIG_RPMSG_CLK
	mutex_unlock(&ambarella_clock_mutex);
#else
	spin_unlock(&ambarella_clock_lock);
#endif

	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	if (IS_ERR(clk) || (clk == NULL)) {
		return;
	}

#ifdef CONFIG_RPMSG_CLK
	mutex_lock(&ambarella_clock_mutex);
#else
	spin_lock(&ambarella_clock_lock);
#endif
	if (clk->ops && clk->ops->disable) {
		(clk->ops->disable)(clk);
	}
#ifdef CONFIG_RPMSG_CLK
	mutex_unlock(&ambarella_clock_mutex);
#else
	spin_unlock(&ambarella_clock_lock);
#endif

	clk_disable(clk->parent);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	if (IS_ERR(clk) || (clk == NULL)) {
		return 0;
	}

	if (clk->ops != NULL && clk->ops->get_rate != NULL) {
		return (clk->ops->get_rate)(clk);
	}

	if (clk->parent != NULL) {
		return clk_get_rate(clk->parent);
	}

	return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (IS_ERR(clk) || (clk == NULL)) {
		return rate;
	}

	if (clk->ops && clk->ops->round_rate) {
		return (clk->ops->round_rate)(clk, rate);
	}

	return rate;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret;

	if (IS_ERR(clk) || (clk == NULL)) {
		return -EINVAL;
	}

	if ((clk->ops == NULL) || (clk->ops->set_rate == NULL)) {
		return -EINVAL;
	}

#ifdef CONFIG_RPMSG_CLK
	mutex_lock(&ambarella_clock_mutex);
#else
	spin_lock(&ambarella_clock_lock);
#endif
	ret = (clk->ops->set_rate)(clk, rate);
#ifdef CONFIG_RPMSG_CLK
	mutex_unlock(&ambarella_clock_mutex);
#else
	spin_unlock(&ambarella_clock_lock);
#endif

	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

struct clk *clk_get_parent(struct clk *clk)
{
	if (IS_ERR(clk) || (clk == NULL)) {
		return ERR_PTR(-EINVAL);
	}

	return clk->parent;
}
EXPORT_SYMBOL(clk_get_parent);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	int ret = 0;

	if (IS_ERR(clk) || (clk == NULL)) {
		return -EINVAL;
	}

#ifdef CONFIG_RPMSG_CLK
	mutex_lock(&ambarella_clock_mutex);
#else
	spin_lock(&ambarella_clock_lock);
#endif
	if (clk->ops && clk->ops->set_parent) {
		ret = (clk->ops->set_parent)(clk, parent);
	}
#ifdef CONFIG_RPMSG_CLK
	mutex_unlock(&ambarella_clock_mutex);
#else
	spin_unlock(&ambarella_clock_lock);
#endif

	return ret;
}
EXPORT_SYMBOL(clk_set_parent);

int ambarella_clk_add(struct clk *clk)
{
#ifdef CONFIG_RPMSG_CLK
	mutex_lock(&ambarella_clock_mutex);
#else
	spin_lock(&ambarella_clock_lock);
#endif
	list_add(&clk->list, &ambarella_all_clocks);
#ifdef CONFIG_RPMSG_CLK
	mutex_unlock(&ambarella_clock_mutex);
#else
	spin_unlock(&ambarella_clock_lock);
#endif

	return 0;
}
EXPORT_SYMBOL(ambarella_clk_add);

/* ==========================================================================*/
#if defined(CONFIG_AMBARELLA_PLL_PROC)
static int ambarella_clock_proc_show(struct seq_file *m, void *v)
{
	int retlen = 0;
	struct clk *p;

	retlen += seq_printf(m, "\nClock Information:\n");
#ifdef CONFIG_RPMSG_CLK
	mutex_lock(&ambarella_clock_mutex);
#else
	spin_lock(&ambarella_clock_lock);
#endif
	list_for_each_entry(p, &ambarella_all_clocks, list) {
		retlen += seq_printf(m, "\t%s:\t%lu Hz\n",
			p->name, p->ops->get_rate(p));
	}
#ifdef CONFIG_RPMSG_CLK
	mutex_unlock(&ambarella_clock_mutex);
#else
	spin_unlock(&ambarella_clock_lock);
#endif

	return retlen;
}

static ssize_t ambarella_clock_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *ppos)
{
	int ret_val = 0;
	unsigned long usr_val;
	u32 freq_hz;
	u32 pre_scaler;
	u32 ctrl2;
	u32 ctrl3;
	u64 divident;
	u64 divider;
	u32 middle;
	union ctrl_reg_u ctrl_reg;
	union frac_reg_u frac_reg;
	u64 diff;
	u32 freq_hz_int;

	ret_val = kstrtoul_from_user(buffer, count, 0, &usr_val);
	if (ret_val) {
		goto ambarella_clock_proc_write_exit;
	}
	freq_hz = usr_val;
	pre_scaler = 1;

	middle = ambarella_rct_find_pll_table_index(freq_hz, pre_scaler,
		ambarella_rct_pll_table, AMBARELLA_RCT_PLL_TABLE_SIZE);

	ctrl_reg.w = 0;
	ctrl_reg.s.intp = ambarella_rct_pll_table[middle].intp;
	ctrl_reg.s.sdiv = ambarella_rct_pll_table[middle].sdiv;
	ctrl_reg.s.sout = ambarella_rct_pll_table[middle].sout;
	ctrl_reg.s.frac_mode = 0;
	ctrl_reg.s.force_lock = 1;
	ctrl_reg.s.write_enable = 0;

	pr_info("post_scaler = [0x%08X]\n",
		ambarella_rct_pll_table[middle].post);

	divident = (ambarella_clk_ref_freq * (ctrl_reg.s.intp + 1) *
		(ctrl_reg.s.sdiv + 1));
	divider = (pre_scaler * (ctrl_reg.s.sout + 1) *
		ambarella_rct_pll_table[middle].post);
	AMBCLK_DO_DIV(divident, divider);
	freq_hz_int = divident;
	if (freq_hz_int < freq_hz) {
		diff = freq_hz - freq_hz_int;
	} else {
		diff = freq_hz_int - freq_hz;
	}
	divident = (diff * pre_scaler *
		(ambarella_rct_pll_table[middle].sout + 1) *
		ambarella_rct_pll_table[middle].post);
	divident = divident << 32;
	divider = ((u64)ambarella_clk_ref_freq *
		(ambarella_rct_pll_table[middle].sdiv + 1));
	AMBCLK_DO_DIV_ROUND(divident, divider);
	if (freq_hz_int <= freq_hz) {
		frac_reg.s.nega	= 0;
		frac_reg.s.frac	= divident;
	} else {
		frac_reg.s.nega	= 1;
		frac_reg.s.frac	= 0x80000000 - divident;
	}
	pr_info("frac_reg = [0x%08X]\n", frac_reg.w);

	if (diff) {
		ctrl_reg.s.frac_mode = 1;
	} else {
		ctrl_reg.s.frac_mode = 0;
	}
	ctrl_reg.s.force_lock = 1;
	ctrl_reg.s.write_enable	= 0;
	pr_info("ctrl_reg = [0x%08X]\n", ctrl_reg.w);

	if (ctrl_reg.s.frac_mode) {
		ctrl2 = 0x3f770000;
		ctrl3 = 0x00069300;
	} else {
		ctrl2 = 0x3f770000;
		ctrl3 = 0x00068300;
	}
	pr_info("ctrl2_reg = [0x%08X]\n", ctrl2);
	pr_info("ctrl3_reg = [0x%08X]\n", ctrl3);

	ret_val = count;

ambarella_clock_proc_write_exit:
	return ret_val;
}

static int ambarella_clock_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ambarella_clock_proc_show, PDE_DATA(inode));
}

static const struct file_operations proc_clock_fops = {
	.open = ambarella_clock_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = ambarella_clock_proc_write,
	.release = single_release,
};
#endif

/* ==========================================================================*/
int __init ambarella_clk_init(void)
{
	int ret_val = 0;

	ambarella_clk_ref_freq = REF_CLK_FREQ;
#if defined(CONFIG_AMBARELLA_PLL_PROC)
	proc_create_data("clock", S_IRUGO, get_ambarella_proc_dir(),
		&proc_clock_fops, NULL);
#endif

	return ret_val;
}

/* ==========================================================================*/
unsigned int ambarella_clk_get_ref_freq(void)
{
	return ambarella_clk_ref_freq;
}
EXPORT_SYMBOL(ambarella_clk_get_ref_freq);

