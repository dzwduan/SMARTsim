/*
 * stats.c - statistical package routines
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

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "eval.h"
#include "stats.h"

/* evaluate a stat as an expression */
struct eval_value_t
stat_eval_ident(struct eval_state_t *es)/* an expression evaluator */
{
  struct stat_sdb_t *sdb = es->user_ptr;
  struct stat_stat_t *stat;
  static struct eval_value_t err_value = { et_int, { 0 } };
  struct eval_value_t val;

  /* locate the stat variable */
  for (stat = sdb->stats; stat != NULL; stat = stat->next)
    {
      if (!strcmp(stat->name, es->tok_buf))
	{
	  /* found it! */
	  break;
	}
    }
  if (!stat)
    {
      /* could not find stat variable */
      eval_error = ERR_UNDEFVAR;
      return err_value;
    }
  /* else, return the value of stat */

  /* convert the stat variable value to a typed expression value */
  switch (stat->sc)
    {
    case sc_int:
      val.type = et_int;
      val.value.as_int = *stat->variant.for_int.var;
      break;
    case sc_uint:
      val.type = et_uint;
      val.value.as_uint = *stat->variant.for_uint.var;
      break;
#ifdef HOST_HAS_QWORD
    case sc_qword:
      /* FIXME: cast to double, eval package doesn't support long long's */
      val.type = et_double;
#ifdef _MSC_VER /* FIXME: MSC does not implement qword_t to dbl conversion */
      val.value.as_double = (double)(sqword_t)*stat->variant.for_qword.var;
#else /* !_MSC_VER */
      val.value.as_double = (double)*stat->variant.for_qword.var;
#endif /* _MSC_VER */
      break;
    case sc_sqword:
      /* FIXME: cast to double, eval package doesn't support long long's */
      val.type = et_double;
      val.value.as_double = (double)*stat->variant.for_sqword.var;
      break;
#endif /* HOST_HAS_QWORD */
    case sc_float:
      val.type = et_float;
      val.value.as_float = *stat->variant.for_float.var;
      break;
    case sc_double:
      val.type = et_double;
      val.value.as_double = *stat->variant.for_double.var;
      break;
    case sc_dist:
    case sc_sdist:
      fatal("stat distributions not allowed in formula expressions");
      break;
    case sc_formula:
      {
	/* instantiate a new evaluator to avoid recursion problems */
	struct eval_state_t *es = eval_new(stat_eval_ident, sdb);
	char *endp;

	val = eval_expr(es, stat->variant.for_formula.formula, &endp);
	if (eval_error != ERR_NOERR || *endp != '\0')
	  {
	    /* pass through eval_error */
	    val = err_value;
	  }
	/* else, use value returned */
	eval_delete(es);
      }
      break;
    case sc_mean:
      val.type = et_double;
      val.value.as_double = stat->variant.for_mean.mean;
      break;
    case sc_deltamean:
      val.type = et_double;
      val.value.as_double = stat->variant.for_deltamean.mean / stat->variant.for_deltamean.basemean;
      break;
    default:
      panic("bogus stat class");
    }

  return val;
}

/* create a new stats database */
struct stat_sdb_t *
stat_new(void)
{
  struct stat_sdb_t *sdb;

  sdb = (struct stat_sdb_t *)calloc(1, sizeof(struct stat_sdb_t));
  if (!sdb)
    fatal("out of virtual memory");

  sdb->stats = NULL;
  sdb->evaluator = eval_new(stat_eval_ident, sdb);

  return sdb;
}

/* delete a stats database */
void
stat_delete(struct stat_sdb_t *sdb)	/* stats database */
{
  int i;
  struct stat_stat_t *stat, *stat_next;
  struct bucket_t *bucket, *bucket_next;

  /* free all individual stat variables */
  for (stat = sdb->stats; stat != NULL; stat = stat_next)
    {
      stat_next = stat->next;
      stat->next = NULL;

      /* free stat */
      switch (stat->sc)
	{
	case sc_int:
	case sc_uint:
#ifdef HOST_HAS_QWORD
	case sc_qword:
	case sc_sqword:
#endif /* HOST_HAS_QWORD */
	case sc_float:
	case sc_double:
	case sc_formula:
	  /* no other storage to deallocate */
	  break;
	case sc_dist:
	  /* free distribution array */
	  free(stat->variant.for_dist.arr);
	  stat->variant.for_dist.arr = NULL;
	  break;
	case sc_sdist:
	  /* free all hash table buckets */
	  for (i=0; i<HTAB_SZ; i++)
	    {
	      for (bucket = stat->variant.for_sdist.sarr[i];
		   bucket != NULL;
		   bucket = bucket_next)
		{
		  bucket_next = bucket->next;
		  bucket->next = NULL;
		  free(bucket);
		}
	      stat->variant.for_sdist.sarr[i] = NULL;
	    }
	  /* free hash table array */
	  free(stat->variant.for_sdist.sarr);
	  stat->variant.for_sdist.sarr = NULL;
	  break;
	default:
	  panic("bogus stat class");
	}
      /* free stat variable record */
      free(stat);
    }
  sdb->stats = NULL;
  eval_delete(sdb->evaluator);
  sdb->evaluator = NULL;
  free(sdb);
}

/* add stat variable STAT to stat database SDB */
static void
add_stat(struct stat_sdb_t *sdb,	/* stat database */
	 struct stat_stat_t *stat)	/* stat variable */
{
  struct stat_stat_t *elt, *prev;

