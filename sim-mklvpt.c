/*
 * sim-mklvpt.c - live-point creation simulator
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
#include "options.h"
#include "eval.h"
#include "stats.h"
#include "sim.h"
#include "power.h"
#include "liblvpt/lvpt.h"

/*
 * This file implements a very detailed out-of-order issue superscalar
 * processor with a two-level memory system and speculative execution support.
 * This simulator is a performance simulator, tracking the latency of all
 * pipeline operations.
 */




/* simulated registers */
static struct regs_t regs;

/* simulated memory */
struct mem_t *mem = NULL;

/*
 * simulator options
 */

/* Checkpointing support */
ckpt_state * ckpts;
char * ckpt_filename_base;
char ckpt_filename[2048];
int ckpt_fileno = 0;
int ckpt_this_file = 0;
int ckpt_per_file = 10000;

counter_t sim_ckpt_lines = 0;

char ckpt_cfg[8192];
char temp_cfg[8192];



/* sampling k value */
static counter_t sampling_k;

/* sampling measurement unit */
counter_t sampling_munit;
counter_t ckpt_munit; //Not used in sim-mklvpt

/* sampling measurement unit */
counter_t sampling_wunit;
counter_t ckpt_wunit; //Not used in sim-mklvpt

/* TRUE if in-order warming is enabled*/
static int sampling_allwarm;

/* period of each sample */
static counter_t sim_sample_period = 0;

static counter_t sim_sample_size = 0;



/* maximum number of inst's to execute */
static counter_t max_insts;

/* Benchmark name - appears in simulator output */
static char *bmark_opt;

/* maximum number of inst's to execute */
static counter_t sim_ckpt_insn;

/* maximum number of inst's to execute */
static counter_t sim_num_ckpt = 0;

/* number of insts skipped before timing starts */
static counter_t fastfwd_count;

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

/* l1 data cache config, i.e., {<config>|none} */
static char *cache_dl1_opt;

/* l2 data cache config, i.e., {<config>|none} */
static char *cache_dl2_opt;

/* l1 instruction cache config, i.e., {<config>|dl1|dl2|none} */
static char *cache_il1_opt;

/* l2 instruction cache config, i.e., {<config>|dl1|dl2|none} */
static char *cache_il2_opt;

/* flush caches on system calls */
static int flush_on_syscalls;

/* convert 64-bit inst addresses to 32-bit inst equivalents */
static int compress_icache_addrs;

/* instruction TLB config, i.e., {<config>|none} */
static char *itlb_opt;

/* data TLB config, i.e., {<config>|none} */
static char *dtlb_opt;

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


/*
 * simulator state variables
 */


/* perfect prediction enabled */
static int pred_perfect = FALSE;

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




