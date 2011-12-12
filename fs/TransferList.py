import time
import sys
import os
import shutil
import cPickle
from utils import *
from StatusLine import status_line

class TransferFile:
  def __init__ (self, hash, size):
    self.hash = hash
    self.size = size

class TransferParams:
  def __init__ (self, rate_limit):
    self.rate_limit = rate_limit

class RateLimiter:
  def __init__ (self, pipe, params):
    self.pipe = pipe
    self.params = params
    self.sent_bytes = 0
    self.start_time = time.time()

  def write (self, data):
    if self.params.rate_limit > 0:
      while (self.sent_bytes / (time.time() - self.start_time)) > (self.params.rate_limit * 1024):
        time.sleep (0.1)
    self.pipe.write (data)
    self.sent_bytes += len (data)

class TransferList:
  def __init__ (self):
    self.tlist = []
    self.bytes_total = 0
    self.bytes_done = 0
    self.start_time = 0 # must be set to time.time() once sending/receiving starts
    self.file_number = 0
    self.last_update = 0
  def add (self, tfile):
    self.tlist += [ tfile ]
    self.bytes_total += tfile.size
  def send_list (self, pipe):
    tlist_str = cPickle.dumps (self.tlist)
    # prepend pickled string len
    tlist_str = str (len (tlist_str)) + "\n" + tlist_str
    pipe.write (tlist_str)
  def send_files (self, repo, pipe, verbose, params):
    self.start_time = time.time()
    rate_limiter = RateLimiter (pipe, params)
    for tfile in self.tlist:
      self.file_number += 1
      f = open (os.path.join (repo.path, "objects", make_object_filename (tfile.hash)))
      remaining = tfile.size
      while (remaining > 0):
        todo = min (remaining, 256 * 1024)
        data = f.read (todo)
        rate_limiter.write (data)
        remaining -= todo
        self.bytes_done += todo
        if verbose and self.need_update():
          self.update_status_line()
      f.close()
    if (verbose):
      self.update_status_line()
      status_line.cleanup()
  def receive_list (self, pipe):
    in_size = True
    size_str = ""
    while in_size:
      s = pipe.read (1)
      if s == '\n':
        in_size = False
      else:
        size_str += s
    size = int (size_str)
    tlist_str = pipe.read (size)
    self.tlist = cPickle.loads (tlist_str)
  def need_update (self):
    update_time = time.time()
    if update_time - self.last_update > 1:
      self.last_update = update_time
      return True
    else:
      return False
  def update_status_line (self):
    elapsed_time = max (time.time() - self.start_time, 1)
    bytes_per_sec = max (self.bytes_done / elapsed_time, 1)
    eta = int ((self.bytes_total - self.bytes_done) / bytes_per_sec)
    status_line.update ("file %d/%d    %s    %.1f%%   %s   ETA: %s" % (
        self.file_number, len (self.tlist),
        format_size (self.bytes_done, self.bytes_total),
        self.bytes_done * 100.0 / max (self.bytes_total, 1),
        format_rate (bytes_per_sec),
        format_time (eta)
      ))
  def receive_files (self, repo, pipe, verbose):
    self.start_time = time.time()
    dest_path = repo.make_temp_name()
    for tfile in self.tlist:
      self.file_number += 1
      f = open (dest_path, "w")
      remaining = tfile.size
      while (remaining > 0):
        todo = min (remaining, 256 * 1024)
        data = pipe.read (todo)
        f.write (data)
        remaining -= todo
        self.bytes_done += todo
        if verbose and self.need_update():
          self.update_status_line()
      f.close()
      move_file_to_objects (repo, dest_path)
    if (verbose):
      self.update_status_line()
      status_line.cleanup()
  def copy_files (self, dest_repo, src_repo):
    self.start_time = time.time()
    dest_path = dest_repo.make_temp_name()
    for tfile in self.tlist:
      self.file_number += 1
      src_path = os.path.join (src_repo.path, "objects", make_object_filename (tfile.hash))
      try:
        shutil.copyfile (src_path, dest_path)
        move_file_to_objects (dest_repo, dest_path)
      except Exception, ex:
        sys.stderr.write ("can't copy file %s to %s: %s\n" % (src_path, dest_path, ex))
        sys.exit (1)
      self.bytes_done += tfile.size
      if self.need_update():
        self.update_status_line()
    self.update_status_line()
    status_line.cleanup()


