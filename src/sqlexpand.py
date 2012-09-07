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

def walk (dir_id, path):
  cury = conn.cursor()
  curz = conn.cursor()
  cury.execute ("""SELECT inode_id, name, size FROM links, inodes WHERE links.inode_id = inodes.id AND 
                   inodes.vmin <= %s AND inodes.vmax >= %s AND
                   links.vmin  <= %s AND links.vmax >= %s AND dir_id=%s""", (version, version, version, version, dir_id))
  while True:
    row = cury.fetchone()
    if not row:
      break
    name = os.path.join (path, row[1])
    curz.execute ("INSERT INTO files VALUES (%s, %s, %s)", (version, version, name))
    walk (row[0], os.path.join (path, row[1]))

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
