/*
 * dump.c - pulled some methods from sim-outorder.c to improve compile time
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


/* dump the contents of the RUU */
static void
ruu_dumpent(struct RUU_station *rs,             /* ptr to RUU station */
            int index,                          /* entry index */
            FILE *stream,                       /* output stream */
            int header)                         /* print header? */
{
  if (!stream)
    stream = stderr;

  if (header)
    fprintf(stream, "idx: %2d: opcode: %s, inst: `",
            index, MD_OP_NAME(rs->op));
  else
    fprintf(stream, "       opcode: %s, inst: `",
            MD_OP_NAME(rs->op));
  md_print_insn(rs->IR, rs->PC, stream);
  fprintf(stream, "'\n");
  myfprintf(stream, "         PC: 0x%08p, NPC: 0x%08p (pred_PC: 0x%08p)\n",
            rs->PC, rs->next_PC, rs->pred_PC);
  fprintf(stream, "         in_LSQ: %s, ea_comp: %s, recover_inst: %s\n",
          rs->in_LSQ ? "t" : "f",
          rs->ea_comp ? "t" : "f",
          rs->recover_inst ? "t" : "f");
  myfprintf(stream, "         spec_mode: %s, addr: 0x%08p, tag: 0x%08x\n",
            rs->spec_mode ? "t" : "f", rs->addr, rs->tag);
  fprintf(stream, "         seq: 0x%08x, ptrace_seq: 0x%08x\n",
          rs->seq, rs->ptrace_seq);
  fprintf(stream, "         queued: %s, issued: %s, completed: %s\n",
          rs->queued ? "t" : "f",
          rs->issued ? "t" : "f",
          rs->completed ? "t" : "f");
  fprintf(stream, "         operands ready: %s\n",
          OPERANDS_READY(rs) ? "t" : "f");
}

/* dump the contents of the RUU */
static void
ruu_dump(FILE *stream)                          /* output stream */
{
  int num, head;
  struct RUU_station *rs;

  if (!stream)
    stream = stderr;

  fprintf(stream, "** RUU state **\n");
  fprintf(stream, "RUU_head: %d, RUU_tail: %d\n", RUU_head, RUU_tail);
  fprintf(stream, "RUU_num: %d\n", RUU_num);

  num = RUU_num;
  head = RUU_head;
  while (num)
    {
      rs = &RUU[head];
      ruu_dumpent(rs, rs - RUU, stream, /* header */TRUE);
      head = (head + 1) & RUU_size_mask;
      num--;
    }
}

/* dump the contents of the RUU */
static void
lsq_dump(FILE *stream)                          /* output stream */
{
  int num, head;
  struct RUU_station *rs;

  if (!stream)
    stream = stderr;

  fprintf(stream, "** LSQ state **\n");
  fprintf(stream, "LSQ_head: %d, LSQ_tail: %d\n", LSQ_head, LSQ_tail);
  fprintf(stream, "LSQ_num: %d\n", LSQ_num);

  num = LSQ_num;
  head = LSQ_head;
  while (num)
    {
      rs = &LSQ[head];
      ruu_dumpent(rs, rs - LSQ, stream, /* header */TRUE);
      head = (head + 1) & LSQ_size_mask;
      num--;
    }
}

/* dump the contents of the event queue */
static void
eventq_dump(FILE *stream)                       /* output stream */
{
  struct RS_link *ev;

  if (!stream)
    stream = stderr;

  fprintf(stream, "** event queue state **\n");

  for (ev = event_queue; ev != NULL; ev = ev->next)
    {
      /* is event still valid? */
      if (RSLINK_VALID(ev))
        {
          struct RUU_station *rs = RSLINK_RS(ev);

          fprintf(stream, "idx: %2d: @ %.0f\n",
                  (int)(rs - (rs->in_LSQ ? LSQ : RUU)), (double)ev->x.when);
          ruu_dumpent(rs, rs - (rs->in_LSQ ? LSQ : RUU),
                      stream, /* !header */FALSE);
        }
    }
}

/* dump the contents of the ready queue */
static void
readyq_dump(FILE *stream)                       /* output stream */
{
  struct RS_link *link;

  if (!stream)
    stream = stderr;

  fprintf(stream, "** ready queue state **\n");

  for (link = ready_queue; link != NULL; link = link->next)
    {
      /* is entry still valid? */
      if (RSLINK_VALID(link))
        {
          struct RUU_station *rs = RSLINK_RS(link);

          ruu_dumpent(rs, rs - (rs->in_LSQ ? LSQ : RUU),
                      stream, /* header */TRUE);
        }
    }
}

