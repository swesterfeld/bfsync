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
import os
import bfsyncdb

class ApplyTool:
  def __init__ (self, c, bdb, VERSION):
    self.c = c
    self.bdb = bdb
    self.VERSION = VERSION

  def detach_link (self, description, dir_id, name):
    error_str = description + " (dir_id = %s and name = %s): " % (dir_id, name)

    self.c.execute ("""SELECT * FROM links WHERE dir_id = ? AND name = ? AND ? >= vmin AND ? <= vmax""",
                    (dir_id, name, self.VERSION, self.VERSION))
    count = 0
    for r in self.c:
      if (count == 0):
        old_row = r
      else:
        raise Exception (error_str + "found more than one old entry")
      count += 1
    if count == 0:
      raise Exception (error_str + "not found in db")
    old_vmin = old_row[0]
    old_vmax = old_row[1]
    if old_vmax != self.VERSION:
      raise Exception (error_str + "max version is not current version")
    if old_vmin > self.VERSION - 1:
      raise Exception (error_str + "min version is newer than (current version - 1)")

    self.c.execute ("""UPDATE links SET vmax = ? WHERE dir_id = ? AND name = ? AND ? >= vmin AND ? <= vmax""",
                    (self.VERSION - 1, dir_id, name, self.VERSION, self.VERSION))

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

def apply (repo, diff, expected_hash = None, server = True, verbose = True, commit_args = None):
  changes = parse_diff (diff)
  conn = repo.conn

  c = conn.cursor()

  VERSION = repo.first_unused_version()

  apply_tool = ApplyTool (c, repo.bdb, VERSION)

  for change in changes:
    if change[0] == "l+":
      apply_tool.apply_link_plus (change[1:])
    if change[0] == "i+":
      apply_tool.apply_inode_plus (change[1:])
    if change[0] == "l!":
      apply_tool.apply_link_change (change[1:])
    if change[0] == "i!":
      apply_tool.apply_inode_change (change[1:])
    if change[0] == "l-":
      apply_tool.apply_link_minus (change[1:])
    if change[0] == "i-":
      apply_tool.apply_inode_minus (change[1:])

  conn.commit()
  if expected_hash:
    commit (repo, diff, expected_hash, server = server, verbose = verbose, commit_args = commit_args)
  else:
    commit (repo, server = server, verbose = verbose, commit_args = commit_args)
