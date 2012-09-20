#!/usr/bin/python

from bfsync.utils import *
from bfsync.xzutils import xzcat2
from bfsync.diffutils import DiffIterator
import psycopg2 as dbapi2
from bfsync.StatusLine import status_line, OutputSubsampler
import sys

conn = dbapi2.connect (database="bfsync", user="postgres") # , host="bigraidn1", port=5410) #, password="python")
cur = conn.cursor ()
cur.execute ("""
  DROP TABLE IF EXISTS history;
  CREATE TABLE history (
    version   integer,
    hash      varchar,
    author    varchar,
    message   varchar,
    time      bigint
  );

  DROP TABLE IF EXISTS tags;
  CREATE TABLE tags (
    version   integer,
    tag       varchar,
    value     varchar
  );

  DROP TABLE IF EXISTS files;
  CREATE TABLE files (
    filename  varchar,
    vmin      bigint,
    vmax      bigint,
    id        varchar,
    parent_id varchar,
    type      integer,
    hash      varchar,
    size      bigint
  );

  DROP INDEX IF EXISTS files_fn_idx;
  CREATE INDEX files_fn_idx ON files (filename, vmin);
""")

sql_max_version = 0

cur.execute ("SELECT (version) FROM history;")
while True:
  row = cur.fetchone()
  if not row:
    break
  print "V:", row[0]


repo = cd_repo_connect_db()
bdb_max_version = 0
version = 1
while True:
  hentry = repo.bdb.load_history_entry (version)
  if not hentry.valid:
    break
  bdb_max_version = version
  version += 1

def get_parent (data):
  if data.id.str() == bfsyncdb.id_root().str():
    # root always has no parent
    return None
  else:
    return data.parent_id.str()

outss = OutputSubsampler()
start_time = time.time()

sql_export = bfsyncdb.SQLExport (repo.bdb)

for version in range (sql_max_version + 1, bdb_max_version + 1):
  print "exporting version %d/%d" % (version, bdb_max_version)
  sxi = sql_export.export_version (version)
  sql_start_time = time.time()
  while True:
    data = sxi.get_next()
    if data.status == data.NONE:
      break

    # continue

    if data.status == data.DEL or data.status == data.MOD:
      cur.execute ("UPDATE files SET vmax = %s WHERE filename = %s AND vmax = %s", (version - 1, data.filename, bfsyncdb.VERSION_INF))

    if data.status == data.ADD or data.status == data.MOD:
      fields = (data.filename, version, bfsyncdb.VERSION_INF, data.id.str(), get_parent (data), data.type, data.hash, data.size)
      cur.execute ("""INSERT INTO files (filename, vmin, vmax, id, parent_id, type, hash, size)
                      VALUES (%s, %s, %s, %s, %s, %s, %s, %s)""", fields)
  conn.commit()
  sql_end_time = time.time()
  print "### sql time: %.2f" % (sql_end_time - sql_start_time)

cur.close()
conn.close()
