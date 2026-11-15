#!/bin/sh
HOST=`uname -n | cut -f 1 -d .`
export HOST
make -e -f Makefile.common bindist

