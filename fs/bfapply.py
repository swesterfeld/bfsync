#!/usr/bin/python

from utils import *
from applyutils import apply
import sys

repo = cd_repo_connect_db()

apply (repo, sys.stdin.read())


