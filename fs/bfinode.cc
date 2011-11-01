#include <glib.h>
#include <sys/time.h>
#include <assert.h>

#include "bfinode.hh"
#include "bfsyncfs.hh"
#include "bfleakdebugger.hh"
#include "bflink.hh"
#include "bfsql.hh"

#include <set>

using std::string;
using std::vector;
using std::map;
using std::set;

namespace BFSync {

static INodeRepo *inode_repo = 0;

INodeRepo::INodeRepo() :
  m_sql_statements (0)
{
  assert (!inode_repo);

  inode_repo = this;
}

INodeRepo::~INodeRepo()
{
  assert (inode_repo);

  inode_repo = 0;
}

INodeRepo*
INodeRepo::the()
{
  assert (inode_repo);
  return inode_repo;
}

void
INodeRepo::clear_cache()
{
  save_changes (SC_CLEAR_CACHE);
}

void
INodeRepo::save_changes (SaveChangesMode sc)
{
  Lock lock (mutex);

  SQLStatement& inode_stmt = sql_statements().get
    ("INSERT INTO inodes VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
  SQLStatement& link_stmt = sql_statements().get
    ("INSERT INTO links VALUES (?,?,?,?,?)");
  SQLStatement& del_inode_stmt = sql_statements().get
    ("DELETE FROM inodes WHERE id=? and (vmin=? or vmax=?)");
  SQLStatement& addi_stmt = sql_statements().get
   ("INSERT INTO local_inodes VALUES (?,?)");

  double start_t = gettime();

  int inodes_saved = 0;
  inode_stmt.begin();
  for (map<ID, INodeVersionList>::iterator ci = cache.begin(); ci != cache.end(); ci++)
    {
      INodeVersionList& ivlist = ci->second;
      bool need_save = false;
      const ID& id  = ci->first;
      string id_str = id.str();

      for (size_t i = 0; i < ivlist.size(); i++)
        {
          INodePtr inode_ptr = ivlist[i];

          if (inode_ptr && inode_ptr->updated)
            {
              need_save = true;
            }
        }
      if (need_save)
        {
          for (size_t i = 0; i < ivlist.size(); i++)
            {
              INodePtr inode_ptr = ivlist[i];
              assert (inode_ptr);

              // this will reliably delete the old inode entry for both modifications that
              // can be made for an inode entry:
              //  - just change some fields
              //  - split into two inodes (copy-on-write)
              del_inode_stmt.reset();
              del_inode_stmt.bind_text (1, id_str);
              del_inode_stmt.bind_int (2, inode_ptr->vmin);
              del_inode_stmt.bind_int (3, inode_ptr->vmax);
              del_inode_stmt.step();
            }
        }

      INodeLinksPtr links = INodeLinksPtr::null();
      for (size_t i = 0; i < ivlist.size(); i++)
        {
          INodePtr inode_ptr = ivlist[i];

          if (inode_ptr && need_save)
            {
              inodes_saved++;

              INode *inode = inode_ptr.get_ptr_without_update();
              inode->save (inode_stmt);
              inode->updated = false;
              if (!links)
                links = inode->links;
            }
        }
      if (need_save && links)
        {
          INodeLinks *inode_links = links.get_ptr_without_update();
          inode_links->save (link_stmt);
        }
    }

  // write newly allocated inodes to local_inodes table
  for (map<ino_t, ID>::const_iterator ni = new_inodes.begin(); ni != new_inodes.end(); ni++)
    {
      addi_stmt.reset();
      addi_stmt.bind_text (1, ni->second.str());
      addi_stmt.bind_int (2, ni->first);
      addi_stmt.step();
    }
  new_inodes.clear();
  debug ("time for sql prepare: %.2fms (%d inodes needed saving)\n", (gettime() - start_t) * 1000,
         inodes_saved);
  inode_stmt.commit();

  if (inode_stmt.success() && link_stmt.success() && del_inode_stmt.success())
    debug ("sql exec OK\n");
  else
    debug ("sql exec FAIL\n");

  double end_t = gettime();
  debug ("time for sql: %.2fms\n", (end_t - start_t) * 1000);

  if (sc == SC_CLEAR_CACHE)
    {
      cache.clear();
      links_cache.clear();
    }
}

void
INodeRepo::free_sql_statements()
{
  Lock lock (mutex);

  if (m_sql_statements)
    {
      delete m_sql_statements;
      m_sql_statements = 0;
    }
}

void
INodeRepo::delete_unused_inodes (DeleteMode dmode)
{
  Lock lock (mutex);

  map<ID, INodeVersionList>::iterator ci = cache.begin();
  while (ci != cache.end())
    {
      INodeVersionList& ivlist = ci->second;
      const ID& id = ci->first;

      map<ID, INodeVersionList>::iterator nexti = ci;
      nexti++;

      bool del = true;                          // dmode == DM_ALL <-> delete all
      if (dmode == DM_SOME)
        del = g_random_int_range (0, 100) <= 5; // randomly delete 5%
      if (del)
        {
          for (size_t i = 0; i < ivlist.size(); i++)
            {
              // can only delete cache entries that have not been modified (and not saved)
              if (ivlist[i]->updated)
                {
                  del = false;
                  break;
                }
            }
          if (del)
            {
              cache.erase (ci);

              map<ID, INodeLinksPtr>::iterator lci = links_cache.find (id);
              if (lci != links_cache.end())
                links_cache.erase (lci);
            }
        }
      ci = nexti;
    }
}

int
INodeRepo::cached_inode_count()
{
  Lock lock (mutex);
  return cache.size();
}

int
INodeRepo::cached_dir_count()
{
  Lock lock (mutex);
  return links_cache.size();
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
const int INODES_SIZE     = 9;
const int INODES_MAJOR    = 10;
const int INODES_MINOR    = 11;
const int INODES_NLINK    = 12;
const int INODES_CTIME    = 13;
const int INODES_CTIME_NS = 14;
const int INODES_MTIME    = 15;
const int INODES_MTIME_NS = 16;

INodePtr::INodePtr (const Context& ctx, const ID& id) :
  ptr (NULL)
{
  Lock lock (INodeRepo::the()->mutex);

  // do we have the inode ptr for requested version in cache?
  INodeVersionList& ivlist = INodeRepo::the()->cache[id];
  for (size_t i = 0; i < ivlist.size(); i++)
    {
      INodePtr& ip = ivlist[i];
      if (ctx.version >= ip->vmin && ctx.version <= ip->vmax)
        {
          *this = ip;   // yes, in cache
          return;
        }
    }
  // not in cache, load and add to cache
  ptr = new INode;
  if (!ptr->load (ctx, id))
    {
      delete ptr;
      ptr = NULL;
    }
  else
    {
      ivlist.add (*this);
    }
}

INodePtr::INodePtr (const Context& ctx)
{
  ptr = new INode();
  ptr->vmin = ctx.version;
  ptr->vmax = ctx.version;
  ptr->id = ID::gen_new();
  ptr->uid = ctx.fc->uid;
  ptr->gid = ctx.fc->gid;
  ptr->size = 0;
  ptr->major = 0;
  ptr->minor = 0;
  ptr->nlink = 0;
  ptr->set_mtime_ctime_now();
  ptr->alloc_ino();
  ptr->updated = true;

  Lock lock (INodeRepo::the()->mutex);
  INodeVersionList& ivlist = INodeRepo::the()->cache [ptr->id];
  ivlist.add (*this);

  // setup links (and cache entry)
  INodeLinksPtr& repo_links = INodeRepo::the()->links_cache[ptr->id];

  assert (!repo_links);  // ID is new, so there should not be a cache entry yet
  repo_links = INodeLinksPtr (new INodeLinks());
  ptr->links = repo_links;
}

INodePtr::INodePtr (INode *inode) :
  ptr (inode)
{
}

INodePtr
INodePtr::null()
{
  return INodePtr();
}

INodePtr::~INodePtr()
{
  if (ptr)
    {
      ptr->unref();
      /* eager deletion */
      if (ptr->has_zero_refs())
        delete ptr;
      ptr = NULL;
    }
}

INode*
INodePtr::update() const
{
  g_return_val_if_fail (ptr, NULL);

  if (!ptr->updated && ptr->vmin != ptr->vmax)
    {
      // INode copy-on-write

      INode *old_ptr = new INode (*ptr);
      old_ptr->vmax--;
      old_ptr->updated = true;

      ptr->vmin = ptr->vmax;
      ptr->updated = true;

      // add old version to cache
      Lock lock (INodeRepo::the()->mutex);
      INodeVersionList& ivlist = INodeRepo::the()->cache [ptr->id];
      INodePtr old_inode (old_ptr);
      INode *result = ptr;        // store ptr, cannot access it below
      ivlist.add (old_inode);     // this might "delete this;" since the inodes are stored in the vector
      g_assert (result);
      return result;
    }
  else
    {
      ptr->updated = true;
      return ptr;
    }
}


/*-------------------------------------------------------------------------------------------*/

static LeakDebugger inode_leak_debugger ("BFSync::INode");

INode::INode() :
  ref_count (1)
{
  inode_leak_debugger.add (this);
}

INode::INode (const INode& other) :
  ref_count (1)
{
  inode_leak_debugger.add (this);

  vmin      = other.vmin;
  vmax      = other.vmax;
  id        = other.id;
  uid       = other.uid;
  gid       = other.gid;
  size      = other.size;
  hash      = other.hash;
  mtime     = other.mtime;
  mtime_ns  = other.mtime_ns;
  ctime     = other.ctime;
  ctime_ns  = other.ctime_ns;
  mode      = other.mode;
  link      = other.link;
  type      = other.type;
  major     = other.major;
  minor     = other.minor;
  nlink     = other.nlink;
  ino       = other.ino;
  links     = other.links;      /* FIXME: deep copy */
  updated   = other.updated;
}

INode::~INode()
{
  inode_leak_debugger.del (this);
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
INode::save (SQLStatement& stmt)
{
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

  stmt.reset();
  stmt.bind_int   (1 + INODES_VMIN, vmin);
  stmt.bind_int   (1 + INODES_VMAX, vmax);
  stmt.bind_text  (1 + INODES_ID, id.str());
  stmt.bind_int   (1 + INODES_UID, uid);
  stmt.bind_int   (1 + INODES_GID, gid);
  stmt.bind_int   (1 + INODES_MODE, mode);
  stmt.bind_text  (1 + INODES_TYPE, type_str);
  stmt.bind_text  (1 + INODES_HASH, hash);
  stmt.bind_text  (1 + INODES_LINK, link);
  stmt.bind_int   (1 + INODES_SIZE, size);
  stmt.bind_int   (1 + INODES_MAJOR, major);
  stmt.bind_int   (1 + INODES_MINOR, minor);
  stmt.bind_int   (1 + INODES_NLINK, nlink);
  stmt.bind_int   (1 + INODES_CTIME, ctime);
  stmt.bind_int   (1 + INODES_CTIME_NS, ctime_ns);
  stmt.bind_int   (1 + INODES_MTIME, mtime);
  stmt.bind_int   (1 + INODES_MTIME_NS, mtime_ns);
  stmt.step();

  return true;
}

bool
INode::load (const Context& ctx, const ID& id)
{
  bool found = false;

  SQLStatement& load_inode = INodeRepo::the()->sql_statements().get
    ("SELECT * FROM inodes WHERE id = ? AND ? >= vmin AND ? <= vmax");

  load_inode.reset();
  load_inode.bind_text (1, id.str());
  load_inode.bind_int (2, ctx.version);
  load_inode.bind_int (3, ctx.version);

  for (;;)
    {
      int rc = load_inode.step();
      if (rc != SQLITE_ROW)
        break;

      vmin     = load_inode.column_int  (INODES_VMIN);
      vmax     = load_inode.column_int  (INODES_VMAX);
      this->id = load_inode.column_id   (INODES_ID);
      uid      = load_inode.column_int  (INODES_UID);
      gid      = load_inode.column_int  (INODES_GID);
      mode     = load_inode.column_int  (INODES_MODE);

      string val = load_inode.column_text (INODES_TYPE);

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

      hash     = load_inode.column_text (INODES_HASH);
      link     = load_inode.column_text (INODES_LINK);
      size     = load_inode.column_int  (INODES_SIZE);
      major    = load_inode.column_int  (INODES_MAJOR);
      minor    = load_inode.column_int  (INODES_MINOR);
      nlink    = load_inode.column_int  (INODES_NLINK);
      ctime    = load_inode.column_int  (INODES_CTIME);
      ctime_ns = load_inode.column_int  (INODES_CTIME_NS);
      mtime    = load_inode.column_int  (INODES_MTIME);
      mtime_ns = load_inode.column_int  (INODES_MTIME_NS);

      found = true;
    }
  if (!found)
    return false;

  // load links
  assert (!links);

  // setup shared (via cache) links
  INodeLinksPtr& cache_links = INodeRepo::the()->links_cache[id];
  if (!cache_links)
    cache_links = INodeLinksPtr (new INodeLinks());

  links = cache_links;

  SQLStatement& load_links = INodeRepo::the()->sql_statements().get
    ("SELECT * FROM links WHERE dir_id = ? AND ? >= vmin AND ? <= vmax");

  load_links.reset();
  load_links.bind_text (1, id.str());
  load_links.bind_int (2, ctx.version);
  load_links.bind_int (3, ctx.version);
  for (;;)
    {
      int rc = load_links.step();
      if (rc != SQLITE_ROW)
        break;

      Link *link = new Link();

      link->vmin = load_links.column_int (0);
      link->vmax = load_links.column_int (1);
      link->dir_id = load_links.column_id (2);
      link->inode_id = load_links.column_id (3);
      link->name = load_links.column_text (4);

      links.update()->link_map[link->name].add (LinkPtr (link));
    }

  load_or_alloc_ino();
  updated = false;
  return true;
}

vector<ino_t> INode::ino_pool;

void
INode::load_or_alloc_ino()
{
  // load inode number
  ino = 0;
  SQLStatement& loadi_stmt = INodeRepo::the()->sql_statements().get (
    "SELECT ino FROM local_inodes WHERE id = ?"
  );
  loadi_stmt.reset();
  loadi_stmt.bind_text (1, id.str());
  for (;;)
    {
      int rc = loadi_stmt.step();
      if (rc != SQLITE_ROW)
        break;
      ino = loadi_stmt.column_int (0);
    }

  // need to create new entry
  if (!ino)
    alloc_ino();
}

void
INode::alloc_ino()
{
  while (ino_pool.empty())
    {
      SQLStatement& searchi_stmt = INodeRepo::the()->sql_statements().get (
        "SELECT * FROM local_inodes WHERE ino in (?, ?, ?, ?, ?, "
                                                " ?, ?, ?, ?, ?, "
                                                " ?, ?, ?, ?, ?, "
                                                " ?, ?, ?, ?, ?)"
      );

      searchi_stmt.reset();
      while (ino_pool.size() != 20)
        {
          /* low inode numbers are reserved for .bfsync inodes */
          ino = g_random_int_range (100 * 1000, 2 * 1000 * 1000 * 1000);  // 100000 .. ~ 2^31
          map<ino_t, ID>::const_iterator ni = INodeRepo::the()->new_inodes.find (ino);
          if (ni == INodeRepo::the()->new_inodes.end())  // not recently allocated & in use
            {
              searchi_stmt.bind_int (1 + ino_pool.size(), ino);
              ino_pool.push_back (ino);
            }
        }
      int rc = searchi_stmt.step();
      if (rc == SQLITE_ROW)  // (at least one) inode numbers already in use
        ino_pool.clear();
    }
  ino = ino_pool.back();
  ino_pool.pop_back();
  INodeRepo::the()->new_inodes[ino] = id;
}

string
INode::new_file_path() const
{
  string id_str = id.str();
  return Options::the()->repo_path + "/new/" + id_str.substr (0, 2) + "/" + id_str.substr (2);
}

string
INode::file_path() const
{
  FileStatus fs = file_status();
  if (fs == FS_CHANGED)
    return new_file_path();
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
      string old_name = file_path();
      string new_name = new_file_path();

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

void
INode::add_link (const Context& ctx, INodePtr to, const string& name)
{
  Link *link = new Link();

  link->vmin = ctx.version;
  link->vmax = ctx.version;
  link->dir_id = id;
  link->inode_id = to->id;
  link->name = name;

  to.update()->nlink++;

  links.update()->link_map[name].add (LinkPtr (link));
}

bool
INode::unlink (const Context& ctx, const string& name)
{
  LinkPtr& lp = links.update()->link_map[name].find_version (ctx.version);
  if (lp && lp->name == name && !lp->deleted)
    {
      INodePtr inode (ctx, lp->inode_id);

      if (inode)
        inode.update()->nlink--;

      lp.update()->deleted = true;
      return true;
    }
  return false;
}

bool
INode::read_perm_ok (const Context& ctx) const
{
  if (ctx.fc->uid == 0)
    return true;

  if (ctx.fc->uid == uid)
    return (mode & S_IRUSR);

  if (ctx.fc->gid == gid)
    return (mode & S_IRGRP);

  return (mode & S_IROTH);
}

bool
INode::write_perm_ok (const Context& ctx) const
{
  if (ctx.fc->uid == 0)
    return true;

  if (ctx.fc->uid == uid)
    return (mode & S_IWUSR);

  if (ctx.fc->gid == gid)
    return (mode & S_IWGRP);

  return (mode & S_IWOTH);
}

bool
INode::search_perm_ok (const Context& ctx) const
{
  if (ctx.fc->uid == 0)
    return true;

  if (ctx.fc->uid == uid)
    return (mode & S_IXUSR);

  if (ctx.fc->gid == gid)
    return (mode & S_IXGRP);

  return (mode & S_IXOTH);
}

void
INode::get_child_names (const Context& ctx, vector<string>& names) const
{
  for (map<string, LinkVersionList>::const_iterator li = links->link_map.begin(); li != links->link_map.end(); li++)
    {
      const LinkVersionList& lvlist = li->second;
      const LinkPtr& lp = lvlist.find_version (ctx.version);
      if (lp && !lp->deleted)
        names.push_back (lp->name);
    }
}

INodePtr
INode::get_child (const Context& ctx, const string& name) const
{
  map<string, LinkVersionList>::const_iterator li = links->link_map.find (name);

  if (li == links->link_map.end())
    return INodePtr::null();

  const LinkPtr& lp = li->second.find_version (ctx.version);
  if (!lp)
    return INodePtr::null();

  if (lp->deleted)
    return INodePtr::null();

  return INodePtr (ctx, lp->inode_id);
}

size_t
INodeVersionList::size() const
{
  return inodes.size();
}

INodePtr&
INodeVersionList::operator[] (size_t pos)
{
  return inodes[pos];
}

void
INodeVersionList::add (INodePtr& p)
{
  inodes.push_back (p);
}
/*------------------------------*/

INodeLinks*
INodeLinksPtr::update() const
{
  ptr->updated = true;
  return ptr;
}

INodeLinksPtr::INodeLinksPtr (INodeLinks *inode_links) :
  ptr (inode_links)
{
}

INodeLinksPtr::~INodeLinksPtr()
{
  if (ptr)
    {
      ptr->unref();
      /* eager deletion */
      if (ptr->has_zero_refs())
        delete ptr;
      ptr = NULL;
    }
}

static INodeLinksPtr inode_links_ptr_null;

INodeLinksPtr&
INodeLinksPtr::null()
{
  return inode_links_ptr_null;
}


BFSync::LeakDebugger inode_links_leak_debugger ("BFSync::INodeLinks");

INodeLinks::INodeLinks() :
  ref_count (1)
{
  inode_links_leak_debugger.add (this);
}

INodeLinks::~INodeLinks()
{
  inode_links_leak_debugger.del (this);
}

bool
INodeLinks::save (SQLStatement& stmt)
{
  SQLStatement& del_links_stmt = INodeRepo::the()->sql_statements().get
    ("DELETE FROM links WHERE dir_id=? and inode_id=? and (vmin=? or vmax=?)");

  // delete links that were (possibly) modified
  for (map<string, LinkVersionList>::const_iterator li = link_map.begin(); li != link_map.end(); li++)
    {
      const LinkVersionList& lvlist = li->second;
      for (size_t i = 0; i < lvlist.size(); i++)
        {
          const LinkPtr& lp = lvlist[i];

          del_links_stmt.reset();
          del_links_stmt.bind_text (1, lp->dir_id.str());
          del_links_stmt.bind_text (2, lp->inode_id.str());
          del_links_stmt.bind_int  (3, lp->vmin);
          del_links_stmt.bind_int  (4, lp->vmax);
          del_links_stmt.step();
        }
    }
  // re-write links
  for (map<string, LinkVersionList>::const_iterator li = link_map.begin(); li != link_map.end(); li++)
    {
      const LinkVersionList& lvlist = li->second;
      for (size_t i = 0; i < lvlist.size(); i++)
        {
          const LinkPtr& lp = lvlist[i];

          if (!lp->deleted)
            {
              stmt.reset();
              stmt.bind_int  (1, lp->vmin);
              stmt.bind_int  (2, lp->vmax);
              stmt.bind_text (3, lp->dir_id.str());
              stmt.bind_text (4, lp->inode_id.str());
              stmt.bind_text (5, lp->name);
              stmt.step();
            }
        }
    }
  return true;
}
/*------------------------------*/

size_t
LinkVersionList::size() const
{
  return links.size();
}

void
LinkVersionList::add (const LinkPtr& ptr)
{
  links.push_back (ptr);
}

LinkPtr&
LinkVersionList::operator[] (size_t pos)
{
  return links[pos];
}

const LinkPtr&
LinkVersionList::operator[] (size_t pos) const
{
  return links[pos];
}

LinkPtr&
LinkVersionList::find_version (int version)
{
  for (size_t i = 0; i < links.size(); i++)
    {
      if (!links[i]->deleted && version >= links[i]->vmin && version <= links[i]->vmax)
        return links[i];
    }
  return LinkPtr::null();
}

const LinkPtr&
LinkVersionList::find_version (int version) const
{
  for (size_t i = 0; i < links.size(); i++)
    {
      if (!links[i]->deleted && version >= links[i]->vmin && version <= links[i]->vmax)
        return links[i];
    }
  return LinkPtr::null();
}

}
