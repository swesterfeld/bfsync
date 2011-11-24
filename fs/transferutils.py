from RemoteRepo import RemoteRepo
from TransferList import TransferList, TransferFile
import os
from utils import *

def get_remote_objects (remote_repo, transfer_objs):
  # make a list of hashes that we need
  need_hash = dict()
  for thash in transfer_objs:
    dest_file = os.path.join ("objects", make_object_filename (thash))
    if not validate_object (dest_file, thash):
      need_hash[thash] = True

  # check for objects in remote repo
  remote_list = remote_repo.ls()
  tlist = TransferList()
  for rfile in remote_list:
    if need_hash.has_key (rfile.hash):
      src_file = os.path.join (remote_repo.path, "objects", make_object_filename (rfile.hash))
      dest_file = os.path.join ("objects", make_object_filename (rfile.hash))
      tlist.add (TransferFile (src_file, dest_file, rfile.size, 0400))

  # do the actual copying
  remote_repo.get_objects (tlist)

def get (repo, urls):
  conn = repo.conn
  repo_path = repo.path

  if len (urls) == 0:
    default_get = repo.config.get ("default/get")
    if len (default_get) == 0:
      raise Exception ("get: no repository specified and default/get config value empty")
    url = default_get[0]
  else:
    url = urls[0]

  remote_repo = RemoteRepo (url)

  c = conn.cursor()

  # create list of required objects
  objs = []
  c.execute ('''SELECT DISTINCT hash FROM inodes''')
  for row in c:
    s = "%s" % row[0]
    if len (s) == 40:
      objs += [ s ]

  get_remote_objects (remote_repo, objs)

def push (repo, urls):
  conn = repo.conn
  repo_path = repo.path

  if len (urls) == 0:
    default_push = repo.config.get ("default/push")
    if len (default_push) == 0:
      raise Exception ("push: no repository specified and default/push config value empty")
    url = default_push[0]
  else:
    url = urls[0]

  remote_repo = RemoteRepo (url)
  remote_history = remote_repo.get_history()

  c = conn.cursor()
  c.execute ('''SELECT * FROM history WHERE hash != '' ORDER BY version''')

  local_history = []
  for row in c:
    local_history += [ row ]

  common_version = 0
  for v in range (min (len (local_history), len (remote_history))):
    lh = local_history[v]
    rh = remote_history[v]
    # check version
    assert (lh[0] == v + 1)
    assert (rh[0] == v + 1)
    if lh[1] == rh[1]:
      common_version = v + 1
    else:
      break
  if common_version != len (remote_history):
    raise Exception ("push failed, remote history contains commits not in local history (pull to fix this)")

  delta_history = []
  for v in range (len (local_history)):
    if v + 1 > common_version:
      delta_history += [ local_history[v] ]

  print remote_repo.update_history (delta_history)

  need_objs = remote_repo.need_objects()

  tl = TransferList()
  for hash in need_objs:
    src_file = os.path.join ("objects", make_object_filename (hash))
    if validate_object (src_file, hash):
      tl.add (TransferFile (src_file, os.path.join (remote_repo.path, src_file), os.path.getsize (src_file), 0400))

  remote_repo.put_objects (tl)
