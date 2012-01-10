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
    self.c.execute ("""SELECT * FROM inodes WHERE id = ? AND ? >= vmin AND ? <= vmax""", (id, self.VERSION, self.VERSION))
    count = 0
    for r in self.c:
      if (count == 0):
        old_row = r
      else:
        raise Exception (error_str + "got more than one entry")
      count += 1
    if count == 0:
      raise Exception (error_str + "missing inode entry")
    self.c.execute ("""UPDATE inodes SET vmax = ? WHERE id = ? AND ? >= vmin AND ? <= vmax""",
                    (self.VERSION - 1, id, self.VERSION, self.VERSION))
    return old_row

  def apply_link_plus (self, row):
    self.c.execute ("INSERT INTO links VALUES (?,?,?,?,?)", (self.VERSION, self.VERSION, row[0], row[2], row[1]))

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
    print "create inode FIXME: %s" % row
    inode = bfsyncdb.INode()
    inode.vmin = self.VERSION
    inode.vmax = self.VERSION
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
    old_row = self.detach_inode ("change inode", id)

    # modify only the fields contained in the change entry
    row = [ self.VERSION, self.VERSION ] + row
    if len (old_row) != len (row):
      raise Exception ("apply inode: id=%s record length mismatch" % id)

    for i in range (2, len (row)):
      if row[i] == "":
        row[i] = old_row[i]
    self.c.execute ("""INSERT INTO inodes VALUES (?, ?, ?, ?, ?,
                                                  ?, ?, ?, ?, ?,
                                                  ?, ?, ?, ?, ?,
                                                  ?, ?)""", tuple (row))


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
