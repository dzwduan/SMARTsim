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

#include "lvpt.h"
#include "asn.hpp"

#include <cstdio>
#include <iostream>
#include <iomanip>
#include <zlib.h>
#include <vector>
#include <algorithm>
#include <utility>

ckpt_state * theCurrentCkptState = 0;

/* extern refs to global variables in sim-outorder */

  long long ckptCompressedSize = 0;
  long long ckptUncompressedSize = 0;
  long long ckptRegsSize = 0;
  long long ckptIL1Size = 0;
  long long ckptDL1Size = 0;
  long long ckptUL2Size = 0;
  long long ckptITLBSize = 0;
  long long ckptDTLBSize = 0;
  long long ckptBPredSize = 0;
  long long ckptDataSize = 0;
  long long ckptSyscallSize = 0;


static const int Kb = 1024;
static const int Mb = 1024 * Kb;

static const int kCompressBufferSize = 11 * Mb;
static const int kCkptBufferSize = 10 * Mb;

unsigned char gzbuffer[ kCompressBufferSize ] ;

void ckpt_connect_structures
  ( struct ckpt_state * state
  , struct regs_t *     aRegs
  , struct mem_t *      aMem
  , struct cache_t *    aIl1
  , struct cache_t *    aDl1
  , struct cache_t *    aUl2
  , struct cache_t *    aItlb
  , struct cache_t *    aDtlb
  , struct bpred_t *    aBpred
  ) {

  state->theRegs = aRegs;
  state->theMem = aMem;
  state->theIl1 = aIl1;
  state->theDl1 = aDl1;
  state->theUl2 = aUl2;
  state->theItlb = aItlb;
  state->theDtlb = aDtlb;
  state->theBpred = aBpred;

}


extern "C"
ckpt_state * ckpt_open_reader(char * fname) {

  ckpt_state * state = new ckpt_state();

  state->theFile = fopen(fname, "r");
  if ( ! state->theFile ) {
    std::cout << "ckpt_open_reader() : could not open " << fname << std::endl;
    std::exit(-1);
  }
  state->theDir = kReader;
  state->theInCkpt = FALSE;

  state->theCkptBuffer = new unsigned char[ 10 * Mb ];
  state->theCkptBufferSize = 10 * Mb;

  return state;
}



extern "C"
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
  ) {

  ckpt_state * state = new ckpt_state();

  state->theFile = fopen(fname, "w");
  if ( ! state->theFile ) {
    std::cout << "ckpt_open_writer() : could not open " << fname << std::endl;
    std::exit(-1);
  }
  state->theDir = kWriter;
  state->theInCkpt = FALSE;

  state->theCkptBuffer = new unsigned char[ 10 * Mb ];
  state->theCkptBufferSize = 10 * Mb;

  ckpt_connect_structures(state, aRegs, aMem, aIl1, aDl1, aUl2, aItlb, aDtlb, aBpred);

  return state;
}

extern "C"
void ckpt_close(ckpt_state * aState) {
  if (! aState || !aState->theFile ) {
    std::cout << "ckpt_close() : aState is not valid" << std::endl;
    return;
  }
  fclose(aState->theFile);
  aState->theFile = 0;
  if (aState->theCkptBuffer) {
    delete [] aState->theCkptBuffer;
    aState->theCkptBuffer = 0;
  }
}

extern "C"
void ckpt_write_header(ckpt_state * aWriter, char const * aBenchmark, int aU, int aW, char const * aCfg) {
  unsigned long compressed_size = kCompressBufferSize ;


  if (! aWriter || !aWriter->theFile || aWriter->theDir != kWriter) {
    std::cout << "ckpt_write_header() : aWriter is not valid" << std::endl;
    return;
  }

  CkptHeader whdr;

  whdr.setBenchmark(aBenchmark);
  whdr.setU(aU);
  whdr.setW(aW);
  whdr.setCfg(aCfg);

  int size = whdr.derSize();

  unsigned char * wrt = aWriter->theCkptBuffer;

  whdr.derWrite(wrt);
  compress( gzbuffer, &compressed_size, aWriter->theCkptBuffer, size);
  fwrite( &compressed_size, sizeof(unsigned long), 1, aWriter->theFile );
  fwrite( gzbuffer, compressed_size, 1 ,aWriter->theFile);
}


