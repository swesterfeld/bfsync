#!/usr/bin/python

import sqlite3
import os
import time

try:
  os.remove ("test/db")
except:
  pass

conn = sqlite3.connect ('test/db')
c = conn.cursor()
c.execute ('''create table inodes
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
                 ctime    integer,
                 ctime_ns integer,
                 mtime    integer,
                 mtime_ns integer
               )''')
c.execute ('''create table links
               (
                 vmin     integer,
                 vmax     integer,
                 dir_id   text,
                 inode_id text,
                 name     text
               )''')
c.execute ('''create table history
               (
                 version integer,
                 author  text,
                 message text,
                 time    integer
               )''')
c.execute ('''insert into history values (1, "", "", 0)''')

time_now = int (time.time())
c.execute ("""insert into inodes values (1, 1, "root", %d, %d, %d, "dir", "", "", 0, 0, %d, 0, %d, 0)""" % (
  os.getuid(), os.getgid(), 0755, time_now, time_now
))
conn.commit()
c.close()
