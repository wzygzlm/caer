
Spikegenerator with fixed ISI:

The fpga sram file should contain one address per line.
The address should be composed of the following bits:

13,12,11,10,9,8,7,6,5,4,3,2,1,0
Neuron Address Pre 6:13
Virtual Source Chip ID 4:5
Destination Core 0:3

example :
143
143
143
..

Spikegenerator with variable ISI

The sram file should contain the address as before comma the ISI 

example
143,1024
143,12
143,123
..



