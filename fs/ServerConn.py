#!/usr/bin/python

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

  def add_new (self, new_list):
    result = self.process_call ([ "add-new" ] + new_list)
    if result and len (result) == 1:
      if result[0] == "ok":
        return
      else:
        raise Exception (result[0])
    raise Exception ("ServerConn: unable to add new file(s) (bad response received)")

  def __init__ (self, repo_dir):
    self.conn_socket = socket.socket (socket.AF_UNIX, socket.SOCK_STREAM)
    self.conn_socket.connect (repo_dir + "/.bfsync/socket")
