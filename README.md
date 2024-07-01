# TurboSMARTSim

refer https://parsa.epfl.ch/simflex/smarts.html

version 2.1
Note: Requires gcc-2.95.3 to compile.


TurboSMARTSim Written by Thomas Wenisch & Roland Wunderlich
Document revision 2.1 5/27/2005

This README file describes the features of the TurboSMARTSim simulator and how
they can be used to obtain fast and accurate simulation results.

TurboSMARTSim = SimpleScalar 3.0 + WATTCH + SMARTS + Live-points

Live-points, the checkpointing technique by TurboSMARTSim, is described in:
  T. Wenisch, R. Wunderlich, B. Falsafi, and J. Hoe, "Simulation Sampling with
  Live-points", Computer Architecture Lab at Carnegie Mellon Tech Report
  #2004-03a.

This technical report has been included in this distribution as: live-points.pdf

The SMARTS sampling technique used in TurboSMARTSim is described in detail in:
  R. Wunderlich, T. Wenisch, B. Falsafi, and J. Hoe, "SMARTS: Accelerating
  Microarchitecture Simulation via Rigorous Statistical Sampling",
  Appearing in the Proceedings of the 30th Annual International Symposium
  on Computer Architecture, 2003.

This paper has been included in this distribution as: smarts2003isca.pdf

This README assumes the reader is familiar with SimpleScalar, Live-points,
and SMARTS. It is highly recommended that you read and familiarize yourself
with the Live-points and SMARTS papers before reading the remainder of this
documentation.

This software distribution is a part of the SimFlex project, and is available
from the SimFlex website
  http://www.ece.cmu.edu/~simflex


LEGAL NOTICES:
==============

Redistributions of any form whatsoever must retain and/or include the
following acknowledgment, notices and disclaimer:

This product includes software developed by Carnegie Mellon University.

Copyright (c) 2005 by Thomas F. Wenisch, Roland E. Wunderlich, Babak Falsafi
and James C. Hoe for the SimFlex Project, Computer Architecture Lab at
Carnegie Mellon, Carnegie Mellon University

This source file includes SMARTSim extensions originally written by
Thomas Wenisch and Roland Wunderlich of the SimFlex Project.

For more information, see the SimFlex project website at:
  http://www.ece.cmu.edu/~simflex

You may not use the name "Carnegie Mellon University" or derivations
thereof to endorse or promote products derived from this software.

If you modify the software you must place a notice on or within any
modified version provided or made available to any third party stating
that you have modified the software.  The notice shall include at least
your name, address, phone number, email address and the date and purpose
of the modification.

THE SOFTWARE IS PROVIDED "AS-IS" WITHOUT ANY WARRANTY OF ANY KIND, EITHER
EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT LIMITED TO ANY WARRANTY
THAT THE SOFTWARE WILL CONFORM TO SPECIFICATIONS OR BE ERROR-FREE AND ANY
IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE,
TITLE, OR NON-INFRINGEMENT.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
BE LIABLE FOR ANY DAMAGES, INCLUDING BUT NOT LIMITED TO DIRECT, INDIRECT,
SPECIAL OR CONSEQUENTIAL DAMAGES, ARISING OUT OF, RESULTING FROM, OR IN
ANY WAY CONNECTED WITH THIS SOFTWARE (WHETHER OR NOT BASED UPON WARRANTY,
CONTRACT, TORT OR OTHERWISE).


TurboSMARTSim
=============

TurboSMARTSim adds live-points support to the detailed cycle-accurate processor
simulator from SimpleScalar 3.0c.  TurboSMARTSim extends the functionality of
SMARTSim from the SimFlex project at Carnegie Mellon, builds on
SimpleScalar 3.0c by Todd Austin and Doug Burger, and the WATTCH enhancements
from David Brooks.

TurboSMARTSim generates a live-point library to store on disk.  The live-point
library captures the state required to simulate a representative sample
of a benchmark.  Each live-point contains the architectural and
microarchitectural state neccessary to simulate a small execution window from
the benchmark.  By simulating a random subset of the available live-points,
TurboSMARTSim enables rapid and accurate sampled simulation of the complete
benchmark.

