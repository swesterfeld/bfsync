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
from StatusLine import status_line, OutputSubsampler
from diffutils import diff
from utils import *
from xzutils import xz
from HashCache import hash_cache

import os
import time
import hashlib
import cPickle

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

class INodeInfo:
  def __init__ (self):
    self.names = []
    self.size  = 0

  def get_name (self):
    if len (self.names) == 1:
      return self.names[0]
    else:
      return "%s" % self.names

def type2str (type):
  if type == bfsyncdb.FILE_REGULAR:
    return "file"
  if type == bfsyncdb.FILE_SYMLINK:
    return "symlink"
  if type == bfsyncdb.FILE_DIR:
    return "dir"
  if type == bfsyncdb.FILE_FIFO:
    return "fifo"
  if type == bfsyncdb.FILE_SOCKET:
    return "socket"
  if type == bfsyncdb.FILE_BLOCK_DEV:
    return "blockdev"
  if type == bfsyncdb.FILE_CHAR_DEV:
    return "chardev"
  return "unknown"

# Status message generation
#
# Time complexity:
# ================
# - O(t) where t is the total number of inodes stored in the repository
#
# Memory usage:
# =============
# - O(c) where c is the number of changed inodes
#
def gen_status (repo):
  change_list = []

  VERSION = repo.first_unused_version()

  changed_dict = dict()
  changed_it = bfsyncdb.ChangedINodesIterator (repo.bdb)
  while True:
    id = changed_it.get_next()
    if not id.valid:
      break
    changed_dict[id.str()] = True
  del changed_it

  def walk (id, prefix, version, out_dict):
    inode = repo.bdb.load_inode (id, version)
    if inode.valid:
      id_str = id.str()
      if changed_dict.has_key (id_str):
        if not out_dict.has_key (id_str):
          out_dict[id_str] = INodeInfo()
          if inode.hash == "new":
            try:
              filename = repo.make_number_filename (inode.new_file_number)
              size = os.path.getsize (filename)
            except:
              size = 0
          else:
            size = inode.size
          out_dict[id_str].size = size
          out_dict[id_str].type = type2str (inode.type)
        if prefix == "":
          name = "/"
        else:
          name = prefix
        out_dict[id_str].names.append (name)
      if inode.type == bfsyncdb.FILE_DIR:
        links = repo.bdb.load_links (id, version)
        for link in links:
          inode_name = prefix + "/" + link.name
          walk (link.inode_id, inode_name, version, out_dict)

  new_dict = dict()
  old_dict = dict()
  walk (bfsyncdb.id_root(), "", VERSION, new_dict)
  walk (bfsyncdb.id_root(), "", VERSION - 1, old_dict)

  n_changed = 0
  bytes_changed_old = 0
  bytes_changed_new = 0
  n_added = 0
  bytes_added = 0
  n_deleted = 0
  bytes_deleted = 0
  n_renamed = 0

  for id in changed_dict:
    if id in new_dict:
      if id in old_dict: # NEW & OLD
        if old_dict[id].names != new_dict[id].names:
          change_list.append (("R!", new_dict[id].type, "%s => %s" % (old_dict[id].get_name(), new_dict[id].get_name())))
          n_renamed += 1
        else:
          change_list.append (("!", new_dict[id].type, new_dict[id].get_name()))
        n_changed += 1
        bytes_changed_old += old_dict[id].size
        bytes_changed_new += new_dict[id].size
      else: # NEW
        change_list.append (("+", new_dict[id].type , new_dict[id].get_name()))
        bytes_added += new_dict[id].size
        n_added += 1
    else: # OLD
      change_list.append (("-", old_dict[id].type, old_dict[id].get_name()))
      n_deleted += 1
      bytes_deleted += old_dict[id].size

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

class WorkingSetGenerator:
  def __init__ (self, work_function):
    self.work_function = work_function
    self.max_set_size = 20000
    self.wset = []

  def add_item (self, item):
    self.wset += [ item ]
    if len (self.wset) == self.max_set_size:
      self.work_function (self.wset)
      self.wset = []

  def finish (self):
    if len (self.wset):
      self.work_function (self.wset)
    self.wset = []

