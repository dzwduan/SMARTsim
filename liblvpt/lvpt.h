/*
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

#ifndef LVPT_H
#define LVPT_H

#include <stdio.h>

struct CkptEntry;

#ifdef __cplusplus
extern "C" {
#endif

#include "../host.h"
#include "../misc.h"
#include "../machine.h"
#include "../regs.h"
#include "../memory.h"
#include "../loader.h"
#include "../syscall.h"
#include "../sim.h"
#include "../endian.h"
#include "../cache.h"
#include "../bpred.h"


extern long long ckptCompressedSize;
extern long long ckptUncompressedSize;
extern long long ckptRegsSize;
extern long long ckptIL1Size;
extern long long ckptDL1Size;
extern long long ckptUL2Size;
extern long long ckptITLBSize;
extern long long ckptDTLBSize;
extern long long ckptBPredSize;
extern long long ckptDataSize;
extern long long ckptSyscallSize;

/* Checkpoiny state data structure */
  static const int kReader = 0;
  static const int kWriter = 1;

  typedef struct ckpt_state {
    FILE * theFile;
    int theDir;
    struct regs_t *   theRegs;
    struct mem_t *    theMem;
    struct cache_t *  theIl1;
    struct cache_t *  theDl1;
    struct cache_t *  theUl2;
    struct cache_t *  theItlb;
    struct cache_t *  theDtlb;
    struct bpred_t *  theBpred;

    unsigned char * theCkptBuffer;
    int theCkptBufferSize;
    struct CkptEntry * theCkptEntry;
    struct SyscallUpdate * theSyscall;

    bool_t theInCkpt;

    counter_t theStartInsn;
    counter_t theL2Misses;
    counter_t theSyscalls;
    counter_t theSyscallMemWrites;
  } ckpt_state ;


/* Methods for manipulating checkpoint files */
  ckpt_state * ckpt_open_reader( char * fname);

   void ckpt_connect_structures
    ( ckpt_state *      aCkpt
    , struct regs_t *   aRegs
    , struct mem_t *    aMem
    , struct cache_t *  aIl1
    , struct cache_t *  aDl1
    , struct cache_t *  aUl2
    , struct cache_t *  aItlb
    , struct cache_t *  aDtlb
    , struct bpred_t *  aBpred
    );

  ckpt_state * ckpt_open_writer
    ( char *            fname
    , struct regs_t *   aRegs
    , struct mem_t *    aMem
    , struct cache_t *  aIl1
    , struct cache_t *  aDl1
    , struct cache_t *  aUl2
    , struct cache_t *  aItlb
    , struct cache_t *  aDtlb
    , struct bpred_t *  aBpred
    );

  void ckpt_close(ckpt_state * aState);

/* Methods used to create checkpoints */
  void ckpt_write_header(ckpt_state * aWriter, char const * aBenchmark, int aU, int aW, char const * aCfg);

  void ckpt_begin(ckpt_state * aWriter, counter_t aStartInsn, int aWriteCaches);

  void ckpt_finish(counter_t anEndInsn);

  void ckpt_l2_miss( enum mem_cmd cmd, md_addr_t addr, int aSize);

  void ckpt_syscall(
    counter_t icnt,		  	        /* instruction count */
		struct regs_t *regs,		      /* registers to update */
		mem_access_fn mem_fn,		      /* generic memory accessor */
		struct mem_t *mem,		        /* memory to update */
		md_inst_t inst);			        /* system call inst */

  enum md_fault_type ckpt_syscall_access_handler(struct mem_t *mem,		/* memory space to access */
	  enum mem_cmd cmd,		                                            /* Read (from sim mem) or Write */
	  md_addr_t addr,		                                              /* target address to access */
	  void *vp,			                                                  /* host memory address to access */
	  int nbytes);			                                              /* number of bytes to access */

/* Methods used to replay checkpoints */
  void ckpt_read_header(ckpt_state * aReader, int print_info, unsigned long long exec_window_size);

  bool_t ckpt_more_ckpts(ckpt_state * aReader);

  void ckpt_dorestore_ckpt(ckpt_state * aReader);

  void ckpt_restore_ckpt(ckpt_state * aReader);

  bool_t ckpt_is_value_known(md_addr_t anAddr);

  void ckpt_skip_ckpts(ckpt_state * aReader, int aCount);

  void ckpt_replay_syscall(
    counter_t icnt,		  	        /* instruction count */
		struct regs_t *regs,		      /* registers to update */
		mem_access_fn mem_fn,		      /* generic memory accessor */
		struct mem_t *mem,		        /* memory to update */
		md_inst_t inst);			        /* system call inst */



#ifdef __cplusplus
} //End extern "C"
#endif

#endif /* LVPT_H */