  /* append at end of stat database list */
  for (prev=NULL, elt=sdb->stats; elt != NULL; prev=elt, elt=elt->next)
    /* nada */;

  /* append stat to stats chain */
  if (prev != NULL)
    prev->next = stat;
  else /* prev == NULL */
    sdb->stats = stat;
  stat->next = NULL;
}

/* register an integer statistical variable */
struct stat_stat_t *
stat_reg_int(struct stat_sdb_t *sdb,	/* stat database */
	     char *name,		/* stat variable name */
	     char *desc,		/* stat variable description */
	     int *var,			/* stat variable */
	     int init_val,		/* stat variable initial value */
	     char *format)		/* optional variable output format */
{
  struct stat_stat_t *stat;

  stat = (struct stat_stat_t *)calloc(1, sizeof(struct stat_stat_t));
  if (!stat)
    fatal("out of virtual memory");

  stat->name = mystrdup(name);
  stat->desc = mystrdup(desc);
  stat->format = format ? format : "%12d";
  stat->sc = sc_int;
  stat->variant.for_int.var = var;
  stat->variant.for_int.init_val = init_val;

  /* link onto SDB chain */
  add_stat(sdb, stat);

  /* initialize stat */
  *var = init_val;

  return stat;
}

/* register an unsigned integer statistical variable */
struct stat_stat_t *
stat_reg_uint(struct stat_sdb_t *sdb,	/* stat database */
	      char *name,		/* stat variable name */
	      char *desc,		/* stat variable description */
	      unsigned int *var,	/* stat variable */
	      unsigned int init_val,	/* stat variable initial value */
	      char *format)		/* optional variable output format */
{
  struct stat_stat_t *stat;

  stat = (struct stat_stat_t *)calloc(1, sizeof(struct stat_stat_t));
  if (!stat)
    fatal("out of virtual memory");

  stat->name = mystrdup(name);
  stat->desc = mystrdup(desc);
  stat->format = format ? format : "%12u";
  stat->sc = sc_uint;
  stat->variant.for_uint.var = var;
  stat->variant.for_uint.init_val = init_val;

  /* link onto SDB chain */
  add_stat(sdb, stat);

  /* initialize stat */
  *var = init_val;

  return stat;
}

#ifdef HOST_HAS_QWORD
/* register a qword integer statistical variable */
struct stat_stat_t *
stat_reg_qword(struct stat_sdb_t *sdb,	/* stat database */
	      char *name,		/* stat variable name */
	      char *desc,		/* stat variable description */
	      qword_t *var,		/* stat variable */
	      qword_t init_val,		/* stat variable initial value */
	      char *format)		/* optional variable output format */
{
  struct stat_stat_t *stat;

  stat = (struct stat_stat_t *)calloc(1, sizeof(struct stat_stat_t));
  if (!stat)
    fatal("out of virtual memory");

  stat->name = mystrdup(name);
  stat->desc = mystrdup(desc);
  stat->format = format ? format : "%12lu";
  stat->sc = sc_qword;
  stat->variant.for_qword.var = var;
  stat->variant.for_qword.init_val = init_val;

  /* link onto SDB chain */
  add_stat(sdb, stat);

  /* initialize stat */
  *var = init_val;

  return stat;
}

/* register a signed qword integer statistical variable */
struct stat_stat_t *
stat_reg_sqword(struct stat_sdb_t *sdb,	/* stat database */
	       char *name,		/* stat variable name */
	       char *desc,		/* stat variable description */
	       sqword_t *var,		/* stat variable */
	       sqword_t init_val,	/* stat variable initial value */
	       char *format)		/* optional variable output format */
{
  struct stat_stat_t *stat;

  stat = (struct stat_stat_t *)calloc(1, sizeof(struct stat_stat_t));
  if (!stat)
    fatal("out of virtual memory");

  stat->name = mystrdup(name);
  stat->desc = mystrdup(desc);
  stat->format = format ? format : "%12ld";
  stat->sc = sc_sqword;
  stat->variant.for_sqword.var = var;
  stat->variant.for_sqword.init_val = init_val;

  /* link onto SDB chain */
  add_stat(sdb, stat);

  /* initialize stat */
  *var = init_val;

  return stat;
}
#endif /* HOST_HAS_QWORD */

/* register a float statistical variable */
struct stat_stat_t *
stat_reg_float(struct stat_sdb_t *sdb,	/* stat database */
	       char *name,		/* stat variable name */
	       char *desc,		/* stat variable description */
	       float *var,		/* stat variable */
	       float init_val,		/* stat variable initial value */
	       char *format)		/* optional variable output format */
{
  struct stat_stat_t *stat;

  stat = (struct stat_stat_t *)calloc(1, sizeof(struct stat_stat_t));
  if (!stat)
    fatal("out of virtual memory");

  stat->name = mystrdup(name);
  stat->desc = mystrdup(desc);
  stat->format = format ? format : "%12.4f";
  stat->sc = sc_float;
  stat->variant.for_float.var = var;
  stat->variant.for_float.init_val = init_val;

  /* link onto SDB chain */
  add_stat(sdb, stat);

  /* initialize stat */
  *var = init_val;

  return stat;
}

