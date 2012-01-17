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
import sqlite3
import CfgParser
import bfsyncdb
from tempfile import NamedTemporaryFile

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
    c = self.conn.cursor()
    c.execute ('''INSERT INTO temp_files VALUES (?, ?)''', (os.path.basename (tmp_file.name), os.getpid()))
    self.conn.commit()
    return tmp_file.name

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

    if inode.type != 3: # FIXME: constants
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
      if not inode.valid:
        raise Exception ("missing inode in Repo.foreach_changed_inode")

      inode_callback (inode)

def cd_repo_connect_db():
  repo_path = find_repo_dir()
  bfsync_info = parse_config (repo_path + "/config")

  sqlite_sync = bfsync_info.get ("sqlite-sync")
  if len (sqlite_sync) != 1:
    raise Exception ("bad sqlite-sync setting")

  if sqlite_sync[0] == "0":
    sqlite_sync = False
  else:
    sqlite_sync = True

  os.chdir (repo_path)

  repo = Repo()
  repo.conn = sqlite3.connect (os.path.join (repo_path, 'db'))
  repo.bdb = bfsyncdb.open_db (os.path.join (repo_path, 'bdb'))
  repo.conn.text_factory = str;
  repo.path = repo_path
  repo.config = bfsync_info

  if not sqlite_sync:
    c = repo.conn.cursor()
    c.execute ('''PRAGMA synchronous=off''')

  # wipe old temp files
  try:
    c.execute ('''SELECT * FROM temp_files''')
    for row in c:
      filename = os.path.join (repo_path, "tmp", row[0])
      pid = row[1]
      try:
        os.kill (pid, 0)
        # pid still running
      except:
        try:
          os.remove (filename)
        except:
          pass
  except:
    pass # no temp_files table => no temp files

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
  ],
  [
    "repo-type",
    "repo-path",
    "mount-point",
    "cached-inodes",
    "cached-dirs",
    "sqlite-sync",
    "default/pull",
    "default/push",
    "default/get",
    "default/put",
    "get-rate-limit",
    "put-rate-limit",
    "use-uid-gid"
  ])
  return bfsync_info

def make_object_filename (hash):
  if len (hash) != 40:
    raise Exception ("bad hash %s (not len 40)" % hash)
  return hash[0:2] + "/" + hash[2:]

def validate_object (object_file, hash):
  try:
    import HashCache
    os.stat (object_file)
    if HashCache.hash_cache.compute_hash (object_file) == hash:
      return True
  except:
    pass
  return False

def move_file_to_objects (repo, filename):
  import HashCache
  hash = HashCache.hash_cache.compute_hash (filename)
  object_name = os.path.join (repo.path, "objects", make_object_filename (hash))
  if os.path.exists (object_name):
    # already known
    os.unlink (filename)
  else:
    # add new object
    os.rename (filename, object_name)
    os.chmod (object_name, 0400)
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

def printable_name (c, id, VERSION):
  if id == "0" * 40:
    return "/"
  c.execute ("SELECT dir_id, name FROM links WHERE inode_id=? AND ? >= vmin AND ? <= VMAX", (id, VERSION, VERSION))
  for row in c:
    return os.path.join (printable_name (c, row[0], VERSION), row[1])
  return "*unknown*"

class RemoteFile:
  pass

class BFSyncError (Exception):
  pass

def init_bfsync_file (dir):
  bfsync_file = os.path.join (dir, ".bfsync")
  f = open (bfsync_file, "w")
  f.write ("# do not delete: this file allows bfsync recognizing this directory as repository\n")
  f.close()