/* register simulator-specific options */
void
sim_reg_options(struct opt_odb_t *odb)
{
  opt_reg_header(odb,
    "sim-mklvpt: This simulator creates live-points that can be processed by\n"
    "sim-oolvpt.\n"
  );



  /* instruction limit */


  opt_reg_longint(odb, "-max:inst", "maximum number of inst's to execute",
         &max_insts, /* default */0,
         /* print */TRUE, /* format */NULL);

  opt_reg_string(odb, "-bmark",
     "Benchmark name (recorded in the livepoint library file - no other effect)",
     &bmark_opt, "unknown",
     /* print */TRUE, NULL);


  /* sampling options */
  opt_reg_longint(odb, "-sampling",
	      "enable systematic sampling with the specified k value",
	      &sampling_k, /* default */ 0, /* print */TRUE, /* format */NULL);
  opt_reg_longint(odb, "-sampling:w-unit",
        "for systematic sampling, the oo-warming unit size",
        &sampling_wunit, /* default */ 2000, /* print */TRUE, /* format */NULL);
  opt_reg_longint(odb, "-sampling:m-unit",
        "for systematic sampling, the measurement unit size in instructions",
        &sampling_munit, /* default */ 1000, /* print */TRUE, /* format */NULL);

  opt_reg_flag(odb, "-sampling:allwarm",
        "perform in-order warming always",
         &sampling_allwarm, /* default */TRUE, /* print */TRUE, NULL);

  opt_reg_string(odb, "-library",
     "Live-point library base file name",
     &ckpt_filename_base, NULL, /* print */TRUE, NULL);
  opt_reg_int(odb, "-lvpt_per_file",
        "Number of live-points to store in one file",
        &ckpt_per_file, /* default */ 10000, /* print */TRUE, /* format */NULL);


  /* trace options */

  opt_reg_longint(odb, "-fastfwd", "number of insts skipped before timing starts",
        &fastfwd_count, /* default */0,
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

  opt_reg_string(odb, "-cache:dl2",
     "l2 data cache config, i.e., {<config>|none}",
     &cache_dl2_opt, "ul2:2048:128:4:l",
     /* print */TRUE, NULL);

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

  opt_reg_string(odb, "-cache:il2",
     "l2 instruction cache config, i.e., {<config>|dl2|none}",
     &cache_il2_opt, "dl2",
     /* print */TRUE, NULL);


  opt_reg_flag(odb, "-cache:flush", "flush caches on system calls",
         &flush_on_syscalls, /* default */FALSE, /* print */TRUE, NULL);

  opt_reg_flag(odb, "-cache:icompress",
         "convert 64-bit inst addresses to 32-bit inst equivalents",
         &compress_icache_addrs, /* default */FALSE,
         /* print */TRUE, NULL);


  /* TLB options */

  opt_reg_string(odb, "-tlb:itlb",
     "instruction TLB config, i.e., {<config>|none}",
     &itlb_opt, "itlb:32:4096:4:l", /* print */TRUE, NULL);

  opt_reg_string(odb, "-tlb:dtlb",
     "data TLB config, i.e., {<config>|none}",
     &dtlb_opt, "dtlb:64:4096:4:l", /* print */TRUE, NULL);

  opt_reg_note(odb,
    "Impetus-version-tag:   sim-mklvpt - " CMU_IMPETUS_VERSION_TAG "\n"
  );

}


/* miss handler function */
static unsigned int     /* latency of block access */
cache_access_fn(enum mem_cmd cmd,   /* access cmd, Read or Write */
        md_addr_t baddr,    /* block address to access */
        int bsize,    /* size of block to access */
        struct cache_blk_t *blk,  /* ptr to block in upper level */
        tick_t now,
        int stat_flags)   /* time of access */
{
  panic("cache miss handler should not be called in this binary");
}

/* check simulator-specific option values */
void
sim_check_options(struct opt_odb_t *odb,        /* options database */
      int argc, char **argv)        /* command line arguments */
{
  char name[128], c;
  int nsets, bsize, assoc;

  /* Verify sampling parameters */
  if (sampling_k <= 0) {
    fatal("must specify -sampling <k value> to create checkpoints");
  }
  if (sampling_munit <= 0) {
      fatal("sampling:munit may not be 0");
  }
  if (sampling_wunit+sampling_munit > sampling_k*sampling_munit) {
      fatal("sampling:k * sampling:munit may not exceed sampling:munit + sampling:wunit");
  }

  if (! ckpt_filename_base) {
    fatal("must specify -library <base file name> to create live-points");
  }

  if (fastfwd_count < 0 ) {
    fatal("bad fast forward count: %d", fastfwd_count);
  }

  if (!mystricmp(pred_type, "perfect")) {
      /* perfect predictor */
      pred = NULL;
      pred_perfect = TRUE;
  } else if (!mystricmp(pred_type, "taken")) {
      /* static predictor, not taken */
      pred = bpred_create(BPredTaken, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  } else if (!mystricmp(pred_type, "nottaken")) {
      /* static predictor, taken */
      pred = bpred_create(BPredNotTaken, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  } else if (!mystricmp(pred_type, "bimod")) {
      /* bimodal predictor, bpred_create() checks BTB_SIZE */
      if (bimod_nelt != 1) {
        fatal("bad bimod predictor config (<table_size>)");
      }
      if (btb_nelt != 2) {
        fatal("bad btb config (<num_sets> <associativity>)");
      }

      /* bimodal predictor, bpred_create() checks BTB_SIZE */
      pred = bpred_create
        ( BPred2bit
        , bimod_config[0]     /* bimod table size */
        , 0                   /* 2lev l1 size */
        , 0                   /* 2lev l2 size */
        , 0                   /* meta table size */
        , 0                   /* history reg size */
        , 0                   /* history xor address */
        , btb_config[0]       /* btb sets */
        , btb_config[1]       /* btb assoc */
        , ras_size            /* ret-addr stack size */
        );
  } else if (!mystricmp(pred_type, "2lev")) {
      /* 2-level adaptive predictor, bpred_create() checks args */
      if (twolev_nelt != 4) {
        fatal("bad 2-level pred config (<l1size> <l2size> <hist_size> <xor>)");
      }
      if (btb_nelt != 2) {
        fatal("bad btb config (<num_sets> <associativity>)");
      }

      pred = bpred_create
        ( BPred2Level
        , 0                   /* bimod table size */
        , twolev_config[0]    /* 2lev l1 size */
        , twolev_config[1]    /* 2lev l2 size */
        , 0                   /* meta table size */
        , twolev_config[2]    /* history reg size */
        , twolev_config[3]    /* history xor address */
        , btb_config[0]       /* btb sets */
        , btb_config[1]       /* btb assoc */
        , ras_size            /* ret-addr stack size */
        );
  } else if (!mystricmp(pred_type, "comb")) {
      /* combining predictor, bpred_create() checks args */
      if (twolev_nelt != 4) {
        fatal("bad 2-level pred config (<l1size> <l2size> <hist_size> <xor>)");
      }
      if (bimod_nelt != 1) {
        fatal("bad bimod predictor config (<table_size>)");
      }
      if (comb_nelt != 1) {
        fatal("bad combining predictor config (<meta_table_size>)");
      }
      if (btb_nelt != 2) {
        fatal("bad btb config (<num_sets> <associativity>)");
      }

      pred = bpred_create
        ( BPredComb
        , bimod_config[0]     /* bimod table size */
        , twolev_config[0]    /* l1 size */
        , twolev_config[1]    /* l2 size */
        , comb_config[0]      /* meta table size */
        , twolev_config[2]    /* history reg size */
        , twolev_config[3]    /* history xor address */
        , btb_config[0]       /* btb sets */
        , btb_config[1]       /* btb assoc */
        , ras_size            /* ret-addr stack size */
        );
  } else {
    fatal("cannot parse predictor type `%s'", pred_type);
  }


  /* use a level 1 D-cache? */
  if (!mystricmp(cache_dl1_opt, "none")) {
    cache_dl1 = NULL;

    /* the level 2 D-cache cannot be defined */
    if (strcmp(cache_dl2_opt, "none")) {
      fatal("the l1 data cache must defined if the l2 cache is defined");
    }
    cache_dl2 = NULL;
  } else {
    if (sscanf(cache_dl1_opt, "%[^:]:%d:%d:%d:%c", name, &nsets, &bsize, &assoc, &c) != 5) {
      fatal("bad l1 D-cache parms: <name>:<nsets>:<bsize>:<assoc>:<repl>");
    }
    /* mshr: create dl1 */
    cache_dl1 = cache_create
      ( name
      , nsets
      , bsize
      , FALSE /* balloc */
      , 0 /* usize */
      , assoc
      , cache_char2policy(c)
      , cache_access_fn
      , 0 /* hit lat */
      );

    /* is the level 2 D-cache defined? */
    if (!mystricmp(cache_dl2_opt, "none")) {
      cache_dl2 = NULL;
    } else {
      if (sscanf(cache_dl2_opt, "%[^:]:%d:%d:%d:%c", name, &nsets, &bsize, &assoc, &c) != 5) {
        fatal("bad l2 D-cache parms: <name>:<nsets>:<bsize>:<assoc>:<repl>");
      }
      cache_dl2 = cache_create
        (name
        , nsets
        , bsize
        , FALSE /* balloc */
        , 0 /* usize */
        , assoc
        , cache_char2policy(c)
        , cache_access_fn
        , 0 /* hit lat */
        );
    }
  }

  /* use a level 1 I-cache? */
  if (!mystricmp(cache_il1_opt, "none")) {
    cache_il1 = NULL;

    /* the level 2 I-cache cannot be defined */
    if (strcmp(cache_il2_opt, "none")) {
      fatal("the l1 inst cache must defined if the l2 cache is defined");
      cache_il2 = NULL;
    }
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
    cache_il1 = cache_create
      ( name
      , nsets
      , bsize
      , /* balloc */FALSE
      , /* usize */0
      , assoc
      , cache_char2policy(c)
      , cache_access_fn
      , 0 /* hit lat */
      );

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
      cache_il2 = cache_create
        ( name
        , nsets
        , bsize
        , /* balloc */FALSE
        , /* usize */0
        , assoc
        , cache_char2policy(c)
        , cache_access_fn
        , 0 /* hit lat */
        );
    }
  }

  /* use an I-TLB? */
  if (!mystricmp(itlb_opt, "none")) {
    itlb = NULL;
  } else {
    if (sscanf(itlb_opt, "%[^:]:%d:%d:%d:%c", name, &nsets, &bsize, &assoc, &c) != 5) {
      fatal("bad TLB parms: <name>:<nsets>:<page_size>:<assoc>:<repl>");
    }

    itlb = cache_create
      (name
      , nsets
      , bsize
      , /* balloc */FALSE
      , /* usize */sizeof(md_addr_t)
      , assoc
      , cache_char2policy(c)
      , cache_access_fn
      , 0 /* hit latency */
      );
  }

  /* use a D-TLB? */
  if (!mystricmp(dtlb_opt, "none")) {
    dtlb = NULL;
  } else {
    if (sscanf(dtlb_opt, "%[^:]:%d:%d:%d:%c", name, &nsets, &bsize, &assoc, &c) != 5) {
      fatal("bad TLB parms: <name>:<nsets>:<page_size>:<assoc>:<repl>");
    }
    dtlb = cache_create
      ( name
      , nsets
      , bsize
      , /* balloc */FALSE
      , /* usize */sizeof(md_addr_t)
      , assoc
      , cache_char2policy(c)
      , cache_access_fn
      , 0 /* hit latency */
      );
  }

  if (sampling_allwarm) {
      /* The Fast functional warming code makes a bunch of assumptions about the options passed in from
         the command line.  This is to increase the speed of functional warming (it avoids a bunch of
         if statements).  These checks cause the simulator to die if an unsupported option is
         used when functional warming is enabled.
       */

     if (cache_dl2 != cache_il2) {
        fatal("Functional warming code assumes a unified L2 cache.");
     }

     if (pred->klass != BPredComb) {
        fatal("Functional warming code assumes a combined branch predictor.");
     }

     if (pred->retstack.size == 0) {
        fatal("Functional warming code assumes that a RAS is being used.");
     }

     if (
      (cache_dl1 && (cache_dl1->policy !=  LRU )) ||
      (cache_dl2 && (cache_dl2->policy !=  LRU )) ||
      (cache_il1 && (cache_il1->policy !=  LRU )) ||
      (cache_il2 && (cache_il2->policy !=  LRU )) ||
      (dtlb && (dtlb->policy !=  LRU )) ||
      (itlb && (itlb->policy !=  LRU ))
      ) {
        fatal("Functional warming code assumes that LRU replacement is used in caches and TLBs.");
     }
  } //(sampling_allwarm) {

  sprintf(ckpt_cfg, "IL1: %s\nIL2: %s\nDL1: %s\nDL2: %s\nITLB: %s\nDTLB: %s\nBpred Type: %s\nK: %lld\nM-unit: %lld\nW-unit: %lld\n",cache_il1_opt, cache_il2_opt, cache_dl1_opt, cache_dl2_opt, itlb_opt, dtlb_opt, pred_type, sampling_k, sampling_munit,sampling_wunit);

}

/* print simulator-specific configuration information */
void
sim_aux_config(FILE *stream)            /* output stream */
{
  /* nada */
}

/* register simulator-specific statistics */
void
sim_reg_stats(struct stat_sdb_t *sdb) {

  stat_reg_counter(sdb, "sim_ckpt_insn",
       "total number of instructions included in live-points",
       &sim_ckpt_insn, sim_ckpt_insn, NULL);
  stat_reg_counter(sdb, "sim_pop_insn",
       "total number of instructions in application",
       &sim_pop_insn, sim_pop_insn, NULL);
  stat_reg_counter(sdb, "sim_num_ckpt",
       "Number of checkpoints collected",
       &sim_num_ckpt, sim_num_ckpt, NULL);
  stat_reg_counter(sdb, "sim_ckpt_lines",
       "Number of cache lines stored in all live-points",
       &sim_ckpt_lines, sim_ckpt_lines, NULL);
  stat_reg_formula(sdb, "lines_per_ckpt",
       "average cache lines per live-point",
       "sim_ckpt_lines / sim_num_ckpt", NULL);
  stat_reg_counter(sdb, "sim_sample_size",
       "sample size when sampling is enabled",
       &sim_sample_size, sim_sample_size, NULL);
  stat_reg_counter(sdb, "sim_sample_period",
       "sampling period when sampling is enabled",
       &sim_sample_period, sim_sample_period, NULL);
  stat_reg_int(sdb, "sim_elapsed_time",
         "total simulation time in seconds",
         &sim_elapsed_time, 0, NULL);
  stat_reg_formula(sdb, "sim_pop_rate",
       "simulation speed (in all simulated insts/sec)",
       "sim_pop_insn / sim_elapsed_time", NULL);


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

  ld_reg_stats(sdb);
  mem_reg_stats(mem, sdb);
}


/* initialize the simulator */
void sim_init(void) {
  /* allocate and initialize register file */
  regs_init(&regs);

  /* allocate and initialize memory space */
  mem = mem_create("mem");
  mem_init(mem);
}


/* load program into simulated state */
void
sim_load_prog(char *fname,    /* program to load */
        int argc, char **argv,  /* program arguments */
        char **envp)    /* program environment */
{
  /* load program text and data, set up environment, memory, and regs */
  ld_load_prog(fname, argc, argv, envp, &regs, mem, TRUE);
}

/* dump simulator-specific auxiliary simulator statistics */
void sim_aux_stats(FILE *stream) { }

/* un-initialize the simulator */
void sim_uninit(void) { }



/* Warming related macros */
#define CACHE_BADDR(cp, addr)   ((addr) & ~(cp)->blk_mask)

static md_addr_t last_dl1_tag = 0;
static md_addr_t dl1_tagset_mask;
static md_addr_t last_dtlb_tag = 0;
static md_addr_t dtlb_tagset_mask;
int last_dl1_block_dirty = 0;


#define WARM_D(cmd, addr, cnt)                                                                                                               \
    (                                                                                                                                        \
       sampling_allwarm ?                                                                                                                    \
         (                                                                                                                                   \
            ( (last_dl1_tag == ((addr) & dl1_tagset_mask)) && (((cmd) == Read) || last_dl1_block_dirty ) ) ?                                 \
               (                                                                                                                             \
                  NULL /* do nothing if tagset matches last dl1 tagset, and the op is a read or the block is already dirty */                \
               ) : (                                                                                                                         \
                  last_dl1_tag = ((addr) & dl1_tagset_mask),                                                                                 \
                  last_dl1_block_dirty = ((cmd) == Write),                                                                                   \
                  ( dl1_extremely_fast_access((cmd), (addr), (cnt)) ?                                                                        \
                     (                                                                                                                       \
                        NULL                                                                                                                 \
                      ) : (                                                                                                                  \
                        l2_fast_access((cmd), (addr), (cnt))                                                                                 \
                      )                                                                                                                      \
                   ),                                                                                                                        \
                  ( last_dtlb_tag != ((addr) & dtlb_tagset_mask) ?                                                                           \
                    (                                                                                                                        \
                       last_dtlb_tag = ((addr) & dtlb_tagset_mask),                                                                          \
                       dtlb_extremely_fast_access(dtlb, (addr), (cnt))                                                                       \
                     ) : (                                                                                                                   \
                       NULL                                                                                                                  \
                     )                                                                                                                       \
                   )                                                                                                                         \
               )                                                                                                                             \
          )                                                                                                                                  \
          : NULL /* do nothing if ! sampling_allwarm */                                                                                      \
     )


/*
 * configure the execution engine for fast forwarding
 */

/* next program counter */
#define SET_NPC(EXPR)   (regs.regs_NPC = (EXPR))

/* current program counter */
#define CPC     (regs.regs_PC)

/* general purpose registers */
#define GPR(N)      (regs.regs_R[N])
#define SET_GPR(N,EXPR)   (regs.regs_R[N] = (EXPR))

#if defined(TARGET_PISA)

/* floating point registers, L->word, F->single-prec, D->double-prec */
#define FPR_L(N)    (regs.regs_F.l[(N)])
#define SET_FPR_L(N,EXPR) (regs.regs_F.l[(N)] = (EXPR))
#define FPR_F(N)    (regs.regs_F.f[(N)])
#define SET_FPR_F(N,EXPR) (regs.regs_F.f[(N)] = (EXPR))
#define FPR_D(N)    (regs.regs_F.d[(N) >> 1])
#define SET_FPR_D(N,EXPR) (regs.regs_F.d[(N) >> 1] = (EXPR))

/* miscellaneous register accessors */
#define SET_HI(EXPR)    (regs.regs_C.hi = (EXPR))
#define HI      (regs.regs_C.hi)
#define SET_LO(EXPR)    (regs.regs_C.lo = (EXPR))
#define LO      (regs.regs_C.lo)
#define FCC     (regs.regs_C.fcc)
#define SET_FCC(EXPR)   (regs.regs_C.fcc = (EXPR))

#elif defined(TARGET_ALPHA)

/* floating point registers, L->word, F->single-prec, D->double-prec */
#define FPR_Q(N)    (regs.regs_F.q[N])
#define SET_FPR_Q(N,EXPR) (regs.regs_F.q[N] = (EXPR))
#define FPR(N)      (regs.regs_F.d[N])
#define SET_FPR(N,EXPR)   (regs.regs_F.d[N] = (EXPR))

/* miscellaneous register accessors */
#define FPCR      (regs.regs_C.fpcr)
#define SET_FPCR(EXPR)    (regs.regs_C.fpcr = (EXPR))
#define UNIQ      (regs.regs_C.uniq)
#define SET_UNIQ(EXPR)    (regs.regs_C.uniq = (EXPR))

#else
#error No ISA target defined...
#endif

/* precise architected memory state accessor macros */
#define READ_BYTE(SRC, FAULT)           \
  (WARM_D(Read, (SRC), sim_pop_insn), (FAULT) = md_fault_none, MEM_READ_BYTE(mem, (SRC)))
#define READ_HALF(SRC, FAULT)           \
  (WARM_D(Read, (SRC), sim_pop_insn), (FAULT) = md_fault_none, MEM_READ_HALF(mem, (SRC)))
#define READ_WORD(SRC, FAULT)           \
  (WARM_D(Read, (SRC), sim_pop_insn), (FAULT) = md_fault_none, MEM_READ_WORD(mem, (SRC)))
#ifdef HOST_HAS_QWORD
#define READ_QWORD(SRC, FAULT)            \
  (WARM_D(Read, (SRC), sim_pop_insn), (FAULT) = md_fault_none, MEM_READ_QWORD(mem, (SRC)))
#endif /* HOST_HAS_QWORD */

#define WRITE_BYTE(SRC, DST, FAULT)         \
  (WARM_D(Write, (DST), sim_pop_insn), (FAULT) = md_fault_none, MEM_WRITE_BYTE(mem, (DST), (SRC)))
#define WRITE_HALF(SRC, DST, FAULT)         \
  (WARM_D(Write, (DST), sim_pop_insn), (FAULT) = md_fault_none, MEM_WRITE_HALF(mem, (DST), (SRC)))
#define WRITE_WORD(SRC, DST, FAULT)         \
  (WARM_D(Write, (DST), sim_pop_insn), (FAULT) = md_fault_none, MEM_WRITE_WORD(mem, (DST), (SRC)))
#ifdef HOST_HAS_QWORD
#define WRITE_QWORD(SRC, DST, FAULT)          \
  (WARM_D(Write, (DST), sim_pop_insn), (FAULT) = md_fault_none, MEM_WRITE_QWORD(mem, (DST), (SRC)))
#endif /* HOST_HAS_QWORD */

/* system call handler macro */
#define SYSCALL(INST) sys_syscall(&regs, mem_access, mem, INST, TRUE)

#ifndef NO_INSN_COUNT
#define INC_INSN_CTR()  sim_meas_insn++
#else /* !NO_INSN_COUNT */
#define INC_INSN_CTR()  /* nada */
#endif /* NO_INSN_COUNT */

#ifdef TARGET_ALPHA
#define ZERO_FP_REG() regs.regs_F.d[MD_REG_ZERO] = 0.0
#else
#define ZERO_FP_REG() /* nada... */
#endif

#ifndef TARGET_ALPHA
#undef MD_FETCH_INST
#define MD_FETCH_INST(INST, MEM, PC)                                  \
  { inst.a = MD_SWAPW(__UNCHK_MEM_READ(MEM, PC, word_t));             \
    inst.b = MD_SWAPW(__UNCHK_MEM_READ(MEM, (PC) + sizeof(word_t), word_t)); }
#endif

void switch_to_fast_forwarding() {

   if (! (simulator_state == NOT_STARTED || simulator_state == CHECKPOINTING )) {
      fatal("We should only switch to fast forwarding from NOT_STARTED or DRAINING state.");
   }
   simulator_state = FAST_FORWARDING;

}

void fast_forward(counter_t fast_forward_count) {

  /* Go to fast_forwarding mode */
  switch_to_fast_forwarding();

  /* fast forward simulator loop, performs functional simulation for
     FASTFWD_COUNT insts, then turns on performance (timing) simulation */
  if (fast_forward_count > 0) {
    md_inst_t inst;     /* actual instruction bits */
    enum md_opcode op;    /* decoded opcode enum */
    md_addr_t last_il1_tag = 0;
    md_addr_t il1_tagset_mask = cache_il1->tagset_mask;
    md_addr_t last_itlb_tag = 0;
    md_addr_t itlb_tagset_mask = itlb->tagset_mask;
    unsigned long icount_high, icount_high_limit = fast_forward_count >> 32;
    unsigned long icount_low, icount_low_limit;
    dl1_tagset_mask = cache_dl1->tagset_mask;
    dtlb_tagset_mask = dtlb->tagset_mask;

    myfprintf(stderr, "*** Fast-Forwarding %lld instructions ***\n", fast_forward_count);


    for (icount_high = 0; icount_high <= icount_high_limit; ++icount_high) {
      if (icount_high == icount_high_limit) {
        icount_low_limit = fast_forward_count & (unsigned long)(-1);
        if (icount_low_limit == 0) break;
      } else {
        icount_low_limit = 0;
      }
      icount_low = 0;
      do {

        /* maintain $r0 semantics */
        regs.regs_R[MD_REG_ZERO] = 0;
        #ifdef TARGET_ALPHA
          regs.regs_F.d[MD_REG_ZERO] = 0.0;
        #endif /* TARGET_ALPHA */

        /* get the next instruction to execute */
        MD_FETCH_INST(inst, mem, regs.regs_PC);

        /* Warm the ICACHE, if enabled */
        if (sampling_allwarm && last_il1_tag != (regs.regs_PC & il1_tagset_mask)) {
          last_il1_tag = (regs.regs_PC & il1_tagset_mask);
          if (! icache_extremely_fast_access(cache_il1, regs.regs_PC, sim_pop_insn) ) {
            l2_fast_access(Read, regs.regs_PC, sim_pop_insn);
          }
          if ( last_itlb_tag != (regs.regs_PC & itlb_tagset_mask)) {
            last_itlb_tag = (regs.regs_PC & itlb_tagset_mask);
            icache_extremely_fast_access(itlb, regs.regs_PC, sim_pop_insn);
          }
        }


        /* decode the instruction */
        MD_SET_OPCODE(op, inst);

        /* execute the instruction */
        switch (op) {
            #define DEFINST(OP,MSK,NAME,OPFORM,RES,FLAGS,O1,O2,I1,I2,I3)    \
              case OP:                                                      \
              SYMCAT(OP,_IMPL);                                             \
              break;                                                        /**/

            #define DEFLINK(OP,MSK,NAME,MASK,SHIFT)                         \
              case OP:                                                      \
                panic("attempted to execute a linking opcode");             /**/

            #define CONNECT(OP)                                             /**/

            #undef DECLARE_FAULT
            #define DECLARE_FAULT(FAULT)                                    \
              { /* uncaught */ break; }                                     /**/

            #include "machine.def"
          default:
            panic("attempted to execute a bogus opcode");
        }

        /* WARM BPRED */
        if (sampling_allwarm && MD_OP_FLAGS(op) & F_CTRL) {
          if (MD_IS_RETURN(op)) {
            pred->retstack.tos = (pred->retstack.tos + pred->retstack.size - 1) % pred->retstack.size;
          } else {
            bpred_update_fast(regs.regs_PC,regs.regs_NPC,op, MD_IS_CALL(op));
          }
        }

        /* go to the next instruction */
        regs.regs_PC = regs.regs_NPC;
        regs.regs_NPC += sizeof(md_inst_t);
        ++icount_low;
        ++sim_pop_insn;
      } while (icount_low != icount_low_limit);
    }
  } //end fast_forward_count > 0
}


void switch_to_checkpointing() {
   if (simulator_state != FAST_FORWARDING ) {
      fatal("We should only switch to CHECKPOINTING from FAST_FORWARDING state.");
   }
   simulator_state = CHECKPOINTING;
}


/* Redefine macros for checkpointing support */
#undef SYSCALL
#define SYSCALL(INST) ckpt_syscall(sim_pop_insn + 1, &regs, mem_access, mem, INST)

/* precise architected memory state accessor macros */
#undef WARM_D
#define WARM_D(cmd, addr, cnt)                                                                                                          \
    (                                                                                                                                   \
       ( (last_dl1_tag == ((addr) & dl1_tagset_mask)) && (((cmd) == Read) || last_dl1_block_dirty ) ) ?                                 \
          (                                                                                                                             \
             NULL /* do nothing if tagset matches last dl1 tagset, and the op is a read or the block is already dirty */                \
          ) : (                                                                                                                         \
             last_dl1_tag = ((addr) & dl1_tagset_mask),                                                                                 \
             last_dl1_block_dirty = ((cmd) == Write),                                                                                   \
             (  ckpt_l2_miss((cmd),( addr & cache_dl2->tagset_mask), cache_dl2->bsize)                                                  \
             ,  dl1_extremely_fast_access((cmd), (addr), (cnt)) ?                                                                       \
                (                                                                                                                       \
                   NULL                                                                                                                 \
                 ) : (                                                                                                                  \
                   l2_fast_access((cmd), (addr), (cnt))                                                                                 \
                 )                                                                                                                      \
              ),                                                                                                                        \
             ( last_dtlb_tag != ((addr) & dtlb_tagset_mask) ?                                                                           \
               (                                                                                                                        \
                  last_dtlb_tag = ((addr) & dtlb_tagset_mask),                                                                          \
                  dtlb_extremely_fast_access(dtlb, (addr), (cnt))                                                                       \
                ) : (                                                                                                                   \
                  NULL                                                                                                                  \
                )                                                                                                                       \
              )                                                                                                                         \
          )                                                                                                                             \
     )

void open_next_ckpt_file(void) {
  /* Create next checkpoint file name */
  sprintf(ckpt_filename, "%s.%02d.lvpt",ckpt_filename_base, ckpt_fileno);

  sprintf(temp_cfg, "%s\nFile: %02d",ckpt_cfg, ckpt_fileno);

  /* open up the checkpoint file */
  if (ckpt_fileno != 0) {
    ckpt_close(ckpts);
  }

  /* open up the checkpoint file */
  ckpts = ckpt_open_writer
    ( ckpt_filename
    , &regs
    , mem
    , cache_il1
    , cache_dl1
    , cache_dl2
    , itlb
    , dtlb
    , pred
    );

  ++ckpt_fileno;

  ckpt_write_header( ckpts, bmark_opt, sampling_munit, sampling_wunit, temp_cfg );

}


bool_t checkpoint(counter_t end_of_checkpoint) {
  md_inst_t inst;     /* actual instruction bits */
  enum md_opcode op;    /* decoded opcode enum */
  md_addr_t last_il1_tag = 0;
  md_addr_t il1_tagset_mask = cache_il1->tagset_mask;
  md_addr_t last_itlb_tag = 0;
  md_addr_t itlb_tagset_mask = itlb->tagset_mask;
  dl1_tagset_mask = cache_dl1->tagset_mask;
  dtlb_tagset_mask = dtlb->tagset_mask;

  if (end_of_checkpoint == 0 || end_of_checkpoint < sim_pop_insn) {
    myfprintf(stderr, "No more live-points to create.  Exiting\n");
    return TRUE;
  }

  myfprintf(stderr, "*** Creating live-point for instructions %lld:%lld ***\n", sim_pop_insn, end_of_checkpoint);

  switch_to_checkpointing();

  ckpt_begin( ckpts, sim_pop_insn, 0 /* write caches */ );
  last_dl1_tag = 0; //allow us to use dl1 fast hit optimization to avoid ckpt calls
  sim_num_ckpt++;

  while ( sim_pop_insn < end_of_checkpoint ) {

    /* maintain $r0 semantics */
    regs.regs_R[MD_REG_ZERO] = 0;
    #ifdef TARGET_ALPHA
      regs.regs_F.d[MD_REG_ZERO] = 0.0;
    #endif /* TARGET_ALPHA */

    /* get the next instruction to execute */
    MD_FETCH_INST(inst, mem, regs.regs_PC);

    /* Warm the ICACHE, if enabled */
    if (sampling_allwarm && last_il1_tag != (regs.regs_PC & il1_tagset_mask)) {
      last_il1_tag = (regs.regs_PC & il1_tagset_mask);
      if (! icache_extremely_fast_access(cache_il1, regs.regs_PC, sim_pop_insn) ) {
        l2_fast_access(Read, regs.regs_PC, sim_pop_insn);
      }
      if ( last_itlb_tag != (regs.regs_PC & itlb_tagset_mask)) {
        last_itlb_tag = (regs.regs_PC & itlb_tagset_mask);
        icache_extremely_fast_access(itlb, regs.regs_PC, sim_pop_insn);
      }
    }

    /* decode the instruction */
    MD_SET_OPCODE(op, inst);

    /* execute the instruction */
    switch (op) {
        #define DEFINST(OP,MSK,NAME,OPFORM,RES,FLAGS,O1,O2,I1,I2,I3)    \
          case OP:                                                      \
          SYMCAT(OP,_IMPL);                                             \
          break;                                                        /**/

        #define DEFLINK(OP,MSK,NAME,MASK,SHIFT)                         \
          case OP:                                                      \
            panic("attempted to execute a linking opcode");             /**/

        #define CONNECT(OP)                                             /**/

        #undef DECLARE_FAULT
        #define DECLARE_FAULT(FAULT)                                    \
          { /* uncaught */ break; }                                     /**/

        #include "machine.def"
      default:
        panic("attempted to execute a bogus opcode");
    }


    /* WARM BPRED */
    if (sampling_allwarm && MD_OP_FLAGS(op) & F_CTRL) {
       if (MD_IS_RETURN(op)) {
           pred->retstack.tos = (pred->retstack.tos + pred->retstack.size - 1) % pred->retstack.size;
       } else {
           bpred_update_fast(regs.regs_PC,regs.regs_NPC,op, MD_IS_CALL(op));
       }
    }

    /* go to the next instruction */
    regs.regs_PC = regs.regs_NPC;
    regs.regs_NPC += sizeof(md_inst_t);

    ++sim_pop_insn;
    ++sim_ckpt_insn;

    /* finish early? */
    if (max_insts && sim_pop_insn >= max_insts) {
      ckpt_finish( sim_pop_insn );
      myfprintf(stderr, "Reached max_insts.  Terminating checkpoint and exiting.\n");
      return TRUE;
    }

  } //end while (sim_pop_insn < end_of_sample)

  ckpt_finish( sim_pop_insn );

  return FALSE;

}



void prepare_checkpoint_locations(void) {
  /* Calculate overall sampling period if sampling is enabled */
  sim_sample_period = sampling_k * sampling_munit;

  open_next_ckpt_file();

}

counter_t calculate_fast_forward_count() {
  counter_t fast_forward_count = 0;

  /* calculate the number of instructions to fast forward. */
  if (fastfwd_count && simulator_state == NOT_STARTED) {
     fast_forward_count = fastfwd_count;
  } else {
    counter_t end_of_next_period =
      ( ( (sim_pop_insn - fastfwd_count) / sim_sample_period ) + 1 ) * sim_sample_period + fastfwd_count;
    fast_forward_count = end_of_next_period - sim_pop_insn - sampling_wunit - sampling_munit;
  }

  return fast_forward_count;
}

counter_t calculate_checkpoint_count() {
  counter_t end_of_sample = 0;
    //Calculate the end of the checkpoint period
    /* Calculate the end of the warming period for this warming interval */
    counter_t start_of_measurement = sim_pop_insn + sampling_wunit;

    /* Stop measureing m-unit instructions after we stop warming */
    end_of_sample = start_of_measurement + sampling_munit;
  return end_of_sample;
}


/* start simulation, program loaded, processor precise state initialized */
void sim_main(void) {

  /* ignore any floating point exceptions, they may occur on mis-speculated
     execution paths */
  signal(SIGFPE, SIG_IGN);

  /* set up program entry state */
  regs.regs_PC = ld_prog_entry;
  regs.regs_NPC = regs.regs_PC + sizeof(md_inst_t);


  /* Prepare checkpointing support */
  prepare_checkpoint_locations();

  /* Until the simulation exits */
  while(1) {
    fast_forward(calculate_fast_forward_count());

    if (max_insts && sim_pop_insn >= max_insts) {
      return;
    }

    ckpt_this_file++;
    if (ckpt_this_file > ckpt_per_file) {
      open_next_ckpt_file();
      ckpt_this_file = 0;
    }

    if ( checkpoint(calculate_checkpoint_count()) ) {
      return;
    }

    ++sim_sample_size;

    if (max_insts && sim_pop_insn >= max_insts) {
      return;
    }

  }
}

/* These all do nothing in this simulator */
void simoo_restore_stats() { }
void stop_measuring() { }