/* register a double statistical variable */
struct stat_stat_t *
stat_reg_double(struct stat_sdb_t *sdb,	/* stat database */
		char *name,		/* stat variable name */
		char *desc,		/* stat variable description */
		double *var,		/* stat variable */
		double init_val,	/* stat variable initial value */
		char *format)		/* optional variable output format */
{
  struct stat_stat_t *stat;

  stat = (struct stat_stat_t *)calloc(1, sizeof(struct stat_stat_t));
  if (!stat)
    fatal("out of virtual memory");

  stat->name = mystrdup(name);
  stat->desc = mystrdup(desc);
  stat->format = format ? format : "%12.4f";
  stat->sc = sc_double;
  stat->variant.for_double.var = var;
  stat->variant.for_double.init_val = init_val;

  /* link onto SDB chain */
  add_stat(sdb, stat);

  /* initialize stat */
  *var = init_val;

  return stat;
}

/* create an array distribution (w/ fixed size buckets) in stat database SDB,
   the array distribution has ARR_SZ buckets with BUCKET_SZ indicies in each
   bucked, PF specifies the distribution components to print for optional
   format FORMAT; the indicies may be optionally replaced with the strings from
   IMAP, or the entire distribution can be printed with the optional
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
	      print_fn_t print_fn)	/* optional user print function */
{
  counter_t i;
  struct stat_stat_t *stat;
  counter_t *arr;

  stat = (struct stat_stat_t *)calloc(1, sizeof(struct stat_stat_t));
  if (!stat)
    fatal("out of virtual memory");

  stat->name = mystrdup(name);
  stat->desc = mystrdup(desc);
  stat->format = format ? format : NULL;
  stat->sc = sc_dist;
  stat->variant.for_dist.init_val = init_val;
  stat->variant.for_dist.arr_sz = arr_sz;
  stat->variant.for_dist.bucket_sz = bucket_sz;
  stat->variant.for_dist.pf = pf;
  stat->variant.for_dist.imap = imap;
  stat->variant.for_dist.print_fn = print_fn;
  stat->variant.for_dist.overflows = 0;

  arr = (counter_t *)calloc(arr_sz, sizeof(counter_t));
  if (!arr)
    fatal("out of virtual memory");
  stat->variant.for_dist.arr = arr;

  /* link onto SDB chain */
  add_stat(sdb, stat);

  /* initialize stat */
  for (i=0; i < arr_sz; i++)
    arr[i] = init_val;

  return stat;
}

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
	       print_fn_t print_fn)	/* optional user print function */
{
  struct stat_stat_t *stat;
  struct bucket_t **sarr;

  stat = (struct stat_stat_t *)calloc(1, sizeof(struct stat_stat_t));
  if (!stat)
    fatal("out of virtual memory");

  stat->name = mystrdup(name);
  stat->desc = mystrdup(desc);
  stat->format = format ? format : NULL;
  stat->sc = sc_sdist;
  stat->variant.for_sdist.init_val = init_val;
  stat->variant.for_sdist.pf = pf;
  stat->variant.for_sdist.print_fn = print_fn;

  /* allocate hash table */
  sarr = (struct bucket_t **)calloc(HTAB_SZ, sizeof(struct bucket_t *));
  if (!sarr)
    fatal("out of virtual memory");
  stat->variant.for_sdist.sarr = sarr;

  /* link onto SDB chain */
  add_stat(sdb, stat);

  return stat;
}

/* add NSAMPLES to array or sparse array distribution STAT */
void
stat_add_samples(struct stat_stat_t *stat,/* stat database */
		 md_addr_t index,	/* distribution index of samples */
		 counter_t nsamples)		/* number of samples to add to dist */
{
  switch (stat->sc)
    {
    case sc_dist:
      {
	unsigned int i;

	/* compute array index */
	i = index / stat->variant.for_dist.bucket_sz;

	/* check for overflow */
	if (i >= stat->variant.for_dist.arr_sz)
	  stat->variant.for_dist.overflows += nsamples;
	else
	  stat->variant.for_dist.arr[i] += nsamples;
      }
      break;
    case sc_sdist:
      {
	struct bucket_t *bucket;
	int hash = HTAB_HASH(index);

	if (hash < 0 || hash >= HTAB_SZ)
	  panic("hash table index overflow");

	/* find bucket */
	for (bucket = stat->variant.for_sdist.sarr[hash];
	     bucket != NULL;
	     bucket = bucket->next)
	  {
	    if (bucket->index == index)
	      break;
	  }
	if (!bucket)
	  {
	    /* add a new sample bucket */
	    bucket = (struct bucket_t *)calloc(1, sizeof(struct bucket_t));
	    if (!bucket)
	      fatal("out of virtual memory");
	    bucket->next = stat->variant.for_sdist.sarr[hash];
	    stat->variant.for_sdist.sarr[hash] = bucket;
	    bucket->index = index;
	    bucket->count = stat->variant.for_sdist.init_val;
	  }
	bucket->count += nsamples;
      }
      break;
    default:
      panic("stat variable is not an array distribution");
    }
}

/* add a single sample to array or sparse array distribution STAT */
void
stat_add_sample(struct stat_stat_t *stat,/* stat variable */
		md_addr_t index)	/* index of sample */
{
  stat_add_samples(stat, index, 1);
}

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
		 char *format)		/* optional variable output format */
{
  struct stat_stat_t *stat;

  stat = (struct stat_stat_t *)calloc(1, sizeof(struct stat_stat_t));
  if (!stat)
    fatal("out of virtual memory");

  stat->name = mystrdup(name);
  stat->desc = mystrdup(desc);
  stat->format = format ? format : "%12.4f";
  stat->sc = sc_formula;
  stat->variant.for_formula.formula = mystrdup(formula);

  /* link onto SDB chain */
  add_stat(sdb, stat);

  return stat;
}

