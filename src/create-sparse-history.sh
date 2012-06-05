#!/bin/bash

usage()
{
  echo "create-sparse-history.sh <mount-point>"
  exit 1
}

die()
{
  echo "$1"
  exit 1
}

[ -d "$1" ] || usage;
[ -d "$1/.bfsync" ] || usage;

BFSYNC=$PWD/bfsync.py

for run in $(seq 1 20)
do
  rm -f $1/run*
  echo "run$run" > $1/run$run || die "file creation failed"
  cd $1 || die "chdir failed"
  $BFSYNC commit -m "test run $run" || die "commit failed"
  $BFSYNC debug-change-time $((run + 1)) $((run * 24)) || die "change time failed"
done

$BFSYNC push
