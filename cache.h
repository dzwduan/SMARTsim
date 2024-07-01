/*
 * cache.h - cache module interfaces
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

#ifndef CACHE_H
#define CACHE_H

#include <stdio.h>

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "memory.h"
#include "stats.h"

/*
 * This module contains code to implement various cache-like structures.  The
 * user instantiates caches using cache_new().  When instantiated, the user
 * may specify the geometry of the cache (i.e., number of set, line size,
 * associativity), and supply a block access function.  The block access
 * function indicates the latency to access lines when the cache misses,
 * accounting for any component of miss latency, e.g., bus acquire latency,
 * bus transfer latency, memory access latency, etc...  In addition, the user
 * may allocate the cache with or without lines allocated in the cache.
 * Caches without tags are useful when implementing structures that map data
 * other than the address space, e.g., TLBs which map the virtual address
 * space to physical page address, or BTBs which map text addresses to
 * branch prediction state.  Tags are always allocated.  User data may also be
 * optionally attached to cache lines, this space is useful to storing
 * auxilliary or additional cache line information, such as predecode data,
 * physical page address information, etc...
 *
 * The caches implemented by this module provide efficient storage management
 * and fast access for all cache geometries.  When sets become highly
 * associative, a hash table (indexed by address) is allocated for each set
 * in the cache.
 *
 * This module also tracks latency of accessing the data cache, each cache has
 * a hit latency defined when instantiated, miss latency is returned by the
 * cache's block access function, the caches may service any number of hits
 * under any number of misses, the calling simulator should limit the number
 * of outstanding misses or the number of hits under misses as per the
 * limitations of the particular microarchitecture being simulated.
 *
 * Due to the organization of this cache implementation, the latency of a
 * request cannot be affected by a later request to this module.  As a result,
 * reordering of requests in the memory hierarchy is not possible.
 */

/* highly associative caches are implemented using a hash table lookup to
   speed block access, this macro decides if a cache is "highly associative" */
#define CACHE_HIGHLY_ASSOC(cp)	((cp)->assoc > 4)

/* cache replacement policy */
enum cache_policy {
  LRU,		/* replace least recently used block (perfect LRU) */
  Random,	/* replace a random block */
  FIFO		/* replace the oldest block in the set */
};

/* block status values */
#define CACHE_BLK_VALID		0x00000001	/* block in valid, in use */
#define CACHE_BLK_DIRTY		0x00000002	/* dirty block */

#define WRONGPATH_FLAG          1
#define STORE_FLAG              2

/* cache block (or line) definition */
struct cache_blk_t
{
  struct cache_blk_t *way_next;	/* next block in the ordered way chain, used
				   to order blocks for replacement */
  struct cache_blk_t *way_prev;	/* previous block in the order way chain */
  struct cache_blk_t *hash_next;/* next block in the hash bucket chain, only
				   used in highly-associative caches */
  /* since hash table lists are typically small, there is no previous
     pointer, deletion requires a trip through the hash table bucket list */
  md_addr_t tag;		/* data block tag value */
  unsigned int status;		/* block status, see CACHE_BLK_* defs above */
  tick_t ready;		/* time when block will be accessible, field
				   is set when a miss fetch is initiated */
  byte_t *user_data;		/* pointer to user defined data, e.g.,
				   pre-decode data or physical page address */
  /* DATA should be pointer-aligned due to preceeding field */
  /* NOTE: this is a variable-size tail array, this must be the LAST field
     defined in this structure! */
  byte_t data[1];		/* actual data block starts here, block size
				   should probably be a multiple of 8 */
};

/* cache set definition (one or more blocks sharing the same set index) */
struct cache_set_t
{
  struct cache_blk_t **hash;	/* hash table: for fast access w/assoc, NULL
				   for low-assoc caches */
  struct cache_blk_t *way_head;	/* head of way list */
  struct cache_blk_t *way_tail;	/* tail pf way list */
  struct cache_blk_t *blks;	/* cache blocks, allocated sequentially, so
				   this pointer can also be used for random
				   access to cache blocks */
};

/* mshr */
#define MAX_MSHRS 8
#define MAX_TARGET 4
#define MSHR_FULL -1
#define MSHR_TARGET_FULL -2

struct cache_mshr
{
  md_addr_t tagset;		/* data block tagset value */
  unsigned int status;	/* block status, to allow clean and dirty to combine */
  unsigned int ntarget;	/* # targets for secondary misses */
  tick_t ready;
};

/* structure for backing up cache statistics */
struct cache_stats_t {
  counter_t hits;
  counter_t misses;
  counter_t replacements;
  counter_t writebacks;
  counter_t invalidations;
  counter_t mshr_hits;
  counter_t mshr_misses;
  counter_t mshr_full;
  counter_t mshr_full_stall;
  counter_t mshr_target_full;
  counter_t mshr_target_full_stall;

