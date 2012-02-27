#!/bin/bash

for n in $(seq 1 $1)
do
  echo "... n=$n"
  mkdir run$n
  cd run$n
  mkfiles.sh run$n
  cd ..
done
