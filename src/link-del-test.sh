#!/bin/bash

usage()
{
  echo "link-del-test.sh <dir>"
  exit 1
}

[ -d "$1" ] || usage;

mkdir -p $1/mnt-a
mkdir -p $1/mnt-b

bfsync init $1/master

bfsync clone $1/master $1/repo-b
bfsync clone $1/master $1/repo-a

bfsyncfs $1/repo-a $1/mnt-a
bfsyncfs $1/repo-b $1/mnt-b

for i in $(seq 10)
do
  for n in $(seq 1 ${i}000)
  do
    touch $1/mnt-a/$n
  done
  cd $1/mnt-a
  bfsync commit -m "add links"
  rm $1/mnt-a/[0-9]*
  bfsync commit -m "del links"
  bfsync push
  cd $1/mnt-b
  /usr/bin/time -f "$i link-del-time %e" bfsync pull 2>&1
done

cd /

fusermount -u $1/mnt-a
fusermount -u $1/mnt-b

rm -rf $1/mnt-a $1/mnt-b $1/master $1/repo-a $1/repo-b
