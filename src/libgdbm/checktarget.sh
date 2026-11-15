#!/bin/sh
if [ -r arch ]; then
  echo -n
else
  mkdir arch
fi

HOST=`uname -n | cut -f 1 -d .`
export HOST
if [ -r ../../config/config.$HOST ]; then
  ARCH=`cat ../../config/config.$HOST | grep "^ARCH" | cut -f 2 -d "=" | cut -f 2 -d " "`
  if [ -r arch/$ARCH ]; then
    echo "Using ARCH=$ARCH"
  else
    echo "Creating $ARCH"
    mkdir arch/$ARCH
  fi
  if [ -r arch/$ARCH/Makefile.in ]; then
    echo "Already configured"
  else
    cd arch/$ARCH
    for f in `ls -1 ../../gdbm-1.8.0/*`; do
      NAME=`echo $f | cut -f 4 -d /`
      ln -s $f $NAME
    done
    ./configure
  fi
else
  echo "Configuration for $HOST not found"
fi


