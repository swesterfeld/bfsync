# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

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

def remote_tags (repo):
  result_tags = []

  VERSION = 1
  while True:
    hentry = repo.bdb.load_history_entry (VERSION)
    VERSION += 1

    if not hentry.valid:
      break

    row = [ hentry.version ]
    tags = repo.bdb.list_tags (hentry.version)
    for tag in tags:
      tvlist = [ tag ]
      values = repo.bdb.load_tag (hentry.version, tag)
      for value in values:
        tvlist += [ value ]
      row.append (tvlist)

    result_tags += [ row ]

  return result_tags

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
  hi = bfsyncdb.INodeHashIterator (repo.bdb)
  while True:
    hash = hi.get_next()
    if hash == "":
      break           # done
    if not repo.validate_object (hash):
      objs.append (hash)
  del hi # free locks iterator may have held
  # end MERGE ME
  return objs
