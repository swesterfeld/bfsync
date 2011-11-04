#!/usr/bin/python

from utils import *
import sys

conn, repo_path = cd_repo_connect_db()
c = conn.cursor()

diff = sys.stdin.read()

changes = parse_diff (diff)

VERSION = 1
c.execute ('''SELECT version FROM history''')
for row in c:
  VERSION = max (row[0], VERSION)

def detach_link (description, dir_id, name):
  error_str = description + " (dir_id = %s and name = %s): " % (dir_id, name)

  c.execute ("""SELECT * FROM links WHERE dir_id = ? AND name = ? AND ? >= vmin AND ? <= vmax""",
             (dir_id, name, VERSION, VERSION))
  count = 0
  for r in c:
    if (count == 0):
      old_row = r
    else:
      raise Exception (error_str + "found more than one old entry")
    count += 1
  if count == 0:
    raise Exception (error_str + "not found in db")
  old_vmin = old_row[0]
  old_vmax = old_row[1]
  if old_vmax != VERSION:
    raise Exception (error_str + "max version is not current version")
  if old_vmin > VERSION - 1:
    raise Exception (error_str + "min version is newer than (current version - 1)")

  c.execute ("""UPDATE links SET vmax = ? WHERE dir_id = ? AND name = ? AND ? >= vmin AND ? <= vmax""",
             (VERSION - 1, dir_id, name, VERSION, VERSION))

def detach_inode (description, id):
  error_str = description + " (inode_id = %s): " % id
  c.execute ("""SELECT * FROM inodes WHERE id = ? AND ? >= vmin AND ? <= vmax""", (id, VERSION, VERSION))
  count = 0
  for r in c:
    if (count == 0):
      old_row = r
    else:
      raise Exception (error_str + "got more than one entry")
    count += 1
  if count == 0:
    raise Exception (error_str + "missing inode entry")
  c.execute ("""UPDATE inodes SET vmax = ? WHERE id = ? AND ? >= vmin AND ? <= vmax""", (VERSION - 1, id, VERSION, VERSION))
  return old_row

def apply_link_plus (row):
  c.execute ("INSERT INTO links VALUES (?,?,?,?,?)", (VERSION, VERSION, row[0], row[2], row[1]))

def apply_link_minus (row):
  dir_id = row[0]
  name = row[1]
  detach_link ("delete link", dir_id, name)

def apply_link_change (row):
  dir_id = row[0]
  name = row[1]
  detach_link ("change link", dir_id, name)
  apply_link_plus (row)

def apply_inode_plus (row):
  c.execute ("""INSERT INTO inodes VALUES (?, ?, ?, ?, ?,
                                           ?, ?, ?, ?, ?,
                                           ?, ?, ?, ?, ?,
                                           ?, ?)""", tuple ([VERSION, VERSION] + row))

def apply_inode_minus (row):
  id = row[0]
  detach_inode ("delete inode", id)

def apply_inode_change (row):
  # read old values
  id = row[0]
  old_row = detach_inode ("change inode", id)

  # modify only the fields contained in the change entry
  row = [ VERSION, VERSION ] + row
  if len (old_row) != len (row):
    raise Exception ("apply inode: id=%s record length mismatch" % id)

  for i in range (2, len (row)):
    if row[i] == "":
      row[i] = old_row[i]
  c.execute ("""INSERT INTO inodes VALUES (?, ?, ?, ?, ?,
                                           ?, ?, ?, ?, ?,
                                           ?, ?, ?, ?, ?,
                                           ?, ?)""", tuple (row))


for change in changes:
  if change[0] == "l+":
    apply_link_plus (change[1:])
  if change[0] == "i+":
    apply_inode_plus (change[1:])
  if change[0] == "l!":
    apply_link_change (change[1:])
  if change[0] == "i!":
    apply_inode_change (change[1:])
  if change[0] == "l-":
    apply_link_minus (change[1:])
  if change[0] == "i-":
    apply_inode_minus (change[1:])

conn.commit()
os.system ("bfsync2 commit")
