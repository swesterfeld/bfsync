#include <glib.h>
#include <sys/time.h>

#include "bfinode.hh"
#include "bfsyncfs.hh"
#include "bfleakdebugger.hh"
#include "bflink.hh"
#include "bfsql.hh"

using std::string;
using std::vector;
using std::map;

namespace BFSync {

INodeRepo inode_repo;

INodeRepo*
INodeRepo::the()
{
  return &inode_repo;
}

void
INodeRepo::save_changes()
{
  inode_repo.mutex.lock();

  SQLStatement& inode_stmt = inode_repo.sql_statements.get
    ("INSERT INTO inodes VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
  SQLStatement& link_stmt = inode_repo.sql_statements.get
    ("INSERT INTO links VALUES (?,?,?,?,?)");
  SQLStatement& del_inode_stmt = inode_repo.sql_statements.get
    ("DELETE FROM inodes WHERE id=?");
  SQLStatement& del_links_stmt = inode_repo.sql_statements.get
    ("DELETE FROM links WHERE dir_id=?");
  SQLStatement& addi_stmt = inode_repo.sql_statements.get
   ("INSERT INTO local_inodes VALUES (?,?)");

  double start_t = gettime();

  int inodes_saved = 0;
  inode_stmt.begin();
  for (map<string, INode*>::iterator ci = cache.begin(); ci != cache.end(); ci++)
    {
      INode *inode_ptr = ci->second;
      if (inode_ptr && inode_ptr->updated)
        {
          del_inode_stmt.reset();
          del_inode_stmt.bind_text (1, inode_ptr->id);
          del_inode_stmt.step();

          del_links_stmt.reset();
          del_links_stmt.bind_text (1, inode_ptr->id);
          del_links_stmt.step();

          inodes_saved++;
          inode_ptr->save (inode_stmt, link_stmt);
          inode_ptr->updated = false;
        }
    }

  // write newly allocated inodes to local_inodes table
  for (map<ino_t, string>::const_iterator ni = new_inodes.begin(); ni != new_inodes.end(); ni++)
    {
      addi_stmt.reset();
      addi_stmt.bind_text (1, ni->second);
      addi_stmt.bind_int (2, ni->first);
      addi_stmt.step();
    }
  new_inodes.clear();
  debug ("time for sql prepare: %.2fms (%d inodes needed saving)\n", (gettime() - start_t) * 1000,
         inodes_saved);
  inode_stmt.commit();

  if (inode_stmt.success() && link_stmt.success() && del_inode_stmt.success() && del_links_stmt.success())
    debug ("sql exec OK\n");
  else
    debug ("sql exec FAIL\n");

  double end_t = gettime();
  debug ("time for sql: %.2fms\n", (end_t - start_t) * 1000);

  inode_repo.mutex.unlock();
}

const int INODES_VMIN     = 0;
const int INODES_VMAX     = 1;
const int INODES_ID       = 2;
const int INODES_UID      = 3;
const int INODES_GID      = 4;
const int INODES_MODE     = 5;
const int INODES_TYPE     = 6;
const int INODES_HASH     = 7;
const int INODES_LINK     = 8;
const int INODES_MAJOR    = 9;
const int INODES_MINOR    = 10;
const int INODES_NLINK    = 11;
const int INODES_CTIME    = 12;
const int INODES_CTIME_NS = 13;
const int INODES_MTIME    = 14;
const int INODES_MTIME_NS = 15;

INodePtr::INodePtr (const string& id) :
  ptr (NULL)
{
  inode_repo.mutex.lock();

  INode*& cached_ptr = inode_repo.cache[id];
  if (cached_ptr)
    {
      ptr = cached_ptr;
    }
  else
    {
      ptr = new INode;
      if (!ptr->load (id))
        {
          delete ptr;
          ptr = NULL;
        }
    }
  cached_ptr = ptr;

  inode_repo.mutex.unlock();
}

static string
gen_id()
{
  string id;
  // globally (across all versions/hosts) uniq id, with the same amount of information as a SHA1-hash
  while (id.size() < 40)
    {
      char hex[32];
      sprintf (hex, "%08x", g_random_int());
      id += hex;
    }
  return id;
}

INodePtr::INodePtr (fuse_context *context)
{
  ptr = new INode();
  ptr->id = gen_id();
  ptr->uid = context->uid;
  ptr->gid = context->gid;
  ptr->nlink = 0;
  ptr->set_mtime_ctime_now();
  ptr->load_or_alloc_ino();
  ptr->updated = true;

  inode_repo.mutex.lock();
  inode_repo.cache[ptr->id] = ptr;
  inode_repo.mutex.unlock();
}

INodePtr::INodePtr() :
  ptr (NULL)
{
}

INodePtr
INodePtr::null()
{
  return INodePtr();
}

/*-------------------------------------------------------------------------------------------*/

static LeakDebugger leak_debugger ("BFSync::INode");

INode::INode() :
  vmin (1),
  vmax (1)
{
  leak_debugger.add (this);
}

INode::~INode()
{
  leak_debugger.del (this);
}


void
INode::set_mtime_ctime_now()
{
  timespec time_now;

  if (clock_gettime (CLOCK_REALTIME, &time_now) == 0)
    {
      mtime     = time_now.tv_sec;
      mtime_ns  = time_now.tv_nsec;
      ctime     = time_now.tv_sec;
      ctime_ns  = time_now.tv_nsec;
    }
}

void
INode::set_ctime_now()
{
  timespec time_now;

  if (clock_gettime (CLOCK_REALTIME, &time_now) == 0)
    {
      ctime     = time_now.tv_sec;
      ctime_ns  = time_now.tv_nsec;
    }
}

bool
INode::save (SQLStatement& stmt, SQLStatement& link_stmt)
{
  string type_str;
  if (type == FILE_REGULAR)
    {
      type_str = "file";
    }
  else if (type == FILE_DIR)
    {
      type_str = "dir";
    }
  else if (type == FILE_SYMLINK)
    {
      type_str = "symlink";
    }
  else if (type == FILE_FIFO)
    {
      type_str = "fifo";
    }
  else if (type == FILE_SOCKET)
    {
      type_str = "socket";
    }
  else if (type == FILE_BLOCK_DEV)
    {
      type_str = "blockdev";
    }
  else if (type == FILE_CHAR_DEV)
    {
      type_str = "chardev";
    }
  else
    {
      return false; // unsupported type
    }

  stmt.reset();
  stmt.bind_int   (1 + INODES_VMIN, vmin);
  stmt.bind_int   (1 + INODES_VMAX, vmax);
  stmt.bind_text  (1 + INODES_ID, id);
  stmt.bind_int   (1 + INODES_UID, uid);
  stmt.bind_int   (1 + INODES_GID, gid);
  stmt.bind_int   (1 + INODES_MODE, mode);
  stmt.bind_text  (1 + INODES_TYPE, type_str);
  stmt.bind_text  (1 + INODES_HASH, hash);
  stmt.bind_text  (1 + INODES_LINK, link);
  stmt.bind_int   (1 + INODES_MAJOR, major);
  stmt.bind_int   (1 + INODES_MINOR, minor);
  stmt.bind_int   (1 + INODES_NLINK, nlink);
  stmt.bind_int   (1 + INODES_CTIME, ctime);
  stmt.bind_int   (1 + INODES_CTIME_NS, ctime_ns);
  stmt.bind_int   (1 + INODES_MTIME, mtime);
  stmt.bind_int   (1 + INODES_MTIME_NS, mtime_ns);
  stmt.step();

  for (vector<LinkPtr>::iterator li = links.begin(); li != links.end(); li++)
    {
      LinkPtr& lp = *li;

      if (!lp->deleted)
        {
          link_stmt.reset();
          link_stmt.bind_int  (1, lp->vmin);
          link_stmt.bind_int  (2, lp->vmax);
          link_stmt.bind_text (3, lp->dir_id);
          link_stmt.bind_text (4, lp->inode_id);
          link_stmt.bind_text (5, lp->name);
          link_stmt.step();
        }
    }
  return true;
}

bool
INode::load (const string& id)
{
  bool found = false;

  SQLStatement& load_inode = inode_repo.sql_statements.get
    ("SELECT * FROM inodes WHERE id = ? AND vmin >= 1 AND vmax <= 1;");

  double start_t = gettime();
  load_inode.reset();
  load_inode.bind_text (1, id);

  for (;;)
    {
      int rc = load_inode.step();
      if (rc != SQLITE_ROW)
        break;

      vmin     = load_inode.column_int  (INODES_VMIN);
      vmax     = load_inode.column_int  (INODES_VMAX);
      this->id = load_inode.column_text (INODES_ID);
      uid      = load_inode.column_int  (INODES_UID);
      gid      = load_inode.column_int  (INODES_GID);
      mode     = load_inode.column_int  (INODES_MODE);

      string val = load_inode.column_text (INODES_TYPE);

      if (val == "file")
        type = FILE_REGULAR;
      else if (val == "symlink")
        type = FILE_SYMLINK;
      else if (val == "dir")
        type = FILE_DIR;
      else if (val == "fifo")
        type = FILE_FIFO;
      else if (val == "socket")
        type = FILE_SOCKET;
      else if (val == "blockdev")
        type = FILE_BLOCK_DEV;
      else if (val == "chardev")
        type = FILE_CHAR_DEV;
      else
        type = FILE_NONE;

      hash     = load_inode.column_text (INODES_HASH);
      link     = load_inode.column_text (INODES_LINK);
      major    = load_inode.column_int  (INODES_MAJOR);
      minor    = load_inode.column_int  (INODES_MINOR);
      nlink    = load_inode.column_int  (INODES_NLINK);
      ctime    = load_inode.column_int  (INODES_CTIME);
      ctime_ns = load_inode.column_int  (INODES_CTIME_NS);
      mtime    = load_inode.column_int  (INODES_MTIME);
      mtime_ns = load_inode.column_int  (INODES_MTIME_NS);

      found = true;
    }
  if (!found)
    return false;
  double end_t = gettime();
  debug ("time for sql %.2f ms\n", (end_t - start_t) * 1000);

  // load links
  SQLStatement& load_links = inode_repo.sql_statements.get
    ("SELECT * FROM links WHERE dir_id = ?");

  start_t = gettime();
  load_links.reset();
  load_links.bind_text (1, id);
  for (;;)
    {
      int rc = load_links.step();
      if (rc != SQLITE_ROW)
        break;

      Link *link = new Link();

      link->vmin = load_links.column_int (0);
      link->vmax = load_links.column_int (1);
      link->dir_id = load_links.column_text (2);
      link->inode_id = load_links.column_text (3);
      link->name = load_links.column_text (4);

      links.push_back (LinkPtr (link));
    }
  debug ("time for sql %.2f ms\n", (gettime() - start_t) * 1000);

  load_or_alloc_ino();
  return found;
}

void
INode::load_or_alloc_ino()
{
  // load inode number
  ino = 0;
  SQLStatement& loadi_stmt = inode_repo.sql_statements.get (
    "SELECT * FROM local_inodes WHERE id = ?"
  );
  loadi_stmt.bind_text (1, id);
  for (;;)
    {
      int rc = loadi_stmt.step();
      if (rc != SQLITE_ROW)
        break;
      ino = loadi_stmt.column_int (0);
    }

  // need to create new entry
  if (!ino)
    {
      SQLStatement& searchi_stmt = inode_repo.sql_statements.get (
        "SELECT * FROM local_inodes WHERE ino = ?"
      );

      while (!ino)
        {
          ino = g_random_int_range (1, 2000 * 1000 * 1000);  // 1 .. ~ 2^31
          if (!inode_repo.new_inodes[ino].empty())
            ino = 0;
          else
            {
              searchi_stmt.reset();
              searchi_stmt.bind_int (1, ino);

              int rc = searchi_stmt.step();
              if (rc == SQLITE_ROW)  // inode number already in use
                ino = 0;
            }
        }
      inode_repo.new_inodes[ino] = id;
    }
}

vector<LinkPtr>
INode::children() const
{
  return links;
}

string
INode::file_path() const
{
  FileStatus fs = file_status();
  if (fs == FS_CHANGED)
    return Options::the()->repo_path + "/new/" + id;

  if (fs == FS_RDONLY)
    return make_object_filename (hash);

  return "";
}

FileStatus
INode::file_status() const
{
  if (hash == "new")
    return FS_CHANGED;
  else
    return FS_RDONLY;
}

void
INode::copy_on_write()
{
  if (file_status() == FS_RDONLY && type == FILE_REGULAR)
    {
      string new_name = Options::the()->repo_path + "/new/" + id;
      string old_name = file_path();

      int old_fd = open (old_name.c_str(), O_RDONLY);
      int new_fd = open (new_name.c_str(), O_WRONLY | O_CREAT, 0644);

      vector<unsigned char> buffer (128 * 1024);
      ssize_t read_bytes;
      while ((read_bytes = read (old_fd, &buffer[0], buffer.size())) > 0)
        {
          write (new_fd, &buffer[0], read_bytes);
        }
      close (old_fd);
      close (new_fd);

      hash = "new";
    }
}

void
INode::add_link (INodePtr to, const string& name)
{
  Link *link = new Link();

  link->vmin = 1;
  link->vmax = 1;
  link->dir_id = id;
  link->inode_id = to->id;
  link->name = name;

  to.update()->nlink++;

  links.push_back (LinkPtr (link));
}

bool
INode::unlink (const string& name)
{
  for (vector<LinkPtr>::iterator li = links.begin(); li != links.end(); li++)
    {
      LinkPtr& lp = *li;

      if (lp->name == name && !lp->deleted)
        {
          INodePtr inode (lp->inode_id);

          if (inode)
            inode.update()->nlink--;

          lp.update()->deleted = true;
          return true;
        }
    }
  return false;
}

bool
INode::read_perm_ok() const
{
  const fuse_context *ctx = fuse_get_context();

  if (ctx->uid == 0)
    return true;

  if (ctx->uid == uid)
    return (mode & S_IRUSR);

  if (ctx->gid == gid)
    return (mode & S_IRGRP);

  return (mode & S_IROTH);
}

bool
INode::write_perm_ok() const
{
  const fuse_context *ctx = fuse_get_context();

  if (ctx->uid == 0)
    return true;

  if (ctx->uid == uid)
    return (mode & S_IWUSR);

  if (ctx->gid == gid)
    return (mode & S_IWGRP);

  return (mode & S_IWOTH);
}

bool
INode::search_perm_ok() const
{
  const fuse_context *ctx = fuse_get_context();

  if (ctx->uid == 0)
    return true;

  if (ctx->uid == uid)
    return (mode & S_IXUSR);

  if (ctx->gid == gid)
    return (mode & S_IXGRP);

  return (mode & S_IXOTH);
}

}
