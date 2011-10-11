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

  SQLStatement inode_stmt ("insert into inodes values (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
  SQLStatement link_stmt ("insert into links values (?,?,?,?,?)");
  SQLStatement del_inode_stmt ("DELETE FROM inodes WHERE id=?");
  SQLStatement del_links_stmt ("DELETE FROM links WHERE dir_id=?");

  double start_t = gettime();

  int inodes_saved = 0;
  inode_stmt.begin();
  for (map<string, INode*>::iterator ci = cache.begin(); ci != cache.end(); ci++)
    {
      INode *inode_ptr = ci->second;
      if (inode_ptr && inode_ptr->updated)
        {
          del_inode_stmt.reset();
          del_inode_stmt.bind_str (1, inode_ptr->id);
          del_inode_stmt.step();

          del_links_stmt.reset();
          del_links_stmt.bind_str (1, inode_ptr->id);
          del_links_stmt.step();

          inodes_saved++;
          inode_ptr->save (inode_stmt, link_stmt);
          inode_ptr->updated = false;
        }
    }
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
const int INODES_CTIME    = 11;
const int INODES_CTIME_NS = 12;
const int INODES_MTIME    = 13;
const int INODES_MTIME_NS = 14;

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
  ptr->set_mtime_ctime_now();
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
  stmt.bind_int (1 + INODES_VMIN, vmin);
  stmt.bind_int (1 + INODES_VMAX, vmax);
  stmt.bind_str (1 + INODES_ID, id);
  stmt.bind_int (1 + INODES_UID, uid);
  stmt.bind_int (1 + INODES_GID, gid);
  stmt.bind_int (1 + INODES_MODE, mode);
  stmt.bind_str (1 + INODES_TYPE, type_str);
  stmt.bind_str (1 + INODES_HASH, hash);
  stmt.bind_str (1 + INODES_LINK, link);
  stmt.bind_int (1 + INODES_MAJOR, major);
  stmt.bind_int (1 + INODES_MINOR, minor);
  stmt.bind_int (1 + INODES_CTIME, ctime);
  stmt.bind_int (1 + INODES_CTIME_NS, ctime_ns);
  stmt.bind_int (1 + INODES_MTIME, mtime);
  stmt.bind_int (1 + INODES_MTIME_NS, mtime_ns);
  stmt.step();

  for (vector<LinkPtr>::iterator li = links.begin(); li != links.end(); li++)
    {
      LinkPtr& lp = *li;

      if (!lp->deleted)
        {
          link_stmt.reset();
          link_stmt.bind_int (1, lp->vmin);
          link_stmt.bind_int (2, lp->vmax);
          link_stmt.bind_str (3, lp->dir_id);
          link_stmt.bind_str (4, lp->inode_id);
          link_stmt.bind_str (5, lp->name);
          link_stmt.step();
        }
    }
  return true;
}

bool
INode::load (const string& id)
{
  bool found = false;

  sqlite3 *db = sqlite_db();
  sqlite3_stmt *stmt_ptr = NULL;

  string sql = "SELECT * FROM inodes WHERE id = \"" + id + "\" AND vmin >= 1 AND vmax <= 1;";

  debug ("sql: %s\n", sql.c_str());
  double start_t = gettime();
  int rc = sqlite3_prepare_v2 (db, sql.c_str(), sql.size(), &stmt_ptr, NULL);
  if (rc != SQLITE_OK)
    return false;

  for (;;)
    {
      rc = sqlite3_step (stmt_ptr);
      if (rc != SQLITE_ROW)
        break;

      vmin     = sqlite3_column_int  (stmt_ptr, INODES_VMIN);
      vmax     = sqlite3_column_int  (stmt_ptr, INODES_VMAX);
      this->id = (const char *) sqlite3_column_text (stmt_ptr, INODES_ID);
      uid      = sqlite3_column_int  (stmt_ptr, INODES_UID);
      gid      = sqlite3_column_int  (stmt_ptr, INODES_GID);
      mode     = sqlite3_column_int  (stmt_ptr, INODES_MODE);

      string val = (const char *) sqlite3_column_text (stmt_ptr, INODES_TYPE);

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

      hash     = (const char *) sqlite3_column_text (stmt_ptr, INODES_HASH);
      link     = (const char *) sqlite3_column_text (stmt_ptr, INODES_LINK);
      major    = sqlite3_column_int  (stmt_ptr, INODES_MAJOR);
      minor    = sqlite3_column_int  (stmt_ptr, INODES_MINOR);
      ctime    = sqlite3_column_int  (stmt_ptr, INODES_CTIME);
      ctime_ns = sqlite3_column_int  (stmt_ptr, INODES_CTIME_NS);
      mtime    = sqlite3_column_int  (stmt_ptr, INODES_MTIME);
      mtime_ns = sqlite3_column_int  (stmt_ptr, INODES_MTIME_NS);

      found = true;
    }
  if (!found)
    return false;
  double end_t = gettime();
  debug ("time for sql %.2f ms\n", (end_t - start_t) * 1000);

  // load links
  char *sql_c = g_strdup_printf ("SELECT * FROM links WHERE dir_id = \"%s\"", id.c_str());
  sql = sql_c;
  g_free (sql_c);

  debug ("sql: %s\n", sql.c_str());

  start_t = gettime();
  rc = sqlite3_prepare_v2 (db, sql.c_str(), sql.size(), &stmt_ptr, NULL);
  if (rc != SQLITE_OK)
    return false;

  for (;;)
    {
      rc = sqlite3_step (stmt_ptr);
      if (rc != SQLITE_ROW)
        break;

      Link *link = new Link();

      link->vmin = sqlite3_column_int (stmt_ptr, 0);
      link->vmax = sqlite3_column_int (stmt_ptr, 1);
      link->dir_id = (const char *) sqlite3_column_text (stmt_ptr, 2);
      link->inode_id = (const char *) sqlite3_column_text (stmt_ptr, 3);
      link->name = (const char *) sqlite3_column_text (stmt_ptr, 4);

      links.push_back (LinkPtr (link));
    }
  debug ("time for sql %.2f ms\n", (gettime() - start_t) * 1000);

  return found;
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

  links.push_back (LinkPtr (link));
}

bool
INode::unlink (const string& name)
{
  for (vector<LinkPtr>::iterator li = links.begin(); li != links.end(); li++)
    {
      LinkPtr& lp = *li;

      if (lp->name == name)
        {
          lp.update()->deleted = true;
          return true;
        }
    }
  return false;
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
