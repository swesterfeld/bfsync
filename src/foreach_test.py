#!/usr/bin/python

# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

from bfsync.utils import *
import sys
import os

os.chdir (sys.argv[1])
repo = cd_repo_connect_db()

def link_callback (x):
  print "L:", x.inode_id, x.name

def inode_callback (x):
  print "I:", x.id

repo.foreach_inode_link (1, inode_callback, link_callback)
