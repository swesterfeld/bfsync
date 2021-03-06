# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

from ServerConn import ServerConn
from StatusLine import status_line, OutputSubsampler
from diffutils import diff
from utils import *
from xzutils import xz
from HashCache import hash_cache
from journal import mk_journal_entry, queue_command, CMD_DONE, CMD_AGAIN

import os
import pwd
import time
import hashlib
import cPickle
import subprocess

# in case the repo is not mounted, we don't need a ServerConn
#
# this fake server conn is only to be used during "clone"
class NoServerConn:
  def get_lock (self):
    pass

  def save_changes (self):
    pass

  def clear_cache (self):
    pass

  def close (self):
    pass

def launch_editor (filename):
  editor = os.getenv ("VISUAL")
  if editor is None:
    editor = os.getenv ("EDITOR")
  if editor is None:
    editor = "vi"
  subprocess.call ([editor, filename])

def commit_msg_ok (filename):
  file = open (filename, "r")
  result = False
  for line in file:
    line = line.strip()
    if len (line):
      if line[0] == "#":
        pass
      else:
        result = True
  file.close()
  return result

class INodeInfo:
  def __init__ (self, info):
    if info:
      self.info = info
      self.size = info[0]
      self.type = type2str (info[1])
      self.names = self.info[2]

  def get_name (self):
    if len (self.info[2]) == 1:
      return self.info[2][0]
    else:
      return "%s" % self.info[2]

def type2str (type):
  if type == bfsyncdb.FILE_REGULAR:
    return "file"
  if type == bfsyncdb.FILE_SYMLINK:
    return "symlink"
  if type == bfsyncdb.FILE_DIR:
    return "dir"
  if type == bfsyncdb.FILE_FIFO:
    return "fifo"
  if type == bfsyncdb.FILE_SOCKET:
    return "socket"
  if type == bfsyncdb.FILE_BLOCK_DEV:
    return "blockdev"
  if type == bfsyncdb.FILE_CHAR_DEV:
    return "chardev"
  return "unknown"

