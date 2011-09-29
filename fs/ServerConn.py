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
        return data[skip + 1:-1].split ("\0")
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
      if result:
        return result

  def __init__ (self, repo_dir):
    self.conn_socket = socket.socket (socket.AF_UNIX, socket.SOCK_STREAM)
    self.conn_socket.connect (repo_dir + "/.bfsync/socket")
