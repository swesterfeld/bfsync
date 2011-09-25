#!/bin/bash
of=/dev/null
echo "+++ $of +++"
for i in $(seq 0 10); do
  size=$((8*1024**3)) #ensure this is big enough
  bs=$((1024*2**$i))
  printf "%7s=" $bs
  dd bs=$bs if=/dev/zero of=$of count=$(($size/$bs)) 2>&1 | sed -n 's/.* \([0-9.,]* [GM]B\/s\)/\1/p'
done

of=mnt/subdir/x
echo "+++ $of +++"
for i in $(seq 0 10); do
  size=$((2*1024**3)) #ensure this is big enough
  bs=$((1024*2**$i))
  printf "%7s=" $bs
  dd bs=$bs if=/dev/zero of=$of count=$(($size/$bs)) 2>&1 | sed -n 's/.* \([0-9.,]* [GM]B\/s\)/\1/p'
done

of=x
echo "+++ $of +++"
for i in $(seq 0 10); do
  size=$((2*1024**3)) #ensure this is big enough
  bs=$((1024*2**$i))
  printf "%7s=" $bs
  dd bs=$bs if=/dev/zero of=$of count=$(($size/$bs)) 2>&1 | sed -n 's/.* \([0-9.,]* [GM]B\/s\)/\1/p'
done


