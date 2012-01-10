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

class BDB {
public:
  BFSync::BDB *my_bdb;

  BDB (BFSync::BDB *bdb);
  ~BDB();

  INode             *load_inode (const ID *id, int version);
  std::vector<Link> *load_links (const ID *id, int version);
  void               walk();
  void               store_history_entry (int version,
                                          const std::string& hash,
                                          const std::string& author,
                                          const std::string& msg,
                                          int time);
  void               close();
};

extern BDB               *open_db (const std::string& db);
extern ID*                id_root();
class DiffGenerator
{
  BFSync::DbcPtr dbc;

  Dbt key;
  Dbt data;

  int dbc_ret;

  BDB *bdb;
  unsigned int v_old, v_new;
  std::vector< std::vector<std::string>* > diffs;
public:
  DiffGenerator (BDB *bdb, unsigned int v_old, unsigned int v_new);
  ~DiffGenerator();

  std::vector<std::string> *get_next();
};
