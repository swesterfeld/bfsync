import os
import sqlite3
import CfgParser
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
      cfg_file = os.path.join (dir, ".bfsync/info")
      f = open (cfg_file, "r")
      f.close()
      file_ok = True
    except:
      pass
    if file_ok:
      cfg = parse_config (cfg_file)
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
      raise Exception ("error: can not find .bfsync directory")
    dir = newdir

class Repo:
  def make_temp_name (self):
    tmp_file = NamedTemporaryFile (dir = os.path.join (self.path, "tmp"), delete = False)
    tmp_file.close()
    c = self.conn.cursor()
    c.execute ('''INSERT INTO temp_files VALUES (?, ?)''', (os.path.basename (tmp_file.name), os.getpid()))
    self.conn.commit()
    return tmp_file.name

def cd_repo_connect_db():
  repo_path = find_repo_dir()
  bfsync_info = parse_config (repo_path + "/.bfsync/config")

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
    "put-rate-limit"
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
