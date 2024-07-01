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
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>

static const int k = 1024;
static const int M = 1024 * k;

using namespace std;



std::string theBaseFilename;
bool theFileSet = false;
int theFileNo = 0;

char theFilename[2048];
FILE * theFile = 0;

std::vector< std::string > theNames;
std::vector< std::pair< int, long> > theCkptIndex;
unsigned long theNextSeek = 0;

long advance(FILE * aFile) {
  fseek(aFile, theNextSeek, SEEK_CUR);
  unsigned long pos = ftell(aFile);
  if ( ! fread(&theNextSeek, sizeof(unsigned long), 1, aFile) ) {
    return -1;
  }
  return pos;
}

void openNextFile() {
  if (theFile) {
    fclose(theFile);
    theFile = 0;
  }
  if (! theFileSet ) {
    if (theFileNo > 0) {
      return;
    }
    strcpy(theFilename, theBaseFilename.c_str());
  } else {
    sprintf(theFilename, "%s.%02d.lvpt",theBaseFilename.c_str(), theFileNo);
  }
  std::cerr << "Scanning: " << theFilename << std::endl;
  theFile = fopen(theFilename,"r");
  theNextSeek = 0;

  ++theFileNo;
}

void buildDirectory() {
  while (true) {
    openNextFile();

    if (theFile) {
      theNames.push_back(std::string(theFilename));
      int file_idx = theNames.size() - 1;
      while (! feof(theFile) ) {
         long position = advance(theFile);
         if (position > 0) {
            theCkptIndex.push_back( std::make_pair( file_idx, position) );
         }
      }
    } else {
      break;
    }
  }
}

int theOutputSets = 1;
int theCkptsPerFile = 400;
int theNextSet = 0;
FILE * theOutputFile = 0;
int theOutputFileNo = 0;

static const int Kb = 1024;
static const int Mb = 1024 * Kb;

char buffer[ 11 * Mb ];

char theHeader[ 1 * Mb ];
unsigned long theHeaderLength;

void writeNextSet() {
  if(theOutputFile) {
    fclose(theOutputFile);
    theOutputFile = 0;
  }
  if (theNextSet >= theOutputSets) {
    return;
  }
  std::cout << "Advanced to next live-point set."<< std::endl;
  theOutputFileNo = 0;
  if (theOutputSets > 1) {
    sprintf(theFilename, "shuffled-%d.%s.%02d.lvpt",theNextSet, theBaseFilename.c_str(), theOutputFileNo);
  } else {
    sprintf(theFilename, "shuffled.%s.%02d.lvpt",theBaseFilename.c_str(), theOutputFileNo);
  }
  theOutputFile = fopen( theFilename, "w");
  if (! theOutputFile) return;
  fwrite(&theHeaderLength, sizeof(unsigned long), 1, theOutputFile);
  fwrite(&theHeader, theHeaderLength, 1, theOutputFile);
  std::cout << "Opened output file: " << theFilename << std::endl;
  ++theNextSet;
}

void writeNextFile() {
  if(theOutputFile) {
    fclose(theOutputFile);
    theOutputFile = 0;
  }
  ++theOutputFileNo;
  if (theOutputSets > 1) {
    sprintf(theFilename, "shuffled-%d.%s.%02d.lvpt",theNextSet-1, theBaseFilename.c_str(), theOutputFileNo);
  } else {
    sprintf(theFilename, "shuffled.%s.%02d.lvpt",theBaseFilename.c_str(), theOutputFileNo);
  }
  theOutputFile = fopen( theFilename, "w");
  if (! theOutputFile) return;
  fwrite(&theHeaderLength, sizeof(unsigned long), 1, theOutputFile);
  fwrite(&theHeader, theHeaderLength, 1, theOutputFile);
  std::cout << "Opened output file: " << theFilename << std::endl;
}


void writeCheckpoints() {
  int file_ckpt_count = 0;
  int set_ckpt_count = 0;
  int ckpts_per_set = theCkptIndex.size() / theOutputSets;
  int total_ckpts = 0;

  FILE * hdr = fopen(theNames[0].c_str(),"r");
  fread(&theHeaderLength, sizeof(unsigned long), 1, hdr);
  fread(&theHeader, theHeaderLength, 1, hdr);
  fclose(hdr);

  writeNextSet();

  std::vector< std::pair< int, long> >::iterator iter = theCkptIndex.begin();
  std::vector< std::pair< int, long> >::iterator end = theCkptIndex.end();
  while (iter != end) {
    if (set_ckpt_count > ckpts_per_set) {
      writeNextSet();
      file_ckpt_count = 0;
      set_ckpt_count = 0;
    }
    if (file_ckpt_count > theCkptsPerFile) {
      writeNextFile();
      file_ckpt_count = 0;
    }
    if (! theOutputFile) {
      break;
    }

    std::cout << theNames[iter->first] << ":" << iter->second << std::endl;

    FILE * in = fopen(theNames[iter->first].c_str(), "r");
    fseek(in, iter->second, SEEK_SET);
    unsigned long length;
    fread(&length, sizeof(unsigned long), 1, in);
    fread(&buffer, length, 1, in);
    fclose(in);

    fwrite(&length, sizeof(unsigned long), 1, theOutputFile);
    fwrite(&buffer, length, 1, theOutputFile);

    ++file_ckpt_count;
    ++set_ckpt_count;
    ++total_ckpts;
    ++iter;
  }

  std::cout << std::endl << "Wrote: " << total_ckpts << std::endl;
}



void printDirectory() {
  std::vector< std::pair< int, long> >::iterator iter = theCkptIndex.begin();
  std::vector< std::pair< int, long> >::iterator end = theCkptIndex.end();
  while (iter != end) {
    std::cout << theNames[iter->first] << ":" << iter->second << std::endl;
    ++iter;
  }

  std::cout << std::endl << "Total Live-points: " << theCkptIndex.size() << std::endl;
}

void shuffleDirectory() {
  std::random_shuffle(theCkptIndex.begin(), theCkptIndex.end());
}


void usage() {
  std::cout << "Usage:" << std::endl;
  std::cout << "   shuffle <base file name> [<output file sets> <live-points per file>]" << std::endl;
}

int main(int argc, char ** argv) {
  if (argc < 2 || argc > 4) {
    usage();
    std::exit(0);
  }
  theBaseFilename = argv[1];

  if (argc >= 3 ) {
    theOutputSets = atoi(argv[2]);
  } else {
    theOutputSets = 1;
  }

  if (argc >= 4 ) {
    theCkptsPerFile = atoi(argv[3]);
  } else {
    theCkptsPerFile = 10000;
  }

  /* See if the theBaseFilename is a complete filename) */
  FILE * exist = fopen(theBaseFilename.c_str(), "r");
  if ( exist ) {
    fclose(exist);

    theFileSet = false;
  } else {
    /* See if theBaseFilename is a valid set */
    sprintf(theFilename, "%s.00.lvpt",theBaseFilename.c_str());

    exist = fopen(theFilename, "r");
    if ( ! exist ) {
      std::cerr << "Specified checkpoint file or fileset does not exist" << std::endl;
      exit(0);
    }
    theFileSet = true;
    fclose(exist);
  }

  buildDirectory();

  shuffleDirectory();

  writeCheckpoints();
}