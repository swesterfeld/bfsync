#!/bin/bash

for i in $(seq 1 100)
do
  mkdir $i
  for j in $(seq 1 1000)
  do
    echo "$i/$j" > $i/$j
  done
done
