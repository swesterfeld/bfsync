#!/usr/bin/python

# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

import time

i = 0
f = open ("mnt/slowwrite", "w")
while True:
  f.write (str (i) + "\n");
  f.flush()
  time.sleep (1)
  i += 1
