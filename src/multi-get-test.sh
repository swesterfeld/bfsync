#!/bin/bash

# before running this script
#
# create master
# create slave1 (clone from master)
# create slave2 (clone from master)
# mount slave1 to mnt1
# mount slave2 to mnt2

locks()
{
  db5.3_stat -e | grep 'lock objects at any one time' | awk '{ print $1 }'
}

for i in $(seq 1 1000)
do
  (
    cd mnt1
    mkdir it$i
    cd it$i
    mkfiles.sh it$i
    bfsync.py commit -m it$i
    bfsync.py push
  )
  (
    cd slave1/bdb
    echo slave1locks $i $(locks)
  )
  (
    cd mnt2
    bfsync.py pull
    bfsync.py get ../slave1
  )
  (
    cd slave2/bdb
    echo slave2locks $i $(locks)
  )
  wc -l ~/.bfsync_cache
  rm ~/.bfsync_cache
done
