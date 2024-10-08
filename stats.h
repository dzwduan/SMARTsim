/*
 * stats.h - statistical package interfaces
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
 * distributor accepts any responsibility for the conseqauences of its
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

#ifndef STAT_H
#define STAT_H

#include <stdio.h>

#include "host.h"
#include "machine.h"
#include "eval.h"

/*
 * The stats package is a uniform module for handling statistical variables,
 * including counters, distributions, and expressions.  The user must first
 * create a stats database using stat_new(), then statical counters are added
 * to the database using the *_reg_*() functions.  Interfaces are included to
 * allocate and manipulate distributions (histograms) and general expression
 * of other statistical variables constants.  Statistical variables can be
 * located by name using stat_find_stat().  And, statistics can be print in
 * a highly standardized and stylized fashion using stat_print_stats().
 */

/* stat variable classes */
enum stat_class_t {
  sc_int = 0,			/* integer stat */
  sc_uint,			/* unsigned integer stat */
#ifdef HOST_HAS_QWORD
  sc_qword,			/* qword integer stat */
  sc_sqword,			/* signed qword integer stat */
#endif /* HOST_HAS_QWORD */
  sc_float,			/* single-precision FP stat */
  sc_double,			/* double-precision FP stat */
  sc_dist,			/* array distribution stat */
  sc_sdist,			/* sparse array distribution stat */
  sc_formula,			/* stat expression formula */
  sc_mean,                      /* stat for sampled mean of a value */
  sc_deltamean,                 /* stat for paired-comparison relative change */
  sc_NUM
};

/* sparse array distributions are implemented with a hash table */
#define HTAB_SZ			1024
#define HTAB_HASH(I)		((((I) >> 8) ^ (I)) & (HTAB_SZ - 1))

/* hash table bucket definition */
struct bucket_t {
  struct bucket_t *next;	/* pointer to the next bucket */
  md_addr_t index;		/* bucket index - as large as an addr */
  counter_t count;		/* bucket count */
};

/* forward declaration */
struct stat_stat_t;

/* enable distribution components:  index, count, probability, cumulative */
#define PF_COUNT		0x0001
#define PF_PDF			0x0002
#define PF_CDF			0x0004
#define PF_ALL			(PF_COUNT|PF_PDF|PF_CDF)

/* user-defined print function, if this option is selected, a function of this
   form is called for each bucket in the distribution, in ascending index
   order */
typedef void
(*print_fn_t)(struct stat_stat_t *stat,	/* the stat variable being printed */
	      md_addr_t index,		/* entry index to print */
	      counter_t count,		/* entry count */
	      double sum,		/* cumulative sum */
	      double total);		/* total count for distribution */

/* statistical variable definition */
struct stat_stat_t {
  struct stat_stat_t *next;	/* pointer to next stat in database list */
  char *name;			/* stat name */
  char *desc;			/* stat description */
  char *format;			/* stat output print format */
  enum stat_class_t sc;		/* stat class */
  union stat_variant_t {
    /* sc == sc_int */
    struct stat_for_int_t {
      int *var;			/* integer stat variable */
      int init_val;		/* initial integer value */
    } for_int;
    /* sc == sc_uint */
    struct stat_for_uint_t {
      unsigned int *var;	/* unsigned integer stat variable */
      unsigned int init_val;	/* initial unsigned integer value */
    } for_uint;
#ifdef HOST_HAS_QWORD
    /* sc == sc_qword */
    struct stat_for_qword_t {
      qword_t *var;		/* qword integer stat variable */
      qword_t init_val;		/* qword integer value */
    } for_qword;
    /* sc == sc_sqword */
    struct stat_for_sqword_t {
      sqword_t *var;		/* signed qword integer stat variable */
      sqword_t init_val;	/* signed qword integer value */
    } for_sqword;
#endif /* HOST_HAS_QWORD */
    /* sc == sc_float */
    struct stat_for_float_t {
      float *var;		/* float stat variable */
      float init_val;		/* initial float value */
    } for_float;
    /* sc == sc_double */
    struct stat_for_double_t {
      double *var;		/* double stat variable */
      double init_val;		/* initial double value */
    } for_double;
    /* sc == sc_dist */
    struct stat_for_dist_t {
      counter_t init_val;	/* initial dist value */
      counter_t *arr;	/* non-sparse array pointer */
      counter_t arr_sz;	/* array size */
      counter_t bucket_sz;	/* array bucket size */
      int pf;			/* printables */
      char **imap;		/* index -> string map */
      print_fn_t print_fn;	/* optional user-specified print fn */
      counter_t overflows;	/* total overflows in stat_add_samples() */
    } for_dist;
    /* sc == sc_sdist */
    struct stat_for_sdist_t {
      counter_t init_val;	/* initial dist value */
      struct bucket_t **sarr;	/* sparse array pointer */
      int pf;			/* printables */
      print_fn_t print_fn;	/* optional user-specified print fn */
    } for_sdist;
    /* sc == sc_formula */
    struct stat_for_formula_t {
      char *formula;		/* stat formula, see eval.h for format */
    } for_formula;
    /* sc == sc_mean */
    struct stat_for_mean_t {
      double mean;
      double sumSq;
      double Z;
      double targetErrorPct;
      int count;
    } for_mean;
    /* sc == sc_deltamean */
    struct stat_for_deltamean_t {
      double mean;
      double sumSq;
      double Z;
      double targetErrorPct;
      int count;
      double basemean;
    } for_deltamean;
  } variant;
};

