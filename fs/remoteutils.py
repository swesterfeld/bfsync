import os
import sys
import cPickle

from TransferList import TransferList
from HashCache import hash_cache
from utils import *

def remote_ls (repo):
  file_list = []
  object_dir = os.path.join (repo.path, "objects")
  for dir, dirs, files in os.walk (object_dir):
    for f in files:
      full_name = os.path.join (dir, f)
      if os.path.isfile (full_name):
        name_hash = os.path.basename (dir) + f
        real_hash = hash_cache.compute_hash (full_name)
        if (name_hash == real_hash):
          remote_file = RemoteFile()
          remote_file.hash = real_hash
          remote_file.size = os.path.getsize (full_name)
          file_list += [ remote_file ]
  return file_list

def remote_send():
  tl = TransferList()
  tl.receive_list (sys.stdin)
  tl.send_files (sys.stdout, False)
  sys.stdout.flush()

def remote_receive():
  tl = TransferList()
  tl.receive_list (sys.stdin)
  tl.receive_files (sys.stdin, False)

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
  conn = repo.conn
  c = conn.cursor()
  c.execute ('''SELECT * FROM history WHERE hash != '' ORDER BY version''')
  hlist = []
  for row in c:
    hlist += [ row ]
  return hlist

def remote_need_objects (repo):
  conn = repo.conn
  repo_path = repo.path
  c = conn.cursor()

  # MERGE ME WITH get
  c.execute ('''SELECT DISTINCT hash FROM history''')
  objs = []
  for row in c:
    hash = "%s" % row[0]
    if len (hash) == 40:
      dest_file = os.path.join (repo_path, "objects", make_object_filename (hash))
      if not validate_object (dest_file, hash):
        objs += [ hash ]
  # end MERGE ME
  return objs