  counter_t wrong_path_hits;
  counter_t wrong_path_misses;
  counter_t wrong_path_replacements;
  counter_t wrong_path_writebacks;

  counter_t store_misses;

};

/* cache definition */
struct cache_t
{
  /* parameters */
  char *name;			/* cache name */
  int nsets;			/* number of sets */
  int bsize;			/* block size in bytes */
  int balloc;			/* maintain cache contents? */
  int usize;			/* user allocated data size */
  int assoc;			/* cache associativity */
  enum cache_policy policy;	/* cache replacement policy */
  unsigned int hit_latency;	/* cache hit latency */

  /* mshr */
  tick_t ready;		/* indicates when cache ready for new op if all mshr full */
  int num_mshrs;
  int num_mshr_targets;
  struct cache_mshr *mshr;

  /* pipelined L2 cache */
  int nlev_issuelat;	/* issue latency of next level cache */

  /* miss/replacement handler, read/write BSIZE bytes starting at BADDR
     from/into cache block BLK, returns the latency of the operation
     if initiated at NOW, returned latencies indicate how long it takes
     for the cache access to continue (e.g., fill a write buffer), the
     miss/repl functions are required to track how this operation will
     effect the latency of later operations (e.g., write buffer fills),
     if !BALLOC, then just return the latency; BLK_ACCESS_FN is also
     responsible for generating any user data and incorporating the latency
     of that operation */
  unsigned int					/* latency of block access */
    (*blk_access_fn)(enum mem_cmd cmd,		/* block access command */
		     md_addr_t baddr,		/* program address to access */
		     int bsize,			/* size of the cache block */
		     struct cache_blk_t *blk,	/* ptr to cache block struct */
		     tick_t now, /* when fetch was initiated */
		     int stat_flags);

  /* derived data, for fast decoding */
  int hsize;			/* cache set hash table size */
  md_addr_t blk_mask;
  int set_shift;
  md_addr_t set_mask;		/* use *after* shift */
  int tag_shift;
  md_addr_t tag_mask;		/* use *after* shift */
  md_addr_t tagset_mask;	/* used for fast hit detection */

  /* bus resource */
  tick_t bus_free;		/* time when bus to next level of cache is
				   free, NOTE: the bus model assumes only a
				   single, fully-pipelined port to the next
 				   level of memory that requires the bus only
 				   one cycle for cache line transfer (the
 				   latency of the access to the lower level
 				   may be more than one cycle, as specified
 				   by the miss handler */

  /* pipelined L2 cache */
  tick_t bus_open;		/* sum of time bus is not occupied */

  /* per-cache stats */
  counter_t hits;		/* total number of hits */
  counter_t misses;		/* total number of misses */
  counter_t replacements;	/* total number of replacements at misses */
  counter_t writebacks;		/* total number of writebacks at misses */
  counter_t invalidations;	/* total number of external invalidations */

  counter_t wrong_path_hits;
  counter_t wrong_path_misses;
  counter_t wrong_path_replacements;
  counter_t wrong_path_writebacks;

  /* mshr: stats */
  counter_t mshr_hits;
  counter_t mshr_misses;
  counter_t mshr_full;
  counter_t mshr_full_stall;
  counter_t mshr_target_full;
  counter_t mshr_target_full_stall;

  counter_t store_misses;

  struct cache_stats_t *backup;

  /* last block to hit, used to optimize cache hit processing */
  md_addr_t last_tagset;	/* tag of last line accessed */
  struct cache_blk_t *last_blk;	/* cache block last accessed */

  /* data blocks */
  byte_t *data;			/* pointer to data blocks allocation */

  /* NOTE: this is a variable-size tail array, this must be the LAST field
     defined in this structure! */
  struct cache_set_t sets[1];	/* each entry is a set */
};

/* mshr: cache create with mshrs */
struct cache_t *                      /* pointer to cache created */
cache_create_mshr(char *name,         /* name of the cache */
                  int nsets,          /* total number of sets in cache */
                  int bsize,          /* block (line) size of cache */
                  int balloc,         /* allocate data space for blocks? */
                  int usize,          /* size of user data to alloc w/blks */
                  int mshrs,                 /* # mshrs */
                  int mshr_targets,          /* # targets for each mshr */
                  int assoc,                 /* associativity of cache */
                  enum cache_policy policy,  /* replacement policy w/in sets */
                  /* block access function, see description w/in struct cache def */
                  unsigned int (*blk_access_fn)(enum mem_cmd cmd,
                                                md_addr_t baddr, int bsize,
                                                struct cache_blk_t *blk,
                                                tick_t now,
                                                int stat_flags),
                  unsigned int hit_latency);/* latency in cycles for a hit */

