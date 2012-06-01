#!/bin/bash

usage()
{
  echo "multi-run-test.sh <mount-point>"
  exit 1
}

die()
{
  echo "$1"
  exit 1
}

[ -d "$1" ] || usage;
[ -d "$1/.bfsync" ] || usage;

MKFILES=$PWD/mkfiles.sh
BFSYNC=$PWD/bfsync.py

for run in $(seq 1 500)
do
  echo "*** RUN $run ***"
  export BFSYNC_NO_HASH_CACHE=1
  mkdir $1/run$run || die "mkdir failed"
  cd $1/run$run || die "chdir failed"
  /usr/bin/time -f "$run add-time %e" bash -c "$MKFILES run$run; $BFSYNC commit -m 'test run $run'" 2>&1 || die "time failed"
done