/* statistical database */
struct stat_sdb_t {
  struct stat_stat_t *stats;		/* list of stats in database */
  struct eval_state_t *evaluator;	/* an expression evaluator */
};

/* evaluate a stat as an expression */
struct eval_value_t
stat_eval_ident(struct eval_state_t *es);/* expression stat to evaluate */

/* create a new stats database */
struct stat_sdb_t *stat_new(void);

/* delete a stats database */
void
stat_delete(struct stat_sdb_t *sdb);	/* stats database */

/* register an integer statistical variable */
struct stat_stat_t *
stat_reg_int(struct stat_sdb_t *sdb,	/* stat database */
	     char *name,		/* stat variable name */
	     char *desc,		/* stat variable description */
	     int *var,			/* stat variable */
	     int init_val,		/* stat variable initial value */
	     char *format);		/* optional variable output format */

/* register an unsigned integer statistical variable */
struct stat_stat_t *
stat_reg_uint(struct stat_sdb_t *sdb,	/* stat database */
	      char *name,		/* stat variable name */
	      char *desc,		/* stat variable description */
	      unsigned int *var,	/* stat variable */
	      unsigned int init_val,	/* stat variable initial value */
	      char *format);		/* optional variable output format */

#ifdef HOST_HAS_QWORD
/* register a qword integer statistical variable */
struct stat_stat_t *
stat_reg_qword(struct stat_sdb_t *sdb,	/* stat database */
	       char *name,		/* stat variable name */
	       char *desc,		/* stat variable description */
	       qword_t *var,		/* stat variable */
	       qword_t init_val,	/* stat variable initial value */
	       char *format);		/* optional variable output format */

/* register a signed qword integer statistical variable */
struct stat_stat_t *
stat_reg_sqword(struct stat_sdb_t *sdb,	/* stat database */
		char *name,		/* stat variable name */
		char *desc,		/* stat variable description */
		sqword_t *var,		/* stat variable */
		sqword_t init_val,	/* stat variable initial value */
		char *format);		/* optional variable output format */
#endif /* HOST_HAS_QWORD */

/* register a float statistical variable */
struct stat_stat_t *
stat_reg_float(struct stat_sdb_t *sdb,	/* stat database */
	       char *name,		/* stat variable name */
	       char *desc,		/* stat variable description */
	       float *var,		/* stat variable */
	       float init_val,		/* stat variable initial value */
	       char *format);		/* optional variable output format */

/* register a double statistical variable */
struct stat_stat_t *
stat_reg_double(struct stat_sdb_t *sdb,	/* stat database */
		char *name,		/* stat variable name */
		char *desc,		/* stat variable description */
		double *var,		/* stat variable */
		double init_val,	/* stat variable initial value */
		char *format);		/* optional variable output format */

static const double k997_Z = 3.0;
static const double k95_Z = 1.96;

/* register a sampled mean variable */
struct stat_stat_t *
stat_reg_mean(struct stat_sdb_t *sdb,	/* stat database */
		char *name,		/* stat variable name */
		char *desc,		/* stat variable description */
		double Z,               /* Z-score for confidence calculations */
		double desiredEpsilon,  /* target epsilon for n-tuned calculation */
		char *format);	        /* optional variable output format.  Note that it needs to have 2 double format codes */

