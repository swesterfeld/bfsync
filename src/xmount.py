#!/usr/bin/python

import subprocess
import sys

# mount_point
mount_point = "mnt"

# unmount if mounted
try:
  f = open (mount_point + "/.bfsync/info")
  f.close()
  subprocess.call (["fusermount", "-u", mount_point])
  print "bfsyncfs stopped."
except:
  pass # not mounted

# remount

if subprocess.call (["../fs/bfsyncfs", "test/repo", mount_point]) != 0:
  print "can't start bfsyncfs"
  sys.exit (1)
else:
  print "bfsyncfs started."
