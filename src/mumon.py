#!/usr/bin/python

import sys
import resource
import time

def get_mem_usage (pid):
  f = open ("/proc/%d/smaps" % pid)
  mem_smaps = 0
  for line in f:
    fields = line.split ()
    if fields[0][0].isupper():
      if fields[0] == "Rss:" and inode == 0:
        mem_smaps += int (fields[1])
    else:
      inode = int (fields[4])
  f.close()
  return mem_smaps

pid = int (sys.argv[1])
t = 0
while True:
  try:
    mem = get_mem_usage (pid)
    print t, mem
    sys.stdout.flush()
  except:
    sys.exit (0)

  time.sleep (0.5)
  t += 0.5
