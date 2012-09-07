#!/usr/bin/python

import psycopg2 as dbapi2
from bfsync.utils import *
from bfsync.StatusLine import status_line, OutputSubsampler

conn = dbapi2.connect (database="bfsync", user="postgres", host="bigraidn1", port=5410) #, password="python")
cur = conn.cursor()

cur.execute ("DROP TABLE IF EXISTS files")
cur.execute ("""
  CREATE TABLE files (
    vmin      bigint,
    vmax      bigint,
    name      varchar
  );
""")

cur.execute ("DROP INDEX IF EXISTS i1")
#cur.execute ("CREATE INDEX i1 ON links (inode_id)")
cur.execute ("DROP INDEX IF EXISTS i2")
#cur.execute ("CREATE INDEX i2 ON inodes (id)")
cur.execute ("DROP INDEX IF EXISTS i3")
#cur.execute ("CREATE INDEX i3 ON links (dir_id)")

recs = 0
def walk (dir_id, path):
  global recs
  parent = dict()
  cury = conn.cursor()
  cury.execute ("""SELECT dir_id, inode_id, name FROM links, inodes WHERE links.inode_id = inodes.id AND inodes.type = 3 AND
                   inodes.vmin <= %s AND inodes.vmax >= %s AND
                   links.vmin  <= %s AND links.vmax >= %s""", (version, version, version, version))
  while True:
    row = cury.fetchone()
    if not row:
      break

    dir_id = row[0]
    inode_id = row[1]
    name = row[2]
    parent[inode_id] = (name, dir_id)

  curz = conn.cursor()
  cury.execute ("""SELECT dir_id, name, size FROM links, inodes WHERE links.inode_id = inodes.id AND
                   inodes.vmin <= %s AND inodes.vmax >= %s AND
                   links.vmin  <= %s AND links.vmax >= %s""", (version, version, version, version))
  while True:
    row = cury.fetchone()
    if not row:
      break

    dir_id = row[0]
    name = row[1]
    size = row[2]

    root_id = "/" + "0" * 40
    path = name
    while dir_id != root_id:
      path = os.path.join (parent[dir_id][0], path)
      dir_id = parent[dir_id][1]
    recs += 1
    curz.execute ("INSERT INTO files VALUES (%s, %s, %s)", (version, version, path))
  print recs
  return

cur.execute ("SELECT version FROM history")
while True:
  row = cur.fetchone()
  if not row:
    break

  version = row[0]
  print "version:", version

  inode_id = "/" + "0" * 40
  walk (inode_id, "/")

conn.commit()
