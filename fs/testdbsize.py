#!/usr/bin/python

import sqlite3
import CfgParser
import sys
from utils import *

conn, repo_path = cd_repo_connect_db()
c = conn.cursor()

os.remove (os.path.join (repo_path, 'dbnew'))
conn_new = sqlite3.connect (os.path.join (repo_path, 'dbnew'))
c_new = conn_new.cursor()

conn_new.execute ('''CREATE TABLE inodes
                 (
                   vmin     integer,
                   vmax     integer,
                   id       integer,
                   uid      integer,
                   gid      integer,
                   mode     integer,
                   type     text,
                   hash     integer,
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
conn_new.execute ('''CREATE TABLE links
                 (
                   vmin     integer,
                   vmax     integer,
                   dir_id   integer,
                   inode_id integer,
                   name     text
                 )''')
conn_new.execute ('''CREATE TABLE hash_inodes2id
                 (
                   int_id  integer,
                   id      text
                 )''')

print "inodes..."
c.execute ("SELECT * FROM inodes")
for row in c:
  conn_new.execute ("""insert into inodes values (?,?,?,?,?,
                                                  ?,?,?,?,?,
                                                  ?,?,?,?,?,
                                                  ?,?)""", row)

print "links..."
c.execute ("SELECT * FROM links")
for row in c:
  conn_new.execute ("""insert into links values (?,?,?,?,?)""", row)
conn_new.commit()
