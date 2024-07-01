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

static struct stat_stat_t *sample_CPI_mean = NULL;
#if CMU_WATTCH_ENABLED
  static struct stat_stat_t *sample_EPI_mean = NULL;
#endif //CMU_WATTCH_ENABLED
static struct stat_stat_t *paired_delta_mean = NULL;

/* sampling enabled.  1 if enabled, 0 otherwise */
static unsigned int sampling_enabled;


/* sampling k value */
static counter_t sampling_k;

/* sampling measurement unit */
extern counter_t sampling_munit;

/* sampling measurement unit */
static counter_t sampling_wunit;

/* TRUE if in-order warming is enabled*/
static int sampling_allwarm;


/* Paired-sample comparison support */
extern FILE * paired_out_file;
static FILE * paired_in_file = NULL;
static double paired_next_val = 0.0;
static counter_t paired_location = 0;

/*
 * simulator stats
 */
#if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
  /* SLIP variable */
  static counter_t sim_slip = 0;
#endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )

/* period of each sample */
static counter_t sim_sample_period = 0;

/* total number of instructions measured */
static counter_t sim_detail_insn = 0;

/* total number of instructions measured */
static tick_t sim_meas_cycle = 0;
static counter_t sim_meas_insn = 0;

/* total number of instructions measured */
static counter_t sim_total_insn = 0;

/* total number of memory references committed */
static counter_t sim_num_refs = 0;

/* total number of memory references executed */
static counter_t sim_total_refs = 0;

/* total number of loads committed */
static counter_t sim_num_loads = 0;

/* total number of loads executed */
static counter_t sim_total_loads = 0;

/* total number of branches committed */
static counter_t sim_num_branches = 0;

/* total number of branches executed */
static counter_t sim_total_branches = 0;

/* cycle counter */
static tick_t sim_cycle = 0;

static counter_t sb_full_stall_cycles = 0;
static counter_t sb_count = 0;
static int sb_max = 0;

/* occupancy counters */
static counter_t IFQ_count;		/* cumulative IFQ occupancy */
static counter_t IFQ_fcount;		/* cumulative IFQ full count */
static counter_t RUU_count;		/* cumulative RUU occupancy */
static counter_t RUU_fcount;		/* cumulative RUU full count */
static counter_t LSQ_count;		/* cumulative LSQ occupancy */
static counter_t LSQ_fcount;		/* cumulative LSQ full count */

/* total non-speculative bogus addresses seen (debug var) */
static counter_t sim_invalid_addrs;

/* flag to indicate that fetching should be halted to drain the pipe
   when switching to fast-forwaring */
static int halt_fetch = 0;
static int fetch_just_redirected = 0;


static counter_t sim_sample_size = 0;


#if CMU_WATTCH_ENABLED
  extern double rename_power_cc3,bpred_power_cc3,lsq_power_cc3,window_power_cc3,regfile_power_cc3,icache_power_cc3,resultbus_power_cc3,clock_power_cc3, alu_power_cc3, dcache_power_cc3, dcache2_power_cc3;
#endif //CMU_WATTCH_ENABLED


/* SAMPLING SUPPORT CODE */
/*************************/

struct simoo_stats_t {
  counter_t sim_meas_insn;
  tick_t sim_meas_cycle;
#if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
  counter_t sim_slip;
#endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
  counter_t sim_total_insn;
  counter_t sim_num_refs;
  counter_t sim_total_refs;
  counter_t sim_num_loads;
  counter_t sim_total_loads;
  counter_t sim_num_branches;
  counter_t sim_total_branches;
  counter_t IFQ_count;
  counter_t IFQ_fcount;
  counter_t RUU_count;
  counter_t RUU_fcount;
  counter_t LSQ_count;
  counter_t LSQ_fcount;
  counter_t sim_invalid_addrs;
  counter_t sb_full_stall_cycles;
  counter_t sb_count;
  int sb_max;
} ;
struct simoo_stats_t * simoo_stats_backup;

