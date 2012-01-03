/*
  bfsync: Big File synchronization tool

  Copyright (C) 2011 Stefan Westerfeld

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <glib.h>
#include <sys/time.h>
#include <assert.h>

#include "bfinode.hh"
#include "bfsyncfs.hh"
#include "bfleakdebugger.hh"
#include "bflink.hh"
#include "bfbdb.hh"

#include <set>

using std::string;
using std::vector;
using std::map;
using std::set;

namespace BFSync {

static INodeRepo *inode_repo = 0;

INodeRepo::INodeRepo()
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

  int inodes_saved = 0;
  for (map<ID, INodeVersionList>::iterator ci = cache.begin(); ci != cache.end(); ci++)
    {
      INodeVersionList& ivlist = ci->second;
      bool need_save = false;

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
          // this will reliably delete the old inode entry for both modifications that
          // can be made for an inode entry:
          //  - just change some fields
          //  - split into two inodes (copy-on-write)
          BDB::the()->delete_inodes (ivlist);
        }

      INodeLinksPtr links = INodeLinksPtr::null();
      for (size_t i = 0; i < ivlist.size(); i++)
        {
          INodePtr inode_ptr = ivlist[i];

          if (inode_ptr && need_save)
            {
              inodes_saved++;

              INode *inode = inode_ptr.get_ptr_without_update();
              inode->save();
              inode->updated = false;
              if (!links)
                links = inode->links;
            }
        }
      if (need_save && links)
        {
          INodeLinks *inode_links = links.get_ptr_without_update();
          inode_links->save();
        }
    }
  if (sc == SC_CLEAR_CACHE)
    {
      cache.clear();
      links_cache.clear();
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
              map<ID, INodeLinksPtr>::iterator lci = links_cache.find (id);
              if (lci != links_cache.end())
                links_cache.erase (lci);

              cache.erase (ci);
              // do not access id after this point (deleted)
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

INodePtr::INodePtr (const Context& ctx, const char *path)
{
  ptr = new INode();
  ptr->vmin = ctx.version;
  ptr->vmax = ctx.version;
  ptr->id = ID::gen_new (path);
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
INode::save()
{
  if (nlink != 0) // nlink == 0 means that the inode is not referenced anymore and can be deleted
    BDB::the()->store_inode (this);

  return true;
}

bool
INode::load (const Context& ctx, const ID& id)
{
  bool found = BDB::the()->load_inode (id, ctx.version, this);

  if (!found)
    return false;

  // load links
  assert (!links);

  // setup shared (via cache) links
  INodeLinksPtr& cache_links = INodeRepo::the()->links_cache[id];
  if (!cache_links)
    cache_links = INodeLinksPtr (new INodeLinks());

  links = cache_links;

  vector<Link*> load_links;
  BDB::the()->load_links (load_links, id, ctx.version);

  for (vector<Link*>::const_iterator li = load_links.begin(); li != load_links.end(); li++)
    links.update()->link_map[(*li)->name].add (LinkPtr (*li));

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
  if (BDB::the()->load_ino (id, ino))
    return;

  // need to create new entry
  alloc_ino();
}

void
INode::alloc_ino()
{
  do
    {
      /* low inode numbers are reserved for .bfsync inodes */
      ino = g_random_int_range (100 * 1000, 2 * 1000 * 1000 * 1000);  // 100000 .. ~ 2^31
    }
  while (!BDB::the()->try_store_id2ino (id, ino));
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

  if (ctx.fc->uid == uid || !Options::the()->use_uid_gid)
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

  if (ctx.fc->uid == uid || !Options::the()->use_uid_gid)
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

  if (ctx.fc->uid == uid || !Options::the()->use_uid_gid)
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

const INodePtr&
INodeVersionList::operator[] (size_t pos) const
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
INodeLinks::save()
{
  // delete links that were (possibly) modified
  for (map<string, LinkVersionList>::const_iterator li = link_map.begin(); li != link_map.end(); li++)
    {
      const LinkVersionList& lvlist = li->second;
      BDB::the()->delete_links (lvlist);
    }
  // re-write links
  for (map<string, LinkVersionList>::const_iterator li = link_map.begin(); li != link_map.end(); li++)
    {
      const LinkVersionList& lvlist = li->second;
      for (size_t i = 0; i < lvlist.size(); i++)
        {
          const LinkPtr& lp = lvlist[i];

          if (!lp->deleted)
            BDB::the()->store_link (lp);

          Link *link = lp.get_ptr_without_update();
          link->updated = false;
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
