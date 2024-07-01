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

#include "asn.hpp"


static const long long kVersion = 2;

struct BadASNError { };



//ASN Type constants
static const u_char ASN_BOOLEAN = 0x01;
static const u_char ASN_INTEGER = 0x02;
static const u_char ASN_BIT_STR = 0x03;
static const u_char ASN_OCTETSTRING = 0x04;
static const u_char ASN_NULL = 0x05;
static const u_char ASN_OBJECTIDENTIFIER = 0x06;
static const u_char ASN_SEQUENCE = 0x30 ;
static const u_char ASN_TIMETICKS = 0x43;
static const u_char ASN_IPADDRESS = 0x40;
static const u_char ASN_COUNTER32 = 0x41;
static const u_char ASN_GAUGE32 = 0x42;
static const u_char ASN_UNSIGNED32 = 0x47;
static const u_char ASN_INTEGER32 = ASN_INTEGER;
static const u_char ASN_OPAQUE = 0x44;
static const u_char ASN_INTEGER64 = 0x4A;
static const u_char ASN_UNSIGNED64 = 0x4B;

static const u_char ASN_CONTEXT = 0x80;
static const u_char ASN_CONSTRUCTOR = 0x20;

static const u_char ASN_CKPT_HEADER = (ASN_CONTEXT | ASN_CONSTRUCTOR | 0x0);
static const u_char ASN_CKPT_ENTRY = (ASN_CONTEXT | ASN_CONSTRUCTOR | 0x1);
static const u_char ASN_MEM_UPDATE = (ASN_CONTEXT | ASN_CONSTRUCTOR | 0x2);
static const u_char ASN_SYSCALL_UPDATE = (ASN_CONTEXT | ASN_CONSTRUCTOR | 0x3);


//Fundamental operations
  typedef unsigned int u_int;
  typedef unsigned long u_long;
  typedef unsigned long long u_longlong;

  u_int integer_minimum_octets(u_longlong aVal) {
  	if (aVal < (0x80LL)) {
  		return 1;
  	} else if (aVal < 0x8000LL) {
  		return 2;
  	} else if (aVal < 0x800000LL) {
  		return 3;
  	} else if (aVal < 0x80000000LL) {
  		return 4;
  	} else if (aVal < 0x8000000000LL) {
  		return 5;
  	} else if (aVal < 0x800000000000LL) {
  		return 6;
  	} else if (aVal < 0x80000000000000LL) {
  		return 7;
  	} else {
  		return 8;
  	}
  }

  u_int integer_minimum_octets(long long aVal) {
  	if (aVal < 0) {
  		return integer_minimum_octets(static_cast<u_longlong>(-aVal));
  	} else {
  		return integer_minimum_octets(static_cast<u_longlong>(aVal));
  	}
  }

  u_int length_minimum_octets(u_long aLength) {
  	if (aLength < 128) {
  		return 1;
  	} else {
  		return integer_minimum_octets(static_cast<u_longlong>(aLength)) + 1;
  	}
  }

  void write_unsigned(u_char * & buf, u_char type, u_longlong val) {
  	int oclen = integer_minimum_octets(val);
  	buf[0] = type;
  	buf[1] = oclen;
  	for (int i = 2, j = oclen; j > 0; ++i) {
  		buf[i] = static_cast<u_char>( (val >> ( (--j) * 8)) );
  	}
  	buf += oclen + 2;
  }

  void write_signed(u_char * & buf, u_char type, long long val) {
  	int oclen = integer_minimum_octets(val);
  	buf[0] = type;
  	buf[1] = oclen;
  	for (int i = 2, j = oclen; j > 0; ++i) {
  		buf[i] = (u_char) (val >> ( (--j) * 8));
  	}
  	buf += oclen + 2;
  }

  void write_length(u_char * & buf, u_long val) {
  	if (val < 128) {
  		*buf = (u_char)val;
  		++buf;
  	} else {
  		unsigned int oclen = integer_minimum_octets(static_cast<u_longlong>(val));
  		*buf = (u_char) (oclen | 0x80);
  		for (int i = 1, j = oclen; j > 0; ++i) {
  			buf[i] = (u_char) (val >> ( (--j) * 8));
  		}
  		buf += oclen + 1;
  	}
  }

  void write_string(u_char * & buf, u_char type, u_char const * val, u_int length) {
  	//Determine number of bytes needed to encode length
  	unsigned int tot_length = length + length_minimum_octets(length) + 1;
  	buf[0] = type;
  	++buf;
  	write_length(buf,length);
  	memcpy(buf,val,length);
  	buf += length;
  }


  u_long read_length(u_char const * & aPtr) {
  	unsigned long length = 0;
  	if (*aPtr & 0x80) {
  		for(unsigned int len_octets = *aPtr & 0x7F; len_octets > 0; ) {
  			++aPtr;
  			length += (*aPtr) << (8 * --len_octets);
  		}
  	  ++aPtr;
  	} else {
  		length = *aPtr;
  		++aPtr;
  	}
  	return length;
  }

  u_longlong read_unsigned(u_char const * & aPtr, u_char anExpectedType) {
  	if (*aPtr != anExpectedType) {
  	  throw BadASNError();
  	}
  	++aPtr;

	  u_longlong val = 0;
	  long len = read_length(aPtr);
	  while(len > 0) {
		  val += (static_cast<u_longlong>((*aPtr)) << (8 * --len));
		  ++aPtr;
	  }
	  return val;
  }

  long long read_signed(u_char const * & aPtr, u_char anExpectedType) {
  	if (*aPtr != anExpectedType) {
  	  throw BadASNError();
  	}
  	++aPtr;

  	long long val = 0;
  	long len = read_length(aPtr);
  	char sign = *aPtr;
  	for (int byte = 7; byte >= 0; --byte) {
  		if ((byte >= len) ) {
  			if (sign < 0) {
  				//This is neccessary to sign-extend negative values
  				val |= (0xFFLL << (8 * byte));
  			}
  		} else {
  			val |= ( static_cast<long long>(*aPtr) << (8 * byte));
  			++aPtr;
  		}
  	}
  	return val;
  }

  std::string read_string(u_char const * & buf, u_char anExpectedType) {
  	if (*buf != anExpectedType) {
  	  throw BadASNError();
  	}
  	++buf ;

  	//Determine number of bytes needed to encode length
  	unsigned int length = read_length(buf);
    std::string ret_val(reinterpret_cast<char const *>(buf), length);
    buf += length;
  	return ret_val;
  }

  void read_object(u_char const * & buf, u_char const * & obj, int & size) {
  	if (*buf != ASN_OCTETSTRING) {
  	  throw BadASNError();
  	}
  	++buf;

  	size = read_length(buf);
    obj = buf;
    buf += size;
  }

  ASNSequence read_sequence(u_char const * & buf) {
  	if (*buf != ASN_SEQUENCE) {
  	  throw BadASNError();
  	}
  	++buf ;

  	//Determine number of bytes needed to encode length
  	unsigned int length = read_length(buf);

    unsigned char const * first = buf;
    buf += length;
    if (length > 0) {
      return ASNSequence(first, length);
    } else {
      return ASNSequence();
    }
  }

