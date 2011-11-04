#!/usr/bin/python

from utils import *
import sys

conn, repo_path = cd_repo_connect_db()
c = conn.cursor()

diff = sys.stdin.read()

sdiff = diff.split ("\0")

start = 0

VERSION = 1
c.execute ('''SELECT version FROM history''')
for row in c:
  VERSION = max (row[0], VERSION)

def link_detach (description, dir_id, name):
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

def apply_link_plus (row):
  c.execute ("INSERT INTO links VALUES (?,?,?,?,?)", (VERSION, VERSION, row[0], row[2], row[1]))

def apply_link_minus (row):
  dir_id = row[0]
  name = row[1]
  link_detach ("delete link", dir_id, name)

def apply_link_change (row):
  dir_id = row[0]
  name = row[1]
  link_detach ("change link", dir_id, name)
  apply_link_plus (row)

def apply_inode_plus (row):
  c.execute ("""INSERT INTO inodes VALUES (?, ?, ?, ?, ?,
                                           ?, ?, ?, ?, ?,
                                           ?, ?, ?, ?, ?,
                                           ?, ?)""", tuple ([VERSION, VERSION] + row))

def apply_inode_change (row):
  id = row[0]
  c.execute ("""SELECT * FROM inodes WHERE id = ? AND ? >= vmin AND ? <= vmax""", (id, VERSION, VERSION))
  count = 0
  for r in c:
    if (count == 0):
      old_row = r
    else:
      raise Exception ("got more than one entry for inode id = %s" % id)
    count += 1
  if count == 0:
    raise Exception ("missing inode entry for inode id = %s" % id)
  c.execute ("""UPDATE inodes SET vmax = ? WHERE id = ? AND ? >= vmin AND ? <= vmax""", (VERSION - 1, id, VERSION, VERSION))
  row = [ VERSION, VERSION ] + row
  if len (old_row) != len (row):
    raise Exception ("record length mismatch during inode change")
  # modify only the fields contained in the change entry
  for i in range (2, len (row)):
    if row[i] == "":
      row[i] = old_row[i]
  c.execute ("""INSERT INTO inodes VALUES (?, ?, ?, ?, ?,
                                           ?, ?, ?, ?, ?,
                                           ?, ?, ?, ?, ?,
                                           ?, ?)""", tuple (row))

while len (sdiff) - start > 1:
  fcount = 0
  if sdiff[start] == "l+" or sdiff[start] == "l!":
    fcount = 4
  elif sdiff[start] == "l-":
    fcount = 3
  elif sdiff[start] == "i+" or sdiff[start] == "i!":
    fcount = 16
  elif sdiff[start] == "i-":
    fcount = 2

  if fcount == 0:
    print sdiff[start:]
  assert (fcount != 0)

  if sdiff[start] == "l+":
    apply_link_plus (sdiff[start + 1:start + fcount])
  if sdiff[start] == "i+":
    apply_inode_plus (sdiff[start + 1:start + fcount])
  if sdiff[start] == "l!":
    apply_link_change (sdiff[start + 1:start + fcount])
  if sdiff[start] == "i!":
    apply_inode_change (sdiff[start + 1:start + fcount])
  if sdiff[start] == "l-":
    apply_link_minus (sdiff[start + 1:start + fcount])
  if sdiff[start] == "i-":
    apply_inode_minus (sdiff[start + 1:start + fcount])
  start += fcount

conn.commit()
os.system ("bfsync2 commit")
