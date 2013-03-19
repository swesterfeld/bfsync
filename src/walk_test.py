#!/usr/bin/python

# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

from bfsync.utils import *
import sys
import os

os.chdir (sys.argv[1])
repo = cd_repo_connect_db()

def walk (id, prefix, version):
  inode = repo.bdb.load_inode (id, version)
  if inode.valid:
    id_str = id.str()
    if prefix == "":
      name = "/"
    else:
      name = prefix
    #print name
    if inode.type == bfsyncdb.FILE_DIR:
      links = repo.bdb.load_links (id, version)
      for link in links:
        inode_name = prefix + "/" + link.name
        walk (link.inode_id, inode_name, version)

walk (bfsyncdb.id_root(), "", 2)