template < class Iterator >
void derWriteSequence( unsigned char * & aBuffer, Iterator aBegin, Iterator anEnd) {
  Iterator iter(aBegin);
  long total_size = 0;
  while (iter != anEnd) {
    total_size += iter->derSize();
    ++iter;
  }
  //write out sequence tag
  *aBuffer = ASN_SEQUENCE;
  aBuffer++;
  //write out size
  write_length( aBuffer, total_size);

  while (aBegin != anEnd) {
    aBegin->derWrite(aBuffer);
    ++aBegin;
  }
}

template < class Iterator >
int derSequenceSize( Iterator aBegin, Iterator anEnd) {
  int length = 0;
  while (aBegin != anEnd) {
    length += aBegin->derSize();
    ++aBegin ;
  }
  return length + length_minimum_octets( length ) + 1;
}

ASNSequence::ASNSequence()
  : theHead(0)
  , theTail(0)
  , theTailSize(0)
  { }

ASNSequence::ASNSequence(unsigned char const * aBuffer, int aSize) {
  if (aSize == 0) {
    theHead = 0;
  } else {
    theHead = aBuffer;
    aBuffer ++;
    int size = read_length( aBuffer );
    theTail = aBuffer + size;
    theTailSize = aSize - (theTail - theHead);
  }
}

ASNSequence ASNSequence::next() const {
  if (theTailSize > 0) {
    return ASNSequence(theTail, theTailSize);
  } else {
    return ASNSequence();
  }
}


int ASNElement::derSize() const {
  //the content;
  int size = contentLength();

  //the length field;
  size += length_minimum_octets(size);

  //the type byte;
  size++;

  return size;
}


//Checkpoint Header:
  //SEQUENCE
    //Version : Int (1)
    //Benchmark : OctetString
    //U : Int
    //W : Int
    //Cfg : OctetString