Each live-point stores registers, memory state, cache tags, TLB contents and
branch predictor contents for its corresponding execution window.  When a
live-point library is created, the configuration for these structures must
specified.  A live-point library can be used to simulate smaller and less
associative caches and TLBs (the smaller structure will be reconstructed
during checkpoint loading), however, the branch predictor configuration cannot
be changed.

TurboSMARTSim includes a modified sim-outorder from SimpleScalar that supports
SMARTS.  All other SimpleScalar simulators are unmodified from SimpleScalar.
Note that sim-eio generated checkpoints are not supported by our modifed
sim-outorder.  In addition, TurboSMARTSim adds:

  sim-mklvpt - for creating live-point libraries
  shuffle    - for shuffling live-point libraries into random order
  sim-oolvpt - for running out-of-order simulations from a live-point library


Running a live-point experiment with TurboSMARTSim
==================================================

This section gives a step-by-step description of how to create and use a
live-point library with TurboSMARTSim.  We detail the tools and command lines
neccessary to follow the procedure described in Section 6.3 and Figure 6 of the
accompanying Live-point tech report.  We suggest that the reader refer to this
sub-section and figure in the tech report while following these instructions.

Throughout this section, we give sample command lines and simulator outputs for
the "test" input of the "mcf" SPEC CPU 2000 benchmark.  You can follow along
with the examples by building all the tool binaries with "make," and copying the
tools, the mcf binary and test input to the same directory.  Exact simulator
outputs will be slightly different from the examples included here, because of
differing benchmark execution environment and different random seeds for
shuffling.  (Because of licensing reasons, we do not include the mcf sources,
binary, or input files in our distribution.  More information on obtaining SPEC
benchmarks for SimpleScalar is available at www.spec.org and
www.simplescalar.com).

Step 1 - Measure baseline variance
------
sim-outorder -estimate_k 1:3.0:3.0 mcf inp.in
>> Estimates the k value needed by Step 2 for 99.7% confidence of +/- 3% error

Before we can create a live-point library, we must determine an appropriate
sample size (number of live-points) for the library for a particular benchmark.
The requred sample size depends on the desired target confidence and the metric
variability of the particular benchmark under investigation.  Some SPEC 2000
benchmarks have highly homogenous performance, while others are composed of many
distinct phases, resulting in a wide range of sample size requirements.  For
99.7% confidence of +/- 3% error, 10,000 live-points is sufficient for most of
the SPEC 2000 benchmarks, but is slightly too small, or far too large in some
cases.

We have added support to sim-outorder to run a simulation which measures
benchmark variability and suggests an appropriate sample size.  This simulation
mode uses the SMARTS sampling methodology with a rough guess at an appropriate
sample design to measure variability and recommend a precise sample size.  The
simulators outputs the recommended size and a corresponding k value for
"1 in k" systematic sampling.  The k-value is computed with the formula
    k = (benchmark length) / (execution window size) / (recommended sample size)
We recommend an execution window size of 1000 instructions as explained in the
SMARTS paper. The resulting k value will be passed to sim-mklvpt to generate a
live-point library.

To use the automatic k-estimation feature of sim-outorder, you must know the
approximate length of the benchmark and choose the highest statistical
confidence you want the live-point library to be able to achieve.  The benchmark
length is specified in billions of instructions and can be very approximate
(e.g. within a factor of +/-10).  The length is used to choose a sample design
for the k estimation simulation.  If you specify a length shorter than the
actual benchmark length, the simulation will take a bit longer than neccessary.
If you specify a greatly longer length, the sample size estimate may be less
accurate.

For the desired confidence, you must choose both the confidence level (e.g., 95%
or 99.7% confidence) and desired error interval (e.g., +/- 3%).  The confidence
level is passed to the simulator as a "Z-score", the value of the unit normal
curve for the specified confidence level.  A Z-score of 1.96 corresponds to 95%
confidence, and 3.0 corresponds to 99.7% confidence.  The desired error interval
is specified as a percent error (3.0 corresponds to +/- 3%).  We use 99.7%
confidence of +/- 3% error (corresponding to a Z-score of 3.0 and a percent
error of 3.0) in our SMARTS and Live-point reports.

