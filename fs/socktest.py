#!/usr/bin/python

from ServerConn import ServerConn

server_conn = ServerConn ("test")

msg = [ "print" ]
i = 0
while i < 10:
  i += 1
  msg += [ "Hello world %d!\n" % i ]
  result = server_conn.process_call (msg)
  print 'Received result ', result
