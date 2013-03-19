#!/usr/bin/python

# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

from bfsync.main import main
import bfsyncdb

if False:
  import cProfile
  cProfile.run ("main()", "/tmp/bfsync-profile")
  print bfsyncdb.time_prof_result()
else:
  main()