To run a simulation to estimate k, use the following command line options:
  sim-outorder -estimate_k <B-inst>:<Z-score>:<pct-err> [ rest of command line ]

  where:
    <B-inst: integer> - approximate benchmark length, in billions of
       instructions
    <Z-score: float> - desired confidence level for the sample that sim-outorder
       designs.  Reasonable values are 3.0 for 99.7% confidence, and 1.96 for
       95% confidence.
    <pct-err: float> - desired target percent error for the sample that
       sim-outorder designs.  A reasonable choice is 3.0 (for +/- 3% error)

For mcf, the complete command is:
  sim-outorder -estimate_k 1:3.0:3.0 mcf inp.in

At the end of simulation, sim-outorder will print:

  Determined checkpoint library size for sample_CPI_mean @ Z=3.00 pctError=3.00
  Library Size: 5912  Corresponding k: 43  Unit size: 1000

The "Corresponding k" value must be passed to sim-mklvpt in Step 2.


Step 2 - Collect live-points
------
sim-mklvpt -sampling 43 -library mcf-test mcf inp.in
>> Generates live-point library in files mcf-test.??.lvpt.gz

Once you have a k-value, either obtained through step 1 or by computing a k
value from a benchmark length and sample size chosen in some other fashion,
you can collect a live-point library.  Live-point libraries are created with
the sim-mklvpt simulator.

A live-point library places some restrictions on the simulator configuration
for subsequent experiments that use the library.  You must choose these
parameters when you create the live-point library.  In particular, you must
choose:
   - detailed warming and measurement periods (i.e., execution window length)
   - branch predictor configuration
   - Maximum cache and TLB sizes and associativities

The warming and measurement interval lengths determine the number of
instructions over which memory and system call state are collected.  The default
values are 2000 instructions of detailed warming and 1000 instructions of
measurement, which is appropriate for the default (8-way) simulator
configuration.  A live-point library created for particular detailed warming and
measurement periods also supports shorter periods.

The complete branch predictor state at the start of the execution window is
stored in each live-point.  This release of TurboSMARTSim does not support
storing multiple or configurable branch predictor state in a live-point.
sim-oolvpt will report an error if its branch predictor configuration does not
match the configuration stored in a live-point library.

When sim-mklvpt stores cache and TLB state, it stores it in a form that can
reconstruct smaller and less associative caches.  Thus, when creating
live-points, you must choose the maximum sizes of interest for these
structures.

To create a live-point library, use the following command:
  sim-mklvpt -sampling <k> -library <file name> [ rest of command line ]

  where:
    <k> - recommended k value from Step 1
    <file name> - filename prefix for the livepoint library files.

For mcf, the complete command is:
  sim-mklvpt -sampling 43 -library mcf-test mcf inp.in

(For mcf-test, live-point generation will take 2-3 times as long as the
simulation for Step 1.)

At the end of simulation, the output from sim-mklvpt will include statistics
similar to:

  sim_ckpt_insn      18114000 # total number of instructions included in live-points
  sim_pop_insn      259643339 # total number of instructions in application
  sim_num_ckpt           6038 # Number of checkpoints collected
  sim_ckpt_lines       807907 # Number of cache lines stored in all live-points
  lines_per_ckpt     133.8037 # average cache lines per live-point
  sim_sample_size        6038 # sample size when sampling is enabled
  sim_sample_period     43000 # sampling period when sampling is enabled

The live-point library will be stored in a file called mcf-test.00.lvpt.gz.


Step 3 - Shuffle live-point library
------
shuffle mcf-test
>> Shuffles the live-point library into random order on disk

To allow for the online result reporting capability of sim-oolvpt, a live-point
library must be shuffled into a random order.  The shuffle utility program
performs this operation.

shuffle also provides the capability to split a live-point library over
multiple files, to adjust file size or allow for parallel simulation on
multiple hosts.  However, this release of TurboSMARTSim does not provide support
for aggregating statistics from parallel simulations.

