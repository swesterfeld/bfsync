import time
import sys
import os
import shutil
from utils import mkdir_recursive, format_size, format_rate, format_time
from StatusLine import status_line

class TransferFile:
  def __init__ (self, src_path, dest_path, size, mode):
    self.src_path = src_path
    self.dest_path = dest_path
    self.size = size
    self.mode = mode

class TransferList:
  def __init__ (self):
    self.tlist = []
    self.bytes_total = 0
    self.bytes_done = 0
    self.start_time = 0 # must be set to time.time() once sending/receiving starts
    self.file_number = 0
  def add (self, tfile):
    self.tlist += [ tfile ]
    self.bytes_total += tfile.size
  def send_list (self, pipe):
    tlist_str = pickle.dumps (self.tlist)
    # prepend pickled string len
    tlist_str = str (len (tlist_str)) + "\n" + tlist_str
    pipe.write (tlist_str)
  def send_files (self, pipe, verbose):
    self.start_time = time.time()
    for tfile in self.tlist:
      self.file_number += 1
      f = open (tfile.src_path)
      remaining = tfile.size
      while (remaining > 0):
        todo = min (remaining, 256 * 1024)
        data = f.read (todo)
        pipe.write (data)
        remaining -= todo
        self.bytes_done += todo
        if (verbose):
          self.update_status_line()
      f.close()
    if (verbose):
      print
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
    self.tlist = pickle.loads (tlist_str)
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
  def receive_files (self, pipe, verbose):
    self.start_time = time.time()
    for tfile in self.tlist:
      self.file_number += 1
      mkdir_recursive (os.path.dirname (tfile.dest_path))
      f = open (tfile.dest_path, "w")
      remaining = tfile.size
      while (remaining > 0):
        todo = min (remaining, 256 * 1024)
        data = pipe.read (todo)
        f.write (data)
        remaining -= todo
        self.bytes_done += todo
        if (verbose):
          self.update_status_line()
      f.close()
      os.chmod (tfile.dest_path, tfile.mode)
    if (verbose):
      print
  def copy_files (self):
    self.start_time = time.time()
    for tfile in self.tlist:
      self.file_number += 1
      try:
        mkdir_recursive (os.path.dirname (tfile.dest_path))
        shutil.copy2 (tfile.src_path, tfile.dest_path)
        os.chmod (tfile.dest_path, tfile.mode)
      except Exception, ex:
        sys.stderr.write ("can't copy file %s to %s: %s\n" % (tfile.src_path, tfile.dest_path, ex))
        sys.exit (1)
      self.bytes_done += tfile.size
      self.update_status_line()
    print


