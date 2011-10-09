#!/usr/bin/python

import subprocess
import sys

# mount_point
mount_point = "mnt"

# compile
if subprocess.call (["make"]):
  print "compilation failed"
  sys.exit (1)

# unmount if mounted
try:
  f = open (mount_point + "/.bfsync/info")
  f.close()
  subprocess.call (["fusermount", "-u", mount_point])
except:
  pass # not mounted

# remount

if subprocess.call (["./bfsyncfs", "test", mount_point]) != 0:
  print "can't start bfsyncfs"
  sys.exit (1)
else:
  print "bfsyncfs started."
