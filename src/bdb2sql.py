#!/usr/bin/python

import psycopg2 as dbapi2
import bfsyncdb
from bfsync.utils import *
from bfsync.StatusLine import status_line, OutputSubsampler

conn = dbapi2.connect (database="bfsync", user="postgres", host="bigraidn1", port=5410) #, password="python")
cur = conn.cursor()
cur.execute ("DROP TABLE IF EXISTS links")
cur.execute ("DROP TABLE IF EXISTS inodes")
cur.execute ("DROP TABLE IF EXISTS history")
cur.execute ("DROP TABLE IF EXISTS tags")
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
    ctime     bigint,
    ctime_ns  integer,
    mtime     bigint,
    mtime_ns  integer,
    new_file_number   integer
  );
""")
cur.execute ("""
  CREATE TABLE history (
    version   integer,
    hash      varchar,
    author    varchar,
    message   varchar,
    time      bigint
  );
""")
cur.execute ("""
  CREATE TABLE tags (
    version   integer,
    tag       varchar,
    value     varchar
  );
""")


repo = cd_repo_connect_db()
ai = bfsyncdb.AllINodesIterator (repo.bdb)

ops = 0
outss = OutputSubsampler()

def update_status():
  status_line.update ("%d" % ops)

while True:
  id = ai.get_next()
  if not id.valid:
    break
  inodes = repo.bdb.load_all_inodes (id)
  links = repo.bdb.load_all_links (id)

  for link in links:
    fields = ( link.dir_id.str(), link.vmin, link.vmax, link.inode_id.str(), link.name )
    # print "L", fields

    cur.execute ("INSERT INTO links (dir_id, vmin, vmax, inode_id, name) VALUES (%s, %s, %s, %s, %s)", fields)
    ops += 1
    if outss.need_update():
      update_status()
  for inode in inodes:
    fields = (
      inode.id.str(), inode.vmin, inode.vmax, inode.uid, inode.gid, inode.mode, inode.type, inode.hash, inode.link,
      inode.size, inode.major, inode.minor, inode.nlink, inode.ctime, inode.ctime_ns, inode.mtime, inode.mtime_ns,
      inode.new_file_number
    )
    # print "I", fields

    cur.execute ("""INSERT INTO inodes (id, vmin, vmax, uid, gid, mode, type, hash, link, size, major, minor, nlink, ctime, ctime_ns,
                                        mtime, mtime_ns, new_file_number)
                           VALUES      (%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s)""", fields)
    ops += 1
    if outss.need_update():
      update_status()

# import history table & tags table
VERSION = 1
while True:
  hentry = repo.bdb.load_history_entry (VERSION)
  VERSION += 1

  if not hentry.valid:
    break

  fields = ( hentry.version, hentry.hash, hentry.author, hentry.message, hentry.time )
  # print "H", fields

  cur.execute ("INSERT INTO history (version, hash, author, message, time) VALUES (%s, %s, %s, %s, %s)", fields)

  tags = repo.bdb.list_tags (hentry.version)
  for t in tags:
    values = repo.bdb.load_tag (hentry.version, t)
    for v in values:
      fields = (hentry.version, t, v)
      cur.execute ("INSERT INTO tags (version, tag, value) VALUES (%s, %s, %s)", fields)

update_status()

print

cur.execute("SELECT COUNT (*) FROM links;")
while True:
  row = cur.fetchone()
  if not row:
    break
  print "LINKS:", row[0]

cur.execute("SELECT COUNT (*) FROM inodes;")
while True:
  row = cur.fetchone()
  if not row:
    break
  print "INODES:", row[0]


conn.commit()
cur.close()
conn.close()
