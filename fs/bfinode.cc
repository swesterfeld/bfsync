#include <glib.h>

#include "bfinode.hh"
#include "bfsyncfs.hh"
#include "bfleakdebugger.hh"
#include "bflink.hh"

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

  for (map<string, INode*>::iterator ci = cache.begin(); ci != cache.end(); ci++)
    {
      INode *inode_ptr = ci->second;
      if (inode_ptr && inode_ptr->updated)
        {
          inode_ptr->save();
          inode_ptr->updated = false;
        }
    }

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
  ptr->save();
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
INode::save()
{
  sqlite3 *db = sqlite_db();
  sqlite3_stmt *stmt_ptr = NULL;

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

  char *gen_sql = g_strdup_printf ("INSERT INTO inodes VALUES (%d, %d, \"%s\", %d, %d, %d, \"%s\", \"%s\", \"%s\", "
                                   "%d, %d, %d, %d, %d, %d)",
    vmin, vmax,
    id.c_str(),
    uid, gid,
    mode,
    type_str.c_str(),
    hash.c_str(),
    link.c_str(),
    (int) major,
    (int) minor,
    (int) ctime, ctime_ns,
    (int) mtime, mtime_ns);

  string sql = gen_sql;
  g_free (gen_sql);

  printf ("sql: %s\n", sql.c_str());
  int rc = sqlite3_prepare_v2 (db, sql.c_str(), sql.size(), &stmt_ptr, NULL);
  if (rc != SQLITE_OK)
    return false;

  rc = sqlite3_step (stmt_ptr);
  if (rc != SQLITE_DONE)
    return false;
  return true;
}

bool
INode::load (const string& id)
{
  bool found = false;

  sqlite3 *db = sqlite_db();
  sqlite3_stmt *stmt_ptr = NULL;

  string sql = "SELECT * FROM inodes WHERE id = \"" + id + "\" AND vmin >= 1 AND vmax <= 1;";

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
  return found;
}

vector<LinkPtr>
INode::children() const
{
  vector<LinkPtr> result;
  sqlite3 *db = sqlite_db();
  sqlite3_stmt *stmt_ptr = NULL;

  char *sql_c = g_strdup_printf ("SELECT * FROM links WHERE dir_id = \"%s\"", id.c_str());

  string sql = sql_c;
  g_free (sql_c);

  printf ("sql: %s\n", sql.c_str());
  int rc = sqlite3_prepare_v2 (db, sql.c_str(), sql.size(), &stmt_ptr, NULL);
  if (rc != SQLITE_OK)
    return result;

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

      result.push_back (LinkPtr (link));
    }

  return result;
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

}
