#!/usr/bin/python

import sqlite3
import os
import time

reinit_tables = not os.path.exists ('test/db')

conn = sqlite3.connect ('test/db')
c = conn.cursor()
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
                   author  text,
                   message text,
                   time    integer
                 )''')
  c.execute ('''CREATE TABLE local_inodes
                 (
                   id      text,
                   ino     integer
                 )''')
  c.execute ('''CREATE INDEX local_inodes_idx ON local_inodes (id, ino)''')
else:
  c.execute ('''DELETE FROM local_inodes''')
  c.execute ('''DELETE FROM inodes''')
  c.execute ('''DELETE FROM links''')
  c.execute ('''DELETE FROM history''')

c.execute ('''insert into history values (1, "", "", 0)''')

time_now = int (time.time())
c.execute ("""insert into inodes values (1, 1, "root", %d, %d, %d, "dir", "", "", 0, 0, 1, %d, 0, %d, 0)""" % (
  os.getuid(), os.getgid(), 0755, time_now, time_now
))
conn.commit()
c.close()
