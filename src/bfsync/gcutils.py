# bfsync: Big File synchronization tool

# Copyright (C) 2012 Stefan Westerfeld
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

import bfsyncdb
import os
from StatusLine import status_line
from utils import *

def gc (repo):
  DEBUG_MEM = True

  if DEBUG_MEM:
    print_mem_usage ("gc start")

  need_files = bfsyncdb.SortedArray()

  ai = bfsyncdb.AllINodesIterator (repo.bdb)
  while True:
    id = ai.get_next()
    if not id.valid:
      break
    inodes = repo.bdb.load_all_inodes (id)
    for inode in inodes:
      if len (inode.hash) == 40:
        file_number = repo.bdb.load_hash2file (inode.hash)
        if file_number != 0:
          need_files.append (file_number)
      if inode.hash == "new":
        file_number = inode.new_file_number
        if file_number != 0:
          need_files.append (file_number)
  del ai

  if DEBUG_MEM:
    print_mem_usage ("after need files loop")

  version = 1
  while True:
    hentry = repo.bdb.load_history_entry (version)
    if not hentry.valid:
      break

    file_number = repo.bdb.load_hash2file (hentry.hash)
    if file_number != 0:
      need_files.append (file_number)

    version += 1

  need_files.sort_unique()

  if DEBUG_MEM:
    print_mem_usage ("after history loop")

  file_count = 0
  clean_count = 0
  for root, dirs, files in os.walk (os.path.join (repo.path, "objects")):
    for f in files:
      file_count += 1
      file_number = int (os.path.basename (root) + f, 16)
      if not need_files.search (file_number):
        os.remove (os.path.join (root, f))
        clean_count += 1
      status_line.update ("removed %d/%d files" % (clean_count, file_count))

  if DEBUG_MEM:
    print_mem_usage ("after remove files loop")

  repo.bdb.begin_transaction()
  hi = bfsyncdb.Hash2FileIterator (repo.bdb)
  while True:
    h2f = hi.get_next()
    if not h2f.valid:
      break
    if not need_files.search (h2f.file_number):
      repo.bdb.delete_hash2file (h2f.hash)
  del hi
  repo.bdb.commit_transaction()

  if DEBUG_MEM:
    print_mem_usage ("after remove h2f loop")
