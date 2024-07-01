/*
 * sim-oockpt.c - out-of-order checkpoint processing simulator
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

/*
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
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <signal.h>
#include <zlib.h>
#include <time.h>
#include <unistd.h>

#include "cmu-config.h"
#include "host.h"
#include "misc.h"
#include "machine.h"
#include "regs.h"
#include "memory.h"
#include "cache.h"
#include "loader.h"
#include "syscall.h"
#include "bpred.h"
#include "resource.h"
#include "bitmap.h"
#include "options.h"
#include "eval.h"
#include "stats.h"
#include "ptrace.h"
#include "dlite.h"
#include "sim.h"
#include "power.h"
#include "storebuf.h"
#include "liblvpt/lvpt.h"

#include "sampling.h"

/* Checkpoint file management */
static char * ckpt_filename_base = 0;
static char ckpt_filename[1024];
static int ckpt_fileno = 0;
ckpt_state * ckpts;
static counter_t max_pop_insn = 0;
int max_pop_insn_changed = 0;

/* Support for target confidence */
int all_ckpts;
double target_pcterror;
double target_z;

/* paired-sample support */
static char * paired_out_filename = NULL;
static char * paired_in_filename = NULL;

#define TARGET_STAT sample_CPI_mean

double absolute_confidence() {
  double ci = 100.0;
  if (TARGET_STAT->variant.for_mean.count > 1) {
    double stdev = sqrt( TARGET_STAT->variant.for_mean.sumSq / ( TARGET_STAT->variant.for_mean.count - 1 ) );
    ci = target_z * stdev / TARGET_STAT->variant.for_mean.mean / sqrt( TARGET_STAT->variant.for_mean.count ) * 100.0;
  }
  return ci;
}

int absolute_ntuned() {
  double stdev = sqrt( TARGET_STAT->variant.for_mean.sumSq / ( TARGET_STAT->variant.for_mean.count - 1 ) );
  double sqrtn_tuned = target_z * stdev / TARGET_STAT->variant.for_mean.mean / (target_pcterror / 100.0);
  int n_tuned = sqrtn_tuned * sqrtn_tuned;
  return n_tuned;
}

double paired_confidence() {
  double ci = 100.0;
  if (paired_delta_mean->variant.for_deltamean.count > 1) {
    double stdev = sqrt( paired_delta_mean->variant.for_deltamean.sumSq / ( paired_delta_mean->variant.for_deltamean.count - 1 ) );
    ci = target_z * stdev / paired_delta_mean->variant.for_deltamean.basemean / sqrt( paired_delta_mean->variant.for_deltamean.count ) * 100.0;
  }
  return ci;
}

int paired_ntuned() {
  double stdev = sqrt( paired_delta_mean->variant.for_deltamean.sumSq / ( paired_delta_mean->variant.for_mean.count - 1 ) );
  double sqrtn_tuned = target_z * stdev / paired_delta_mean->variant.for_deltamean.basemean / (target_pcterror / 100.0);
  int n_tuned = sqrtn_tuned * sqrtn_tuned;
  return n_tuned;
}

bool_t target_confidence_reached() {
  if ( sim_sample_size > 50) {
    double current;
    if (paired_in_filename) {
      current = paired_confidence();
    } else {
      current = absolute_confidence();
    }
    if ( !all_ckpts && current < target_pcterror) {
      myfprintf(stderr, "Target Confidence Reached. Z=%2.2f pctError=+/- %2.2f%% afted %lld live-points.\n", target_z, current, sim_sample_size);
      return TRUE;
    }
    if ( (sim_sample_size & 127) == 0) {
      if (paired_in_filename) {
        fprintf(stderr, "%5lld live-points done.  Paired Delta: %2.2f%% Current conf. (Z=%1.2f): +/- % 2.2f%%. ", sim_sample_size, paired_delta_mean->variant.for_deltamean.mean / paired_delta_mean->variant.for_deltamean.basemean * 100.0, target_z, current);
        if (! all_ckpts) {
          int n_tuned = paired_ntuned();
          fprintf(stderr, "Est. live-points remaining: %5lld. ", n_tuned-sim_sample_size);
        }
      } else {
        fprintf(stderr, "%5lld live-points done.  CPI: %2.4f Current conf. (Z=%1.2f): +/- %2.2f%%. ", sim_sample_size, sample_CPI_mean->variant.for_mean.mean, target_z, current);
        if (! all_ckpts) {
          int n_tuned = absolute_ntuned();
          fprintf(stderr, "Est. live-points remaining: %5lld. ", n_tuned-sim_sample_size);
        }
      }
      fprintf(stderr, "CTRL-C to stop now.\n");
    }
  }
  return FALSE;
}


/*
 * This file implements a very detailed out-of-order issue superscalar
 * processor with a two-level memory system and speculative execution support.
 * This simulator is a performance simulator, tracking the latency of all
 * pipeline operations.
 */


/* simulated registers */
static struct regs_t regs;

/* simulated memory */
static struct mem_t *mem = NULL;

/*
 * simulator options
 */


/* maximum number of inst's to execute */
static counter_t max_insts;

static int skip_ckpts = 0;

counter_t sim_ckpt_lines = 0;


#if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    /* pipeline trace range and output filename */
    static int ptrace_nelt = 0;
    static char *ptrace_opts[2];
#endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

/* store buffer size */
int sb_size;

/* instruction fetch queue size (in insts) */
static int ruu_ifq_size;

/* extra branch mis-prediction latency */
static int ruu_branch_penalty;

/* speed of front-end of machine relative to execution core */
static int fetch_speed;

/* branch predictor type {nottaken|taken|perfect|bimod|2lev} */
static char *pred_type;

/* bimodal predictor config (<table_size>) */
static int bimod_nelt = 1;
STATIC_UNLESS_WATTCH int bimod_config[1] =
  { /* bimod tbl size */2048 };

/* 2-level predictor config (<l1size> <l2size> <hist_size> <xor>) */
static int twolev_nelt = 4;
STATIC_UNLESS_WATTCH int twolev_config[4] =
  { /* l1size */2, /* l2size */2048, /* hist */11, /* xor */1};

/* combining predictor config (<meta_table_size> */
static int comb_nelt = 1;
STATIC_UNLESS_WATTCH int comb_config[1] =
  { /* meta_table_size */2048 };

/* return address stack (RAS) size */
STATIC_UNLESS_WATTCH int ras_size = 16;

/* BTB predictor config (<num_sets> <associativity>) */
static int btb_nelt = 2;
STATIC_UNLESS_WATTCH int btb_config[2] =
  { /* nsets */512, /* assoc */4 };

/* instruction fetch B/W (insts/cycle) */
STATIC_UNLESS_WATTCH int fetch_width;

/* instruction decode B/W (insts/cycle) */
STATIC_UNLESS_WATTCH int ruu_decode_width;

/* instruction issue B/W (insts/cycle) */
STATIC_UNLESS_WATTCH int ruu_issue_width;

/* run pipeline with in-order issue */
static int ruu_inorder_issue;

/* issue instructions down wrong execution paths */
static int ruu_include_spec = TRUE;

/* instruction commit B/W (insts/cycle) */
STATIC_UNLESS_WATTCH int ruu_commit_width;

/* register update unit (RUU) size */
STATIC_UNLESS_WATTCH int RUU_size = 8;
STATIC_UNLESS_WATTCH int RUU_size_mask = 0;

/* load/store queue (LSQ) size */
STATIC_UNLESS_WATTCH int LSQ_size = 4;
STATIC_UNLESS_WATTCH int LSQ_size_mask = 0;

/* l1 data cache config, i.e., {<config>|none} */
static char *cache_dl1_opt;

/* l1 data cache hit latency (in cycles) */
static int cache_dl1_lat;

/* mshr: # of mshrs & # of targets */
static int cache_dl1_mshrs;
static int cache_dl1_mshr_targets;
static struct stat_stat_t *mshr_prof = NULL;

/* pipelined L2 cache: issue latency */
static int cache_l2_issuelat;

/* l2 data cache config, i.e., {<config>|none} */
static char *cache_dl2_opt;

/* l2 data cache hit latency (in cycles) */
static int cache_dl2_lat;

/* l1 instruction cache config, i.e., {<config>|dl1|dl2|none} */
static char *cache_il1_opt;

/* l1 instruction cache hit latency (in cycles) */
static int cache_il1_lat;

/* l2 instruction cache config, i.e., {<config>|dl1|dl2|none} */
static char *cache_il2_opt;

/* l2 instruction cache hit latency (in cycles) */
static int cache_il2_lat;

/* flush caches on system calls */
static int flush_on_syscalls;

/* convert 64-bit inst addresses to 32-bit inst equivalents */
static int compress_icache_addrs;

/* memory access latency (<first_chunk> <inter_chunk>) */
static int mem_nelt = 2;
int mem_lat[2] =
  { /* lat to first chunk */100, /* lat between remaining chunks */5 };

/* memory access bus width (in bytes) */
static int mem_bus_width;

/* instruction TLB config, i.e., {<config>|none} */
static char *itlb_opt;

/* data TLB config, i.e., {<config>|none} */
static char *dtlb_opt;

/* inst/data TLB miss latency (in cycles) */
static int tlb_miss_lat;

/* total number of integer ALU's available */
STATIC_UNLESS_WATTCH int res_ialu;

/* total number of integer multiplier/dividers available */
static int res_imult;

/* total number of memory system ports available (to CPU) */
STATIC_UNLESS_WATTCH int res_memport;

/* total number of floating point ALU's available */
STATIC_UNLESS_WATTCH int res_fpalu;

/* total number of floating point multiplier/dividers available */
STATIC_UNLESS_WATTCH int res_fpmult;

/* options for Wattch */
  int data_width = 64;

  /* static power model results */
  extern power_result_type power;

  /* counters added for Wattch */
  counter_t rename_access=0;
  counter_t bpred_access=0;
  counter_t window_access=0;
  counter_t lsq_access=0;
  counter_t regfile_access=0;
  counter_t icache_access=0;
  counter_t dcache_access=0;
  counter_t dcache2_access=0;
  counter_t alu_access=0;
  counter_t ialu_access=0;
  counter_t falu_access=0;
  counter_t resultbus_access=0;

  counter_t window_preg_access=0;
  counter_t window_selection_access=0;
  counter_t window_wakeup_access=0;
  counter_t lsq_store_data_access=0;
  counter_t lsq_load_data_access=0;
  counter_t lsq_preg_access=0;
  counter_t lsq_wakeup_access=0;

  counter_t window_total_pop_count_cycle=0;
  counter_t window_num_pop_count_cycle=0;
  counter_t lsq_total_pop_count_cycle=0;
  counter_t lsq_num_pop_count_cycle=0;
  counter_t regfile_total_pop_count_cycle=0;
  counter_t regfile_num_pop_count_cycle=0;
  counter_t resultbus_total_pop_count_cycle=0;
  counter_t resultbus_num_pop_count_cycle=0;

#if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
   /* text-based stat profiles */
#  define MAX_PCSTAT_VARS 8
   static int pcstat_nelt = 0;
   static char *pcstat_vars[MAX_PCSTAT_VARS];
#endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

/* convert 64-bit inst text addresses to 32-bit inst equivalents */
#ifdef TARGET_PISA
#define IACOMPRESS(A)             \
  (compress_icache_addrs ? ((((A) - ld_text_base) >> 1) + ld_text_base) : (A))
#define ISCOMPRESS(SZ)              \
  (compress_icache_addrs ? ((SZ) >> 1) : (SZ))
#else /* !TARGET_PISA */
#define IACOMPRESS(A)   (A)
#define ISCOMPRESS(SZ)    (SZ)
#endif /* TARGET_PISA */

#if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
  /* operate in backward-compatible bugs mode (for testing only) */
  static int bugcompat_mode;
#endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

/*
 * functional unit resource configuration
 */

/* resource pool indices, NOTE: update these if you change FU_CONFIG */
#define FU_IALU_INDEX     0
#define FU_IMULT_INDEX      1
#define FU_MEMPORT_INDEX    2
#define FU_FPALU_INDEX      3
#define FU_FPMULT_INDEX     4

/* resource pool definition, NOTE: update FU_*_INDEX defs if you change this */
struct res_desc fu_config[] = {
  {
    "integer-ALU",
    4,
    0,
    {
      { IntALU, 1, 1 }
    }
  },
  {
    "integer-MULT/DIV",
    2,
    0,
    {
      { IntMULT, 3, 1 },
      { IntDIV, 20, 19 }
    }
  },
  {
    "memory-port",
    2,
    0,
    {
      { RdPort, 1, 1 },
      { WrPort, 1, 1 }
    }
  },
  {
    "FP-adder",
    2,
    0,
    {
      { FloatADD, 2, 1 },
      { FloatCMP, 2, 1 },
      { FloatCVT, 2, 1 }
    }
  },
  {
    "FP-MULT/DIV",
    1,
    0,
    {
      { FloatMULT, 4, 1 },
      { FloatDIV, 12, 12 },
      { FloatSQRT, 24, 24 }
    }
  },
};





/*
 * simulator state variables
 */


/* instruction sequence counter, used to assign unique id's to insts */
static unsigned int inst_seq = 0;

#if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    /* pipetrace instruction sequence counter */
    static unsigned int ptrace_seq = 0;
#endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )


/* speculation mode, non-zero when mis-speculating, i.e., executing
   instructions down the wrong path, thus state recovery will eventually have
   to occur that resets processor register and memory state back to the last
   precise state */
static int spec_mode = FALSE;

/* cycles until fetch issue resumes */
static unsigned ruu_fetch_issue_delay = 0;

/* perfect prediction enabled */
static int pred_perfect = FALSE;

/* speculative bpred-update enabled */
static char *bpred_spec_opt;
static enum { spec_ID, spec_WB, spec_CT } bpred_spec_update;

/* level 1 instruction cache, entry level instruction cache */
struct cache_t *cache_il1;

/* level 1 instruction cache */
struct cache_t *cache_il2;

/* level 1 data cache, entry level data cache */
struct cache_t *cache_dl1;

/* level 2 data cache */
struct cache_t *cache_dl2;

/* instruction TLB */
struct cache_t *itlb;

/* data TLB */
struct cache_t *dtlb;

/* branch predictor */
struct bpred_t *pred;

/* functional unit resource pool */
static struct res_pool *fu_pool = NULL;

#if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
  /* text-based stat profiles */
  static struct stat_stat_t *pcstat_stats[MAX_PCSTAT_VARS];
  static counter_t pcstat_lastvals[MAX_PCSTAT_VARS];
  static struct stat_stat_t *pcstat_sdists[MAX_PCSTAT_VARS];

  /* wedge all stat values into a counter_t */
  #define STATVAL(STAT)             \
    ((STAT)->sc == sc_int             \
     ? (counter_t)*((STAT)->variant.for_int.var)      \
     : ((STAT)->sc == sc_uint           \
      ? (counter_t)*((STAT)->variant.for_uint.var)    \
      : ((STAT)->sc == sc_counter         \
   ? *((STAT)->variant.for_counter.var)       \
   : (panic("bad stat class"), 0))))
#endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )


/* memory access latency, assumed to not cross a page boundary */
static unsigned int     /* total latency of access */
mem_access_latency(int blk_sz)    /* block size accessed */
{
  int chunks = (blk_sz + (mem_bus_width - 1)) / mem_bus_width;

  assert(chunks > 0);

  return (/* first chunk latency */mem_lat[0] +
    (/* remainder chunk latency */mem_lat[1] * (chunks - 1)));
}


/*
 * cache miss handlers
 */

/* l1 data cache l1 block miss handler function */
static unsigned int     /* latency of block access */
dl1_access_fn(enum mem_cmd cmd,   /* access cmd, Read or Write */
        md_addr_t baddr,    /* block address to access */
        int bsize,    /* size of block to access */
        struct cache_blk_t *blk,  /* ptr to block in upper level */
        tick_t now,
        int stat_flags)   /* time of access */
{
  unsigned int lat;

  if (cache_dl2)
    {
      /* access next level of data cache hierarchy */
      lat = cache_access(cache_dl2, cmd, baddr, NULL, NULL, bsize,
       /* now */now, /* pudata */NULL, /* repl addr */NULL, stat_flags);

       /* Wattch -- Dcache2 access */
       CMU_WATTCH(dcache2_access++);

      if (cmd == Read)
  return lat;
      else
  {
    /* FIXME: unlimited write buffers */
    return 0;
  }
    }
  else
    {
      /* access main memory */
      if (cmd == Read)
  return mem_access_latency(bsize);
      else
  {
    /* FIXME: unlimited write buffers */
    return 0;
  }
    }
}

/* l2 data cache block miss handler function */
static unsigned int     /* latency of block access */
dl2_access_fn(enum mem_cmd cmd,   /* access cmd, Read or Write */
        md_addr_t baddr,    /* block address to access */
        int bsize,    /* size of block to access */
        struct cache_blk_t *blk,  /* ptr to block in upper level */
        tick_t now,
        int stat_flags)   /* time of access */
{
  /* this is a miss to the lowest level, so access main memory */
  if (cmd == Read)
    return mem_access_latency(bsize);
  else
    {
      /* FIXME: unlimited write buffers */
      return 0;
    }
}

/* l1 inst cache l1 block miss handler function */
static unsigned int     /* latency of block access */
il1_access_fn(enum mem_cmd cmd,   /* access cmd, Read or Write */
        md_addr_t baddr,    /* block address to access */
        int bsize,    /* size of block to access */
        struct cache_blk_t *blk,  /* ptr to block in upper level */
        tick_t now,
        int stat_flags)   /* time of access */
{
  unsigned int lat;

if (cache_il2)
    {
      /* access next level of inst cache hierarchy */
      lat = cache_access(cache_il2, cmd, baddr, NULL, NULL, bsize,
       /* now */now, /* pudata */NULL, /* repl addr */NULL, stat_flags);

       /* Wattch -- Dcache2 access */
       CMU_WATTCH(dcache2_access++);

      if (cmd == Read)
  return lat;
      else
  panic("writes to instruction memory not supported");
    }
  else
    {
      /* access main memory */
      if (cmd == Read)
  return mem_access_latency(bsize);
      else
  panic("writes to instruction memory not supported");
    }
}

/* l2 inst cache block miss handler function */
static unsigned int     /* latency of block access */
il2_access_fn(enum mem_cmd cmd,   /* access cmd, Read or Write */
        md_addr_t baddr,    /* block address to access */
        int bsize,    /* size of block to access */
        struct cache_blk_t *blk,  /* ptr to block in upper level */
        tick_t now,
        int stat_flags)   /* time of access */
{
  /* this is a miss to the lowest level, so access main memory */
  if (cmd == Read)
    return mem_access_latency(bsize);
  else
    panic("writes to instruction memory not supported");
}


/*
 * TLB miss handlers
 */

/* inst cache block miss handler function */
static unsigned int     /* latency of block access */
itlb_access_fn(enum mem_cmd cmd,  /* access cmd, Read or Write */
         md_addr_t baddr,   /* block address to access */
         int bsize,   /* size of block to access */
         struct cache_blk_t *blk, /* ptr to block in upper level */
         tick_t now,
         int stat_flags)    /* time of access */
{
  md_addr_t *phy_page_ptr = (md_addr_t *)blk->user_data;

  /* no real memory access, however, should have user data space attached */
  assert(phy_page_ptr);

  /* fake translation, for now... */
  *phy_page_ptr = 0;

  /* return tlb miss latency */
  return tlb_miss_lat;
}

/* data cache block miss handler function */
static unsigned int     /* latency of block access */
dtlb_access_fn(enum mem_cmd cmd,  /* access cmd, Read or Write */
         md_addr_t baddr, /* block address to access */
         int bsize,   /* size of block to access */
         struct cache_blk_t *blk, /* ptr to block in upper level */
         tick_t now,
         int stat_flags)    /* time of access */
{
  md_addr_t *phy_page_ptr = (md_addr_t *)blk->user_data;

  /* no real memory access, however, should have user data space attached */
  assert(phy_page_ptr);

  /* fake translation, for now... */
  *phy_page_ptr = 0;

  /* return tlb miss latency */
  return tlb_miss_lat;
}


