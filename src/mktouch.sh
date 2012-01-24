#!/bin/bash

for i in $(seq 1 100)
do
  echo -ne "\rmktouch: $i/100"
  mkdir $i
  cd $i
  touch $(seq 1 1000)
  cd ..
done
echo -e "\r                                \rmktouch: done."
