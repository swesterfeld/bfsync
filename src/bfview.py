#!/usr/bin/python

# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

import sys
import os
import subprocess
from bfsync.utils import *
from bfsync.xzutils import xzcat

if len (sys.argv) == 2:
  repo = cd_repo_connect_db()
  if len (sys.argv[1]) < 10:
    version = int (sys.argv[1])
    hash = repo.bdb.load_history_entry (version).hash
  else:
    hash = sys.argv[1]
  file_number = repo.bdb.load_hash2file (hash)
  if file_number != 0:
    full_name = repo.make_number_filename (file_number)
    diff = xzcat (full_name)
else:
  diff = sys.stdin.read()

changes = parse_diff (diff)

for c in changes:
  print "|".join (c)
