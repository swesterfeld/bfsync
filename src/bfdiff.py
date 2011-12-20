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

import sqlite3
import sys
import bfsync.CfgParser
from bfsync.utils import *
from bfsync.diffutils import diff

repo = cd_repo_connect_db()
conn = repo.conn
repo_path = repo.path
c = conn.cursor()

version_a = int (sys.argv[1])
version_b = int (sys.argv[2])

diff (c, version_a, version_b, sys.stdout)