/* register a sampled mean */
struct stat_stat_t *
stat_reg_mean(struct stat_sdb_t *sdb,   /* stat database */
		 char *name,		/* stat variable name */
		 char *desc,		/* stat variable description */
		 double Z,		/* Z value to use for confidence calculations */
		 double desired_error,  /* Target % error */
		 char *format)		/* optional variable output format */
{
  struct stat_stat_t *stat;

  stat = (struct stat_stat_t *)calloc(1, sizeof(struct stat_stat_t));
  if (!stat)
    fatal("out of virtual memory");

  stat->name = mystrdup(name);
  stat->desc = mystrdup(desc);
  stat->format = format ? format : "%12.4f +/- %2.2f%%";
  stat->sc = sc_mean;
  stat->variant.for_mean.mean = 0.0;
  stat->variant.for_mean.sumSq = 0.0;
  stat->variant.for_mean.Z = Z;
  stat->variant.for_mean.targetErrorPct = desired_error;
  stat->variant.for_mean.count = 0;

  /* link onto SDB chain */
  add_stat(sdb, stat);

  return stat;
}

struct stat_stat_t *
stat_reg_deltamean(struct stat_sdb_t *sdb,   /* stat database */
		 char *name,		/* stat variable name */
		 char *desc,		/* stat variable description */
		 double Z,		/* Z value to use for confidence calculations */
		 double desired_error,  /* Target % error */
		 char *format)		/* optional variable output format */
{
  struct stat_stat_t *stat;

  stat = (struct stat_stat_t *)calloc(1, sizeof(struct stat_stat_t));
  if (!stat)
    fatal("out of virtual memory");

  stat->name = mystrdup(name);
  stat->desc = mystrdup(desc);
  stat->format = format ? format : "%+11.2f%% +/- %2.2f%%";
  stat->sc = sc_deltamean;
  stat->variant.for_deltamean.mean = 0.0;
  stat->variant.for_deltamean.sumSq = 0.0;
  stat->variant.for_deltamean.Z = Z;
  stat->variant.for_deltamean.targetErrorPct = desired_error;
  stat->variant.for_deltamean.count = 0;
  stat->variant.for_deltamean.basemean = 0;

  /* link onto SDB chain */
  add_stat(sdb, stat);

  return stat;
}

/* call once per sample to update running total for stdev calculation */
void stat_update_mean(struct stat_stat_t *stat, double value) {
  double d;
  if (stat->sc != sc_mean) {
    fatal("Can't call stat_update_mean on non-mean stats");
  }
  d = (value) - stat->variant.for_mean.mean;
  stat->variant.for_mean.mean += d / (stat->variant.for_mean.count + 1);
  if (stat->variant.for_mean.count > 0) {
    stat->variant.for_mean.sumSq += (d*d) * stat->variant.for_mean.count / ( stat->variant.for_mean.count + 1);
  } else {
    stat->variant.for_mean.sumSq = 0;
  }
  stat->variant.for_mean.count++;
}

void stat_update_deltamean(struct stat_stat_t *stat, double base, double delta) {
  double abs_d;
  double delta_d;
  if (stat->sc != sc_deltamean) {
    fatal("Can't call stat_update_mean on non-mean stats");
  }
  delta_d = (delta) - stat->variant.for_deltamean.mean;
  abs_d = (base) - stat->variant.for_deltamean.basemean;
  stat->variant.for_deltamean.mean += delta_d / (stat->variant.for_deltamean.count + 1);
  stat->variant.for_deltamean.basemean += abs_d / (stat->variant.for_deltamean.count + 1);
  if (stat->variant.for_deltamean.count > 0) {
    stat->variant.for_deltamean.sumSq += (delta_d*delta_d) * stat->variant.for_deltamean.count / ( stat->variant.for_deltamean.count + 1);
  } else {
    stat->variant.for_deltamean.sumSq = 0;
  }
  stat->variant.for_deltamean.count++;
}


/* compare two indicies in a sparse array hash table, used by qsort() */
static int
compare_fn(void *p1, void *p2)
{
  struct bucket_t **pb1 = p1, **pb2 = p2;

  /* compare indices */
  if ((*pb1)->index < (*pb2)->index)
    return -1;
  else if ((*pb1)->index > (*pb2)->index)
    return 1;
  else /* ((*pb1)->index == (*pb2)->index) */
    return 0;
}

