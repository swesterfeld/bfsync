import subprocess
import cPickle
import os
from utils import connect_db
from remoteutils import *

LOCAL = 1
SSH = 2

class RemoteRepo:
  def __init__ (self, url):
    self.url = url

    url_list = url.split (":")
    if len (url_list) == 1:
      self.path = os.path.abspath (url)
      self.conn = LOCAL
      self.repo = connect_db (self.path)
    elif len (url_list) == 2:
      self.host = url_list[0]
      self.path = url_list[1]
      self.conn = SSH
      command = ["ssh", self.host, "bfsync2", "remote", self.path]
      self.remote_p = subprocess.Popen (command, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    else:
      raise Exception ("unknown url type %s, neither local nor remote?" % url)

  def get_history (self):
    if self.conn == LOCAL:
      return remote_history (self.repo)
    else:
      self.remote_p.stdin.write ("history\n")
      result_len = int (self.remote_p.stdout.readline())
      return cPickle.loads (self.remote_p.stdout.read (result_len))

  def update_history (self, delta_history):
    if self.conn == LOCAL:
      remote_update_history (self.repo, delta_history)
    else:
      self.remote_p.stdin.write ("update-history\n")
      delta_hist_str = cPickle.dumps (delta_history)
      self.remote_p.stdin.write ("%s\n%s" % (len (delta_hist_str), delta_hist_str))
      self.remote_p.stdin.flush()
      result_len = int (self.remote_p.stdout.readline())
      return self.remote_p.stdout.read (result_len)

  def need_objects (self):
    if self.conn == LOCAL:
      return remote_need_objects (self.repo)
    else:
      self.remote_p.stdin.write ("need-objects\n")
      result_len = int (self.remote_p.stdout.readline())
      return cPickle.loads (self.remote_p.stdout.read (result_len))

  def ls (self):
    if self.conn == LOCAL:
      return remote_ls (self.repo)
    else:
      self.remote_p.stdin.write ("ls\n")
      result_len = int (self.remote_p.stdout.readline())
      return cPickle.loads (self.remote_p.stdout.read (result_len))

  def get_objects (self, tlist):
    if self.conn == LOCAL:
      tlist.copy_files()
    else:
      self.remote_p.stdin.write ("send\n")
      tlist.send_list (self.remote_p.stdin)
      tlist.receive_files (self.remote_p.stdout, True)

  def put_objects (self, tlist):
    if self.conn == LOCAL:
      tlist.copy_files()
    else:
      self.remote_p.stdin.write ("receive\n")
      tlist.send_list (self.remote_p.stdin)
      tlist.send_files (self.remote_p.stdin, True)
      self.remote_p.stdin.flush()
