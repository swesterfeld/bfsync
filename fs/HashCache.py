import time
import random
import os

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
