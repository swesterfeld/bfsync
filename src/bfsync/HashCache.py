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

import time
import random
import os
import hashlib
import cPickle
import bfsyncdb
from utils import format_size, format_rate, format_time
from StatusLine import status_line, OutputSubsampler

class HashCache:
  def __init__ (self):
    self.cache = bfsyncdb.HashCacheDict()
    self.need_load = True

  def load_cache (self):
    if os.getenv ("BFSYNC_NO_HASH_CACHE") == "1":
      return
    try:
      f = open (os.path.expanduser ('~/.bfsync_cache'), "r")
      load_time = time.time()
      for line in f.readlines():
        (stat_hash, file_hash, expire_time) = line.split()
        # expire cache entries which are too old
        expire_time = int (expire_time)
        if load_time < expire_time:
          self.cache.insert (stat_hash, file_hash, expire_time)
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
    self.cache.insert (stat_hash, file_hash, self.gen_expire_time())

  def lookup (self, stat_hash):
    entry = self.cache.lookup (stat_hash)
    if entry.valid:
      # renew expire time during item lookup (to keep it longer in cache)
      self.cache.insert (stat_hash, entry.file_hash, max (entry.expire_time, self.gen_expire_time()))
      return entry.file_hash
    else:
      return ""

  def compute_hash (self, filename):
    # on demand loading
    if self.need_load:
      self.need_load = False
      self.load_cache()

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
      stat.st_mode, stat.st_ino, stat.st_nlink,
      stat.st_uid, stat.st_gid, stat.st_size,
      stat.st_mtime, stat.st_ctime,
      # stat.st_atime, - we keep out atime to allow reading file without changing the hash value
      # stat.st_dev    - we keep out device since usb devices can change device stat entry
    ]
    stat_hash = hashlib.sha1 (cPickle.dumps (l)).hexdigest()
    return stat_hash

  def save (self):
    # if we didn't on-demand-load the cache, we have no new entries
    if self.need_load:
      return

    if os.getenv ("BFSYNC_NO_HASH_CACHE") == "1":
      return
    # reload cache data in case another bfsync process has added entries to the cache
    self.load_cache()
    try:
      f = open (os.path.expanduser ('~/.bfsync_cache'), "w")
      it = bfsyncdb.HashCacheIterator (self.cache)
      while True:
        entry = it.get_next()
        if not entry.valid:
          break
        f.write ("%s %s %d\n" % (entry.stat_hash, entry.file_hash, entry.expire_time))
      f.close()
    except:
      pass

hash_cache = HashCache()
