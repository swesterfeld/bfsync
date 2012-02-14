#!/usr/bin/env python

# bfsync: Big File synchronization tool

# Copyright (C) 2011 Stefan Westerfeld
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

import sys
import os
import subprocess
import hashlib
import cPickle
import traceback
import time
import tempfile
import bfsync.CfgParser
import HashCache
import StatusLine
import shutil
import argparse
import shutil
import datetime
import random

from utils import *
from diffutils import diff
from commitutils import commit, revert, gen_status, new_commit, new_commit_continue
from remoteutils import *
from TransferList import TransferList, TransferFile
from StatusLine import status_line
from HashCache import hash_cache
from ServerConn import ServerConn
from RemoteRepo import RemoteRepo
from stat import *
from transferutils import get, put, push, pull, collect

def find_bfsync_dir():
  old_cwd = os.getcwd()
  dir = old_cwd
  while True:
    try:
      test_dir = os.path.join (dir, ".bfsync")
      os.chdir (test_dir)
      os.chdir (old_cwd)
      return test_dir
    except:
      pass
    # try parent directory
    newdir = os.path.dirname (dir)
    if newdir == dir:
      # no more parent
      raise Exception ("can not find .bfsync directory")
    dir = newdir

def cmd_commit():
  parser = argparse.ArgumentParser (prog='bfsync commit')
  parser.add_argument ('-m', help='set commit message')
  parser.add_argument ('-a', help='set author')
  parsed_args = parser.parse_args (args)

  commit_args = dict()
  if parsed_args.m is not None:
    commit_args["message"] = parsed_args.m
  if parsed_args.a is not None:
    commit_args["author"] = parsed_args.a

  repo = cd_repo_connect_db()
  status_line.set_op ("COMMIT")
  new_commit (repo, commit_args = commit_args)

def cmd_debug_load_all_inodes():
  bfsync_dir = find_bfsync_dir()

  bfsync_info = parse_config (bfsync_dir + "/info")

  repo_path = bfsync_info.get ("repo-path")
  if len (repo_path) != 1:
    raise Exception ("bad repo path")
  repo_path = repo_path[0]

  os.chdir (repo_path)
  server_conn = ServerConn (repo_path)
  print server_conn.process_call (["load-all-inodes"])[0]

def cmd_debug_perf_getattr():
  bfsync_dir = find_bfsync_dir()

  bfsync_info = parse_config (bfsync_dir + "/info")

  repo_path = bfsync_info.get ("repo-path")
  if len (repo_path) != 1:
    raise Exception ("bad repo path")
  repo_path = repo_path[0]

  os.chdir (repo_path)
  server_conn = ServerConn (repo_path)
  print server_conn.process_call (["perf-getattr", args[0], args[1]])[0]

def cmd_debug_get_prof():
  bfsync_dir = find_bfsync_dir()

  bfsync_info = parse_config (bfsync_dir + "/info")

  repo_path = bfsync_info.get ("repo-path")
  if len (repo_path) != 1:
    raise Exception ("bad repo path")
  repo_path = repo_path[0]

  os.chdir (repo_path)
  server_conn = ServerConn (repo_path)
  print server_conn.process_call (["get-prof"])[0]

def cmd_debug_reset_prof():
  bfsync_dir = find_bfsync_dir()

  bfsync_info = parse_config (bfsync_dir + "/info")

  repo_path = bfsync_info.get ("repo-path")
  if len (repo_path) != 1:
    raise Exception ("bad repo path")
  repo_path = repo_path[0]

  os.chdir (repo_path)
  server_conn = ServerConn (repo_path)
  print server_conn.process_call (["reset-prof"])[0]

def cmd_debug_clear_cache():
  bfsync_dir = find_bfsync_dir()

  bfsync_info = parse_config (bfsync_dir + "/info")

  repo_path = bfsync_info.get ("repo-path")
  if len (repo_path) != 1:
    raise Exception ("bad repo path")
  repo_path = repo_path[0]

  os.chdir (repo_path)
  server_conn = ServerConn (repo_path)
  server_conn.get_lock()
  server_conn.clear_cache()

