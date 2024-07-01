/*
 * bpred.h - branch predictor interfaces
 *
 * This file is a part of the SimpleScalar tool suite written by
 * Todd M. Austin as a part of the Multiscalar Research Project.
 *
 * The tool suite is currently maintained by Doug Burger and Todd M. Austin.
 *
 * Copyright (C) 1994, 1995, 1996, 1997, 1998 by Todd M. Austin
 *
 * This source file is distributed "as is" in the hope that it will be
 * useful.  The tool set comes with no warranty, and no author or
 * distributor accepts any responsibility for the consequences of its
 * use.
 *
 * Everyone is granted permission to copy, modify and redistribute
 * this tool set under the following conditions:
 *
 *    This source code is distributed for non-commercial use only.
 *    Please contact the maintainer for restrictions applying to
 *    commercial use.
 *
 *    Permission is granted to anyone to make or distribute copies
 *    of this source code, either as received or modified, in any
 *    medium, provided that all copyright notices, permission and
 *    nonwarranty notices are preserved, and that the distributor
 *    grants the recipient permission for further redistribution as
 *    permitted by this document.
 *
 *    Permission is granted to distribute this file in compiled
 *    or executable form under the same conditions that apply for
 *    source code, provided that either:
 *
 *    A. it is accompanied by the corresponding machine-readable
 *       source code,
 *    B. it is accompanied by a written offer, with no time limit,
 *       to give anyone a machine-readable copy of the corresponding
 *       source code in return for reimbursement of the cost of
 *       distribution.  This written offer must permit verbatim
 *       duplication by anyone, or
 *    C. it is distributed by someone who received only the
 *       executable form, and is accompanied by a copy of the
 *       written offer of source code that they received concurrently.
 *
 * In other words, you are welcome to use, share and improve this
 * source file.  You are forbidden to forbid anyone else to use, share
 * and improve what you give them.
 *
 * INTERNET: dburger@cs.wisc.edu
 * US Mail:  1210 W. Dayton Street, Madison, WI 53706
 *
 */

/*
 *
 * Redistributions of any form whatsoever must retain and/or include the
 * following acknowledgment, notices and disclaimer:
 *
 * This product includes software developed by Carnegie Mellon University.
 *
 * Copyright (c) 2005 by Thomas F. Wenisch, Roland E. Wunderlich, Babak Falsafi
 * and James C. Hoe for the SimFlex Project, Computer Architecture Lab at
 * Carnegie Mellon, Carnegie Mellon University
 *
 * This source file includes SMARTSim extensions originally written by
 * Thomas Wenisch and Roland Wunderlich of the SimFlex Project.
 *
 * For more information, see the SimFlex project website at:
 *   http://www.ece.cmu.edu/~simflex
 *
 * You may not use the name "Carnegie Mellon University" or derivations
 * thereof to endorse or promote products derived from this software.
 *
 * If you modify the software you must place a notice on or within any
 * modified version provided or made available to any third party stating
 * that you have modified the software.  The notice shall include at least
 * your name, address, phone number, email address and the date and purpose
 * of the modification.
 *
 * THE SOFTWARE IS PROVIDED "AS-IS" WITHOUT ANY WARRANTY OF ANY KIND, EITHER
 * EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT LIMITED TO ANY WARRANTY
 * THAT THE SOFTWARE WILL CONFORM TO SPECIFICATIONS OR BE ERROR-FREE AND ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE,
 * TITLE, OR NON-INFRINGEMENT.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
 * BE LIABLE FOR ANY DAMAGES, INCLUDING BUT NOT LIMITED TO DIRECT, INDIRECT,
 * SPECIAL OR CONSEQUENTIAL DAMAGES, ARISING OUT OF, RESULTING FROM, OR IN
 * ANY WAY CONNECTED WITH THIS SOFTWARE (WHETHER OR NOT BASED UPON WARRANTY,
 * CONTRACT, TORT OR OTHERWISE).
 *
 */

#ifndef BPRED_H
#define BPRED_H

#define dassert(a) assert(a)

#include <stdio.h>

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "stats.h"

/*
 * This module implements a number of branch predictor mechanisms.  The
 * following predictors are supported:
 *
 *	BPred2Level:  two level adaptive branch predictor
 *
 *		It can simulate many prediction mechanisms that have up to
 *		two levels of tables. Parameters are:
 *		     N   # entries in first level (# of shift register(s))
 *		     W   width of shift register(s)
 *		     M   # entries in 2nd level (# of counters, or other FSM)
 *		One BTB entry per level-2 counter.
 *
 *		Configurations:   N, W, M
 *
 *		    counter based: 1, 0, M
 *
 *		    GAg          : 1, W, 2^W
 *		    GAp          : 1, W, M (M > 2^W)
 *		    PAg          : N, W, 2^W
 *		    PAp          : N, W, M (M == 2^(N+W))
 *
 *	BPred2bit:  a simple direct mapped bimodal predictor
 *
 *		This predictor has a table of two bit saturating counters.
 *		Where counter states 0 & 1 are predict not taken and
 *		counter states 2 & 3 are predict taken, the per-branch counters
 *		are incremented on taken branches and decremented on
 *		no taken branches.  One BTB entry per counter.
 *
 *	BPredTaken:  static predict branch taken
 *
 *	BPredNotTaken:  static predict branch not taken
 *
 */

