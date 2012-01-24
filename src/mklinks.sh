#!/bin/bash

for i in $(seq 1 100)
do
  echo -ne "\rmklinks: $i/100"
  mkdir $i
  ln -s $(seq 1 1000) $i
done
echo -e "\r                                \rmklinks: done."
