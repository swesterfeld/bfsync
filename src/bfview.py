#!/usr/bin/python

import sys
import os
import subprocess
from bfsync.utils import *
from bfsync.xzutils import xzcat

if len (sys.argv) == 2:
  obj_name = os.path.join (find_repo_dir(), "objects", make_object_filename (sys.argv[1]))
  diff = xzcat (obj_name)
else:
  diff = sys.stdin.read()

changes = parse_diff (diff)

for c in changes:
  print "|".join (c)
