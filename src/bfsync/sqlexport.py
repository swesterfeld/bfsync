# bfsync: Big File synchronization tool

# Copyright (C) 2012 Stefan Westerfeld
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

from bfsync.utils import *
from bfsync.xzutils import xzcat2
from bfsync.diffutils import DiffIterator
import psycopg2 as dbapi2
from bfsync.StatusLine import status_line, OutputSubsampler
import argparse
import sys

WITH_SQL = True

def sql_export (repo, args):
  parser = argparse.ArgumentParser (prog='bfsync sql-export')
  parser.add_argument ('-d', help='set database')
  parser.add_argument ('-u', help='set user')
  parser.add_argument ('-w', help='set password')
  parser.add_argument ('-H', help='set host')
  parser.add_argument ('-p', help='set port')
  parser.add_argument ("-r", action="store_true", dest="initial", default=False, help='reset all database tables')
  parsed_args = parser.parse_args (args)

  connection_args = dict()
  if (parsed_args.d):
    connection_args["database"] = parsed_args.d
  if (parsed_args.u):
    connection_args["user"] = parsed_args.u
  if (parsed_args.w):
    connection_args["password"] = parsed_args.w
  if (parsed_args.H):
    connection_args["host"] = parsed_args.H
  if (parsed_args.p):
    connection_args["port"] = parsed_args.p

  conn = dbapi2.connect (**connection_args)
  cur = conn.cursor ()

  if WITH_SQL:
    if (parsed_args.initial):
      cur.execute ("""
        DROP TABLE IF EXISTS history;
        DROP TABLE IF EXISTS tags;
        DROP TABLE IF EXISTS files;
      """)
      conn.commit()
      print "Reset: reinitialized all database tables."

    cur.execute ("""
      CREATE TABLE IF NOT EXISTS history (
        version   integer,
        hash      varchar,
        author    varchar,
        message   varchar,
        time      bigint
      );

      CREATE TABLE IF NOT EXISTS tags (
        version   integer,
        tag       varchar,
        value     varchar
      );

      CREATE TABLE IF NOT EXISTS files (
        filename  varchar,
        vmin      bigint,
        vmax      bigint,
        id        varchar,
        parent_id varchar,
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
        CONSTRAINT name_vmin PRIMARY KEY (filename, vmin)
      );
      CREATE TEMPORARY TABLE IF NOT EXISTS temp_delete (
        filename  varchar PRIMARY KEY
      );
    """)

  # compute max version that was already imported earlier
  sql_max_version = 0

  if WITH_SQL:
    cur.execute ("SELECT (version) FROM history;")
    while True:
      row = cur.fetchone()
      if not row:
        break
      version = row[0]
      sql_max_version = max (sql_max_version, version)

  # compute max version in the Repository repo
  bdb_max_version = 0
  version = 1
  while True:
    hentry = repo.bdb.load_history_entry (version)
    if not hentry.valid:
      break
    bdb_max_version = version
    version += 1

  outss = OutputSubsampler()
  start_time = time.time()

  # create temp name for inserts
  repo.bdb.begin_transaction()
  insert_filename = repo.make_temp_name()
  delete_filename = repo.make_temp_name()
  repo.bdb.commit_transaction()

  sql_export = bfsyncdb.SQLExport (repo.bdb)

  for version in range (sql_max_version + 1, bdb_max_version + 1):
    print "\n::: exporting version %d/%d :::" % (version, bdb_max_version)
    sys.stdout.flush()

    sql_export.export_version (version, insert_filename, delete_filename)

    if WITH_SQL:
      sql_start_time = time.time()
      cur.execute ("DELETE FROM temp_delete")

      delete_file = open (delete_filename, "r")
      cur.copy_from (delete_file, "temp_delete")
      delete_file.close()

      cur.execute ("UPDATE files SET vmax = %s FROM temp_delete WHERE files.filename = temp_delete.filename AND vmax = %s",
                   (version - 1, bfsyncdb.VERSION_INF))
      cur.execute ("DELETE FROM temp_delete")

      insert_file = open (insert_filename, "r")
      cur.copy_from (insert_file, "files")
      insert_file.close()

      # import history entry

      hentry = repo.bdb.load_history_entry (version)
      fields = ( hentry.version, hentry.hash, hentry.author, hentry.message, hentry.time )
      if WITH_SQL:
        cur.execute ("INSERT INTO history (version, hash, author, message, time) VALUES (%s, %s, %s, %s, %s)", fields)

      # import tags

      tags = repo.bdb.list_tags (hentry.version)
      for t in tags:
        values = repo.bdb.load_tag (hentry.version, t)
        for v in values:
          fields = (hentry.version, t, v)
          cur.execute ("INSERT INTO tags (version, tag, value) VALUES (%s, %s, %s)", fields)

      conn.commit()
      sql_end_time = time.time()

      print "### sql time: %.2f" % (sql_end_time - sql_start_time)
      sys.stdout.flush()

  cur.close()
  conn.close()
