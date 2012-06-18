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

from utils import parse_diff
from commitutils import commit
from journal import mk_journal_entry, queue_command, CMD_DONE, CMD_AGAIN
from StatusLine import status_line, OutputSubsampler
import os
import bfsyncdb

class ApplyTool:
  def __init__ (self, bdb, VERSION):
    self.bdb = bdb
    self.VERSION = VERSION

  def detach_link (self, description, dir_id, name):
    error_str = description + " (dir_id = %s and name = %s): " % (dir_id, name)
    links = self.bdb.load_links (bfsyncdb.ID (dir_id), self.VERSION)
    count = 0
    for link in links:
      if link.name == name:
        count += 1
        if link.vmin > self.VERSION - 1:
          raise Exception (error_str + "min version is newer than (current version - 1)")
        if link.vmax != bfsyncdb.VERSION_INF:
          raise Exception (error_str + "max version is not inf")
        self.bdb.delete_link (link)
        link.vmax = self.VERSION - 1
        self.bdb.store_link (link)
    if count != 1:
      raise Exception (error_str + "expected 1 link candidate, got %d" % count)

  def detach_inode (self, description, id):
    error_str = description + " (inode_id = %s): " % id
    inode = self.bdb.load_inode (bfsyncdb.ID (id), self.VERSION)
    if not inode.valid:
      raise Exception (error_str + "missing inode entry")
    self.bdb.delete_inode (inode)
    inode.vmax = self.VERSION - 1
    self.bdb.store_inode (inode)
    return inode

  def apply_link_plus (self, row):
    link = bfsyncdb.Link()
    link.vmin = self.VERSION
    link.vmax = bfsyncdb.VERSION_INF
    link.dir_id = bfsyncdb.ID (row[0])
    link.name = row[1]
    link.inode_id = bfsyncdb.ID (row[2])
    self.bdb.store_link (link)
    # self.c.execute ("INSERT INTO links VALUES (?,?,?,?,?)", (self.VERSION, self.VERSION, row[0], row[2], row[1]))

  def apply_link_minus (self, row):
    dir_id = row[0]
    name = row[1]
    self.detach_link ("delete link", dir_id, name)

  def apply_link_change (self, row):
    dir_id = row[0]
    name = row[1]
    self.detach_link ("change link", dir_id, name)
    self.apply_link_plus (row)

  def apply_inode_plus (self, row):
    inode = bfsyncdb.INode()
    inode.vmin = self.VERSION
    inode.vmax = bfsyncdb.VERSION_INF
    inode.id   = bfsyncdb.ID (row[0])
    inode.uid  = int (row[1])
    inode.gid  = int (row[2])
    inode.mode  = int (row[3])
    inode.type  = int (row[4])
    inode.hash  = row[5]
    inode.link  = row[6]
    inode.size  = int (row[7])
    inode.major  = int (row[8])
    inode.minor  = int (row[9])
    inode.nlink  = int (row[10])
    inode.ctime  = int (row[11])
    inode.ctime_ns  = int (row[12])
    inode.mtime  = int (row[13])
    inode.mtime_ns  = int (row[14])
    self.bdb.store_inode (inode)
    # self.c.execute ("""INSERT INTO inodes VALUES (?, ?, ?, ?, ?,
                                                  # ?, ?, ?, ?, ?,
                                                  # ?, ?, ?, ?, ?,
                                                  # ?, ?)""", tuple ([self.VERSION, self.VERSION] + row))

  def apply_inode_minus (self, row):
    id = row[0]
    self.detach_inode ("delete inode", id)

  def apply_inode_change (self, row):
    # read old values
    id = row[0]
    inode = self.detach_inode ("change inode", id)
    inode.vmin = self.VERSION
    inode.vmax = bfsyncdb.VERSION_INF
    # keep inode.id
    if row[1] != "":
      inode.uid  = int (row[1])
    if row[2] != "":
      inode.gid  = int (row[2])
    if row[3] != "":
      inode.mode  = int (row[3])
    if row[4] != "":
      inode.type  = int (row[4])
    if row[5] != "":
      inode.hash  = row[5]
    if row[6] != "":
      inode.link  = row[6]
    if row[7] != "":
      inode.size  = int (row[7])
    if row[8] != "":
      inode.major  = int (row[8])
    if row[9] != "":
      inode.minor  = int (row[9])
    if row[10] != "":
      inode.nlink  = int (row[10])
    if row[11] != "":
      inode.ctime  = int (row[11])
    if row[12] != "":
      inode.ctime_ns  = int (row[12])
    if row[13] != "":
      inode.mtime  = int (row[13])
    if row[14] != "":
      inode.mtime_ns  = int (row[14])
    self.bdb.store_inode (inode)