def cmd_debug_integrity():
  repo = cd_repo_connect_db()
  conn = repo.conn
  repo_path = repo.path
  sys.stderr.write ("FIXME: fake debug integrity")
  print "ok"
  return

  c = conn.cursor()
  c.execute ('''SELECT vmin, vmax,id FROM inodes''')
  fail = False
  inode_d = dict()
  conflicts = []
  for row in c:
    vmin = int (row[0])
    vmax = int (row[1])
    version = vmin
    while version <= vmax:
      s = "%d|%s" % (version, row[2])
      if inode_d.has_key (s):
        conflicts += [ (version, row[2]) ]
      inode_d[s] = 1
      version += 1

  for conflict in conflicts:
    version = conflict[0]
    id = conflict[1]
    print "error: version %d available more than once for inode %s, name %s" % (
           version, id, printable_name (c, id, version))
    fail = True

  c.execute ('''SELECT vmin, vmax, dir_id, name FROM links''')
  link_d = dict()
  conflicts = []
  for row in c:
    vmin = int (row[0])
    vmax = int (row[1])
    version = vmin
    while version <= vmax:
      s = "%d|%s|%s" % (version, row[2], row[3])
      if link_d.has_key (s):
        conflicts += [ (version, row[2], row[3]) ]
      link_d[s] = 1
      version += 1

  for conflict in conflicts:
    version = conflict[0]
    id = conflict[1]
    name = conflict[2]
    print "error: version %d available more than once for link %s->%s, name %s" % (
          version, id, name, os.path.join (printable_name (c, id, version), name))
    fail = True

  c.execute ('''SELECT vmin, vmax, id, nlink FROM inodes''')
  check_links = []
  for row in c:
    vmin = int (row[0])
    vmax = int (row[1])
    version = vmin
    while version <= vmax:
      check_links += [ (version, row[2], row[3]) ]
      version += 1
  for version, id, nlink in check_links:
    for row in c.execute ('''SELECT COUNT (*) FROM links WHERE inode_id = ? AND ? >= vmin AND ? <= vmax''',
                         (id, version, version)):
      have_nlink = row[0]
    if id == "0"*40:
      have_nlink += 1
    if nlink != have_nlink:
      print "error: nlink field for inode id %s is %d, should be %d" % (id, nlink, have_nlink)
      fail = True

  c.close()
  if fail:
    sys.exit (1)
  print "ok"
  return

def cmd_log():
  repo = cd_repo_connect_db()
  repo_path = repo.path

  VERSION = 1
  while True:
    hentry = repo.bdb.load_history_entry (VERSION)
    VERSION += 1

    if not hentry.valid:
      break

    version = hentry.version
    hash    = hentry.hash
    author  = hentry.author
    msg     = hentry.message
    time    = hentry.time

    msg = msg.strip()
    print "-" * 80
    print "%4d   Hash   %s" % (version, hash)
    print "       Author %s" % author
    print "       Date   %s" % datetime.datetime.fromtimestamp (time).strftime ("%F %H:%M:%S")
    print
    for line in msg.split ("\n"):
      print "       %s" % line
  print "-" * 80

def cmd_status():
  repo = cd_repo_connect_db()
  status = gen_status (repo)

  print "=" * 80
  for s in status:
    print s
  print "=" * 80

def cmd_revert():
  print "FIXME: revert"
  return
  repo = cd_repo_connect_db()
  if len (args) == 0:
    revert (repo, -1)
  else:
    revert (repo, int (args[0]))

