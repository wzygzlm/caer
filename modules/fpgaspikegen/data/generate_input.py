# Author federico.corradi@inilabs.com
# script used to generate stimuli txt file for FPGASpikeGenerator
import numpy as np

# The timing of each spike in output is in unit of ISIBase. 
# One ISIBase is 1/90 Mhz = 11.11 ns. For example an ISI = 2  mean a spike every 22.22 ns.
isiBase = 90.0 # 1 us

def flattern_(litems):
    return [item for sublist in litems for item in sublist]

def make_stim_linear(neuronAddress, coreDest, virtualSourceChip, freqStart, freqStop, freqSteps, freqPhaseDuration):
    freqs = np.linspace(freqStart, freqStop, freqSteps) # in seconds
    periods = ( 1.0/freqs )*1e+6 ## in us units
    one_sec = freqPhaseDuration*1e+6  # in us
    
    periods_base = [int(i) for i in np.ceil(periods)]  #in isiBase unit
    
    #make address
    core_d = coreDest # to all cores    
    address = ( (neuronAddress & 0xff) << 6) | core_d & 0xf | (virtualSourceChip << 4) & 0x30
    
    #now generates
    addresses = []
    times = []
    for i in periods_base:
        num_entry_this_freq = one_sec / i                                   #one second duration for each phase                      
        addresses.append(np.repeat(address, num_entry_this_freq))           #addresses that will be stimulated
        times.append(np.repeat(i, num_entry_this_freq))                     #time difference between spikes
    
    times = np.transpose(flattern_(times))
    addresses = np.transpose(flattern_(addresses))
        
    return times, addresses
    
def write_to_file(times,addresses, filename="stimulus.txt"):
    f = open(filename, 'w')
    if(len(times) != len(addresses)):
        print("Error! Check your stimulus, times and addresses have different lenght")
        raise Exception
    if(len(times) > (2**15-1)):
        print("Error! Stimulus is too big, it will not fit in SRAM!")
        raise Exception            
    if(np.max(times) > 2**16-1):
        print("Error! Integer value for delay is too big, consider changing isiBase unit")
        raise Exception    
    for i in range(len(times)):            
        f.write(str(int(addresses[i]))+','+str(int(times[i]))+'\n')
    f.close()
    
    
if __name__ == '__main__':

    neuronAddress = 1
    coreDest = 15
    virtualSourceChip = 0
    freqStart = 50 # Hz
    freqStop = 250 # Hz
    freqSteps = 10
    freqPhaseDuration = 1 # sec
    
    # returns address and inter spike interval 
    times, addresses = make_stim_linear(neuronAddress, coreDest, virtualSourceChip, freqStart, freqStop, freqSteps, freqPhaseDuration)
    write_to_file(times,addresses)
    
    
    
    
    
    