/* print an array distribution */
static void
print_dist(struct stat_stat_t *stat,	/* stat variable */
	   FILE *fd)			/* output stream */
{
  counter_t i, bcount, imax, imin;
  double btotal, bsum, bvar, bavg, bsqsum;
  int pf = stat->variant.for_dist.pf;

  /* count and sum entries */
  bcount = 0; btotal = 0.0; bvar = 0.0; bsqsum = 0.0;
  imax = 0; imin = UINT_MAX;
  for (i=0; i<stat->variant.for_dist.arr_sz; i++)
    {
      bcount++;
      btotal += stat->variant.for_dist.arr[i];
      /* on-line variance computation, tres cool, no!?! */
      bsqsum += ((double)stat->variant.for_dist.arr[i] *
		 (double)stat->variant.for_dist.arr[i]);
      bavg = btotal / MAX((double)bcount, 1.0);
      bvar = (bsqsum - ((double)bcount * bavg * bavg)) /
	(double)(((bcount - 1) > 0) ? (bcount - 1) : 1);
    }

  /* print header */
  fprintf(fd, "\n");
  fprintf(fd, "%-22s # %s\n", stat->name, stat->desc);
  fprintf(fd, "%s.array_size = %lld\n",
	  stat->name, stat->variant.for_dist.arr_sz);
  fprintf(fd, "%s.bucket_size = %lld\n",
	  stat->name, stat->variant.for_dist.bucket_sz);

  fprintf(fd, "%s.count = %lld\n", stat->name, bcount);
  fprintf(fd, "%s.total = %.0f\n", stat->name, btotal);
  if (bcount > 0)
    {
      fprintf(fd, "%s.imin = %u\n", stat->name, 0U);
      fprintf(fd, "%s.imax = %lld\n", stat->name, bcount);
    }
  else
    {
      fprintf(fd, "%s.imin = %d\n", stat->name, -1);
      fprintf(fd, "%s.imax = %d\n", stat->name, -1);
    }
  fprintf(fd, "%s.average = %8.4f\n", stat->name, btotal/MAX(bcount, 1.0));
  fprintf(fd, "%s.std_dev = %8.4f\n", stat->name, sqrt(bvar));
  fprintf(fd, "%s.overflows = %lld\n",
	  stat->name, stat->variant.for_dist.overflows);

  fprintf(fd, "# pdf == prob dist fn, cdf == cumulative dist fn\n");
  fprintf(fd, "# %14s ", "index");
  if (pf & PF_COUNT)
    fprintf(fd, "%10s ", "count");
  if (pf & PF_PDF)
    fprintf(fd, "%6s ", "pdf");
  if (pf & PF_CDF)
    fprintf(fd, "%6s ", "cdf");
  fprintf(fd, "\n");

  fprintf(fd, "%s.start_dist\n", stat->name);

  if (bcount > 0)
    {
      /* print the array */
      bsum = 0.0;
      for (i=0; i<bcount; i++)
	{
	  bsum += (double)stat->variant.for_dist.arr[i];
	  if (stat->variant.for_dist.print_fn)
	    {
	      stat->variant.for_dist.print_fn(stat,
					      i,
					      stat->variant.for_dist.arr[i],
					      bsum,
					      btotal);
	    }
	  else
	    {
	      if (stat->format == NULL)
		{
		  if (stat->variant.for_dist.imap)
		    fprintf(fd, "%-16s ", stat->variant.for_dist.imap[i]);
		  else
		    fprintf(fd, "%16lld ",
			    i * stat->variant.for_dist.bucket_sz);
		  if (pf & PF_COUNT)
		    fprintf(fd, "%10lld ", stat->variant.for_dist.arr[i]);
		  if (pf & PF_PDF)
		    fprintf(fd, "%6.2f ",
			    (double)stat->variant.for_dist.arr[i] /
			    MAX(btotal, 1.0) * 100.0);
		  if (pf & PF_CDF)
		    fprintf(fd, "%6.2f ", bsum/MAX(btotal, 1.0) * 100.0);
		}
	      else
		{
		  if (pf == (PF_COUNT|PF_PDF|PF_CDF))
		    {
		      if (stat->variant.for_dist.imap)
		        fprintf(fd, stat->format,
			        stat->variant.for_dist.imap[i],
			        stat->variant.for_dist.arr[i],
			        (double)stat->variant.for_dist.arr[i] /
			        MAX(btotal, 1.0) * 100.0,
			        bsum/MAX(btotal, 1.0) * 100.0);
		      else
		        fprintf(fd, stat->format,
			        i * stat->variant.for_dist.bucket_sz,
			        stat->variant.for_dist.arr[i],
			        (double)stat->variant.for_dist.arr[i] /
			        MAX(btotal, 1.0) * 100.0,
			        bsum/MAX(btotal, 1.0) * 100.0);
		    }
		  else
		    fatal("distribution format not yet implemented");
		}
	      fprintf(fd, "\n");
	    }
	}
    }

  fprintf(fd, "%s.end_dist\n", stat->name);
}

