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

The system is designed for low latency and was originaly made to controll devices like 3D Printers and CNC mill over LAN or WLAN (latter one not suggested because of reliability)
It disables all TCP buffering to ensure minimal delay when sending serial data.
Both remote and local serial ports can be fully configured independently from the client.

It does support hardware flow control and software flow controll (XON/XOFF) is implemented by simply ignoring these characters and passing them trough.

**IMPORTAND: The protocoll does not implement any kind of encryption or security features, it was purely developed for usage in local networks.**

## Note on Com0Com

The repository contains some files from https://sourceforge.net/u/vfrolov/profile located in the folder "Com0Com"
These are not part of this project and only for testing my own code.
