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

import os
import CfgParser
import bfsyncdb
import resource
import cPickle
import time
from tempfile import NamedTemporaryFile
from ServerConn import ServerConn

ID_ROOT = "/" + "0" * 40        # root directory id string

def format_size (size, total_size):
  unit_str = [ "B", "KB", "MB", "GB", "TB" ]
  unit = 0
  while (total_size > 10000) and (unit + 1 < len (unit_str)):
    size = (size + 512) / 1024
    total_size = (total_size + 512) / 1024
    unit += 1
  return "%d/%d %s" % (size, total_size, unit_str[unit])

def format_size1 (size):
  unit_str = [ "B", "KB", "MB", "GB", "TB" ]
  unit = 0
  while (size > 10000) and (unit + 1 < len (unit_str)):
    size = (size + 512) / 1024
    unit += 1
  return "%d %s" % (size, unit_str[unit])

def format_rate (bytes_per_sec):
  unit_str = [ "B/s", "KB/s", "MB/s", "GB/s", "TB/s" ]
  unit = 0
  while (bytes_per_sec > 10000) and (unit + 1 < len (unit_str)):
    bytes_per_sec /= 1024
    unit += 1
  return "%.1f %s" % (bytes_per_sec, unit_str[unit])

def format_time (sec):
  sec = int (sec)
  if (sec < 3600):
    return "%d:%02d" % (sec / 60, sec % 60)
  else:
    return "%d:%02d:%02d" % (sec / 3600, (sec / 60) % 60, sec % 60)

def mkdir_recursive (dir):
  if os.path.exists (dir) and os.path.isdir (dir):
    return
  else:
    try:
      os.makedirs (dir)
      if os.path.exists (dir) and os.path.isdir (dir):
        return
    except:
      pass
  raise Exception ("can't create directory %s\n" % dir)

def find_repo_dir():
  dir = os.getcwd()
  while True:
    file_ok = False
    try:
      cfg_filename = os.path.join (dir, ".bfsync/info")
      f = open (cfg_filename, "r")
      f.close()
      file_ok = True
    except:
      pass
    try:
      bfsync_filename = os.path.join (dir, ".bfsync")
      f = open (bfsync_filename, "r")
      f.close()
      file_ok = True
      cfg_filename = os.path.join (dir, "info")
    except:
      pass
    if file_ok:
      cfg = parse_config (cfg_filename)
      repo_type_list = cfg.get ("repo-type")
      if len (repo_type_list) != 1:
        raise Exception ("bad repo-type list (should have exactly 1 entry)")
      if repo_type_list[0] == "store" or repo_type_list[0] == "master":
        pass # dir ok
      elif repo_type_list[0] == "mount":
        dir = cfg.get ("repo-path")
        if len (dir) == 1:
          dir = dir[0]
        else:
          raise Exception ("bad repo-path list (should have exactly 1 entry)")
      else:
        raise Exception ("unknown repo-type '%s' in find_repo_dir", cfg.get ("repo-type"))
      return dir
    # try parent directory
    newdir = os.path.dirname (dir)
    if newdir == dir:
      # no more parent
      raise BFSyncError ("error: can not find .bfsync directory/file")
    dir = newdir

