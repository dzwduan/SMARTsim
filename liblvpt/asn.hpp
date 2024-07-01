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

#ifndef ASN_HPP
#define ASN_HPP

#include <string>
#include <list>
#include <set>

struct ASNElement {
  virtual void derWrite(unsigned char * & aBuffer) const = 0;
  virtual int contentLength() const = 0;
  int derSize() const;

  virtual ~ASNElement() {}
};

//Checkpoint Header:
  //SEQUENCE
    //Version : Int (1)
    //Benchmark : OctetString
    //U : Int
    //W : Int
    //Cfg : OctetString

struct CkptHeader : public ASNElement {
  static CkptHeader const * createFrom(unsigned char const * aBuffer);
  void derWrite(unsigned char * & aBuffer) const;
  int CkptHeader::contentLength() const;

  std::string benchmark() const { return theBenchmark; }
  void setBenchmark(std::string const & aBenchmark) { theBenchmark = aBenchmark; }

  long long U() const { return theU; }
  void setU(long long aU) { theU = aU; }

  long long W() const { return theW; }
  void setW(long long aW) { theW = aW; }

  std::string cfg() const { return theCfg; }
  void setCfg(std::string const & aCfg) { theCfg = aCfg; }

  private:
    std::string theBenchmark;
    long long theU;
    long long theW;
    std::string theCfg;
};

//MemUpdate
  //Addr : Int
  //Size : Int
  //Data : OctetString

struct MemUpdate : public ASNElement {
  typedef MemUpdate seq_type;
  static MemUpdate * createFrom(unsigned char const * aBuffer);
  void derWrite(unsigned char * & aBuffer) const;
  int contentLength() const ;

  unsigned long long theAddress;
  int theSize;
  u_char * theData;
  bool theAlloc;

  bool operator == (MemUpdate const & anUpdate) const {
    return theData == anUpdate.theData;
  }

  MemUpdate( )
   : theAddress(0)
   , theSize(0)
   , theData(0)
   , theAlloc(false)
   {}

  ~MemUpdate( ) {
    if (theAlloc) {
      delete [] theData;
    }
  }


  void setAddress( unsigned long long anAddress ) { theAddress = anAddress; }
  unsigned long long address() const { return theAddress; }
  int size() const { return theSize; }
  void setData( unsigned char const * someData, int aSize ) {
      theData = new unsigned char [aSize];
      theSize = aSize;
      theAlloc = true;
      memcpy(theData, someData, aSize);
  }
  unsigned char const * data() const { return theData; }

};


struct ASNSequence {
  ASNSequence();
  ASNSequence(unsigned char const *, int aSize);

  template <class T>
  T const * current() const {
    if (theHead) {
      return T::createFrom(theHead);
    } else {
      return 0;
    }
  }

  ASNSequence next() const;

  unsigned long size() { return static_cast<unsigned long>(theTail - theHead) + theTailSize; }

  bool operator ==( ASNSequence const & aRHS) const {
    return theHead == aRHS.theHead;
  }

  u_char const * theHead;
  u_char const * theTail;
  int theTailSize;
};

template <class T>
struct UpdateIter {
  ASNSequence theUpdate;

  UpdateIter(ASNSequence const & aSequence)
    : theUpdate(aSequence)
    {}

  unsigned long size() { return theUpdate.size(); }

  bool operator ==( UpdateIter const & anIter) const {
    return theUpdate == anIter.theUpdate;
  }
  const UpdateIter operator ++() {
    theUpdate = theUpdate.next();
    return *this;
  }
  T const * operator ->() const {
    return theUpdate.template current<T>();
  }
  T const & operator *() const {
    return *theUpdate.template current<T>();
  }
};


//SyscallUpdate
  //Icount : Int
  //Instr : Int64
  //regs : Opaque
  //SEQUENCE of MemUpdate

struct SyscallUpdate : public ASNElement {
  typedef SyscallUpdate * seq_type;
  static SyscallUpdate * createFrom(unsigned char const * aBuffer);
  void derWrite(unsigned char * & aBuffer) const;
  int contentLength() const ;

  MemUpdate & addMemUpdate( ) { theMemUpdates.push_back( MemUpdate() ); return theMemUpdates.back(); }

  typedef UpdateIter<const MemUpdate> mem_update_iterator;
  mem_update_iterator beginMemUpdates() const {
    return mem_update_iterator( theMemUpdates_seq );
  }
  mem_update_iterator endMemUpdates() const {
    return mem_update_iterator( ASNSequence() );
  }

  unsigned long long icount() const { return theICount; }
  void setICount(unsigned long long aCount) { theICount = aCount; }

  unsigned long long dataBreakPoint() const { return theDataBreakPoint; }
  void setDataBreakPoint(unsigned long long aDataBreakPoint) { theDataBreakPoint = aDataBreakPoint; }

