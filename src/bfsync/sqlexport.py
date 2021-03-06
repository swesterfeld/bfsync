# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

from bfsync.utils import *
from bfsync.xzutils import xzcat2
from bfsync.diffutils import DiffIterator
from bfsync.StatusLine import status_line, OutputSubsampler
import argparse
import sys

WITH_SQL = True

def sql_export (repo, args):
  # we don't want a dependency on psycopg2, so we only import it if we need it
  import psycopg2 as dbapi2

  parser = argparse.ArgumentParser (prog='bfsync sql-export')
  parser.add_argument ('-d', help='set database')
  parser.add_argument ('-u', help='set user')
  parser.add_argument ('-w', help='set password')
  parser.add_argument ('-H', help='set host')
  parser.add_argument ('-p', help='set port')
  parser.add_argument ("-r", action="store_true", dest="initial", default=False, help='reset all database tables')
  parsed_args = parser.parse_args (args)

  connection_args = dict()
  have_cmdline_args = False
  if (parsed_args.d):
    connection_args["database"] = parsed_args.d
    have_cmdline_args = True
  if (parsed_args.u):
    connection_args["user"] = parsed_args.u
    have_cmdline_args = True
  if (parsed_args.w):
    connection_args["password"] = parsed_args.w
    have_cmdline_args = True
  if (parsed_args.H):
    connection_args["host"] = parsed_args.H
    have_cmdline_args = True
  if (parsed_args.p):
    connection_args["port"] = parsed_args.p
    have_cmdline_args = True

  def cfg_value (name):
    xlist = repo.config.get ("sql-export/%s" % name)
    if len (xlist) > 1:
      raise BFSyncError ("sql-export: need at most sql-export/%s entry" % name)
    if len (xlist) == 0:
      return None
    else:
      return xlist[0]

  if not have_cmdline_args:
    have_config_args = False
    if cfg_value ("database"):
      connection_args["database"] = cfg_value ("database")
      have_config_args = True
    if cfg_value ("user"):
      connection_args["user"] = cfg_value ("user")
      have_config_args = True
    if cfg_value ("password"):
      connection_args["password"] = cfg_value ("password")
      have_config_args = True
    if cfg_value ("host"):
      connection_args["host"] = cfg_value ("host")
      have_config_args = True
    if cfg_value ("port"):
      connection_args["port"] = cfg_value ("port")
      have_config_args = True
    if not have_config_args:
      raise BFSyncError ("sql-export: no commandline arguments given and no sql-export config values found")

  conn = dbapi2.connect (**connection_args)
  cur = conn.cursor ()

  if WITH_SQL:
    # check if sql export version uses the same database layout that this exporter uses
    cur.execute ("""
      CREATE TABLE IF NOT EXISTS sql_export_version (
        version   integer
      )""")
    db_sql_export_version = 0
    my_sql_export_version = 2     # bump this version if db layout changes
    reset = False

    if WITH_SQL:
      cur.execute ("SELECT (version) FROM sql_export_version;")
      while True:
        row = cur.fetchone()
        if not row:
          break
        version = row[0]
        # should only have one version entry, and version should be > 0
        assert (version > 0)
        assert (db_sql_export_version == 0)
        db_sql_export_version = version
    if db_sql_export_version != my_sql_export_version:
      reset = True
      if db_sql_export_version > 0:
        print "sql-export: Database generated by old sql-export version (%d)." % db_sql_export_version
        print "sql-export: Running full export with new sql-export version (%d)." % my_sql_export_version
      cur.execute ("DELETE FROM sql_export_version")
      cur.execute ("INSERT INTO sql_export_version (version) VALUES (%s)", (my_sql_export_version,))

    if (parsed_args.initial or reset):
      cur.execute ("""
        DROP TABLE IF EXISTS history;
        CREATE TABLE history (
          repo_id   varchar,
          version   integer,
          hash      varchar,
          author    varchar,
          message   varchar,
          time      bigint
        );

        DROP TABLE IF EXISTS tags;
        CREATE TABLE tags (
          repo_id   varchar,
          version   integer,
          tag       varchar,
          value     varchar
        );

        DROP TABLE IF EXISTS files;
        CREATE TABLE files (
          repo_id   varchar,
          pathname  varchar,
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
          mtime_ns  integer
        );

        DROP TABLE IF EXISTS temp_delete;
        CREATE TABLE temp_delete (
          pathname  varchar,
          filename  varchar
        );

        DROP INDEX IF EXISTS files_fn_idx;
        CREATE INDEX files_fn_idx ON files (repo_id, pathname, filename, vmin);

        DROP INDEX IF EXISTS temp_delete_fn_idx;
        CREATE INDEX temp_delete_fn_idx ON temp_delete (pathname, filename);
        """)
      conn.commit()
      print "Reset: reinitialized all database tables."

  sql_export = bfsyncdb.SQLExport (repo.bdb)

  # compute max version that was already imported earlier
  sql_max_version = 0

  if WITH_SQL:
    cur.execute ("SELECT version, hash, author, message, time FROM history WHERE repo_id = %s;", (sql_export.repo_id(), ))
    while True:
      row = cur.fetchone()
      if not row:
        break
      version = row[0]
      hentry = repo.bdb.load_history_entry (version)
      if not hentry.valid or hentry.hash != row[1] or hentry.author != row[2] or hentry.message != row[3] or hentry.time != row[4]:
        raise BFSyncError ("export failed: history entry %d of sql table doesn't match repo history" % version)
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

  need_clean_status = False

  for version in range (sql_max_version + 1, bdb_max_version + 1):
    sql_export.export_version (version, bdb_max_version, insert_filename, delete_filename)
    need_clean_status = True

    if WITH_SQL:
      sql_export.update_status ("updating", True)

      sql_start_time = time.time()
      cur.execute ("DELETE FROM temp_delete")

      delete_file = open (delete_filename, "r")
      cur.copy_from (delete_file, "temp_delete")
      delete_file.close()

      cur.execute ("""UPDATE files SET vmax = %s FROM temp_delete WHERE files.repo_id = %s AND
                                                                        files.pathname = temp_delete.pathname AND
                                                                        files.filename = temp_delete.filename AND vmax = %s""",
                   (version - 1, sql_export.repo_id(), bfsyncdb.VERSION_INF))
      cur.execute ("DELETE FROM temp_delete")

      insert_file = open (insert_filename, "r")
      cur.copy_from (insert_file, "files")
      insert_file.close()

      # import history entry

      hentry = repo.bdb.load_history_entry (version)
      fields = ( sql_export.repo_id(), hentry.version, hentry.hash, hentry.author, hentry.message, hentry.time )
      if WITH_SQL:
        cur.execute ("INSERT INTO history (repo_id, version, hash, author, message, time) VALUES (%s, %s, %s, %s, %s, %s)", fields)

      conn.commit()
      sql_end_time = time.time()

      # print "### sql time: %.2f" % (sql_end_time - sql_start_time)
      # sys.stdout.flush()

  # import tags
  cur.execute ("DELETE FROM tags WHERE repo_id = %s", (sql_export.repo_id(), ))
  for version in range (1, bdb_max_version + 1):
    tags = repo.bdb.list_tags (version)
    for t in tags:
      values = repo.bdb.load_tag (version, t)
      for v in values:
        fields = (sql_export.repo_id(), version, t, v)
        cur.execute ("INSERT INTO tags (repo_id, version, tag, value) VALUES (%s, %s, %s, %s)", fields)
  conn.commit()

  if need_clean_status:
    sql_export.update_status ("done.    ", True)
    print

  cur.close()
  conn.close()
