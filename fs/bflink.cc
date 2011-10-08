#include "bfinode.hh"
#include "bflink.hh"
#include "bfleakdebugger.hh"
#include <glib.h>
#include <string>

using std::string;

namespace BFSync
{


LinkPtr::LinkPtr (const INodePtr& dir, const INodePtr& inode, const string& filename)
{
  ptr     = new Link();

  ptr->vmin     = 1;
  ptr->vmax     = 1;
  ptr->dir_id   = dir->id;
  ptr->inode_id = inode->id;
  ptr->name     = filename;

  ptr->save();
}

LinkPtr::LinkPtr (Link *link)
{
  ptr = link;
}

/*-------------------------------------------------------------------------------------------*/

static LeakDebugger leak_debugger ("BFSync::Link");

Link::Link()
{
  leak_debugger.add (this);
}

Link::~Link()
{
  leak_debugger.del (this);
}

bool
Link::save()
{
  sqlite3 *db = sqlite_db();
  sqlite3_stmt *stmt_ptr = NULL;

  char *sql_c = g_strdup_printf ("INSERT INTO links VALUES (%d, %d, \"%s\", \"%s\", \"%s\")",
    vmin, vmax,
    dir_id.c_str(),
    inode_id.c_str(),
    name.c_str());

  string sql = sql_c;
  g_free (sql_c);

  debug ("sql: %s\n", sql.c_str());
  double start_t = gettime();
  int rc = sqlite3_prepare_v2 (db, sql.c_str(), sql.size(), &stmt_ptr, NULL);
  if (rc != SQLITE_OK)
    return false;
  debug ("sql prepare: %.2f ms\n", 1000 * (gettime() - start_t));
  rc = sqlite3_step (stmt_ptr);
  if (rc != SQLITE_DONE)
    return false;
  debug ("sql prepare+step: %.2f ms\n", 1000 * (gettime() - start_t));
  return true;
}

}
