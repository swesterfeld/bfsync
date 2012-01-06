#include "bfsyncdb.hh"
#include "bfbdb.hh"

using BFSync::DataOutBuffer;
using BFSync::DataBuffer;
using BFSync::DbcPtr;
using BFSync::BDB_TABLE_INODES;

int
foo()
{
  BFSync::bdb_open ("test/bdb");
  return 42;
}

void
id_store (const ID *id, DataOutBuffer& data_buf)
{
  data_buf.write_string (id->path_prefix);
  data_buf.write_uint32 (id->a);
  data_buf.write_uint32 (id->b);
  data_buf.write_uint32 (id->c);
  data_buf.write_uint32 (id->d);
  data_buf.write_uint32 (id->e);
}

INode*
load_inode (const ID *id, int version)
{
  INode *inode = new INode();
  DataOutBuffer kbuf;

  id_store (id, kbuf);
  kbuf.write_table (BDB_TABLE_INODES);

  Dbt ikey (kbuf.begin(), kbuf.size());
  Dbt idata;

  DbcPtr dbc; /* Acquire a cursor for the database. */

  int ret = dbc->get (&ikey, &idata, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) idata.get_data(), idata.get_size());

      inode->vmin = dbuffer.read_uint32();
      inode->vmax = dbuffer.read_uint32();

      if (version >= inode->vmin && version <= inode->vmax)
        {
          // inode->id   = id;
          inode->uid  = dbuffer.read_uint32();
          inode->gid  = dbuffer.read_uint32();
          inode->mode = dbuffer.read_uint32();
          inode->type = BFSync::FileType (dbuffer.read_uint32());
          inode->hash = dbuffer.read_string();
          inode->link = dbuffer.read_string();
          inode->size = dbuffer.read_uint32();
          inode->major = dbuffer.read_uint32();
          inode->minor = dbuffer.read_uint32();
          inode->nlink = dbuffer.read_uint32();
          inode->ctime = dbuffer.read_uint32();
          inode->ctime_ns = dbuffer.read_uint32();
          inode->mtime = dbuffer.read_uint32();
          inode->mtime_ns = dbuffer.read_uint32();
          return inode;
        }
      ret = dbc->get (&ikey, &idata, DB_NEXT_DUP);
    }
  delete inode;
  return NULL;
}

ID*
id_root()
{
  ID *id = new ID();
  id->a = id->b = id->c = id->d = id->e = 0;
  return id;
}