def cmd_db_fingerprint():
  repo = cd_repo_connect_db()
  conn = repo.conn
  repo_path = repo.path
  c = conn.cursor()

  sys.stderr.write ("FIXME: db fingerprint")
  print "foo"
  return

  # lock repo to ensure changes are written before we do something
  server_conn = ServerConn (repo_path)
  server_conn.get_lock()

  c.execute ("SELECT * FROM inodes")
  inode_l = []
  for row in c:
    s = "i\0"
    for f in row:
      s += "%s\0" % f
    inode_l += [ s ]
  inode_l.sort()
  c.execute ("SELECT * FROM links")
  link_l = []
  for row in c:
    s = "l\0"
    for f in row:
      s += "%s\0" % f
    link_l += [ s ]
  link_l.sort()
  c.execute ("SELECT * FROM history")
  history_l = []
  for row in c:
    s = "h\0"
    for f in row:
      s += "%s\0" % f
    history_l += [ s ]
  history_l.sort()
  all_str = ""
  for r in link_l + inode_l + history_l:
    all_str += r + "\0"
  print hashlib.sha1 (all_str).hexdigest()

def cmd_remote():
  os.chdir (args[0])
  repo = cd_repo_connect_db()
  conn = repo.conn
  repo_path = repo.path
  c = conn.cursor()

  while True:
    command = sys.stdin.readline().strip()
    result = None
    if command == "history":
      result = cPickle.dumps (remote_history (repo))
    elif command == "ls":
      hashes_len = int (sys.stdin.readline())
      hashes = cPickle.loads (sys.stdin.read (hashes_len))
      result = cPickle.dumps (remote_ls (repo, hashes))
    elif command == "send":
      params_len = int (sys.stdin.readline())
      params = cPickle.loads (sys.stdin.read (params_len))
      remote_send (repo, params)
    elif command == "receive":
      remote_receive (repo)
    elif command == "update-history":
      dhlen = int (sys.stdin.readline())
      delta_history = cPickle.loads (sys.stdin.read (dhlen))

      result = remote_update_history (repo, delta_history)
    elif command == "need-objects":
      table_len = int (sys.stdin.readline())
      table = cPickle.loads (sys.stdin.read (table_len))
      result = cPickle.dumps (remote_need_objects (repo, table))
    elif command == "":
      return
    else:
      raise Exception ("unknown command in cmd_remote(): '%s'" % command)
    if not result is None:
      sys.stdout.write ("%s\n%s" % (len (result), result))
      sys.stdout.flush()

def cmd_pull():
  repo = cd_repo_connect_db ()
  status_line.set_op ("PULL")
  pull (repo, args)

def cmd_push():
  repo = cd_repo_connect_db()
  status_line.set_op ("PUSH")
  push (repo, args)

def cmd_get():
  repo = cd_repo_connect_db()

  status_line.set_op ("GET")
  get (repo, args)

def cmd_put():
  repo = cd_repo_connect_db()

  status_line.set_op ("PUT")
  put (repo, args)

def gen_repo_id():
  l = []
  for i in range (5):
    l.append ("%08x" % random.randint (0, 2**32 - 1))
  return "-".join (l)

def cmd_init():
  dir = args[0]
  try:
    os.mkdir (dir, 0700)
  except:
    raise Exception ("can't create directory %s for repository" % dir)

  init_bfsync_file (dir)
  os.chdir (dir)

  # info file
  f = open ("info", "w")
  f.write ("repo-type master;\n")
  f.close()

  # config file
  f = open ("config", "w")
  f.write ("# Repository ID (auto-generated, do not edit)\n");
  f.write ("""repo-id "%s";\n\n""" % gen_repo_id());
  f.write ("cache-size 16;\n")
  f.close()

  # create objects directory
  os.mkdir ("objects", 0700)

  # create tmp dir
  os.mkdir ("tmp", 0700)

  # create bdb dir
  os.mkdir ("bdb", 0700)

  # create process registration dir
  os.mkdir ("processes", 0700)

  # create history table
  repo = cd_repo_connect_db()

  # create initial commit diff
  time_now = int (time.time())
  change_list = [
    "i+",
    "/" + "0" * 40,     # id (root inode)
    "%d" % os.getuid(), # uid
    "%d" % os.getgid(), # gid
    "%d" % 0755,        # mode
    "3",                # type
    "", "", "0", "0", "0", "1",
    "%d" % time_now, "0", # ctime
    "%d" % time_now, "0"  # mtime
  ]

  f = open ("init-diff", "w")
  for s in change_list:
    f.write (s + "\0")
  f.close()
  os.system ("xz -9 init-diff")
  hash = move_file_to_objects (repo, "init-diff.xz")

  # create initial history entry
  repo.bdb.begin_transaction()
  repo.bdb.store_history_entry (1, hash, "no author", "initial commit", time_now)
  repo.bdb.commit_transaction()

