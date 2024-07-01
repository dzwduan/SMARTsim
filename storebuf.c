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

#include <assert.h>
#include <memory.h>
#include "storebuf.h"

struct store_buffer_entry * theSBHead = 0;

struct store_buffer_entry * theSBEntryPool = 0;
struct store_buffer_entry ** theSBEntryFreeList = 0;
int theSBSize = 0;
int theSBCapacity = 0;

tick_t SB_next_release_cycle() {
    assert (! SB_empty());
    return theSBHead->release_cycle;
}

void SB_pop_head() {
    /* Error to pop from an empty queue */
    assert (! SB_empty());
    theSBSize--;
    theSBEntryFreeList[theSBSize] = theSBHead;
    theSBHead = theSBHead->next;
}

/* Note: multiple return paths */
void SB_enqueue(tick_t release_cycle) {
    struct store_buffer_entry * sb_entry;

    /* Error to enqueue in a full queue */
    assert(! SB_full());

    if (SB_empty()) {
    /* Special case - empty queue */
        theSBHead = theSBEntryFreeList[0];
        theSBSize++;
        theSBHead->release_cycle = release_cycle;
        theSBHead->next = 0;
    } else if (theSBHead->release_cycle < release_cycle) {
    /* Special case - insert at head */
        theSBEntryFreeList[theSBSize]->next = theSBHead;
        theSBHead = theSBEntryFreeList[theSBSize];
        theSBSize++;
        theSBHead->release_cycle = release_cycle;
    } else {
    /* General case */
       sb_entry = theSBHead;
       while (sb_entry->next && sb_entry->next->release_cycle < release_cycle) {
         sb_entry = sb_entry->next;
       }
       theSBEntryFreeList[theSBSize]->next = sb_entry->next;
       sb_entry->next = theSBEntryFreeList[theSBSize];
       theSBSize++;
       sb_entry->next->release_cycle = release_cycle;
    }
}



void SB_alloc(int anSBCapacity) {
    int i = 0;
    struct store_buffer_entry * ptr;
    assert( anSBCapacity > 0);

    theSBEntryPool = calloc(1, sizeof(struct store_buffer_entry) * anSBCapacity);
    theSBEntryFreeList = calloc(1, sizeof(struct store_buffer_entry *) * anSBCapacity);
    theSBCapacity = anSBCapacity;
    theSBSize = 0;

    ptr = theSBEntryPool;
    for (i=0; i < theSBCapacity; ++i) {
       theSBEntryFreeList[i] = ptr;
       ++ptr;
    }
}
