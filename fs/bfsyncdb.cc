#include "bfsyncdb.hh"
#include "bfbdb.hh"

using BFSync::DataOutBuffer;
using BFSync::DataBuffer;
using BFSync::DbcPtr;
using BFSync::BDB_TABLE_INODES;
using BFSync::BDB_TABLE_LINKS;
using BFSync::string_printf;
using BFSync::BDB;
using BFSync::HistoryEntry;

using std::string;
using std::vector;
using std::map;

bool
open_db()
{
  return BFSync::bdb_open ("test/bdb");
}

void
close_db()
{
  BFSync::bdb_close();
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

void
do_walk (const ID& id, const string& prefix = "")
{
  INode *inode = load_inode (&id, 1);
  if (inode)
    {
      if (inode->type == BFSync::FILE_DIR)
        {
          vector<Link> *links = load_links (&id, 1);
          for (vector<Link>::iterator li = links->begin(); li != links->end(); li++)
            {
              printf ("%s/%s\n", prefix.c_str(), li->name.c_str());
              do_walk (li->inode_id, prefix + "/" + li->name);
            }
          delete links;
        }
      delete inode;
    }
}

void
walk()
{
  ID *root = id_root();
  do_walk (*root);
  delete root;
}

DiffGenerator::DiffGenerator (unsigned int v_old, unsigned int v_new) :
  v_old (v_old), v_new (v_new)
{
  dbc_ret = dbc->get (&key, &data, DB_FIRST);
}

DiffGenerator::~DiffGenerator()
{
  while (!diffs.empty())
    {
      delete diffs.back();
      diffs.pop_back();
    }
}

void
make_lmap (map<string, const Link*>& lmap, const vector<Link>& links)
{
  for (vector<Link>::const_iterator li = links.begin(); li != links.end(); li++)
    {
      const Link& link = *li;
      lmap[link.name] = &link;
    }
}

string
print_id (const ID& id)
{
  string result;

  for (size_t i = 0; i < id.path_prefix.size(); i++)
    result += string_printf ("%02x", (unsigned char) id.path_prefix[i]);

  result += string_printf ("/%08x%08x%08x%08x%08x", id.a, id.b, id.c, id.d, id.e);
  return result;
}

vector<string> *
gen_iplus (INode *inode)
{
  vector<string> *result = new vector<string>;
  result->push_back ("i+");
  result->push_back (print_id (inode->id));
  result->push_back (string_printf ("%u", inode->uid));
  result->push_back (string_printf ("%u", inode->gid));
  result->push_back (string_printf ("%u", inode->mode));
  result->push_back (string_printf ("%u", inode->type));
  result->push_back (inode->hash);
  result->push_back (inode->link);
  result->push_back (string_printf ("%u", inode->size));
  result->push_back (string_printf ("%u", inode->major));
  result->push_back (string_printf ("%u", inode->minor));
  result->push_back (string_printf ("%u", inode->nlink));
  result->push_back (string_printf ("%u", inode->ctime));
  result->push_back (string_printf ("%u", inode->ctime_ns));
  result->push_back (string_printf ("%u", inode->mtime));
  result->push_back (string_printf ("%u", inode->mtime_ns));
  return result;
}

vector<string> *
gen_lplus (const Link *link)
{
  vector<string> *result = new vector<string>;
  result->push_back ("l+");
  result->push_back (print_id (link->dir_id));
  result->push_back (link->name);
  result->push_back (print_id (link->inode_id));
  return result;
}

vector<string> *
DiffGenerator::get_next()
{
  while (dbc_ret == 0 && diffs.empty())
    {
      DataBuffer kbuffer ((char *) key.get_data(), key.get_size());

      char table = ((char *) key.get_data()) [key.get_size() - 1];
      if (table == BDB_TABLE_INODES)
        {
          ID id;

          id_load (&id, kbuffer);
          INode *i_old = load_inode (&id, v_old);
          INode *i_new = load_inode (&id, v_new);

          if (i_old && i_new)
            {
            }
          else if (!i_old && i_new)
            {
              diffs.push_back (gen_iplus (i_new));
            }
          else if (i_old && !i_new)
            {
            }
        }
      else if (table == BDB_TABLE_LINKS)
        {
          ID id;
          id_load (&id, kbuffer);

          vector<Link> *lvec_old = load_links (&id, v_old);
          vector<Link> *lvec_new = load_links (&id, v_new);

          map<string, const Link*> lmap_old;
          map<string, const Link*> lmap_new;

          make_lmap (lmap_old, *lvec_old);
          make_lmap (lmap_new, *lvec_new);

          for (map<string, const Link*>::iterator mi = lmap_new.begin(); mi != lmap_new.end(); mi++)
            {
              const Link *l_old = lmap_old[mi->first];
              const Link *l_new = lmap_new[mi->first];

              if (!l_old && l_new)
                diffs.push_back (gen_lplus (l_new));
            }
        }

      /* goto next record */
      dbc_ret = dbc->get (&key, &data, DB_NEXT_NODUP);
    }
  if (!diffs.empty())
    {
      vector<string> *d = diffs.back();
      diffs.pop_back();
      return d;
    }
  else
    {
      return NULL;
    }
}

void
store_history_entry (int version, const string& hash, const string& author, const string& message, int time)
{
  HistoryEntry he;

  he.version = version;
  he.hash    = hash;
  he.author  = author;
  he.message = message;
  he.time    = time;

  BDB::the()->store_history_entry (version, he);
}
