# Pyrrha NS-3 simulator
This is an NS-3 simulator for Floodgate. It is based on [the NS3 simulation of HPCC](https://github.com/alibaba-edu/High-Precision-Congestion-Control) which also includes the implementation of DCQCN, TIMELY, DCTCP, PFC, ECN and Broadcom shared buffer switch. 

If you have any questions, please contact us.

## Quick Start

### Build
`./waf -d optimized configure`

`./waf build`

Please note if gcc version > 5, compilation will fail due to some ns3 code style.  If this what you encounter, please use:

`CC='gcc-5' CXX='g++-5' ./waf configure`

### Run
The direct command to run is:
`./waf --run 'third mix_withcc/config-*.ini'`

## Important Files
The core logic of Pyrrha was written in following files:

`point-to-point/model/switch-node.cc/h`: the node class for switch

`point-to-point/model/switch-mmu.cc/h`: the mmu module of switch
