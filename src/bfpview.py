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

class DiffIterator:
  def __init__ (self, diff):
    self.diff = diff
    self.start = 0

  def next_field (self):
    end = self.diff.find ("\0", self.start)
    if end == -1:
      return None

    result = self.diff[self.start:end]
    self.start = end + 1
    return result

  def next (self):
    change_type = self.next_field()
    if change_type is None:
      return None

    fcount = 0

    if change_type == "l+" or change_type == "l!":
      fcount = 4
    elif change_type == "l-":
      fcount = 3
    elif change_type == "i+" or change_type == "i!":
      fcount = 16
    elif change_type == "i-":
      fcount = 2

    assert (fcount != 0)

    result = [ change_type ]
    for i in range (fcount - 1):
      result += [ self.next_field() ]
    return result

#print_mem_usage ("x")

di = DiffIterator (diff)

while True:
  change = di.next()
  if change is None:
    break
  print "|".join (change)
