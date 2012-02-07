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

import os
import sys
import cPickle

from TransferList import TransferList
from HashCache import hash_cache
from utils import *
from stat import *

def remote_ls (repo, hashes):
  file_list = []
  object_dir = os.path.join (repo.path, "objects")
  for hash in hashes:
    file_number = repo.bdb.load_hash2file (hash)
    if file_number != 0:
      full_name = repo.make_number_filename (file_number)
      try:
        st = os.stat (full_name)
      except:
        st = False   # file not there
      if st and S_ISREG (st.st_mode):
        real_hash = hash_cache.compute_hash (full_name)
        if (hash == real_hash):
          remote_file = RemoteFile()
          remote_file.hash = real_hash
          remote_file.size = st.st_size
          remote_file.number = file_number
          file_list += [ remote_file ]
  return file_list

def remote_send (repo, params):
  tl = TransferList()
  tl.receive_list (sys.stdin)
  tl.send_files (repo, sys.stdout, False, params)
  sys.stdout.flush()

def remote_receive (repo):
  tl = TransferList()
  tl.receive_list (sys.stdin)
  tl.receive_files (repo, sys.stdin, False)

def remote_update_history (repo, delta_history):
  repo_path = repo.path
  fail = False
  for dh in delta_history:
    version = dh[0]
    hentry = repo.bdb.load_history_entry (version)
    if hentry.valid:
      return "fail"

  repo.bdb.begin_transaction()
  for dh in delta_history:
    version = dh[0]
    repo.bdb.store_history_entry (dh[0], dh[1], dh[2], dh[3], dh[4])
  repo.bdb.commit_transaction()

  return "ok"

def remote_history (repo):
  hlist = []

  VERSION = 1
  while True:
    hentry = repo.bdb.load_history_entry (VERSION)
    VERSION += 1

    if not hentry.valid:
      break

    row = (hentry.version, hentry.hash, hentry.author, hentry.message, hentry.time)
    hlist += [ row ]

  return hlist

def remote_need_objects (repo, table):
  repo_path = repo.path

  if table == "history":
    need_hash = dict()
    objs = []

    VERSION = 1
    while True:
      hentry = repo.bdb.load_history_entry (VERSION)
      VERSION += 1

      if not hentry.valid:
        break

      if not need_hash.has_key (hentry.hash):
        if not repo.validate_object (hentry.hash):
          need_hash[hentry.hash] = True
          objs += [ hentry.hash ]

    return objs

  # MERGE ME WITH get
  objs = []
  c.execute ('''SELECT DISTINCT hash FROM %s''' % table)
  for row in c:
    hash = "%s" % row[0]
    if len (hash) == 40:
      dest_file = os.path.join (repo_path, "objects", make_object_filename (hash))
      if not validate_object (dest_file, hash):
        objs += [ hash ]
  # end MERGE ME
  return objs
