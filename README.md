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

The Library can be downloaded from the GitHub packages (both C++ and Java)

## Serial Terminal

An simple command line serial terminal.
Allows to connect to any local serial port and configure its baud, data bits, stop bits, parity and flow control using simple flag options.
Also allows two modes of operation for user input:

* line based, enter and edit content before sending with enter, if an LF or CR is appended to the text can also be configured
* raw input mode, each key typed is transmitted over serial instantly

## Virtual Serial Ports

The utilities contain an virtual serial port driver which allows the creation of virtual serial ports.
These ports can be used for an arbitrary application to expose an virtual port to an another application, which can connect to it like to any other serial port.
The port are created using the vcom.exe tool, which requires admin rights, but the port can be used by any application trough the virtualserial library.
The backing application of the virtual port receives all data and configurations which are applied to the virtual port.

NOTE: The virtual serial functionality is not yet supported on linux

## Serial Over Ethernet/IP

An low latancy real time serial over ethernet protocoll to allow an remote serial port to be linked to an local on over ethernet, while keeping the latency even for small single character messages as low as possible.

The system is designed for low latency and was originaly made to controll devices like 3D Printers and CNC mill over LAN or WLAN (latter one not suggested because of reliability)
It disables all TCP buffering to ensure minimal delay when sending serial data.
Both remote and local serial ports can be fully configured independently from the client.

In combination with the virtual port utility, it is also possible to create an virtual serial port and bind it to an remote port using SOE.
This allows configuration which are applied to the virtual port trough an application to be automatically applied to the remote port as well, no setup requierd.

It does support hardware flow control and software flow controll includings the neccessary IOCTL codes.
It does not however support special functions like EOF and BREAK characters.

**IMPORTAND: The protocoll does not implement any kind of encryption or security features, it was purely developed for usage in local networks.**

# Building Everything

The project utilizes an custom build system, but it does not require any additonals setup except an moddern Java 17+ JDK (an JRE will not work!)
It also requires compilers for the different platforms to be available, and symbolic links which link to the correct compilers:
- win-amd-64-g++ - The windows AMD64 compiler
- lin-amd-64-g++ - The linux AMD64 compiler
- lin-arm-64-g++ - The linux ARM64 compiler
- lin-arm-32-g++ - The linux ARM32 compiler
I don't know the exact C++ Standard they need to support, but any recently moddern compiler should work.

If only an specific platform has to be build, two options are available:
- uncommenting the code blocks in the init() section of all build files (build.mete in the top level sub-directories) which are labled with a platform which is not required
- calling the platform specific build tasks in each sub-project manualy (publishLocal*Platform* for libraries and build*Platform* for applications)
  The libraries can be downloaded frmo GitHub Packages, so it is not required to build them, unles the configured versions are SNAPSHOT versions, which generally don't get published and are only for developement.
  If they are build and published to the maven local repository, the local ones will be prefered.
To build everything for every platform, it would be enough to run buildAll in the BuildAll sub-directory.

All tasks are run in the sub-directory of the project using ./metaw *task name*

# Binaries

The most recent binaries for all platforms, which are considered "stable", are uploaded as "SerialUtilities.zip" in the root directory.
Since its a collection of tools, I can't think of any propper way of handling the versioning there ...