To shuffle a live-point library, use the following command:
  shuffle <file name>

  where:
    <file name> - filename prefix for the live-point library files.

For mcf, the complete command is:
  shuffle mcf-test

(The shuffling process will require a few minutes.)

The shuffled live-point library will be stored in a file called
shuffled.mcf-test.00.lvpt.gz.  Once this step is complete, the original
unshuffled library should be deleted.  In particular, it should not be used for
simulations as this may lead to simulation results biased towards the beginning
of the benchmark.


Step 4 - Baseline experiment
------
sim-oolvpt -all_lvpts -library shuffled.mcf-test -paired:write mcf.baseline mcf
>> Writes out the mcf.baseline file needed for matched-pair experiments

You now have a complete live-point library that can be used for absolute
performance estimates.  Live-points are processed with the sim-oolvpt
simulator.  To run a live-point experiment, you must pass sim-oolvpt the base
filename of the shuffled live-point library created in Step 3, and the path to
the benchmark binary (to load the code for the benchmark).  Note that it is not
neccessary to pass the benchmark's input files and parameters to sim-oolvpt, as
the live-points record the results of all system calls that occur during
simulation.

By default, sim-oolvpt will continue processing checkpoints until the
estimated CPI reaches 99.7% confidence of +/- 3% error.  This confidence target
can be controlled with the -target:z and -target:pctError command line
parameters.  During simulation, sim-oolvpt will periodically print an
estimated CPI and confidence for the live-points processed thus far, and
estimate how many more live-points must be processed to reach the target
confidence.  You can press CTRL-C at any time to halt the simulation and print
statistics for the sample that has already been measured.  To disable target
confidence and online results reporting, specify the -all_lvpts parameter.

To run an absolute performance estimate to a desired confidence, use the
following command:

  sim-oolvpt -library <library file> -target:z <z-score>
    -target:pctError <pct-error> [ rest of command line ]

  where:
    <file name> - filename prefix for the live-point library files.
    <z-score> - desired confidence level for the sample. Typical choices are
      1.96 for 95% confidence, and 3.0 for 99.7% confidence.  Other values are
      available in any elementary statistics textbook.
    <pct-error> - desired target percent error for the sample.

To run mcf to 95% confidence of +/- 5% error, the complete command is:
  sim-oolvpt -library shuffled.mcf-test -target:z 1.96 -target:pctError 5.0 mcf

(The simulation will require 15-30 seconds.)

At the end of simulation, the output statistics will include:
  sample_CPI_mean              1.9299 +/- 5.00% # sampled cycles per instruction
  sample_EPI_mean             42.0742 +/- 3.65% # sampled energy per instruction

sample_CPI_mean indicates the estimated cycles per instruction  and
corresponding confidence interval (using the confidence level specified for
target_z). sample_EPI_mean indicates the estimated energy per instruction.

To be able to perform matched-pair sampling for comparative performance studies,
sim-oolvpt must write a file containing the base system CPI for all live-points.
Matched-pair comparison drastically reduces the sample that must be measured
for comparative studies.

To create a matched-pair comparison file for the baseline configurtion, use the
following command:
  sim-oolvpt -library <library file> -paired:write <data file> [ rest of command line ]

  where:
    <library file> - filename prefix for the livepoint library files.
    <data file> - filename where paired-measurement data should be written.

For mcf, the complete command is:
  sim-oolvpt -all_lvpts -library shuffled.mcf-test -paired:write mcf.baseline mcf

(The simulation will take several minutes.)

The simulation will generate a file called mcf.baseline that contains the
measured CPI for each live-point in the library.  This file can be passed to
sim-oolvpt for comparative studies as described in Step 5.  Note that if you
reshuffle the live-point library, the paired-measurement file must be
regenerated (to match the new live-point order).  You should set the
microarchitecture configuration for this step to be your 'baseline'
configuration from which you will be making comparisons of performance.


