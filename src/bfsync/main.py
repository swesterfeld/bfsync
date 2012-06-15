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
import re
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
from expire import expire
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
from transferbench import transfer_bench

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
  parser = argparse.ArgumentParser (prog='bfsync log')
  parser.add_argument ("-a", action="store_true", dest="all", default=False)
  parsed_args = parser.parse_args (args)

  repo = cd_repo_connect_db()
  repo_path = repo.path

  deleted_versions = repo.get_deleted_version_set()

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

    tag_list = []
    tags = repo.bdb.list_tags (hentry.version)
    for t in tags:
      values = repo.bdb.load_tag (hentry.version, t)
      for v in values:
        tag_list.append ("%s=%s" % (t, v))

    if version not in deleted_versions or parsed_args.all:
      msg = msg.strip()
      print "-" * 80
      print "%4d   Hash   %s" % (version, hash)
      print "       Author %s" % author
      print "       Date   %s" % datetime.datetime.fromtimestamp (time).strftime ("%A, %F %H:%M:%S")
      if tag_list:
        print "       Tags   %s" % ",".join (tag_list)
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
    elif command == "version":
      result = cPickle.dumps (bfsyncdb.repo_version())
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
  parser = argparse.ArgumentParser (prog='bfsync pull')
  parser.add_argument ('--rsh', help='set remote shell')
  parser.add_argument ("url", nargs = "*")
  parsed_args = parser.parse_args (args)

  if parsed_args.rsh is not None:
    rsh = parsed_args.rsh
  else:
    rsh = "ssh"

  repo = cd_repo_connect_db ()
  status_line.set_op ("PULL")
  pull (repo, parsed_args.url, rsh)
  run_commands (repo)

def cmd_push():
  parser = argparse.ArgumentParser (prog='bfsync push')
  parser.add_argument ('--rsh', help='set remote shell')
  parser.add_argument ("url", nargs = "*")
  parsed_args = parser.parse_args (args)

  if parsed_args.rsh is not None:
    rsh = parsed_args.rsh
  else:
    rsh = "ssh"

  repo = cd_repo_connect_db()
  status_line.set_op ("PUSH")
  push (repo, parsed_args.url, rsh)

def cmd_get():
  parser = argparse.ArgumentParser (prog='bfsync get')
  parser.add_argument ('--rsh', help='set remote shell')
  parser.add_argument ("url", nargs = "*")
  parsed_args = parser.parse_args (args)

  if parsed_args.rsh is not None:
    rsh = parsed_args.rsh
  else:
    rsh = "ssh"

  repo = cd_repo_connect_db()

  status_line.set_op ("GET")
  get (repo, parsed_args.url, rsh)

def cmd_put():
  parser = argparse.ArgumentParser (prog='bfsync put')
  parser.add_argument ('--rsh', help='set remote shell')
  parser.add_argument ("url", nargs = "*")
  parsed_args = parser.parse_args (args)

  if parsed_args.rsh is not None:
    rsh = parsed_args.rsh
  else:
    rsh = "ssh"

  repo = cd_repo_connect_db()

  status_line.set_op ("PUT")
  put (repo, parsed_args.url, rsh)

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
  f.write ("# This file was automatically generated by bfsync init (do not edit)\n")
  f.write ("repo-type master;\n")
  f.write ("""repo-id "%s";\n""" % gen_repo_id());
  f.write ("version \"%s\";\n" % bfsyncdb.repo_version())
  f.close()

  # config file
  f = open ("config", "w")
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
  parser.add_argument ('--rsh', help='set remote shell')
  parser.add_argument ("repo")
  parser.add_argument ("dest_dir", nargs = "?")
  parsed_args = parser.parse_args (args)

  if parsed_args.rsh is not None:
    rsh = parsed_args.rsh
  else:
    rsh = "ssh"

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
  f.write ("""repo-id "%s";\n\n""" % gen_repo_id());
  f.write ("version \"%s\";\n" % bfsyncdb.repo_version())
  f.close()

  # default config
  f = open (os.path.join (dir, "config"), "w")

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
  pull (repo, [ url ], rsh, server = False)
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

