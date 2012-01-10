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
    full_name = os.path.join (object_dir, make_object_filename (hash))
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
  conn = repo.conn
  repo_path = repo.path
  c = conn.cursor()
  fail = False
  for dh in delta_history:
    version = dh[0]
    for row in c.execute ("""SELECT * FROM history WHERE version=?""", (version,)):
      fail = True
    c.execute ("""INSERT INTO history VALUES (?,?,?,?,?)""", dh)
  if fail:
    conn.rollback()
    return "fail"
  else:
    conn.commit()
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
  conn = repo.conn
  repo_path = repo.path
  c = conn.cursor()

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
