# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

from diffutils import DiffIterator
from commitutils import commit
from journal import mk_journal_entry, queue_command, CMD_DONE, CMD_AGAIN
from StatusLine import status_line, OutputSubsampler
import os
import bfsyncdb

class ApplyTool:
  def __init__ (self, bdb, VERSION):
    self.bdb = bdb
    self.VERSION = VERSION
    self.inode_repo = bfsyncdb.INodeRepo (self.bdb)

  def apply_link_plus (self, row):
    inode = self.inode_repo.load_inode (bfsyncdb.ID (row[2]), self.VERSION)
    dir_inode = self.inode_repo.load_inode (bfsyncdb.ID (row[0]), self.VERSION)
    dir_inode.add_link_raw (inode, row[1], self.VERSION)

  def apply_link_change (self, row):
    inode = self.inode_repo.load_inode (bfsyncdb.ID (row[2]), self.VERSION)
    dir_inode = self.inode_repo.load_inode (bfsyncdb.ID (row[0]), self.VERSION)
    dir_inode.unlink_raw (row[1], self.VERSION)
    dir_inode.add_link_raw (inode, row[1], self.VERSION)

  def apply_link_minus (self, row):
    inode = self.inode_repo.load_inode (bfsyncdb.ID (row[0]), self.VERSION)
    inode.unlink_raw (row[1], self.VERSION)

  def apply_inode_plus (self, row):
    inode = self.inode_repo.create_inode_with_id (bfsyncdb.ID (row[0]), self.VERSION)
    inode.set_uid (int (row[1]))
    inode.set_gid (int (row[2]))
    inode.set_mode (int (row[3]))
    inode.set_type (int (row[4]))
    inode.set_hash (row[5])
    inode.set_link (row[6])
    inode.set_size (int (row[7]))
    inode.set_major (int (row[8]))
    inode.set_minor (int (row[9]))
    inode.set_nlink (int (row[10]))
    inode.set_ctime (int (row[11]))
    inode.set_ctime_ns (int (row[12]))
    inode.set_mtime (int (row[13]))
    inode.set_mtime_ns (int (row[14]))

  def apply_inode_change (self, row):
    inode = self.inode_repo.load_inode (bfsyncdb.ID (row[0]), self.VERSION)
    if row[1] != "":
      inode.set_uid (int (row[1]))
    if row[2] != "":
      inode.set_gid (int (row[2]))
    if row[3] != "":
      inode.set_mode (int (row[3]))
    if row[4] != "":
      inode.set_type (int (row[4]))
    if row[5] != "":
      inode.set_hash (row[5])
    if row[6] != "":
      inode.set_link (row[6])
    if row[7] != "":
      inode.set_size (int (row[7]))
    if row[8] != "":
      inode.set_major (int (row[8]))
    if row[9] != "":
      inode.set_minor (int (row[9]))
    if row[10] != "":
      inode.set_nlink (int (row[10]))
    if row[11] != "":
      inode.set_ctime (int (row[11]))
    if row[12] != "":
      inode.set_ctime_ns (int (row[12]))
    if row[13] != "":
      inode.set_mtime (int (row[13]))
    if row[14] != "":
      inode.set_mtime_ns (int (row[14]))

  def apply_inode_minus (self, row):
    inode = self.inode_repo.load_inode (bfsyncdb.ID (row[0]), self.VERSION)
    inode.set_nlink (0)        # will be omitted during save

  def save_changes_no_txn (self):
    self.inode_repo.save_changes_no_txn()
    self.inode_repo.delete_unused_keep_count (200000)

class ApplyState:
  pass

class ApplyCommand:
  def open_diff (self):
    self.diff_file = open (self.state.diff_filename, "r")
    self.diff_iterator = DiffIterator (self.diff_file)

  def start (self, repo, diff_filename, server, verbose, commit_args):
    self.state = ApplyState()
    self.state.change_pos = 0
    self.state.phase = 0
    self.state.VERSION = repo.first_unused_version()
    self.state.diff_filename = diff_filename
    self.repo = repo
    self.open_diff()

  def restart (self, repo):
    self.repo = repo
    self.open_diff()

  def execute (self):
    apply_tool = ApplyTool (self.repo.bdb, self.state.VERSION)

    OPS = 0
    self.repo.bdb.begin_transaction()

    # apply is done in three phases:
    #
    # - phase == 0 - apply i+ and i! changes
    # - phase == 1 - apply l+, l- and l! changes
    # - phase == 2 - apply i- changes
    #
    # this ensures that l+, l- and l! are executed on a database that
    # contains both, the directory inode (link src) and the inode the
    # link points to (link dest)
    while self.state.phase < 3:
      self.diff_iterator.seek (self.state.change_pos)
      while True:
        change = self.diff_iterator.next()
        if change is None:
          break

        self.state.change_pos += 1

        # apply one change
        if self.state.phase == 0:
          if change[0] == "i+":
            apply_tool.apply_inode_plus (change[1:])
          if change[0] == "i!":
            apply_tool.apply_inode_change (change[1:])

        if self.state.phase == 1:
          if change[0] == "l+":
            apply_tool.apply_link_plus (change[1:])
          if change[0] == "l!":
            apply_tool.apply_link_change (change[1:])
          if change[0] == "l-":
            apply_tool.apply_link_minus (change[1:])

        if self.state.phase == 2:
          if change[0] == "i-":
            apply_tool.apply_inode_minus (change[1:])

        OPS += 1
        if OPS >= 20000:
          OPS = 0
          mk_journal_entry (self.repo)
          apply_tool.save_changes_no_txn()
          self.repo.bdb.commit_transaction()
          self.repo.bdb.begin_transaction()

      self.state.change_pos = 0
      self.state.phase += 1

    apply_tool.save_changes_no_txn()
    self.repo.bdb.commit_transaction()
    self.diff_file.close()
    return CMD_DONE

  def get_operation (self):
    return "apply"

  def get_state (self):
    return self.state

  def set_state (self, state):
    self.state = state

def apply (repo, diff_filename, expected_hash = None, server = True, verbose = True, commit_args = None):
  if verbose:
    raise Exception ("apply with verbose = True no longer supported")

  cmd = ApplyCommand()

  cmd.start (repo, diff_filename, server, verbose, commit_args)
  queue_command (cmd)
  commit (repo, commit_args, server = server, verbose = verbose)

  return # old code:


  #if expected_hash:
    #commit (repo, diff, expected_hash, server = server, verbose = verbose, commit_args = commit_args)
  #else:
  #  commit (repo, server = server, verbose = verbose, commit_args = commit_args)
