#!/bin/sh
HOST=`uname -n | cut -f 1 -d .`
export HOST
case "$MAKE" in
  make)
     ;;
  gmake)
     ;;
  *)
     if [ -f /usr/local/bin/gmake ]; then
       MAKE="gmake"
     elif [ -f /usr/freeware/bin/gmake ]; then
       MAKE="gmake"
     elif [ -f /usr/bin/make ]; then
       MAKE="make"
     elif [ -f /bin/make ]; then
       MAKE="make"
     elif [ -f /opt/bin/gmake ]; then
       MAKE="gmake"
     fi
     ;;
esac
export MAKE
$MAKE -e -f Makefile.common