class Repo:
  def make_temp_name (self):
    tmp_file = NamedTemporaryFile (dir = os.path.join (self.path, "tmp"), delete = False)
    tmp_file.close()
    self.bdb.add_temp_file (os.path.basename (tmp_file.name), os.getpid())
    return tmp_file.name

  def make_number_filename (self, file_number, create_dir = False):
    dn = file_number / 4096
    fn = file_number % 4096
    dirname = os.path.join (self.path, "objects/%x" % dn)
    filename = "%03x" % fn
    if create_dir and not os.path.exists (dirname):
      os.mkdir (dirname)
    return os.path.join (dirname, filename)

  def make_object_filename (self, hash):
    file_number = self.bdb.load_hash2file (hash)
    if file_number == 0:
      raise Exception ("object %s not found" % hash)
    objectname = self.make_number_filename (file_number)
    return objectname

  def validate_object (self, hash):
    try:
      object_name = self.make_object_filename (hash)
      import HashCache
      my_hash = HashCache.hash_cache.compute_hash (object_name)
      if my_hash == hash:
        return True
    except:
      pass
    return False

  def first_unused_version (self):
    version = 1
    while True:
      hentry = self.bdb.load_history_entry (version)
      if not hentry.valid:
        break

      version += 1
    # => return the first version not present in history
    return version

  # foreach_crawl / foreach_inode_link recursively walk the filesystem tree for a one version
  def foreach_crawl (self, inode, version, inode_callback, link_callback):
    if not inode.valid:
      raise Exception ("missing inode in Repo.foreach_crawl")

    if inode_callback:
      inode_callback (inode)

    if inode.type != bfsyncdb.FILE_DIR:
      return # not a directory, no links

    links = self.bdb.load_links (inode.id, version)
    for link in links:
      if link_callback:
        link_callback (link)

      child = self.bdb.load_inode (link.inode_id, version)
      self.foreach_crawl (child, version, inode_callback, link_callback)

  def foreach_inode_link (self, version, inode_callback, link_callback):
    root = self.bdb.load_inode (bfsyncdb.id_root(), version)
    self.foreach_crawl (root, version, inode_callback, link_callback)

  def foreach_changed_inode (self, version, inode_callback):
    changed_it = bfsyncdb.ChangedINodesIterator (self.bdb)

    while True:
      id = changed_it.get_next()
      if not id.valid:
        return

      inode = self.bdb.load_inode (id, version)
      if inode.valid:
        inode_callback (inode)

  def printable_name (self, inode_id, version):
    name_dict = dict()
    def update_names (link):
      name_dict[link.inode_id.str()] = (link.dir_id.str(), link.name)

    self.foreach_inode_link (version, None, update_names)
    name = []
    while inode_id != ID_ROOT:
      pn = name_dict[inode_id]
      inode_id = pn[0]
      name.insert (0, pn[1])
    return "/" + "/".join (name)

  def check_uncommitted_changes (self):
    dg = bfsyncdb.DiffGenerator (self.bdb)
    change = dg.get_next()
    del dg

    if len (change) == 0: # empty diff -> no uncommitted changes
      return False
    else:
      return True

  # return all versions which have been deleted (have a deleted tag)
  def get_deleted_version_set (self):
    # read log, scan for deleted versions
    deleted_versions = set()
    VERSION = 1
    while True:
      hentry = self.bdb.load_history_entry (VERSION)
      VERSION += 1

      if not hentry.valid:
        break

      tags = self.bdb.list_tags (hentry.version)
      if "deleted" in tags:
        deleted_versions.add (hentry.version)
    return deleted_versions

  # returns an instance of ServerConn with active lock, or
  #         None if server could not be connected
  def try_lock (self):
    # lock if server is running: this causes history to be reloaded in fs process
    try:
      server_conn = ServerConn (self.path)
      server_conn.get_lock()
      return server_conn
    except IOError, e:
      return None       # no server => no lock

def cd_repo_connect_db (cont = False):
  repo_path = find_repo_dir()
  bfsync_config = parse_config (repo_path + "/config")
  bfsync_info = parse_config (repo_path + "/info")

  cache_size = bfsync_config.get ("cache-size")
  if len (cache_size) != 1:
    raise Exception ("bad cache-size setting")
  cache_size = int (cache_size[0])

  version = bfsync_info.get ("version")
  if len (version) != 1:
    raise Exception ("bad version setting");
  if version[0] != bfsyncdb.repo_version():
    raise BFSyncError ("incompatible repository version, need version %s, got version %s" % (bfsyncdb.repo_version(), version[0]))

  os.chdir (repo_path)

  repo = Repo()
  repo.bdb = bfsyncdb.open_db (repo_path, cache_size, False)
  if not repo.bdb.open_ok():
    raise BFSyncError ("database of repository %s can't be opened" % repo_path)

  jentries = repo.bdb.load_journal_entries()
  if len (jentries) != 0 and not cont:
    op = cPickle.loads (jentries[0].operation)
    try:
      os.kill (op.pid, 0)
      # pid still running
      running = True
    except:
      running = False
    if running:
      print "The following command is still running (pid %d):" % op.pid
      print
      print " *", op.command_line
      print
      raise BFSyncError ("wait until this command is finished before trying again")
    else:
      print "The following command was interrupted:"
      print
      print " *", op.command_line
      print
      raise BFSyncError ("run bfsync continue %s to fix this" % repo_path)

  repo.path = repo_path
  repo.config = bfsync_config
  repo.info = bfsync_info

  if not cont:
    # wipe old temp files (only if we're not starting in "continue" mode)
    repo.bdb.begin_transaction()
    temp_files = repo.bdb.load_temp_files()
    for temp_file in temp_files:
      filename = os.path.join (repo_path, "tmp", temp_file.filename)
      pid = temp_file.pid
      try:
        os.kill (pid, 0)
        # pid still running
      except:
        try:
          os.remove (filename)
        except:
          pass
        repo.bdb.delete_temp_file (temp_file.filename)
    repo.bdb.commit_transaction()

  return repo