#define MAX_L1_SIZE 4

/* branch predictor types */
enum bpred_class {
  BPredComb,                    /* combined predictor (McFarling) */
  BPred2Level,			/* 2-level correlating pred w/2-bit counters */
  BPred2bit,			/* 2-bit saturating cntr pred (dir mapped) */
  BPredTaken,			/* static predict taken */
  BPredNotTaken,		/* static predict not taken */
  BPred_NUM
};

/* an entry in a BTB */
struct bpred_btb_ent_t {
  md_addr_t addr;		/* address of branch being tracked */
  enum md_opcode op;		/* opcode of branch corresp. to addr */
  md_addr_t target;		/* last destination of branch when taken */
  struct bpred_btb_ent_t *prev, *next; /* lru chaining pointers */
};

/* direction predictor def */
struct bpred_dir_t {
  enum bpred_class klass;	/* type of predictor */
  union {
    struct {
      unsigned int size;	/* number of entries in direct-mapped table */
      unsigned char *table;	/* prediction state table */
    } bimod;
    struct {
      int l1size;		/* level-1 size, number of history regs */
      int l2size;		/* level-2 size, number of pred states */
      int shift_width;		/* amount of history in level-1 shift regs */
      int shift_mask;		/* amount of history in level-1 shift regs */
      int xor;			/* history xor address flag */
      int *shiftregs;		/* level-1 history table */
      unsigned char *l2table;	/* level-2 prediction state table */
    } two;
  } config;
};

/* Data structure for backing up branch predictor statistics */
struct bpred_stats_t {
  counter_t addr_hits;		/* num correct addr-predictions */
  counter_t dir_hits;		/* num correct dir-predictions (incl addr) */
  counter_t used_ras;		/* num RAS predictions used */
  counter_t used_bimod;		/* num bimodal predictions used (BPredComb) */
  counter_t used_2lev;		/* num 2-level predictions used (BPredComb) */
  counter_t jr_hits;		/* num correct addr-predictions for JR's */
  counter_t jr_seen;		/* num JR's seen */
  counter_t jr_non_ras_hits;	/* num correct addr-preds for non-RAS JR's */
  counter_t jr_non_ras_seen;	/* num non-RAS JR's seen */
  counter_t misses;		/* num incorrect predictions */

  counter_t lookups;		/* num lookups */
  counter_t wplookups;		/* num lookups */
  counter_t retstack_pops;	/* number of times a value was popped */
  counter_t retstack_pushes;	/* number of times a value was pushed */
  counter_t ras_hits;		/* num correct return-address predictions */

};

/* branch predictor def */
struct bpred_t {
  enum bpred_class klass;	/* type of predictor */
  struct {
    struct bpred_dir_t *bimod;	  /* first direction predictor */
    struct bpred_dir_t *twolev;	  /* second direction predictor */
    struct bpred_dir_t *meta;	  /* meta predictor */
  } dirpred;

  struct {
    int sets;			/* num BTB sets */
    int assoc;			/* BTB associativity */
    struct bpred_btb_ent_t *btb_data; /* BTB addr-prediction table */
  } btb;

  struct {
    int size;			/* return-address stack size */
    int tos;			/* top-of-stack */
    struct bpred_btb_ent_t *stack; /* return-address stack */
  } retstack;

  /* stats */
  counter_t addr_hits;		/* num correct addr-predictions */
  counter_t dir_hits;		/* num correct dir-predictions (incl addr) */
  counter_t used_ras;		/* num RAS predictions used */
  counter_t used_bimod;		/* num bimodal predictions used (BPredComb) */
  counter_t used_2lev;		/* num 2-level predictions used (BPredComb) */
  counter_t jr_hits;		/* num correct addr-predictions for JR's */
  counter_t jr_seen;		/* num JR's seen */
  counter_t jr_non_ras_hits;	/* num correct addr-preds for non-RAS JR's */
  counter_t jr_non_ras_seen;	/* num non-RAS JR's seen */
  counter_t misses;		/* num incorrect predictions */

  counter_t lookups;		/* num lookups */
  counter_t wplookups;		/* num lookups */
  counter_t retstack_pops;	/* number of times a value was popped */
  counter_t retstack_pushes;	/* number of times a value was pushed */
  counter_t ras_hits;		/* num correct return-address predictions */

  struct bpred_stats_t *backup;
};

/* branch predictor update information */
struct bpred_update_t {
  char *pdir1;		/* direction-1 predictor counter */
  char *pdir2;		/* direction-2 predictor counter */
  char *pmeta;		/* meta predictor counter */
  struct {		/* predicted directions */
    unsigned int ras    : 1;	/* RAS used */
    unsigned int bimod  : 1;    /* bimodal predictor */
    unsigned int twolev : 1;    /* 2-level predictor */
    unsigned int meta   : 1;    /* meta predictor (0..bimod / 1..2lev) */
  } dir;
  int recovery_shiftregs[MAX_L1_SIZE];
};

