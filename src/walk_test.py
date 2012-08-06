#!/usr/bin/python

# bfsync: Big File synchronization tool

# Copyright (C) 2011-2012 Stefan Westerfeld
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

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