inline unsigned long long makeAddress(cache_t const * aCache, unsigned long long aTag, int aSet) {
  return  (aTag << aCache->tag_shift) | ( aSet  << aCache->set_shift)  ;
}

inline bool isText(unsigned long long anAddress) {
  return (anAddress >= ld_text_base) && (anAddress < ld_text_base + ld_text_size);
}

void write_address(CkptEntry & anEntry, mem_t * aMem, unsigned long long anAddress) {
  //Align to 128 byte boundary
  anAddress &= ~(127LL);

  //See if the address is data or instruction
  if (! isText(anAddress) ) {
    //We don't include text addresses in the dump
    if (! anEntry.isStored(anAddress)) {
      unsigned char * page = MEM_PAGE(aMem, anAddress);
      if (page != 0) {
        anEntry.store(anAddress);
        unsigned char * host_addr = page + MEM_OFFSET(anAddress);
        MemUpdate & update = anEntry.addMemUpdate();
        update.setAddress(anAddress);
        update.setData(host_addr, 128);
      }
    }
  }
}

static const int kBTBEntrySize = sizeof(md_addr_t) * 2 + sizeof(md_opcode);

std::pair<unsigned char const *, long> ckpt_bpred(bpred_t * bpred) {

  if (bpred->klass == BPredTaken || bpred->klass == BPredNotTaken) {
    return std::make_pair( reinterpret_cast<unsigned char const *>("stateless"), sizeof("stateless"));
  } else if (bpred->klass == BPredComb ) {
    //Calculate the total amount of storage that will be needed for the predictor image
    long size = 2 * sizeof(long); //retstack size and tos
    size += bpred->retstack.size * kBTBEntrySize; //retstack contents
    size += 2 * sizeof(long); //btb sets and assoc
    size += bpred->btb.sets * bpred->btb.assoc * kBTBEntrySize;
    size += sizeof(long); //bimod size
    size += bpred->dirpred.bimod->config.bimod.size;
    size += sizeof(long); //meta size
    size += bpred->dirpred.meta->config.bimod.size;
    size += 5 * sizeof(long); //twolev l1size, l2size, shiftwidth, shiftmask, xor
    size += bpred->dirpred.twolev->config.two.l1size * sizeof(int); //shiftregs
    size += bpred->dirpred.twolev->config.two.l2size; //l2table

    //Copy all the data into the buffer
    unsigned char * bpred_buf = new unsigned char[size];
    unsigned char * bpred_buf_ptr = bpred_buf;

    //Retstack
    //========

      //Retstack size
      * reinterpret_cast<long *>(bpred_buf_ptr) = bpred->retstack.size;
      bpred_buf_ptr += sizeof(long);

      //Retstack tos
      * reinterpret_cast<long *>(bpred_buf_ptr) = bpred->retstack.tos;
      bpred_buf_ptr += sizeof(long);

      //Retstack contents
      for (int i = 0; i < bpred->retstack.size; ++i) {
        memcpy(bpred_buf_ptr, & bpred->retstack.stack[i], kBTBEntrySize);
        bpred_buf_ptr += kBTBEntrySize;
      }

    //BTB
    //===

      //BTB sets
      * reinterpret_cast<long *>(bpred_buf_ptr) = bpred->btb.sets;
      bpred_buf_ptr += sizeof(long);

      //BTB assoc
      * reinterpret_cast<long *>(bpred_buf_ptr) = bpred->btb.assoc;
      bpred_buf_ptr += sizeof(long);

      //Write out the BTB data sorted in LRU order
      for (int i = 0; i < bpred->btb.sets; ++i) {
        //Find the lru chain head within this set
        bpred_btb_ent_t * entry = 0;
        int set_base = i * bpred->btb.assoc;

        for (int j = 0; j < bpred->btb.assoc; ++j) {
          if (bpred->btb.btb_data[set_base + j].prev == 0) {
            entry = & bpred->btb.btb_data[set_base + j];
            break;
          }
        }

        while (entry) {
          memcpy(bpred_buf_ptr, entry, kBTBEntrySize);
          bpred_buf_ptr += kBTBEntrySize;
          entry = entry->next;
        }
      }

    //Bimodal
    //=======
      //Table size
      * reinterpret_cast<long *>(bpred_buf_ptr) = bpred->dirpred.bimod->config.bimod.size;
      bpred_buf_ptr += sizeof(long);

      //Table data
      memcpy(bpred_buf_ptr, bpred->dirpred.bimod->config.bimod.table, bpred->dirpred.bimod->config.bimod.size);
      bpred_buf_ptr += bpred->dirpred.bimod->config.bimod.size;

    //Meta
    //====
      //Table size
      * reinterpret_cast<long *>(bpred_buf_ptr) = bpred->dirpred.meta->config.bimod.size;
      bpred_buf_ptr += sizeof(long);

      //Table data
      memcpy(bpred_buf_ptr, bpred->dirpred.meta->config.bimod.table, bpred->dirpred.meta->config.bimod.size);
      bpred_buf_ptr += bpred->dirpred.meta->config.bimod.size;

    //Two-level
    //=========
      //Table configuration
      memcpy(bpred_buf_ptr, & bpred->dirpred.twolev->config.two.l1size, 5 * sizeof(long) );
      bpred_buf_ptr += 5 * sizeof(long);

      //Shift registers
      memcpy(bpred_buf_ptr, bpred->dirpred.twolev->config.two.shiftregs, bpred->dirpred.twolev->config.two.l1size * sizeof(int));
      bpred_buf_ptr += bpred->dirpred.twolev->config.two.l1size * sizeof(int);

      //L2 table
      memcpy(bpred_buf_ptr, bpred->dirpred.twolev->config.two.l2table, bpred->dirpred.twolev->config.two.l2size);
      bpred_buf_ptr += bpred->dirpred.twolev->config.two.l2size ;

      return std::make_pair( bpred_buf, size);
  } else {
    fatal("Unsupported branch predictor type");
 }
}