Step 5 - Matched-pair experiments
------
sim-oolvpt -ruu:size 32 -library shuffled.mcf-test -paired:compare mcf.baseline mcf
>> Matched-pair sampling experiment, extimates performance delta from baseline

Once you have created a baseline CPI file for a live-point library, you can run
matched pair comparison experiments.  In matched-pair comparison, sim-oolvpt
uses the CPI measurements from the baseline simulation to estimate the change
in CPI in a subsequent simulation.  Change in CPI can typically be assessed with
a far smaller sample than absolute performance, in particular, if the overall
performance difference is small.  The simulation will stop as soon as the
confidence on the CPI change has reached the target.  The target can be changed
using the same command line switches as for absolute performance estimates.

To run a matched-pair comparison experiment, use the following command:
  sim-oolvpt -library <library file> -paired:compare <data file> [ rest of command line ]

  where:
    <library file> - filename prefix for the livepoint library files.
    <data file> - filename where paired-measurement data should be read.

For compare mcf with a smaller RUU to the baseline data created above:
  sim-oolvpt -ruu:size 32 -library shuffled.mcf-test
    -paired:compare mcf.baseline mcf

(The simulation will require 15-30 seconds.)

At the end of simulation, the output statistics will include:
  paired_delta_mean           +14.48% +/- 3.00% # Mean delta on paired-measurement statistic

The paired_delta_mean statistic indicates the estimated change in CPI calculated
as ( new mean CPI - old mean CPI) / (old mean CPI).  The result above indicates
that reducing the RUU size increases CPI (decreases performance) by a percentage
within the interval from 11.48% to 17.48%.


SMARTS systematic sampling support
==================================

The sim-outorder simulator included with TurboSMARTSim also supports SMARTS
sampling simulation, as in SMARTSim.  The SMARTS framework adds sampling
capabilities to sim-outorder, so that only a fraction of each benchmark is
simulated using the full cycle-accurate processor model.  The remainder of the
benchmark is simulated in a mode much like a combination of the sim-cache and
sim-bpred simulators, where cache and branch predictor state are updated, but
the remainder of the processor is not simulated in detail, and no measurements
are collected.

Systematic sampling collects measurements at even intervals.  Each measurement
is collected over a unit of consecutive instructions.  The size of this unit
is called the measurement unit size, and is controlled by the
-sampling:m-unit command line parameter.  In each sampling period,
TurboSMARTSim will collect measurements for the first unit of instructions, and
then skip past the succeeding units, fast-forwarding through the skipped
instructions as quickly as possible.  The number of units in one period is
controlled by the -sampling parameter.  Specifying -sampling enables sampled
simulation.

Prior to starting each measurement, TurboSMARTSim can simulate instructions using
the detailed cycle-accurate model, without collecting any measurements.  This
allows the detailed-simulator state to be warmed up.  The length of this
warmup period is measured in instructions, and can be as short as 0
instructions (no detailed warmup) or as long as (measurement period - m-unit)
instructions (always run the detailed simulator).  For the default
configuration of the simulator (an 8-way superscalar microarchitecture), 2000
instructions of detailed warmup is sufficient, as described in the SMARTS paper.

Between sampling units, TurboSMARTSim can simply fast-forward past instructions,
after the fashion of sim-fast, or can update cache, branch predictor and TLB
state, which we refer to as functional warming.  By default, functional
warming between sampling units is enabled.  It can be disabled with the
-sampling:allwarm parameter.


REFERENCE
=========

Command line options
====================

sim-outorder
------------

-estimate_k <sample design: string>
    Tells sim-outorder to run a simulation to determine an appopriate live-point
  library size and corresponding k value for a new benchmark.  At the end of
  simulation, sim-outorder will print the recommended library size and k value,
  which can then be passed to sim-ckpt.  The sample design parameter is
  composed of three parts, separated by colons, as follows:
    <B-instr>:<Z-score>:<percent error>
  where:
    <B-instr: integer> - approximate benchmark length, in billions of
       instructions
    <Z-score: float> - desired confidence level for the sample that sim-outorder
       designs.  Reasonable values are 3.0 for 99.7% confidence, and 1.96 for
       95% confidence.
    <percent error: float> - desired target percent error for the sample that
       sim-outorder designs.  A reasonable choice is 3.0 (for +/- 3% error)
  Specifying estimate_k runs a simulation as if the following parameters were
  specified:
    -sampling [ (benchmark length) / (m-unit) / 8000 ]
    -sampling:all_warm true