/* dump the contents of the create vector */
static void
cv_dump(FILE *stream)                           /* output stream */
{
  int i;
  struct CV_link ent;

  if (!stream)
    stream = stderr;

  fprintf(stream, "** create vector state **\n");

  for (i=0; i < MD_TOTAL_REGS; i++)
    {
      ent = CREATE_VECTOR(i);
      if (!ent.rs)
        fprintf(stream, "[cv%02d]: from architected reg file\n", i);
      else
        fprintf(stream, "[cv%02d]: from %s, idx: %d\n",
                i, (ent.rs->in_LSQ ? "LSQ" : "RUU"),
                (int)(ent.rs - (ent.rs->in_LSQ ? LSQ : RUU)));
    }
}

/* dump speculative register state */
static void
rspec_dump(FILE *stream)                        /* output stream */
{
  int i;

  if (!stream)
    stream = stderr;

  fprintf(stream, "** speculative register contents **\n");

  fprintf(stream, "spec_mode: %s\n", spec_mode ? "t" : "f");

  /* dump speculative integer regs */
  for (i=0; i < MD_NUM_IREGS; i++)
    {
      if (BITMAP_SET_P(use_spec_R, R_BMAP_SZ, i))
        {
          md_print_ireg(spec_regs_R, i, stream);
          fprintf(stream, "\n");
        }
    }

  /* dump speculative FP regs */
  for (i=0; i < MD_NUM_FREGS; i++)
    {
      if (BITMAP_SET_P(use_spec_F, F_BMAP_SZ, i))
        {
          md_print_fpreg(spec_regs_F, i, stream);
          fprintf(stream, "\n");
        }
    }

  /* dump speculative CTRL regs */
  for (i=0; i < MD_NUM_CREGS; i++)
    {
      if (BITMAP_SET_P(use_spec_C, C_BMAP_SZ, i))
        {
          md_print_creg(spec_regs_C, i, stream);
          fprintf(stream, "\n");
        }
    }
}

/* dump speculative memory state */
static void
mspec_dump(FILE *stream)                        /* output stream */
{
  int i;
  struct spec_mem_ent *ent;

  if (!stream)
    stream = stderr;

  fprintf(stream, "** speculative memory contents **\n");

  fprintf(stream, "spec_mode: %s\n", spec_mode ? "t" : "f");

  for (i=0; i<STORE_HASH_SIZE; i++)
    {
      /* dump contents of all hash table buckets */
      for (ent=store_htable[i]; ent; ent=ent->next)
        {
          myfprintf(stream, "[0x%08p]: %12.0f/0x%08x:%08x\n",
                    ent->addr, (double)(*((double *)ent->data)),
                    *((unsigned int *)&ent->data[0]),
                    *(((unsigned int *)&ent->data[0]) + 1));
        }
    }
}

/* default memory state accessor, used by DLite */
static char *                                   /* err str, NULL for no err */
simoo_mem_obj(struct mem_t *mem,                /* memory space to access */
              int is_write,                     /* access type */
              md_addr_t addr,                   /* address to access */
              char *p,                          /* input/output buffer */
              int nbytes)                       /* size of access */
{
  enum mem_cmd cmd;

  if (!is_write)
    cmd = Read;
  else
    cmd = Write;


  /* else, no error, access memory */
  if (spec_mode)
    spec_mem_access(mem, cmd, addr, p, nbytes);
  else
    mem_access(mem, cmd, addr, p, nbytes);

  /* no error */
  return NULL;
}

/* dump contents of fetch stage registers and fetch queue */
void
fetch_dump(FILE *stream)                        /* output stream */
{
  int num, head;

  if (!stream)
    stream = stderr;

  fprintf(stream, "** fetch stage state **\n");

  fprintf(stream, "spec_mode: %s\n", spec_mode ? "t" : "f");
  myfprintf(stream, "pred_PC: 0x%08p, recover_PC: 0x%08p\n",
            pred_PC, recover_PC);
  myfprintf(stream, "fetch_regs_PC: 0x%08p, fetch_pred_PC: 0x%08p\n",
            fetch_regs_PC, fetch_pred_PC);
  fprintf(stream, "\n");

  fprintf(stream, "** fetch queue contents **\n");
  fprintf(stream, "fetch_num: %d\n", fetch_num);
  fprintf(stream, "fetch_head: %d, fetch_tail: %d\n",
          fetch_head, fetch_tail);

  num = fetch_num;
  head = fetch_head;
  while (num)
    {
      fprintf(stream, "idx: %2d: inst: `", head);
      md_print_insn(fetch_data[head].IR, fetch_data[head].regs_PC, stream);
      fprintf(stream, "'\n");
      myfprintf(stream, "         regs_PC: 0x%08p, pred_PC: 0x%08p\n",
                fetch_data[head].regs_PC, fetch_data[head].pred_PC);
      head = (head + 1) & (ruu_ifq_size - 1);
      num--;
    }
}

