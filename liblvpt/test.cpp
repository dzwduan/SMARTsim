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

#include <iostream>
#include <iomanip>

#include "asn.hpp"

static const int k = 1024;
static const int M = 1024 * k;



void testASN() {
  unsigned char * buf = new unsigned char[1*M];

  std::cout << "This test application verifies the ASN.1 encoding and decoding code agree.\n";

  std::cout << "Memory Allocated\n";

  CkptHeader whdr;

  whdr.setBenchmark("foo");
  whdr.setU(1000);
  whdr.setW(2000);
  whdr.setCfg("bar");

  std::cout << "CkptHeader Created\n";

  unsigned char * wrt = buf;
  int size = whdr.derSize();
  whdr.derWrite(wrt);

  std::cout << std::hex;
  for (int i = 0 ; i < size; ++i) {
    if ((i & 15) == 15) std::cout << std::endl;
    std::cout << std::setw(2) << std::setfill('0') << (int) buf[i] << " ";
  }
  std::cout << std::dec << "\n\n";

  std::cout << "CkptHeader Written\n";


  unsigned char * rd = buf;
  CkptHeader const * rhdr = CkptHeader::createFrom(rd);

  std::cout << "CkptHeader Read\n";

  if (rhdr->benchmark() != whdr.benchmark()) {
    cout << "Benchmark mismatch" << endl;
  }
  if (rhdr->U() != whdr.U()) {
    cout << "Benchmark mismatch" << endl;
  }
  if (rhdr->W() != whdr.W()) {
    cout << "Benchmark mismatch" << endl;
  }
  if (rhdr->cfg() != whdr.cfg()) {
    cout << "Benchmark mismatch" << endl;
  }

  delete rhdr;

  std::cout << "CkptHeader Verified\n";


  CkptEntry wentry;

  unsigned char const * regs = (unsigned char const *) "regs";
  unsigned char const * il1 = (unsigned char const *) "il1";
  unsigned char const * dl1 = (unsigned char const *) "dl1";
  unsigned char const * ul2 = (unsigned char const *) "ul2";
  unsigned char const * itlb = (unsigned char const *) "itlb";
  unsigned char const * dtlb = (unsigned char const *) "dtlb";
  unsigned char const * bpred = (unsigned char const *) "bpred";
  unsigned char const * mem1 = (unsigned char const *) "mem1";
  unsigned char const * mem2 = (unsigned char const *) "mem2";
  unsigned char const * mem3 = (unsigned char const *) "mem3";

  wentry.setRegs( regs, sizeof(regs) );
  wentry.setIl1( il1, sizeof(il1) );
  wentry.setDl1( dl1, sizeof(dl1) );
  wentry.setUl2( ul2, sizeof(ul2) );
  wentry.setItlb( itlb, sizeof(itlb) );
  wentry.setDtlb( dtlb, sizeof(dtlb) );
  wentry.setBPred( bpred, sizeof(bpred) );

  wentry.setLocation( 25 );

  MemUpdate & update = wentry.addMemUpdate();
  update.setAddress( 47 );
  update.setData( mem1, sizeof(mem1) );

  SyscallUpdate & sys_update = wentry.addSyscallUpdate();
  sys_update.setICount( 32 );
  sys_update.setRegs( regs, sizeof(regs) );

  MemUpdate & supdate = sys_update.addMemUpdate();
  supdate.setAddress( 53 );
  supdate.setData( mem2, sizeof(mem2) );

  MemUpdate & supdate2 = sys_update.addMemUpdate();
  supdate2.setAddress( 97 );
  supdate2.setData( mem3, sizeof(mem3) );

  std::cout << "CkptEntry Created\n";

  wrt = buf;
  size = wentry.derSize();
  wentry.derWrite(wrt);

  std::cout << std::hex;
  for (int i = 0 ; i < size; ++i) {
    if ((i & 15) == 0 ) std::cout << std::endl;
    std::cout << std::setw(2) << std::setfill('0') << (int) buf[i] << " ";
  }
  std::cout << std::dec << "\n\n";

  std::cout << "CkptEntry Written\n";

  rd = buf;
  CkptEntry * rentry = CkptEntry::createFrom(rd);

  std::cout << "CkptEntry Read.\n";

  if (rentry->location() != wentry.location()) {
    std::cout << "Location mismatch" << std::endl;
  }

  if (rentry->regsSize() != sizeof(regs)) {
    std::cout << "Regs Size mismatch" << std::endl;
  }

  if ( memcmp( rentry->regs(), regs, sizeof(regs) ) != 0) {
    std::cout << "Regs contents mismatch" << std::endl;
  }

  if (rentry->il1Size() != sizeof(il1)) {
    std::cout << "il1 Size mismatch" << std::endl;
  }

  if ( memcmp( rentry->il1(), il1, sizeof(il1) ) != 0) {
    std::cout << "il1 contents mismatch" << std::endl;
  }

  if (rentry->dl1Size() != sizeof(dl1)) {
    std::cout << "dl1 Size mismatch" << std::endl;
  }

  if ( memcmp( rentry->dl1(), dl1, sizeof(dl1) ) != 0) {
    std::cout << "dl1 contents mismatch" << std::endl;
  }

  if (rentry->ul2Size() != sizeof(ul2)) {
    std::cout << "ul2 Size mismatch" << std::endl;
  }

  if ( memcmp( rentry->ul2(), ul2, sizeof(ul2) ) != 0) {
    std::cout << "ul2 contents mismatch" << std::endl;
  }

  if (rentry->itlbSize() != sizeof(itlb)) {
    std::cout << "itlb Size mismatch" << std::endl;
  }

  if ( memcmp( rentry->itlb(), itlb, sizeof(itlb) ) != 0) {
    std::cout << "itlb contents mismatch" << std::endl;
  }

  if (rentry->dtlbSize() != sizeof(dtlb)) {
    std::cout << "dtlb Size mismatch" << std::endl;
  }

  if ( memcmp( rentry->dtlb(), dtlb, sizeof(dtlb) ) != 0) {
    std::cout << "dtlb contents mismatch" << std::endl;
  }

  if (rentry->bpredSize() != sizeof(bpred)) {
    std::cout << "bpred Size mismatch" << std::endl;
  }

  if ( memcmp( rentry->bpred(), bpred, sizeof(bpred) ) != 0) {
    std::cout << "bpred contents mismatch" << std::endl;
  }

  CkptEntry::mem_update_iterator iter = rentry->beginMemUpdates();

  if (iter->address() != 47) {
    std::cout << "mem_update address mismatch" << std::endl;
  }

  if (iter->size() != sizeof(mem1)) {
    std::cout << "mem_update size mismatch" << std::endl;
  }

  if ( memcmp( iter->data(), mem1, sizeof(mem1) ) != 0) {
    std::cout << "mem_update contents mismatch" << std::endl;
  }

  ++iter;
  if (iter != rentry->endMemUpdates()) {
    std::cout << "mem_update_iter did not end when expected" << std::endl;
  }

  CkptEntry::syscall_update_iterator siter = rentry->beginSyscallUpdates();

  if (siter->icount() != 32) {
    std::cout << "syscall_update icount mismatch" << std::endl;
  }

  if (siter->regsSize() != sizeof(regs)) {
    std::cout << "syscall_update regs size mismatch" << std::endl;
  }

  if ( memcmp( siter->regs(), regs, sizeof(regs) ) != 0) {
    std::cout << "syscall_update regs mismatch" << std::endl;
  }

  SyscallUpdate::mem_update_iterator smiter = siter->beginMemUpdates();

  if (smiter->address() != 53) {
    std::cout << "syscall_update mem_update address mismatch" << std::endl;
  }

  if (smiter->size() != sizeof(mem2)) {
    std::cout << "syscall_update mem_update size mismatch" << std::endl;
  }

  if ( memcmp( smiter->data(), mem2, sizeof(mem2) ) != 0) {
    std::cout << "syscall_update mem_update contents mismatch" << std::endl;
  }

  ++smiter;

  if (smiter->address() != 97) {
    std::cout << "syscall_update mem_update address mismatch" << std::endl;
  }

  if (smiter->size() != sizeof(mem3)) {
    std::cout << "syscall_update mem_update size mismatch" << std::endl;
  }

  if ( memcmp( smiter->data(), mem3, sizeof(mem3) ) != 0) {
    std::cout << "syscall_update mem_update contents mismatch" << std::endl;
  }

  ++smiter;


  if (smiter != siter->endMemUpdates()) {
    std::cout << "syscall_update mem_update_iter did not end when expected" << std::endl;
  }

  ++siter;
  if (siter != rentry->endSyscallUpdates()) {
    std::cout << "syscall_update_iter did not end when expected" << std::endl;
  }

  std::cout << "CkptEntry Verified.\n";

  delete rentry;
  delete [] buf;
}

int main() {
  testASN();

  std::cout << "ASN Test Complete\n\n\n";
}