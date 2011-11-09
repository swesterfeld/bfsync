#!/usr/bin/python

import sqlite3
import os
import time
import CfgParser
from dbutils import create_tables, init_tables

def parse_config (filename):
  bfsync_info = CfgParser.CfgParser (filename,
  [
  ],
  [
    "repo-type",
    "repo-path",
    "mount-point",
    "cached-inodes",
    "cached-dirs",
    "sqlite-sync",
  ])
  return bfsync_info

cfg = parse_config ("test/.bfsync/config")

sqlite_sync = cfg.get ("sqlite-sync")
if len (sqlite_sync) != 1:
  raise Exception ("bad sqlite-sync setting")

if sqlite_sync[0] == "0":
  sqlite_sync = False
else:
  sqlite_sync = True

reinit_tables = not os.path.exists ('test/db')

conn = sqlite3.connect ('test/db')
c = conn.cursor()
if not sqlite_sync:
  c.execute ('''PRAGMA synchronous=off''')
if reinit_tables:
  create_tables (c)
else:
  c.execute ('''DELETE FROM local_inodes''')
  c.execute ('''DELETE FROM inodes''')
  c.execute ('''DELETE FROM links''')
  c.execute ('''DELETE FROM history''')
  c.execute ('''VACUUM''')

init_tables (c)

time_now = int (time.time())
c.execute ("""INSERT INTO inodes VALUES (1, 1, "0000000000000000000000000000000000000000", %d, %d, %d, "dir", "", "", 0, 0, 0, 1, %d, 0, %d, 0)""" % (
  os.getuid(), os.getgid(), 0755, time_now, time_now
))
conn.commit()
c.close()