/* create and initialize a general cache structure */
struct cache_t *			/* pointer to cache created */
cache_create(char *name,		/* name of the cache */
	     int nsets,			/* total number of sets in cache */
	     int bsize,			/* block (line) size of cache */
	     int balloc,		/* allocate data space for blocks? */
	     int usize,			/* size of user data to alloc w/blks */
	     int assoc,			/* associativity of cache */
	     enum cache_policy policy,	/* replacement policy w/in sets */
	     /* block access function, see description w/in struct cache def */
	     unsigned int (*blk_access_fn)(enum mem_cmd cmd,
					   md_addr_t baddr, int bsize,
					   struct cache_blk_t *blk,
					   tick_t now,
					   int stat_flags),
	     unsigned int hit_latency);/* latency in cycles for a hit */

/* parse policy */
enum cache_policy			/* replacement policy enum */
cache_char2policy(char c);		/* replacement policy as a char */

/* print cache configuration */
void
cache_config(struct cache_t *cp,	/* cache instance */
	     FILE *stream);		/* output stream */

/* register cache stats */
void
cache_reg_stats(struct cache_t *cp,	/* cache instance */
		struct stat_sdb_t *sdb);/* stats database */

/* print cache stats */
void
cache_stats(struct cache_t *cp,		/* cache instance */
	    FILE *stream);		/* output stream */

/* print cache stats */
void cache_stats(struct cache_t *cp, FILE *stream);

/* mshr */
int count_mshr(struct cache_t *cp, tick_t now);


/* access a cache, perform a CMD operation on cache CP at address ADDR,
   places NBYTES of data at *P, returns latency of operation if initiated
   at NOW, places pointer to block user data in *UDATA, *P is untouched if
   cache blocks are not allocated (!CP->BALLOC), UDATA should be NULL if no
   user data is attached to blocks */
int				/* latency of access in cycles */
cache_access(struct cache_t *cp,	/* cache to access */
	     enum mem_cmd cmd,		/* access type, Read or Write */
	     md_addr_t addr,		/* address of access */
	     void *vp,			/* ptr to buffer for input/output */
	     tick_t *mem_ready,	        /* ptr to mem_ready of ruu_station */
	     int nbytes,		/* number of bytes to access */
	     tick_t now,		/* time of access */
	     byte_t **udata,		/* for return of user data ptr */
	     md_addr_t *repl_addr, /* for address of replaced block */
	     int is_wrongpath);


/* return non-zero if block containing address ADDR is contained in cache
   CP, this interface is used primarily for debugging and asserting cache
   invariants */
int					/* non-zero if access would hit */
cache_probe(struct cache_t *cp,		/* cache instance to probe */
	    md_addr_t addr);		/* address of block to probe */

/* flush the entire cache, returns latency of the operation */
unsigned int				/* latency of the flush operation */
cache_flush(struct cache_t *cp,		/* cache instance to flush */
	    tick_t now);		/* time of cache flush */

/* flush the block containing ADDR from the cache CP, returns the latency of
   the block flush operation */
unsigned int				/* latency of flush operation */
cache_flush_addr(struct cache_t *cp,	/* cache instance to flush */
		 md_addr_t addr,	/* address of block to flush */
		 tick_t now);		/* time of cache flush */

/* SAMPLING SUPPORT CODE */

/* Called to copy cache statistics into a "backup" buffer after measurement */
void cache_backup_stats(struct cache_t *cp);

/* Called to copy cache statistics into a "backup" buffer after measurement */
void cache_alloc_stats(struct cache_t *cp);

/* Called to copy cache statistics from a "backup" buffer before measurement */
void cache_restore_stats(struct cache_t *cp);

/* Called before warming to clean out all the queues in the memory system*/
void cache_mark_everything_ready(struct cache_t *cp, tick_t now);

bool_t icache_extremely_fast_access(struct cache_t *cp,	/* cache to access */
	md_addr_t addr,								/* address of access */
  counter_t cnt);

void * l2_fast_access(
	enum mem_cmd cmd,							/* access type, Read or Write */
	md_addr_t addr,								/* address of access */
  counter_t cnt);


bool_t dl1_extremely_fast_access(
	enum mem_cmd cmd,							/* access type, Read or Write */
	md_addr_t addr,								/* address of access */
        counter_t cnt);

void * dtlb_extremely_fast_access(struct cache_t *cp,	                        /* cache to access */
	md_addr_t addr,								/* address of access */
        counter_t cnt);

bool_t cache_fast_access(
  struct cache_t *cp,	          /* cache to access */
	enum mem_cmd cmd,							/* access type, Read or Write */
	md_addr_t addr);								/* address of access */

void cache_clear(struct cache_t *cp);



/* level 1 instruction cache, entry level instruction cache */
extern struct cache_t *cache_il1;

/* level 1 instruction cache */
extern struct cache_t *cache_il2;

/* level 1 data cache, entry level data cache */
extern struct cache_t *cache_dl1;

/* level 2 data cache */
extern struct cache_t *cache_dl2;

/* instruction TLB */
extern struct cache_t *itlb;

/* data TLB */
extern struct cache_t *dtlb;


#endif /* CACHE_H */