CkptHeader const * CkptHeader::createFrom(unsigned char const * aBuffer) {
  CkptHeader * header = new CkptHeader();
  //std::cout << "Read type byte\n";
  if (aBuffer[0] != ASN_CKPT_HEADER) {
    std::cout << "Bad type byte: " << std::hex << (int) aBuffer[0] << std::dec << "\n";
    throw BadASNError();
  }
  ++aBuffer;
  u_long length = read_length( aBuffer );
  //std::cout << "Read length: " << length << "\n";
  int version = read_signed( aBuffer, ASN_INTEGER );
  //std::cout << "Read version: " << version << "\n";
  if ( version != kVersion) {
    std::cout << "Bad version: " << version << "\n";
    throw BadASNError();
  }
  header->theBenchmark = read_string( aBuffer, ASN_OCTETSTRING );
  //std::cout << "Read benchmark: " << header->theBenchmark << "\n";
  header->theU = read_unsigned( aBuffer, ASN_UNSIGNED32);
  //std::cout << "Read U: " << header->theU << "\n";
  header->theW = read_unsigned( aBuffer, ASN_UNSIGNED32);
  //std::cout << "Read W: " << header->theW << "\n";
  header->theCfg = read_string( aBuffer, ASN_OCTETSTRING );
  //std::cout << "Read cfg: " << header->theCfg << "\n";

  return header;
}

void CkptHeader::derWrite(unsigned char * & aBuffer) const {
  int content = contentLength();
  *aBuffer = ASN_CKPT_HEADER;
  ++aBuffer;

  write_length( aBuffer, content );

  write_signed( aBuffer, ASN_INTEGER, kVersion);

  write_string( aBuffer, ASN_OCTETSTRING, reinterpret_cast<u_char const *>(theBenchmark.c_str()), theBenchmark.size());
  write_unsigned( aBuffer, ASN_UNSIGNED32, theU);
  write_unsigned( aBuffer, ASN_UNSIGNED32, theW);
  write_string( aBuffer, ASN_OCTETSTRING, reinterpret_cast<u_char const *>(theCfg.c_str()), theCfg.size());

}

int CkptHeader::contentLength() const {
  int size = 0;
  //Version
  size += integer_minimum_octets( kVersion ) + 2;
  //Benchmark
  size += theBenchmark.length() + length_minimum_octets( theBenchmark.length() ) + 1;
  //U
  size += integer_minimum_octets( theU ) + 2;
  //W
  size += integer_minimum_octets( theW ) + 2;
  //Cfg
  size += theCfg.length() + length_minimum_octets( theCfg.length() ) + 1;

  return size;
}


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

CkptEntry * CkptEntry::createFrom(unsigned char const * aBuffer) {
  CkptEntry * entry = new CkptEntry();
  unsigned long length = 0;
  int version = 0;
  if (aBuffer[0] != ASN_CKPT_ENTRY) {
    std::cout << "Type bad" << std::endl;
    throw BadASNError();
  }
  ++aBuffer;
  length = read_length( aBuffer );

  entry->theLocation = read_unsigned( aBuffer, ASN_UNSIGNED64 );
  entry->theDataBreakPoint = read_unsigned( aBuffer, ASN_UNSIGNED64 );

  read_object( aBuffer, entry->theRegs, entry->theRegs_size );
  read_object( aBuffer, entry->theIL1, entry->theIL1_size);
  read_object( aBuffer, entry->theDL1, entry->theDL1_size);
  read_object( aBuffer, entry->theUL2, entry->theUL2_size);
  read_object( aBuffer, entry->theITLB, entry->theITLB_size);
  read_object( aBuffer, entry->theDTLB, entry->theDTLB_size);
  read_object( aBuffer, entry->theBPred, entry->theBPred_size);


  entry->theMemUpdates_seq = read_sequence( aBuffer );
  entry->theSyscallUpdates_seq = read_sequence( aBuffer );

  return entry;
}

void CkptEntry::derWrite(unsigned char * & aBuffer) const {
  int content = contentLength();
  *aBuffer = ASN_CKPT_ENTRY;
  ++aBuffer;

  write_length( aBuffer, content );

  write_unsigned( aBuffer, ASN_UNSIGNED64, theLocation);
  write_unsigned( aBuffer, ASN_UNSIGNED64, theDataBreakPoint);

  write_string( aBuffer, ASN_OCTETSTRING, theRegs, theRegs_size);
  write_string( aBuffer, ASN_OCTETSTRING, theIL1, theIL1_size);
  write_string( aBuffer, ASN_OCTETSTRING, theDL1, theDL1_size);
  write_string( aBuffer, ASN_OCTETSTRING, theUL2, theUL2_size);
  write_string( aBuffer, ASN_OCTETSTRING, theITLB, theITLB_size);
  write_string( aBuffer, ASN_OCTETSTRING, theDTLB, theDTLB_size);
  write_string( aBuffer, ASN_OCTETSTRING, theBPred, theBPred_size);

  derWriteSequence( aBuffer, theMemUpdates.begin(), theMemUpdates.end());
  derWriteSequence( aBuffer, theSyscallUpdates.begin(), theSyscallUpdates.end());
}