/* default machine state accessor, used by DLite */
static char *                                   /* err str, NULL for no err */
simoo_mstate_obj(FILE *stream,                  /* output stream */
                 char *cmd,                     /* optional command string */
                 struct regs_t *regs,           /* registers to access */
                 struct mem_t *mem)             /* memory space to access */
{
  if (!cmd || !strcmp(cmd, "help"))
    fprintf(stream,
"mstate commands:\n"
"\n"
"    mstate help   - show all machine-specific commands (this list)\n"
"    mstate stats  - dump all statistical variables\n"
"    mstate res    - dump current functional unit resource states\n"
"    mstate ruu    - dump contents of the register update unit\n"
"    mstate lsq    - dump contents of the load/store queue\n"
"    mstate eventq - dump contents of event queue\n"
"    mstate readyq - dump contents of ready instruction queue\n"
"    mstate cv     - dump contents of the register create vector\n"
"    mstate rspec  - dump contents of speculative regs\n"
"    mstate mspec  - dump contents of speculative memory\n"
"    mstate fetch  - dump contents of fetch stage registers and fetch queue\n"
"\n"
            );
  else if (!strcmp(cmd, "stats"))
    {
      /* just dump intermediate stats */
      sim_print_stats(stream);
    }
  else if (!strcmp(cmd, "res"))
    {
      /* dump resource state */
      res_dump(fu_pool, stream);
    }
  else if (!strcmp(cmd, "ruu"))
    {
      /* dump RUU contents */
      ruu_dump(stream);
    }
  else if (!strcmp(cmd, "lsq"))
    {
      /* dump LSQ contents */
      lsq_dump(stream);
    }
  else if (!strcmp(cmd, "eventq"))
    {
      /* dump event queue contents */
      eventq_dump(stream);
    }
  else if (!strcmp(cmd, "readyq"))
    {
      /* dump event queue contents */
      readyq_dump(stream);
    }
  else if (!strcmp(cmd, "cv"))
    {
      /* dump event queue contents */
      cv_dump(stream);
    }
  else if (!strcmp(cmd, "rspec"))
    {
      /* dump event queue contents */
      rspec_dump(stream);
    }
  else if (!strcmp(cmd, "mspec"))
    {
      /* dump event queue contents */
      mspec_dump(stream);
    }
  else if (!strcmp(cmd, "fetch"))
    {
      /* dump event queue contents */
      fetch_dump(stream);
    }
  else
    return "unknown mstate command";

  /* no error */
  return NULL;
}

/* default register state accessor, used by DLite */
static char *					/* err str, NULL for no err */
simoo_reg_obj(struct regs_t *xregs,		/* registers to access */
	      int is_write,			/* access type */
	      enum md_reg_type rt,		/* reg bank to probe */
	      int reg,				/* register number */
	      struct eval_value_t *val)		/* input, output */
{
  switch (rt)
    {
    case rt_gpr:
      if (reg < 0 || reg >= MD_NUM_IREGS)
	return "register number out of range";

      if (!is_write)
	{
	  val->type = et_uint;
	  val->value.as_uint = GPR(reg);
	}
      else
	SET_GPR(reg, eval_as_uint(*val));
      break;

    case rt_lpr:
      if (reg < 0 || reg >= MD_NUM_FREGS)
	return "register number out of range";

      /* FIXME: this is not portable... */
      abort();
      break;

    case rt_fpr:
      /* FIXME: this is not portable... */
      abort();
      break;

    case rt_dpr:
      /* FIXME: this is not portable... */
      abort();
      break;

      /* FIXME: this is not portable... */

    case rt_PC:
      if (!is_write)
	{
	  val->type = et_addr;
	  val->value.as_addr = regs.regs_PC;
	}
      else
	regs.regs_PC = eval_as_addr(*val);
      break;

    case rt_NPC:
      if (!is_write)
	{
	  val->type = et_addr;
	  val->value.as_addr = regs.regs_NPC;
	}
      else
	regs.regs_NPC = eval_as_addr(*val);
      break;

    default:
      panic("bogus register bank");
    }

  /* no error */
  return NULL;
}

 