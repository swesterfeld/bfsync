#!/usr/bin/python

import sqlite3
import CfgParser
import sys
import random
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
conn_new.execute ('''CREATE INDEX inodes_idx ON inodes (id)''')
conn_new.execute ('''CREATE TABLE links
                 (
                   vmin     integer,
                   vmax     integer,
                   dir_id   integer,
                   inode_id integer,
                   name     text
                 )''')
conn_new.execute ('''CREATE INDEX links_idx ON links (dir_id)''')
conn_new.execute ('''CREATE TABLE hash_inodes2id
                 (
                   int_id  integer,
                   id      text
                 )''')
conn_new.execute ('''CREATE INDEX hash_inodes2id_idx_id ON hash_inodes2id (id)''')
conn_new.execute ('''CREATE INDEX hash_inodes2id_idx_int_id ON hash_inodes2id (int_id)''')

id_dict = dict()

print "inodes..."
c.execute ("SELECT * FROM inodes")
for row in c:
  id = row[2]
  if not id_dict.has_key (id):
    id_dict[id] = random.randint (100000, 2 * 1000 * 1000 * 1000)

  hash = row[2]
  if not id_dict.has_key (hash):
    id_dict[hash] = random.randint (100000, 2 * 1000 * 1000 * 1000)

  lrow = list (row)
  lrow[2] = id_dict[id]
  lrow[7] = id_dict[hash]
  row = tuple (lrow)
  conn_new.execute ("""insert into inodes values (?,?,?,?,?,
                                                  ?,?,?,?,?,
                                                  ?,?,?,?,?,
                                                  ?,?)""", row)

print "links..."
c.execute ("SELECT * FROM links")
for row in c:
  dir_id = row[2]
  if not id_dict.has_key (dir_id):
    id_dict[dir_id] = random.randint (100000, 2 * 1000 * 1000 * 1000)

  inode_id = row[2]
  if not id_dict.has_key (inode_id):
    id_dict[inode_id] = random.randint (100000, 2 * 1000 * 1000 * 1000)

  lrow = list (row)
  lrow[2] = id_dict[dir_id]
  lrow[3] = id_dict[inode_id]
  row = tuple (lrow)
  conn_new.execute ("""insert into links values (?,?,?,?,?)""", row)

print "hash_inodes2id..."
c.execute ("SELECT * FROM local_inodes")
for row in c:
  #conn_new.execute ("""insert into hash_inodes2id values (?,?)""", row)
  pass
conn_new.commit()
