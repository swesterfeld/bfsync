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

def same_data (d1, d2):
  return (d1.filename == d2.filename and d1.type == d2.type and d1.size == d2.size and d1.hash == d2.hash)

for version in range (sql_max_version + 1, bdb_max_version + 1):
  print "*** importing version %d ***" % version
  hentry = repo.bdb.load_history_entry (version)
  assert (hentry.valid)

  def walk (inode, filename):
    if inode.valid:
      old_data = repo.bdb.sql_export_get (filename)
      data = bfsyncdb.SQLExportData()
      data.valid = True
      data.filename = filename
      data.type = inode.type
      data.size = inode.size
      data.hash = inode.hash
      data.export_version = version

      if (old_data.valid):
        if (same_data (old_data, data)):
          status = "same"
        else:
          status = "changed"
      else:
        status = "new"

      print "%-60s%-12d%42s -- %s" % (data.filename, data.size, data.hash, status)
      repo.bdb.sql_export_set (data)

      # recurse into subdirs
      if inode.type == bfsyncdb.FILE_DIR:
        links = repo.bdb.load_links (inode.id, version)
        for link in links:
          inode_name = os.path.join (filename, link.name)
          child_inode = repo.bdb.load_inode (link.inode_id, version)
          walk (child_inode, inode_name)

  def check_deleted():
    sxi = bfsyncdb.SQLExportIterator (repo.bdb)
    while True:
      entry = sxi.get_next()
      if not entry.valid:
        break
      if entry.export_version != version:
        print "%-60s%-12d%42s -- %s" % (entry.filename, entry.size, entry.hash, "del")
        repo.bdb.sql_export_delete (entry.filename)

  id = bfsyncdb.id_root()
  inode = repo.bdb.load_inode (id, version)
  walk (inode, "/")
  check_deleted()
