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

## clear old sql export entries
n_clear = 0
while True:
  repo.bdb.begin_transaction()
  deleted = repo.bdb.sql_export_clear (20000)
  repo.bdb.commit_transaction()
  n_clear += deleted
  status_line.update ("clear old sql export table... %d" % n_clear)

  if deleted == 0: # sql export table empty
    break

status_line.update ("clear old sql export table... done.")
status_line.cleanup()

outss = OutputSubsampler()
start_time = time.time()

sql_export = bfsyncdb.SQLExport (repo.bdb)

for version in range (sql_max_version + 1, bdb_max_version + 1):
  print "exporting version %d/%d" % (version, bdb_max_version)
  sql_export.export_version (version)

cur.close()
conn.close()
