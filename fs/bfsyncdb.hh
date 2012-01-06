#include "glib.h"
#include <string>

struct INode {
  unsigned int vmin, vmax;
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

struct ID {
  std::string path_prefix;
  unsigned int a, b, c, d, e; // guint32
};

extern int foo();
extern INode *load_inode (const ID *id, int version);
extern ID*    id_root();
