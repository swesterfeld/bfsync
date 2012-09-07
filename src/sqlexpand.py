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
    name      varchar,
    uid       integer,
    gid       integer,
    hash      varchar,
    size      bigint
  );
""")

files_dict = dict()

def build_files (dir_id, path):
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
  cury.execute ("""SELECT dir_id, name, uid, gid, hash, size FROM links, inodes WHERE links.inode_id = inodes.id AND
                   inodes.vmin <= %s AND inodes.vmax >= %s AND
                   links.vmin  <= %s AND links.vmax >= %s""", (version, version, version, version))

  root_id = "/" + "0" * 40

  while True:
    row = cury.fetchone()
    if not row:
      break

    (dir_id, name, uid, gid, hash, size) = row

    path = name
    while dir_id != root_id:
      path = os.path.join (parent[dir_id][0], path)
      dir_id = parent[dir_id][1]

    if files_dict.has_key (path):
      if files_dict[path][3:] == row[2:]:
        vmin = files_dict[path][0]
        files_dict[path] = (vmin, version) + files_dict[path][2:]
      else:
        dump_one_record (files_dict[path])
        files_dict[path] = (version, version, path) + row[2:]
    else:
      files_dict[path] = (version, version, path) + row[2:]
  return

def dump_one_record (record):
  cur = conn.cursor()
  cur.execute ("INSERT INTO files VALUES (%s, %s, %s, %s, %s, %s, %s)", record)

def dump_remaining_records():
  for path in files_dict:
    dump_one_record (files_dict[path])

cur.execute ("SELECT version FROM history")
while True:
  row = cur.fetchone()
  if not row:
    break

  version = row[0]

  inode_id = "/" + "0" * 40
  build_files (inode_id, "/")

dump_remaining_records()

conn.commit()
