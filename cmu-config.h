/*
 * cmu-config.h - Configuration settings to enable/disble CMU-specific features
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

#ifndef CMU_CONFIG_H
#define CMU_CONFIG_H

/* This tag appears in the output and can be used to identify source code
   version associated with an output file*/
#define CMU_IMPETUS_VERSION_TAG "TurboSMARTSim 1.0"

/* This configuration switch enables or disables wattch energy and power statistics collection */
#define CMU_CONFIG_WATTCH_ENABLED

/* This configuration switch enables or disables wattch energy and power statistics collection */
/* Note: Dynamic power with WATTCH is untested */
/* #define CMU_CONFIG_WATTCH_DYNAMIC_POWER */

/* This configuration switch shuts off all code that has to do with debugging
   or gathering uninteresting statistics.  It completely disables ptrace and
   DLite debugging support. */
#define CMU_AGGRESSIVE_CODE_ELIMINATION_ENABLED


#ifdef CMU_CONFIG_WATTCH_ENABLED
#   define CMU_WATTCH_FEATURE_STRING "WATTCH_ENABLED  true # wattch code is enabled "
#   define CMU_WATTCH_ENABLED 1
#   define CMU_WATTCH(x) x
#   define STATIC_UNLESS_WATTCH
#   ifdef CMU_CONFIG_WATTCH_DYNAMIC_POWER
#      define CMU_WATTCH_DYNAMIC_POWER 1
#      define CMU_WATTCH_DYNAMIC_POWER_FEATURE_STRING "WATTCH_DYNAMIC_POWER true # wattch uses dynamic power\nNOTE: This mode is UNTESTED."
#    else
#      define CMU_WATTCH_DYNAMIC_POWER 0
#      define CMU_WATTCH_DYNAMIC_POWER_FEATURE_STRING "WATTCH_DYNAMIC_POWER false # wattch does not use dynamic power "
#   endif
#else
#   define CMU_WATTCH_FEATURE_STRING "WATTCH_ENABLED  false # wattch code is not enabled "
#   define CMU_WATTCH_DYNAMIC_POWER_FEATURE_STRING "WATTCH_DYNAMIC_POWER  false # wattch does not use dynamic power "
#   define CMU_WATTCH_ENABLED 0
#   define CMU_WATTCH_DYNAMIC_POWER 0
#   define CMU_WATTCH(x) while (0)
#   define STATIC_UNLESS_WATTCH
#endif

#ifdef CMU_AGGRESSIVE_CODE_ELIMINATION_ENABLED
#   define CMU_AGGRESSIVE_CODE_ELIMINATION_FEATURE_STRING "AGGRESSIVE_CODE_ELIMINATION  true # Kill all unneccessary code"
#   define CMU_AGGRESSIVE_CODE_ELIMINATION 1
#else
#   define CMU_AGGRESSIVE_CODE_ELIMINATION_FEATURE_STRING "AGGRESSIVE_CODE_ELIMINATION  false # Leave checking code intact"
#   define CMU_AGGRESSIVE_CODE_ELIMINATION 0
#endif

#endif /* CMU_CONFIG_H */
