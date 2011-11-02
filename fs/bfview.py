#!/usr/bin/python

import sys
import os
import subprocess
from utils import *

if len (sys.argv) == 2:
  obj_name = os.path.join (find_repo_dir(), "objects", make_object_filename (sys.argv[1]))
  diff = subprocess.Popen(["xzcat", obj_name], stdout=subprocess.PIPE).communicate()[0]
else:
  diff = sys.stdin.read()

sdiff = diff.split ("\0")

start = 0

while len (sdiff) - start > 1:
  fcount = 0
  if sdiff[start] == "l+" or sdiff[start] == "l!":
    fcount = 4
  elif sdiff[start] == "l-":
    fcount = 3
  elif sdiff[start] == "i+" or sdiff[start] == "i!":
    fcount = 16
  elif sdiff[start] == "i-":
    fcount = 2

  if fcount == 0:
    print sdiff[start:]
  assert (fcount != 0)
  print "|".join (sdiff[start:start + fcount])
  start += fcount