  u_char const * regs() const { return theRegs; }
  int regsSize() const { return theRegs_size; }
  void setRegs(u_char const * aRegs, int aSize) { theRegs = aRegs; theRegs_size = aSize; }

  unsigned long long theICount;
  unsigned long long theDataBreakPoint;
  u_char const * theRegs;
  int theRegs_size;
  std::list< MemUpdate > theMemUpdates;
  ASNSequence theMemUpdates_seq;
};


//Checkpoint Entry
  //SEQUENCE
    //Location : Int64
    //regs : Opaque
    //il1 : Opaque
    //dl1 : Opaque
    //ul2 : Opaque
    //itlb : Opaque
    //dtlb : Opaque
    //bpred : Opaque
    //mem_updates : SEQUENCE of MemUpdate
        //MemUpdate
    //syscall_updates : SEQUENCE of SyscallUpdate

struct CkptEntry : public ASNElement {
  static CkptEntry * createFrom(unsigned char const * aBuffer);
  void derWrite(unsigned char * & aBuffer) const;
  int contentLength() const ;

  unsigned long long location() const { return theLocation; }
  void setLocation(unsigned long long aLocation) { theLocation = aLocation; }

  unsigned long long dataBreakPoint() const { return theDataBreakPoint; }
  void setDataBreakPoint(unsigned long long aDataBreakPoint) { theDataBreakPoint = aDataBreakPoint; }

  u_char const * regs() const { return theRegs; }
  int regsSize() const { return theRegs_size; }
  void setRegs(u_char const * aRegs, int aSize) { theRegs = aRegs; theRegs_size = aSize; }

  u_char const * il1() const { return theIL1; }
  int il1Size() const { return theIL1_size; }
  void setIl1(u_char const * aBuf, int aSize) { theIL1 = aBuf; theIL1_size = aSize; }

  u_char const * dl1() const { return theDL1; }
  int dl1Size() const { return theDL1_size; }
  void setDl1(u_char const * aBuf, int aSize) { theDL1 = aBuf; theDL1_size = aSize; }

  u_char const * ul2() const { return theUL2; }
  int ul2Size() const { return theUL2_size; }
  void setUl2(u_char const * aBuf, int aSize) { theUL2 = aBuf; theUL2_size = aSize; }

  u_char const * itlb() const { return theITLB; }
  int itlbSize() const { return theITLB_size; }
  void setItlb(u_char const * aBuf, int aSize) { theITLB = aBuf; theITLB_size = aSize; }

  u_char const * dtlb() const { return theDTLB; }
  int dtlbSize() const { return theDTLB_size; }
  void setDtlb(u_char const * aBuf, int aSize) { theDTLB = aBuf; theDTLB_size = aSize; }

  u_char const * bpred() const { return theBPred; }
  int bpredSize() const { return theBPred_size; }
  void setBPred(u_char const * aBuf, int aSize) { theBPred = aBuf; theBPred_size = aSize; }

  MemUpdate & addMemUpdate( ) { theMemUpdates.push_back( MemUpdate() ); return theMemUpdates.back(); }
  SyscallUpdate & addSyscallUpdate( ) { theSyscallUpdates.push_back( SyscallUpdate() ); return theSyscallUpdates.back(); }

  typedef UpdateIter<const MemUpdate> mem_update_iterator;
  mem_update_iterator beginMemUpdates() const {
    return mem_update_iterator( theMemUpdates_seq );
  }
  mem_update_iterator endMemUpdates() const {
    return mem_update_iterator( ASNSequence() );
  }

  typedef UpdateIter<const SyscallUpdate> syscall_update_iterator;
  syscall_update_iterator beginSyscallUpdates() const {
    return syscall_update_iterator( theSyscallUpdates_seq );
  }
  syscall_update_iterator endSyscallUpdates() const {
    return syscall_update_iterator( ASNSequence() );
  }

  bool isStored( unsigned long long anAddress ) const { return theStoredAddresses.count(anAddress) > 0; }
  void store( unsigned long long anAddress ) { theStoredAddresses.insert(anAddress); }
  unsigned long storedLines() { return theMemUpdates.size(); }

  void cleanSyscalls();


  unsigned long long theLocation;
  unsigned long long theDataBreakPoint;
  u_char const * theRegs;
  int theRegs_size;
  u_char const * theIL1;
  int theIL1_size;
  u_char const * theDL1;
  int theDL1_size;
  u_char const * theUL2;
  int theUL2_size;
  u_char const * theITLB;
  int theITLB_size;
  u_char const * theDTLB;
  int theDTLB_size;
  u_char const * theBPred;
  int theBPred_size;

  std::list< MemUpdate > theMemUpdates;
  std::list< SyscallUpdate > theSyscallUpdates;

  std::set< unsigned long long > theStoredAddresses;

  ASNSequence theMemUpdates_seq;
  ASNSequence theSyscallUpdates_seq;

};


#endif /* ASN_HPP */
