#!/bin/bash

usage()
{
  echo "multi-run-test.sh <mount-point>"
  exit 1
}

[ -d "$1" ] || usage;
[ -d "$1/.bfsync" ] || usage;

MKFILES=$PWD/mkfiles.sh
BFSYNC=$PWD/bfsync.py

cd $1

for run in $(seq 1 500)
do
  echo "*** RUN $run ***"
  export BFSYNC_NO_HASH_CACHE=1
  mkdir run$run
  cd run$run
  /usr/bin/time -f "$run mkfiles-time %e" $MKFILES 2>&1
  /usr/bin/time -f "$run commit-time  %e" $BFSYNC commit -m "test run $run" 2>&1
  $BFSYNC debug-get-prof
  $BFSYNC debug-reset-prof
  cd ..
done

