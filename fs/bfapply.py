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

def apply_link_plus (row):
  c.execute ("INSERT INTO links VALUES (?,?,?,?,?)", tuple ([VERSION, VERSION] + row))

def apply_inode_plus (row):
  c.execute ("""INSERT INTO inodes VALUES (?, ?, ?, ?, ?,
                                           ?, ?, ?, ?, ?,
                                           ?, ?, ?, ?, ?,
                                           ?, ?)""", tuple ([VERSION, VERSION] + row))

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
  start += fcount

conn.commit()
os.system ("bfsync2 commit")
