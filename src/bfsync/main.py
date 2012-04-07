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
from commitutils import commit, revert, gen_status
from remoteutils import *
from TransferList import TransferList, TransferFile
from StatusLine import status_line, OutputSubsampler
from HashCache import hash_cache
from ServerConn import ServerConn
from RemoteRepo import RemoteRepo
from stat import *
from transferutils import get, put, push, pull, collect
from xzutils import xz
from journal import run_commands, run_continue, init_journal
from gcutils import gc

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
  commit (repo, commit_args = commit_args)
  run_commands (repo)

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

  ids = []
  ai = bfsyncdb.AllINodesIterator (repo.bdb)
  while True:
    id = ai.get_next()
    if not id.valid:
      break
    ids.append (id.str())
  del ai

  fail = False
  inode_d = dict()
  conflicts = []
  VERSION = repo.first_unused_version()

  ## check inode records
  for id_str in ids:
    id = bfsyncdb.ID (id_str)
    if not id.valid:
      raise Exception ("found invalid id during debug integrity")

    inodes = repo.bdb.load_all_inodes (id)
    for inode in inodes:
      version = inode.vmin
      while version <= inode.vmax and version <= VERSION:
        s = "%d|%s" % (version, id_str)
        if inode_d.has_key (s):
          conflicts += [ (version, id_str) ]
        inode_d[s] = 1
        version += 1

  for conflict in conflicts:
    version = conflict[0]
    id = conflict[1]
    print "error: version %d available more than once for inode %s" % (version, id)
    fail = True

  ## check link records
  link_d = dict()
  conflicts = []
  for id_str in ids:
    id = bfsyncdb.ID (id_str)
    if not id.valid:
      raise Exception ("found invalid id during debug integrity")
    links = repo.bdb.load_all_links (id)
    for link in links:
      version = link.vmin
      while version <= link.vmax and version <= VERSION:
        s = "%d|%s|%s" % (version, id_str, link.name)
        if link_d.has_key (s):
          conflicts += [ (version, id_str, link.name) ]
        link_d[s] = 1
        version += 1

  for conflict in conflicts:
    version = conflict[0]
    id = conflict[1]
    name = conflict[2]
    print "error: version %d available more than once for link %s->%s" % (version, id, name)
    fail = True

  if fail:
    sys.exit (1)
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

def cmd_debug_inode_name():
  repo = cd_repo_connect_db()
  inode_id = args[0]
  version = int (args[1])
  print repo.printable_name (inode_id, version)

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

  # lock if server is running: this causes bfsyncfs cache to be written
  try:
    server_conn = ServerConn (repo.path)
    server_conn.get_lock()
  except IOError, e:
    pass       # no server => no lock

  status = gen_status (repo)

  print "=" * 80
  for s in status:
    print s
  print "=" * 80

def cmd_revert():
  repo = cd_repo_connect_db()
  if len (args) == 0:
    revert (repo, -1)
  else:
    revert (repo, int (args[0]))
  run_commands (repo)

def cmd_remove():
  bdb_dir = os.path.join (find_repo_dir(), "bdb")
  bfsyncdb.remove_db (bdb_dir)

def cmd_db_fingerprint():
  repo = cd_repo_connect_db()
  repo_path = repo.path

  # lock repo to ensure changes are written before we do something
  server_conn = ServerConn (repo_path)
  server_conn.get_lock()

  ai = bfsyncdb.AllINodesIterator (repo.bdb)
  inode_l = []
  link_l = []
  while True:
    id = ai.get_next()
    if not id.valid:
      break
    inodes = repo.bdb.load_all_inodes (id)
    for inode in inodes:
      s = "\0".join ([
        "i",
        "%d" % inode.vmin,
        "%d" % inode.vmax,
        inode.id.str(),
        "%d" % inode.uid,
        "%d" % inode.gid,
        "%o" % inode.mode,
        "%d" % inode.type,
        inode.hash,
        inode.link,
        "%d" % inode.size,
        "%d" % inode.major,
        "%d" % inode.minor,
        "%d" % inode.nlink,
        "%d" % inode.ctime,
        "%d" % inode.ctime_ns,
        "%d" % inode.mtime,
        "%d" % inode.mtime_ns,
        "%d" % inode.new_file_number
      ])
      inode_l.append (s)
    links = repo.bdb.load_all_links (id)
    for link in links:
      s = "\0".join ([
        "l",
        "%d" % link.vmin,
        "%d" % link.vmax,
        link.dir_id.str(),
        link.inode_id.str(),
        link.name
      ])
      link_l.append (s)
  del ai

  history_l = []
  VERSION = 1
  while True:
    hentry = repo.bdb.load_history_entry (VERSION)
    VERSION += 1

    if not hentry.valid:
      break

    s = "\0".join ([
      "h",
      "%d" % hentry.version,
      hentry.hash,
      hentry.author,
      hentry.message,
      "%d" % hentry.time
    ])
    history_l.append (s)

  inode_l.sort()
  link_l.sort()
  history_l.sort()

  all_str = ""
  for r in inode_l + link_l + history_l:
    all_str += r + "\0"
  print hashlib.sha1 (all_str).hexdigest()

