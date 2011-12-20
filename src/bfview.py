#!/usr/bin/python

# bfsync: Big File synchronization tool

# Copyright (C) 2011 Stefan Westerfeld
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

import sys
import os
import subprocess
from bfsync.utils import *
from bfsync.xzutils import xzcat

if len (sys.argv) == 2:
  obj_name = os.path.join (find_repo_dir(), "objects", make_object_filename (sys.argv[1]))
  diff = xzcat (obj_name)
else:
  diff = sys.stdin.read()

changes = parse_diff (diff)

for c in changes:
  print "|".join (c)