std::pair<unsigned char *, long> ckpt_cache(bool aWriteFlag, CkptEntry & anEntry, cache_t * aCache, mem_t * aMem) {
  std::vector< std::pair<long, unsigned long long> > addrs;

  //For all ways of all sets
  for (int set_idx = 0; set_idx < aCache->nsets; ++set_idx) {
    for (int blk_idx = 0; blk_idx < aCache->assoc; ++blk_idx) {
      if (aCache->sets[set_idx].blks[blk_idx].status & CACHE_BLK_VALID) {
        unsigned long long address =
          makeAddress(aCache, aCache->sets[set_idx].blks[blk_idx].tag, set_idx);

        if (aWriteFlag) {
          write_address(anEntry, aMem, address);
        }

        if (aCache->sets[set_idx].blks[blk_idx].status & CACHE_BLK_DIRTY) {
          address |= 1;
        }

        addrs.push_back( std::make_pair( aCache->sets[set_idx].blks[blk_idx].ready, address) );

      }
    }
  }

  std::sort( addrs.begin(), addrs.end() );

  unsigned long long * cache_buf = new unsigned long long [ addrs.size() + 2 ];

  std::vector< std::pair<long, unsigned long long> >::iterator iter = addrs.begin();
  std::vector< std::pair<long, unsigned long long> >::iterator end = addrs.end();
  unsigned long long * cache_buf_ptr = cache_buf;

  //Write nsets
  *cache_buf_ptr = aCache->nsets; ++cache_buf_ptr;

  //Write assoc
  *cache_buf_ptr = aCache->assoc; ++cache_buf_ptr;

  //Write tags
  while (iter != end) {
    *cache_buf_ptr = iter->second;
    //std::cerr << "Saving " << std::hex << iter->second << std::dec << " for " << aCache->name << std::endl;
    ++cache_buf_ptr;
    ++iter;
  }

  return std::make_pair( reinterpret_cast<unsigned char *> (cache_buf) , addrs.size() * sizeof(unsigned long long) );
}