# Status message generation
#
# Time complexity:
# ================
# - O(t) where t is the total number of inodes stored in the repository
#
# Memory usage:
# =============
# - O(c) where c is the number of changed inodes
#
def gen_status (repo):
  change_list = []

  n_changed = 0
  bytes_changed_old = 0
  bytes_changed_new = 0
  n_added = 0
  bytes_added = 0
  n_deleted = 0
  bytes_deleted = 0
  n_renamed = 0

  DEBUG_MEM = False

  if repo.check_uncommitted_changes():
    VERSION = repo.first_unused_version()

    if DEBUG_MEM:
      print_mem_usage ("gen_status: start")

    changed_dict = dict()
    changed_it = bfsyncdb.ChangedINodesIterator (repo.bdb)
    while True:
      id = changed_it.get_next()
      if not id.valid:
        break
      changed_dict[id.str()] = (None, None)
    del changed_it

    if DEBUG_MEM:
      print_mem_usage ("gen_status: after id iteration")

    def walk (id, prefix, version, new_old):
      inode = repo.bdb.load_inode (id, version)
      if inode.valid:
        id_str = id.str()
        if changed_dict.has_key (id_str):
          new, old = changed_dict[id_str]
          if new_old == 0:
            my = new
          else:
            my = old
          if not my:
            if inode.hash == "new":
              try:
                filename = repo.make_number_filename (inode.new_file_number)
                size = os.path.getsize (filename)
              except:
                size = 0
            else:
              size = inode.size
            my = (size, inode.type, [])
          if prefix == "":
            name = "/"
          else:
            name = prefix
          my[2].append (name)
          if new_old == 0:
            changed_dict[id_str] = (my, old)
          else:
            changed_dict[id_str] = (new, my)
        if inode.type == bfsyncdb.FILE_DIR:
          links = repo.bdb.load_links (id, version)
          for link in links:
            inode_name = prefix + "/" + link.name
            walk (link.inode_id, inode_name, version, new_old)

    walk (bfsyncdb.id_root(), "", VERSION, 0)
    walk (bfsyncdb.id_root(), "", VERSION - 1, 1)

    if DEBUG_MEM:
      print_mem_usage ("gen_status: after walk()")

    for id in changed_dict:
      new_tuple, old_tuple = changed_dict[id]
      new = INodeInfo (new_tuple)
      old = INodeInfo (old_tuple)
      if new_tuple:
        if old_tuple: # NEW & OLD
          if old.names != new.names:
            change_list.append (("R!", new.type, "%s => %s" % (old.get_name(), new.get_name())))
            n_renamed += 1
          else:
            change_list.append (("!", new.type, new.get_name()))
          n_changed += 1
          bytes_changed_old += old.size
          bytes_changed_new += new.size
        else: # NEW
          change_list.append (("+", new.type , new.get_name()))
          bytes_added += new.size
          n_added += 1
      elif old_tuple: # OLD
        change_list.append (("-", old.type, old.get_name()))
        n_deleted += 1
        bytes_deleted += old.size

    if DEBUG_MEM:
      print_mem_usage ("gen_status: after change list gen")

  status = []
  status += [ "+ %d objects added (%s)." % (n_added, format_size1 (bytes_added)) ]
  status += [ "! %d objects changed (%s => %s)." % (n_changed,
                                                    format_size1 (bytes_changed_old),
                                                    format_size1 (bytes_changed_new)) ]
  status += [ "- %d objects deleted (%s)." % (n_deleted, format_size1 (bytes_deleted)) ]
  status += [ "R %d objects renamed." % n_renamed ]
  status += [ "" ]

  change_list.sort (key = lambda x: x[2])
  for change in change_list:
    status += [ "%2s %-8s %s" % change ]

  if DEBUG_MEM:
    print_mem_usage ("gen_status: end")

  return status

def init_commit_msg (repo, filename):
  VERSION = repo.first_unused_version()

  msg_file = open (filename, "w")
  msg_file.write ("\n# commit version %d\n#\n" % VERSION)
  status = gen_status (repo)
  for s in status:
    msg_file.write ("# %s\n" % s)
  msg_file.close()

def get_author():
  username = pwd.getpwuid (os.getuid()).pw_name
  hostname = os.uname()[1]
  return "%s@%s" % (username, hostname)

class RevertState:
  pass