/* create a branch predictor */
struct bpred_t *			/* branch predictory instance */
bpred_create(enum bpred_class klass,	/* type of predictor to create */
	     unsigned int bimod_size,	/* bimod table size */
	     unsigned int l1size,	/* level-1 table size */
	     unsigned int l2size,	/* level-2 table size */
	     unsigned int meta_size,	/* meta predictor table size */
	     unsigned int shift_width,	/* history register width */
	     unsigned int xor,		/* history xor address flag */
	     unsigned int btb_sets,	/* number of sets in BTB */
	     unsigned int btb_assoc,	/* BTB associativity */
	     unsigned int retstack_size);/* num entries in ret-addr stack */

/* create a branch direction predictor */
struct bpred_dir_t *		/* branch direction predictor instance */
bpred_dir_create (
  enum bpred_class klass,	/* type of predictor to create */
  unsigned int l1size,		/* level-1 table size */
  unsigned int l2size,		/* level-2 table size (if relevant) */
  unsigned int shift_width,	/* history register width */
  unsigned int xor);	   	/* history xor address flag */

/* print branch predictor configuration */
void
bpred_config(struct bpred_t *pred,	/* branch predictor instance */
	     FILE *stream);		/* output stream */

/* print predictor stats */
void
bpred_stats(struct bpred_t *pred,	/* branch predictor instance */
	    FILE *stream);		/* output stream */

/* register branch predictor stats */
void
bpred_reg_stats(struct bpred_t *pred,	/* branch predictor instance */
		struct stat_sdb_t *sdb);/* stats database */

/* reset stats after priming, if appropriate */
void bpred_after_priming(struct bpred_t *bpred);

/* probe a predictor for a next fetch address, the predictor is probed
   with branch address BADDR, the branch target is BTARGET (used for
   static predictors), and OP is the instruction opcode (used to simulate
   predecode bits; a pointer to the predictor state entry (or null for jumps)
   is returned in *DIR_UPDATE_PTR (used for updating predictor state),
   and the non-speculative top-of-stack is returned in stack_recover_idx
   (used for recovering ret-addr stack after mis-predict).  */
md_addr_t				/* predicted branch target addr */
bpred_lookup(
       struct bpred_t *pred,	/* branch predictor instance */
	     md_addr_t baddr,		/* branch address */
	     md_addr_t btarget,		/* branch target if taken */
	     enum md_opcode op,		/* opcode of instruction */
	     int is_call,		/* non-zero if inst is fn call */
	     int is_return,		/* non-zero if inst is fn return */
	     struct bpred_update_t *dir_update_ptr, /* pred state pointer */
	     int *stack_recover_idx);	/* Non-speculative top-of-stack;
					 * used on mispredict recovery */


/* Speculative execution can corrupt the ret-addr stack.  So for each
 * lookup we return the top-of-stack (TOS) at that point; a mispredicted
 * branch, as part of its recovery, restores the TOS using this value --
 * hopefully this uncorrupts the stack. */
void
bpred_recover(struct bpred_t *pred,	/* branch predictor instance */
	      md_addr_t baddr,		/* branch address */
	      int stack_recover_idx,
	      struct bpred_update_t * update);	/* Non-speculative top-of-stack;
					 * used on mispredict recovery */

/* update the branch predictor, only useful for stateful predictors; updates
   entry for instruction type OP at address BADDR.  BTB only gets updated
   for branches which are taken.  Inst was determined to jump to
   address BTARGET and was taken if TAKEN is non-zero.  Predictor
   statistics are updated with result of prediction, indicated by CORRECT and
   PRED_TAKEN, predictor state to be updated is indicated by *DIR_UPDATE_PTR
   (may be NULL for jumps, which shouldn't modify state bits).  Note if
   bpred_update is done speculatively, branch-prediction may get polluted. */
void
bpred_update(struct bpred_t *pred,	/* branch predictor instance */
	     md_addr_t baddr,		/* branch address */
	     md_addr_t btarget,		/* resolved branch target */
	     int taken,			/* non-zero if branch was taken */
	     int pred_taken,		/* non-zero if branch was pred taken */
	     int correct,		/* was earlier prediction correct? */
	     enum md_opcode op,		/* opcode of instruction */
	     struct bpred_update_t *dir_update_ptr); /* pred state pointer */

/* SAMPLING SUPPORT CODE */

/* Called to copy mem statistics into a "backup" buffer after measurement */
void bpred_backup_stats(struct bpred_t *bp);

void bpred_alloc_stats(struct bpred_t *bp);

/* Called to copy mem statistics from a "backup" buffer before measurement */
void bpred_restore_stats(struct bpred_t *bp);

/* no stats are counted, for use by fast forwarding only */
void
bpred_update_fast(
	     md_addr_t baddr,		/* branch address */
	     md_addr_t btarget,		/* resolved branch target */
	     enum md_opcode op,
	     int is_call);	/* non-zero if inst is fn call */

/* branch predictor */
extern struct bpred_t *pred;


#endif /* BPRED_H */