/* print a sparse array distribution */
static void
print_sdist(struct stat_stat_t *stat,	/* stat variable */
	    FILE *fd)			/* output stream */
{
  counter_t i, bcount;
  md_addr_t imax, imin;
  double btotal, bsum, bvar, bavg, bsqsum;
  struct bucket_t *bucket;
  int pf = stat->variant.for_sdist.pf;

  /* count and sum entries */
  bcount = 0; btotal = 0.0; bvar = 0.0; bsqsum = 0.0;
  imax = 0; imin = UINT_MAX;
  for (i=0; i<HTAB_SZ; i++)
    {
      for (bucket = stat->variant.for_sdist.sarr[i];
	   bucket != NULL;
	   bucket = bucket->next)
	{
	  bcount++;
	  btotal += bucket->count;
	  /* on-line variance computation, tres cool, no!?! */
	  bsqsum += ((double)bucket->count * (double)bucket->count);
	  bavg = btotal / (double)bcount;
	  bvar = (bsqsum - ((double)bcount * bavg * bavg)) /
	    (double)(((bcount - 1) > 0) ? (bcount - 1) : 1);
	  if (bucket->index < imin)
	    imin = bucket->index;
	  if (bucket->index > imax)
	    imax = bucket->index;
	}
    }

  /* print header */
  fprintf(fd, "\n");
  fprintf(fd, "%-22s # %s\n", stat->name, stat->desc);
  fprintf(fd, "%s.count = %lld\n", stat->name, bcount);
  fprintf(fd, "%s.total = %.0f\n", stat->name, btotal);
  if (bcount > 0)
    {
      myfprintf(fd, "%s.imin = 0x%p\n", stat->name, imin);
      myfprintf(fd, "%s.imax = 0x%p\n", stat->name, imax);
    }
  else
    {
      fprintf(fd, "%s.imin = %d\n", stat->name, -1);
      fprintf(fd, "%s.imax = %d\n", stat->name, -1);
    }
  fprintf(fd, "%s.average = %8.4f\n", stat->name, btotal/bcount);
  fprintf(fd, "%s.std_dev = %8.4f\n", stat->name, sqrt(bvar));
  fprintf(fd, "%s.overflows = 0\n", stat->name);

  fprintf(fd, "# pdf == prob dist fn, cdf == cumulative dist fn\n");
  fprintf(fd, "# %14s ", "index");
  if (pf & PF_COUNT)
    fprintf(fd, "%10s ", "count");
  if (pf & PF_PDF)
    fprintf(fd, "%6s ", "pdf");
  if (pf & PF_CDF)
    fprintf(fd, "%6s ", "cdf");
  fprintf(fd, "\n");

  fprintf(fd, "%s.start_dist\n", stat->name);

  if (bcount > 0)
    {
      unsigned int bindex;
      struct bucket_t **barr;

      /* collect all buckets */
      barr = (struct bucket_t **)calloc(bcount, sizeof(struct bucket_t *));
      if (!barr)
	fatal("out of virtual memory");
      for (bindex=0,i=0; i<HTAB_SZ; i++)
	{
	  for (bucket = stat->variant.for_sdist.sarr[i];
	       bucket != NULL;
	       bucket = bucket->next)
	    {
	      barr[bindex++] = bucket;
	    }
	}

      /* sort the array by index */
      qsort(barr, bcount, sizeof(struct bucket_t *), (void *)compare_fn);

      /* print the array */
      bsum = 0.0;
      for (i=0; i<bcount; i++)
	{
	  bsum += (double)barr[i]->count;
	  if (stat->variant.for_sdist.print_fn)
	    {
	      stat->variant.for_sdist.print_fn(stat,
					       barr[i]->index,
					       barr[i]->count,
					       bsum,
					       btotal);
	    }
	  else
	    {
	      if (stat->format == NULL)
		{
		  myfprintf(fd, "0x%p ", barr[i]->index);
		  if (pf & PF_COUNT)
		    fprintf(fd, "%10lld ", barr[i]->count);
		  if (pf & PF_PDF)
		    fprintf(fd, "%6.2f ",
			    (double)barr[i]->count/MAX(btotal, 1.0) * 100.0);
		  if (pf & PF_CDF)
		    fprintf(fd, "%6.2f ", bsum/MAX(btotal, 1.0) * 100.0);
		}
	      else
		{
		  if (pf == (PF_COUNT|PF_PDF|PF_CDF))
		    {
		      myfprintf(fd, stat->format,
				barr[i]->index, barr[i]->count,
				(double)barr[i]->count/MAX(btotal, 1.0)*100.0,
				bsum/MAX(btotal, 1.0) * 100.0);
		    }
		  else if (pf == (PF_COUNT|PF_PDF))
		    {
		      myfprintf(fd, stat->format,
				barr[i]->index, barr[i]->count,
				(double)barr[i]->count/MAX(btotal, 1.0)*100.0);
		    }
		  else if (pf == PF_COUNT)
		    {
		      myfprintf(fd, stat->format,
				barr[i]->index, barr[i]->count);
		    }
		  else
		    fatal("distribution format not yet implemented");
		}
	      fprintf(fd, "\n");
	    }
	}

      /* all done, release bucket pointer array */
      free(barr);
    }

  fprintf(fd, "%s.end_dist\n", stat->name);
}

static void
stat_print_mean(FILE *fd,			/* output stream */
                struct stat_stat_t *stat)	/* stat variable */
{
   /* calculate confidence interval */
   double stdev = 0.0;
   double ci = 0.0;
   if (stat->variant.for_mean.count > 1) {
    stdev = sqrt( stat->variant.for_mean.sumSq / ( stat->variant.for_mean.count - 1 ) );
   }
   if (stat->variant.for_mean.count > 0 ) {
     ci = stat->variant.for_mean.Z * stdev / stat->variant.for_mean.mean / sqrt( stat->variant.for_mean.count ) * 100.0;
   }
   /* instantiate a new evaluator to avoid recursion problems */
   fprintf(fd, "%-22s ", stat->name);
   myfprintf(fd, stat->format, stat->variant.for_mean.mean, ci );
   fprintf(fd, " # %s", stat->desc);
   if (stat->variant.for_mean.targetErrorPct > 0) {
     double sqrtn_tuned = stat->variant.for_mean.Z * stdev / stat->variant.for_mean.mean / (stat->variant.for_mean.targetErrorPct / 100.0);
     int n_tuned = sqrtn_tuned * sqrtn_tuned;
     if (n_tuned < 50) {
       n_tuned = 50;
     }
     if ( n_tuned - stat->variant.for_mean.count > 30) {
       fprintf(fd, "\n   N-tuned for %2.2f%% error", stat->variant.for_mean.targetErrorPct);
       myfprintf(fd, "%9d", n_tuned  );
     }
   }

}

