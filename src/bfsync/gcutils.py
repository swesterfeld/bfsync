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
from StatusLine import status_line, OutputSubsampler
from utils import *

def gc (repo):
  DEBUG_MEM = False

  if DEBUG_MEM:
    print_mem_usage ("gc start")

  need_files = bfsyncdb.SortedArray()
  object_count = 0
  outss = OutputSubsampler()
  deleted_versions = repo.get_deleted_version_set()

  def update_status():
    status_line.update ("phase 1/4: scanning objects: %d" % object_count)

  ai = bfsyncdb.AllINodesIterator (repo.bdb)
  while True:
    id = ai.get_next()
    if not id.valid:
      break
    inodes = repo.bdb.load_all_inodes (id)
    for inode in inodes:
      if len (inode.hash) == 40:
        needed = False
        if inode.vmax == bfsyncdb.VERSION_INF:
          needed = True
        else:
          for version in range (inode.vmin, inode.vmax + 1):
            if version not in deleted_versions:
              needed = True
              break

        if needed:           # only keep files if versions have not been tagged deleted in history
          file_number = repo.bdb.load_hash2file (inode.hash)
          if file_number != 0:
            need_files.append (file_number)
      if inode.hash == "new":
        file_number = inode.new_file_number
        if file_number != 0:
          need_files.append (file_number)
      object_count += 1
      if outss.need_update():
        update_status()

  update_status()
  status_line.cleanup()

  if DEBUG_MEM:
    # need to do this before deleting ai
    print_mem_usage ("after need files loop")

  del ai  # free memory

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

  ## create file containing hashes that are no longer needed
  repo.bdb.begin_transaction()
  h2f_delete_filename = repo.make_temp_name()
  repo.bdb.commit_transaction()

  h2f_delete_file = open (h2f_delete_filename, "w")
  h2f_entries = 0

  def update_status_h2f():
    status_line.update ("phase 2/4: scanning hash db entries: %d" % h2f_entries)

  hi = bfsyncdb.Hash2FileIterator (repo.bdb)
  while True:
    h2f = hi.get_next()
    if not h2f.valid:
      break
    if not need_files.search (h2f.file_number):
      h2f_delete_file.write ("%s\n" % h2f.hash)
    if outss.need_update():
      update_status_h2f()
    h2f_entries += 1
  del hi

  update_status_h2f()
  status_line.cleanup()

  h2f_delete_file.close()

  if DEBUG_MEM:
    print_mem_usage ("after delete h2f file gen")

  ## delete unused hash entries in small chunks
  h2f_delete_file = open (h2f_delete_filename, "r")

  status_line.update ("phase 3/4: removing unused hash db entries...")

  OPS = 0
  repo.bdb.begin_transaction()

  for hash in h2f_delete_file:
    hash = hash.strip()
    repo.bdb.delete_hash2file (hash)
    OPS += 1
    if OPS >= 20000:
      repo.bdb.commit_transaction()
      repo.bdb.begin_transaction()
      OPS = 0

  repo.bdb.commit_transaction()

  h2f_delete_file.close()

  status_line.update ("phase 3/4: removing unused hash db entries: done")
  status_line.cleanup()

  if DEBUG_MEM:
    print_mem_usage ("after remove h2f loop")

  file_count = 0
  clean_count = 0

  def update_status_rm():
    status_line.update ("phase 4/4: removing unused files: %d files checked, %d files removed" % (file_count, clean_count))

  for root, dirs, files in os.walk (os.path.join (repo.path, "objects")):
    for f in files:
      file_count += 1
      file_number = int (os.path.basename (root) + f, 16)
      if not need_files.search (file_number):
        os.remove (os.path.join (root, f))
        clean_count += 1
      if outss.need_update():
        update_status_rm()

  update_status_rm()
  status_line.cleanup()

  if DEBUG_MEM:
    print_mem_usage ("after remove files loop")

  # ideal cache setting: at least 100 Mb per 1.000.000 files
  cache_size = int (repo.config.get ("cache-size")[0])
  status_line.update ("db cache size: %d Mb, %d files => about %.2f%% cache is currently in use" % (cache_size, file_count, (file_count * 100 / 1000000.0) / cache_size * 100))
  status_line.cleanup()
