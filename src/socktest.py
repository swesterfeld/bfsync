#!/usr/bin/python

# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

from bfsync.ServerConn import ServerConn
import sys

server_conn = ServerConn (sys.argv[1])

msg = [ "print" ]
i = 0
while i < 10:
  i += 1
  msg += [ "Hello world %d!\n" % i ]
  result = server_conn.process_call (msg)
  print 'Received result ', result