/* register a paired-comparison mean variable */
struct stat_stat_t *
stat_reg_deltamean(struct stat_sdb_t *sdb,/* stat database */
		char *name,		/* stat variable name */
		char *desc,		/* stat variable description */
		double Z,               /* Z-score for confidence calculations */
		double desiredEpsilon,  /* target epsilon for n-tuned calculation */
		char *format);	        /* optional variable output format.  Note that it needs to have 2 double format codes */

void
stat_update_mean(struct stat_stat_t *stat,/* stat variable */
		 double value);	          /* value to include in mean&stdev */

void
stat_update_deltamean(struct stat_stat_t *stat,  /* stat variable */
		 double absolute, double delta); /* value to include in mean&stdev */


/* create an array distribution (w/ fixed size buckets) in stat database SDB,
   the array distribution has ARR_SZ buckets with BUCKET_SZ indicies in each
   bucked, PF specifies the distribution components to print for optional
   format FORMAT; the indicies may be optionally replaced with the strings
   from IMAP, or the entire distribution can be printed with the optional
   user-specified print function PRINT_FN */
struct stat_stat_t *
stat_reg_dist(struct stat_sdb_t *sdb,	/* stat database */
	      char *name,		/* stat variable name */
	      char *desc,		/* stat variable description */
	      counter_t init_val,	/* dist initial value */
	      counter_t arr_sz,	/* array size */
	      counter_t bucket_sz,	/* array bucket size */
	      int pf,			/* print format, use PF_* defs */
	      char *format,		/* optional variable output format */
	      char **imap,		/* optional index -> string map */
	      print_fn_t print_fn);	/* optional user print function */

/* create a sparse array distribution in stat database SDB, while the sparse
   array consumes more memory per bucket than an array distribution, it can
   efficiently map any number of indicies from 0 to 2^32-1, PF specifies the
   distribution components to print for optional format FORMAT; the indicies
   may be optionally replaced with the strings from IMAP, or the entire
   distribution can be printed with the optional user-specified print function
   PRINT_FN */
struct stat_stat_t *
stat_reg_sdist(struct stat_sdb_t *sdb,	/* stat database */
	       char *name,		/* stat variable name */
	       char *desc,		/* stat variable description */
	       counter_t init_val,	/* dist initial value */
	       int pf,			/* print format, use PF_* defs */
	       char *format,		/* optional variable output format */
	       print_fn_t print_fn);	/* optional user print function */

/* add NSAMPLES to array or sparse array distribution STAT */
void
stat_add_samples(struct stat_stat_t *stat,/* stat database */
		 md_addr_t index,	/* distribution index of samples */
		 counter_t nsamples);		/* number of samples to add to dist */

/* add a single sample to array or sparse array distribution STAT */
void
stat_add_sample(struct stat_stat_t *stat,/* stat variable */
		md_addr_t index);	/* index of sample */

/* register a double statistical formula, the formula is evaluated when the
   statistic is printed, the formula expression may reference any registered
   statistical variable and, in addition, the standard operators '(', ')', '+',
   '-', '*', and '/', and literal (i.e., C-format decimal, hexidecimal, and
   octal) constants are also supported; NOTE: all terms are immediately
   converted to double values and the result is a double value, see eval.h
   for more information on formulas */
struct stat_stat_t *
stat_reg_formula(struct stat_sdb_t *sdb,/* stat database */
		 char *name,		/* stat variable name */
		 char *desc,		/* stat variable description */
		 char *formula,		/* formula expression */
		 char *format);		/* optional variable output format */

/* print the value of stat variable STAT */
void
stat_print_stat(struct stat_sdb_t *sdb,	/* stat database */
		struct stat_stat_t *stat,/* stat variable */
		FILE *fd);		/* output stream */

/* print the value of all stat variables in stat database SDB */
void
stat_print_stats(struct stat_sdb_t *sdb,/* stat database */
		 FILE *fd);		/* output stream */


/* find a stat variable, returns NULL if it is not found */
struct stat_stat_t *
stat_find_stat(struct stat_sdb_t *sdb,	/* stat database */
	       char *stat_name);	/* stat name */

#endif /* STAT_H */