def connect_db (path):
  old_cwd = os.getcwd()
  try:
    # connect to db
    os.chdir (path)
    repo = cd_repo_connect_db()
  finally:
    os.chdir (old_cwd)
  return repo

def parse_config (filename):
  bfsync_info = CfgParser.CfgParser (filename,
  [
    "default",
    "expire",
    "sql-export",
  ],
  [
    "repo-type",
    "repo-path",
    "mount-point",
    "cached-inodes",
    "cached-dirs",
    "default/pull",
    "default/push",
    "default/get",
    "default/put",
    "expire/keep_most_recent",
    "expire/create_daily",
    "expire/keep_daily",
    "expire/create_weekly",
    "expire/keep_weekly",
    "expire/create_monthly",
    "expire/keep_monthly",
    "expire/create_yearly",
    "expire/keep_yearly",
    "sql-export/database",
    "sql-export/user",
    "sql-export/password",
    "sql-export/host",
    "sql-export/port",
    "get-rate-limit",
    "put-rate-limit",
    "use-uid-gid",
    "cache-size",
    "repo-id",
    "version"
  ])
  return bfsync_info

def move_file_to_objects (repo, filename, need_transaction = True):
  import HashCache
  hash = HashCache.hash_cache.compute_hash (filename)

  if need_transaction:
    repo.bdb.begin_transaction()

  if repo.bdb.load_hash2file (hash) == 0:
    new_file_number = repo.bdb.gen_new_file_number()
    objectname = repo.make_number_filename (new_file_number, True)
    os.rename (filename, repo.make_number_filename (new_file_number))
    os.chmod (objectname, 0400)
    repo.bdb.store_hash2file (hash, new_file_number)
  else:
    # already known
    os.unlink (filename)

  if need_transaction:
    repo.bdb.commit_transaction()
  return hash

def parse_diff (diff):
  changes = []
  sdiff = diff.split ("\0")
  start = 0
  while len (sdiff) - start > 1:
    fcount = 0
    if sdiff[start] == "l+" or sdiff[start] == "l!":
      fcount = 4
    elif sdiff[start] == "l-":
      fcount = 3
    elif sdiff[start] == "i+" or sdiff[start] == "i!":
      fcount = 16
    elif sdiff[start] == "i-":
      fcount = 2

    if fcount == 0:
      raise Exception ("error during diff parse %s" % sdiff[start:])
    assert (fcount != 0)
    changes += [ sdiff[start:start + fcount] ]
    start += fcount
  return changes

class RemoteFile:
  pass

class BFSyncError (Exception):
  pass

def init_bfsync_file (dir):
  bfsync_file = os.path.join (dir, ".bfsync")
  f = open (bfsync_file, "w")
  f.write ("# do not delete: this file allows bfsync recognizing this directory as repository\n")
  f.close()

class MemUsageTime:
  pass

mem_usage_time = MemUsageTime()

def init_mem_usage_time():
  mem_usage_time.start = time.time()

def print_mem_usage (comment):
  f = open ("/proc/self/smaps")
  mem_smaps = 0
  for line in f:
    fields = line.split ()
    if fields[0][0].isupper():
      if fields[0] == "Rss:" and inode == 0:
        mem_smaps += int (fields[1])
    else:
      inode = int (fields[4])
  f.close()
  print
  print "** %.2f ** %s ** memory usage: %d K" % (time.time() - mem_usage_time.start, comment, resource.getrusage (resource.RUSAGE_SELF).ru_maxrss)
  print "** %.2f ** %s ** memory smaps: %d K" % (time.time() - mem_usage_time.start, comment, mem_smaps)
  print

class TransactionSplitter:
  def __init__ (self, repo, max_count):
    self.repo = repo
    self.count = 0
    self.max_count = max_count
    self.repo.bdb.begin_transaction()

  def __del__ (self):
    assert self.count == 0

  def split (self):
    self.count += 1
    if self.count >= self.max_count:
      self.repo.bdb.commit_transaction()
      self.repo.bdb.begin_transaction()
      self.count = 0

  def commit (self):
    self.repo.bdb.commit_transaction()
    self.count = 0