/* register simulator-specific options */
void
sim_reg_options(struct opt_odb_t *odb)
{
  opt_reg_header(odb,
    "sim-oolvpt: This simulator implements a very detailed out-of-order issue\n"
    "superscalar processor with a two-level memory system and speculative\n"
    "execution support.  It executes benchmarks using a live-point library\n"
    "generated with sim-mklvpt\n"
  );



  /* instruction limit */


  opt_reg_longint(odb, "-max:inst", "maximum number of inst's to measure",
         &max_insts, /* default */0,
         /* print */TRUE, /* format */NULL);

  opt_reg_int(odb, "-lvpt:skip", "number of live-points to skip",
         &skip_ckpts, /* default */0,
         /* print */TRUE, /* format */NULL);

  opt_reg_flag(odb, "-all_lvpts",
         "Ignore target confidence and process all live-points",
         &all_ckpts, /* default */FALSE,
         /* print */TRUE, /* format */NULL);


  opt_reg_double(odb, "-target:pctError", "target pct. error",
         &target_pcterror, /* default */3.0,
         /* print */TRUE, /* format */NULL);

  opt_reg_double(odb, "-target:z", "target Z score (3.0==99.7%)",
         &target_z, /* default */3.0,
         /* print */TRUE, /* format */NULL);

  /* paired comparison options */
  opt_reg_string(odb, "-paired:write",
     "Write paired-measurement comparison data",
     &paired_out_filename, NULL, /* print */TRUE, NULL);

  opt_reg_string(odb, "-paired:compare",
     "Read and compare to paired-measurement data",
     &paired_in_filename, NULL, /* print */TRUE, NULL);


  /* sampling options */
  opt_reg_longint(odb, "-sampling:w-unit",
        "for systematic sampling, the oo-warming unit size",
        &sampling_wunit, /* default */ 2000, /* print */TRUE, /* format */NULL);
  opt_reg_longint(odb, "-sampling:m-unit",
        "for systematic sampling, the measurement unit size in instructions",
        &sampling_munit, /* default */ 1000, /* print */TRUE, /* format */NULL);

  opt_reg_string(odb, "-library",
     "live-point library file base name",
     &ckpt_filename_base, NULL, /* print */TRUE, NULL);


  /* trace options */

# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
      opt_reg_string_list(odb, "-ptrace",
        "generate pipetrace, i.e., <fname|stdout|stderr> <range>",
        ptrace_opts, /* arr_sz */2, &ptrace_nelt, /* default */NULL,
        /* !print */FALSE, /* format */NULL, /* !accrue */FALSE);


      opt_reg_note(odb,
        "  Pipetrace range arguments are formatted as follows:\n"
        "\n"
        "    {{@|#}<start>}:{{@|#|+}<end>}\n"
        "\n"
        "  Both ends of the range are optional, if neither are specified, the entire\n"
        "  execution is traced.  Ranges that start with a `@' designate an address\n"
        "  range to be traced, those that start with an `#' designate a cycle count\n"
        "  range.  All other range values represent an instruction count range.  The\n"
        "  second argument, if specified with a `+', indicates a value relative\n"
        "  to the first argument, e.g., 1000:+100 == 1000:1100.  Program symbols may\n"
        "  be used in all contexts.\n"
        "\n"
        "    Examples:   -ptrace FOO.trc #0:#1000\n"
        "                -ptrace BAR.trc @2000:\n"
        "                -ptrace BLAH.trc :1500\n"
        "                -ptrace UXXE.trc :\n"
        "                -ptrace FOOBAR.trc @main:+278\n"
         );
#endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

  /* ifetch options */

  opt_reg_int(odb, "-fetch:ifqsize", "instruction fetch queue size (in insts)",
        &ruu_ifq_size, /* default */16,
        /* print */TRUE, /* format */NULL);

  opt_reg_int(odb, "-fetch:mplat", "extra branch mis-prediction latency",
        &ruu_branch_penalty, /* default */7,
        /* print */TRUE, /* format */NULL);

  opt_reg_int(odb, "-fetch:speed",
        "speed of front-end of machine relative to execution core",
        &fetch_speed, /* default */1,
        /* print */TRUE, /* format */NULL);

  /* branch predictor options */

  opt_reg_note(odb,
"  Branch predictor configuration examples for 2-level predictor:\n"
"    Configurations:   N, M, W, X\n"
"      N   # entries in first level (# of shift register(s))\n"
"      W   width of shift register(s)\n"
"      M   # entries in 2nd level (# of counters, or other FSM)\n"
"      X   (yes-1/no-0) xor history and address for 2nd level index\n"
"    Sample predictors:\n"
"      GAg     : 1, W, 2^W, 0\n"
"      GAp     : 1, W, M (M > 2^W), 0\n"
"      PAg     : N, W, 2^W, 0\n"
"      PAp     : N, W, M (M == 2^(N+W)), 0\n"
"      gshare  : 1, W, 2^W, 1\n"
"  Predictor `comb' combines a bimodal and a 2-level predictor.\n"
               );

  opt_reg_string(odb, "-bpred",
     "branch predictor type {nottaken|taken|perfect|bimod|2lev|comb}",
                 &pred_type, /* default */"comb",
                 /* print */TRUE, /* format */NULL);

  opt_reg_int_list(odb, "-bpred:bimod",
       "bimodal predictor config (<table size>)",
       bimod_config, bimod_nelt, &bimod_nelt,
       /* default */bimod_config,
       /* print */TRUE, /* format */NULL, /* !accrue */FALSE);

  opt_reg_int_list(odb, "-bpred:2lev",
                   "2-level predictor config "
       "(<l1size> <l2size> <hist_size> <xor>)",
                   twolev_config, twolev_nelt, &twolev_nelt,
       /* default */twolev_config,
                   /* print */TRUE, /* format */NULL, /* !accrue */FALSE);

  opt_reg_int_list(odb, "-bpred:comb",
       "combining predictor config (<meta_table_size>)",
       comb_config, comb_nelt, &comb_nelt,
       /* default */comb_config,
       /* print */TRUE, /* format */NULL, /* !accrue */FALSE);

  opt_reg_int(odb, "-bpred:ras",
              "return address stack size (0 for no return stack)",
              &ras_size, /* default */ras_size,
              /* print */TRUE, /* format */NULL);

  opt_reg_int_list(odb, "-bpred:btb",
       "BTB config (<num_sets> <associativity>)",
       btb_config, btb_nelt, &btb_nelt,
       /* default */btb_config,
       /* print */TRUE, /* format */NULL, /* !accrue */FALSE);

  opt_reg_string(odb, "-bpred:spec_update",
     "speculative predictors update in {ID|WB} (default non-spec)",
     &bpred_spec_opt, /* default */"ID",
     /* print */TRUE, /* format */NULL);

  opt_reg_int(odb, "-fetch:width",
	      "instruction fetch B/W (insts/cycle)",
	      &fetch_width, /* default */8,
	      /* print */TRUE, /* format */NULL);

  /* decode options */

  opt_reg_int(odb, "-decode:width",
        "instruction decode B/W (insts/cycle)",
        &ruu_decode_width, /* default */8,
        /* print */TRUE, /* format */NULL);

  /* issue options */

  opt_reg_int(odb, "-issue:width",
        "instruction issue B/W (insts/cycle)",
        &ruu_issue_width, /* default */8,
        /* print */TRUE, /* format */NULL);

  opt_reg_flag(odb, "-issue:inorder", "run pipeline with in-order issue",
         &ruu_inorder_issue, /* default */FALSE,
         /* print */TRUE, /* format */NULL);

  opt_reg_flag(odb, "-issue:wrongpath",
         "issue instructions down wrong execution paths",
         &ruu_include_spec, /* default */TRUE,
         /* print */TRUE, /* format */NULL);

  /* commit options */

  opt_reg_int(odb, "-commit:width",
        "instruction commit B/W (insts/cycle)",
        &ruu_commit_width, /* default */8,
        /* print */TRUE, /* format */NULL);

  /* register scheduler options */

  opt_reg_int(odb, "-ruu:size",
        "register update unit (RUU) size",
        &RUU_size, /* default */128,
        /* print */TRUE, /* format */NULL);

  /* memory scheduler options  */

  opt_reg_int(odb, "-lsq:size",
        "load/store queue (LSQ) size",
        &LSQ_size, /* default */64,
        /* print */TRUE, /* format */NULL);

  opt_reg_int(odb, "-sb:size", "store buffer size",
        &sb_size, /* default */16,
        /* print */TRUE, /* format */NULL);


  /* cache options */

  opt_reg_string(odb, "-cache:dl1",
     "l1 data cache config, i.e., {<config>|none}",
     &cache_dl1_opt, "dl1:512:32:2:l",
     /* print */TRUE, NULL);

  opt_reg_note(odb,
"  The cache config parameter <config> has the following format:\n"
"\n"
"    <name>:<nsets>:<bsize>:<assoc>:<repl>\n"
"\n"
"    <name>   - name of the cache being defined\n"
"    <nsets>  - number of sets in the cache\n"
"    <bsize>  - block size of the cache\n"
"    <assoc>  - associativity of the cache\n"
"    <repl>   - block replacement strategy, 'l'-LRU, 'f'-FIFO, 'r'-random\n"
"\n"
"    Examples:   -cache:dl1 dl1:4096:32:1:l\n"
"                -dtlb dtlb:128:4096:32:r\n"
         );

  opt_reg_int(odb, "-cache:dl1lat",
        "l1 data cache hit latency (in cycles)",
        &cache_dl1_lat, /* default */1,
        /* print */TRUE, /* format */NULL);

  /* mshr: dl1 mshr options */
  opt_reg_int(odb, "-dl1:mshr", "# dl1 mshrs", &cache_dl1_mshrs,
              /* default */ 8, TRUE, NULL);

  opt_reg_int(odb, "-dl1:mshr_target", "# targets for each dl1 mshr",
              &cache_dl1_mshr_targets, /* default */ 8, TRUE, NULL);

  /* pipelined L2 cache: issue latency */
  opt_reg_int(odb, "-cache:l2_issuelat",
        "l2 cache issue latency (in cycles)",
        &cache_l2_issuelat, /* default */ 6,
        /* print */TRUE, /* format */NULL);

  opt_reg_string(odb, "-cache:dl2",
     "l2 data cache config, i.e., {<config>|none}",
     &cache_dl2_opt, "ul2:2048:128:4:l",
     /* print */TRUE, NULL);

  opt_reg_int(odb, "-cache:dl2lat",
        "l2 data cache hit latency (in cycles)",
        &cache_dl2_lat, /* default */12,
        /* print */TRUE, /* format */NULL);

  opt_reg_string(odb, "-cache:il1",
     "l1 inst cache config, i.e., {<config>|dl1|dl2|none}",
     &cache_il1_opt, "il1:512:64:2:l",
     /* print */TRUE, NULL);

  opt_reg_note(odb,
"  Cache levels can be unified by pointing a level of the instruction cache\n"
"  hierarchy at the data cache hiearchy using the \"dl1\" and \"dl2\" cache\n"
"  configuration arguments.  Most sensible combinations are supported, e.g.,\n"
"\n"
"    A unified l2 cache (il2 is pointed at dl2):\n"
"      -cache:il1 il1:128:64:1:l -cache:il2 dl2\n"
"      -cache:dl1 dl1:256:32:1:l -cache:dl2 ul2:1024:64:2:l\n"
"\n"
"    Or, a fully unified cache hierarchy (il1 pointed at dl1):\n"
"      -cache:il1 dl1\n"
"      -cache:dl1 ul1:256:32:1:l -cache:dl2 ul2:1024:64:2:l\n"
         );

  opt_reg_int(odb, "-cache:il1lat",
        "l1 instruction cache hit latency (in cycles)",
        &cache_il1_lat, /* default */1,
        /* print */TRUE, /* format */NULL);

  opt_reg_string(odb, "-cache:il2",
     "l2 instruction cache config, i.e., {<config>|dl2|none}",
     &cache_il2_opt, "dl2",
     /* print */TRUE, NULL);

  opt_reg_int(odb, "-cache:il2lat",
        "l2 instruction cache hit latency (in cycles)",
        &cache_il2_lat, /* default */12,
        /* print */TRUE, /* format */NULL);

  opt_reg_flag(odb, "-cache:flush", "flush caches on system calls",
         &flush_on_syscalls, /* default */FALSE, /* print */TRUE, NULL);

  opt_reg_flag(odb, "-cache:icompress",
         "convert 64-bit inst addresses to 32-bit inst equivalents",
         &compress_icache_addrs, /* default */FALSE,
         /* print */TRUE, NULL);


  /* mem options */
  opt_reg_int_list(odb, "-mem:lat",
       "memory access latency (<first_chunk> <inter_chunk>)",
       mem_lat, mem_nelt, &mem_nelt, mem_lat,
       /* print */TRUE, /* format */NULL, /* !accrue */FALSE);

  opt_reg_int(odb, "-mem:width", "memory access bus width (in bytes)",
        &mem_bus_width, /* default */8,
        /* print */TRUE, /* format */NULL);

  /* TLB options */

  opt_reg_string(odb, "-tlb:itlb",
     "instruction TLB config, i.e., {<config>|none}",
     &itlb_opt, "itlb:32:4096:4:l", /* print */TRUE, NULL);

  opt_reg_string(odb, "-tlb:dtlb",
     "data TLB config, i.e., {<config>|none}",
     &dtlb_opt, "dtlb:64:4096:4:l", /* print */TRUE, NULL);

  opt_reg_int(odb, "-tlb:lat",
        "inst/data TLB miss latency (in cycles)",
        &tlb_miss_lat, /* default */200,
        /* print */TRUE, /* format */NULL);

  /* resource configuration */

  opt_reg_int(odb, "-res:ialu",
        "total number of integer ALU's available",
        &res_ialu, /* default */fu_config[FU_IALU_INDEX].quantity,
        /* print */TRUE, /* format */NULL);

  opt_reg_int(odb, "-res:imult",
        "total number of integer multiplier/dividers available",
        &res_imult, /* default */fu_config[FU_IMULT_INDEX].quantity,
        /* print */TRUE, /* format */NULL);

  opt_reg_int(odb, "-res:memport",
        "total number of memory system ports available (to CPU)",
        &res_memport, /* default */fu_config[FU_MEMPORT_INDEX].quantity,
        /* print */TRUE, /* format */NULL);

  opt_reg_int(odb, "-res:fpalu",
        "total number of floating point ALU's available",
        &res_fpalu, /* default */fu_config[FU_FPALU_INDEX].quantity,
        /* print */TRUE, /* format */NULL);

  opt_reg_int(odb, "-res:fpmult",
        "total number of floating point multiplier/dividers available",
        &res_fpmult, /* default */fu_config[FU_FPMULT_INDEX].quantity,
        /* print */TRUE, /* format */NULL);

# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    opt_reg_string_list(odb, "-pcstat",
          "profile stat(s) against text addr's (mult uses ok)",
          pcstat_vars, MAX_PCSTAT_VARS, &pcstat_nelt, NULL,
          /* !print */FALSE, /* format */NULL, /* accrue */TRUE);

    opt_reg_flag(odb, "-bugcompat",
         "operate in backward-compatible bugs mode (for testing only)",
         &bugcompat_mode, /* default */FALSE, /* print */TRUE, NULL);
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

  opt_reg_note(odb,
        "Impetus-version-tag:   sim-oolvpt - " CMU_IMPETUS_VERSION_TAG "\n"
        CMU_WATTCH_FEATURE_STRING "\n"
        CMU_WATTCH_DYNAMIC_POWER_FEATURE_STRING "\n"
         );

}


bool_t open_next_file() {
  FILE * exist;

  ckpt_close(ckpts);

  ++ckpt_fileno;

  /* Create next checkpoint file name */
  sprintf(ckpt_filename, "%s.%02d.lvpt",ckpt_filename_base, ckpt_fileno);

  exist = fopen(ckpt_filename, "r");
  if ( exist ) {
    fclose(exist);

    ckpts = ckpt_open_reader(ckpt_filename);

    fprintf(stderr, "Reading live-point data from %s\n", ckpt_filename);
    ckpt_read_header(ckpts, FALSE, sampling_munit+sampling_wunit);

    ckpt_connect_structures
      ( ckpts
      , &regs
      , mem
      , cache_il1
      , cache_dl1
      , cache_dl2
      , itlb
      , dtlb
      , pred
      );


    return TRUE;
  } else {
    return FALSE;
  }

}

void prepare_checkpointing() {
  /* See if the ckpt_filename_base is a complete filename) */

  /* Strip .00.lvpt from the end of ckpt_filename_base if present */
  FILE * exist = NULL;
  int len = strlen(ckpt_filename_base);
  if (len > 8) {
    int match = strcmp(ckpt_filename_base + len - 8, ".00.lvpt");
    if (match == 0) {
      *(ckpt_filename_base + len - 8) = 0;
    }
  }

  exist = fopen(ckpt_filename_base, "r");
  if ( exist ) {
    fclose(exist);

    ckpts = ckpt_open_reader(ckpt_filename_base);
  } else {
    /* Create next checkpoint file name */
    sprintf(ckpt_filename, "%s.%02d.lvpt",ckpt_filename_base, ckpt_fileno);

    exist = fopen(ckpt_filename, "r");
    if ( ! exist ) {
      fatal("Specified checkpoint file or fileset does not exist");
    }

    ckpts = ckpt_open_reader(ckpt_filename);
  }

  fprintf(stderr, "Reading checkpoint data from %s\n", ckpt_filename);
  ckpt_read_header(ckpts, TRUE, sampling_munit + sampling_wunit);


  simoo_alloc_stats(1);
  simoo_backup_stats(0);

}


