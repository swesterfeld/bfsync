#include "glib.h"
#include "bfbdb.hh"
#include <string>
#include <vector>

struct ID {
  std::string path_prefix;
  unsigned int a, b, c, d, e; // guint32
};

struct INode {
  unsigned int vmin, vmax;
  ID           id;
  unsigned int gid, uid;
  unsigned int mode, type;

  std::string hash;
  std::string link;

  unsigned int size; // FIXME
  unsigned int major, minor;
  unsigned int nlink;
  unsigned int mtime, mtime_ns;
  unsigned int ctime, ctime_ns;
};

struct Link {
  unsigned int vmin, vmax;
  ID dir_id;
  ID inode_id;
  std::string name;
};

struct HistoryEntry
{
  bool          valid;

  unsigned int  version;
  std::string   hash;
  std::string   author;
  std::string   message;
  unsigned int  time;
};

class BDBWrapper
{
  unsigned int ref_count;
  BFSync::Mutex ref_mutex;
public:
  BDBWrapper();
  ~BDBWrapper();

  BFSync::BDB  *my_bdb;

  void
  ref()
  {
    BFSync::Lock lock (ref_mutex);

    g_return_if_fail (ref_count > 0);
    ref_count++;
  }

  void
  unref()
  {
    BFSync::Lock lock (ref_mutex);

    g_return_if_fail (ref_count > 0);
    ref_count--;
  }

  bool
  has_zero_refs()
  {
    BFSync::Lock lock (ref_mutex);

    return ref_count == 0;
  }
};

class BDBPtr {
  BDBWrapper *ptr;

public:
  BDBPtr (BDBWrapper *wrapper = NULL);
  BDBPtr (const BDBPtr& other);
  BDBPtr& operator= (const BDBPtr& other);

  ~BDBPtr();

  INode             *load_inode (const ID *id, int version);
  void               store_inode (const INode *inode);
  std::vector<Link> *load_links (const ID *id, int version);
  void               walk();
  void               store_history_entry (int version,
                                          const std::string& hash,
                                          const std::string& author,
                                          const std::string& msg,
                                          int time);
  HistoryEntry       load_history_entry (int version);
  void               close();

  BFSync::BDB*
  get_bdb()
  {
    return ptr->my_bdb;
  }
};

extern BDBPtr             open_db (const std::string& db);
extern ID*                id_root();
class DiffGenerator
{
  BFSync::DbcPtr dbc;

  Dbt key;
  Dbt data;

  int dbc_ret;

  BDBPtr bdb_ptr;
  unsigned int v_old, v_new;
  std::vector< std::vector<std::string> > diffs;
public:
  DiffGenerator (BDBPtr bdb_ptr, unsigned int v_old, unsigned int v_new);
  ~DiffGenerator();

  std::vector<std::string> get_next();
};