def guess_dir_name (url):
  dir_name = ""
  for ch in reversed (url):
    if (ch == ":") or (ch == "/"):
      return dir_name
    dir_name = ch + dir_name
  return url

def table_objs (repo, table):
  thashes = []
  conn = repo.conn
  c = conn.cursor()

  objs = 0
  need_objs = 0
  c.execute ("""SELECT DISTINCT hash FROM %s""" % table)
  for row in c:
    hash = row[0]
    if len (hash) == 40:
      thashes.append (hash)

  return thashes

def check_objs (repo, hobjs, iobjs):
  all_objs = len (hobjs) + len (iobjs)

  check_objs = 0
  need_hobjs = 0
  need_iobjs = 0

  for hash in hobjs:
    dest_file = os.path.join (repo.path, "objects", make_object_filename (hash))
    check_objs += 1
    if not validate_object (dest_file, hash):
      need_hobjs += 1
    status_line.update ("checking object %d/%d" % (check_objs, all_objs))

  for hash in iobjs:
    dest_file = os.path.join (repo.path, "objects", make_object_filename (hash))
    check_objs += 1
    if not validate_object (dest_file, hash):
      need_iobjs += 1
    status_line.update ("checking object %d/%d" % (check_objs, all_objs))

  status_line.cleanup()

  print
  for x in [("History", need_hobjs, len (hobjs)),
            ("Files", need_iobjs, len (iobjs))]:
    if x[1] != 0:
      print "%s:\n - need to transfer %d/%d objects." % (x[0], x[1], x[2])
    else:
      print "%s:\n - all %d objects available." % (x[0], x[2])

def unused_objects (repo):
  result = []

  # check objects required by db
  used_set = set()
  for table in [ "history", "inodes" ]:
    for row in repo.conn.execute ("""SELECT DISTINCT hash FROM %s""" % table):
      hash = row[0]
      if len (hash) == 40:
        used_set.add (hash)

  for root, dirs, files in os.walk (os.path.join (repo.path, "objects")):
    for f in files:
      hash = os.path.basename (root) + f
      if hash not in used_set:
        result.append (hash)
  return result

def cmd_check():
  repo = cd_repo_connect_db()

  status_line.set_op ("CHECK")
  history_objs = table_objs (repo, "history")
  inode_objs = table_objs (repo, "inodes")
  check_objs (repo, history_objs, inode_objs)
  print "Unused:"
  print " - %d objects are not used." % len (unused_objects (repo))

def cmd_gc():
  repo = cd_repo_connect_db()

  status_line.set_op ("GC")
  objs = unused_objects (repo)
  lobjs = len (objs)
  pos = 1
  for o in objs:
    status_line.update ("removing file %d/%d" % (pos, lobjs))
    pos += 1
    os.remove (os.path.join (repo.path, "objects", make_object_filename (o)))

