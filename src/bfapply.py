#!/usr/bin/python

from bfsync.utils import *
from bfsync.applyutils import apply
import sys

patch = open (sys.argv[1], "r")

repo = cd_repo_connect_db()
apply (repo, patch.read())