def commit (repo, expected_diff = None, expected_diff_hash = None, server = True, verbose = True, commit_args = None, need_transaction = True):
  repo_path = repo.path

  DEBUG_MEM = False
  if DEBUG_MEM:
    print_mem_usage ("commit start")

  # lock repo to allow modifications
  if server:
    server_conn = ServerConn (repo_path)
  else:
    server_conn = NoServerConn()
  server_conn.get_lock()

  VERSION = repo.first_unused_version()

  if DEBUG_MEM:
    print_mem_usage ("after stats")

  class Status:
    def __init__ (self):
      self.total_file_size = 0
      self.total_file_count = 0
      self.outss = OutputSubsampler()

    def scan_inode (self, inode):
      if inode.hash == "new":
        filename = repo.make_number_filename (inode.new_file_number)
        self.total_file_size += os.path.getsize (filename)
        self.total_file_count += 1
        if verbose and self.outss.need_update():
          status_line.update ("scanning file %d (total %s)" % (
            self.total_file_count, format_size1 (self.total_file_size)))

  status = Status()

  def update_status():
    elapsed_time = max (time.time() - status.start_time, 1)
    bytes_per_sec = max (status.bytes_done / elapsed_time, 1)
    eta = int ((status.total_file_size - status.bytes_done) / bytes_per_sec)
    status_line.update ("adding file %d/%d    %s    %.1f%%   %s   ETA: %s" % (
        status.files_added, status.files_total,
        format_size (status.bytes_done, status.total_file_size),
        status.bytes_done * 100.0 / max (status.total_file_size, 1),
        format_rate (bytes_per_sec),
        format_time (eta)
      ))

  def hash_one (filename):
    file = open (filename, "r")
    hash = hashlib.sha1()
    eof = False
    while not eof:
      data = file.read (256 * 1024)
      if data == "":
        eof = True
      else:
        hash.update (data)
        status.bytes_done += len (data)
        if verbose and status.outss.need_update():
          update_status()
    file.close()
    return hash.hexdigest()

  def print_it (wset):
    wset_number_list = []
    for inode in wset:
      if inode.hash == "new":
        wset_number_list.append (inode)
    wset_number_list.sort (key = lambda inode: inode.new_file_number)
    if need_transaction:
      repo.bdb.begin_transaction()

    for inode in wset_number_list:
      status.files_added += 1

      filename = repo.make_number_filename (inode.new_file_number)
      hash = hash_one (filename)
      size = os.path.getsize (filename)
      repo.bdb.delete_inode (inode)

      if repo.bdb.load_hash2file (hash) == 0:
        repo.bdb.store_hash2file (hash, inode.new_file_number)
      else:
        repo.bdb.add_deleted_file (inode.new_file_number)

      inode.hash = hash
      inode.size = size
      inode.new_file_number = 0
      repo.bdb.store_inode (inode)
    if need_transaction:
      repo.bdb.commit_transaction()

    # we can delete the files only if the transaction before this one succeeded,
    # since otherwise we'll still need them to rerun the last transaction
    if need_transaction:
      repo.bdb.begin_transaction()

    files = repo.bdb.load_deleted_files()
    for file_number in files:
      file_name = repo.make_number_filename (file_number)
      if os.path.exists (file_name):
        os.remove (file_name)

    repo.bdb.clear_deleted_files()
    if need_transaction:
      repo.bdb.commit_transaction()

  # read list of ids ; since this could be huge, we write the id strings
  # to a file and reread the file later on
  #
  # we also need to do this before actually modifying the database, because
  # the read cursor held by the ChangedINodesIterator will block writes

  if need_transaction:
    repo.bdb.begin_transaction()

  id_list_filename = repo.make_temp_name()

  if need_transaction:
    repo.bdb.commit_transaction()

  id_list_file = open (id_list_filename, "w")
  changed_it = bfsyncdb.ChangedINodesIterator (repo.bdb)
  while True:
    id = changed_it.get_next()
    if not id.valid:
      break
    id_list_file.write (id.str() + "\n")
    inode = repo.bdb.load_inode (id, VERSION)
    if inode.valid:
      status.scan_inode (inode)
  del changed_it # close read cursor
  id_list_file.close()

  status.files_total = status.total_file_count
  status.files_added = 0
  status.bytes_done = 0
  status.start_time = time.time()

  # process files to add in small chunks
  id_list_file = open (id_list_filename, "r")
  working_set_generator = WorkingSetGenerator (print_it)
  for id_str in id_list_file:
    id = bfsyncdb.ID (id_str.strip())
    if not id.valid:
      raise Exception ("found invalid id during commit")
    inode = repo.bdb.load_inode (id, VERSION)
    if inode.valid:
      working_set_generator.add_item (inode)
  working_set_generator.finish()
  id_list_file.close()

  if verbose:
    update_status()
    status_line.cleanup()

  if DEBUG_MEM:
    print_mem_usage ("with file_list/hash_list")

  have_message = False
  if commit_args:
    if commit_args.get ("message"):
      have_message = True

  if verbose and not have_message:
    if need_transaction:
      repo.bdb.begin_transaction()

    commit_msg_filename = repo.make_temp_name()

    if need_transaction:
      repo.bdb.commit_transaction()

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

  if DEBUG_MEM:
    print_mem_usage ("after add")

  VERSION = repo.first_unused_version()

  # compute commit diff
  if verbose:
    status_line.update ("computing changes")

  if need_transaction:
    repo.bdb.begin_transaction()

  diff_filename = repo.make_temp_name()

  if need_transaction:
    repo.bdb.commit_transaction()

  diff_file = open (diff_filename, "w")
  diff (repo, diff_file)
  diff_file.close()

  if DEBUG_MEM:
    print_mem_usage ("after diff")

  commit_size_ok = True
  if expected_diff:
    diff_file = open (diff_filename, "r")
    new_diff = diff_file.read()
    diff_file.close()
    if new_diff != expected_diff:
      raise Exception ("commit called with expected diff argument, but diffs didn't match")
    hash = expected_diff_hash
    if not repo.validate_object (hash):
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

  if DEBUG_MEM:
    print_mem_usage ("after xz")

  if commit_size_ok:
    if need_transaction:
      repo.bdb.begin_transaction()

    repo.bdb.store_history_entry (VERSION, hash, commit_author, commit_msg, commit_time)
    repo.bdb.clear_changed_inodes (1000000)

    if need_transaction:
      repo.bdb.commit_transaction()
  else:
    print "Nothing to commit."

  if DEBUG_MEM:
    print_mem_usage ("commit end")

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