-sampling <k value: long integer>
    Enables SMARTS sampling and sets the k value for "1 in k" sampling.  The
  first sampling unit of each consecutive group of k sampling units will be
  measured.  Thus, the sampling period is (k * sampling unit size) and the
  sample size is ( benchmark length / k / sampling unit size). The statistics
  printed at the end of simulation reflect only the measured sample.

-sampling:m-unit <unit size: long integer]
    Sets the size of each measurement unit.  This corresponds to the
  variable U referred to in the SMARTS paper.  Each measurement will be
  taken of this many consecutive instructions.  k * m-unit determines the
  sampling period.  The default value is 1000.  As explained in the SMARTS
  paper, this default is near optimal for all SPEC benchmarks.

-sampling:w-unit <detailed warming size: long integer>
    Sets the size of the detailed warmup period prior to each measurement,
  in instructions.  The detailed simulator will execute this many instructions
  prior to taking measurements.  This value may not exceed (k-1) * m-unit.
  The detailed warmup period defaults to 2000 instructions.  This value is
  sufficient for the default configuration of the simulator (an 8-wide
  superscalar configuration), but needs to be increased if new structures are
  added to the cycle-accurate model which remember history of more than
  2000 instructions.

-sampling:allwarm <true | false>
    Enables or disables functional warming between sampling units.  When
  enabled, the cache, branch predictor, and TLB state is updated whenever
  the simulator is fast-forwarding.  When disabled, the normal sim-fast code
  is used, and no state updates take place.  Note that enabling functional
  warming places certain restrictions on your simulator configuration, as
  some assumptions are made in the functional warming code to increase its
  speed.  See "Assumptions of the functional warming code" for more
  information.

sim-mklvpt
----------

-library <live-point base filename: string>
    Specifies the base filename to which live-points will be written.
  Live-points will be written to a sequence of files named
  <live-point base filename>.<##>.lvpt.gz.  <##> starts at 00 and increments
  if the live-points are split over multiple files (to avoid hitting
  file-system size limitations).  The number of live-points in each file is
  controlled via -ckpt_per_file.

-lvpt_per_file <live-points per file: long integer>
    Specifies the maximum number of live-points to store in one file.  The
  default is 10,000, which will produce output files up to about 300MB each.

-sampling <k value: long integer>
    Specifies k value for checkpoint creation, as for sim-outorder above.  An
  appropriate choice for k can be obtained by running sim-outorder on a
  benchmark with -estimate_k.

-sampling:m-unit <unit size: long integer]
-sampling:w-unit <detailed warming size: long integer>
-sampling:allwarm <true | false>
     Same as for sim-outorder above.

-benchmark <benchmark name>
     The specified benchmark name is recorded in the live-point library file
  header.  The recorded header information is printed by sim-oolvpt when a
  live-point library is processed.  This field can be used to label live-point
  libraries, but has no other effect.

sim-oolvpt
----------
-library <live-point base filename: string>
    Specifies the base filename to which live-points will be written.
  Live-points will be written to a sequence of files named
  <live-point base filename>.<##>.lvpt.gz.  <##> starts at 00 and increments
  if the live-points are split over multiple files (to avoid hitting
  file-system size limitations).  The number of live-points in each file is
  controlled via -ckpt_per_file.

-sampling:m-unit <unit size: long integer>
-sampling:w-unit <detailed warming size: long integer>
    Same as for sim-outorder above.  Note that it is ok to specify m-unit and
  w-unit below those used to create a live-point library, but (m-unit + w-unit)
  specified here may not exceed (m-unit + w-unit) used to create the live-point
  library.

-target:z <z-score: float>
    Specify the Z score for the desired confidence level of the simulation's
  CPI result.  1.96 corresponds to 95% confidence; 3.0 corresponds to 99.7%
  confidence.

