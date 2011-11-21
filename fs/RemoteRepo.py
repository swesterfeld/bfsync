import subprocess
import cPickle
import os

LOCAL = 1
SSH = 2

class RemoteRepo:
  def __init__ (self, url):
    self.url = url

    url_list = url.split (":")
    if len (url_list) == 1:
      self.path = os.path.abspath (url)
      self.conn = LOCAL
      command = ["bfsync2", "remote", self.path]
    elif len (url_list) == 2:
      self.host = url_list[0]
      self.path = url_list[1]
      self.conn = SSH
      command = ["ssh", self.host, "bfsync2", "remote", self.path]
    else:
      raise Exception ("unknown url type %s, neither local nor remote?" % url)
    self.remote_p = subprocess.Popen (command, stdin=subprocess.PIPE, stdout=subprocess.PIPE)

  def get_history (self):
    self.remote_p.stdin.write ("history\n")
    result_len = int (self.remote_p.stdout.readline())
    remote_history = cPickle.loads (self.remote_p.stdout.read (result_len))
    return remote_history

  def update_history (self, delta_history):
    self.remote_p.stdin.write ("update-history\n")
    delta_hist_str = cPickle.dumps (delta_history)
    self.remote_p.stdin.write ("%s\n%s" % (len (delta_hist_str), delta_hist_str))
    self.remote_p.stdin.flush()
    result_len = int (self.remote_p.stdout.readline())
    return self.remote_p.stdout.read (result_len)

  def need_objects (self):
    self.remote_p.stdin.write ("need-objects\n")
    result_len = int (self.remote_p.stdout.readline())
    need_objs = cPickle.loads (self.remote_p.stdout.read (result_len))
    return need_objs

  def ls (self):
    self.remote_p.stdin.write ("ls\n")
    result_len = int (self.remote_p.stdout.readline())
    remote_list = cPickle.loads (self.remote_p.stdout.read (result_len))
    return remote_list

  def get_objects (self, tlist):
    self.remote_p.stdin.write ("send\n")
    tlist.send_list (self.remote_p.stdin)
    tlist.receive_files (self.remote_p.stdout, True)

  def put_objects (self, tlist):
    self.remote_p.stdin.write ("receive\n")
    tlist.send_list (self.remote_p.stdin)
    tlist.send_files (self.remote_p.stdin, True)
    self.remote_p.stdin.flush()
