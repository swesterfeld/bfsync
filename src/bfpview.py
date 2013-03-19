#!/usr/bin/python

# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

import sys
import os
import subprocess
from bfsync.utils import *
from bfsync.xzutils import xzcat
from bfsync.diffutils import DiffIterator

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
  diff_file = None # FIXME
else:
  diff_file = sys.stdin

#print_mem_usage ("x")

di = DiffIterator (diff_file)

while True:
  change = di.next()
  if change is None:
    break
  print "|".join (change)