def cmd_clone():
  parser = argparse.ArgumentParser (prog='bfsync clone')
  parser.add_argument ("-u", action="store_true", dest="use_uid_gid", default=False)
  parser.add_argument ('-c', help='set cache size')
  parser.add_argument ("repo")
  parser.add_argument ("dest_dir", nargs = "?")
  parsed_args = parser.parse_args (args)

  url = parsed_args.repo
  if parsed_args.dest_dir:
    dir = parsed_args.dest_dir
  else:
    dir = guess_dir_name (args[0])
  if os.path.exists (dir):
    print "fatal: destination path '" + dir + "' already exists"
    sys.exit (1)

  cache_size = 16
  if parsed_args.c:
    cache_size = int (parsed_args.c)

  status_line.set_op ("CLONE")

  url_list = url.split (":")
  if len (url_list) == 1:
    # local repository => use absolute path
    url = os.path.abspath (url)

  print "Cloning into %s..." % dir

  try:
    os.mkdir (dir, 0700)
  except:
    raise Exception ("can't create directory %s for repository" % dir)

  init_bfsync_file (dir)

  # init info
  f = open (os.path.join (dir, "info"), "w")
  f.write ("repo-type store;\n")
  f.close()

  # default config
  f = open (os.path.join (dir, "config"), "w")

  f.write ("# Repository ID (auto-generated, do not edit)\n");
  f.write ("""repo-id "%s";\n\n""" % gen_repo_id());

  ## use-uid-gid
  if parsed_args.use_uid_gid:
    f.write ("use-uid-gid 1;\n")
  else:
    f.write ("use-uid-gid 0;\n")

  ## cache size
  print " - cache size: %d Mb" % cache_size
  f.write ("cache-size %d;\n" % cache_size)

  ## default push/pull
  f.write ("default {\n")
  f.write ("""  pull "%s";\n""" % url)
  f.write ("""  push "%s";\n""" % url)
  f.write ("}\n")
  f.close()

  os.chdir (dir)
  os.mkdir ("bdb", 0700)
  os.mkdir ("tmp", 0700)
  os.mkdir ("new", 0700)
  os.mkdir ("objects", 0700)
  os.mkdir ("processes", 0700)

  repo = cd_repo_connect_db()
  repo_path = repo.path

  # pull changes from master
  pull (repo, [ url ], server = False)

def cmd_repo_files():
  repo = connect_db (os.getcwd())

  parser = argparse.ArgumentParser (prog='bfsync repo-files')
  parser.add_argument ("-0", "--null",
                  action="store_true", dest="null", default=False)
  parser.add_argument ('dirs', nargs = '+')
  repo_files_args = parser.parse_args (args)

  dir_list = repo_files_args.dirs
  null = repo_files_args.null

  c = repo.conn.cursor()

  # determine current db version
  VERSION = 1
  c.execute ('''SELECT version FROM history''')
  for row in c:
    VERSION = max (row[0], VERSION)

  hash_dict = dict()
  c.execute ("SELECT hash FROM inodes WHERE ? >= vmin AND ? <= vmax", (VERSION, VERSION))
  for row in c:
    if len (row[0]) == 40:
      hash_dict[row[0]] = True

  for dir in dir_list:
    for walk_dir, walk_dirs, walk_files in os.walk (dir):
      for f in walk_files:
        full_name = os.path.join (walk_dir, f)
        if os.path.isfile (full_name):        # ignore symlinks
          hash = hash_cache.compute_hash (full_name)
          if hash_dict.has_key (hash):
            if null:
              sys.stdout.write (full_name + '\0')
            else:
              print full_name

def cmd_collect():
  old_cwd = os.getcwd()
  repo = cd_repo_connect_db()
  collect (repo, args, old_cwd)

def cmd_recover():
  parser = argparse.ArgumentParser (prog='bfsync recover')
  parser.add_argument ("dest_dir", nargs = "?")
  parsed_args = parser.parse_args (args)

  if parsed_args.dest_dir:
    dir = parsed_args.dest_dir
    try:
      os.chdir (dir)
    except:
      print "fatal: path '" + dir + "' does not exist"
      sys.exit (1)

  repo_path = find_repo_dir()
  bfsync_info = parse_config (repo_path + "/config")

  cache_size = bfsync_info.get ("cache-size")
  if len (cache_size) != 1:
    raise Exception ("bad cache-size setting")
  cache_size = int (cache_size[0])

  pid_files = os.listdir (repo_path + "/processes")
  alive = 0
  del_after_recover = []
  for pid in pid_files:
    try:
      ipid = int (pid)
      os.kill (ipid, 0)
      print " - process %d is still running" % ipid
      alive += 1
    except:
      del_after_recover.append (os.path.join (repo_path, "processes", pid))
  if alive > 0:
    print "can't recover, %d processes are still running <=> terminate them to recover" % alive
    sys.exit (1)

  status_line.set_op ("RECOVER")
  status_line.update ("running recover on %s" % repo_path)
  bdb = bfsyncdb.open_db (repo_path, cache_size, True)
  status_line.update ("running recover on %s: done" % repo_path)
  for filename in del_after_recover:
    os.remove (filename)

