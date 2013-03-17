# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

import os
from utils import *
import bfsyncdb

def follow_link (repo, inode, path_part, VERSION):
  links = repo.bdb.load_links (inode.id, VERSION)
  for link in links:
    if link.name == path_part:
      return repo.bdb.load_inode (link.inode_id, VERSION)
  return None

def get_filename (repo, filename, VERSION):
  inode = repo.bdb.load_inode (bfsyncdb.id_root(), VERSION)
  for path_part in os.path.split (os.path.join (repo.start_dir, filename)):
    inode = follow_link (repo, inode, path_part, VERSION)
    if not inode:
      return None

  if inode.new_file_number > 0:
    return repo.make_number_filename (inode.new_file_number)

  file_number = repo.bdb.load_hash2file (inode.hash)
  if file_number != 0:
    return repo.make_number_filename (file_number)

def text_diff (repo, args):
  VERSION = repo.first_unused_version()
  for filename in args:
    full_filename = os.path.join (repo.start_dir, filename)
    last_filename = get_filename (repo, filename, VERSION - 1)
    if last_filename is None:
      raise BFSyncError ("error: %s not found in previous version (%d)" % (full_filename, VERSION - 1))
    this_filename = get_filename (repo, filename, VERSION)
    if this_filename is None:
      raise BFSyncError ("error: %s not found in previous version (%d)" % (this_filename, VERSION))
    os.system ("diff -u %s %s --label 'a/%s' --label 'b/%s'" % (last_filename, this_filename, full_filename, full_filename))
