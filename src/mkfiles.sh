#!/bin/bash

if [ "x$1" != "x" ]; then
  p="$1:"
else
  p=""
fi

for i in $(seq 1 100)
do
  echo -ne "\rmkfiles: $p$i/100"
  mkdir $i
  for j in $(seq 1 1000)
  do
    echo "$p$i/$j" > $i/$j
  done
done
echo -e "\r                                \rmkfiles: done."
