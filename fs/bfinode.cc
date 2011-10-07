#include <glib.h>

#include "bfinode.hh"
#include "bfsyncfs.hh"

using std::string;

namespace BFSync {

const int INODES_VMIN     = 0;
const int INODES_VMAX     = 1;
const int INODES_ID       = 2;
const int INODES_UID      = 3;
const int INODES_GID      = 4;
const int INODES_MODE     = 5;
const int INODES_TYPE     = 6;
const int INODES_HASH     = 7;
const int INODES_CTIME    = 8;
const int INODES_CTIME_NS = 9;
const int INODES_MTIME    = 10;
const int INODES_MTIME_NS = 11;

INodePtr::INodePtr (const string& id) :
  ptr (NULL)
{
  sqlite3 *db = sqlite_db();
  sqlite3_stmt *stmt_ptr = NULL;

  string sql = "SELECT * FROM inodes WHERE id = \"" + id + "\" AND vmin >= 1 AND vmax <= 1;";

  int rc = sqlite3_prepare_v2 (db, sql.c_str(), sql.size(), &stmt_ptr, NULL);
  if (rc != SQLITE_OK)
    return;

  ptr = new INode;
  for (;;)
    {
      rc = sqlite3_step (stmt_ptr);
      if (rc != SQLITE_ROW)
        break;

      ptr->vmin     = sqlite3_column_int  (stmt_ptr, INODES_VMIN);
      ptr->vmax     = sqlite3_column_int  (stmt_ptr, INODES_VMAX);
      ptr->id       = (const char *) sqlite3_column_text (stmt_ptr, INODES_ID);
      ptr->uid      = sqlite3_column_int  (stmt_ptr, INODES_UID);
      ptr->gid      = sqlite3_column_int  (stmt_ptr, INODES_GID);
      ptr->mode     = sqlite3_column_int  (stmt_ptr, INODES_MODE);

      string val = (const char *) sqlite3_column_text (stmt_ptr, INODES_TYPE);

      if (val == "file")
        ptr->type = FILE_REGULAR;
      else if (val == "symlink")
        ptr->type = FILE_SYMLINK;
      else if (val == "dir")
        ptr->type = FILE_DIR;
      else if (val == "fifo")
        ptr->type = FILE_FIFO;
      else if (val == "socket")
        ptr->type = FILE_SOCKET;
      else if (val == "blockdev")
        ptr->type = FILE_BLOCK_DEV;
      else if (val == "chardev")
        ptr->type = FILE_CHAR_DEV;
      else
        ptr->type = FILE_NONE;

      ptr->hash     = (const char *) sqlite3_column_text (stmt_ptr, INODES_HASH);
      ptr->ctime    = sqlite3_column_int  (stmt_ptr, INODES_CTIME);
      ptr->ctime_ns = sqlite3_column_int  (stmt_ptr, INODES_CTIME_NS);
      ptr->mtime    = sqlite3_column_int  (stmt_ptr, INODES_MTIME);
      ptr->mtime_ns = sqlite3_column_int  (stmt_ptr, INODES_MTIME_NS);
    }
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
  printf ("save()\n");
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

  char *gen_sql = g_strdup_printf ("INSERT INTO inodes VALUES (%d, %d, \"%s\", %d, %d, %d, \"%s\", \"%s\", %d, %d, %d, %d)",
    vmin, vmax,
    id.c_str(),
    uid, gid,
    mode,
    type_str.c_str(),
    hash.c_str(),
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

}