extern "C"
void ckpt_begin(ckpt_state * aWriter, counter_t aStartInsn, int aWriteCaches) {

  theCurrentCkptState = aWriter;

  aWriter->theStartInsn = aStartInsn;
  aWriter->theL2Misses = 0;
  aWriter->theSyscalls = 0;
  aWriter->theSyscallMemWrites = 0;
  aWriter->theInCkpt = TRUE;
  aWriter->theCkptEntry = new CkptEntry();

  aWriter->theCkptEntry->setLocation(aStartInsn);
  aWriter->theCkptEntry->setDataBreakPoint(ld_brk_point);

  unsigned char * regs = new unsigned char [sizeof(regs_t) ];
  memcpy(regs, aWriter->theRegs, sizeof(regs_t));
  aWriter->theCkptEntry->setRegs( regs, sizeof(regs_t) );


  std::pair<unsigned char *, long> il1 = ckpt_cache(false, *aWriter->theCkptEntry, aWriter->theIl1, aWriter->theMem);
  std::pair<unsigned char *, long> dl1 = ckpt_cache(!! aWriteCaches, *aWriter->theCkptEntry, aWriter->theDl1, aWriter->theMem);
  std::pair<unsigned char *, long> ul2 = ckpt_cache(!! aWriteCaches, *aWriter->theCkptEntry, aWriter->theUl2, aWriter->theMem);
  std::pair<unsigned char *, long> itlb = ckpt_cache(false, *aWriter->theCkptEntry, aWriter->theItlb, aWriter->theMem);
  std::pair<unsigned char *, long> dtlb = ckpt_cache(false, *aWriter->theCkptEntry, aWriter->theDtlb, aWriter->theMem);

  aWriter->theCkptEntry->setIl1( il1.first, il1.second );
  aWriter->theCkptEntry->setDl1( dl1.first , dl1.second );
  aWriter->theCkptEntry->setUl2( ul2.first , ul2.second );
  aWriter->theCkptEntry->setItlb( itlb.first, itlb.second );
  aWriter->theCkptEntry->setDtlb( dtlb.first, dtlb.second );

  std::pair<unsigned char const *, long> bpred = ckpt_bpred(aWriter->theBpred);

  //Need to add BPred support
  aWriter->theCkptEntry->setBPred( bpred.first, bpred.second );

}

extern "C" counter_t sim_ckpt_lines;

extern "C"
void ckpt_finish(counter_t anEndInsn) {
  unsigned long compressed_size = kCompressBufferSize ;

  if (!theCurrentCkptState  || !theCurrentCkptState->theCkptEntry ) {
    fatal("ckpt_finish() called without corresponding ckpt_begin()");
  }

  sim_ckpt_lines += theCurrentCkptState->theCkptEntry->storedLines();

  int size = theCurrentCkptState->theCkptEntry->derSize();

  if (size > theCurrentCkptState->theCkptBufferSize) {
    fatal("Live-point creation buffer is too small.  Increase its size to at least %d", size);
  }

  unsigned char * wrt = theCurrentCkptState->theCkptBuffer;

  theCurrentCkptState->theCkptEntry->derWrite(wrt);

  compress( gzbuffer, &compressed_size, theCurrentCkptState->theCkptBuffer, size);
  fwrite( &compressed_size, sizeof(unsigned long), 1, theCurrentCkptState->theFile);
  fwrite( gzbuffer, compressed_size, 1, theCurrentCkptState->theFile);

/*
  myfprintf(stderr, "End of checkpoint\n");
  myfprintf(stderr, "  %lld instructions\n", anEndInsn - theCurrentCkptState->theStartInsn);
  myfprintf(stderr, "  %lld L2 misses\n", theCurrentCkptState->theL2Misses);
  myfprintf(stderr, "  %lld Syscalls\n", theCurrentCkptState->theSyscalls);
  myfprintf(stderr, "  %lld Syscall memory writes\n", theCurrentCkptState->theSyscallMemWrites);
  myfprintf(stderr, "\n\n");
*/

  //Free all memory allocated for creating the checkpoint
  delete [] theCurrentCkptState->theCkptEntry->regs();
  delete [] theCurrentCkptState->theCkptEntry->il1();
  delete [] theCurrentCkptState->theCkptEntry->dl1();
  delete [] theCurrentCkptState->theCkptEntry->ul2();
  delete [] theCurrentCkptState->theCkptEntry->itlb();
  delete [] theCurrentCkptState->theCkptEntry->dtlb();
  delete [] theCurrentCkptState->theCkptEntry->bpred();

  //Free all memory allocated by syscalls
  theCurrentCkptState->theCkptEntry->cleanSyscalls();

  delete theCurrentCkptState->theCkptEntry;
  theCurrentCkptState->theCkptEntry = 0;

  theCurrentCkptState->theInCkpt = FALSE;
  theCurrentCkptState = 0;

}


std::pair<unsigned long, unsigned long> asn_read(FILE * aFile, unsigned char * aBuffer, int aBufferSize) {
  unsigned long length = 0;
  unsigned long buf_size = aBufferSize;

  fread(&length, sizeof(unsigned long), 1, aFile);
  fread(gzbuffer, length, 1, aFile);
  uncompress(aBuffer, &buf_size, gzbuffer, length);
  return std::make_pair(length, buf_size);
}

