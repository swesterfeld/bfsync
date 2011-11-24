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