static void
stat_print_deltamean(FILE *fd,			/* output stream */
                struct stat_stat_t *stat)	/* stat variable */
{
   /* calculate confidence interval */
   double stdev = 0.0;
   double ci = 0.0;
   if (stat->variant.for_deltamean.count > 1) {
    stdev = sqrt( stat->variant.for_deltamean.sumSq / ( stat->variant.for_mean.count - 1 ) );
   }
   if (stat->variant.for_deltamean.count > 0 ) {
    //strictly speaking, we should use Student's T distribution rather than a Z
    //value from the standard normal curve for this.  However, for sample sizes
    //above 50, the difference between the two is negligible.
     ci = stat->variant.for_deltamean.Z * stdev / stat->variant.for_deltamean.basemean / sqrt( stat->variant.for_deltamean.count ) * 100.0;
   }
   /* instantiate a new evaluator to avoid recursion problems */
   fprintf(fd, "%-22s ", stat->name);
   fprintf(fd, stat->format, stat->variant.for_deltamean.mean / stat->variant.for_deltamean.basemean * 100.0, ci );
   fprintf(fd, " # %s", stat->desc);
   if (stat->variant.for_deltamean.targetErrorPct > 0) {
     double sqrtn_tuned = stat->variant.for_deltamean.Z * stdev / stat->variant.for_deltamean.basemean / (stat->variant.for_deltamean.targetErrorPct / 100.0);
     int n_tuned = sqrtn_tuned * sqrtn_tuned;
     if (n_tuned < 50) {
       n_tuned = 50;
     }
     if ( n_tuned - stat->variant.for_deltamean.count > 30) {
       fprintf(fd, "\n   N-tuned for %2.2f%% error", stat->variant.for_deltamean.targetErrorPct);
       myfprintf(fd, "%9d", n_tuned  );
     }
   }

}

/* print the value of stat variable STAT */
void
stat_print_stat(struct stat_sdb_t *sdb,	/* stat database */
		struct stat_stat_t *stat,/* stat variable */
		FILE *fd)		/* output stream */
{
  struct eval_value_t val;

  switch (stat->sc)
    {
    case sc_int:
      fprintf(fd, "%-22s ", stat->name);
      myfprintf(fd, stat->format, *stat->variant.for_int.var);
      fprintf(fd, " # %s", stat->desc);
      break;
    case sc_uint:
      fprintf(fd, "%-22s ", stat->name);
      myfprintf(fd, stat->format, *stat->variant.for_uint.var);
      fprintf(fd, " # %s", stat->desc);
      break;
#ifdef HOST_HAS_QWORD
    case sc_qword:
      {
	char buf[128];

	fprintf(fd, "%-22s ", stat->name);
	mysprintf(buf, stat->format, *stat->variant.for_qword.var);
	fprintf(fd, "%s # %s", buf, stat->desc);
      }
      break;
    case sc_sqword:
      {
	char buf[128];

	fprintf(fd, "%-22s ", stat->name);
	mysprintf(buf, stat->format, *stat->variant.for_sqword.var);
	fprintf(fd, "%s # %s", buf, stat->desc);
      }
      break;
#endif /* HOST_HAS_QWORD */
    case sc_float:
      fprintf(fd, "%-22s ", stat->name);
      myfprintf(fd, stat->format, (double)*stat->variant.for_float.var);
      fprintf(fd, " # %s", stat->desc);
      break;
    case sc_double:
      fprintf(fd, "%-22s ", stat->name);
      myfprintf(fd, stat->format, *stat->variant.for_double.var);
      fprintf(fd, " # %s", stat->desc);
      break;
    case sc_dist:
      print_dist(stat, fd);
      break;
    case sc_sdist:
      print_sdist(stat, fd);
      break;
    case sc_formula:
      {
	/* instantiate a new evaluator to avoid recursion problems */
	struct eval_state_t *es = eval_new(stat_eval_ident, sdb);
	char *endp;

	fprintf(fd, "%-22s ", stat->name);
	val = eval_expr(es, stat->variant.for_formula.formula, &endp);
	if (eval_error != ERR_NOERR || *endp != '\0')
	  fprintf(fd, "<error: %s>", eval_err_str[eval_error]);
	else
	  myfprintf(fd, stat->format, eval_as_double(val));
	fprintf(fd, " # %s", stat->desc);

	/* done with the evaluator */
	eval_delete(es);
      }
      break;
    case sc_mean:
      stat_print_mean(fd, stat);
      break;
    case sc_deltamean:
      stat_print_deltamean(fd, stat);
      break;
    default:
      panic("bogus stat class");
    }
  fprintf(fd, "\n");
}

/* print the value of all stat variables in stat database SDB */
void
stat_print_stats(struct stat_sdb_t *sdb,/* stat database */
		 FILE *fd)		/* output stream */
{
  struct stat_stat_t *stat;

  if (!sdb)
    {
      /* no stats */
      return;
    }

  for (stat=sdb->stats; stat != NULL; stat=stat->next)
    stat_print_stat(sdb, stat, fd);
}