class RevertCommand:
  def make_id_list (self):
    self.repo.bdb.begin_transaction()
    self.state.id_list_filename = self.repo.make_temp_name()
    self.repo.bdb.commit_transaction()

    id_list_file = open (self.state.id_list_filename, "w")

    ai = bfsyncdb.AllINodesIterator (self.repo.bdb)
    while True:
      id = ai.get_next()
      if not id.valid:
        break
      id_list_file.write ("%s\n" % id.str())

    id_list_file.close()
    del ai

  def restart (self, repo):
    self.repo = repo

    # lock repo to allow modifications
    self.server_conn = ServerConn (repo.path)
    self.server_conn.get_lock()
    return True

  def start (self, repo, VERSION, verbose):
    self.state = RevertState()
    self.state.verbose = verbose
    self.repo = repo

    # VERSION == -1 <=> revert to current version (discard new entries from FuSE fs)
    if VERSION == -1:
      self.state.VERSION = repo.first_unused_version() - 1
    else:
      self.state.VERSION = VERSION

    if verbose:
      print "reverting to version %d..." % self.state.VERSION

    # lock repo to allow modifications
    self.server_conn = ServerConn (repo.path)
    self.server_conn.get_lock()

    self.make_id_list()
    return True

  def execute (self):
    id_list_file = open (self.state.id_list_filename, "r")

    OPS = 0  # to keep number of operations per transaction below a pre-defined limit

    self.repo.bdb.begin_transaction()
    for id_str in id_list_file:
      id = bfsyncdb.ID (id_str.strip())
      if not id.valid:
        raise Exception ("found invalid id during revert")

      inodes = self.repo.bdb.load_all_inodes (id)
      OPS += 1 # even read access will create new locks

      for inode in inodes:
        if inode.vmin > self.state.VERSION:
          self.repo.bdb.delete_inode (inode)
          OPS += 1
        elif inode.vmax != bfsyncdb.VERSION_INF and inode.vmax >= self.state.VERSION:
          self.repo.bdb.delete_inode (inode)
          inode.vmax = bfsyncdb.VERSION_INF
          self.repo.bdb.store_inode (inode)
          OPS += 1

      links = self.repo.bdb.load_all_links (id)
      del_links = bfsyncdb.LinkVector()
      for link in links:
        if link.vmin > self.state.VERSION:
          del_links.push_back (link)
          OPS += 1
        elif link.vmax != bfsyncdb.VERSION_INF and link.vmax >= self.state.VERSION:
          del_links.push_back (link)
          link.vmax = bfsyncdb.VERSION_INF
          self.repo.bdb.store_link (link)
          OPS += 1
      # deleting all links in one step is faster than deleting them one-by-one
      if len (del_links) > 0:
        self.repo.bdb.delete_links (del_links)

      if OPS >= 20000:
        self.repo.bdb.commit_transaction()
        self.repo.bdb.begin_transaction()
        OPS = 0

    self.repo.bdb.commit_transaction()

    ## clear changed inodes
    while True:
      self.repo.bdb.begin_transaction()
      deleted = self.repo.bdb.clear_changed_inodes (20000)
      self.repo.bdb.commit_transaction()

      if deleted == 0: # changed inodes table empty
        break

    ## fix history
    self.repo.bdb.begin_transaction()
    v = 1
    while True:
      he = self.repo.bdb.load_history_entry (v)
      if not he.valid:
        break
      if he.version > self.state.VERSION:
        self.repo.bdb.delete_history_entry (v)
      v += 1
    self.repo.bdb.commit_transaction()

    # we modified the db, so the fs needs to reload everything
    # in-memory cached items will not be correct
    self.server_conn.clear_cache()

    # this will release the lock
    self.server_conn.close()
    return CMD_DONE

  def get_state (self):
    return self.state

  def get_operation (self):
    return "revert"

  def set_state (self, state):
    self.state = state

def revert (repo, VERSION, verbose = True):
  cmd = RevertCommand()

  if not cmd.start (repo, VERSION, verbose):
    return False

  queue_command (cmd)
  return True

class CommitState:
  pass

