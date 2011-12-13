from ServerConn import ServerConn
from StatusLine import status_line
from diffutils import diff
from utils import *
from xzutils import xz
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

def launch_editor (filename):
  editor = os.getenv ("VISUAL")
  if editor is None:
    editor = os.getenv ("EDITOR")
  if editor is None:
    editor = "vi"
  os.system ("%s %s" % (editor, filename))

def commit_msg_ok (filename):
  file = open (filename, "r")
  result = False
  for line in file:
    line = line.strip()
    if len (line):
      if line[0] == "#":
        pass
      else:
        result = True
  file.close()
  return result

def get_inode_type (c, inode, version):
  c.execute ('''SELECT type FROM inodes WHERE id = ? AND ? >= vmin AND ? <= vmax''', (inode, version, version))
  for row in c:
    return row[0]
  return "*unknown*"

def init_commit_msg (repo, filename):
  conn = repo.conn
  c = conn.cursor()

  VERSION = 1
  c.execute ('''SELECT version FROM history''')
  for row in c:
    VERSION = max (row[0], VERSION)

  msg_file = open (filename, "w")
  msg_file.write ("\n# commit version %d\n#\n" % VERSION)

  touched_inodes = set()
  c.execute ('''SELECT * FROM inodes WHERE vmin = ?''', (VERSION,))
  for row in c:
    touched_inodes.add (row[2])

  old_inodes = set()
  c.execute ('''SELECT * FROM inodes WHERE ? >= vmin AND ? <= vmax''', (VERSION - 1, VERSION - 1,))
  for row in c:
    old_inodes.add (row[2])

  new_inodes = set()
  c.execute ('''SELECT * FROM inodes WHERE ? >= vmin AND ? <= vmax''', (VERSION, VERSION,))
  for row in c:
    new_inodes.add (row[2])

  n_changed = 0
  n_added = 0
  n_deleted = 0

  change_list = []
  for inode in touched_inodes:
    inode_name = printable_name (c, inode, VERSION)
    inode_type = get_inode_type (c, inode, VERSION)
    if inode in old_inodes:
      change_list.append (("!", inode_type, inode_name))
      n_changed += 1
    else:
      change_list.append (("+", inode_type, inode_name))
      n_added += 1

  for inode in old_inodes:
    if not inode in new_inodes:
      inode_name = printable_name (c, inode, VERSION - 1)
      inode_type = get_inode_type (c, inode, VERSION - 1)
      change_list.append (("-", inode_type, inode_name))
      n_deleted += 1

  msg_file.write ("# %d objects added.\n" % n_added)
  msg_file.write ("# %d objects changed.\n" % n_changed)
  msg_file.write ("# %d objects deleted.\n" % n_deleted)
  msg_file.write ("#\n")

  change_list.sort (key = lambda x: x[2])
  for change in change_list:
    msg_file.write ("# %s %-8s %s\n" % change)
  msg_file.close()

def commit (repo, expected_diff = None, expected_diff_hash = None, server = True, verbose = True):
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

  if verbose:
    commit_msg_filename = repo.make_temp_name()
    init_commit_msg (repo, commit_msg_filename)
    launch_editor (commit_msg_filename)
    if not commit_msg_ok (commit_msg_filename):
      raise BFSyncError ("commit: empty commit message - aborting commit")

  #hash_cache.hash_all (hash_list)
  #status_line.cleanup()

  # add new files via BFSync::Server
  add_new_list = []
  for (filename, id) in file_list:
    hash = hash_cache.compute_hash (filename)
    add_new_list += [id, hash]

  files_added = 0
  files_total = len (add_new_list) / 2
  while len (add_new_list) > 0:
    items = min (len (add_new_list), 200)
    server_conn.add_new (add_new_list[0:items])
    add_new_list = add_new_list[items:]
    files_added += items / 2
    if verbose:
      status_line.update ("adding file %d/%d" % (files_added, files_total))

  if verbose:
    status_line.cleanup()

  server_conn.save_changes()

  VERSION = 1
  c.execute ('''SELECT version FROM history''')
  for row in c:
    VERSION = max (row[0], VERSION)

  # compute commit diff
  if verbose:
    status_line.update ("computing changes")

  diff_filename = repo.make_temp_name()
  diff_file = open (diff_filename, "w")
  diff (c, VERSION - 1, VERSION, diff_file)
  diff_file.close()

  commit_size_ok = True
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
    if os.path.getsize (diff_filename) != 0:
      xz (diff_filename)
      hash = move_file_to_objects (repo, diff_filename + ".xz")
    else:
      commit_size_ok = False

  if verbose:
    status_line.update ("computing changes: done")
    status_line.cleanup()

  if commit_size_ok:
    c.execute ('''UPDATE inodes SET vmax=? WHERE vmax = ?''', (VERSION + 1, VERSION))
    c.execute ('''UPDATE links SET vmax=? WHERE vmax = ?''', (VERSION + 1, VERSION))
    c.execute ('''UPDATE history SET message="commit message", author="author", hash=? WHERE version=?''', (hash, VERSION, ))
    c.execute ('''INSERT INTO history VALUES (?,?,?,?,?)''', (VERSION + 1, "", "", "", 0))
  else:
    print "Nothing to commit."
  conn.commit()
  c.close()

  # we modified the db, so the fs needs to reload everything
  # in-memory cached items will not be correct
  server_conn.clear_cache()

  # this will release the lock
  server_conn.close()

def revert (repo, VERSION, verbose = True):
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

  if verbose:
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
