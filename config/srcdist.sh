#!/bin/sh
rm -f dist/postoffice-complete-`date +%Y%m%d`.tar
tar cf dist/postoffice-complete-`date +%Y%m%d`.tar Makefile config/*.sh include/defs.h config/configure skel/vroot/init config/update
for pkg in `(cd pkg && ls -1 *.spkg | cut -f 1 -d .)`; do
  pkgname=`echo $pkg | cut -f 2 -d _`
  echo " "
  echo "---------------------------------------------------------------------------";
  echo -n "Building source distribution: "
  cat pkg/$pkg.desc
  echo "---------------------------------------------------------------------------";
  tar cvf dist/$pkgname-`date +%Y%m%d`-src.tar `pkg/$pkg.dist`
  tar rf dist/postoffice-complete-`date +%Y%m%d`.tar `pkg/$pkg.dist`
  rm -f dist/$pkgname-`date +%Y%m%d`-src.tar.gz
  gzip dist/$pkgname-`date +%Y%m%d`-src.tar
done

echo " "
echo "---------------------------------------------------------------------------";
echo "Building core distribution"
echo "---------------------------------------------------------------------------";

tar cvf dist/postoffice-build-`date +%Y%m%d`.tar Makefile config/*.sh include/defs.h config/configure skel/vroot/init config/update
rm -f dist/postoffice-build-`date +%Y%m%d`.tar.gz
gzip dist/postoffice-build-`date +%Y%m%d`.tar

echo " "
echo "---------------------------------------------------------------------------";
echo "Building pkgbuild distribution"
echo "---------------------------------------------------------------------------";

rm -f dist/postoffice-complete-`date +%Y%m%d`.tar.gz
gzip dist/postoffice-complete-`date +%Y%m%d`.tar
echo " "
