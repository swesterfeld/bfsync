import subprocess
import pickle
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
    elif len (url_list) == 2:
      self.host = url_list[0]
      self.path = url_list[1]
      self.conn = SSH
    else:
      raise Exception ("unknown url type %s, neither local nor remote?" % url)

  def get_history (self):
    if self.conn == LOCAL:
      command = ["bfsync2", "remote-history", self.path]
    else:
      command = ["ssh", self.host, "bfsync2", "remote-history", self.path]
    remote_p = subprocess.Popen (command, stdout=subprocess.PIPE).communicate()[0]
    remote_history = pickle.loads (remote_p)
    return remote_history

  def ls (self):
    if self.conn == LOCAL:
      command = ["bfsync2", "remote-ls", self.path]
    else:
      command = ["ssh", self.host, "bfsync2", "remote-ls", self.path]

    remote_p = subprocess.Popen (command, stdout=subprocess.PIPE).communicate()[0]
    remote_list = pickle.loads (remote_p)
    return remote_list

  def get_objects (self, tlist):
    if self.conn == LOCAL:
      command = [ "bfsync2", "remote-send" ]
    else:
      command = [ "ssh", self.host, "bfsync2", "remote-send" ]

    remote_send_p = subprocess.Popen (command, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    tlist.send_list (remote_send_p.stdin)
    tlist.receive_files (remote_send_p.stdout, True)
