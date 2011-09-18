#!/usr/bin/python

import time

i = 0
f = open ("mnt/slowwrite", "w")
while True:
  f.write (str (i) + "\n");
  f.flush()
  time.sleep (1)
  i += 1
