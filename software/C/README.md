adsb_cape C sources
===================

Software for a Beaglebone-based ADS-B receiver. More details
about the design are at the following site:

http://ebrombaugh.studionebula.com/radio/adsb-cape/index.html

LICENSE
=======

C sources for bcc_tool, bcc_lib and adsb_tool are provided under the GPL V2.
C sources for anet routines are provided with their own license.

CONTENTS
========

This directory contains C source for applications that talk to the adsb_cape.
There are two different applications here.

bcc_tool
--------

bcc_tool is used to load the FPGA bitstream into the adsb_cape and to do
low-level communication with the running design via the SPI port.

tl;dr

* bcc_tool -v <bitstream file> will load the FPGA

* bcc_tool -r <register> will read a SPI port register.

* bcc_tool -w <register> <data> will write a SPI port register.

* bcc_tool -a <addr> [ anything else ] selects the board address.


adsb_tool
---------

adsb_tool monitors the running FPGA design and fetches decoded ADS-B frames
from it, forwarding them through UDP port 30002. adsb_tool borrows heavily
from code originating in the dump1090 project.

https://github.com/antirez/dump1090

tl;dr

* adsb_tool -r will log raw frame data to "raw_msg.txt"

* adsb_tool -v will dump verbose info to the console

* adsb_tool -a <addr> [ anything else ] selects the board address.

USING
=====

Installing must be done on a Beaglebone with a recent kernel that provides
i2c_dev and spi_dev at the proper bus addresses.

1) Edit Makefile to select Beaglebone White or Black

2) make

3) make adsb_tool

4) run bcc_tool to load FPGA bitstream

5) configure FPGA bitstream via read/write commands

6) run adsb_tool

7) connect to adsb_tool via UDP to display received data.

Eric Brombaugh 10/25/2014

