#include "bfsyncdb.hh"
#include "bfbdb.hh"

int
foo()
{
  BFSync::bdb_open ("testdb");
  return 42;
}

INode*
load_inode (const ID *id, int version)
{
  return new INode();
}

ID*
id_root()
{
  return new ID();
}