/* check simulator-specific option values */
void
sim_check_options(struct opt_odb_t *odb,        /* options database */
      int argc, char **argv)        /* command line arguments */
{
  char name[128], c;
  int nsets, bsize, assoc;

  if (! ckpt_filename_base) {
    fatal("must specify -library <live-point library> ");
  }

  if (ruu_ifq_size < 1 || (ruu_ifq_size & (ruu_ifq_size - 1)) != 0)
    fatal("inst fetch queue size must be positive > 0 and a power of two");

  if (ruu_branch_penalty < 1)
    fatal("mis-prediction penalty must be at least 1 cycle");

  if (fetch_speed < 1)
    fatal("front-end speed must be positive and non-zero");

  if (!mystricmp(pred_type, "perfect"))
    {
      /* perfect predictor */
      pred = NULL;
      pred_perfect = TRUE;
    }
  else if (!mystricmp(pred_type, "taken"))
    {
      /* static predictor, not taken */
      pred = bpred_create(BPredTaken, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
  else if (!mystricmp(pred_type, "nottaken"))
    {
      /* static predictor, taken */
      pred = bpred_create(BPredNotTaken, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
  else if (!mystricmp(pred_type, "bimod"))
    {
      /* bimodal predictor, bpred_create() checks BTB_SIZE */
      if (bimod_nelt != 1)
  fatal("bad bimod predictor config (<table_size>)");
      if (btb_nelt != 2)
  fatal("bad btb config (<num_sets> <associativity>)");

      /* bimodal predictor, bpred_create() checks BTB_SIZE */
      pred = bpred_create(BPred2bit,
        /* bimod table size */bimod_config[0],
        /* 2lev l1 size */0,
        /* 2lev l2 size */0,
        /* meta table size */0,
        /* history reg size */0,
        /* history xor address */0,
        /* btb sets */btb_config[0],
        /* btb assoc */btb_config[1],
        /* ret-addr stack size */ras_size);
    }
  else if (!mystricmp(pred_type, "2lev"))
    {
      /* 2-level adaptive predictor, bpred_create() checks args */
      if (twolev_nelt != 4)
  fatal("bad 2-level pred config (<l1size> <l2size> <hist_size> <xor>)");
      if (btb_nelt != 2)
  fatal("bad btb config (<num_sets> <associativity>)");

      pred = bpred_create(BPred2Level,
        /* bimod table size */0,
        /* 2lev l1 size */twolev_config[0],
        /* 2lev l2 size */twolev_config[1],
        /* meta table size */0,
        /* history reg size */twolev_config[2],
        /* history xor address */twolev_config[3],
        /* btb sets */btb_config[0],
        /* btb assoc */btb_config[1],
        /* ret-addr stack size */ras_size);
    }
  else if (!mystricmp(pred_type, "comb"))
    {
      /* combining predictor, bpred_create() checks args */
      if (twolev_nelt != 4)
  fatal("bad 2-level pred config (<l1size> <l2size> <hist_size> <xor>)");
      if (bimod_nelt != 1)
  fatal("bad bimod predictor config (<table_size>)");
      if (comb_nelt != 1)
  fatal("bad combining predictor config (<meta_table_size>)");
      if (btb_nelt != 2)
  fatal("bad btb config (<num_sets> <associativity>)");

      pred = bpred_create(BPredComb,
        /* bimod table size */bimod_config[0],
        /* l1 size */twolev_config[0],
        /* l2 size */twolev_config[1],
        /* meta table size */comb_config[0],
        /* history reg size */twolev_config[2],
        /* history xor address */twolev_config[3],
        /* btb sets */btb_config[0],
        /* btb assoc */btb_config[1],
        /* ret-addr stack size */ras_size);
    }
  else
    fatal("cannot parse predictor type `%s'", pred_type);

  if (!bpred_spec_opt)
    bpred_spec_update = spec_CT;
  else if (!mystricmp(bpred_spec_opt, "ID"))
    bpred_spec_update = spec_ID;
  else if (!mystricmp(bpred_spec_opt, "WB"))
    bpred_spec_update = spec_WB;
  else
    fatal("bad speculative update stage specifier, use {ID|WB}");

  if (ruu_decode_width < 1 || (ruu_decode_width & (ruu_decode_width-1)) != 0)
    fatal("issue width must be positive non-zero and a power of two");

  if (ruu_issue_width < 1 || (ruu_issue_width & (ruu_issue_width-1)) != 0)
    fatal("issue width must be positive non-zero and a power of two");

  if (ruu_commit_width < 1)
    fatal("commit width must be positive non-zero");

  if (RUU_size < 2 || (RUU_size & (RUU_size-1)) != 0)
    fatal("RUU size must be a positive number > 1 and a power of two");

  if (LSQ_size < 2 || (LSQ_size & (LSQ_size-1)) != 0)
    fatal("LSQ size must be a positive number > 1 and a power of two");

  if (sb_size < 1 )
    fatal("SB size must be a positive number > 0");


  if (cache_dl1_lat < 1)
    fatal("l1 data cache latency must be greater than zero");

  if (cache_dl2_lat < 1)
    fatal("l2 data cache latency must be greater than zero");

  if (cache_il1_lat < 1)
    fatal("l1 instruction cache latency must be greater than zero");

  if (cache_il2_lat < 1)
    fatal("l2 instruction cache latency must be greater than zero");

  if (mem_nelt != 2)
    fatal("bad memory access latency (<first_chunk> <inter_chunk>)");

  if (mem_lat[0] < 1 || mem_lat[1] < 1)
    fatal("all memory access latencies must be greater than zero");

  if (mem_bus_width < 1 || (mem_bus_width & (mem_bus_width-1)) != 0)
    fatal("memory bus width must be positive non-zero and a power of two");

  if (tlb_miss_lat < 1)
    fatal("TLB miss latency must be greater than zero");

  if (res_ialu < 1)
    fatal("number of integer ALU's must be greater than zero");
  if (res_ialu > MAX_INSTS_PER_CLASS)
    fatal("number of integer ALU's must be <= MAX_INSTS_PER_CLASS");
  fu_config[FU_IALU_INDEX].quantity = res_ialu;

  if (res_imult < 1)
    fatal("number of integer multiplier/dividers must be greater than zero");
  if (res_imult > MAX_INSTS_PER_CLASS)
    fatal("number of integer mult/div's must be <= MAX_INSTS_PER_CLASS");
  fu_config[FU_IMULT_INDEX].quantity = res_imult;

  if (res_memport < 1)
    fatal("number of memory system ports must be greater than zero");
  if (res_memport > MAX_INSTS_PER_CLASS)
    fatal("number of memory system ports must be <= MAX_INSTS_PER_CLASS");
  fu_config[FU_MEMPORT_INDEX].quantity = res_memport;

  if (res_fpalu < 1)
    fatal("number of floating point ALU's must be greater than zero");
  if (res_fpalu > MAX_INSTS_PER_CLASS)
    fatal("number of floating point ALU's must be <= MAX_INSTS_PER_CLASS");
  fu_config[FU_FPALU_INDEX].quantity = res_fpalu;

  if (res_fpmult < 1)
    fatal("number of floating point multiplier/dividers must be > zero");
  if (res_fpmult > MAX_INSTS_PER_CLASS)
    fatal("number of FP mult/div's must be <= MAX_INSTS_PER_CLASS");
  fu_config[FU_FPMULT_INDEX].quantity = res_fpmult;

  /* Stuff that will go away or change once checkpointing is enabled */

  /* use a level 1 D-cache? */
  if (!mystricmp(cache_dl1_opt, "none")) {
      cache_dl1 = NULL;

      /* the level 2 D-cache cannot be defined */
      if (strcmp(cache_dl2_opt, "none")) {
        fatal("the l1 data cache must defined if the l2 cache is defined");
      }
      cache_dl2 = NULL;
  } else /* dl1 is defined */ {
    if (sscanf(cache_dl1_opt, "%[^:]:%d:%d:%d:%c", name, &nsets, &bsize, &assoc, &c) != 5) {
      fatal("bad l1 D-cache parms: <name>:<nsets>:<bsize>:<assoc>:<repl>");
    }

    /* mshr: create dl1 with mshr */
    cache_dl1 = cache_create_mshr(name, nsets, bsize, /* balloc */FALSE,
           /* usize */0, assoc, cache_dl1_mshrs,
                             cache_dl1_mshr_targets, cache_char2policy(c),
           dl1_access_fn, /* hit lat */cache_dl1_lat);

    /* pipelined L2 cache: define l2 issue latency */
    cache_dl1->nlev_issuelat = cache_l2_issuelat;

    /* is the level 2 D-cache defined? */
    if (!mystricmp(cache_dl2_opt, "none")) {
      cache_dl2 = NULL;
    } else {
      if (sscanf(cache_dl2_opt, "%[^:]:%d:%d:%d:%c", name, &nsets, &bsize, &assoc, &c) != 5) {
        fatal("bad l2 D-cache parms: <name>:<nsets>:<bsize>:<assoc>:<repl>");
      }
      cache_dl2 = cache_create(name, nsets, bsize, /* balloc */FALSE, /* usize */0, assoc, cache_char2policy(c), dl2_access_fn, /* hit lat */cache_dl2_lat);
    }
  }

  /* use a level 1 I-cache? */
  if (!mystricmp(cache_il1_opt, "none")) {
    cache_il1 = NULL;

    /* the level 2 I-cache cannot be defined */
    if (strcmp(cache_il2_opt, "none")) {
      fatal("the l1 inst cache must defined if the l2 cache is defined");
    }
    cache_il2 = NULL;
  } else if (!mystricmp(cache_il1_opt, "dl1")) {
    if (!cache_dl1) {
      fatal("I-cache l1 cannot access D-cache l1 as it's undefined");
    }
    cache_il1 = cache_dl1;

    /* the level 2 I-cache cannot be defined */
    if (strcmp(cache_il2_opt, "none")) {
      fatal("the l1 inst cache must defined if the l2 cache is defined");
    }
    cache_il2 = NULL;
  } else if (!mystricmp(cache_il1_opt, "dl2")) {
    if (!cache_dl2) {
      fatal("I-cache l1 cannot access D-cache l2 as it's undefined");
    }
    cache_il1 = cache_dl2;

    /* the level 2 I-cache cannot be defined */
    if (strcmp(cache_il2_opt, "none")) {
      fatal("the l1 inst cache must defined if the l2 cache is defined");
    }
    cache_il2 = NULL;
  } else {
    if (sscanf(cache_il1_opt, "%[^:]:%d:%d:%d:%c", name, &nsets, &bsize, &assoc, &c) != 5) {
      fatal("bad l1 I-cache parms: <name>:<nsets>:<bsize>:<assoc>:<repl>");
    }
    cache_il1 = cache_create(name, nsets, bsize, /* balloc */FALSE, /* usize */0, assoc, cache_char2policy(c), il1_access_fn, /* hit lat */cache_il1_lat);

    /* pipelined L2 cache: define l2 issue latency */
    cache_il1->nlev_issuelat = cache_l2_issuelat;

    /* is the level 2 D-cache defined? */
    if (!mystricmp(cache_il2_opt, "none")) {
      cache_il2 = NULL;
    } else if (!mystricmp(cache_il2_opt, "dl2")) {
      if (!cache_dl2) {
        fatal("I-cache l2 cannot access D-cache l2 as it's undefined");
      }
      cache_il2 = cache_dl2;
    } else {
      if (sscanf(cache_il2_opt, "%[^:]:%d:%d:%d:%c", name, &nsets, &bsize, &assoc, &c) != 5) {
        fatal("bad l2 I-cache parms: <name>:<nsets>:<bsize>:<assoc>:<repl>");
      }
      cache_il2 = cache_create(name, nsets, bsize, /* balloc */FALSE, /* usize */0, assoc, cache_char2policy(c), il2_access_fn, /* hit lat */cache_il2_lat);
    }
  }

  /* use an I-TLB? */
  if (!mystricmp(itlb_opt, "none")) {
    itlb = NULL;
  } else {
    if (sscanf(itlb_opt, "%[^:]:%d:%d:%d:%c", name, &nsets, &bsize, &assoc, &c) != 5) {
      fatal("bad TLB parms: <name>:<nsets>:<page_size>:<assoc>:<repl>");
    }
    itlb = cache_create(name, nsets, bsize, /* balloc */FALSE,
      /* usize */sizeof(md_addr_t), assoc,
      cache_char2policy(c), itlb_access_fn,
      /* hit latency */1);
  }

  /* use a D-TLB? */
  if (!mystricmp(dtlb_opt, "none")) {
    dtlb = NULL;
  } else  {
    if (sscanf(dtlb_opt, "%[^:]:%d:%d:%d:%c", name, &nsets, &bsize, &assoc, &c) != 5) {
      fatal("bad TLB parms: <name>:<nsets>:<page_size>:<assoc>:<repl>");
    }
    dtlb = cache_create(name, nsets, bsize, /* balloc */FALSE,
      /* usize */sizeof(md_addr_t), assoc,
      cache_char2policy(c), dtlb_access_fn,
      /* hit latency */1);
  }

  prepare_checkpointing();

}

/* print simulator-specific configuration information */
void
sim_aux_config(FILE *stream)            /* output stream */
{
  /* nada */
}

/* register simulator-specific statistics */
void
sim_reg_stats(struct stat_sdb_t *sdb)   /* stats database */
{
# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    int i;
# endif (! CMU_AGGRESSIVE_CODE_ELIMINATION )

  /* mshr: mshr stats */
  mshr_prof = stat_reg_dist(sdb, "mshr_prof",
         "number of cycles spent in each mshr size",
         /* initial value */  0,
         /* array size */     MAX_MSHRS,
         /* bucket size */    1,
         /* print format */   (PF_COUNT|PF_PDF),
         /* format */         NULL,
         /* index map */      NULL,
         /* print fn */       NULL);

  stat_reg_counter(sdb, "sim_sample_size",
       "total number of checkpoints processed",
       &sim_sample_size, sim_sample_size, NULL);
  stat_reg_counter(sdb, "sim_meas_insn",
       "total number of instructions measured",
       &sim_meas_insn, sim_meas_insn, NULL);
  stat_reg_counter(sdb, "sim_detail_insn",
       "total number of instructions in detailed simulation",
       &sim_detail_insn, sim_detail_insn, NULL);
  stat_reg_counter(sdb, "sim_pop_insn",
       "total number of instructions in functional and detailed simulation",
       &sim_pop_insn, sim_pop_insn, NULL);
  stat_reg_counter(sdb, "sim_num_refs",
       "total number of loads and stores committed",
       &sim_num_refs, 0, NULL);
  stat_reg_counter(sdb, "sim_num_loads",
       "total number of loads committed",
       &sim_num_loads, 0, NULL);
  stat_reg_formula(sdb, "sim_num_stores",
       "total number of stores committed",
       "sim_num_refs - sim_num_loads", NULL);
  stat_reg_counter(sdb, "sim_num_branches",
       "total number of branches committed",
       &sim_num_branches, /* initial value */0, /* format */NULL);
  stat_reg_int(sdb, "sim_elapsed_time",
         "total simulation time in seconds",
         &sim_elapsed_time, 0, NULL);
  stat_reg_formula(sdb, "sim_inst_rate",
       "simulation speed (in measured insts/sec)",
       "sim_meas_insn / sim_elapsed_time", NULL);
  stat_reg_formula(sdb, "sim_pop_rate",
       "simulation speed (in all simulated insts/sec)",
       "sim_pop_insn / sim_elapsed_time", NULL);

  stat_reg_counter(sdb, "sim_total_insn",
       "total number of instructions executed",
       &sim_total_insn, 0, NULL);
  stat_reg_counter(sdb, "sim_total_refs",
       "total number of loads and stores executed",
       &sim_total_refs, 0, NULL);
  stat_reg_counter(sdb, "sim_total_loads",
       "total number of loads executed",
       &sim_total_loads, 0, NULL);
  stat_reg_formula(sdb, "sim_total_stores",
       "total number of stores executed",
       "sim_total_refs - sim_total_loads", NULL);
  stat_reg_counter(sdb, "sim_total_branches",
       "total number of branches executed",
       &sim_total_branches, /* initial value */0, /* format */NULL);

  /* register performance stats */
  stat_reg_counter(sdb, "sim_meas_cycle",
       "total number of cycles measured",
       &sim_meas_cycle, sim_meas_cycle, NULL);
  stat_reg_formula(sdb, "sim_IPC",
       "instructions per cycle",
       "sim_meas_insn / sim_meas_cycle", /* format */NULL);

  stat_reg_formula(sdb, "sim_CPI",
       "cycles per instruction",
       "sim_meas_cycle / sim_meas_insn", /* format */NULL);

  sample_CPI_mean = stat_reg_mean(sdb, "sample_CPI_mean",
		   "sampled cycles per instruction",
		   /* Z */ target_z, /* desiredErrorPct */ target_pcterror, /* format */NULL);

# if CMU_WATTCH_ENABLED
    sample_EPI_mean = stat_reg_mean(sdb, "sample_EPI_mean",
		     "sampled energy per instruction",
		     /* Z */ target_z, /* desiredErrorPct */ target_pcterror, /* format */NULL);
# endif CMU_WATTCH_ENABLED

  if (paired_in_filename) {
      paired_delta_mean = stat_reg_deltamean(sdb, "paired_delta_mean",
		   "Mean delta on paired-measurement statistic",
		   /* Z */ target_z, /* desiredErrorPct */ target_pcterror, /* format */NULL);
  }

  stat_reg_formula(sdb, "sim_exec_BW",
       "total instructions (mis-spec + committed) per cycle",
       "sim_total_insn / sim_meas_cycle", /* format */NULL);
  stat_reg_formula(sdb, "sim_IPB",
       "instruction per branch",
       "sim_meas_insn / sim_num_branches", /* format */NULL);

  /* occupancy stats */
  stat_reg_counter(sdb, "IFQ_count", "cumulative IFQ occupancy",
                   &IFQ_count, /* initial value */0, /* format */NULL);
  stat_reg_counter(sdb, "IFQ_fcount", "cumulative IFQ full count",
                   &IFQ_fcount, /* initial value */0, /* format */NULL);
  stat_reg_formula(sdb, "ifq_occupancy", "avg IFQ occupancy (insn's)",
                   "IFQ_count / sim_meas_cycle", /* format */NULL);
  stat_reg_formula(sdb, "ifq_rate", "avg IFQ dispatch rate (insn/cycle)",
                   "sim_total_insn / sim_meas_cycle", /* format */NULL);
  stat_reg_formula(sdb, "ifq_latency", "avg IFQ occupant latency (cycle's)",
                   "ifq_occupancy / ifq_rate", /* format */NULL);
  stat_reg_formula(sdb, "ifq_full", "fraction of time (cycle's) IFQ was full",
                   "IFQ_fcount / sim_meas_cycle", /* format */NULL);

  stat_reg_counter(sdb, "RUU_count", "cumulative RUU occupancy",
                   &RUU_count, /* initial value */0, /* format */NULL);
  stat_reg_counter(sdb, "RUU_fcount", "cumulative RUU full count",
                   &RUU_fcount, /* initial value */0, /* format */NULL);
  stat_reg_formula(sdb, "ruu_occupancy", "avg RUU occupancy (insn's)",
                   "RUU_count / sim_meas_cycle", /* format */NULL);
  stat_reg_formula(sdb, "ruu_rate", "avg RUU dispatch rate (insn/cycle)",
                   "sim_total_insn / sim_meas_cycle", /* format */NULL);
  stat_reg_formula(sdb, "ruu_latency", "avg RUU occupant latency (cycle's)",
                   "ruu_occupancy / ruu_rate", /* format */NULL);
  stat_reg_formula(sdb, "ruu_full", "fraction of time (cycle's) RUU was full",
                   "RUU_fcount / sim_meas_cycle", /* format */NULL);

  stat_reg_counter(sdb, "LSQ_count", "cumulative LSQ occupancy",
                   &LSQ_count, /* initial value */0, /* format */NULL);
  stat_reg_counter(sdb, "LSQ_fcount", "cumulative LSQ full count",
                   &LSQ_fcount, /* initial value */0, /* format */NULL);
  stat_reg_formula(sdb, "lsq_occupancy", "avg LSQ occupancy (insn's)",
                   "LSQ_count / sim_meas_cycle", /* format */NULL);
  stat_reg_formula(sdb, "lsq_rate", "avg LSQ dispatch rate (insn/cycle)",
                   "sim_total_insn / sim_meas_cycle", /* format */NULL);
  stat_reg_formula(sdb, "lsq_latency", "avg LSQ occupant latency (cycle's)",
                   "lsq_occupancy / lsq_rate", /* format */NULL);
  stat_reg_formula(sdb, "lsq_full", "fraction of time (cycle's) LSQ was full",
                   "LSQ_fcount / sim_meas_cycle", /* format */NULL);

  stat_reg_counter(sdb, "SB_full_stalls", "total stall cycles due to SB full",
                   &sb_full_stall_cycles, /* initial value */0, /* format */NULL);
  stat_reg_counter(sdb, "SB_count", "cumulative SB occupancy",
                   &sb_count, /* initial value */0, /* format */NULL);
  stat_reg_int(sdb, "SB_max", "Maximum SB occupancy",
                   &sb_max, /* initial value */0, /* format */NULL);
  stat_reg_formula(sdb, "SB_occupancy", "avg SB occupancy",
                   "SB_count / sim_meas_cycle", /* format */NULL);

# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    stat_reg_counter(sdb, "sim_slip",
                   "total number of slip cycles",
                   &sim_slip, 0, NULL);
    /* register baseline stats */
    stat_reg_formula(sdb, "avg_sim_slip",
                   "the average slip between issue and retirement",
                   "sim_slip / sim_meas_insn", NULL);
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

  /* register predictor stats */
  if (pred)
    bpred_reg_stats(pred, sdb);

  /* register cache stats */
  if (cache_il1
      && (cache_il1 != cache_dl1 && cache_il1 != cache_dl2))
    cache_reg_stats(cache_il1, sdb);
  if (cache_il2
      && (cache_il2 != cache_dl1 && cache_il2 != cache_dl2))
    cache_reg_stats(cache_il2, sdb);
  if (cache_dl1)
    cache_reg_stats(cache_dl1, sdb);
  if (cache_dl2)
    cache_reg_stats(cache_dl2, sdb);
  if (itlb)
    cache_reg_stats(itlb, sdb);
  if (dtlb)
    cache_reg_stats(dtlb, sdb);

  /* register power stats */
  CMU_WATTCH(power_reg_stats(sdb));


  /* debug variable(s) */
  stat_reg_counter(sdb, "sim_invalid_addrs",
       "total non-speculative bogus addresses seen (debug var)",
                   &sim_invalid_addrs, /* initial value */0, /* format */NULL);

# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    for (i=0; i<pcstat_nelt; i++) {
      char buf[512], buf1[512];
      struct stat_stat_t *stat;

      /* track the named statistical variable by text address */

      /* find it... */
      stat = stat_find_stat(sdb, pcstat_vars[i]);
      if (!stat)
  fatal("cannot locate any statistic named `%s'", pcstat_vars[i]);

      /* stat must be an integral type */
      if (stat->sc != sc_int && stat->sc != sc_uint && stat->sc != sc_counter)
  fatal("`-pcstat' statistical variable `%s' is not an integral type",
        stat->name);

      /* register this stat */
      pcstat_stats[i] = stat;
      pcstat_lastvals[i] = STATVAL(stat);

      /* declare the sparce text distribution */
      sprintf(buf, "%s_by_pc", stat->name);
      sprintf(buf1, "%s (by text address)", stat->desc);
      pcstat_sdists[i] = stat_reg_sdist(sdb, buf, buf1,
          /* initial value */0,
          /* print format */(PF_COUNT|PF_PDF),
          /* format */"0x%lx %lu %.2f",
          /* print fn */NULL);
    }
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

  ld_reg_stats(sdb);
  mem_reg_stats(mem, sdb);
}

/* forward declarations */
static void ruu_init(void);
static void lsq_init(void);
static void rslink_init(int nlinks);
static void eventq_init(void);
static void readyq_init(void);
static void cv_init(void);
static void tracer_init(void);
static void fetch_init(void);

/* initialize the simulator */
void
sim_init(void)
{
  sim_num_refs = 0;

  /* allocate and initialize register file */
  regs_init(&regs);

  /* allocate and initialize memory space */
  mem = mem_create("mem");
  mem_init(mem);


  /* compute static power estimates */
  CMU_WATTCH(calculate_power(&power));
}

/* total RS links allocated at program start */
#define MAX_RS_LINKS                    4096

#if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
  /* Forward declate registration function for DLite support */

  static char *         /* err str, NULL for no err */
  simoo_reg_obj(struct regs_t *xregs,   /* registers to access */
        int is_write,     /* access type */
        enum md_reg_type rt,    /* reg bank to probe */
        int reg,        /* register number */
        struct eval_value_t *val);    /* input, output */

  static char *                                   /* err str, NULL for no err */
  simoo_mstate_obj(FILE *stream,                  /* output stream */
                 char *cmd,                     /* optional command string */
                 struct regs_t *regs,           /* registers to access */
                 struct mem_t *mem);             /* memory space to access */

  static char *                                   /* err str, NULL for no err */
  simoo_mem_obj(struct mem_t *mem,                /* memory space to access */
              int is_write,                     /* access type */
              md_addr_t addr,                   /* address to access */
              char *p,                          /* input/output buffer */
              int nbytes);                       /* size of access */

#endif


/* load program into simulated state */
void
sim_load_prog(char *fname,    /* program to load */
        int argc, char **argv,  /* program arguments */
        char **envp)    /* program environment */
{
  /* load program text and data, set up environment, memory, and regs */
  ld_load_prog(fname, argc, argv, envp, &regs, mem, TRUE);

  /* NOTE: Alpha-specific pre-decoding goes here. I do not include the
     code since we only care about PISA */

# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
      /* initialize here, so symbols can be loaded */
      if (ptrace_nelt == 2)
        {
          /* generate a pipeline trace */
          ptrace_open(/* fname */ptrace_opts[0], /* range */ptrace_opts[1]);
        }
      else if (ptrace_nelt == 0)
        {
          /* no pipetracing */;
        }
      else
        fatal("bad pipetrace args, use: <fname|stdout|stderr> <range>");
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

  /* finish initialization of the simulation engine */
  fu_pool = res_create_pool("fu-pool", fu_config, N_ELT(fu_config));
  rslink_init(MAX_RS_LINKS);
  tracer_init();
  fetch_init();
  cv_init();
  eventq_init();
  readyq_init();
  ruu_init();
  lsq_init();
  SB_alloc(sb_size);

# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    /* initialize the DLite debugger */
    dlite_init(simoo_reg_obj, simoo_mem_obj, simoo_mstate_obj);
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
}

/* dump simulator-specific auxiliary simulator statistics */
void
sim_aux_stats(FILE *stream)             /* output stream */
{
  /* nada */
}

/* un-initialize the simulator */
void
sim_uninit(void)
{
#    if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
      if (ptrace_nelt > 0)
        ptrace_close();
#    endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
}


/*
 * processor core definitions and declarations
 */

/* inst tag type, used to tag an operation instance in the RUU */
typedef unsigned int INST_TAG_TYPE;

/* inst sequence type, used to order instructions in the ready list, if
   this rolls over the ready list order temporarily will get messed up,
   but execution will continue and complete correctly */
typedef unsigned int INST_SEQ_TYPE;


/* total input dependencies possible */
#define MAX_IDEPS               3

/* total output dependencies possible */
#define MAX_ODEPS               2

/* a register update unit (RUU) station, this record is contained in the
   processors RUU, which serves as a collection of ordered reservations
   stations.  The reservation stations capture register results and await
   the time when all operands are ready, at which time the instruction is
   issued to the functional units; the RUU is an order circular queue, in which
   instructions are inserted in fetch (program) order, results are stored in
   the RUU buffers, and later when an RUU entry is the oldest entry in the
   machines, it and its instruction's value is retired to the architectural
   register file in program order, NOTE: the RUU and LSQ share the same
   structure, this is useful because loads and stores are split into two
   operations: an effective address add and a load/store, the add is inserted
   into the RUU and the load/store inserted into the LSQ, allowing the add
   to wake up the load/store when effective address computation has finished */
struct RUU_station {
  /* inst info */
  md_inst_t IR;     /* instruction bits */
  enum md_opcode op;      /* decoded instruction opcode */
  md_addr_t PC, next_PC, pred_PC; /* inst PC, next PC, predicted PC */
  int in_LSQ;       /* non-zero if op is in LSQ */
  int ea_comp;        /* non-zero if op is an addr comp */
  int recover_inst;     /* start of mis-speculation? */
  int stack_recover_idx;    /* non-speculative TOS for RSB pred */
  struct bpred_update_t dir_update; /* bpred direction update info */
  int spec_mode;      /* non-zero if issued in spec_mode */
  md_addr_t addr;     /* effective address for ld/st's */
  INST_TAG_TYPE tag;      /* RUU slot tag, increment to
             squash operation */
  INST_SEQ_TYPE seq;      /* instruction sequence, used to
             sort the ready list and tag inst */
# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    unsigned int ptrace_seq;    /* pipetrace sequence number */
    int slip;
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
  /* instruction status */
  int queued;       /* operands ready and queued */
  int issued;       /* operation is/was executing */
  int completed;      /* operation has completed execution */

  /* mshr: mem_ready */
  tick_t mem_ready;   /* time when cache_dl1 is ready for access */

  tick_t tlb_clear_time;  /* time when store will clear TLB */

  int measured;


  /* Wattch: values of source operands and result operand used for AF generation */
  quad_t val_ra, val_rb, val_rc, val_ra_result;

  /* output operand dependency list, these lists are used to
     limit the number of associative searches into the RUU when
     instructions complete and need to wake up dependent insts */
  int onames[MAX_ODEPS];    /* output logical names (NA=unused) */
  struct RS_link *odep_list[MAX_ODEPS]; /* chains to consuming operations */

  /* input dependent links, the output chains rooted above use these
     fields to mark input operands as ready, when all these fields have
     been set non-zero, the RUU operation has all of its register
     operands, it may commence execution as soon as all of its memory
     operands are known to be read (see lsq_refresh() for details on
     enforcing memory dependencies) */
  int idep_ready[MAX_IDEPS];    /* input operand ready? */
};

/* non-zero if all register operands are ready, update with MAX_IDEPS */
#define OPERANDS_READY(RS)                                              \
  ((RS)->idep_ready[0] && (RS)->idep_ready[1] && (RS)->idep_ready[2])

/* non-zero if one register operands is ready, update with MAX_IDEPS */
#define ONE_OPERANDS_READY(RS)                                              \
  ((RS)->idep_ready[0] || (RS)->idep_ready[1])

/* register update unit, combination of reservation stations and reorder
   buffer device, organized as a circular queue */
static struct RUU_station *RUU;   /* register update unit */
static int RUU_head, RUU_tail;    /* RUU head and tail pointers */
static int RUU_num;     /* num entries currently in RUU */

/* allocate and initialize register update unit (RUU) */
static void
ruu_init(void)
{
  RUU = calloc(RUU_size, sizeof(struct RUU_station));
  if (!RUU)
    fatal("out of virtual memory");

  RUU_size_mask = (RUU_size - 1);

  RUU_num = 0;
  RUU_head = RUU_tail = 0;
  RUU_count = 0;
  RUU_fcount = 0;
}


/*
 * load/store queue (LSQ): holds loads and stores in program order, indicating
 * status of load/store access:
 *
 *   - issued: address computation complete, memory access in progress
 *   - completed: memory access has completed, stored value available
 *   - squashed: memory access was squashed, ignore this entry
 *
 * loads may execute when:
 *   1) register operands are ready, and
 *   2) memory operands are ready (no earlier unresolved store)
 *
 * loads are serviced by:
 *   1) previous store at same address in LSQ (hit latency), or
 *   2) data cache (hit latency + miss latency)
 *
 * stores may execute when:
 *   1) register operands are ready
 *
 * stores are serviced by:
 *   1) depositing store value into the load/store queue
 *   2) writing store value to the store buffer (plus tag check) at commit
 *   3) writing store buffer entry to data cache when cache is free
 *
 * NOTE: the load/store queue can bypass a store value to a load in the same
 *   cycle the store executes (using a bypass network), thus stores complete
 *   in effective zero time after their effective address is known
 */
static struct RUU_station *LSQ;         /* load/store queue */
static int LSQ_head, LSQ_tail;          /* LSQ head and tail pointers */
static int LSQ_num;                     /* num entries currently in LSQ */

/*
 * input dependencies for stores in the LSQ:
 *   idep #0 - operand input (value that is store'd)
 *   idep #1 - effective address input (address of store operation)
 */
#define STORE_OP_INDEX                  0
#define STORE_ADDR_INDEX                1

#define STORE_OP_READY(RS)              ((RS)->idep_ready[STORE_OP_INDEX])
#define STORE_ADDR_READY(RS)            ((RS)->idep_ready[STORE_ADDR_INDEX])

/* allocate and initialize the load/store queue (LSQ) */
static void
lsq_init(void)
{
  LSQ = calloc(LSQ_size, sizeof(struct RUU_station));
  if (!LSQ)
    fatal("out of virtual memory");

  LSQ_size_mask = (LSQ_size - 1);

  LSQ_num = 0;
  LSQ_head = LSQ_tail = 0;
  LSQ_count = 0;
  LSQ_fcount = 0;
}


/*
 * RS_LINK defs and decls
 */

/* a reservation station link: this structure links elements of a RUU
   reservation station list; used for ready instruction queue, event queue, and
   output dependency lists; each RS_LINK node contains a pointer to the RUU
   entry it references along with an instance tag, the RS_LINK is only valid if
   the instruction instance tag matches the instruction RUU entry instance tag;
   this strategy allows entries in the RUU can be squashed and reused without
   updating the lists that point to it, which significantly improves the
   performance of (all to frequent) squash events */
struct RS_link {
  struct RS_link *next;     /* next entry in list */
  struct RUU_station *rs;   /* referenced RUU resv station */
  INST_TAG_TYPE tag;      /* inst instance sequence number */
  union {
    tick_t when;      /* time stamp of entry (for eventq) */
    INST_SEQ_TYPE seq;      /* inst sequence */
    int opnum;        /* input/output operand number */
  } x;
};

/* RS link free list, grab RS_LINKs from here, when needed */
static struct RS_link *rslink_free_list;

/* NULL value for an RS link */
#define RSLINK_NULL_DATA    { NULL, NULL, 0 }
static struct RS_link RSLINK_NULL = RSLINK_NULL_DATA;

/* create and initialize an RS link */
#define RSLINK_INIT(RSL, RS)            \
  ((RSL).next = NULL, (RSL).rs = (RS), (RSL).tag = (RS)->tag)

/* non-zero if RS link is NULL */
#define RSLINK_IS_NULL(LINK)            ((LINK)->rs == NULL)

/* non-zero if RS link is to a valid (non-squashed) entry */
#define RSLINK_VALID(LINK)              ((LINK)->tag == (LINK)->rs->tag)

/* extra RUU reservation station pointer */
#define RSLINK_RS(LINK)                 ((LINK)->rs)

#if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
   /* get a new RS link record */
# define RSLINK_NEW(DST, RS)            \
  { struct RS_link *n_link;           \
    if (!rslink_free_list)            \
      panic("out of rs links");           \
    n_link = rslink_free_list;            \
    rslink_free_list = rslink_free_list->next;        \
    n_link->next = NULL;            \
    n_link->rs = (RS); n_link->tag = n_link->rs->tag;     \
    (DST) = n_link;             \
  }
#else //Aggressive version follows
   /* get a new RS link record */
# define RSLINK_NEW(DST, RS)            \
  { struct RS_link *n_link;           \
    n_link = rslink_free_list;            \
    rslink_free_list = rslink_free_list->next;        \
    n_link->next = NULL;            \
    n_link->rs = (RS); n_link->tag = n_link->rs->tag;     \
    (DST) = n_link;             \
  }
#endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

/* free an RS link record */
#define RSLINK_FREE(LINK)           \
  {  struct RS_link *f_link = (LINK);         \
     f_link->rs = NULL; f_link->tag = 0;        \
     f_link->next = rslink_free_list;         \
     rslink_free_list = f_link;           \
  }

/* FIXME: could this be faster!!! */
/* free an RS link list */
#define RSLINK_FREE_LIST(LINK)            \
  {  struct RS_link *fl_link, *fl_link_next;        \
     for (fl_link=(LINK); fl_link; fl_link=fl_link_next)    \
       {                \
   fl_link_next = fl_link->next;          \
   RSLINK_FREE(fl_link);            \
       }                \
  }

/* initialize the free RS_LINK pool */
static void
rslink_init(int nlinks)     /* total number of RS_LINK available */
{
  int i;
  struct RS_link *link;

  rslink_free_list = NULL;
  for (i=0; i<nlinks; i++)
    {
      link = calloc(1, sizeof(struct RS_link));
      if (!link)
  fatal("out of virtual memory");
      link->next = rslink_free_list;
      rslink_free_list = link;
    }
}

/* service all functional unit release events, this function is called
   once per cycle, and it used to step the BUSY timers attached to each
   functional unit in the function unit resource pool, as long as a functional
   unit's BUSY count is > 0, it cannot be issued an operation */
static void
ruu_release_fu(void)
{
  int i;

  /* walk all resource units, decrement busy counts by one */
  for (i=0; i<fu_pool->num_resources; i++)
    {
      /* resource is released when BUSY hits zero */
      if (fu_pool->resources[i].busy > 0)
  fu_pool->resources[i].busy--;
    }
}


/*
 * the execution unit event queue implementation follows, the event queue
 * indicates which instruction will complete next, the writeback handler
 * drains this queue
 */

/* pending event queue, sorted from soonest to latest event (in time), NOTE:
   RS_LINK nodes are used for the event queue list so that it need not be
   updated during squash events */
static struct RS_link *event_queue;

/* initialize the event queue structures */
static void
eventq_init(void)
{
  event_queue = NULL;
}

/* insert an event for RS into the event queue, event queue is sorted from
   earliest to latest event, event and associated side-effects will be
   apparent at the start of cycle WHEN */
static void
eventq_queue_event(struct RUU_station *rs, tick_t when)
{
  struct RS_link *prev, *ev, *new_ev;

# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    if (rs->completed)
      panic("event completed");
    if (when <= sim_cycle)
      panic("event occurred in the past");
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

  /* get a free event record */
  RSLINK_NEW(new_ev, rs);
  new_ev->x.when = when;

  /* locate insertion point */
  for (prev=NULL, ev=event_queue;
       ev && ev->x.when < when;
       prev=ev, ev=ev->next);

  if (prev)
    {
      /* insert middle or end */
      new_ev->next = prev->next;
      prev->next = new_ev;
    }
  else
    {
      /* insert at beginning */
      new_ev->next = event_queue;
      event_queue = new_ev;
    }
}

/* return the next event that has already occurred, returns NULL when no
   remaining events or all remaining events are in the future */
static struct RUU_station *
eventq_next_event(void)
{
  struct RS_link *ev;

  if (event_queue && event_queue->x.when <= sim_cycle)
    {
      /* unlink and return first event on priority list */
      ev = event_queue;
      event_queue = event_queue->next;

      /* event still valid? */
      if (RSLINK_VALID(ev))
  {
    struct RUU_station *rs = RSLINK_RS(ev);

    /* reclaim event record */
    RSLINK_FREE(ev);

    /* event is valid, return resv station */
    return rs;
  }
      else
  {
    /* reclaim event record */
    RSLINK_FREE(ev);

    /* receiving inst was squashed, return next event */
    return eventq_next_event();
  }
    }
  else
    {
      /* no event or no event is ready */
      return NULL;
    }
}


/*
 * the ready instruction queue implementation follows, the ready instruction
 * queue indicates which instruction have all of there *register* dependencies
 * satisfied, instruction will issue when 1) all memory dependencies for
 * the instruction have been satisfied (see lsq_refresh() for details on how
 * this is accomplished) and 2) resources are available; ready queue is fully
 * constructed each cycle before any operation is issued from it -- this
 * ensures that instruction issue priorities are properly observed; NOTE:
 * RS_LINK nodes are used for the event queue list so that it need not be
 * updated during squash events
 */

/* the ready instruction queue */
static struct RS_link *ready_queue;

/* initialize the event queue structures */
static void
readyq_init(void)
{
  ready_queue = NULL;
}

/* insert ready node into the ready list using ready instruction scheduling
   policy; currently the following scheduling policy is enforced:

     memory and long latency operands, and branch instructions first

   then

     all other instructions, oldest instructions first

  this policy works well because branches pass through the machine quicker
  which works to reduce branch misprediction latencies, and very long latency
  instructions (such loads and multiplies) get priority since they are very
  likely on the program's critical path */
static void
readyq_enqueue(struct RUU_station *rs)    /* RS to enqueue */
{
  struct RS_link *prev, *node, *new_node;

  /* node is now queued */
# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    if (rs->queued)
      panic("node is already queued");
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

  rs->queued = TRUE;

  /* get a free ready list node */
  RSLINK_NEW(new_node, rs);
  new_node->x.seq = rs->seq;

  /* locate insertion point */
  if (rs->in_LSQ || MD_OP_FLAGS(rs->op) & (F_LONGLAT|F_CTRL))
    {
      /* insert loads/stores and long latency ops at the head of the queue */
      prev = NULL;
      node = ready_queue;
    }
  else
    {
      /* otherwise insert in program order (earliest seq first) */
      for (prev=NULL, node=ready_queue;
     node && node->x.seq < rs->seq;
     prev=node, node=node->next);
    }

  if (prev)
    {
      /* insert middle or end */
      new_node->next = prev->next;
      prev->next = new_node;
    }
  else
    {
      /* insert at beginning */
      new_node->next = ready_queue;
      ready_queue = new_node;
    }
}


/*
 * the create vector maps a logical register to a creator in the RUU (and
 * specific output operand) or the architected register file (if RS_link
 * is NULL)
 */

/* an entry in the create vector */
struct CV_link {
  struct RUU_station *rs;               /* creator's reservation station */
  int odep_num;                         /* specific output operand */
};

/* a NULL create vector entry */
static struct CV_link CVLINK_NULL = { NULL, 0 };

/* get a new create vector link */
#define CVLINK_INIT(CV, RS,ONUM)  ((CV).rs = (RS), (CV).odep_num = (ONUM))

/* size of the create vector (one entry per architected register) */
#define CV_BMAP_SZ              (BITMAP_SIZE(MD_TOTAL_REGS))

/* the create vector, NOTE: speculative copy on write storage provided
   for fast recovery during wrong path execute (see tracer_recover() for
   details on this process */
static BITMAP_TYPE(MD_TOTAL_REGS, use_spec_cv);
static struct CV_link create_vector[MD_TOTAL_REGS];
static struct CV_link spec_create_vector[MD_TOTAL_REGS];

/* these arrays shadow the create vector an indicate when a register was
   last created */
static tick_t create_vector_rt[MD_TOTAL_REGS];
static tick_t spec_create_vector_rt[MD_TOTAL_REGS];

/* read a create vector entry */
#define CREATE_VECTOR(N)        (BITMAP_SET_P(use_spec_cv, CV_BMAP_SZ, (N))\
         ? spec_create_vector[N]                \
         : create_vector[N])

/* read a create vector timestamp entry */
#define CREATE_VECTOR_RT(N)     (BITMAP_SET_P(use_spec_cv, CV_BMAP_SZ, (N))\
         ? spec_create_vector_rt[N]             \
         : create_vector_rt[N])

