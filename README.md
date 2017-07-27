# caer


AER event-based framework, written in C, targeting embedded systems.<br />
[![Build Status](https://travis-ci.org/inilabs/caer.svg?branch=master)](https://travis-ci.org/inilabs/caer)

# Dependencies:

Linux, MacOS X or Windows (for Windows build instructions see README.Windows) <br />
cmake >= 2.6 <br />
gcc >= 5.2 or clang >= 3.6 <br />
libcaer >= 2.2.0 <br />
mini-xml (mxml) >= 2.7 <br />
libuv >= 1.7.5 <br />
Boost >= 1.50 (with system, filesystem, program_options) <br />
Optional: libpng >= 1.6 (input/output frame PNG compression) <br />
Optional: tcmalloc >= 2.2 (faster memory allocation) <br />
Optional: allegro5 >= 5.0.11 (visualizer module) <br />
Optional: OpenCV >= 3.1 (cameracalibration, poseestimation modules) <br />

# Installation

1) configure: <br />

$ cmake <OPTIONS> <MODULES_TO_BUILD> . <br />

The following options are currently supported: <br />
-DUSE_TCMALLOC=1 -- Enables usage of TCMalloc from Google to allocate memory. <br />

The following modules can currently be selected to be built: <br />
-DDVS128=1 -- DVS128 device input. <br />
-DEDVS=1 -- eDVS4337 device input. <br />
-DDAVIS=1 -- DAVIS device input. <br />
-DDYNAPSE=1 -- Dynap-se device input (neuromorphic chip). <br />
-DBAFILTER=1 -- Filter background activity (uncorrelated noise). <br />
-DFRAMEENHANCER=1 -- Demosaic/enhance frames. <br />
-DCAMERACALIBRATION=1 -- Calculate and apply single camera lens calibration. <br />
-DPOSEESTIMATION=1 -- Estimate pose of camera relative to special markers. <br />
-DSTATISTICS=1 -- Print statistics to console. <br />
-DVISUALIZER=1 -- Open windows in which to visualize data. <br />
-DINPUT_FILE=1 -- Get input from an AEDAT file. <br />
-DOUTPUT_FILE=1 -- Write data to an AEDAT 3.X file. <br />
-DINPUT_NETWORK=1 -- Read input from a network stream. <br />
-DOUTPUT_NETWORK=1 -- Send data out via network. <br />
-DROTATE=1 -- Rotate events. <br />
-DMEDIANTRACKER=1 -- Track points of high event activity. <br />
-DRECTANGULARTRACKER=1 -- Track clusters of events. <br />
-DDYNAMICRECTANGULARTRACKER=1 -- Track a variable number of clusters of events. <br />
-DSPIKEFEATURES=1 -- Create frames which represents decay of polarity events. <br />
-DMEANRATEFILTER=1 -- Measure mean rate of spike events. <br />
-DSYNAPSERECONFIG=1 -- Enable Davis240C to Dynap-se mapping  <br />
-DFPGASPIKEGEN=1 -- Enable FPGA spike generator Dynap-se <br />
-DPOISSONSPIKEGEN=1 -- Enable FPGA Poisson spike generator for Dynap-se <br />

To enable all just type: <br />
 cmake -DDVS128=1 -DEDVS=1 -DDAVIS=1 -DDYNAPSE=1 -DBAFILTER=1 -DFRAMEENHANCER=1 -DCAMERACALIBRATION=1  
 -DPOSEESTIMATION=1 -DSTATISTICS=1  -DVISUALIZER=1 -DINPUT_FILE=1 -DOUTPUT_FILE=1 -DINPUT_NETWORK=1  
 -DOUTPUT_NETWORK=1 -DROTATE=1 -DMEDIANTRACKER=1  -DRECTANGULARTRACKER=1 -DDYNAMICRECTANGULARTRACKER=1  
 -DSPIKEFEATURES=1  -DMEANRATEFILTER=1 -DSYNAPSERECONFIG=1 -DFPGASPIKEGEN=1 -DPOISSONSPIKEGEN=1 .
<br />
2) build:
<br />
$ make
<br />
3) install:
<br />
$ make install
<br />
# Usage

You will need a 'caer-config.xml' file that specifies which and how modules
should be interconnected. A good starting point is 'docs/davis-config.xml', 
or 'docs/dynapse-config.xml', please also read through 'docs/modules.txt' for 
an explanation of the modules system and its configuration syntax.
<br />
$ caer-bin (see docs/ for more info on how to use cAER) <br />
$ caer-ctl (command-line run-time control program, optional) <br />


