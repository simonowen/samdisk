#!/bin/sh

cd `dirname "$0"`

[ -x Makefile ] && make clean

rm -rf CMakeFiles CMakeCache.txt Makefile cmake_install.cmake config.h