/* set a create vector entry */
#define SET_CREATE_VECTOR(N, L) (spec_mode                              \
         ? (BITMAP_SET(use_spec_cv, CV_BMAP_SZ, (N)),\
            spec_create_vector[N] = (L))        \
         : (create_vector[N] = (L)))

/* initialize the create vector */
static void
cv_init(void)
{
  int i;

  /* initially all registers are valid in the architected register file,
     i.e., the create vector entry is CVLINK_NULL */
  for (i=0; i < MD_TOTAL_REGS; i++)
    {
      create_vector[i] = CVLINK_NULL;
      create_vector_rt[i] = 0;
      spec_create_vector[i] = CVLINK_NULL;
      spec_create_vector_rt[i] = 0;
    }

  /* all create vector entries are non-speculative */
  BITMAP_CLEAR_MAP(use_spec_cv, CV_BMAP_SZ);
}

/*
 *  RUU_COMMIT() - instruction retirement pipeline stage
 */

/* this function commits the results of the oldest completed entries from the
   RUU and LSQ to the architected reg file, stores in the LSQ will commit
   their store data to the data cache at this point as well */
static void
ruu_commit(void)
{
# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    int i, events;
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

  int lat, committed = 0;
  static counter_t sim_ret_insn = 0;

  /* all values must be retired to the architected reg file in program order */
  while (RUU_num > 0 && committed < ruu_commit_width)
    {
      struct RUU_station *rs = &(RUU[RUU_head]);

      if (!rs->completed)
  {
    /* at least RUU entry must be complete */
    break;
  }

#     if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        /* default commit events */
        events = 0;
#     endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

      /* load/stores must retire load/store queue entry as well */
      if (RUU[RUU_head].ea_comp)
  {
    /* load/store, retire head of LSQ as well */
#         if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
      if (LSQ_num <= 0 || !LSQ[LSQ_head].in_LSQ)
        panic("RUU out of sync with LSQ");
#         endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

    /* load/store operation must be complete */
    if (!LSQ[LSQ_head].completed)
      {
        /* load/store operation is not yet complete */
        break;
      }
    if ((MD_OP_FLAGS(LSQ[LSQ_head].op) & (F_MEM|F_STORE))
        == (F_MEM|F_STORE))
      {
        struct res_template *fu;

              /* Stores must incur TLB latency before they may retire */
              if ( LSQ[LSQ_head].tlb_clear_time == 0) {
      /* all loads and stores must to access D-TLB */
      if (dtlb)
        {
          /* access the D-TLB */
          lat =
      cache_access(dtlb, Read, (LSQ[LSQ_head].addr & ~3),
             NULL, NULL, 4, sim_cycle, NULL, NULL, STORE_FLAG);
          LSQ[LSQ_head].tlb_clear_time = sim_cycle + lat;
          if (lat > 1) {
#                       if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        events |= PEV_TLBMISS;
#                       endif (! CMU_AGGRESSIVE_CODE_ELIMINATION )
      break; //Store must wait for TLB miss.
                      }
        }

              } else if (LSQ[LSQ_head].tlb_clear_time > sim_cycle) {
                  break; //Store is still waiting for TLB miss, so stall
              }

              /* If we get here, the store has cleared the TLB */

              /* Now, we need to ensure the store can have an SB entry */
              if (SB_full()) {
                sb_full_stall_cycles++;
                break;  //Store must wait for an empty SB entry
              }

              /* There is an SB entry for the store */

        /* stores must retire their store value to the cache at commit,
     try to get a store port (functional unit allocation) */
        fu = res_get(fu_pool, MD_OP_FUCLASS(LSQ[LSQ_head].op));

        /* mshr: when mshr is ready */
        if (fu && LSQ[LSQ_head].mem_ready <= sim_cycle)
    {
      /* reserve the functional unit */
#                 if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        if (fu->master->busy)
          panic("functional unit already in use");
#                 endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

      /* schedule functional unit release event */
      fu->master->busy = fu->issuelat;

      /* go to the data cache */
      if (cache_dl1)
        {
          /* Wattch -- D-cache access */
          CMU_WATTCH(dcache_access++);

          /* commit store value to D-cache */
          lat =
      cache_access(cache_dl1, Write, (LSQ[LSQ_head].addr&~3),
             NULL, &LSQ[LSQ_head].mem_ready, 4, sim_cycle, NULL, NULL, STORE_FLAG);

                      if (lat < 0) { /* mshr is full or target is not available */
#                        if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
                           assert(LSQ[LSQ_head].mem_ready > sim_cycle); /* mem_ready should be updated */
#                        endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
                         break;
                      }
                      else {
                         SB_enqueue(sim_cycle + lat);

#                        if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
                           if (lat > cache_dl1_lat) {
          events |= PEV_CACHEMISS;
                           }
#                        endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
                      }
        }
    }
        else
    {
      /* no store ports left, cannot continue to commit insts */
      break;
    }
      }

    /* invalidate load/store operation instance */
    LSQ[LSQ_head].tag++;

#         if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
              sim_slip += (sim_cycle - LSQ[LSQ_head].slip);

              /* indicate to pipeline trace that this instruction retired */
              ptrace_newstage(LSQ[LSQ_head].ptrace_seq, PST_COMMIT, events);
              ptrace_endinst(LSQ[LSQ_head].ptrace_seq);
#         endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

    /* commit head of LSQ as well */
    LSQ_head = (LSQ_head + 1) & LSQ_size_mask;
    LSQ_num--;
  }

      /* Wattch -- committed instruction to arch reg file */
      if (CMU_WATTCH_ENABLED && (MD_OP_FLAGS(rs->op) & (F_ICOMP|F_FCOMP)) || ((MD_OP_FLAGS(rs->op) & (F_MEM|F_LOAD)) == (F_MEM|F_LOAD))) {
      regfile_access++;
        if (CMU_WATTCH_DYNAMIC_POWER) {
          regfile_total_pop_count_cycle += pop_count(rs->val_rc);
          regfile_num_pop_count_cycle++;
        }
      }

      if (pred
    && bpred_spec_update == spec_CT
    && (MD_OP_FLAGS(rs->op) & F_CTRL))
  {
    /* Wattch -- bpred access */
    CMU_WATTCH(bpred_access++);

    bpred_update(pred,
           /* branch address */rs->PC,
           /* actual target address */rs->next_PC,
                       /* taken? */rs->next_PC != (rs->PC +
                                                   sizeof(md_inst_t)),
                       /* pred taken? */rs->pred_PC != (rs->PC +
                                                        sizeof(md_inst_t)),
                       /* correct pred? */rs->pred_PC == rs->next_PC,
                       /* opcode */rs->op,
                       /* dir predictor update pointer */&rs->dir_update);
  }

      /* invalidate RUU operation instance */
      RUU[RUU_head].tag++;
#     if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        sim_slip += (sim_cycle - RUU[RUU_head].slip);
#     endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

      /* print retirement trace if in verbose mode */
      if (verbose)
  {
    sim_ret_insn++;
    myfprintf(stderr, "%10n @ 0x%08p: ", sim_ret_insn, RUU[RUU_head].PC);
    md_print_insn(RUU[RUU_head].IR, RUU[RUU_head].PC, stderr);
    if (MD_OP_FLAGS(RUU[RUU_head].op) & F_MEM)
      myfprintf(stderr, "  mem: 0x%08p", RUU[RUU_head].addr);
    fprintf(stderr, "\n");
    /* fflush(stderr); */
  }

#     if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        /* indicate to pipeline trace that this instruction retired */
        ptrace_newstage(RUU[RUU_head].ptrace_seq, PST_COMMIT, events);
        ptrace_endinst(RUU[RUU_head].ptrace_seq);
#     endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

      /* commit head entry of RUU */
      RUU_head = (RUU_head + 1) & RUU_size_mask;
      RUU_num--;

      /* one more instruction committed to architected state */
      committed++;

#     if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        for (i=0; i<MAX_ODEPS; i++) {
    if (rs->odep_list[i])
      panic ("retired instruction has odeps\n");
        }
#     endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
    }
}