int CkptEntry::contentLength() const {
  int size = 0;
  //Location
  size += integer_minimum_octets( static_cast<u_longlong>(theLocation) ) + 2;
  //DataBreakPoint
  size += integer_minimum_octets( static_cast<u_longlong>(theDataBreakPoint) ) + 2;
  //Regs
  size += theRegs_size + length_minimum_octets( theRegs_size ) + 1;
  //IL1
  size += theIL1_size + length_minimum_octets( theIL1_size ) + 1;
  //DL1
  size += theDL1_size + length_minimum_octets( theDL1_size ) + 1;
  //UL2
  size += theUL2_size + length_minimum_octets( theUL2_size ) + 1;
  //ITLB
  size += theITLB_size + length_minimum_octets( theITLB_size ) + 1;
  //DTLB
  size += theDTLB_size + length_minimum_octets( theDTLB_size ) + 1;
  //BPred
  size += theBPred_size + length_minimum_octets( theBPred_size ) + 1;

  size += derSequenceSize(theMemUpdates.begin(), theMemUpdates.end());
  size += derSequenceSize(theSyscallUpdates.begin(), theSyscallUpdates.end());

  return size;
}

void CkptEntry::cleanSyscalls() {
  std::list< SyscallUpdate >::iterator iter = theSyscallUpdates.begin();
  std::list< SyscallUpdate >::iterator end = theSyscallUpdates.end();
  while (iter != end) {
    delete [] iter->regs();
    ++iter;
  }
}


//MemUpdate
  //Addr : Int
  //Size : Int
  //Data : OctetString

MemUpdate * MemUpdate::createFrom(unsigned char const * aBuffer) {
  static MemUpdate update;
  if (aBuffer[0] != ASN_MEM_UPDATE) {
    throw BadASNError();
  }
  aBuffer++;
  unsigned long length = 0;
  length = read_length( aBuffer );

  update.theAddress = read_unsigned( aBuffer, ASN_UNSIGNED64 );
  read_object( aBuffer, const_cast< u_char const *>(update.theData), update.theSize);

  return &update;
}

void MemUpdate::derWrite(unsigned char * & aBuffer) const {
  int content = contentLength();
  *aBuffer = ASN_MEM_UPDATE;
  ++aBuffer;

  write_length( aBuffer, content );
  write_unsigned( aBuffer, ASN_UNSIGNED64, theAddress);
  write_string( aBuffer, ASN_OCTETSTRING, theData, theSize);
}

int MemUpdate::contentLength() const {
  int size = 0;
  //Address
  size += integer_minimum_octets( static_cast<u_longlong>(theAddress) ) + 2;
  //Data
  size += theSize + length_minimum_octets( theSize ) + 1;

  return size;
}


//SyscallUpdate
  //Icount : Int
  //Instr : Int64
  //regs : Opaque
  //SEQUENCE of MemUpdate

SyscallUpdate * SyscallUpdate::createFrom(unsigned char const * aBuffer) {
  static SyscallUpdate update;
  if (aBuffer[0] != ASN_SYSCALL_UPDATE) {
    throw BadASNError();
  }
  aBuffer++;
  unsigned long length = read_length( aBuffer );

  update.theICount = read_unsigned( aBuffer, ASN_UNSIGNED64 );
  update.theDataBreakPoint = read_unsigned( aBuffer, ASN_UNSIGNED64 );
  read_object( aBuffer, update.theRegs, update.theRegs_size);
  update.theMemUpdates_seq = read_sequence( aBuffer );
  return &update;
}

void SyscallUpdate::derWrite(unsigned char * & aBuffer) const {
  int content = contentLength();
  *aBuffer = ASN_SYSCALL_UPDATE;
  ++aBuffer;

  write_length( aBuffer, content );
  write_unsigned( aBuffer, ASN_UNSIGNED64, theICount);
  write_unsigned( aBuffer, ASN_UNSIGNED64, theDataBreakPoint);
  write_string( aBuffer, ASN_OCTETSTRING, theRegs, theRegs_size);
  derWriteSequence( aBuffer, theMemUpdates.begin(), theMemUpdates.end());
}

int SyscallUpdate::contentLength() const {
  int size = 0;
  //theICount
  size += integer_minimum_octets( theICount ) + 2;
  //theDataBreakPoint
  size += integer_minimum_octets( theDataBreakPoint ) + 2;
  //theRegs
  size += theRegs_size + length_minimum_octets( theRegs_size ) + 1;
  //theMemUdates
  size += derSequenceSize(theMemUpdates.begin(), theMemUpdates.end());

  return size;
}



