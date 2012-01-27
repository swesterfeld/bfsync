#!/bin/bash

usage()
{
  echo "multi-add-test.sh <mount-point>"
  exit 1
}

[ -d "$1" ] || usage;
[ -d "$1/.bfsync" ] || usage;

MKNFILES=$PWD/mknfiles.sh
BFSYNC=$PWD/bfsync.py

cd $1

N_FILES=1

while [ $N_FILES -lt 1000 ]; do
  echo "*** ADD ${N_FILES}00000 ***"
  export BFSYNC_NO_HASH_CACHE=1
  mkdir add$N_FILES
  cd add$N_FILES
  /usr/bin/time -f "$N_FILES mknfiles-time %e" $MKNFILES $N_FILES 2>&1
  /usr/bin/time -f "$N_FILES commit-time  %e" $BFSYNC commit -m "test run $run" 2>&1
  cd ..
  N_FILES=$((N_FILES * 2))
done
