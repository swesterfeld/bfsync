#!/usr/bin/python

# bfsync: Big File synchronization tool

# Copyright (C) 2011 Stefan Westerfeld
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

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
