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

from ServerConn import ServerConn
from StatusLine import status_line
from diffutils import diff
from utils import *
from xzutils import xz
from HashCache import hash_cache

import os
import time

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

class INode2Name:
  def __init__ (self, c, version):
    self.link_dict = dict()
    self.c = c
    self.version = version

    c.execute ("SELECT inode_id, dir_id, name FROM links WHERE ? >= vmin AND ? <= VMAX", (version, version))
    for row in c:
      self.link_dict[row[0]] = row[1:]

  def lookup (self, id):
    if id == "0" * 40:
      return "/"
    if not self.link_dict.has_key (id):
      raise Exception ("INode2Name: id %s not found" % id)
    (dir_id, name) = self.link_dict[id]
    return os.path.join (self.lookup (dir_id), name)

  def links (self, id):
    if id == "0" * 40:
      return "/"
    return self.link_dict[id]

class INodeInfo:
  def __init__ (self, repo, c, version):
    self.inode_dict = dict()
    self.repo = repo
    self.c = c
    self.version = version

    c.execute ("SELECT id, type, size, hash FROM inodes WHERE ? >= vmin AND ? <= VMAX", (version, version))
    for row in c:
      self.inode_dict[row[0]] = row[1:]

  def get_type (self, id):
    return self.inode_dict[id][0]

  def get_size (self, id):
    r = self.inode_dict[id]
    hash = r[2]
    size = r[1]
    if hash == "new":
      try:
        size = os.path.getsize (os.path.join (self.repo.path, "new", make_object_filename (id)))
      except:
        pass
    return size

def gen_status (repo):
  conn = repo.conn
  c = conn.cursor()

  return [ "FIXME" ]

  VERSION = 1
  c.execute ('''SELECT version FROM history''')
  for row in c:
    VERSION = max (row[0], VERSION)

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
  bytes_changed_old = 0
  bytes_changed_new = 0
  n_added = 0
  bytes_added = 0
  n_deleted = 0
  bytes_deleted = 0
  n_renamed = 0

  old_inode2name = INode2Name (c, VERSION - 1)
  old_inode_info = INodeInfo (repo, c, VERSION - 1)
  new_inode2name = INode2Name (c, VERSION)
  new_inode_info = INodeInfo (repo, c, VERSION)

  change_list = []
  for inode in touched_inodes:
    inode_name = new_inode2name.lookup (inode)
    inode_type = new_inode_info.get_type (inode)
    if inode in old_inodes:
      old_links = old_inode2name.links (inode)
      new_links = new_inode2name.links (inode)
      if old_links != new_links:
        old_name = old_inode2name.lookup (inode)
        change_list.append (("R!", inode_type, "%s => %s" % (old_name, inode_name)))
        n_renamed += 1
      else:
        change_list.append (("!", inode_type, inode_name))
      n_changed += 1
      bytes_changed_old += old_inode_info.get_size (inode)
      bytes_changed_new += new_inode_info.get_size (inode)
    else:
      change_list.append (("+", inode_type, inode_name))
      n_added += 1
      bytes_added += new_inode_info.get_size (inode)

  for inode in old_inodes:
    if not inode in new_inodes:
      inode_name = old_inode2name.lookup (inode)
      inode_type = old_inode_info.get_type (inode)
      change_list.append (("-", inode_type, inode_name))
      n_deleted += 1
      bytes_deleted += old_inode_info.get_size (inode)

  status = []
  status += [ "+ %d objects added (%s)." % (n_added, format_size1 (bytes_added)) ]
  status += [ "! %d objects changed (%s => %s)." % (n_changed,
                                                    format_size1 (bytes_changed_old),
                                                    format_size1 (bytes_changed_new)) ]
  status += [ "- %d objects deleted (%s)." % (n_deleted, format_size1 (bytes_deleted)) ]
  status += [ "R %d objects renamed." % n_renamed ]
  status += [ "" ]

  change_list.sort (key = lambda x: x[2])
  for change in change_list:
    status += [ "%2s %-8s %s" % change ]
  return status

def init_commit_msg (repo, filename):
  conn = repo.conn
  c = conn.cursor()

  VERSION = repo.first_unused_version()

  msg_file = open (filename, "w")
  msg_file.write ("\n# commit version %d\n#\n" % VERSION)
  status = gen_status (repo)
  for s in status:
    msg_file.write ("# %s\n" % s)
  msg_file.close()

def get_author():
  username = os.getlogin()
  hostname = os.uname()[1]
  return "%s@%s" % (username, hostname)

def commit (repo, expected_diff = None, expected_diff_hash = None, server = True, verbose = True, commit_args = None):
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

  print "FIXME: commit new!!"
  # c.execute ('''SELECT id FROM inodes WHERE hash = "new"''')
  # for row in c:
  #   id = row[0]
  #   filename = os.path.join (repo_path, "new", id[0:2], id[2:])
  #   hash_list += [ filename ]
  #   file_list += [ (filename, id) ]

  have_message = False
  if commit_args:
    if commit_args.get ("message"):
      have_message = True

  if verbose and not have_message:
    commit_msg_filename = repo.make_temp_name()
    init_commit_msg (repo, commit_msg_filename)
    launch_editor (commit_msg_filename)
    if not commit_msg_ok (commit_msg_filename):
      raise BFSyncError ("commit: empty commit message - aborting commit")
    commit_msg = ""
    commit_msg_file = open (commit_msg_filename, "r")
    for line in commit_msg_file:
      if line[0] != "#":
        commit_msg += line
    commit_msg_file.close()
  else:
    commit_msg = "commit message"

  commit_author = None
  commit_time = None

  if commit_args:
    commit_author = commit_args.get ("author")
    commit_time = commit_args.get ("time")
    xcommit_msg = commit_args.get ("message")
    if xcommit_msg is not None:
      commit_msg = xcommit_msg

  if commit_author is None:
    commit_author = get_author()

  if commit_time is None:
    commit_time = int (time.time())

  hash_cache.hash_all (hash_list, verbose)
  if verbose:
    status_line.cleanup()

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

  VERSION = repo.first_unused_version()

  # compute commit diff
  if verbose:
    status_line.update ("computing changes")

  diff_filename = repo.make_temp_name()
  diff_file = open (diff_filename, "w")
  diff (repo, VERSION - 1, VERSION, diff_file)
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
    def bump_inode_version (inode):
      if inode.vmax != VERSION:
        raise Exception ("error bumping inode version, expected version %d, got version %d" % (VERSION, inode.vmax))
      repo.bdb.delete_inode (inode)
      inode.vmax += 1
      repo.bdb.store_inode (inode)

    def bump_link_version (link):
      if link.vmax != VERSION:
        raise Exception ("error bumping link version, expected version %d, got version %d" % (VERSION, link.vmax))
      repo.bdb.delete_link (link)
      link.vmax += 1
      repo.bdb.store_link (link)

    repo.foreach_inode_link (VERSION, bump_inode_version, bump_link_version)
    repo.bdb.store_history_entry (VERSION, hash, commit_author, commit_msg, commit_time)
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
