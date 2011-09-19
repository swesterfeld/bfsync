import time
import random
import os
import hashlib
import pickle
from utils import format_size, format_rate, format_time

class HashCacheEntry:
  def __init__ (self, stat_hash, file_hash, expire_time):
    self.stat_hash = stat_hash
    self.file_hash = file_hash
    self.expire_time = expire_time

class HashCache:
  def __init__ (self):
    self.cache = dict()
    self.load_cache()

  def load_cache (self):
    try:
      f = open (os.path.expanduser ('~/.bfsync2_cache'), "r")
      load_time = time.time()
      for line in f.readlines():
        (stat_hash, file_hash, expire_time) = line.split()
        if not self.cache.has_key (stat_hash):
          # expire cache entries which are too old
          expire_time = int (expire_time)
          if load_time < expire_time:
            self.cache[stat_hash] = HashCacheEntry (stat_hash, file_hash, expire_time)
      f.close ()
    except:
      pass

  def gen_expire_time (self):
    expire_end  = 1   # -> 1 second
    expire_end *= 60  # -> 1 minute
    expire_end *= 60  # -> 1 hour
    expire_end *= 24  # -> 1 day
    expire_end *= 30  # -> 1 month
    expire_start = expire_end / 2  # -> 2 weeks
    expire_time = int (time.time()) + random.randint (expire_start, expire_end)
    return expire_time

  def insert (self, stat_hash, file_hash):
    self.cache[stat_hash] = HashCacheEntry (stat_hash, file_hash, self.gen_expire_time())

  def lookup (self, stat_hash):
    if self.cache.has_key (stat_hash):
      self.cache[stat_hash].expire_time = max (self.cache[stat_hash].expire_time, self.gen_expire_time())
      return self.cache[stat_hash].file_hash
    else:
      return ""

  def compute_hash (self, filename):
    filename = os.path.abspath (filename)
    stat_hash = self.make_stat_hash (filename)
    result = self.lookup (stat_hash)
    if result != "":  # file hash already in cache
      return result
    file = open (filename, "r")
    hash = hashlib.sha1()
    eof = False
    while not eof:
      data = file.read (256 * 1024)
      if data == "":
        eof = True
      else:
        hash.update (data)
    file.close()
    result = hash.hexdigest()
    self.insert (stat_hash, result)
    return result

  def make_stat_hash (self, filename):
    filename = os.path.abspath (filename)
    stat = os.stat (filename)
    l = [
      filename,
      stat.st_mode, stat.st_ino, stat.st_dev, stat.st_nlink,
      stat.st_uid, stat.st_gid, stat.st_size,
      stat.st_mtime, stat.st_ctime,
      # stat.st_atime, - we keep out atime to allow reading file without changing the hash value
    ]
    stat_hash = hashlib.sha1 (pickle.dumps (l)).hexdigest()
    return stat_hash

  def hash_all (self, status_line, filenames):
    # compute total size of all files
    bytes_total = 0
    for filename in filenames:
      bytes_total += os.path.getsize (filename)
    # hash data
    bytes_done = 0
    file_number = 0
    start_time = time.time()
    status_line.set_op ("HASH")
    for filename in filenames:
      file = open (filename, "r")
      file_number += 1
      stat_hash = self.make_stat_hash (filename)
      result = self.lookup (stat_hash)
      if result == "":
        hash = hashlib.sha1()
        eof = False
        while not eof:
          data = file.read (256 * 1024)
          if data == "":
            eof = True
          else:
            hash.update (data)
            bytes_done += len (data)
            elapsed_time = max (time.time() - start_time, 1)
            bytes_per_sec = max (bytes_done / elapsed_time, 1)
            eta = int ((bytes_total - bytes_done) / bytes_per_sec)
            status_line.update ("file %d/%d    %s    %.1f%%   %s   ETA: %s" % (
              file_number, len (filenames),
              format_size (bytes_done, bytes_total),
              bytes_done * 100.0 / max (bytes_total, 1),
              format_rate (bytes_per_sec),
              format_time (eta)
            ))
        file.close()
        result = hash.hexdigest()
        self.insert (stat_hash, result)
      else:
        bytes_done += os.path.getsize (filename)
    return

  def save (self):
    # reload cache data in case another bfsync process has added entries to the cache
    self.load_cache()
    try:
      f = open (os.path.expanduser ('~/.bfsync2_cache'), "w")
      for key in self.cache:
        entry = self.cache[key]
        f.write ("%s %s %d\n" % (entry.stat_hash, entry.file_hash, entry.expire_time))
      f.close()
    except:
      pass

hash_cache = HashCache()
