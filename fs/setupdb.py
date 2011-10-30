#!/usr/bin/python

import sqlite3
import os
import time
import CfgParser

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
  c.execute ('''CREATE TABLE inodes
                 (
                   vmin     integer,
                   vmax     integer,
                   id       text,
                   uid      integer,
                   gid      integer,
                   mode     integer,
                   type     text,
                   hash     text,
                   link     text,
                   size     text,
                   major    integer,
                   minor    integer,
                   nlink    integer,    /* number of hard links */
                   ctime    integer,
                   ctime_ns integer,
                   mtime    integer,
                   mtime_ns integer
                 )''')
  c.execute ('''CREATE INDEX inodes_idx ON inodes (id)''')
  c.execute ('''CREATE TABLE links
                 (
                   vmin     integer,
                   vmax     integer,
                   dir_id   text,
                   inode_id text,
                   name     text
                 )''')
  c.execute ('''CREATE INDEX links_idx ON links (dir_id)''')
  c.execute ('''CREATE TABLE history
                 (
                   version integer,
                   hash    text,
                   author  text,
                   message text,
                   time    integer
                 )''')
  c.execute ('''CREATE TABLE local_inodes
                 (
                   id      text,
                   ino     integer
                 )''')
  c.execute ('''CREATE INDEX local_inodes_idx_id ON local_inodes (id)''')
  c.execute ('''CREATE INDEX local_inodes_idx_ino ON local_inodes (ino)''')
else:
  c.execute ('''DELETE FROM local_inodes''')
  c.execute ('''DELETE FROM inodes''')
  c.execute ('''DELETE FROM links''')
  c.execute ('''DELETE FROM history''')

c.execute ('''PRAGMA default_cache_size=%d''' % (1024 * 1024))     # use 128M cache size
c.execute ('''INSERT INTO history VALUES (1, "", "", "", 0)''')

time_now = int (time.time())
c.execute ("""INSERT INTO inodes VALUES (1, 1, "0000000000000000000000000000000000000000", %d, %d, %d, "dir", "", "", 0, 0, 0, 1, %d, 0, %d, 0)""" % (
  os.getuid(), os.getgid(), 0755, time_now, time_now
))
conn.commit()
c.close()