def cmd_need_recover():
  parser = argparse.ArgumentParser (prog='bfsync need-recover')
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

  need_recover = bfsyncdb.need_recover_db (repo_path)

  print "recovery for " + repo_path + ":",

  if need_recover:
    print "required"
    sys.exit (0)
  else:
    print "not required"
    sys.exit (1)

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

def cmd_need_continue():
  parser = argparse.ArgumentParser (prog='bfsync need-continue')
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

  print "continue for " + repo.path + ":",

  if len (journal) > 0:
    print "required"
    sys.exit (0)
  else:
    print "not required"
    sys.exit (1)

def cmd_config_set():
  repo = cd_repo_connect_db()

  key = args[0]
  values = args[1:]

  repo.config.set (key, values)

  # write new config file
  f = open ("config", "w")
  f.write (repo.config.to_string())
  f.close()

def cmd_show_tags():
  repo = cd_repo_connect_db()

  version = int (args[0])

  tags = repo.bdb.list_tags (version)
  for t in tags:
    values = repo.bdb.load_tag (version, t)
    for v in values:
      print "%s=%s" % (t, v)

def cmd_debug_add_tag():
  repo = cd_repo_connect_db()
  lock = repo.try_lock()

  version = int (args[0])
  repo.bdb.begin_transaction()
  repo.bdb.add_tag (version, args[1], args[2])
  repo.bdb.commit_transaction()

def cmd_debug_del_tag():
  repo = cd_repo_connect_db()
  lock = repo.try_lock()

  version = int (args[0])
  repo.bdb.begin_transaction()
  repo.bdb.del_tag (version, args[1], args[2])
  repo.bdb.commit_transaction()

def cmd_debug_hash_filename():
  hash = args[0]
  repo = cd_repo_connect_db()
  file_number = repo.bdb.load_hash2file (hash)
  if file_number != 0:
    full_name = repo.make_number_filename (file_number)
    print full_name
  else:
    print "not found in objects"

def parse_vrange (vrange):
  m = re.match ("^([0-9]+)-([0-9]+)$", vrange)
  if m:
    return int (m.group (1)), int (m.group (2))

  m = re.match ("^([0-9]+)$", vrange)
  if m:
    return int (m.group (1)), int (m.group (1))

  raise BFSyncError ("'%s' is not a valid version range (should be either a number <N>, or a range <MIN>-<MAX>)" % vrange)

def format_vrange (vmin, vmax):
  if vmin == vmax:
    return "version %d" % vmin
  else:
    return "version range %d-%d" % (vmin, vmax)

def cmd_delete_version():
  repo = cd_repo_connect_db()
  lock = repo.try_lock()

  repo.bdb.begin_transaction()

  for arg in args:
    vmin, vmax = parse_vrange (arg)
    if vmin > vmax:
      raise BFSyncError ("version min %d is bigger than version max %d" % (vmin, vmax))
    if vmin < 1:
      raise BFSyncError ("version min %d is too small" % vmin)
    if vmax >= repo.first_unused_version():
      raise BFSyncError ("version max %d is bigger than the end of the history" % vmax)

    count = 0
    for version in range (vmin, vmax + 1):
      if "deleted" not in repo.bdb.list_tags (version):
        count += 1
        repo.bdb.add_tag (version, "deleted", "1")
    print "DEL: %s deleted, %d version deletion tags modified" % (format_vrange (vmin, vmax), count)

  repo.bdb.commit_transaction()

def cmd_undelete_version():
  repo = cd_repo_connect_db()
  lock = repo.try_lock()

  repo.bdb.begin_transaction()

  for arg in args:
    vmin, vmax = parse_vrange (arg)
    if vmin > vmax:
      raise BFSyncError ("version min %d is bigger than version max %d" % (vmin, vmax))
    if vmin < 1:
      raise BFSyncError ("version min %d is too small" % vmin)
    if vmax >= repo.first_unused_version():
      raise BFSyncError ("version max %d is bigger than the end of the history" % vmax)

    count = 0
    for version in range (vmin, vmax + 1):
      if "deleted" in repo.bdb.list_tags (version):
        count += 1
        repo.bdb.del_tag (version, "deleted", "1")
    print "UNDEL: %s undeleted, %d version deletion tags modified" % (format_vrange (vmin, vmax), count)

  repo.bdb.commit_transaction()

