import os
import sqlite3
import CfgParser

def format_size (size, total_size):
  unit_str = [ "B", "KB", "MB", "GB", "TB" ]
  unit = 0
  while (total_size > 10000) and (unit + 1 < len (unit_str)):
    size = (size + 512) / 1024
    total_size = (total_size + 512) / 1024
    unit += 1
  return "%d/%d %s" % (size, total_size, unit_str[unit])

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
      if repo_type_list[0] == "store":
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
  return sqlite3.connect (os.path.join (repo_path, 'db')), repo_path

def parse_config (filename):
  bfsync_info = CfgParser.CfgParser (filename,
  [
  ],
  [
    "repo-type",
    "repo-path",
    "mount-point",
    "cached-inodes",
    "cached-dirs",
    "sqlite-sync",
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
