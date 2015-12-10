# SAMdisk


## Introduction

[SAMdisk](http://simonowen.com/samdisk/) is a portable disk image utility. It specialises in reading and writing most PC-compatible floppy media, including many copy-protected formats.


## Development

This code base is still very much a work in progress, and expected to be **incomplete** and **buggy** for a little while longer.

Despite being based on the SAMdisk 3 source code, the disk core has been completely rewritten for improved flexibility. This has resulted in a temporary loss of some features -- *most notably Windows floppy disk support* via [fdrawcmd.sys](http://simonowen.com/fdrawcmd/). Some and disk image formats are also missing, and some made be read-only.

Once the missing functionality has been restored it will be released officially as SAMdisk 4.0.


## System Requirements

The current code should build and run under Windows, Linux and Mac OS, and be portable to other systems. Building requires a C++ compiler with good C++11 support, such as Visual Studio 2015, g++ 4.9+, or Clang 3.6+.

The Windows version is usually built using Visual Studio 2015 (the free [Community edition](https://www.visualstudio.com/products/visual-studio-community-vs) works fine), and requires some 3rd-party libraries ([zlib](http://zlib.net/), [bzip2](http://www.bzip.org/), [CAPSimage](http://www.softpres.org/download), [FTDI](http://www.ftdichip.com/Drivers/D2XX.htm)) to be available at compile time. Those dependencies become optional at runtime.

Other platforms use the [CMake](https://cmake.org/) build system and require only *zlib* to be available. *gzip2* and *CAPSimage* libraries are used if they are available. Use `cmake .` to create the build scripts then `make` to build (or `make -j4` to for a parallel build using 4 cores).


## License

The SAMdisk source code is released under the [MIT license](https://tldrlegal.com/license/mit-license).


## Contact

Simon Owen  
[http://simonowen.com](http://simonowen.com)
