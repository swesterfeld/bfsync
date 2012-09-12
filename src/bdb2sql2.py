#!/usr/bin/python

from bfsync.utils import *
from bfsync.xzutils import xzcat2
from bfsync.diffutils import DiffIterator
import psycopg2 as dbapi2
from bfsync.StatusLine import status_line, OutputSubsampler

conn = dbapi2.connect (database="bfsync", user="postgres", host="bigraidn1", port=5410) #, password="python")
cur = conn.cursor ()
cur.execute ("DROP TABLE IF EXISTS history")
cur.execute ("""
  CREATE TABLE history (
    version   integer,
    hash      varchar,
    author    varchar,
    message   varchar,
    time      bigint
  );
""")
cur.execute ("DROP TABLE IF EXISTS files")
cur.execute ("""
  CREATE TABLE files (
    filename  varchar,
    vmin      bigint,
    vmax      bigint,
    type      integer,
    hash      varchar,
    size      bigint
  );
""")

sql_max_version = 0

cur.execute("SELECT (version) FROM history;")
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

#print bdb_max_version, sql_max_version

ops = 0
outss = OutputSubsampler()
start_time = time.time()

def update_status():
  now_time = time.time()
  status_line.update ("version %d/%d - imported changes: %d - rate %.2f changes/sec" % (
    version, bdb_max_version, ops, ops / (now_time - start_time)
  ))

def same_data (d1, d2):
  return (d1.filename == d2.filename and d1.type == d2.type and d1.size == d2.size and d1.hash == d2.hash)

## clear old sql export entries
while True:
  repo.bdb.begin_transaction()
  deleted = repo.bdb.sql_export_clear (20000)
  repo.bdb.commit_transaction()

  if deleted == 0: # sql export table empty
    break

TRANS_OPS = 0
def maybe_split_transaction():
  global TRANS_OPS

  TRANS_OPS += 1
  if TRANS_OPS >= 20000:
    TRANS_OPS = 0
    repo.bdb.commit_transaction()
    repo.bdb.begin_transaction()

for version in range (sql_max_version + 1, bdb_max_version + 1):
  hentry = repo.bdb.load_history_entry (version)
  assert (hentry.valid)

  def walk (inode, filename):
    global ops

    if inode.valid:
      old_data = repo.bdb.sql_export_get (filename)
      data = bfsyncdb.SQLExportData()
      data.valid = True
      data.filename = filename
      data.vmin = old_data.vmin
      data.vmax = old_data.vmax
      data.type = inode.type
      data.size = inode.size
      data.hash = inode.hash
      data.export_version = version

      if (old_data.valid):
        if (same_data (old_data, data)):
          status = "same"
        else:
          status = "changed"
          cur.execute ("UPDATE files SET vmax = %s WHERE filename = %s AND vmin = %s", (version - 1, filename, old_data.vmin))
          fields = (filename, version, bfsyncdb.VERSION_INF, data.type, data.hash, data.size)
          cur.execute ("INSERT INTO files (filename, vmin, vmax, type, hash, size) VALUES (%s, %s, %s, %s, %s, %s)", fields)
          data.vmin = version
          data.vmax = bfsyncdb.VERSION_INF
          ops += 1
          if outss.need_update():
            update_status()
      else:
        fields = (filename, version, bfsyncdb.VERSION_INF, data.type, data.hash, data.size)
        cur.execute ("INSERT INTO files (filename, vmin, vmax, type, hash, size) VALUES (%s, %s, %s, %s, %s, %s)", fields)
        data.vmin = version
        data.vmax = bfsyncdb.VERSION_INF
        status = "new"
        ops += 1
        if outss.need_update():
          update_status()

      repo.bdb.sql_export_set (data)
      maybe_split_transaction()

      #print "%-60s%-12d%42s -- %s" % (data.filename, data.size, data.hash, status)

      # recurse into subdirs
      if inode.type == bfsyncdb.FILE_DIR:
        links = repo.bdb.load_links (inode.id, version)
        for link in links:
          inode_name = os.path.join (filename, link.name)
          child_inode = repo.bdb.load_inode (link.inode_id, version)
          walk (child_inode, inode_name)

  def check_deleted():
    global ops

    sxi = bfsyncdb.SQLExportIterator (repo.bdb)
    while True:
      data = sxi.get_next()
      if not data.valid:
        break
      if data.export_version != version:
        #print "%-60s%-12d%42s -- %s" % (data.filename, data.size, data.hash, "del")
        cur.execute ("UPDATE files SET vmax = %s WHERE filename = %s AND vmin = %s", (version - 1, data.filename, data.vmin))
        repo.bdb.sql_export_delete (data.filename)
        ops += 1
        maybe_split_transaction()

      if outss.need_update():
        update_status()

  repo.bdb.begin_transaction()
  id = bfsyncdb.id_root()
  inode = repo.bdb.load_inode (id, version)
  walk (inode, "/")
  check_deleted()
  repo.bdb.commit_transaction()
update_status()

conn.commit()
cur.close()
conn.close()