def cmd_continue():
  parser = argparse.ArgumentParser (prog='bfsync recover')
  parser.add_argument ("dest_dir", nargs = "?")
  parsed_args = parser.parse_args (args)

  if parsed_args.dest_dir:
    dir = parsed_args.dest_dir
    try:
      os.chdir (dir)
    except:
      print "fatal: path '" + dir + "' does not exist"
      sys.exit (1)

  repo = cd_repo_connect_db (True)
  journal = repo.bdb.load_journal_entries()
  if len (journal) == 0:
    raise BFSyncError ("journal is empty, cannot continue")

  if len (journal) != 1:
    raise BFSyncError ("journal contains more than one entry, cannot continue")

  je = journal[0]

  print "Journal:"
  print "  -> operation %s, state %s" % (je.operation, je.state)
  if je.operation == "commit":
    new_commit_continue (repo, cPickle.loads (je.state))

args = []

def main():
  command_func = None
  command = None
  global args

  arg_iter = sys.argv[1:].__iter__()
  for arg in arg_iter:
    commands = [
      ( "check",                  cmd_check, 1),
      ( "collect",                cmd_collect, 1),
      ( "commit",                 cmd_commit, 1),
      ( "continue",               cmd_continue, 1),
      ( "log",                    cmd_log, 0),
      ( "pull",                   cmd_pull, 1),
      ( "push",                   cmd_push, 1),
      ( "gc",                     cmd_gc, 0),
      ( "get",                    cmd_get, 1),
      ( "put",                    cmd_put, 1),
      ( "status",                 cmd_status, 0),
      ( "revert",                 cmd_revert, 1),
      ( "init",                   cmd_init, 1),
      ( "clone",                  cmd_clone, 1),
      ( "repo-files",             cmd_repo_files, 1),
      ( "db-fingerprint",         cmd_db_fingerprint, 0),
      ( "remote",                 cmd_remote, 1),
      ( "recover",                cmd_recover, 1),
      ( "debug-load-all-inodes",  cmd_debug_load_all_inodes, 0),
      ( "debug-perf-getattr",     cmd_debug_perf_getattr, 1),
      ( "debug-clear-cache",      cmd_debug_clear_cache, 1),
      ( "debug-integrity",        cmd_debug_integrity, 0),
      ( "debug-get-prof",         cmd_debug_get_prof, 0),
      ( "debug-reset-prof",       cmd_debug_reset_prof, 0),
    ]
    parse_ok = False
    if command == None:
      for c in commands:
        if c[0] == arg:
          command_func = c[1]
          command_args = c[2]
          command = c[0]
          parse_ok = True
    else:
      if command_args > 0:
        args += [ arg ]
        parse_ok = True
    if not parse_ok:
      sys.stderr.write ("can't parse command line args...\n")
      sys.exit (1)

  if command_func != None:
    try:
      if False: # profiling
        import cProfile

        cProfile.run ("command_func()", "/tmp/bfsync-profile-%s" % command)
      else:
        command_func()
    except BFSyncError, err:
      hash_cache.save()
      sys.stderr.write ("bfsync: %s\n" % err)
      sys.exit (1)
    except Exception, ex:
      sys.stderr.write ("\n\n\n")
      sys.stderr.write ("==================================================\n")
      traceback.print_exc()
      sys.stderr.write ("==================================================\n")
      sys.stderr.write ("\n\n\n")
      hash_cache.save()
      sys.stderr.write ("bfsync: %s\n" % ex)
      sys.exit (1)
    hash_cache.save()
  else:
    print "usage: bfsync <command> [ args... ]"
