#!/usr/bin/python

# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

import socket

class ServerConn:
  def decode (self, data):
    content_size = -1
    skip = 0
    while skip < len (data):
      if data[skip] == '\0':
        content_size = int (data[0:skip])
        break
      skip += 1
    if content_size != -1:
      if ((skip + 1 + content_size) == len (data)):
        split_data = data[skip + 1:].split ("\0")
        return split_data[:-1]
    return False # need more data

  def encode (self, msg):
    serialized = ""
    for m in msg:
      serialized += m
      serialized += "\0"
    serialized = str (len (serialized)) + "\0" + serialized
    return serialized

  def process_call (self, msg):
    request_data = self.encode (msg)
    self.conn_socket.send (request_data)
    data = ""
    while True:
      data += self.conn_socket.recv (1024)
      result = self.decode (data)
      if result != False:
        return result

  def get_lock (self):
    result = self.process_call (["get-lock"])
    if result and len (result) == 1:
      if result[0] == "ok":
        return
      else:
        raise Exception (result[0])
    raise Exception ("ServerConn: unable to get lock (bad response received)")

  def save_changes (self):
    result = self.process_call (["save-changes"])
    if result and len (result) == 1:
      if result[0] == "ok":
        return
      else:
        raise Exception (result[0])
    raise Exception ("ServerConn: unable to save changes (bad response received)")

  def clear_cache (self):
    result = self.process_call (["clear-cache"])
    if result and len (result) == 1:
      if result[0] == "ok":
        return
      else:
        raise Exception (result[0])
    raise Exception ("ServerConn: unable to clear cache (bad response received)")

  def update_read_only (self):
    result = self.process_call (["update-read-only"])
    if result and len (result) == 1:
      if result[0] == "ok":
        return
      else:
        raise Exception (result[0])
    raise Exception ("ServerConn: unable to update read only mode (bad response received)")

  def close (self):
    self.conn_socket.close()
    self.conn_socket = None

  def __init__ (self, repo_dir):
    self.conn_socket = socket.socket (socket.AF_UNIX, socket.SOCK_STREAM)
    self.conn_socket.connect (repo_dir + "/socket")