class CommitCommand:
  EXEC_PHASE_SCAN     = 1
  EXEC_PHASE_FIX      = 2
  EXEC_PHASE_ADD      = 3
  EXEC_PHASE_DIFF     = 4
  EXEC_PHASE_HISTORY  = 5
  EXEC_PHASE_CLEANUP  = 6

  def __init__ (self):
    self.outss = OutputSubsampler()
    self.DEBUG_MEM = False

  # update work to do
  def scan_inode (self, inode):
    if inode.hash == "new":
      filename = self.repo.make_number_filename (inode.new_file_number)
      self.state.total_file_size += os.path.getsize (filename)
      self.state.total_file_count += 1
      if self.state.verbose and self.outss.need_update():
        status_line.update ("scanning file %d (total %s)" % (
          self.state.total_file_count, format_size1 (self.state.total_file_size)))

  # create file with changed IDs
  def make_id_list (self):
    self.state.total_file_size = 0
    self.state.total_file_count = 0

    self.repo.bdb.begin_transaction()
    id_list_filename = self.repo.make_temp_name()
    self.repo.bdb.commit_transaction()

    id_list_file = open (id_list_filename, "w")
    changed_it = bfsyncdb.ChangedINodesIterator (self.repo.bdb)
    while True:
      id = changed_it.get_next()
      if not id.valid:
        break
      id_list_file.write (id.str() + "\n")
      inode = self.repo.bdb.load_inode (id, self.VERSION)
      if inode.valid:
        self.scan_inode (inode)
    del changed_it
    id_list_file.close()
    return id_list_filename

  # compute SHA1 hash of one file
  def hash_one (self, filename):
    file = open (filename, "r")
    hash = hashlib.sha1()
    eof = False
    while not eof:
      data = file.read (256 * 1024)
      if data == "":
        eof = True
      else:
        hash.update (data)
        self.state.bytes_done += len (data)
        if self.state.verbose and self.outss.need_update():
          self.update_status()
    file.close()
    if self.state.verbose and self.outss.need_update():
      self.update_status()
    return hash.hexdigest()

  # update SHA1 hashing status
  def update_status (self):
    elapsed_time = max (time.time() - self.start_time, 1)
    bytes_per_sec = max (self.state.bytes_done / elapsed_time, 1)
    eta = int ((self.state.total_file_size - self.state.bytes_done) / bytes_per_sec)
    status_line.update ("adding file %d/%d    %s    %.1f%%   %s   ETA: %s" % (
        self.state.files_added, self.state.total_file_count,
        format_size (self.state.bytes_done, self.state.total_file_size),
        self.state.bytes_done * 100.0 / max (self.state.total_file_size, 1),
        format_rate (bytes_per_sec),
        format_time (eta)
      ))

  def make_commit_msg (self, commit_args):
    # commit message
    have_message = False
    if commit_args:
      if commit_args.get ("message"):
        have_message = True

    if self.state.verbose and not have_message:
      self.repo.bdb.begin_transaction()
      commit_msg_filename = self.repo.make_temp_name()
      self.repo.bdb.commit_transaction()

      init_commit_msg (self.repo, commit_msg_filename)
      launch_editor (commit_msg_filename)
      if not commit_msg_ok (commit_msg_filename):
        raise BFSyncError ("commit: empty commit message - aborting commit")
      commit_msg = ""
      commit_msg_file = open (commit_msg_filename, "r")
      for line in commit_msg_file:
        if line[0] != "#":
          commit_msg += line
      commit_msg_file.close()
    else:
      commit_msg = "commit message"

    commit_author = None
    commit_time = None

    if commit_args:
      commit_author = commit_args.get ("author")
      commit_time = commit_args.get ("time")
      xcommit_msg = commit_args.get ("message")
      if xcommit_msg is not None:
        commit_msg = xcommit_msg

    if commit_author is None:
      commit_author = get_author()

    if commit_time is None:
      commit_time = int (time.time())

    self.state.commit_author = commit_author
    self.state.commit_msg    = commit_msg
    self.state.commit_time   = commit_time

  def start (self, repo, commit_args, server, verbose):
    self.state = CommitState()
    self.state.exec_phase = self.EXEC_PHASE_SCAN
    self.state.server = server
    self.state.verbose = verbose

    if self.DEBUG_MEM:
      print_mem_usage ("commit start")

    if self.state.verbose:
      status_line.set_op ("COMMIT")

    self.repo = repo
    self.VERSION = self.repo.first_unused_version()

    # lock repo to allow modifications
    if self.state.server:
      self.server_conn = ServerConn (repo.path)
    else:
      self.server_conn = NoServerConn()
    self.server_conn.get_lock()

    # flush happened due to server lock
    self.make_commit_msg (commit_args)

    if self.DEBUG_MEM:
      print_mem_usage ("after commit msg")

    return True

  def restart (self, repo):
    self.repo = repo
    self.VERSION = self.repo.first_unused_version()

    if self.state.verbose:
      status_line.set_op ("COMMIT")

    # lock repo to allow modifications
    if self.state.server:
      self.server_conn = ServerConn (repo.path)
    else:
      self.server_conn = NoServerConn()
    self.server_conn.get_lock()
    return True

  def execute (self):
    if self.state.exec_phase == self.EXEC_PHASE_SCAN:
      self.state.id_list_filename = self.make_id_list()
      self.state.files_added = 0
      self.state.bytes_done = 0
      self.state.previous_time = 0

      self.state.exec_phase += 1

      # create new journal entry
      self.repo.bdb.begin_transaction()
      mk_journal_entry (self.repo)
      self.repo.bdb.commit_transaction()

      if self.DEBUG_MEM:
        print_mem_usage ("after id list scanning")

    if self.state.exec_phase == self.EXEC_PHASE_FIX:
      # in some (very rare) cases, bfsyncfs will copy-on-write the file associated with the inode
      # without modifying the file data or inode attributes
      #
      # the "fix unchanged inodes" pass is designed to reset the inode to the old version in this
      # case, before committing, which avoids problems with the actual diff created later on
      OPS = 0
      self.repo.bdb.begin_transaction()

      id_list_file = open (self.state.id_list_filename, "r")
      for id_str in id_list_file:
        id = bfsyncdb.ID (id_str.strip())
        inode_new = self.repo.bdb.load_inode (id, self.VERSION)
        inode_old = self.repo.bdb.load_inode (id, self.VERSION - 1)

        if inode_old.valid and inode_new.valid:
          if inode_old.vmin != inode_new.vmin or inode_old.vmax != inode_new.vmax:
            def inode_fields (inode):
              return ( inode.id.str(),
                      inode.uid, inode.gid,
                      inode.mode, inode.type,
                      inode.link, inode.size,
                      inode.major, inode.minor,
                      inode.nlink,
                      inode.ctime, inode.ctime_ns,
                      inode.mtime, inode.mtime_ns )

            if inode_fields (inode_old) == inode_fields (inode_new) and inode_new.hash == "new":
              filename = self.repo.make_number_filename (inode_new.new_file_number)

              if inode_old.hash == hash_cache.compute_hash (filename):
                self.repo.bdb.delete_inode (inode_new)
                self.repo.bdb.delete_inode (inode_old)
                inode_old.vmax = bfsyncdb.VERSION_INF
                self.repo.bdb.store_inode (inode_old)
                self.repo.bdb.add_deleted_file (inode_new.new_file_number)

        OPS += 1
        if OPS >= 20000:
          OPS = 0
          self.repo.bdb.commit_transaction()
          self.repo.bdb.begin_transaction()

      id_list_file.close()

      self.state.exec_phase += 1

      # create new journal entry
      mk_journal_entry (self.repo)
      self.repo.bdb.commit_transaction()

    if self.state.exec_phase == self.EXEC_PHASE_ADD:
      self.start_time = time.time() - self.state.previous_time

      def process_inodes (inodes):
        inodes.sort (key = lambda inode: inode.new_file_number)
        for inode in inodes:
          self.state.files_added += 1
          filename = self.repo.make_number_filename (inode.new_file_number)
          hash = self.hash_one (filename)
          size = os.path.getsize (filename)
          self.repo.bdb.delete_inode (inode)

          if self.repo.bdb.load_hash2file (hash) == 0:
            self.repo.bdb.store_hash2file (hash, inode.new_file_number)
          else:
            self.repo.bdb.add_deleted_file (inode.new_file_number)

          inode.hash = hash
          inode.size = size
          inode.new_file_number = 0
          self.repo.bdb.store_inode (inode)

      # process files to add in small chunks
      id_list_file = open (self.state.id_list_filename, "r")

      self.repo.bdb.begin_transaction()
      inodes = []
      OPS = 0
      for id_str in id_list_file:
        id = bfsyncdb.ID (id_str.strip())
        if not id.valid:
          raise Exception ("found invalid id during commit")
        inode = self.repo.bdb.load_inode (id, self.VERSION)
        if inode.valid and inode.hash == "new":
          inodes.append (inode)

        OPS += 1
        if OPS >= 20000:
          OPS = 0
          process_inodes (inodes)
          inodes = []
          self.state.previous_time = time.time() - self.start_time
          mk_journal_entry (self.repo)
          self.repo.bdb.commit_transaction()
          self.repo.bdb.begin_transaction()

      process_inodes (inodes)
      self.state.exec_phase += 1
      mk_journal_entry (self.repo)
      self.repo.bdb.commit_transaction()

      id_list_file.close()

      if self.state.verbose:
        self.update_status()
        status_line.cleanup()

      if self.DEBUG_MEM:
        print_mem_usage ("after add")

    if self.state.exec_phase == self.EXEC_PHASE_DIFF:
      # compute commit diff
      if self.state.verbose:
        status_line.update ("computing changes")

      self.repo.bdb.begin_transaction()
      diff_filename = self.repo.make_temp_name()
      self.repo.bdb.commit_transaction()

      diff_file = open (diff_filename, "w")
      diff (self.repo, diff_file)
      diff_file.close()

      if os.path.getsize (diff_filename) != 0:
        xz (diff_filename)
        self.state.commit_hash = move_file_to_objects (self.repo, diff_filename + ".xz")
        commit_size_ok = True
      else:
        commit_size_ok = False

      if self.state.verbose:
        status_line.update ("computing changes: done")
        status_line.cleanup()

      if commit_size_ok:
        # next step: write history entry
        self.state.exec_phase += 1
      else:
        print "Nothing to commit."

        # skip history entry
        self.state.exec_phase = self.EXEC_PHASE_CLEANUP

      # create new journal entry
      self.repo.bdb.begin_transaction()
      mk_journal_entry (self.repo)
      self.repo.bdb.commit_transaction()

      if self.DEBUG_MEM:
        print_mem_usage ("after diff")

      ### => we have self.state.commit_hash now (and it points to the diff object)

    if self.state.exec_phase == self.EXEC_PHASE_HISTORY:
      self.repo.bdb.begin_transaction()
      self.repo.bdb.store_history_entry (self.VERSION, self.state.commit_hash,
                                                       self.state.commit_author,
                                                       self.state.commit_msg,
                                                       self.state.commit_time)
      self.state.exec_phase += 1
      mk_journal_entry (self.repo)
      self.repo.bdb.commit_transaction()

      if self.DEBUG_MEM:
        print_mem_usage ("after history update")

    if self.state.exec_phase == self.EXEC_PHASE_CLEANUP:
      ## remove duplicates
      files = self.repo.bdb.load_deleted_files()
      for file_number in files:
        file_name = self.repo.make_number_filename (file_number)
        if os.path.exists (file_name):
          os.remove (file_name)

      ## reset deleted_files table
      while True:
        self.repo.bdb.begin_transaction()
        deleted = self.repo.bdb.clear_deleted_files (20000)
        self.repo.bdb.commit_transaction()

        if deleted == 0: # deleted files table empty
          break

      ## clear changed inodes
      while True:
        self.repo.bdb.begin_transaction()
        deleted = self.repo.bdb.clear_changed_inodes (20000)
        self.repo.bdb.commit_transaction()

        if deleted == 0: # changed inodes table empty
          break

      if self.DEBUG_MEM:
        print_mem_usage ("after cleanup")

      # we modified the db, so the fs needs to reload everything
      # in-memory cached items will not be correct
      self.server_conn.clear_cache()

      # this will release the lock
      self.server_conn.close()
    return CMD_DONE

  def get_state (self):
    return self.state

  def get_operation (self):
    return "commit"

  def set_state (self, state):
    self.state = state

def commit (repo, commit_args, server = True, verbose = True):
  cmd = CommitCommand()

  if not cmd.start (repo, commit_args, server, verbose):
    return False

  queue_command (cmd)