class ApplyToolNew:
  def __init__ (self, bdb, VERSION):
    self.bdb = bdb
    self.VERSION = VERSION
    self.inode_repo = bfsyncdb.INodeRepo (self.bdb)

  def apply_link_plus (self, row):
    link = bfsyncdb.Link()
    link.name = row[1]
    inode = self.inode_repo.load_inode (bfsyncdb.ID (row[2]), self.VERSION)
    dir_inode = self.inode_repo.load_inode (bfsyncdb.ID (row[0]), self.VERSION)
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

  def save_changes_no_txn (self):
    self.inode_repo.save_changes_no_txn()

class ApplyState:
  pass

class ApplyCommand:
  def store_diff (self, diff):
    self.repo.bdb.begin_transaction()
    self.state.diff_filename = self.repo.make_temp_name()
    self.repo.bdb.commit_transaction()

    diff_file = open (self.state.diff_filename, "w")
    diff_file.write (diff)
    diff_file.close()

  def load_diff (self):
    diff_file = open (self.state.diff_filename, "r")
    diff = diff_file.read()
    diff_file.close()
    return diff

  def start (self, repo, diff, server, verbose, commit_args):
    self.state = ApplyState()
    self.state.change_pos = 0
    self.state.VERSION = repo.first_unused_version()
    self.repo = repo
    self.store_diff (diff)
    self.changes = parse_diff (diff)

  def restart (self, repo):
    diff = self.load_diff()
    self.changes = parse_diff (diff)
    self.repo = repo

  def execute (self):
    apply_tool = ApplyToolNew (self.repo.bdb, self.state.VERSION)

    OPS = 0
    self.repo.bdb.begin_transaction()

    for phase in range (3):
      self.state.change_pos = 0
      while self.state.change_pos < len (self.changes):
        change = self.changes[self.state.change_pos]
        self.state.change_pos += 1

        # apply one change
        if phase == 0:
          if change[0] == "i+":
            apply_tool.apply_inode_plus (change[1:])
          if change[0] == "i!":
            apply_tool.apply_inode_change (change[1:])

        if phase == 1:
          if change[0] == "l+":
            apply_tool.apply_link_plus (change[1:])
          if change[0] == "l!":
            apply_tool.apply_link_change (change[1:])
          if change[0] == "l-":
            apply_tool.apply_link_minus (change[1:])

        if phase == 2:
          if change[0] == "i-":
            apply_tool.apply_inode_minus (change[1:])

        OPS += 1
        if OPS >= 20000:
          OPS = 0
          mk_journal_entry (self.repo)
          apply_tool.save_changes_no_txn()
          self.repo.bdb.commit_transaction()
          self.repo.bdb.begin_transaction()

    apply_tool.save_changes_no_txn()
    self.repo.bdb.commit_transaction()
    return CMD_DONE

  def get_operation (self):
    return "apply"

  def get_state (self):
    return self.state

  def set_state (self, state):
    self.state = state

def apply (repo, diff, expected_hash = None, server = True, verbose = True, commit_args = None):
  if verbose:
    raise Exception ("apply with verbose = True no longer supported")

  cmd = ApplyCommand()

  cmd.start (repo, diff, server, verbose, commit_args)
  queue_command (cmd)
  commit (repo, commit_args, server = server, verbose = verbose)

  return # old code:


  #if expected_hash:
    #commit (repo, diff, expected_hash, server = server, verbose = verbose, commit_args = commit_args)
  #else:
  #  commit (repo, server = server, verbose = verbose, commit_args = commit_args)