extern "C"
void ckpt_read_header(ckpt_state * aReader, int print_info, unsigned long long exec_window_size) {
  asn_read(aReader->theFile, aReader->theCkptBuffer, aReader->theCkptBufferSize);

  try {
    unsigned char * buf = aReader->theCkptBuffer;
    CkptHeader const * hdr = CkptHeader::createFrom( buf );

    if (print_info) {
      std::cerr << "Live-point Configuration" << std::endl;
      std::cerr << "========================" << std::endl;

      std::cerr << "Benchmark: " << hdr->benchmark() << std::endl;
    }

    if (print_info) {
      std::cerr << "Max warming + measurement unit: " << hdr->U() + hdr->W() << std::endl;

      std::cerr << "Bpred/Cache/TLB Configuration:\n" << hdr->cfg() << std::endl;
    }

    if ( exec_window_size > hdr->U() + hdr->W() ) {
      fatal( "Requested m-unit and w-unit size exceed execution window of live-point library");
    }

    delete hdr;

  } catch (...) {
    fatal("Unable to parse live-point header");
  }

}

bool_t ckpt_more_ckpts(ckpt_state * aReader) {
  if (! aReader || ! aReader->theFile) {
    return FALSE;
  }
  int c = fgetc(aReader->theFile);
  if (feof(aReader->theFile)) {
    return FALSE;
  } else {
    ungetc(c, aReader->theFile);
    return TRUE;
  }
}

void apply_memupdates(CkptEntry * aCkpt, UpdateIter<const MemUpdate> iter, UpdateIter<const MemUpdate> end, mem_t * aMem) {
  while (iter != end) {
    //Allocate or find the page for the specified address
    unsigned long long address = iter->address();
    aCkpt->store(address);

    unsigned char * page = MEM_PAGE(aMem, address);
    if (page == 0) {
      //Need to allocate the page
      mem_newpage(aMem, address);
      page = MEM_PAGE(aMem, address);
    }
    unsigned char * host_addr = page + MEM_OFFSET(address);

    memcpy(host_addr, iter->data(), iter->size());

    ++iter;
  }

}

CkptEntry * theCurrentRestoreCkpt = 0;
CkptEntry::syscall_update_iterator theCurrentSyscall =  CkptEntry::syscall_update_iterator(ASNSequence());

bool_t ckpt_is_value_known(md_addr_t anAddr) {
  if (!theCurrentRestoreCkpt) {
    return FALSE;
  }
  return theCurrentRestoreCkpt->isStored(anAddr & ~(127LL));
}

void restore_cache( struct cache_t * aCache, unsigned char const * anAddressSeq, int aSeqSize) {
  unsigned long long const * addr = reinterpret_cast<unsigned long long const *>(anAddressSeq);
  aSeqSize /= sizeof(unsigned long long);

  cache_clear(aCache);

  //std::cerr << "Restoring cache " << aCache->name << std::endl;
  unsigned long long nsets = addr[0];
  unsigned long long assoc = addr[1];

  if (aCache->assoc > assoc) {
    fatal("Configured associativity for %s exceeds the maximum supported by this live-point library", aCache->name);
  }

  if (aCache->nsets > nsets) {
    fatal("Configured number of sets for %s exceeds the maximum supported by this live-point library", aCache->name);
  }

  for (int i = 2; i < aSeqSize; ++i) {
    //std::cerr << "    " << std::hex << addr[i] << std::dec << std::endl;

    //cache_fast_access has the right functionality to
    //allocate / update blocks in a cache without propagating
    //misses/replacements to another cache, and is thus ideal for rapid
    //restoring from a checkpoint


    cache_fast_access
      ( aCache
      , ( (addr[i] & 1) == 1 ? Write : Read) //Use a read or write access depending on dirty bit
      , addr[i] & ~1LL //mask off the dirty bit
      );
  }
}

