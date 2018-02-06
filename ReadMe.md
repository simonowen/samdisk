# SAMdisk


## Introduction

[SAMdisk](http://simonowen.com/samdisk/) is a portable disk image utility. It specialises in reading and writing most PC-compatible floppy media, including many copy-protected formats.


## Development

This code base is still very much a work in progress, and expected to be **incomplete** and **buggy** for a little while longer.

Despite being based on the SAMdisk 3 source code, the disk core has been completely rewritten for improved flexibility. This has resulted in a temporary loss of some features -- *most notably Windows floppy disk writing* via [fdrawcmd.sys](http://simonowen.com/fdrawcmd/). Some and disk image formats are also missing, and some made be read-only.

Once the missing functionality has been restored it will be released officially as SAMdisk 4.0.


## System Requirements

The current code builds under Windows, Linux and macOS, and should be portable to other systems. Building requires a C++ compiler with C++14 support, such as Visual Studio 2017, g++ 4.9+, or Clang 3.6+.

All platforms can use the [CMake](https://cmake.org/) build system. A number of optional libraries are used if they are available at configuration time. Use `cmake -DCMAKE_BUILD_TARGET=Release .` to create the build scripts then `make` to build.

Windows developers may wish to install [vcpkg](https://github.com/Microsoft/vcpkg) and add the zlib, bzip2 and liblzma packages. They will be found by CMake and automatically linked by the VS2017 project. `vcpkg` can also be used to create and build a CMake project. See the official site for details.

Install the [CAPSimage](http://www.softpres.org/download) library for IPF/CTRaw/KFStream/Draft support, and the [FTDI](http://www.ftdichip.com/Drivers/D2XX.htm)) library for SuperCard Pro device support. libusb or WinUSB will be used for KryoFlux device support, depending on the platform.

The provided Visual Studio 2017 project file expects all libraries mentioned above to be available at compile time. To disable individual support,  comment out the appropriate `HAVE_*` entries in `SAMdisk.h`.

## License

The SAMdisk source code is released under the [MIT license](https://tldrlegal.com/license/mit-license).


## Contact

Simon Owen  
[http://simonowen.com](http://simonowen.com)