/*
 *  SB_COMMIT() - retire stores from store buffer
 */

static void
sb_commit(void)
{
  sb_count += SB_size();
  if (SB_size() > sb_max) sb_max = SB_size();

  while( ! SB_empty() && SB_next_release_cycle() <= sim_cycle ) {
     SB_pop_head();
  }

}


/*
 *  RUU_RECOVER() - squash mispredicted microarchitecture state
 */

/* recover processor microarchitecture state back to point of the
   mis-predicted branch at RUU[BRANCH_INDEX] */
static void
ruu_recover(int branch_index)     /* index of mis-pred branch */
{
  int i, RUU_index = RUU_tail, LSQ_index = LSQ_tail;
  int RUU_prev_tail = RUU_tail, LSQ_prev_tail = LSQ_tail;

  /* recover from the tail of the RUU towards the head until the branch index
     is reached, this direction ensures that the LSQ can be synchronized with
     the RUU */

  /* go to first element to squash */
  RUU_index = (RUU_index + (RUU_size-1)) & RUU_size_mask;
  LSQ_index = (LSQ_index + (LSQ_size-1)) & LSQ_size_mask;

  /* traverse to older insts until the mispredicted branch is encountered */
  while (RUU_index != branch_index)
    {
      /* the RUU should not drain since the mispredicted branch will remain */
#     if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        if (!RUU_num)
    panic("empty RUU");

        /* should meet up with the tail first */
        if (RUU_index == RUU_head)
    panic("RUU head and tail broken");
#     endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

      /* is this operation an effective addr calc for a load or store? */
      if (RUU[RUU_index].ea_comp)
  {
#         if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
      /* should be at least one load or store in the LSQ */
      if (!LSQ_num)
        panic("RUU and LSQ out of sync");
#         endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

    /* recover any resources consumed by the load or store operation */
    for (i=0; i<MAX_ODEPS; i++)
      {
        RSLINK_FREE_LIST(LSQ[LSQ_index].odep_list[i]);
        /* blow away the consuming op list */
        LSQ[LSQ_index].odep_list[i] = NULL;
      }

    /* squash this LSQ entry */
    LSQ[LSQ_index].tag++;

#         if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
            /* indicate in pipetrace that this instruction was squashed */
      ptrace_endinst(LSQ[LSQ_index].ptrace_seq);
#         endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

    /* go to next earlier LSQ slot */
    LSQ_prev_tail = LSQ_index;
    LSQ_index = (LSQ_index + (LSQ_size-1)) & LSQ_size_mask;
    LSQ_num--;
  }

      /* recover any resources used by this RUU operation */
      for (i=0; i<MAX_ODEPS; i++)
  {
    RSLINK_FREE_LIST(RUU[RUU_index].odep_list[i]);
    /* blow away the consuming op list */
    RUU[RUU_index].odep_list[i] = NULL;
  }

      /* squash this RUU entry */
      RUU[RUU_index].tag++;

#     if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        /* indicate in pipetrace that this instruction was squashed */
        ptrace_endinst(RUU[RUU_index].ptrace_seq);
#     endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

      /* go to next earlier slot in the RUU */
      RUU_prev_tail = RUU_index;
      RUU_index = (RUU_index + (RUU_size-1)) & RUU_size_mask;
      RUU_num--;
    }

  /* reset head/tail pointers to point to the mis-predicted branch */
  RUU_tail = RUU_prev_tail;
  LSQ_tail = LSQ_prev_tail;

  /* revert create vector back to last precise create vector state, NOTE:
     this is accomplished by resetting all the copied-on-write bits in the
     USE_SPEC_CV bit vector */
  BITMAP_CLEAR_MAP(use_spec_cv, CV_BMAP_SZ);

  /* FIXME: could reset functional units at squash time */
}


/*
 *  RUU_WRITEBACK() - instruction result writeback pipeline stage
 */

/* forward declarations */
static void tracer_recover(void);

/* writeback completed operation results from the functional units to RUU,
   at this point, the output dependency chains of completing instructions
   are also walked to determine if any dependent instruction now has all
   of its register operands, if so the (nearly) ready instruction is inserted
   into the ready instruction queue */
static void
ruu_writeback(void) {
  int i;
  struct RUU_station *rs;

  /* service all completed events */
  while ((rs = eventq_next_event())) {
    /* RS has completed execution and (possibly) produced a result */
    #if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
      if (!OPERANDS_READY(rs) || rs->queued || !rs->issued || rs->completed)
        panic("inst completed and !ready, !issued, or completed");
    #endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

    /* operation has completed */
    rs->completed = TRUE;

    /* Wattch -- 1) Writeback result to resultbus
                 2) Write result to phys. regs (RUU)
     3) Access wakeup logic
     */
    if(CMU_WATTCH_ENABLED && !(MD_OP_FLAGS(rs->op) & F_CTRL)) {
    window_access++;
    window_preg_access++;
    window_wakeup_access++;
    resultbus_access++;
      if (CMU_WATTCH_DYNAMIC_POWER) {
        window_total_pop_count_cycle += pop_count(rs->val_rc);
        window_num_pop_count_cycle++;
        resultbus_total_pop_count_cycle += pop_count(rs->val_rc);
        resultbus_num_pop_count_cycle++;
      }
    }

    /* does this operation reveal a mis-predicted branch? */
    if (rs->recover_inst) {
      #if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        if (rs->in_LSQ)
          panic("mis-predicted load or store?!?!?");
      #endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

      /* recover processor state and reinit fetch to correct path */
      ruu_recover(rs - RUU);
      tracer_recover();
      bpred_recover(pred, rs->PC, rs->stack_recover_idx, &rs->dir_update);

      /* stall fetch until I-fetch and I-decode recover */
      ruu_fetch_issue_delay = ruu_branch_penalty;

      /* continue writeback of the branch/control instruction */
    }

    /* if we speculatively update branch-predictor, do it here */
    if (pred && bpred_spec_update == spec_WB && !rs->in_LSQ && (MD_OP_FLAGS(rs->op) & F_CTRL)) {
      /* Wattch -- bpred access */
      CMU_WATTCH(bpred_access++);

      bpred_update(pred,
           /* branch address */rs->PC,
           /* actual target address */rs->next_PC,
           /* taken? */rs->next_PC != (rs->PC +
               sizeof(md_inst_t)),
           /* pred taken? */rs->pred_PC != (rs->PC +
              sizeof(md_inst_t)),
           /* correct pred? */rs->pred_PC == rs->next_PC,
           /* opcode */rs->op,
           /* dir predictor update pointer */&rs->dir_update);
    }

    #if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
      /* entered writeback stage, indicate in pipe trace */
      ptrace_newstage(rs->ptrace_seq, PST_WRITEBACK,
          rs->recover_inst ? PEV_MPDETECT : 0);
    #endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )


    /* broadcast results to consuming operations, this is more efficiently
       accomplished by walking the output dependency chains of the
       completed instruction */
    for (i=0; i<MAX_ODEPS; i++) {
      if (rs->onames[i] != NA) {
        struct CV_link link;
        struct RS_link *olink, *olink_next;

        if (rs->spec_mode) {
          /* update the speculative create vector, future operations
             get value from later creator or architected reg file */
          link = spec_create_vector[rs->onames[i]];
          if (/* !NULL */link.rs && /* refs RS */(link.rs == rs && link.odep_num == i)) {
            /* the result can now be read from a physical register,
               indicate this as so */
            spec_create_vector[rs->onames[i]] = CVLINK_NULL;
            spec_create_vector_rt[rs->onames[i]] = sim_cycle;
          }
        /* else, creator invalidated or there is another creator */
        } else {
          /* update the non-speculative create vector, future
             operations get value from later creator or architected
             reg file */
          link = create_vector[rs->onames[i]];
          if (/* !NULL */link.rs && /* refs RS */(link.rs == rs && link.odep_num == i)) {
            /* the result can now be read from a physical register,
               indicate this as so */
            create_vector[rs->onames[i]] = CVLINK_NULL;
            create_vector_rt[rs->onames[i]] = sim_cycle;
          }
        /* else, creator invalidated or there is another creator */
        }

        /* walk output list, queue up ready operations */
        for (olink=rs->odep_list[i]; olink; olink=olink_next) {
          if (RSLINK_VALID(olink)) {
            #if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
              if (olink->rs->idep_ready[olink->x.opnum])
                panic("output dependence already satisfied");
            #endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

            /* input is now ready */
            olink->rs->idep_ready[olink->x.opnum] = TRUE;

            /* are all the register operands of target ready? */
            if (OPERANDS_READY(olink->rs)) {
              /* yes! enqueue instruction as ready, NOTE: stores
                 complete at dispatch, so no need to enqueue
                 them */
              if (!olink->rs->in_LSQ || ((MD_OP_FLAGS(olink->rs->op)&(F_MEM|F_STORE)) == (F_MEM|F_STORE)))
                readyq_enqueue(olink->rs);
              /* else, ld op, issued when no mem conflict */
            }
          }

          /* grab link to next element prior to free */
          olink_next = olink->next;

          /* free dependence link element */
          RSLINK_FREE(olink);
        }

        /* blow away the consuming op list */
        rs->odep_list[i] = NULL;

      } /* if not NA output */

    } /* for all outputs */

  } /* for all writeback events */

}


/*
 *  LSQ_REFRESH() - memory access dependence checker/scheduler
 */

/* this function locates ready instructions whose memory dependencies have
   been satisfied, this is accomplished by walking the LSQ for loads, looking
   for blocking memory dependency condition (e.g., earlier store with an
   unknown address) */
#define MAX_STD_UNKNOWNS    256
static void
lsq_refresh(void)
{
  int i, j, index, n_std_unknowns;
  md_addr_t std_unknowns[MAX_STD_UNKNOWNS];

  /* scan entire queue for ready loads: scan from oldest instruction
     (head) until we reach the tail or an unresolved store, after which no
     other instruction will become ready */
  for (i=0, index=LSQ_head, n_std_unknowns=0;
       i < LSQ_num;
       i++ )
    {
      /* terminate search for ready loads after first unresolved store,
   as no later load could be resolved in its presence */
      if (/* store? */
    (MD_OP_FLAGS(LSQ[index].op) & (F_MEM|F_STORE)) == (F_MEM|F_STORE))
  {
    if (!STORE_ADDR_READY(&LSQ[index]))
      {
        /* FIXME: a later STD + STD known could hide the STA unknown */
        /* sta unknown, blocks all later loads, stop search */
        break;
      }
    else if (!OPERANDS_READY(&LSQ[index]))
      {
        /* sta known, but std unknown, may block a later store, record
     this address for later referral, we use an array here because
     for most simulations the number of entries to search will be
     very small */
        if (n_std_unknowns == MAX_STD_UNKNOWNS)
    fatal("STD unknown array overflow, increase MAX_STD_UNKNOWNS");
        std_unknowns[n_std_unknowns++] = LSQ[index].addr;
      }
    else /* STORE_ADDR_READY() && OPERANDS_READY() */
      {
        /* a later STD known hides an earlier STD unknown */
        for (j=0; j<n_std_unknowns; j++)
    {
      if (std_unknowns[j] == /* STA/STD known */LSQ[index].addr)
        std_unknowns[j] = /* bogus addr */0;
    }
      }
  }

      if (/* load? */
    ((MD_OP_FLAGS(LSQ[index].op) & (F_MEM|F_LOAD)) == (F_MEM|F_LOAD))
    && /* queued? */!LSQ[index].queued
    && /* waiting? */!LSQ[index].issued
    && /* completed? */!LSQ[index].completed
    && /* regs ready? */OPERANDS_READY(&LSQ[index]))
  {
    /* no STA unknown conflict (because we got to this check), check for
       a STD unknown conflict */
    for (j=0; j<n_std_unknowns; j++)
      {
        /* found a relevant STD unknown? */
        if (std_unknowns[j] == LSQ[index].addr)
    break;
      }
    if (j == n_std_unknowns)
      {
        /* no STA or STD unknown conflicts, put load on ready queue */
        readyq_enqueue(&LSQ[index]);
      }
  }
        index=(index + 1) & LSQ_size_mask;
    }
}


/*
 *  RUU_ISSUE() - issue instructions to functional units
 */

/* attempt to issue all operations in the ready queue; insts in the ready
   instruction queue have all register dependencies satisfied, this function
   must then 1) ensure the instructions memory dependencies have been satisfied
   (see lsq_refresh() for details on this process) and 2) a function unit
   is available in this cycle to commence execution of the operation; if all
   goes well, the function unit is allocated, a writeback event is scheduled,
   and the instruction begins execution */
static void
ruu_issue(void)
{
    int i, load_lat, tlb_lat, n_issued;
    struct RS_link *node, *next_node;
    struct res_template *fu;


    /* FIXME: could be a little more efficient when scanning the ready queue */

    /* copy and then blow away the ready list, NOTE: the ready list is
    always totally reclaimed each cycle, and instructions that are not
    issue are explicitly reinserted into the ready instruction queue,
    this management strategy ensures that the ready instruction queue
    is always properly sorted */
    node = ready_queue;
    ready_queue = NULL;

    /* visit all ready instructions (i.e., insts whose register input
    dependencies have been satisfied, stop issue when no more instructions
    are available or issue bandwidth is exhausted */
    for (n_issued=0;
    node && n_issued < ruu_issue_width;
    node = next_node)
    {
        int cache_access_blocked = 0;
        next_node = node->next;

        /* still valid? */
        if (RSLINK_VALID(node))
        {
            struct RUU_station *rs = RSLINK_RS(node);

#           if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
              /* issue operation, both reg and mem deps have been satisfied */
              if (!OPERANDS_READY(rs) || !rs->queued
              || rs->issued || rs->completed)
              panic("issued inst !ready, issued, or completed");
#           endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

            /* node is now un-queued */
            rs->queued = FALSE;

            if (rs->in_LSQ
            && ((MD_OP_FLAGS(rs->op) & (F_MEM|F_STORE)) == (F_MEM|F_STORE)))
            {
                /* stores complete in effectively zero time, result is
                written into the load/store queue, the actual store into
                the memory system occurs when the instruction is retired
                (see ruu_commit()) */
                rs->issued = TRUE;
                rs->completed = TRUE;
#               if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
                  if (rs->onames[0] || rs->onames[1])
                  panic("store creates result");

                  if (rs->recover_inst)
                  panic("mis-predicted store");

                  /* entered execute stage, indicate in pipe trace */
                  ptrace_newstage(rs->ptrace_seq, PST_WRITEBACK, 0);
#               endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

                /* one more inst issued */
                n_issued++;

                if (CMU_WATTCH_ENABLED) {
                /* Wattch -- LSQ access -- write data into store buffer */
                lsq_access++;
                lsq_store_data_access++;
                lsq_preg_access++;
                  if (CMU_WATTCH_DYNAMIC_POWER) {
                  lsq_total_pop_count_cycle += pop_count(rs->val_ra);
                  lsq_num_pop_count_cycle++;
                  }
                }

            }
            else /* ! store */
            {
                /* issue the instruction to a functional unit */
                if (MD_OP_FUCLASS(rs->op) != NA)
                {
                    fu = res_get(fu_pool, MD_OP_FUCLASS(rs->op));

                    /* mshr:
                    1) for mem ops, mshr or target should be available,
                    2) for non-mem ops, mem_ready is always 0 */
                    if (fu && rs->mem_ready <= sim_cycle)
                    {
                        int is_load = (rs->in_LSQ && ((MD_OP_FLAGS(rs->op) & (F_MEM|F_LOAD)) == (F_MEM|F_LOAD))) ;

                        /* got one! issue inst to functional unit */
                        rs->issued = TRUE;
                        /* reserve the functional unit */
#                       if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
                          if (fu->master->busy)
                          panic("functional unit already in use");
#                       endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

                        /* schedule functional unit release event */
                        fu->master->busy = fu->issuelat;

                        /* schedule a result writeback event */
                        if (is_load)
                        {
                            int events = 0;

                            /* if we are on the wrong path, and this is a load,
                               probe the TLB.  If it misses, do not talk to the
                               caches or try to forward - simply set latency
                               to tlb miss latency.  In all other cases, process
                               the load as before */
                            if (( rs->spec_mode && ! cache_probe(dtlb, (rs->addr & ~3)) )) {
                                /* We have a wrong_path TLB miss.  We assume this
                                   miss will suffer the full 200 cycle latency.  It
                                   is prevented from touching any caches */
                                load_lat = tlb_miss_lat;

                            } else {
                                /* for loads, determine cache access latency:
                                first scan LSQ to see if a store forward is
                                possible, if not, access the data cache */

                                /* Wattch -- LSQ access */
                                CMU_WATTCH(lsq_access++);
                                CMU_WATTCH(lsq_wakeup_access++);

                                load_lat = 0;
                                i = (rs - LSQ);
                                if (i != LSQ_head)
                                {
                                    for (;;)
                                    {
                                        /* go to next earlier LSQ entry */
                                        i = (i + (LSQ_size-1)) & LSQ_size_mask;

                                        /* FIXME: not dealing with partials! */
                                        if ((MD_OP_FLAGS(LSQ[i].op) & F_STORE)
                                        && (LSQ[i].addr == rs->addr))
                                        {
                                            /* hit in the LSQ */
                                            load_lat = 1;
                                            if (CMU_WATTCH_ENABLED) {
                                              lsq_access++;
                                              lsq_preg_access++;
                                              lsq_load_data_access++;
                                              if (CMU_WATTCH_DYNAMIC_POWER) {
                                                lsq_total_pop_count_cycle += pop_count(rs->val_ra_result);
                                                lsq_num_pop_count_cycle++;
                                              }
                                            }
                                            break;
                                        }

                                        /* scan finished? */
                                        if (i == LSQ_head)
                                        break;
                                    }
                                }

                                /* was the value store forwared from the LSQ? */
                                if (!load_lat)
                                {
                                    int valid_addr = MD_VALID_ADDR(rs->addr);

                                    if (!rs->spec_mode && !valid_addr)
                                        sim_invalid_addrs++;

                                    /* no! go to the data cache if addr is valid */
                                    if (cache_dl1 && valid_addr)
                                    {
                                        /* Wattch -- D-cache access */
                                        CMU_WATTCH(dcache_access++);

                                        /* access the cache if non-faulting */
                                        load_lat =
                                        cache_access(cache_dl1, Read,
                                        (rs->addr & ~3), NULL, &rs->mem_ready, 4,
                                        sim_cycle, NULL, NULL, (rs->spec_mode ? WRONGPATH_FLAG : 0) );

                                        /* mshr or target is not available */
                                        if (load_lat < 0) {
                                            cache_access_blocked = 1;
                                            rs->issued = FALSE;
                                            readyq_enqueue(rs);
                                            assert(rs->mem_ready > sim_cycle);
                                            mshr_debug("%10d (issue-) mshr or target full (%d: ready at %d with %d) - %d\n",
                                            (int)sim_cycle, count_mshr(cache_dl1,sim_cycle),
                                            (int) rs->mem_ready, load_lat, cache_dl1->ready);
                                        }
                                        else {
                                            cache_access_blocked = 0;

                                            if (load_lat > cache_dl1_lat) {
                                                events |= PEV_CACHEMISS;
                                                mshr_debug("%10d (issue ) cache miss (%d) - %d\n",
                                                (int)sim_cycle, count_mshr(cache_dl1,sim_cycle),
                                                cache_dl1->ready);
                                            }
                                            else {
                                                mshr_debug("%10d (issue ) cache hit (%d) - %d\n",
                                                (int)sim_cycle, count_mshr(cache_dl1,sim_cycle),
                                                cache_dl1->ready);
                                            }
                                        }
                                    }
                                    else
                                    {
                                        /* no caches defined, just use op latency */
                                        load_lat = fu->oplat;
                                    }
                                }

                                /* all loads and stores must to access D-TLB */
                                if (!cache_access_blocked && dtlb && MD_VALID_ADDR(rs->addr))
                                {
                                    /* access the D-TLB, NOTE: this code will
                                    initiate speculative TLB misses */
                                    tlb_lat =
                                    cache_access(dtlb, Read, (rs->addr & ~3),
                                    NULL, NULL, 4, sim_cycle, NULL, NULL, (rs->spec_mode ? WRONGPATH_FLAG : 0));
                                    if (tlb_lat > 1)
                                    events |= PEV_TLBMISS;

                                    /* D-cache/D-TLB accesses occur in parallel */
                                    load_lat = MAX(tlb_lat, load_lat);
                                }
                            }

                            if (!cache_access_blocked) {
                                /* use computed cache access latency */
                                eventq_queue_event(rs, sim_cycle + load_lat);

#                               if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
                                    /* entered execute stage, indicate in pipe trace */
                                    ptrace_newstage(rs->ptrace_seq, PST_EXECUTE,
                                    ((rs->ea_comp ? PEV_AGEN : 0)
                                    | events));
#                               endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

                            }
                        }
                        else /* !load && !store */
                        {
                            if (CMU_WATTCH_ENABLED) {
                              /* Wattch -- ALU access Wattch-FIXME
                                (different op types)
                                also spread out power of multi-cycle ops
                              */
                              alu_access++;

                              if((MD_OP_FLAGS(rs->op) & (F_FCOMP))== (F_FCOMP))
                                falu_access++;
                              else
                                ialu_access++;
                            }


                            /* use deterministic functional unit latency */
                            eventq_queue_event(rs, sim_cycle + fu->oplat);

#                           if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
                                /* entered execute stage, indicate in pipe trace */
                                ptrace_newstage(rs->ptrace_seq, PST_EXECUTE,
                                rs->ea_comp ? PEV_AGEN : 0);
#                           endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
                        }

                        if (!cache_access_blocked) {

                            if (CMU_WATTCH_ENABLED) {
                            /* Wattch -- window access */
                            window_access++;
                            /* read values from window send to FUs */
                            window_preg_access++;
                            window_preg_access++;
                                if (CMU_WATTCH_DYNAMIC_POWER) {
                                window_total_pop_count_cycle += pop_count(rs->val_ra) + pop_count(rs->val_rb);
                                window_num_pop_count_cycle+=2;
                                }
                            }

                            /* one more inst issued */
                            n_issued++;
                        }
                    }
                    else /* no functional unit */
                    {
                        /* insufficient functional unit resources, put operation
                        back onto the ready list, we'll try to issue it
                        again next cycle */
                        readyq_enqueue(rs);
                    }

                }
                else /* does not require a functional unit! */
                {
                    /* FIXME: need better solution for these */
                    /* the instruction does not need a functional unit */
                    rs->issued = TRUE;

                    /* schedule a result event */
                    eventq_queue_event(rs, sim_cycle + 1);

#                   if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
                        /* entered execute stage, indicate in pipe trace */
                        ptrace_newstage(rs->ptrace_seq, PST_EXECUTE,
                        rs->ea_comp ? PEV_AGEN : 0);
#                   endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

                    if (CMU_WATTCH_ENABLED) {
                    /* Wattch -- window access */
                    window_access++;
                    /* read values from window send to FUs */
                    window_preg_access++;
                    window_preg_access++;
                        if (CMU_WATTCH_DYNAMIC_POWER) {
                        window_total_pop_count_cycle += pop_count(rs->val_ra) + pop_count(rs->val_rb);
                        window_num_pop_count_cycle+=2;
                        }
                    }

                    /* one more inst issued */
                    n_issued++;
                }
            }

        } /* ! RSLINK_VALID */
        /* else, RUU entry was squashed */

        /* reclaim ready list entry, NOTE: this is done whether or not the
        instruction issued, since the instruction was once again reinserted
        into the ready queue if it did not issue, this ensures that the ready
        queue is always properly sorted */
        RSLINK_FREE(node);
    }

    /* put any instruction not issued back into the ready queue, go through
    normal channels to ensure instruction stay ordered correctly */
    for (; node; node = next_node)
    {
        next_node = node->next;

        /* still valid? */
        if (RSLINK_VALID(node))
        {
            struct RUU_station *rs = RSLINK_RS(node);

            /* node is now un-queued */
            rs->queued = FALSE;

            /* not issued, put operation back onto the ready list, we'll try to
            issue it again next cycle */
            readyq_enqueue(rs);
        }
        /* else, RUU entry was squashed */

        /* reclaim ready list entry, NOTE: this is done whether or not the
        instruction issued, since the instruction was once again reinserted
        into the ready queue if it did not issue, this ensures that the ready
        queue is always properly sorted */
        RSLINK_FREE(node);
    }
}


