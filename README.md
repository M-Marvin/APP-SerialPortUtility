# Serial Port Utilities

This project started as an simple serial port library for C++ and Java, but now is more like an collection of serial port utility libraries and tools.
The main point being an real time LAN serial over ethernet protocoll, designed for controlling serial 3D Printers and other CNC equipment over LAN.

Currently supported architectures/operating systems (for all libs and tools):

* AMD 64 / Windows
* AMD 64 / Linux
* ARM 64 / Linux (compiled for Raspberry Pi, but shoud run on other ARM devices as well)
* ARM 32 / Linux (compiled for Raspberry Pi, but shoud run on other ARM devices as well)

## Lib Serial Port Access

Simple and easy to use JNI capable serial port access lib.

* Java and C++ synthax almost identical
* Unified class for serial port configuration on both linux and windows (baud, data bits, stop bits, parity, flow control)
* Configurable timeouts (RX and TX)
* Consecutive read function to reduce complexity in some codes

The Library can be downloaded from the GitHub packages (both C++ and Java)

## Serial Terminal

An simple command line serial terminal.
Allows to connect to any local serial port and configure its baud, data bits, stop bits, parity and flow control using simple flag options.
Also allows two modes of operation for user input:

* line based, enter and edit content before sending with enter, if an LF or CR is appended to the text can also be configured
* raw input mode, only available on windows, each key typed is transmitted over serial instantly

## Serial Over Ethernet/IP

An low latancy real time serial over ethernet protocoll to allow an remote serial port to be linked to an local on over ethernet, while keeping the latency even for small single character messages as low as possible.

The protocoll uses UDP to transmit the data and implements its own connection sheme ontop of it. (so it is an **not**-connection less protocoll even tough it uses the connection less UDP protocoll)
The protocoll gurantees that

* no packages are lost
* the order of the packages is keept
* that no data loss occurs if the remote port is unable to transmit the data (because of flow control or other reasons)
  Altough the last point is only true for the software, since the buffer is limited, data can still be lost on hardware if the local serial port is unable to process received data because of an tranmission halt.

The protocoll does not support to transmit the flow controll status between the ports (AKA, virtually connecting the CTS line of the remote port to the DTS line on the local port), but in theory, software (XON/XOFF) flow controll can be achived trough the ethernet by simply disabling hardware flow controll on both ends and letting the two devices talk to each other directly, allowing the XON/XOFF characters to be interpreted by them.

The protocoll is entirely peer to peer based, if the software is started on an device, the device automatically acts as both client and server, and can establish connections to an arbitrary number of other devices and ports, as well as receive an arbitrary number of connection requests from other devices.
Note that the number of total connections is obviously limited by the number of serial ports available on the device.

The protocoll was developed and tested with some 3D-Printing and CNC-Machining equipment.

**IMPORTAND: The protocoll does not implement any kind of encryption or security features, it was purely developed for usage in local, possible even air-gapped networks.**