-target:pctError <pct-error: float>
    Specify the desired confidence interval of the simulation's CPI result.
  The default, 3.0, corresponds to 3%.

-all_lvpts
    Disables target confidence and causes all available live-points to be
  processed

-paired:write <file name: string>
    Write matched-pair sample comparison data for the base case to the specified
  file.  The file records the CPI value of each live-point.

-paired:compare <file name: string>
    Perform matched-pair sample comparison against the base case in the
  specified file.


New/changed statistics
======================

sim_meas_insn
     Number of instructions measured.

sim_detail_insn
     Number of instructions simulated in detail.  This includes both
   instructions that are measured, and instructions that are executed in
   detailed warmup

sim_pop_insn
     The total number of instructions simulated in detail or in
   fast-forwarding.  When the benchmark is run to completion, this is the
   length of the benchmark in instructions

sim_sample_size
     The number of measurements that were taken.

sim_sample_period
     Number of instructions in one sampling period.  This is always k *
   m-unit.

sim_inst_rate
     The number of instructions measured per second.  This rate will be
   heavily influenced by choice of k.

sim_pop_rate
     The number of instructions simulated per second.  This includes both
   detailed and fast-forward simulation.

sim_meas_cycle
     The total number of cycles that were measured.  sim_meas_insn
   instructions committed in sim_meas_cycle cycles.

sample_CPI_mean
     The CPI estimate over the sample and its corresponding confidence interval.
   The confidence level is controlled by the -target:z parameter (unless
   overridden in the simulator source code).

sample_EPI_mean
     The energy per intruction estimate over the sample and its corresponding
   confidence interval.

paired_delta_mean
     The change relative to the base case for matched-pair sample simulations.
   The reported result is ( new cpi - base cpi ) / (base cpi).


Options controlled from cmu-config.h
====================================

cmu-config.h contains some compile time constants that enable/disable
certain features in the code.

CMU_IMPETUS_VERSION_TAG
     This string appears in all simulation run outputs to indicate the version
  of TurboSMARTSim that was used.

CMU_CONFIG_WATTCH_ENABLED
     When defined, the WATTCH code in sim-outorder is enabled.  When this is
   not defined, the WATTCH code is eliminated by #ifdef directives.  The
   TurboSMARTSim output indicates whether this setting was enabled or not.

CMU_CONFIG_WATTCH_DYNAMIC_POWER
     When defined, the WATTCH will use dynamic population counts to estimate
   power.  This corresponds to the DYNAMIC_AF setting in WATTCH.  See the
   WATTCH documentation for details.  TurboSMARTSim has not been tested with
   CMU_CONFIG_WATTCH_DYNAMIC_POWER enabled.

CMU_AGGRESSIVE_CODE_ELIMINATION_ENABLED
     When defined, all debugging and assertion code in sim-outorder is
   disabled by #ifdef directives.  This disables pipe-tracing, DLite
   debugging, and a number of other SimpleScalar features.  Enabling this
   #define increases the speed of sim-outorder by a bit (~10%).

Assumptions of the functional warming code
==========================================

Simulation speed when sampling is greatly influenced by the speed of
functional warming.  Thus, we have gone to great lengths to optimize the
functional warming code as much as possible.  Because of this, some of the
flexibility of the SimpleScalar cache and branch prediction code was removed
from the fast functional warming code, resulting in significant performance
improvement.

The functional warming code assumes a unified L2 cache, and a combined branch
predictor.  Further, it assumes LRU replacement policy in all caches and
TLBs.  If you wish to simulate another configuration, the functional warming
code will need to be modified appropriately.  The simulator will print a
fatal error if you attempt to run it in an unsupported mode with functional
warming enabled.

The PISA Target
===============

We have only tested and only support TurboSMARTSim targeting the Alpha ISA.
However, we do not know of any specific deficiencies in the code that
would cause it to fail if targetting the PISA instruction set.

EIO Support
===========

The EIO functionality of SimpleScalar 3.0 is disabled in this release, to avoid
confusion with the Live-points functionality.
