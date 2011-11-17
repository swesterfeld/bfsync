#!/usr/bin/python

import sqlite3
import CfgParser
import sys
from utils import *
from diffutils import diff

repo = cd_repo_connect_db()
conn = repo.conn
repo_path = repo.path
c = conn.cursor()

version_a = int (sys.argv[1])
version_b = int (sys.argv[2])

diff (c, version_a, version_b, sys.stdout)