/*
 * routines for generating on-the-fly instruction traces with support
 * for control and data misspeculation modeling
 */

/* integer register file */
#define R_BMAP_SZ       (BITMAP_SIZE(MD_NUM_IREGS))
static BITMAP_TYPE(MD_NUM_IREGS, use_spec_R);
static md_gpr_t spec_regs_R;

/* floating point register file */
#define F_BMAP_SZ       (BITMAP_SIZE(MD_NUM_FREGS))
static BITMAP_TYPE(MD_NUM_FREGS, use_spec_F);
static md_fpr_t spec_regs_F;

/* miscellaneous registers */
#define C_BMAP_SZ       (BITMAP_SIZE(MD_NUM_CREGS))
static BITMAP_TYPE(MD_NUM_FREGS, use_spec_C);
static md_ctrl_t spec_regs_C;



/* speculative memory hash table size, NOTE: this must be a power-of-two */
#define STORE_HASH_SIZE   32

/* speculative memory hash table definition, accesses go through this hash
   table when accessing memory in speculative mode, the hash table flush the
   table when recovering from mispredicted branches */
struct spec_mem_ent {
  struct spec_mem_ent *next;    /* ptr to next hash table bucket */
  md_addr_t addr;     /* virtual address of spec state */
  unsigned int data[2];     /* spec buffer, up to 8 bytes */
};

/* speculative memory hash table */
static struct spec_mem_ent *store_htable[STORE_HASH_SIZE];

/* speculative memory hash table bucket free list */
static struct spec_mem_ent *bucket_free_list = NULL;


/* program counter */
static md_addr_t pred_PC;
static md_addr_t recover_PC;

/* fetch unit next fetch address */
static md_addr_t fetch_regs_PC;
static md_addr_t fetch_pred_PC;

/* IFETCH -> DISPATCH instruction queue definition */
struct fetch_rec {
  md_inst_t IR;       /* inst register */
  enum md_opcode op;
  md_addr_t regs_PC, pred_PC;   /* current PC, predicted next PC */
  struct bpred_update_t dir_update; /* bpred direction update info */
  int stack_recover_idx;    /* branch predictor RSB index */
# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    unsigned int ptrace_seq;    /* print trace sequence id */
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
};
static struct fetch_rec *fetch_data;  /* IFETCH -> DISPATCH inst queue */
static int fetch_num;     /* num entries in IF -> DIS queue */
static int fetch_tail, fetch_head;  /* head and tail pointers of queue */


/* recover instruction trace generator state to precise state state immediately
   before the first mis-predicted branch; this is accomplished by resetting
   all register value copied-on-write bitmasks are reset, and the speculative
   memory hash table is cleared */
static void
tracer_recover(void)
{
  int i;
  struct spec_mem_ent *ent, *ent_next;

  fetch_just_redirected = 1;

  /* better be in mis-speculative trace generation mode */
# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    if (!spec_mode)
      panic("cannot recover unless in speculative mode");
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

  /* reset to non-speculative trace generation mode */
  spec_mode = FALSE;

  /* reset copied-on-write register bitmasks back to non-speculative state */
  BITMAP_CLEAR_MAP(use_spec_R, R_BMAP_SZ);
  BITMAP_CLEAR_MAP(use_spec_F, F_BMAP_SZ);
  BITMAP_CLEAR_MAP(use_spec_C, C_BMAP_SZ);

  /* reset memory state back to non-speculative state */
  /* FIXME: could version stamps be used here?!?!? */
  for (i=0; i<STORE_HASH_SIZE; i++)
    {
      /* release all hash table buckets */
      for (ent=store_htable[i]; ent; ent=ent_next)
  {
    ent_next = ent->next;
    ent->next = bucket_free_list;
    bucket_free_list = ent;
  }
      store_htable[i] = NULL;
    }

# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
      /* if pipetracing, indicate squash of instructions in the inst fetch queue */
      if (ptrace_active) {
          while (fetch_num != 0) {
            /* squash the next instruction from the IFETCH -> DISPATCH queue */
            ptrace_endinst(fetch_data[fetch_head].ptrace_seq);

            /* consume instruction from IFETCH -> DISPATCH queue */
            fetch_head = (fetch_head+1) & (ruu_ifq_size - 1);
            fetch_num--;
        }
      }
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

  /* reset IFETCH state */
  fetch_num = 0;
  fetch_tail = fetch_head = 0;
  fetch_pred_PC = fetch_regs_PC = recover_PC;
}

/* initialize the speculative instruction state generator state */
static void
tracer_init(void)
{
  int i;

  /* initially in non-speculative mode */
  spec_mode = FALSE;

  /* register state is from non-speculative state buffers */
  BITMAP_CLEAR_MAP(use_spec_R, R_BMAP_SZ);
  BITMAP_CLEAR_MAP(use_spec_F, F_BMAP_SZ);
  BITMAP_CLEAR_MAP(use_spec_C, C_BMAP_SZ);

  /* memory state is from non-speculative memory pages */
  for (i=0; i<STORE_HASH_SIZE; i++)
    store_htable[i] = NULL;
}


/* speculative memory hash table address hash function */
#define HASH_ADDR(ADDR)             \
  ((((ADDR) >> 24)^((ADDR) >> 16)^((ADDR) >> 8)^(ADDR)) & (STORE_HASH_SIZE-1))

/* this functional provides a layer of mis-speculated state over the
   non-speculative memory state, when in mis-speculation trace generation mode,
   the simulator will call this function to access memory, instead of the
   non-speculative memory access interfaces defined in memory.h; when storage
   is written, an entry is allocated in the speculative memory hash table,
   future reads and writes while in mis-speculative trace generation mode will
   access this buffer instead of non-speculative memory state; when the trace
   generator transitions back to non-speculative trace generation mode,
   tracer_recover() clears this table, returns any access fault */
static enum md_fault_type
spec_mem_access(struct mem_t *mem,    /* memory space to access */
    enum mem_cmd cmd,   /* Read or Write access cmd */
    md_addr_t addr,     /* virtual address of access */
    void *p,      /* input/output buffer */
    int nbytes)     /* number of bytes to access */
{
  int i, index;
  struct spec_mem_ent *ent, *prev;

  /* FIXME: partially overlapping writes are not combined... */
  /* FIXME: partially overlapping reads are not handled correctly... */

  /* check alignments, even speculative this test should always pass */
  if ((nbytes & (nbytes-1)) != 0 || (addr & (nbytes-1)) != 0)
    {
      /* no can do, return zero result */
      for (i=0; i < nbytes; i++)
  ((char *)p)[i] = 0;

      return md_fault_none;
    }

  /* check permissions */
  if (!((addr >= ld_text_base && addr < (ld_text_base+ld_text_size)
   && cmd == Read)
  || MD_VALID_ADDR(addr)))
    {
      /* no can do, return zero result */
      for (i=0; i < nbytes; i++)
  ((char *)p)[i] = 0;

      return md_fault_none;
    }

  /* has this memory state been copied on mis-speculative write? */
  index = HASH_ADDR(addr);
  for (prev=NULL,ent=store_htable[index]; ent; prev=ent,ent=ent->next)
    {
      if (ent->addr == addr)
  {
    /* reorder chains to speed access into hash table */
    if (prev != NULL)
      {
        /* not at head of list, relink the hash table entry at front */
        prev->next = ent->next;
              ent->next = store_htable[index];
              store_htable[index] = ent;
      }
    break;
  }
    }

  /* no, if it is a write, allocate a hash table entry to hold the data */
  if (!ent && cmd == Write)
    {
      /* try to get an entry from the free list, if available */
      if (!bucket_free_list)
  {
    /* otherwise, call calloc() to get the needed storage */
    bucket_free_list = calloc(1, sizeof(struct spec_mem_ent));
    if (!bucket_free_list)
      fatal("out of virtual memory");
  }
      ent = bucket_free_list;
      bucket_free_list = bucket_free_list->next;

#     if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        if (!bugcompat_mode) {
#     endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
    /* insert into hash table */
    ent->next = store_htable[index];
    store_htable[index] = ent;
    ent->addr = addr;
    ent->data[0] = 0; ent->data[1] = 0;
#     if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
  }
#     endif (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    }

  /* handle the read or write to speculative or non-speculative storage */
  switch (nbytes)
    {
    case 1:
      if (cmd == Read)
  {
    if (ent)
      {
        /* read from mis-speculated state buffer */
        *((byte_t *)p) = *((byte_t *)(&ent->data[0]));
      }
    else
      {
        /* read from non-speculative memory state, don't allocate
           memory pages with speculative loads */
        *((byte_t *)p) = MEM_READ_BYTE(mem, addr);
      }
  }
      else
  {
    /* always write into mis-speculated state buffer */
    *((byte_t *)(&ent->data[0])) = *((byte_t *)p);
  }
      break;
    case 2:
      if (cmd == Read)
  {
    if (ent)
      {
        /* read from mis-speculated state buffer */
        *((half_t *)p) = *((half_t *)(&ent->data[0]));
      }
    else
      {
        /* read from non-speculative memory state, don't allocate
           memory pages with speculative loads */
        *((half_t *)p) = MEM_READ_HALF(mem, addr);
      }
  }
      else
  {
    /* always write into mis-speculated state buffer */
    *((half_t *)&ent->data[0]) = *((half_t *)p);
  }
      break;
    case 4:
      if (cmd == Read)
  {
    if (ent)
      {
        /* read from mis-speculated state buffer */
        *((word_t *)p) = *((word_t *)&ent->data[0]);
      }
    else
      {
        /* read from non-speculative memory state, don't allocate
           memory pages with speculative loads */
        *((word_t *)p) = MEM_READ_WORD(mem, addr);
      }
  }
      else
  {
    /* always write into mis-speculated state buffer */
    *((word_t *)&ent->data[0]) = *((word_t *)p);
  }
      break;
    case 8:
      if (cmd == Read)
  {
    if (ent)
      {
        /* read from mis-speculated state buffer */
        *((word_t *)p) = *((word_t *)&ent->data[0]);
        *(((word_t *)p)+1) = *((word_t *)&ent->data[1]);
      }
    else
      {
        /* read from non-speculative memory state, don't allocate
           memory pages with speculative loads */
        *((word_t *)p) = MEM_READ_WORD(mem, addr);
        *(((word_t *)p)+1) =
    MEM_READ_WORD(mem, addr + sizeof(word_t));
      }
  }
      else
  {
    /* always write into mis-speculated state buffer */
    *((word_t *)&ent->data[0]) = *((word_t *)p);
    *((word_t *)&ent->data[1]) = *(((word_t *)p)+1);
  }
      break;
    default:
      panic("access size not supported in mis-speculative mode");
    }

  return md_fault_none;
}

/*
 *  RUU_DISPATCH() - decode instructions and allocate RUU and LSQ resources
 */

/* link RS onto the output chain number of whichever operation will next
   create the architected register value IDEP_NAME */
static INLINE void
ruu_link_idep(struct RUU_station *rs,   /* rs station to link */
        int idep_num,     /* input dependence number */
        int idep_name)      /* input register name */
{
  struct CV_link head;
  struct RS_link *link;

  /* any dependence? */
  if (idep_name == NA)
    {
      /* no input dependence for this input slot, mark operand as ready */
      rs->idep_ready[idep_num] = TRUE;
      return;
    }

  /* locate creator of operand */
  head = CREATE_VECTOR(idep_name);

  /* any creator? */
  if (!head.rs)
    {
      /* no active creator, use value available in architected reg file,
         indicate the operand is ready for use */
      rs->idep_ready[idep_num] = TRUE;

      if (CMU_WATTCH_ENABLED) {
          /* Wattch -- regfile access (value from arch regfile) */
          regfile_access++;
          if (CMU_WATTCH_DYNAMIC_POWER) {
            if(idep_num == 0)
             regfile_total_pop_count_cycle += pop_count(rs->val_ra);
            else
             regfile_total_pop_count_cycle += pop_count(rs->val_rb);
            regfile_num_pop_count_cycle++;
          }
      }
      return;
    }
  /* else, creator operation will make this value sometime in the future */

  /* indicate value will be created sometime in the future, i.e., operand
     is not yet ready for use */
  rs->idep_ready[idep_num] = FALSE;

  /* link onto creator's output list of dependant operand */
  RSLINK_NEW(link, rs); link->x.opnum = idep_num;
  link->next = head.rs->odep_list[head.odep_num];
  head.rs->odep_list[head.odep_num] = link;
}

/* make RS the creator of architected register ODEP_NAME */
static INLINE void
ruu_install_odep(struct RUU_station *rs,  /* creating RUU station */
     int odep_num,      /* output operand number */
     int odep_name)     /* output register name */
{
  struct CV_link cv;

  /* any dependence? */
  if (odep_name == NA)
    {
      /* no value created */
      rs->onames[odep_num] = NA;
      return;
    }
  /* else, create a RS_NULL terminated output chain in create vector */

  /* record output name, used to update create vector at completion */
  rs->onames[odep_num] = odep_name;

  /* initialize output chain to empty list */
  rs->odep_list[odep_num] = NULL;

  /* indicate this operation is latest creator of ODEP_NAME */
  CVLINK_INIT(cv, rs, odep_num);
  SET_CREATE_VECTOR(odep_name, cv);
}


/*
 * configure the instruction decode engine
 */

#define DNA     (0)

#if defined(TARGET_PISA)

/* general register dependence decoders */
#define DGPR(N)     (N)
#define DGPR_D(N)   ((N) &~1)

/* floating point register dependence decoders */
#define DFPR_L(N)   (((N)+32)&~1)
#define DFPR_F(N)   (((N)+32)&~1)
#define DFPR_D(N)   (((N)+32)&~1)

/* miscellaneous register dependence decoders */
#define DHI     (0+32+32)
#define DLO     (1+32+32)
#define DFCC      (2+32+32)
#define DTMP      (3+32+32)

#elif defined(TARGET_ALPHA)

/* general register dependence decoders, $r31 maps to DNA (0) */
#define DGPR(N)     (31 - (N)) /* was: (((N) == 31) ? DNA : (N)) */

/* floating point register dependence decoders */
#define DFPR(N)     (((N) == 31) ? DNA : ((N)+32))

/* miscellaneous register dependence decoders */
#define DFPCR     (0+32+32)
#define DUNIQ     (1+32+32)
#define DTMP      (2+32+32)

#else
#error No ISA target defined...
#endif


/*
 * configure the execution engine
 */

/* next program counter */
#define SET_NPC(EXPR)           (regs.regs_NPC = (EXPR))

/* target program counter */
#undef  SET_TPC
#define SET_TPC(EXPR)   (target_PC = (EXPR))

/* current program counter */
#define CPC                     (regs.regs_PC)
#define SET_CPC(EXPR)           (regs.regs_PC = (EXPR))

/* general purpose register accessors, NOTE: speculative copy on write storage
   provided for fast recovery during wrong path execute (see tracer_recover()
   for details on this process */
#define GPR(N)                  (BITMAP_SET_P(use_spec_R, R_BMAP_SZ, (N))\
         ? spec_regs_R[N]                       \
         : regs.regs_R[N])
#define SET_GPR(N,EXPR)         (spec_mode        \
         ? ((spec_regs_R[N] = (EXPR)),    \
            BITMAP_SET(use_spec_R, R_BMAP_SZ, (N)),\
            spec_regs_R[N])     \
         : (regs.regs_R[N] = (EXPR)))

#if defined(TARGET_PISA)

/* floating point register accessors, NOTE: speculative copy on write storage
   provided for fast recovery during wrong path execute (see tracer_recover()
   for details on this process */
#define FPR_L(N)                (BITMAP_SET_P(use_spec_F, F_BMAP_SZ, ((N)&~1))\
         ? spec_regs_F.l[(N)]                   \
         : regs.regs_F.l[(N)])
#define SET_FPR_L(N,EXPR)       (spec_mode        \
         ? ((spec_regs_F.l[(N)] = (EXPR)),  \
            BITMAP_SET(use_spec_F,F_BMAP_SZ,((N)&~1)),\
            spec_regs_F.l[(N)])     \
         : (regs.regs_F.l[(N)] = (EXPR)))
