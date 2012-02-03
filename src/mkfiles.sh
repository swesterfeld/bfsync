#!/bin/bash

for i in $(seq 1 100)
do
  echo -ne "\rmkfiles: $1:$i/100"
  mkdir $i
  for j in $(seq 1 1000)
  do
    echo "$1:$i/$j" > $i/$j
  done
done
echo -e "\r                                \rmkfiles: done."