def mk_journal_entry (repo, cmd):
  jentry = bfsyncdb.JournalEntry()
  jentry.operation = "commit"
  jentry.state = cPickle.dumps (cmd.get_state())
  repo.bdb.store_journal_entry (jentry)

class CommitState:
  pass

class CommitCommand:
  def __init__ (self, commit_args):
    self.commit_args = commit_args
    self.total_file_size = 0
    self.total_file_count = 0
    self.outss = OutputSubsampler()

  # update work to do
  def scan_inode (self, inode):
    if inode.hash == "new":
      filename = self.repo.make_number_filename (inode.new_file_number)
      self.total_file_size += os.path.getsize (filename)
      self.total_file_count += 1
      if self.outss.need_update():
        status_line.update ("scanning file %d (total %s)" % (
          self.total_file_count, format_size1 (self.total_file_size)))

  # create file with changed IDs
  def make_id_list (self):
    self.repo.bdb.begin_transaction()
    id_list_filename = self.repo.make_temp_name()
    self.repo.bdb.commit_transaction()

    id_list_file = open (id_list_filename, "w")
    changed_it = bfsyncdb.ChangedINodesIterator (self.repo.bdb)
    while True:
      id = changed_it.get_next()
      if not id.valid:
        break
      id_list_file.write (id.str() + "\n")
      inode = self.repo.bdb.load_inode (id, self.VERSION)
      if inode.valid:
        self.scan_inode (inode)
    del changed_it
    id_list_file.close()
    return id_list_filename

  # compute SHA1 hash of one file
  def hash_one (self, filename):
    file = open (filename, "r")
    hash = hashlib.sha1()
    eof = False
    while not eof:
      data = file.read (256 * 1024)
      if data == "":
        eof = True
      else:
        hash.update (data)
        self.bytes_done += len (data)
        if self.outss.need_update():
          self.update_status()
    file.close()
    return hash.hexdigest()

  # update SHA1 hashing status
  def update_status (self):
    elapsed_time = max (time.time() - self.start_time, 1)
    bytes_per_sec = max (self.bytes_done / elapsed_time, 1)
    eta = int ((self.total_file_size - self.bytes_done) / bytes_per_sec)
    status_line.update ("adding file %d/%d    %s    %.1f%%   %s   ETA: %s" % (
        self.files_added, self.files_total,
        format_size (self.bytes_done, self.total_file_size),
        self.bytes_done * 100.0 / max (self.total_file_size, 1),
        format_rate (bytes_per_sec),
        format_time (eta)
      ))

  def make_commit_msg (self):
    # commit message
    commit_args = self.commit_args
    verbose = True # FIXME
    have_message = False
    if commit_args:
      if commit_args.get ("message"):
        have_message = True

    if verbose and not have_message:
      self.repo.bdb.begin_transaction()
      commit_msg_filename = self.repo.make_temp_name()
      self.repo.bdb.commit_transaction()

      init_commit_msg (self.repo, commit_msg_filename)
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

    self.state.commit_author = commit_author
    self.state.commit_msg    = commit_msg
    self.state.commit_time   = commit_time

  def start (self, repo):
    self.state = CommitState()
    self.repo = repo
    self.make_commit_msg()
    self.VERSION = self.repo.first_unused_version()
    self.server_conn = ServerConn (repo.path)
    return True

  def restart (self, repo):
    self.repo = repo
    self.VERSION = self.repo.first_unused_version()
    self.server_conn = ServerConn (repo.path)
    return True

  def execute (self):
    self.files_total = self.total_file_count
    self.files_added = 0
    self.bytes_done = 0
    self.start_time = time.time()

    self.id_list_filename = self.make_id_list()

    # process files to add in small chunks
    id_list_file = open (self.id_list_filename, "r")
    TXN_OP_COUNT = 0
    self.repo.bdb.begin_transaction()
    for id_str in id_list_file:
      id = bfsyncdb.ID (id_str.strip())
      if not id.valid:
        raise Exception ("found invalid id during commit")
      inode = self.repo.bdb.load_inode (id, self.VERSION)
      if inode.valid and inode.hash == "new":
        self.files_added += 1
        filename = self.repo.make_number_filename (inode.new_file_number)
        hash = self.hash_one (filename)
        size = os.path.getsize (filename)
        self.repo.bdb.delete_inode (inode)

        if self.repo.bdb.load_hash2file (hash) == 0:
          self.repo.bdb.store_hash2file (hash, inode.new_file_number)
        else:
          self.repo.bdb.add_deleted_file (inode.new_file_number)

        inode.hash = hash
        inode.size = size
        inode.new_file_number = 0
        self.repo.bdb.store_inode (inode)

        if TXN_OP_COUNT >= 20000:
          TXN_OP_COUNT = 0
          self.repo.bdb.commit_transaction()
          self.repo.bdb.begin_transaction()
        else:
          TXN_OP_COUNT += 1
    self.repo.bdb.commit_transaction()

    id_list_file.close()

    self.update_status()
    status_line.cleanup()

    # compute commit diff
    status_line.update ("computing changes")

    self.repo.bdb.begin_transaction()
    diff_filename = self.repo.make_temp_name()
    self.repo.bdb.commit_transaction()

    diff_file = open (diff_filename, "w")
    diff (self.repo, diff_file)
    diff_file.close()

    if os.path.getsize (diff_filename) != 0:
      xz (diff_filename)
      hash = move_file_to_objects (self.repo, diff_filename + ".xz")
      commit_size_ok = True
    else:
      commit_size_ok = False

    status_line.update ("computing changes: done")

    if commit_size_ok:
      self.repo.bdb.begin_transaction()
      self.repo.bdb.store_history_entry (self.VERSION, hash, self.state.commit_author,
                                                             self.state.commit_msg,
                                                             self.state.commit_time)
      self.repo.bdb.commit_transaction()

      while True:
        self.repo.bdb.begin_transaction()
        deleted = self.repo.bdb.clear_changed_inodes (20000)
        self.repo.bdb.commit_transaction()

        if deleted == 0: # changed inodes table empty
          break
    else:
      print "Nothing to commit."

  def get_state (self):
    return self.state

  def set_state (self, state):
    self.state = state

def run_command (repo, cmd):
  if not cmd.start (repo):
    return False

  # create journal entry
  repo.bdb.begin_transaction()
  mk_journal_entry (repo, cmd)
  repo.bdb.commit_transaction()

  cmd.execute()

  # remove journal entry
  repo.bdb.begin_transaction()
  repo.bdb.clear_journal_entries()
  repo.bdb.commit_transaction()

def new_commit_continue (repo, state):
  cmd = CommitCommand (None)

  status_line.set_op ("COMMIT")

  cmd.set_state (state)
  cmd.restart (repo)
  cmd.execute()

  # remove journal entry
  repo.bdb.begin_transaction()
  repo.bdb.clear_journal_entries()
  repo.bdb.commit_transaction()

def new_commit (repo, commit_args):
  cmd = CommitCommand (commit_args)

  status_line.set_op ("COMMIT")

  run_command (repo, cmd)