/* find a stat variable, returns NULL if it is not found */
struct stat_stat_t *
stat_find_stat(struct stat_sdb_t *sdb,	/* stat database */
	       char *stat_name)		/* stat name */
{
  struct stat_stat_t *stat;

  for (stat = sdb->stats; stat != NULL; stat = stat->next)
    {
      if (!strcmp(stat->name, stat_name))
	break;
    }
  return stat;
}

#ifdef TESTIT

void
main(void)
{
  struct stat_sdb_t *sdb;
  struct stat_stat_t *stat, *stat1, *stat2, *stat3, *stat4, *stat5;
  int an_int;
  unsigned int a_uint;
  float a_float;
  double a_double;
  static char *my_imap[8] = {
    "foo", "bar", "uxxe", "blah", "gaga", "dada", "mama", "googoo"
  };

  /* make stats database */
  sdb = stat_new();

  /* register stat variables */
  stat_reg_int(sdb, "stat.an_int", "An integer stat variable.",
	       &an_int, 1, NULL);
  stat_reg_uint(sdb, "stat.a_uint", "An unsigned integer stat variable.",
		&a_uint, 2, "%u (unsigned)");
  stat_reg_float(sdb, "stat.a_float", "A float stat variable.",
		 &a_float, 3, NULL);
  stat_reg_double(sdb, "stat.a_double", "A double stat variable.",
		  &a_double, 4, NULL);
  stat_reg_formula(sdb, "stat.a_formula", "A double stat formula.",
		   "stat.a_float / stat.a_uint", NULL);
  stat_reg_formula(sdb, "stat.a_formula1", "A double stat formula #1.",
		   "2 * (stat.a_formula / (1.5 * stat.an_int))", NULL);
  stat_reg_formula(sdb, "stat.a_bad_formula", "A double stat formula w/error.",
		   "stat.a_float / (stat.a_uint - 2)", NULL);
  stat = stat_reg_dist(sdb, "stat.a_dist", "An array distribution.",
		       0, 8, 1, PF_ALL, NULL, NULL, NULL);
  stat1 = stat_reg_dist(sdb, "stat.a_dist1", "An array distribution #1.",
			0, 8, 4, PF_ALL, NULL, NULL, NULL);
  stat2 = stat_reg_dist(sdb, "stat.a_dist2", "An array distribution #2.",
			0, 8, 1, (PF_PDF|PF_CDF), NULL, NULL, NULL);
  stat3 = stat_reg_dist(sdb, "stat.a_dist3", "An array distribution #3.",
			0, 8, 1, PF_ALL, NULL, my_imap, NULL);
  stat4 = stat_reg_sdist(sdb, "stat.a_sdist", "A sparse array distribution.",
			 0, PF_ALL, NULL, NULL);
  stat5 = stat_reg_sdist(sdb, "stat.a_sdist1",
			 "A sparse array distribution #1.",
			 0, PF_ALL, "0x%08lx        %10lu %6.2f %6.2f",
			 NULL);

  /* print initial stats */
  fprintf(stdout, "** Initial stats...\n");
  stat_print_stats(sdb, stdout);

  /* adjust stats */
  an_int++;
  a_uint++;
  a_float *= 2;
  a_double *= 4;

  stat_add_sample(stat, 8);
  stat_add_sample(stat, 8);
  stat_add_sample(stat, 1);
  stat_add_sample(stat, 3);
  stat_add_sample(stat, 4);
  stat_add_sample(stat, 4);
  stat_add_sample(stat, 7);

  stat_add_sample(stat1, 32);
  stat_add_sample(stat1, 32);
  stat_add_sample(stat1, 1);
  stat_add_sample(stat1, 12);
  stat_add_sample(stat1, 17);
  stat_add_sample(stat1, 18);
  stat_add_sample(stat1, 30);

  stat_add_sample(stat2, 8);
  stat_add_sample(stat2, 8);
  stat_add_sample(stat2, 1);
  stat_add_sample(stat2, 3);
  stat_add_sample(stat2, 4);
  stat_add_sample(stat2, 4);
  stat_add_sample(stat2, 7);

  stat_add_sample(stat3, 8);
  stat_add_sample(stat3, 8);
  stat_add_sample(stat3, 1);
  stat_add_sample(stat3, 3);
  stat_add_sample(stat3, 4);
  stat_add_sample(stat3, 4);
  stat_add_sample(stat3, 7);

  stat_add_sample(stat4, 800);
  stat_add_sample(stat4, 800);
  stat_add_sample(stat4, 1123);
  stat_add_sample(stat4, 3332);
  stat_add_sample(stat4, 4000);
  stat_add_samples(stat4, 4001, 18);
  stat_add_sample(stat4, 7);

  stat_add_sample(stat5, 800);
  stat_add_sample(stat5, 800);
  stat_add_sample(stat5, 1123);
  stat_add_sample(stat5, 3332);
  stat_add_sample(stat5, 4000);
  stat_add_samples(stat5, 4001, 18);
  stat_add_sample(stat5, 7);

  /* print final stats */
  fprintf(stdout, "** Final stats...\n");
  stat_print_stats(sdb, stdout);

  /* all done */
  stat_delete(sdb);
  exit(0);
}

#endif /* TEST */