void restore_bpred(bpred_t * bpred, unsigned char const * aBpredData, int aDataSize) {

  if (bpred->klass == BPredTaken || bpred->klass == BPredNotTaken) {
    return; //Nothing to restore
  } else if (bpred->klass == BPredComb ) {

    unsigned char const * bpred_buf_ptr = aBpredData;

    //Retstack
    //========

      //Ensure retstack size matches
      if (* reinterpret_cast<long const *>(bpred_buf_ptr) != bpred->retstack.size) {
        fatal("Bpred retstack size mismatch between live-point and simulation");
      }
      bpred_buf_ptr += sizeof(long);

      //Restore TOS
      bpred->retstack.tos = * reinterpret_cast<long const *>(bpred_buf_ptr);
      bpred_buf_ptr += sizeof(long);

      //Restore Retstack contents
      for (int i = 0; i < bpred->retstack.size; ++i) {
        memcpy(& bpred->retstack.stack[i], bpred_buf_ptr, kBTBEntrySize);
        bpred_buf_ptr += kBTBEntrySize;
      }

    //BTB
    //===

      //Ensure BTB sets match
      if (* reinterpret_cast<long const *>(bpred_buf_ptr) != bpred->btb.sets) {
        fatal("Bpred BTB sets mismatch between live-point and simulation");
      }
      bpred_buf_ptr += sizeof(long);

      //Ensure BTB assoc matches
      if (* reinterpret_cast<long const *>(bpred_buf_ptr) != bpred->btb.assoc) {
        fatal("Bpred BTB assoc mismatch between live-point and simulation");
      }
      bpred_buf_ptr += sizeof(long);

      //Restore BTB contents
      for (int i = 0; i < bpred->btb.sets * bpred->btb.assoc; ++i) {
        memcpy(& bpred->btb.btb_data[i], bpred_buf_ptr, kBTBEntrySize);
        bpred_buf_ptr += kBTBEntrySize;
      }

      //Connect BTB lru chains
      for (int i = 0; i < bpred->btb.sets; ++i) {
        int set_base = i * bpred->btb.assoc;
        for (int j = 0; j < bpred->btb.assoc; ++j) {
          bpred->btb.btb_data[ set_base + j ].prev = & bpred->btb.btb_data[ set_base + j - 1 ];
          bpred->btb.btb_data[ set_base + j ].next = & bpred->btb.btb_data[ set_base + j + 1 ];
        }
        //Fix the first and last
        bpred->btb.btb_data[ set_base ].prev = 0;
        bpred->btb.btb_data[ set_base + bpred->btb.assoc - 1].next = 0;
      }

    //Bimodal
    //=======
      //Table size
      if (* reinterpret_cast<long const *>(bpred_buf_ptr) != bpred->dirpred.bimod->config.bimod.size) {
        fatal("Bpred Bimodal size mismatch between live-point and simulation");
      }
      bpred_buf_ptr += sizeof(long);

      //Table data
      memcpy(bpred->dirpred.bimod->config.bimod.table, bpred_buf_ptr, bpred->dirpred.bimod->config.bimod.size);
      bpred_buf_ptr += bpred->dirpred.bimod->config.bimod.size;

    //Meta
    //====
      //Table size
      if (* reinterpret_cast<long const *>(bpred_buf_ptr) != bpred->dirpred.meta->config.bimod.size) {
        fatal("Bpred Meta size mismatch between live-point and simulation");
      }
      bpred_buf_ptr += sizeof(long);

      //Table data
      memcpy(bpred->dirpred.meta->config.bimod.table, bpred_buf_ptr, bpred->dirpred.meta->config.bimod.size);
      bpred_buf_ptr += bpred->dirpred.meta->config.bimod.size;

    //Two-level
    //=========
      //Table configuration
      if ( memcmp(bpred_buf_ptr, & bpred->dirpred.twolev->config.two.l1size, 5 * sizeof(long) ) != 0) {
        fatal("Bpred Twolev configuration mismatch between live-point and simulation");
      }
      bpred_buf_ptr += 5 * sizeof(long);

      //Shift registers
      memcpy(bpred->dirpred.twolev->config.two.shiftregs, bpred_buf_ptr, bpred->dirpred.twolev->config.two.l1size * sizeof(int));
      bpred_buf_ptr += bpred->dirpred.twolev->config.two.l1size * sizeof(int);

      //L2 table
      memcpy(bpred->dirpred.twolev->config.two.l2table, bpred_buf_ptr, bpred->dirpred.twolev->config.two.l2size);
      bpred_buf_ptr += bpred->dirpred.twolev->config.two.l2size ;

  } else {
    fatal("Unsupported branch predictor type");
 }
}