#define FPR_F(N)                (BITMAP_SET_P(use_spec_F, F_BMAP_SZ, ((N)&~1))\
         ? spec_regs_F.f[(N)]                   \
         : regs.regs_F.f[(N)])
#define SET_FPR_F(N,EXPR)       (spec_mode        \
         ? ((spec_regs_F.f[(N)] = (EXPR)),  \
            BITMAP_SET(use_spec_F,F_BMAP_SZ,((N)&~1)),\
            spec_regs_F.f[(N)])     \
         : (regs.regs_F.f[(N)] = (EXPR)))
#define FPR_D(N)                (BITMAP_SET_P(use_spec_F, F_BMAP_SZ, ((N)&~1))\
         ? spec_regs_F.d[(N) >> 1]              \
         : regs.regs_F.d[(N) >> 1])
#define SET_FPR_D(N,EXPR)       (spec_mode        \
         ? ((spec_regs_F.d[(N) >> 1] = (EXPR)), \
            BITMAP_SET(use_spec_F,F_BMAP_SZ,((N)&~1)),\
            spec_regs_F.d[(N) >> 1])    \
         : (regs.regs_F.d[(N) >> 1] = (EXPR)))

/* miscellanous register accessors, NOTE: speculative copy on write storage
   provided for fast recovery during wrong path execute (see tracer_recover()
   for details on this process */
#define HI      (BITMAP_SET_P(use_spec_C, C_BMAP_SZ, /*hi*/0)\
         ? spec_regs_C.hi     \
         : regs.regs_C.hi)
#define SET_HI(EXPR)    (spec_mode        \
         ? ((spec_regs_C.hi = (EXPR)),    \
            BITMAP_SET(use_spec_C, C_BMAP_SZ,/*hi*/0),\
            spec_regs_C.hi)     \
         : (regs.regs_C.hi = (EXPR)))
#define LO      (BITMAP_SET_P(use_spec_C, C_BMAP_SZ, /*lo*/1)\
         ? spec_regs_C.lo     \
         : regs.regs_C.lo)
#define SET_LO(EXPR)    (spec_mode        \
         ? ((spec_regs_C.lo = (EXPR)),    \
            BITMAP_SET(use_spec_C, C_BMAP_SZ,/*lo*/1),\
            spec_regs_C.lo)     \
         : (regs.regs_C.lo = (EXPR)))
#define FCC     (BITMAP_SET_P(use_spec_C, C_BMAP_SZ,/*fcc*/2)\
         ? spec_regs_C.fcc      \
         : regs.regs_C.fcc)
#define SET_FCC(EXPR)   (spec_mode        \
         ? ((spec_regs_C.fcc = (EXPR)),   \
            BITMAP_SET(use_spec_C,C_BMAP_SZ,/*fcc*/2),\
            spec_regs_C.fcc)      \
         : (regs.regs_C.fcc = (EXPR)))

#elif defined(TARGET_ALPHA)

/* floating point register accessors, NOTE: speculative copy on write storage
   provided for fast recovery during wrong path execute (see tracer_recover()
   for details on this process */
#define FPR_Q(N)    (BITMAP_SET_P(use_spec_F, F_BMAP_SZ, (N))\
         ? spec_regs_F.q[(N)]                   \
         : regs.regs_F.q[(N)])
#define SET_FPR_Q(N,EXPR) (spec_mode        \
         ? ((spec_regs_F.q[(N)] = (EXPR)),  \
            BITMAP_SET(use_spec_F,F_BMAP_SZ, (N)),\
            spec_regs_F.q[(N)])     \
         : (regs.regs_F.q[(N)] = (EXPR)))
#define FPR(N)      (BITMAP_SET_P(use_spec_F, F_BMAP_SZ, (N))\
         ? spec_regs_F.d[(N)]     \
         : regs.regs_F.d[(N)])
#define SET_FPR(N,EXPR)   (spec_mode        \
         ? ((spec_regs_F.d[(N)] = (EXPR)),  \
            BITMAP_SET(use_spec_F,F_BMAP_SZ, (N)),\
            spec_regs_F.d[(N)])     \
         : (regs.regs_F.d[(N)] = (EXPR)))

/* miscellanous register accessors, NOTE: speculative copy on write storage
   provided for fast recovery during wrong path execute (see tracer_recover()
   for details on this process */
#define FPCR      (BITMAP_SET_P(use_spec_C, C_BMAP_SZ,/*fpcr*/0)\
         ? spec_regs_C.fpcr     \
         : regs.regs_C.fpcr)
#define SET_FPCR(EXPR)    (spec_mode        \
         ? ((spec_regs_C.fpcr = (EXPR)),  \
           BITMAP_SET(use_spec_C,C_BMAP_SZ,/*fpcr*/0),\
            spec_regs_C.fpcr)     \
         : (regs.regs_C.fpcr = (EXPR)))
#define UNIQ      (BITMAP_SET_P(use_spec_C, C_BMAP_SZ,/*uniq*/1)\
         ? spec_regs_C.uniq     \
         : regs.regs_C.uniq)
#define SET_UNIQ(EXPR)    (spec_mode        \
         ? ((spec_regs_C.uniq = (EXPR)),  \
           BITMAP_SET(use_spec_C,C_BMAP_SZ,/*uniq*/1),\
            spec_regs_C.uniq)     \
         : (regs.regs_C.uniq = (EXPR)))
#define FCC     (BITMAP_SET_P(use_spec_C, C_BMAP_SZ,/*fcc*/2)\
         ? spec_regs_C.fcc      \
         : regs.regs_C.fcc)
#define SET_FCC(EXPR)   (spec_mode        \
         ? ((spec_regs_C.fcc = (EXPR)),   \
            BITMAP_SET(use_spec_C,C_BMAP_SZ,/*fcc*/1),\
            spec_regs_C.fcc)      \
         : (regs.regs_C.fcc = (EXPR)))

#else
#error No ISA target defined...
#endif

/* precise architected memory state accessor macros, NOTE: speculative copy on
   write storage provided for fast recovery during wrong path execute (see
   tracer_recover() for details on this process */
#define __READ_SPECMEM(SRC, SRC_V, FAULT)       \
  (addr = (SRC),              \
   (spec_mode               \
    ? ((FAULT) = spec_mem_access(mem, Read, addr, &SRC_V, sizeof(SRC_V)))\
    : ((FAULT) = mem_access(mem, Read, addr, &SRC_V, sizeof(SRC_V)))),  \
   SRC_V)

#define READ_BYTE(SRC, FAULT)           \
  __READ_SPECMEM((SRC), temp_byte, (FAULT))
#define READ_HALF(SRC, FAULT)           \
  MD_SWAPH(__READ_SPECMEM((SRC), temp_half, (FAULT)))
#define READ_WORD(SRC, FAULT)           \
  MD_SWAPW(__READ_SPECMEM((SRC), temp_word, (FAULT)))
#ifdef HOST_HAS_QWORD
#define READ_QWORD(SRC, FAULT)            \
  MD_SWAPQ(__READ_SPECMEM((SRC), temp_qword, (FAULT)))
#endif /* HOST_HAS_QWORD */


#define __WRITE_SPECMEM(SRC, DST, DST_V, FAULT)       \
  (DST_V = (SRC), addr = (DST),           \
   (spec_mode               \
    ? ((FAULT) = spec_mem_access(mem, Write, addr, &DST_V, sizeof(DST_V)))\
    : ((FAULT) = mem_access(mem, Write, addr, &DST_V, sizeof(DST_V)))))

#define WRITE_BYTE(SRC, DST, FAULT)         \
  __WRITE_SPECMEM((SRC), (DST), temp_byte, (FAULT))
#define WRITE_HALF(SRC, DST, FAULT)         \
  __WRITE_SPECMEM(MD_SWAPH(SRC), (DST), temp_half, (FAULT))
#define WRITE_WORD(SRC, DST, FAULT)         \
  __WRITE_SPECMEM(MD_SWAPW(SRC), (DST), temp_word, (FAULT))
#ifdef HOST_HAS_QWORD
#define WRITE_QWORD(SRC, DST, FAULT)          \
  __WRITE_SPECMEM(MD_SWAPQ(SRC), (DST), temp_qword, (FAULT))
#endif /* HOST_HAS_QWORD */

/* system call handler macro */
#define SYSCALL(INST)             \
  (/* only execute system calls in non-speculative mode */    \
   (spec_mode ? panic("speculative syscall") : (void) 0),   \
   ckpt_replay_syscall(sim_pop_insn, &regs, mem_access, mem, INST))


/* the last operation that ruu_dispatch() attempted to dispatch, for
   implementing in-order issue */
static struct RS_link last_op = RSLINK_NULL_DATA;

/* dispatch instructions from the IFETCH -> DISPATCH queue: instructions are
   first decoded, then they allocated RUU (and LSQ for load/stores) resources
   and input and output dependence chains are updated accordingly */
static void
ruu_dispatch(void)
{
  int n_dispatched;     /* total insts dispatched */
  md_inst_t inst;     /* actual instruction bits */
  enum md_opcode op;      /* decoded opcode enum */
  int out1, out2, in1, in2, in3;  /* output/input register names */
  md_addr_t target_PC = 0;      /* actual next/target PC address */
  md_addr_t addr;     /* effective address, if load/store */
  struct RUU_station *rs;   /* RUU station being allocated */
  struct RUU_station *lsq;    /* LSQ station for ld/st's */
  struct bpred_update_t *dir_update_ptr;/* branch predictor dir update ptr */
  int stack_recover_idx;    /* bpred retstack recovery index */
# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    int i;
    unsigned int pseq;      /* pipetrace sequence number */
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
  int is_write;       /* store? */
  int made_check;     /* used to ensure DLite entry */
  int br_taken, br_pred_taken;    /* if br, taken?  predicted taken? */
  int fetch_redirected = FALSE;
  byte_t temp_byte = 0;     /* temp variable for spec mem access */
  half_t temp_half = 0;     /* " ditto " */
  word_t temp_word = 0;     /* " ditto " */
# ifdef HOST_HAS_QWORD
  qword_t temp_qword = 0;   /* " ditto " */
# endif /* HOST_HAS_QWORD */
  enum md_fault_type fault;

  /* Wattch:  Added for pop count generation (AFs) */
  quad_t val_ra, val_rb, val_rc, val_ra_result;

  made_check = FALSE;
  n_dispatched = 0;
  while (/* instruction decode B/W left? */
   n_dispatched < ruu_decode_width
   /* RUU and LSQ not full? */
   && RUU_num < RUU_size && LSQ_num < LSQ_size
   /* insts still available from fetch unit? */
   && fetch_num != 0
   /* on an acceptable trace path */
   && (ruu_include_spec || !spec_mode))
    {
      /* if issuing in-order, block until last op issues if inorder issue */
      if (ruu_inorder_issue
    && (last_op.rs && RSLINK_VALID(&last_op)
        && !OPERANDS_READY(last_op.rs)))
  {
    /* stall until last operation is ready to issue */
    break;
  }

      /* get the next instruction from the IFETCH -> DISPATCH queue */
      inst = fetch_data[fetch_head].IR;
      op = fetch_data[fetch_head].op;
      regs.regs_PC = fetch_data[fetch_head].regs_PC;
      pred_PC = fetch_data[fetch_head].pred_PC;
      dir_update_ptr = &(fetch_data[fetch_head].dir_update);
      stack_recover_idx = fetch_data[fetch_head].stack_recover_idx;
#     if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        pseq = fetch_data[fetch_head].ptrace_seq;
#     endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )


      /* decode the inst */
      //MD_SET_OPCODE(op, inst);

      /* compute default next PC */
      regs.regs_NPC = regs.regs_PC + sizeof(md_inst_t);

      /* drain RUU for TRAPs and system calls */
      if (MD_OP_FLAGS(op) & F_TRAP)
  {
    if (RUU_num != 0)
      break;

    /* else, syscall is only instruction in the machine, at this
       point we should not be in (mis-)speculative mode */
#         if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
      if (spec_mode)
        panic("drained and speculative");
#         endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
  }

      /* maintain $r0 semantics (in spec and non-spec space) */
      regs.regs_R[MD_REG_ZERO] = 0; spec_regs_R[MD_REG_ZERO] = 0;
#ifdef TARGET_ALPHA
      regs.regs_F.d[MD_REG_ZERO] = 0.0; spec_regs_F.d[MD_REG_ZERO] = 0.0;
#endif /* TARGET_ALPHA */

      if (!spec_mode)
  {
    /* one more non-speculative instruction executed */
      if (simulator_state == MEASURING) {
      sim_meas_insn++;
    }
    sim_detail_insn++;
    sim_pop_insn++;
  }

      /* default effective address (none) and access */
      addr = 0; is_write = FALSE;

      if (CMU_WATTCH_ENABLED) {
          /* Wattch: Get values of source operands */
#if defined(TARGET_PISA)
         val_ra = GPR(RS);
         val_rb = GPR(RT);
#elif defined(TARGET_ALPHA)
         val_ra = GPR(RA);
         val_rb = GPR(RB);
#endif
      }

      /* set default fault - none */
      fault = md_fault_none;

      /* more decoding and execution */
      switch (op)
  {
#define DEFINST(OP,MSK,NAME,OPFORM,RES,CLASS,O1,O2,I1,I2,I3)    \
  case OP:              \
    /* compute output/input dependencies to out1-2 and in1-3 */ \
    out1 = O1; out2 = O2;           \
    in1 = I1; in2 = I2; in3 = I3;         \
    /* execute the instruction */         \
    SYMCAT(OP,_IMPL);           \
    break;
#define DEFLINK(OP,MSK,NAME,MASK,SHIFT)         \
  case OP:              \
    /* could speculatively decode a bogus inst, convert to NOP */ \
    op = MD_NOP_OP;           \
    /* compute output/input dependencies to out1-2 and in1-3 */ \
    out1 = NA; out2 = NA;           \
    in1 = NA; in2 = NA; in3 = NA;         \
    /* no EXPR */             \
    break;
#define CONNECT(OP) /* nada... */
    /* the following macro wraps the instruction fault declaration macro
       with a test to see if the trace generator is in non-speculative
       mode, if so the instruction fault is declared, otherwise, the
       error is shunted because instruction faults need to be masked on
       the mis-speculated instruction paths */
#define DECLARE_FAULT(FAULT)            \
    {               \
      if (!spec_mode)           \
        fault = (FAULT);            \
      /* else, spec fault, ignore it, always terminate exec... */ \
      break;              \
    }
#include "machine.def"
  default:
    /* can speculatively decode a bogus inst, convert to a NOP */
    op = MD_NOP_OP;
    /* compute output/input dependencies to out1-2 and in1-3 */ \
    out1 = NA; out2 = NA;
    in1 = NA; in2 = NA; in3 = NA;
    /* no EXPR */
  }
      /* operation sets next PC */

      /* print retirement trace if in verbose mode */
      if (!spec_mode && verbose)
        {
          myfprintf(stderr, "++ %10n [xor: 0x%08x] {%d} @ 0x%08p: ",
                    sim_pop_insn, md_xor_regs(&regs),
                    inst_seq+1, regs.regs_PC);
          md_print_insn(inst, regs.regs_PC, stderr);
          fprintf(stderr, "\n");
          /* fflush(stderr); */
        }

      if (fault != md_fault_none)
  fatal("non-speculative fault (%d) detected @ 0x%08p",
        fault, regs.regs_PC);

      if (CMU_WATTCH_ENABLED) {
          /* Wattch: Get result values */
#if defined(TARGET_PISA)
          val_rc = GPR(RD);
          val_ra_result = GPR(RS);
#elif defined(TARGET_ALPHA)
          val_rc = GPR(RC);
          val_ra_result = GPR(RA);
#endif
      }

      /* update memory access stats */
      if (MD_OP_FLAGS(op) & F_MEM)
  {
    sim_total_refs++;
    if (!spec_mode)
      sim_num_refs++;

    if (MD_OP_FLAGS(op) & F_STORE)
      is_write = TRUE;
    else
      {
        sim_total_loads++;
        if (!spec_mode)
    sim_num_loads++;
      }
  }

      br_taken = (regs.regs_NPC != (regs.regs_PC + sizeof(md_inst_t)));
      br_pred_taken = (pred_PC != (regs.regs_PC + sizeof(md_inst_t)));

      if ((pred_PC != regs.regs_NPC && pred_perfect)
    || ((MD_OP_FLAGS(op) & (F_CTRL|F_DIRJMP)) == (F_CTRL|F_DIRJMP)
        && target_PC != pred_PC && br_pred_taken))
  {
    /* Either 1) we're simulating perfect prediction and are in a
             mis-predict state and need to patch up, or 2) We're not simulating
             perfect prediction, we've predicted the branch taken, but our
             predicted target doesn't match the computed target (i.e.,
             mis-fetch).  Just update the PC values and do a fetch squash.
             This is just like calling fetch_squash() except we pre-anticipate
             the updates to the fetch values at the end of this function.  If
             case #2, also charge a mispredict penalty for redirecting fetch */
    fetch_pred_PC = fetch_regs_PC = regs.regs_NPC;
    /* was: if (pred_perfect) */
    if (pred_perfect)
      pred_PC = regs.regs_NPC;

    fetch_head = (ruu_ifq_size-1);
    fetch_num = 1;
    fetch_tail = 0;

    if (!pred_perfect)
      ruu_fetch_issue_delay = ruu_branch_penalty;

    fetch_redirected = TRUE;
  }

      /* is this a NOP */
      if (op != MD_NOP_OP)
  {
    /* for load/stores:
         idep #0     - store operand (value that is store'ed)
         idep #1, #2 - eff addr computation inputs (addr of access)

       resulting RUU/LSQ operation pair:
         RUU (effective address computation operation):
     idep #0, #1 - eff addr computation inputs (addr of access)
         LSQ (memory access operation):
     idep #0     - operand input (value that is store'd)
     idep #1     - eff addr computation result (from RUU op)

       effective address computation is transfered via the reserved
       name DTMP
     */

    /* Wattch -- Dispatch + RAT lookup stage */
    CMU_WATTCH(rename_access++);

    /* fill in RUU reservation station */
    rs = &RUU[RUU_tail];
#         if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
            rs->slip = sim_cycle - 1;
#         endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
    rs->IR = inst;
    rs->op = op;
    rs->PC = regs.regs_PC;
    rs->next_PC = regs.regs_NPC; rs->pred_PC = pred_PC;
    rs->in_LSQ = FALSE;
    rs->ea_comp = FALSE;
    rs->recover_inst = FALSE;
          rs->dir_update = *dir_update_ptr;
    rs->stack_recover_idx = stack_recover_idx;
    rs->spec_mode = spec_mode;
    rs->addr = 0;
    /* rs->tag is already set */
    rs->seq = ++inst_seq;
    rs->queued = rs->issued = rs->completed = FALSE;

#         if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
      rs->ptrace_seq = pseq;
#         endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

    /* mshr: mem_ready set to 0 */
    rs->mem_ready = 0;

    rs->tlb_clear_time = 0;

      if (CMU_WATTCH_ENABLED) {
        /* Wattch: Maintain values through core for AFs*/
        rs->val_ra = val_ra;
        rs->val_rb = val_rb;
        rs->val_rc = val_rc;
        rs->val_ra_result = val_ra_result;
      }

    /* split ld/st's into two operations: eff addr comp + mem access */
    if (MD_OP_FLAGS(op) & F_MEM)
      {
        /* convert RUU operation from ld/st to an add (eff addr comp) */
        rs->op = MD_AGEN_OP;
        rs->ea_comp = TRUE;

        /* fill in LSQ reservation station */
        lsq = &LSQ[LSQ_tail];
#             if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
                lsq->slip = sim_cycle - 1;
#             endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
        lsq->IR = inst;
        lsq->op = op;
        lsq->PC = regs.regs_PC;
        lsq->next_PC = regs.regs_NPC; lsq->pred_PC = pred_PC;
        lsq->in_LSQ = TRUE;
        lsq->ea_comp = FALSE;
        lsq->recover_inst = FALSE;
        lsq->dir_update.pdir1 = lsq->dir_update.pdir2 = NULL;
        lsq->dir_update.pmeta = NULL;
        lsq->stack_recover_idx = 0;
        lsq->spec_mode = spec_mode;
        lsq->addr = addr;
        /* lsq->tag is already set */
        lsq->seq = ++inst_seq;
        lsq->queued = lsq->issued = lsq->completed = FALSE;
#             if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
          lsq->ptrace_seq = ptrace_seq++;
#             endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

        /* mshr: mem_ready set to 0 */
        lsq->mem_ready = 0;

            lsq->tlb_clear_time = 0;

              if (CMU_WATTCH_ENABLED) {
                /* Wattch: Maintain values through core for AFs*/
                lsq->val_ra = val_ra;
                lsq->val_rb = val_rb;
                lsq->val_rc = val_rc;
                lsq->val_ra_result = val_ra_result;
              }

#             if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
                /* pipetrace this uop */
                ptrace_newuop(lsq->ptrace_seq, "internal ld/st", lsq->PC, 0);
                ptrace_newstage(lsq->ptrace_seq, PST_DISPATCH, 0);
#             endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

        /* link eff addr computation onto operand's output chains */
        ruu_link_idep(rs, /* idep_ready[] index */0, NA);
        ruu_link_idep(rs, /* idep_ready[] index */1, in2);
        ruu_link_idep(rs, /* idep_ready[] index */2, in3);

        /* install output after inputs to prevent self reference */
        ruu_install_odep(rs, /* odep_list[] index */0, DTMP);
        ruu_install_odep(rs, /* odep_list[] index */1, NA);

        /* link memory access onto output chain of eff addr operation */
        ruu_link_idep(lsq,
          /* idep_ready[] index */STORE_OP_INDEX/* 0 */,
          in1);
        ruu_link_idep(lsq,
          /* idep_ready[] index */STORE_ADDR_INDEX/* 1 */,
          DTMP);
        ruu_link_idep(lsq, /* idep_ready[] index */2, NA);

        /* install output after inputs to prevent self reference */
        ruu_install_odep(lsq, /* odep_list[] index */0, out1);
        ruu_install_odep(lsq, /* odep_list[] index */1, out2);

        /* install operation in the RUU and LSQ */
        n_dispatched++;
        RUU_tail = (RUU_tail + 1) & RUU_size_mask;
        RUU_num++;
        LSQ_tail = (LSQ_tail + 1) & LSQ_size_mask;
        LSQ_num++;

        if (OPERANDS_READY(rs))
    {
          if (CMU_WATTCH_ENABLED) {
            /* Wattch -- both operands ready, 2 window write accesses */
          /* Wattch -- FIXME: currently being read from arch.
             regfile (in ruu_link_idep) and written to window here.
             should these values be read from arch. regfile or
             another window entry? */
          window_access++;
          window_access++;
          window_preg_access++;
          window_preg_access++;

             if (CMU_WATTCH_DYNAMIC_POWER) {
              regfile_total_pop_count_cycle += pop_count(rs->val_ra);
              regfile_total_pop_count_cycle += pop_count(rs->val_rb);
            regfile_num_pop_count_cycle+=2;
         }
          }

      /* eff addr computation ready, queue it on ready list */
      readyq_enqueue(rs);
    } else if ( CMU_WATTCH_ENABLED && ONE_OPERANDS_READY(rs)) {
      /* Wattch -- one operand ready, 1 window write accesses */
      window_access++;
      window_preg_access++;
          if (CMU_WATTCH_DYNAMIC_POWER) {
        if(rs->idep_ready[0])
            regfile_total_pop_count_cycle += pop_count(rs->val_ra);
        else
            regfile_total_pop_count_cycle += pop_count(rs->val_rb);
        regfile_num_pop_count_cycle++;
      }

    }

        /* issue may continue when the load/store is issued */
        RSLINK_INIT(last_op, lsq);

        /* issue stores only, loads are issued by lsq_refresh() */
        if (((MD_OP_FLAGS(op) & (F_MEM|F_STORE)) == (F_MEM|F_STORE))
      && OPERANDS_READY(lsq))
    {
      /* Wattch -- store operand ready, 1 LSQ access */
      CMU_WATTCH(lsq_store_data_access++);
      /* panic("store immediately ready"); */
      /* put operation on ready list, ruu_issue() issue it later */
      readyq_enqueue(lsq);
    }
      }
    else /* !(MD_OP_FLAGS(op) & F_MEM) */
      {
        /* link onto producing operation */
        ruu_link_idep(rs, /* idep_ready[] index */0, in1);
        ruu_link_idep(rs, /* idep_ready[] index */1, in2);
        ruu_link_idep(rs, /* idep_ready[] index */2, in3);

        /* install output after inputs to prevent self reference */
        ruu_install_odep(rs, /* odep_list[] index */0, out1);
        ruu_install_odep(rs, /* odep_list[] index */1, out2);

        /* install operation in the RUU */
        n_dispatched++;
        RUU_tail = (RUU_tail + 1) & RUU_size_mask;
        RUU_num++;

        /* issue op if all its reg operands are ready (no mem input) */
        if (OPERANDS_READY(rs))
    {
      if (CMU_WATTCH_ENABLED) {
          /* Wattch -- window access,
             both operands ready, write them to window,
          */
          window_access++;
          window_access++;
          window_preg_access++;
          window_preg_access++;

              if (CMU_WATTCH_DYNAMIC_POWER) {
            regfile_total_pop_count_cycle += pop_count(rs->val_ra);
            regfile_total_pop_count_cycle += pop_count(rs->val_rb);
            regfile_num_pop_count_cycle+=2;
          }
          }
      /* put operation on ready list, ruu_issue() issue it later */
      readyq_enqueue(rs);
      /* issue may continue */
      last_op = RSLINK_NULL;
    } else if ( CMU_WATTCH_ENABLED && ONE_OPERANDS_READY(rs)) {
      /* Wattch -- one operand ready, 1 window write accesses */
      window_access++;
      window_preg_access++;
          if (CMU_WATTCH_DYNAMIC_POWER) {
        if(rs->idep_ready[0])
            regfile_total_pop_count_cycle += pop_count(rs->val_ra);
        else
            regfile_total_pop_count_cycle += pop_count(rs->val_rb);
        regfile_num_pop_count_cycle++;
          }
        } else {
      /* could not issue this inst, stall issue until we can */
      RSLINK_INIT(last_op, rs);
    }
      }
  }
      else
  {
    /* this is a NOP, no need to update RUU/LSQ state */
    rs = NULL;
  }

      /* one more instruction executed, speculative or otherwise */
      sim_total_insn++;
      if (MD_OP_FLAGS(op) & F_CTRL)
  sim_total_branches++;

      if (!spec_mode)
  {

    /* if this is a branching instruction update BTB, i.e., only
       non-speculative state is committed into the BTB */
    if (MD_OP_FLAGS(op) & F_CTRL)
      {
        sim_num_branches++;
        if (pred && bpred_spec_update == spec_ID)
    {

          /* Wattch -- bpred access */
        CMU_WATTCH(bpred_access++);

      bpred_update(pred,
             /* branch address */regs.regs_PC,
             /* actual target address */regs.regs_NPC,
             /* taken? */regs.regs_NPC != (regs.regs_PC +
                   sizeof(md_inst_t)),
             /* pred taken? */pred_PC != (regs.regs_PC +
              sizeof(md_inst_t)),
             /* correct pred? */pred_PC == regs.regs_NPC,
             /* opcode */op,
             /* predictor update ptr */&rs->dir_update);
    }
      }

    /* is the trace generator trasitioning into mis-speculation mode? */
    if (pred_PC != regs.regs_NPC && !fetch_redirected)
      {
        /* entering mis-speculation mode, indicate this and save PC */
        spec_mode = TRUE;
        rs->recover_inst = TRUE;
        recover_PC = regs.regs_NPC;
      }
  }

#     if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
          /* entered decode/allocate stage, indicate in pipe trace */
          ptrace_newstage(pseq, PST_DISPATCH,
                      (pred_PC != regs.regs_NPC) ? PEV_MPOCCURED : 0);
          if (op == MD_NOP_OP) {
            /* end of the line */
            ptrace_endinst(pseq);
          }

        /* update any stats tracked by PC */
        for (i=0; i<pcstat_nelt; i++) {
    counter_t newval;
    int delta;

    /* check if any tracked stats changed */
    newval = STATVAL(pcstat_stats[i]);
    delta = newval - pcstat_lastvals[i];
    if (delta != 0)
      {
        stat_add_samples(pcstat_sdists[i], regs.regs_PC, delta);
        pcstat_lastvals[i] = newval;
      }
  }
#     endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

      /* consume instruction from IFETCH -> DISPATCH queue */
      fetch_head = (fetch_head+1) & (ruu_ifq_size - 1);
      fetch_num--;

#     if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        /* check for DLite debugger entry condition */
        made_check = TRUE;
        if (dlite_check_break(pred_PC,
          is_write ? ACCESS_WRITE : ACCESS_READ,
          addr, sim_meas_insn, sim_cycle))
    dlite_main(regs.regs_PC, pred_PC, sim_cycle, &regs, mem);
#     endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
    }

# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
    /* need to enter DLite at least once per cycle */
    if (!made_check) {
      if (dlite_check_break(/* no next PC */0,
          is_write ? ACCESS_WRITE : ACCESS_READ,
          addr, sim_meas_insn, sim_cycle))
  dlite_main(regs.regs_PC, /* no next PC */0, sim_cycle, &regs, mem);
    }
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

}