void simoo_backup_stats() {
  /* Note: sim_meas_insn & sim_meas cycles are backup up, but not restored */
  /* This is for std dev calculation */
  simoo_stats_backup->sim_meas_insn = sim_meas_insn;
  simoo_stats_backup->sim_meas_cycle = sim_meas_cycle;

# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
  simoo_stats_backup->sim_slip = sim_slip;
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
  simoo_stats_backup->sim_total_insn = sim_total_insn;
  simoo_stats_backup->sim_num_refs = sim_num_refs;
  simoo_stats_backup->sim_total_refs = sim_total_refs;
  simoo_stats_backup->sim_num_loads = sim_num_loads;
  simoo_stats_backup->sim_total_loads = sim_total_loads;
  simoo_stats_backup->sim_num_branches = sim_num_branches;
  simoo_stats_backup->sim_total_branches = sim_total_branches;
  simoo_stats_backup->IFQ_count = IFQ_count;
  simoo_stats_backup->IFQ_fcount = IFQ_fcount;
  simoo_stats_backup->RUU_count = RUU_count;
  simoo_stats_backup->RUU_fcount = RUU_fcount;
  simoo_stats_backup->LSQ_count = LSQ_count;
  simoo_stats_backup->LSQ_fcount = LSQ_fcount;
  simoo_stats_backup->sim_invalid_addrs = sim_invalid_addrs;
  simoo_stats_backup->sb_full_stall_cycles = sb_full_stall_cycles;
  simoo_stats_backup->sb_count = sb_count;
  simoo_stats_backup->sb_max = sb_max;

  cache_backup_stats(cache_il1);
  cache_backup_stats(cache_il2);
  cache_backup_stats(cache_dl1);
  cache_backup_stats(cache_dl2);
  cache_backup_stats(itlb);
  cache_backup_stats(dtlb);
  bpred_backup_stats(pred);
  power_backup_stats();
}

void simoo_alloc_stats() {
  simoo_stats_backup = calloc(1, sizeof(struct simoo_stats_t));
  cache_alloc_stats(cache_il1);
  cache_alloc_stats(cache_il2);
  cache_alloc_stats(cache_dl1);
  cache_alloc_stats(cache_dl2);
  cache_alloc_stats(itlb);
  cache_alloc_stats(dtlb);
  bpred_alloc_stats(pred);
  power_alloc_stats();
}

void simoo_restore_stats() {
# if (! CMU_AGGRESSIVE_CODE_ELIMINATION )
  sim_slip = simoo_stats_backup->sim_slip;
# endif //(! CMU_AGGRESSIVE_CODE_ELIMINATION )
  sim_total_insn = simoo_stats_backup->sim_total_insn;
  sim_num_refs = simoo_stats_backup->sim_num_refs;
  sim_total_refs = simoo_stats_backup->sim_total_refs;
  sim_num_loads = simoo_stats_backup->sim_num_loads;
  sim_total_loads = simoo_stats_backup->sim_total_loads;
  sim_num_branches = simoo_stats_backup->sim_num_branches;
  sim_total_branches = simoo_stats_backup->sim_total_branches;
  IFQ_count = simoo_stats_backup->IFQ_count;
  IFQ_fcount = simoo_stats_backup->IFQ_fcount;
  RUU_count = simoo_stats_backup->RUU_count;
  RUU_fcount = simoo_stats_backup->RUU_fcount;
  LSQ_count = simoo_stats_backup->LSQ_count;
  LSQ_fcount = simoo_stats_backup->LSQ_fcount;
  sim_invalid_addrs = simoo_stats_backup->sim_invalid_addrs;
  sb_full_stall_cycles = simoo_stats_backup->sb_full_stall_cycles ;
  sb_count = simoo_stats_backup->sb_count;
  sb_max = simoo_stats_backup->sb_max;

  cache_restore_stats(cache_il1);
  cache_restore_stats(cache_il2);
  cache_restore_stats(cache_dl1);
  cache_restore_stats(cache_dl2);
  cache_restore_stats(itlb);
  cache_restore_stats(dtlb);
  bpred_restore_stats(pred);
  power_restore_stats();
}

