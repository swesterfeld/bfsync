from ServerConn import ServerConn
from StatusLine import status_line
from diffutils import diff
from utils import *
from HashCache import hash_cache

import os

# in case the repo is not mounted, we don't need a ServerConn
#
# this fake server conn is only to be used during "clone"
class NoServerConn:
  def get_lock (self):
    pass

  def save_changes (self):
    pass

  def clear_cache (self):
    pass

  def close (self):
    pass

# the manpage says that levels > "-6" only improve compression ratio if the file
# is bigger than 8M, 16M, 32M.
def xz_level_for_file (filename):
  size = os.path.getsize (filename)
  if (size <= 8 * 1024 * 1024):
    return "-6"
  if (size <= 16 * 1024 * 1024):
    return "-7"
  if (size <= 32 * 1024 * 1024):
    return "-8"
  return "-9"

def commit (repo, expected_diff = None, expected_diff_hash = None, server = True):
  conn = repo.conn
  repo_path = repo.path

  # lock repo to allow modifications
  if server:
    server_conn = ServerConn (repo_path)
  else:
    server_conn = NoServerConn()
  server_conn.get_lock()

  hash_list = []
  file_list = []

  c = conn.cursor()

  c.execute ('''SELECT id FROM inodes WHERE hash = "new"''')
  for row in c:
    id = row[0]
    filename = os.path.join (repo_path, "new", id[0:2], id[2:])
    hash_list += [ filename ]
    file_list += [ (filename, id) ]

  #hash_cache.hash_all (hash_list)
  #status_line.cleanup()

  # add new files via BFSync::Server
  add_new_list = []
  for (filename, id) in file_list:
    hash = hash_cache.compute_hash (filename)
    add_new_list += [id, hash]

  status_line.set_op ("ADD-NEW")
  files_added = 0
  files_total = len (add_new_list) / 2
  while len (add_new_list) > 0:
    items = min (len (add_new_list), 200)
    server_conn.add_new (add_new_list[0:items])
    add_new_list = add_new_list[items:]
    files_added += items / 2
    status_line.update ("file %d/%d" % (files_added, files_total))
  status_line.cleanup()

  server_conn.save_changes()

  VERSION = 1
  c.execute ('''SELECT version FROM history''')
  for row in c:
    VERSION = max (row[0], VERSION)

  # compute commit diff
  status_line.set_op ("COMMIT-DIFF")
  status_line.update ("computing changes between version %d and %d... " % (VERSION - 1, VERSION))
  diff_filename = os.path.join (repo_path, "tmp-commit-diff")
  diff_file = open (diff_filename, "w")
  diff (c, VERSION - 1, VERSION, diff_file)
  diff_file.close()
  if expected_diff:
    diff_file = open (diff_filename, "r")
    new_diff = diff_file.read()
    diff_file.close()
    if new_diff != expected_diff:
      raise Exception ("commit called with expected diff argument, but diffs didn't match")
    hash = expected_diff_hash
    object_name = os.path.join (repo_path, "objects", make_object_filename (hash))
    if not validate_object (object_name, hash):
      raise Exception ("commit called with expected_diff argument, but object with hash %s doesn't validate" % hash)
  else:
    os.system ("xz %s %s" % (xz_level_for_file (diff_filename), diff_filename))
    hash = move_file_to_objects (repo, diff_filename + ".xz")

  status_line.update ("done.")
  status_line.cleanup()

  c.execute ('''UPDATE inodes SET vmax=? WHERE vmax = ?''', (VERSION + 1, VERSION))
  c.execute ('''UPDATE links SET vmax=? WHERE vmax = ?''', (VERSION + 1, VERSION))
  c.execute ('''UPDATE history SET message="commit message", author="author", hash=? WHERE version=?''', (hash, VERSION, ))
  c.execute ('''INSERT INTO history VALUES (?,?,?,?,?)''', (VERSION + 1, "", "", "", 0))
  conn.commit()
  c.close()

  # we modified the db, so the fs needs to reload everything
  # in-memory cached items will not be correct
  server_conn.clear_cache()

  # this will release the lock
  server_conn.close()

def revert (repo, VERSION):
  conn = repo.conn
  repo_path = repo.path

  c = conn.cursor()

  # lock repo to allow modifications
  server_conn = ServerConn (repo_path)
  server_conn.get_lock()

  # VERSION == -1 <=> revert to current version (discard new entries from FuSE fs)
  if VERSION == -1:
    VERSION = 1
    c.execute ('''SELECT version FROM history''')
    for row in c:
      VERSION = max (row[0], VERSION)
    VERSION -= 1

  print "reverting to version %d..." % VERSION
  c.execute ('''SELECT vmin, vmax, id FROM inodes WHERE vmax >= ?''', (VERSION, ))
  del_inode_list = []
  for row in c:
    vmin = row[0]
    vmax = row[1]
    id = row[2]
    if (vmin > VERSION):
      del_inode_list += [ (vmin, id) ]
  for vmin, id in del_inode_list:
    c.execute ('''DELETE FROM inodes WHERE vmin=? AND id=?''', (vmin, id))
  c.execute ('''SELECT vmin, vmax, dir_id, inode_id FROM links WHERE vmax >= ?''', (VERSION, ))
  del_link_list = []
  for row in c:
    vmin = row[0]
    vmax = row[1]
    dir_id = row[2]
    inode_id = row[3]
    if (vmin > VERSION):
      del_link_list += [ (vmin, dir_id, inode_id) ]
  for vmin, dir_id, inode_id in del_link_list:
    c.execute ('''DELETE FROM links WHERE vmin=? AND dir_id=? AND inode_id=?''', (vmin, dir_id, inode_id))

  c.execute ('''UPDATE inodes SET vmax=? WHERE vmax >= ?''', (VERSION + 1, VERSION))
  c.execute ('''UPDATE links SET vmax=? WHERE vmax >= ?''', (VERSION + 1, VERSION))
  c.execute ('''DELETE FROM history WHERE version > ?''', (VERSION, ))
  c.execute ('''INSERT INTO history VALUES (?,?,?,?,?)''', (VERSION + 1, "", "", "", 0))

  conn.commit()
  c.close()
  # we modified the db, so the fs needs to reload everything
  # in-memory cached items will not be correct
  server_conn.clear_cache()