def cmd_debug_change_time():
  repo = cd_repo_connect_db()
  version = int (args[0])
  time_h = int (args[1])

  he1 = repo.bdb.load_history_entry (1)

  repo.bdb.begin_transaction()

  he = repo.bdb.load_history_entry (version)
  repo.bdb.delete_history_entry (version)
  he.time = he1.time + time_h * 3600

  repo.bdb.store_history_entry (he.version, he.hash, he.author, he.message, he.time)

  repo.bdb.commit_transaction()

def cmd_expire():
  repo = cd_repo_connect_db()
  expire (repo, args)

def cmd_version():
  print "bfsync %s" % bfsyncdb.repo_version()

def dot_format (number):  # 12345678 => "12.345.678"
  nstr = "%d" % number
  nstr = nstr[::-1]
  result = ""
  for c in nstr:
    if len (result) % 4 == 3 and len (result) > 0:
      result += "."
    result += c
  return result[::-1]

def cmd_disk_usage():
  parser = argparse.ArgumentParser (prog='bfsync disk-usage', add_help=False)
  parser.add_argument ('-h', action="store_true", default=False)
  parsed_args = parser.parse_args (args)

  repo = cd_repo_connect_db()

  class Usage:
    pass

  usage = Usage()
  hset = set()
  deleted_versions = repo.get_deleted_version_set()

  def du_add (inode):
    if len (inode.hash) == 40:
      if not inode.hash in hset:
        usage.mem += inode.size
        hset.add (inode.hash)

  versions = []
  VERSION = 1
  while True:
    hentry = repo.bdb.load_history_entry (VERSION)
    VERSION += 1

    if not hentry.valid:
      break
    if hentry.version not in deleted_versions:
      versions.append (hentry)

  versions.reverse()

  total_mem = 0
  print "Version |       Time          |   Bytes used (delta)"
  print "--------+---------------------+-----------------------"
  for hentry in versions:
    usage.mem = 0
    repo.foreach_inode_link (hentry.version, du_add, None)
    if parsed_args.h:
      mem_fmt = format_size1 (usage.mem)
    else:
      mem_fmt = dot_format (usage.mem)
    print "%6d  | %s | %20s" % (
      hentry.version,
      datetime.datetime.fromtimestamp (hentry.time).strftime ("%F %H:%M:%S"),
      mem_fmt
    )
    total_mem += usage.mem

  if parsed_args.h:
    total_mem_fmt = format_size1 (total_mem)
  else:
    total_mem_fmt = dot_format (total_mem)
  print "--------+---------------------+-----------------------"
  print "      total                   | %20s" % total_mem_fmt

def cmd_transfer_bench():
  transfer_bench (args)

def cmd_new_files():
  parser = argparse.ArgumentParser (prog='bfsync new-files', add_help=False)
  parser.add_argument ('-s', action="store_true", default=False)
  parser.add_argument ('-h', action="store_true", default=False)
  parser.add_argument ("version")
  parsed_args = parser.parse_args (args)

  repo = cd_repo_connect_db()
  VERSION = int (parsed_args.version)
  hset = set()

  # make a set of hashes that were already in the repository
  # before the version we're checking
  def collect_hashes (inode):
    if len (inode.hash) == 40:
      hset.add (inode.hash)

  if VERSION > 1:
    repo.foreach_inode_link (VERSION - 1, collect_hashes, None)

  size_sort_list = []

  def walk (id, prefix):
    inode = repo.bdb.load_inode (id, VERSION)
    if inode.valid:
      # print filename if hash was not known in previous version
      if len (inode.hash) == 40 and not inode.hash in hset:
        filename = "%s" % prefix
        if parsed_args.s:
          size_sort_list.append ((inode.size, filename))
        else:
          print filename
        hset.add (inode.hash)

      # recurse into subdirs
      if inode.type == bfsyncdb.FILE_DIR:
        links = repo.bdb.load_links (id, VERSION)
        for link in links:
          inode_name = prefix + "/" + link.name
          walk (link.inode_id, inode_name)

  walk (bfsyncdb.id_root(), "")

  if parsed_args.s:
    size_sort_list.sort (key = lambda x: x[0])
    print "%20s | %s" % ("File size", "Filename")
    print "---------------------+--------------------------------------------------------------------"
    for f in size_sort_list:
      if parsed_args.h:
        size_fmt = format_size1 (f[0])
      else:
        size_fmt = dot_format (f[0])
      print "%20s | %s" % (size_fmt, f[1])

