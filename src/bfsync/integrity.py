# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

import bfsyncdb

def check_integrity (repo, args):
  il_errors = bfsyncdb.check_inodes_links_integrity (repo.bdb)
  for e in il_errors:
    print e