extern "C"
void ckpt_skip_ckpts(ckpt_state * aReader, int aCount) {
  if (theCurrentRestoreCkpt) {
    delete theCurrentRestoreCkpt;
  }

  while (aCount > 0) {
    try {
      if (gzeof(aReader->theFile)) {
        return;
      }
      asn_read(aReader->theFile, aReader->theCkptBuffer, aReader->theCkptBufferSize);
    } catch (...) {
      fatal("Unable to parse live-point");
    }
    aCount --;
  }
}

extern "C"
void ckpt_dorestore_ckpt(ckpt_state * aReader) {
    sim_pop_insn = theCurrentRestoreCkpt->location();

    ld_brk_point = theCurrentRestoreCkpt->dataBreakPoint();

    //Restore all the necccessary memory
    apply_memupdates(theCurrentRestoreCkpt, theCurrentRestoreCkpt->beginMemUpdates(), theCurrentRestoreCkpt->endMemUpdates(), aReader->theMem);


    //Restore branch predictor
    restore_bpred(aReader->theBpred, theCurrentRestoreCkpt->bpred(), theCurrentRestoreCkpt->bpredSize());

    //Restore caches
    restore_cache(aReader->theItlb, theCurrentRestoreCkpt->itlb(), theCurrentRestoreCkpt->itlbSize());
    restore_cache(aReader->theDtlb, theCurrentRestoreCkpt->dtlb(), theCurrentRestoreCkpt->dtlbSize());
    restore_cache(aReader->theIl1, theCurrentRestoreCkpt->il1(), theCurrentRestoreCkpt->il1Size());
    restore_cache(aReader->theUl2, theCurrentRestoreCkpt->ul2(), theCurrentRestoreCkpt->ul2Size());
    restore_cache(aReader->theDl1, theCurrentRestoreCkpt->dl1(), theCurrentRestoreCkpt->dl1Size());

    //Restore registers
    memcpy(aReader->theRegs, theCurrentRestoreCkpt->regs(), theCurrentRestoreCkpt->regsSize());

    theCurrentSyscall = theCurrentRestoreCkpt->beginSyscallUpdates();
}

extern "C"
void ckpt_restore_ckpt(ckpt_state * aReader) {
  if (theCurrentRestoreCkpt) {
    delete theCurrentRestoreCkpt;
  }


  try {
    std::pair<unsigned long, unsigned long> len;
    len = asn_read(aReader->theFile, aReader->theCkptBuffer, aReader->theCkptBufferSize);

    unsigned char * buf = aReader->theCkptBuffer;
    theCurrentRestoreCkpt = CkptEntry::createFrom( buf );

    ckptCompressedSize += len.first;
    ckptUncompressedSize += len.second;
    ckptRegsSize += theCurrentRestoreCkpt->regsSize();
    ckptIL1Size += theCurrentRestoreCkpt->il1Size();
    ckptDL1Size += theCurrentRestoreCkpt->dl1Size();
    ckptUL2Size += theCurrentRestoreCkpt->ul2Size();
    ckptITLBSize += theCurrentRestoreCkpt->itlbSize();
    ckptDTLBSize += theCurrentRestoreCkpt->dtlbSize();
    ckptBPredSize += theCurrentRestoreCkpt->bpredSize();
    ckptDataSize += theCurrentRestoreCkpt->beginMemUpdates().size();
    ckptSyscallSize += theCurrentRestoreCkpt->beginSyscallUpdates().size();

    ckpt_dorestore_ckpt(aReader);

  } catch (...) {
    fatal("Unable to parse checkpoint");
  }

}



extern "C"
void ckpt_replay_syscall(
    counter_t icnt,		  	        /* instruction count */
		struct regs_t *regs,		      /* registers to update */
		mem_access_fn mem_fn,		      /* generic memory accessor */
		struct mem_t *mem,		        /* memory to update */
		md_inst_t inst) {			        /* system call inst */

  if (! theCurrentRestoreCkpt ) {
    fatal("Trying to replay a syscall while there is no current checkpoint");
  }
  if (  theCurrentSyscall == theCurrentRestoreCkpt->endSyscallUpdates() ) {
    std::cerr << " WARNING: Syscall replay request with no corresponding syscall in ckpt at " << icnt << std::endl;
    return;
  }

  //See if sim_pop_insn matches
  if (icnt != theCurrentSyscall->icount()) {
    std::cerr << " Syscall @: " << theCurrentSyscall->icount() << " Request @:" <<icnt << std::endl;
    fatal("icount stored in syscall checkpoint does not match icount of syscall replay request.");
  }

  //Restore all the necccessary memory
  apply_memupdates(theCurrentRestoreCkpt, theCurrentSyscall->beginMemUpdates(), theCurrentSyscall->endMemUpdates(), mem);

  memcpy(regs, theCurrentSyscall->regs(), theCurrentSyscall->regsSize());
  ld_brk_point = theCurrentSyscall->dataBreakPoint();

  //Advance to the next syscall
  ++theCurrentSyscall;

}