void mark_all_queues_ready() {
    cache_mark_everything_ready(cache_il1, sim_cycle);
    (cache_dl1 != cache_il1) ? cache_mark_everything_ready(cache_dl1, sim_cycle) : 0;
    cache_mark_everything_ready(cache_il2, sim_cycle);
    (cache_il2 != cache_dl2) ? cache_mark_everything_ready(cache_dl2, sim_cycle) : 0;
    cache_mark_everything_ready(itlb, sim_cycle);
    cache_mark_everything_ready(dtlb, sim_cycle);
}


void switch_to_fast_forwarding() {
   if (! (simulator_state == NOT_STARTED ||	simulator_state == DRAINING)) {
   	  fatal("We should only switch to fast forwarding from NOT_STARTED or DRAINING state.");
   }
   halt_fetch = 0;
   simulator_state = FAST_FORWARDING;

}
void switch_to_warming() {
   if (! (simulator_state == FAST_FORWARDING)) {
   	  fatal("We should only switch to WARMING from FAST_FORWARDING state.");
   }
   simulator_state = WARMING;
   mark_all_queues_ready();
}

long long unit_start_cycle = 0;
long long unit_start_insn = 0;

void start_measuring() {
   if (! (simulator_state == FAST_FORWARDING || simulator_state == WARMING )) {
   	  fatal("We should only switch to MEASURING from FAST_FORWARDING or WARMING state.");
   }
   simulator_state = MEASURING;
   simoo_restore_stats();
   unit_start_cycle = sim_meas_cycle;
   unit_start_insn = sim_meas_insn;


}
void stop_measuring() {
   int cycles_measured;
   int insn_measured;
   double total_cycle_power_cc3;
   double last_total_cycle_power_cc3;
   double power_measured;
   double cpi;
   double epi;
   /* unsigned long insn_measured = 0; */
   if (! (simulator_state == MEASURING )) {
   	  fatal("We should only switch to DRAINING from MEASURING state.");
   }


   simulator_state = DRAINING;
   /* Log an IPC sample */
   cycles_measured = (int) (sim_meas_cycle - unit_start_cycle);
   insn_measured = (int) (sim_meas_insn - unit_start_insn);


#  if CMU_WATTCH_ENABLED
   total_cycle_power_cc3 = rename_power_cc3 + bpred_power_cc3 + lsq_power_cc3 + window_power_cc3 + regfile_power_cc3 + icache_power_cc3 + resultbus_power_cc3 + clock_power_cc3 + alu_power_cc3 + dcache_power_cc3 + dcache2_power_cc3;
   last_total_cycle_power_cc3 = power_stats_backup->rename_power_cc3 + power_stats_backup->bpred_power_cc3 + power_stats_backup->lsq_power_cc3 + power_stats_backup->window_power_cc3 + power_stats_backup->regfile_power_cc3 + power_stats_backup->icache_power_cc3 + power_stats_backup->resultbus_power_cc3 + power_stats_backup->clock_power_cc3 + power_stats_backup->alu_power_cc3 + power_stats_backup->dcache_power_cc3 + power_stats_backup->dcache2_power_cc3;
   power_measured = total_cycle_power_cc3 - last_total_cycle_power_cc3;
#  endif //CMU_WATTCH_ENABLED


   if (insn_measured > 0) {
     cpi = ((double)cycles_measured) / insn_measured;
     stat_update_mean(sample_CPI_mean, cpi);
#  if CMU_WATTCH_ENABLED
     epi = ((double)power_measured) / insn_measured;
     stat_update_mean(sample_EPI_mean, epi);
#  endif //CMU_WATTCH_ENABLED

      //fprintf(stderr, "  -%lld %d cycles %d instructions %f CPI, %f PwrPI\n", sim_pop_insn, cycles_measured, insn_measured, ((double)cycles_measured) / insn_measured, ((double)power_measured) / insn_measured);

     if (paired_out_file != NULL) {
        fprintf(paired_out_file, "%f\n", cpi);
     }

     if (paired_in_file != NULL) {
       double delta = cpi - paired_next_val;
       stat_update_deltamean(paired_delta_mean, paired_next_val, delta);
     }

   }


   sim_sample_size++;
   simoo_backup_stats();

   halt_fetch = 1;
}
