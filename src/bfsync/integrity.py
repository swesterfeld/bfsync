# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

import bfsyncdb

from bfsync.utils import *
from bfsync.StatusLine import status_line, OutputSubsampler

def check_version_ranges (repo):
  vr_errors = []
  object_count = 0
  outss = OutputSubsampler()

  def update_status():
    status_line.update ("INTEGRITY: phase 3/3: checking version ranges: %d" % object_count)

  # check if version ranges overlap
  #  - input:  sorted list of [ vmin, vmax ] pairs
  #  - output: True iff version ranges are ok (do not overlap)
  def check_vmin_vmax_list (vlist):
    for i in range (len (vlist) - 1):
      j = i + 1
      (vmin_i, vmax_i) = vlist[i]
      (vmin_j, vmax_j) = vlist[j]
      if vmax_i >= vmin_j:
        return False
    return True

  ai = bfsyncdb.AllINodesIterator (repo.bdb)
  while True:
    id = ai.get_next()
    if not id.valid:
      break
    inodes = repo.bdb.load_all_inodes (id)
    inode_list = []
    for inode in inodes:
      inode_list.append  ([inode.vmin, inode.vmax])
    inode_list = sorted (inode_list, key = lambda irange: irange[0])
    if not check_vmin_vmax_list (inode_list):
      for irange in inode_list:
        vr_errors += [ "INODE %s %s" % (id.str(), irange) ]

    link_by_name = dict()
    links = repo.bdb.load_all_links (id)
    for link in links:
      #print id.str(), link.vmin, link.vmax, link.name
      if not link_by_name.has_key (link.name):
        link_by_name[link.name] = []
      assert link.vmin <= link.vmax
      link_by_name[link.name].append ([link.vmin, link.vmax])
    for name in link_by_name:
      #print id.str(), name
      link_list = sorted (link_by_name[name], key = lambda lrange: lrange[0])
      if not check_vmin_vmax_list (link_list):
        for lrange in link_list:
          vr_errors += [ "LINK %s %s %s" % (id.str(), name, lrange) ]
    object_count += 1
    if outss.need_update():
      update_status()
  del ai

  update_status()
  status_line.cleanup()
  return vr_errors

def check_integrity (repo, args):
  il_errors = bfsyncdb.check_inodes_links_integrity (repo.bdb)
  vr_errors = check_version_ranges (repo)

  for e in il_errors:
    print e
  for e in vr_errors:
    print e
