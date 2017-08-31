
==
Spikegenerator with fixed ISI, option VariableISI = False is set for this module:
====================================================================================

The fpga sram StimFile should contain one address per line.
The address should be composed of the following fourteen bits:

13,12,11,10,9,8,7,6,5,4,3,2,1,0
Neuron Address Pre 6:13
Virtual Source Chip ID 4:5
Destination Core 0:3

example :
143
79
..

The output of the FPGA spike generator is then a constant rate output that sends spike with these addresses.
The output rate is decided from the ISI and ISIBase parameters. ISIBase is X/90Mhz, and set the base multiplier for the ISI parameters. 

Example ISIBase = 90 then 90/90Mhz = 1us base multiplier. Then the ISI is expressed in 1us time base. This means that the FPGA Spike Generator will send output spikes (addresses 143 and 79) every 1*ISI us. 

==
Spikegenerator with variable ISI, option VariableISI = True is set for this module:
====================================================================================

The sram file should contain the address encoded as before comma the ISI interval in terms of ISIBase. Note that ISI parameter is not effective anymore as it is read from the file. 

example
143,1024
143,12
143,123
..

=
For an example of generation of a linearly increasing spike stimulus see the generate_input.py python script.
Note that the base address for the stimulus.txt must be set manually to 90 (as defined in the generate_input.py script).




