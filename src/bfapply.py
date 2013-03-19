#!/usr/bin/python

# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

from bfsync.utils import *
from bfsync.applyutils import apply
import sys

patch = open (sys.argv[1], "r")

repo = cd_repo_connect_db()
apply (repo, patch.read())