static bool_t seen_write;
static mem_access_fn local_mem_fn;

extern "C"
void ckpt_l2_miss( enum mem_cmd cmd, md_addr_t addr, int aSize) {
  if (!theCurrentCkptState  || !theCurrentCkptState->theCkptEntry ) {
    fatal("ckpt_finish() called without corresponding ckpt_begin()");
  }

  if ( MEM_PAGE(theCurrentCkptState->theMem, addr) == 0) {
    //std::cout << " L2 Miss to: " << std::hex << addr << std::dec << " Ignored as this miss allocates the page."<< std::endl;

  } else {
    //std::cout << " L2 Miss to: " << std::hex << addr << std::dec << std::endl;
    write_address(* theCurrentCkptState->theCkptEntry, theCurrentCkptState->theMem, addr);
  }

  theCurrentCkptState->theL2Misses ++;
}


void syscall_write_address(SyscallUpdate & anUpdate, mem_t * aMem, unsigned long long anAddress, int aSize) {
  unsigned char * page = MEM_PAGE(aMem, anAddress);
  if (page == 0) {
    fatal("A syscall accessed an address that does not have a valid PTE");
  }
  unsigned char * host_addr = page + MEM_OFFSET(anAddress);
  MemUpdate & update = anUpdate.addMemUpdate();
  update.setAddress(anAddress);
  update.setData(host_addr, aSize);

}

extern "C"
void ckpt_syscall(
    counter_t icnt,		  	        /* instruction count */
		struct regs_t *regs,		      /* registers to update */
		mem_access_fn mem_fn,		      /* generic memory accessor */
		struct mem_t *mem,		        /* memory to update */
		md_inst_t inst) 			        /* system call inst */
{
  int i;

  if (! theCurrentCkptState ) {
    fatal("ckpt_syscall called while there is no current checkpoint");
  }

  theCurrentCkptState->theSyscalls++;
  theCurrentCkptState->theSyscall = & theCurrentCkptState->theCkptEntry->addSyscallUpdate();

  //Record the location of the syscall
  theCurrentCkptState->theSyscall->setICount(icnt);

  /* perform the system call, record any memory changes */
  seen_write = FALSE;
  local_mem_fn = mem_fn;

  sys_syscall(regs, ckpt_syscall_access_handler, mem, inst, TRUE);

  //Store the CPU registers after the syscall is complete
  unsigned char * regs_buf = new unsigned char [sizeof(struct regs_t) ];
  memcpy(regs_buf, regs, sizeof(struct regs_t));
  theCurrentCkptState->theSyscall->setRegs( regs_buf, sizeof(struct regs_t) );
  theCurrentCkptState->theSyscall->setDataBreakPoint(ld_brk_point);

  theCurrentCkptState->theSyscall = 0;
}


extern "C"
enum md_fault_type ckpt_syscall_access_handler(struct mem_t *mem,		/* memory space to access */
	  enum mem_cmd cmd,		                                            /* Read (from sim mem) or Write */
	  md_addr_t addr,		                                              /* target address to access */
	  void *vp,			                                                  /* host memory address to access */
	  int nbytes)			                                                /* number of bytes to access */
{
  int i;
  enum md_fault_type fault = md_fault_none;


  if (cmd == Read && seen_write) {
    fatal("Read after Write in ckpt_syscall()");
  }

  /* perform the memory access, Read's first so we can probe *p for data */
  fault = (*local_mem_fn)(mem, cmd, addr, vp, nbytes);

  if (cmd == Write) {
    theCurrentCkptState->theSyscallMemWrites++;

    syscall_write_address(* theCurrentCkptState->theSyscall, theCurrentCkptState->theMem, addr, nbytes);

    seen_write = TRUE;
  }

  return fault;
}
