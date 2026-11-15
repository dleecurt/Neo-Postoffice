#!/bin/sh
for pkg in `(cd pkg && ls -1 *.spkg)`; do
  echo " "
  echo "---------------------------------------------------------------------------"
  echo -n "Building package: "
  cat pkg/`echo $pkg|sed -e "s/\.spkg/\.desc/"`
  echo "---------------------------------------------------------------------------"
  OPWD=`pwd`
  cd `cat pkg/$pkg`
  make $1
  cd $OPWD
done
