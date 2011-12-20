#!/usr/bin/python

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

import sqlite3
import CfgParser
import sys
import random
from utils import *

repo = cd_repo_connect_db()
conn = repo.conn
repo_path = repo.path
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

  inode_id = row[3]
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
