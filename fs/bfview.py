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

changes = parse_diff (diff)

for c in changes:
  print "|".join (c)