/*
 *  RUU_FETCH() - instruction fetch pipeline stage(s)
 */

/* initialize the instruction fetch pipeline stage */
static void
fetch_init(void)
{
  /* allocate the IFETCH -> DISPATCH instruction queue */
  fetch_data =
    (struct fetch_rec *)calloc(ruu_ifq_size, sizeof(struct fetch_rec));
  if (!fetch_data)
    fatal("out of virtual memory");

  fetch_num = 0;
  fetch_tail = fetch_head = 0;
  IFQ_count = 0;
  IFQ_fcount = 0;
}


static int last_inst_missed = FALSE;
static int last_inst_tmissed = FALSE;

/* fetch up as many instruction as one branch prediction and one cache line
   acess will support without overflowing the IFETCH -> DISPATCH QUEUE */
static void
ruu_fetch(void)
{
  int i, lat, tlb_lat, done = FALSE;
  md_inst_t inst;
  int stack_recover_idx;
  int branch_cnt;

  for (i=0, branch_cnt=0;
       /* fetch up to as many instruction as the DISPATCH stage can decode */
       i < fetch_width
       /* fetch until IFETCH -> DISPATCH queue fills */
       && fetch_num < ruu_ifq_size
       /* and no IFETCH blocking condition encountered */
       && !done
       /* and fetching is not halted to drain at the end of a sample */
       && ( !halt_fetch );
       i++)
    {
      enum md_opcode op;

      /* Wattch: add power for i-fetch stage */
      CMU_WATTCH(icache_access++);


      /* fetch an instruction at the next predicted fetch address */
      fetch_regs_PC = fetch_pred_PC;

      /* is this a bogus text address? (can happen on mis-spec path) */
      if (ld_text_base <= fetch_regs_PC
    && fetch_regs_PC < (ld_text_base+ld_text_size)
    && !(fetch_regs_PC & (sizeof(md_inst_t)-1)))
  {
    /* read instruction from memory */
    MD_FETCH_INST(inst, mem, fetch_regs_PC);

    /* address is within program text, read instruction from memory */
    lat = cache_il1_lat;
    if (cache_il1)
      {
        /* access the I-cache */
        lat =
    cache_access(cache_il1, Read, IACOMPRESS(fetch_regs_PC),
           NULL, NULL, ISCOMPRESS(sizeof(md_inst_t)), sim_cycle,
           NULL, NULL, (spec_mode ? WRONGPATH_FLAG : 0));
        if (lat > cache_il1_lat) {
            last_inst_missed = TRUE;
          }
      }

    if (itlb)
      {
        /* access the I-TLB, NOTE: this code will initiate
     speculative TLB misses */
        tlb_lat =
    cache_access(itlb, Read, IACOMPRESS(fetch_regs_PC),
           NULL, NULL, ISCOMPRESS(sizeof(md_inst_t)), sim_cycle,
           NULL, NULL, (spec_mode ? WRONGPATH_FLAG : 0));
        if (tlb_lat > 1) {
        last_inst_tmissed = TRUE;

          /* I-cache/I-TLB accesses occur in parallel */
          lat = MAX(tlb_lat, lat);
          }
      }

    /* I-cache/I-TLB miss? assumes I-cache hit >= I-TLB hit */
    if (lat != cache_il1_lat)
      {
        /* I-cache miss, block fetch until it is resolved */
        ruu_fetch_issue_delay += lat - 1;
        break;
      }
    /* else, I-cache/I-TLB hit */
  }
      else
  {
    /* fetch PC is bogus, send a NOP down the pipeline */
    inst = MD_NOP_INST;
  }

      /* have a valid inst, here */

      /* pre-decode instruction, used for bpred stats recording */
      MD_SET_OPCODE(op, inst);

      /* possibly use the BTB target */
      if (pred)
  {

    /* get the next predicted fetch address; only use branch predictor
       result for branches (assumes pre-decode bits); NOTE: returned
       value may be 1 if bpred can only predict a direction */
    if (MD_OP_FLAGS(op) & F_CTRL)
      fetch_pred_PC =
        bpred_lookup(
         pred,
         /* branch address */fetch_regs_PC,
         /* target address *//* FIXME: not computed */0,
         /* opcode */op,
         /* call? */MD_IS_CALL(op),
         /* return? */MD_IS_RETURN(op),
         /* updt */&(fetch_data[fetch_tail].dir_update),
         /* RSB index */&stack_recover_idx);
    else
      fetch_pred_PC = 0;

    /* valid address returned from branch predictor? */
    if (!fetch_pred_PC)
      {
        /* no predicted taken target, attempt not taken target */
        fetch_pred_PC = fetch_regs_PC + sizeof(md_inst_t);
      }
    else
      {
        /* go with target, NOTE: discontinuous fetch, so terminate */
        branch_cnt++;
        if (branch_cnt >= fetch_speed)
    done = TRUE;
      }
  }
      else
  {
    /* no predictor, just default to predict not taken, and
       continue fetching instructions linearly */
    fetch_pred_PC = fetch_regs_PC + sizeof(md_inst_t);
  }

      /* commit this instruction to the IFETCH -> DISPATCH queue */
      fetch_data[fetch_tail].IR = inst;
      fetch_data[fetch_tail].op = op;
      fetch_data[fetch_tail].regs_PC = fetch_regs_PC;
      fetch_data[fetch_tail].pred_PC = fetch_pred_PC;
      fetch_data[fetch_tail].stack_recover_idx = stack_recover_idx;
#     if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        fetch_data[fetch_tail].ptrace_seq = ptrace_seq++;
#     endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

#     if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        /* for pipe trace */
        ptrace_newinst(fetch_data[fetch_tail].ptrace_seq,
         inst, fetch_data[fetch_tail].regs_PC,
         0);
        ptrace_newstage(fetch_data[fetch_tail].ptrace_seq,
          PST_IFETCH,
          ((last_inst_missed ? PEV_CACHEMISS : 0)
           | (last_inst_tmissed ? PEV_TLBMISS : 0)));
#     endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

      last_inst_missed = FALSE;
      last_inst_tmissed = FALSE;

      /* adjust instruction fetch queue */
      fetch_tail = (fetch_tail + 1) & (ruu_ifq_size - 1);
      fetch_num++;
    }
}


/* Include all the dump methods for DLite support */
#if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
#include "dump.c"
#endif





bool_t load_next_checkpoint() {
  if (skip_ckpts > 0) {
    ckpt_skip_ckpts(ckpts, skip_ckpts);
    skip_ckpts = 0;
  }
  if (! ckpt_more_ckpts(ckpts) ) {
    if ( ! open_next_file() ) {
      return FALSE;
    }
  }
  if ( ckpt_more_ckpts(ckpts) ) {
      ckpt_restore_ckpt(ckpts);
      simulator_state = FAST_FORWARDING;
      return TRUE;
  }
  return FALSE;
}

/* start simulation, program loaded, processor precise state initialized */
void
sim_main(void)
{

  ckpt_connect_structures
    ( ckpts
    , &regs
    , mem
    , cache_il1
    , cache_dl1
    , cache_dl2
    , itlb
    , dtlb
    , pred
    );

  if (paired_out_filename != NULL) {
     paired_out_file = fopen(paired_out_filename,"w");
     if (! paired_out_file) {
        fatal("Unable to open paired-measurement output file");
     }
  }
  if (paired_in_filename != NULL) {
     paired_in_file = fopen(paired_in_filename,"r");
     if (! paired_in_file) {
        fatal("Unable to open paired-measurement input file");
     }

  }

  /* ignore any floating point exceptions, they may occur on mis-speculated
     execution paths */
  signal(SIGFPE, SIG_IGN);

  /* set up program entry state */
  regs.regs_PC = ld_prog_entry;
  regs.regs_NPC = regs.regs_PC + sizeof(md_inst_t);


  /* Until all checkpoints have been processed */
  while(! sim_exit_now) {
    int done_draining = 0;
    counter_t end_of_sample = 0;
    counter_t start_of_measurement = 0;

    switch_to_fast_forwarding();

    //Load a checkpoint
    if (! load_next_checkpoint()) {
      //No more checkpoints!
      return;
    }

    ckpt_dorestore_ckpt(ckpts);
    if (paired_out_file) {
      fprintf(paired_out_file, "%lld,", sim_pop_insn);
    }
    if (paired_in_file) {
      if ( fscanf(paired_in_file, "%lld,%lf", &paired_location, &paired_next_val) != 2) {
         fprintf(stderr, "Reached end of available paired measurement data.  Exiting");
         return;
      }
      if (paired_location != sim_pop_insn) {
        fatal("Mismatch between paired measurement data location %lld and checkpoint location %lld", paired_location, sim_pop_insn);
      }
    }

    if (sim_pop_insn > max_pop_insn) {
      max_pop_insn = sim_pop_insn;
      if (sim_sample_size >= 49 && max_pop_insn_changed == sim_sample_size) {
          fatal("Live-point library appears to be unshuffled. Run with -all_lvpts if you want to proceed anyway.\n");
      }
      ++max_pop_insn_changed;
    }

    switch_to_warming();

    /* set up timing simulation entry state */
    fetch_regs_PC = regs.regs_PC - sizeof(md_inst_t);
    fetch_pred_PC = regs.regs_PC;
    regs.regs_PC = regs.regs_PC - sizeof(md_inst_t);

    start_of_measurement = sim_pop_insn + sampling_wunit;

    /* Stop measureing m-unit instructions after we stop warming */
    end_of_sample = start_of_measurement + sampling_munit;

    /* main simulator loop, NOTE: the pipe stages are traverse in reverse order
       to eliminate this/next state synchronization and relaxation problems */
    while (!done_draining) {
      /* Note: when sampling is off, done_draining can never become true, so
         sample && is implicitly part of this condition */
      if ((simulator_state == WARMING) && sim_pop_insn >= start_of_measurement) {
        start_measuring();
      } else if ((simulator_state == MEASURING) && sim_pop_insn >= end_of_sample) {
        stop_measuring();
        fetch_just_redirected = 0;
      }

      #if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        /* RUU/LSQ sanity checks */
        if (RUU_num < LSQ_num)
          panic("RUU_num < LSQ_num");
        if (((RUU_head + RUU_num) & RUU_size_mask) != RUU_tail)
          panic("RUU_head/RUU_tail wedged");
        if (((LSQ_head + LSQ_num) & LSQ_size_mask) != LSQ_tail)
          panic("LSQ_head/LSQ_tail wedged");
      #endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

      /* added for Wattch to clear hardware access counters */
      CMU_WATTCH(clear_access_stats());


      #if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        /* check if pipetracing is still active */
        ptrace_check_active(regs.regs_PC, sim_pop_insn, sim_cycle);

        /* indicate new cycle in pipetrace */
        ptrace_newcycle(sim_cycle);
      #endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

      /* commit entries from RUU/LSQ to architected register file */
      sb_commit();

      /* commit entries from RUU/LSQ to architected register file */
      ruu_commit();

      /* service function unit release events */
      ruu_release_fu();

      /* ==> may have ready queue entries carried over from previous cycles */

      /* service result completions, also readies dependent operations */
      /* ==> inserts operations into ready queue --> register deps resolved */
      ruu_writeback();

      #if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        if (!bugcompat_mode) {
      #endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
      /* try to locate memory operations that are ready to execute */
      /* ==> inserts operations into ready queue --> mem deps resolved */
      lsq_refresh();

      /* issue operations ready to execute from a previous cycle */
      /* <== drains ready queue <-- ready operations commence execution */
      ruu_issue();
      #if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        }
      #endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

      /* decode and dispatch new operations */
      /* ==> insert ops w/ no deps or all regs ready --> reg deps resolved */
      ruu_dispatch();

      #if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
        if (bugcompat_mode) {
          /* try to locate memory operations that are ready to execute */
          /* ==> inserts operations into ready queue --> mem deps resolved */
          lsq_refresh();

          /* issue operations ready to execute from a previous cycle */
          /* <== drains ready queue <-- ready operations commence execution */
          ruu_issue();
        }
      #endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

      /* call instruction fetch unit if it is not blocked */
      if (simulator_state != DRAINING) {
        /* call instruction fetch unit if it is not blocked */
        if (!ruu_fetch_issue_delay)
         ruu_fetch();
        else
          ruu_fetch_issue_delay--;
      } else {
        if ((RUU_num == 0) && (LSQ_num == 0)) {
          done_draining = TRUE;
          /* Drain the SB */
          while (! SB_empty()) {
            SB_pop_head();
          }
        }
      }

      /* Added by Wattch to update per-cycle power statistics */
      CMU_WATTCH(update_power_stats());

      /* update buffer occupancy stats */
      IFQ_count += fetch_num;
      IFQ_fcount += ((fetch_num == ruu_ifq_size) ? 1 : 0);
      RUU_count += RUU_num;
      RUU_fcount += ((RUU_num == RUU_size) ? 1 : 0);
      LSQ_count += LSQ_num;
      LSQ_fcount += ((LSQ_num == LSQ_size) ? 1 : 0);

      /* go to next cycle */
      sim_cycle++;
      if (simulator_state == MEASURING) {
         sim_meas_cycle++;
      }

      /* mshr: count # entries in use */
      stat_add_sample(mshr_prof, count_mshr(cache_dl1,sim_cycle));

      /* finish early? */
      if (max_insts && sim_meas_insn >= max_insts)
        return;

    }

    /* twenisch To switch from sampling to fast-forwarding, we have to advance the PC */
    if (fetch_just_redirected) {
      regs.regs_PC = fetch_regs_PC;
    } else {
      regs.regs_PC = regs.regs_NPC;
    }
    regs.regs_NPC = regs.regs_PC + sizeof(md_inst_t);

    /* Have we reached our target confidence (check once per 100 ckpts) */
    if (target_confidence_reached()) {
        return;
    }

  }
}

