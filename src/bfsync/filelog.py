# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

import os
import datetime
import argparse
from utils import *
import bfsyncdb

# "a/b/c" => [ "a", "b", "c" ]
def split_path (filename):
  if filename == "":
    return []

  dirname, basename = os.path.split (filename)
  return split_path (dirname) + [ basename ]

def strip_bfsync_commits_dir (path):
  path_parts = split_path (path)
  if len (path_parts) > 3:
    if path_parts[0] == ".bfsync" and path_parts[1] == "commits":
      return os.path.join (*path_parts[3:])
  return path

def get_inode (inode_repo, filename, version):
  inode = inode_repo.load_inode (bfsyncdb.id_root(), version)
  assert (inode.valid())      # root inode should always be there

  for path_part in split_path (filename):
    inode = inode.get_child (version, path_part)
    if not inode.valid():
      return None

  return inode

def file_log (repo, args):
  parser = argparse.ArgumentParser (prog='bfsync file-log')
  parser.add_argument ("-a", action="store_true", dest="all", default=False, help='show all versions (including deleted versions)')
  parser.add_argument ("-v", action="store_true", dest="verbose", default=False, help='show details for each version')
  parser.add_argument ("file")
  parsed_args = parser.parse_args (args)

  full_filename = strip_bfsync_commits_dir (os.path.join (repo.start_dir, parsed_args.file))
  VERSION = repo.first_unused_version()
  deleted_versions = repo.get_deleted_version_set()
  inode_repo = bfsyncdb.INodeRepo (repo.bdb)

  print "-" * 80

  last_attrs = ('')

  for version in range (1, VERSION):
    if version not in deleted_versions or parsed_args.all:
      inode = get_inode (inode_repo, full_filename, version)
      if inode:
        attrs = (inode.hash(), inode.size(), inode.mtime())
        if attrs != last_attrs:
          # load history entry
          hentry = repo.bdb.load_history_entry (version)
          msg = hentry.message
          msg = msg.strip()

          print "%4d   Hash   %s" % (version, inode.hash())
          print "       Size   %s" % inode.size()
          print "       MTime  %s" % datetime.datetime.fromtimestamp (inode.mtime()).strftime ("%F %H:%M:%S")
          if repo.mount_point:
            print "       Path   %s" % os.path.join (repo.mount_point, ".bfsync", "commits", "%d" % version, full_filename)

          if parsed_args.verbose:
            print
            print "       Commit-Author %s" % hentry.author
            print "       Commit-Date   %s" % datetime.datetime.fromtimestamp (hentry.time).strftime ("%A, %F %H:%M:%S")

            tag_list = []
            tags = repo.bdb.list_tags (hentry.version)
            for t in tags:
              values = repo.bdb.load_tag (hentry.version, t)
              for v in values:
                tag_list.append ("%s=%s" % (t, v))

            if tag_list:
              print "       Commit-Tags   %s" % ",".join (tag_list)
          print
          for line in msg.split ("\n"):     # commit message
            print "       %s" % line

          print "-" * 80
          last_attrs = attrs
