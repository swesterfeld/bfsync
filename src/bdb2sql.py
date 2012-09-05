#!/usr/bin/python

import psycopg2 as dbapi2
import bfsyncdb
from bfsync.utils import *

conn = dbapi2.connect (database="bfsync", user="postgres", host="bigraidn1", port=5410) #, password="python")
cur = conn.cursor()
cur.execute ("DROP TABLE IF EXISTS links")
cur.execute ("DROP TABLE IF EXISTS inodes")
cur.execute ("""
  CREATE TABLE links (
    dir_id    varchar,
    vmin      bigint,
    vmax      bigint,
    inode_id  varchar,
    name      varchar
  );
""")

cur.execute ("""
  CREATE TABLE inodes (
    id        varchar,
    vmin      bigint,
    vmax      bigint,
    uid       integer,
    gid       integer,
    mode      integer,
    type      integer,
    hash      varchar,
    link      varchar,
    size      bigint,
    major     integer,
    minor     integer,
    nlink     integer,
    ctime     integer,
    ctime_ns  integer,
    mtime     integer,
    mtime_ns  integer,
    new_file_number   integer
  );
""")


repo = cd_repo_connect_db()
ai = bfsyncdb.AllINodesIterator (repo.bdb)

ops = 0

while True:
  id = ai.get_next()
  if not id.valid:
    break
  inodes = repo.bdb.load_all_inodes (id)
  links = repo.bdb.load_all_links (id)

  for link in links:
    cur.execute("INSERT INTO links (dir_id, vmin, vmax, inode_id, name) VALUES (%s, %s, %s, %s, %s)",  (
                link.dir_id.str(), link.vmin, link.vmax, link.inode_id.str(), link.name))
    print "\r%d" % ops,
    ops += 1
  for inode in inodes:
    cur.execute ("""INSERT INTO inodes (id, vmin, vmax, uid, gid, mode, type, hash, link, size, major, minor, nlink, ctime, ctime_ns,
                                        mtime, mtime_ns, new_file_number)
                           VALUES      (%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s)""", (
                 inode.id.str(), inode.vmin, inode.vmax, inode.uid, inode.gid, inode.mode, inode.type, inode.hash, inode.link,
                 inode.size, inode.major, inode.minor, inode.nlink, inode.ctime, inode.ctime_ns, inode.mtime, inode.mtime_ns,
                 inode.new_file_number))
    print "\r%d" % ops,
    ops += 1

cur.execute("SELECT * FROM links;")
while True:
  row = cur.fetchone()
  if not row:
    break
  print row

cur.execute("SELECT * FROM inodes;")
while True:
  row = cur.fetchone()
  if not row:
    break
  print row


conn.commit()
cur.close()
conn.close()
