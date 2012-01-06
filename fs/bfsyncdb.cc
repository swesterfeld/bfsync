#include "bfsyncdb.hh"
#include "bfbdb.hh"

using BFSync::DataOutBuffer;
using BFSync::DataBuffer;
using BFSync::DbcPtr;
using BFSync::BDB_TABLE_INODES;
using BFSync::BDB_TABLE_LINKS;

using std::vector;

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

void
id_load (ID *id, DataBuffer& dbuf)
{
  id->path_prefix = dbuf.read_string();
  id->a = dbuf.read_uint32();
  id->b = dbuf.read_uint32();
  id->c = dbuf.read_uint32();
  id->d = dbuf.read_uint32();
  id->e = dbuf.read_uint32();
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
          inode->id   = *id;
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

std::vector<Link>*
load_links (const ID *id, int version)
{
  vector<Link>* result = new vector<Link>;

  DataOutBuffer kbuf;

  id_store (id, kbuf);
  kbuf.write_table (BDB_TABLE_LINKS);

  Dbt lkey (kbuf.begin(), kbuf.size());
  Dbt ldata;

  DbcPtr dbc; /* Acquire a cursor for the database. */

  // iterate over key elements and delete records which are in LinkVersionList
  int ret = dbc->get (&lkey, &ldata, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) ldata.get_data(), ldata.get_size());

      guint32 vmin = dbuffer.read_uint32();
      guint32 vmax = dbuffer.read_uint32();

      if (version >= vmin && version <= vmax)
        {
          Link l;

          l.vmin = vmin;
          l.vmax = vmax;
          l.dir_id = *id;
          id_load (&l.inode_id, dbuffer);
          l.name = dbuffer.read_string();

          result->push_back (l);
        }
      ret = dbc->get (&lkey, &ldata, DB_NEXT_DUP);
    }
  return result;
}