def cmd_inr_test():
  repo = cd_repo_connect_db()

  VERSION = repo.first_unused_version()

  def walk (id, prefix):
    inode = repo.bdb.load_inode (id, VERSION)
    if inode.valid:
      print prefix
      # recurse into subdirs
      if inode.type == bfsyncdb.FILE_DIR:
        links = repo.bdb.load_links (id, VERSION)
        for link in links:
          inode_name = prefix + "/" + link.name
          walk (link.inode_id, inode_name)

  # walk (bfsyncdb.id_root(), "")

  def inr_walk (inode, prefix):
    if inode.valid():
      print prefix
      if inode.type() == bfsyncdb.FILE_DIR:
        children = inode.get_child_names (VERSION)
        for child in children:
          inode_name = prefix + "/" + child
          child_inode = inode.get_child (VERSION, child)
          inr_walk (child_inode, inode_name)

  inr = bfsyncdb.INodeRepo (repo.bdb)
  inode = inr.load_inode (bfsyncdb.id_root(), VERSION)

  foo_inode = inr.create_inode ("/foo", VERSION)
  foo_inode.set_type (bfsyncdb.FILE_DIR)
  foo_inode.set_mode (0755);
  inode.add_link (foo_inode, "foo", VERSION);

  bar_inode = inr.create_inode ("/foo/bar", VERSION)
  bar_inode.set_type (bfsyncdb.FILE_DIR)
  bar_inode.set_mode (0755);
  foo_inode.add_link (bar_inode, "bar", VERSION);

  inr.save_changes()
  inr_walk (inode, "")

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
      ( "log",                    cmd_log, 1),
      ( "pull",                   cmd_pull, 1),
      ( "push",                   cmd_push, 1),
      ( "gc",                     cmd_gc, 0),
      ( "expire",                 cmd_expire, 1),
      ( "get",                    cmd_get, 1),
      ( "put",                    cmd_put, 1),
      ( "status",                 cmd_status, 0),
      ( "revert",                 cmd_revert, 1),
      ( "init",                   cmd_init, 1),
      ( "clone",                  cmd_clone, 1),
      ( "repo-files",             cmd_repo_files, 1),
      ( "new-files",              cmd_new_files, 1),
      ( "db-fingerprint",         cmd_db_fingerprint, 0),
      ( "remove",                 cmd_remove, 0),
      ( "remote",                 cmd_remote, 1),
      ( "recover",                cmd_recover, 1),
      ( "need-recover",           cmd_need_recover, 1),
      ( "need-continue",          cmd_need_continue, 1),
      ( "disk-usage",             cmd_disk_usage, 1),
      ( "config-set",             cmd_config_set, 1),
      ( "show-tags",              cmd_show_tags, 1),
      ( "delete-version",         cmd_delete_version, 1),
      ( "undelete-version",       cmd_undelete_version, 1),
      ( "transfer-bench",         cmd_transfer_bench, 1),
      ( "inr-test",               cmd_inr_test, 1),
      ( "debug-add-tag",          cmd_debug_add_tag, 1),
      ( "debug-del-tag",          cmd_debug_del_tag, 1),
      ( "debug-load-all-inodes",  cmd_debug_load_all_inodes, 0),
      ( "debug-perf-getattr",     cmd_debug_perf_getattr, 1),
      ( "debug-clear-cache",      cmd_debug_clear_cache, 1),
      ( "debug-integrity",        cmd_debug_integrity, 0),
      ( "debug-get-prof",         cmd_debug_get_prof, 0),
      ( "debug-reset-prof",       cmd_debug_reset_prof, 0),
      ( "debug-inode-name",       cmd_debug_inode_name, 1),
      ( "debug-hash-filename",    cmd_debug_hash_filename, 1),
      ( "debug-change-time",      cmd_debug_change_time, 1),
      ( "--version",              cmd_version, 0),
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