def cmd_remote():
  os.chdir (args[0])
  repo = cd_repo_connect_db()
  repo_path = repo.path

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
  run_commands (repo)

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
  xz ("init-diff")
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

def check_objs (repo, hobjs, iobjs):
  all_objs = len (hobjs) + len (iobjs)

  check_objs = 0
  need_hobjs = 0
  need_iobjs = 0

  outss = OutputSubsampler()
  def update_status():
    status_line.update ("checking object %d/%d" % (check_objs, all_objs))

  for hash in hobjs:
    check_objs += 1
    if not repo.validate_object (hash):
      need_hobjs += 1
    if outss.need_update():
      update_status()

  for hash in iobjs:
    check_objs += 1
    if not repo.validate_object (hash):
      need_iobjs += 1
    if outss.need_update():
      update_status()

  update_status()
  status_line.cleanup()

  print
  for x in [("History", need_hobjs, len (hobjs)),
            ("Files", need_iobjs, len (iobjs))]:
    if x[1] != 0:
      print "%s:\n - need to transfer %d/%d objects." % (x[0], x[1], x[2])
    else:
      print "%s:\n - all %d objects available." % (x[0], x[2])

def cmd_check():
  repo = cd_repo_connect_db()

  status_line.set_op ("CHECK")

  # create list of objects required by history
  history_objs_set = set()
  VERSION = 1
  while True:
    hentry = repo.bdb.load_history_entry (VERSION)
    VERSION += 1
    if not hentry.valid:
      break
    history_objs_set.add (hentry.hash)
  history_objs = list (history_objs_set)

  # create list of hashes required by inodes
  inode_objs = []
  hi = bfsyncdb.INodeHashIterator (repo.bdb)
  while True:
    hash = hi.get_next()
    if hash == "":
      break           # done
    inode_objs.append (hash)
  del hi # free locks iterator may have held

  check_objs (repo, history_objs, inode_objs)

def cmd_gc():
  repo = cd_repo_connect_db()

  status_line.set_op ("GC")
  gc (repo)

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
  os.mkdir ("objects", 0700)
  os.mkdir ("processes", 0700)

  repo = cd_repo_connect_db()
  repo_path = repo.path

  # pull changes from master
  pull (repo, [ url ], server = False)
  run_commands (repo)

def cmd_repo_files():
  repo = connect_db (os.getcwd())

  parser = argparse.ArgumentParser (prog='bfsync repo-files')
  parser.add_argument ("-0", "--null",
                  action="store_true", dest="null", default=False)
  parser.add_argument ('dirs', nargs = '+')
  repo_files_args = parser.parse_args (args)

  dir_list = repo_files_args.dirs
  null = repo_files_args.null

  # determine current db version
  VERSION = repo.first_unused_version()

  # create set with hashes contained in the current version
  hash_set = set()
  def update_hash_set (inode):
    if len (inode.hash) == 40:
      hash_set.add (inode.hash)

  repo.foreach_inode_link (VERSION, update_hash_set, None)

  # search files in target dir which are available in current version
  for dir in dir_list:
    for walk_dir, walk_dirs, walk_files in os.walk (dir):
      for f in walk_files:
        full_name = os.path.join (walk_dir, f)
        if os.path.isfile (full_name) and not os.path.islink (full_name): # ignore symlinks
          try:
            hash = hash_cache.compute_hash (full_name)
            if hash in hash_set:
              if null:
                sys.stdout.write (full_name + '\0')
              else:
                print full_name
          except IOError, e:
            pass # ignore files that cannot be read due to permissions

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
  parser = argparse.ArgumentParser (prog='bfsync continue')
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
  run_continue (repo, je)

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
      ( "remove",                 cmd_remove, 0),
      ( "remote",                 cmd_remote, 1),
      ( "recover",                cmd_recover, 1),
      ( "debug-load-all-inodes",  cmd_debug_load_all_inodes, 0),
      ( "debug-perf-getattr",     cmd_debug_perf_getattr, 1),
      ( "debug-clear-cache",      cmd_debug_clear_cache, 1),
      ( "debug-integrity",        cmd_debug_integrity, 0),
      ( "debug-get-prof",         cmd_debug_get_prof, 0),
      ( "debug-reset-prof",       cmd_debug_reset_prof, 0),
      ( "debug-inode-name",       cmd_debug_inode_name, 1),
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
        init_journal (" ".join ([ "bfsync" ] + sys.argv[1:]))
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
