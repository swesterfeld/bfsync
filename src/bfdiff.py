#!/usr/bin/python

# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

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


