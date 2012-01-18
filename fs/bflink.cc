/*
  bfsync: Big File synchronization tool - FUSE filesystem

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

#include "bfinode.hh"
#include "bflink.hh"
#include "bfleakdebugger.hh"
#include "bfhistory.hh"
#include "bfbdb.hh"
#include <glib.h>
#include <string>

using std::string;

namespace BFSync
{

LinkPtr::LinkPtr (Link *link)
{
  ptr = link;
}

LinkPtr::~LinkPtr()
{
  if (ptr)
    {
      ptr->unref();
      /* eager deletion (inodes use lazy deletion) */
      if (ptr->has_zero_refs())
        delete ptr;
      ptr = NULL;
    }
}

static LinkPtr link_ptr_null;

LinkPtr&
LinkPtr::null()
{
  return link_ptr_null;
}

Link*
LinkPtr::update() const
{
  g_return_val_if_fail (ptr, NULL);

  if (!ptr->updated && ptr->vmin != INodeRepo::the()->bdb->history()->current_version())
    {
      g_assert (ptr->vmax == VERSION_INF);

      // Link copy-on-write
      Link *old_ptr = new Link (*ptr);
      old_ptr->vmax = INodeRepo::the()->bdb->history()->current_version() - 1;
      old_ptr->updated = true;

      ptr->vmin = INodeRepo::the()->bdb->history()->current_version();
      ptr->updated = true;

      // add old version to cache
      Lock lock (INodeRepo::the()->mutex);
      INodeLinksPtr& inp = INodeRepo::the()->links_cache [ptr->dir_id];
      LinkVersionList& lvlist = inp.update()->link_map[ptr->name];
      LinkPtr old_link (old_ptr);

      Link *result = ptr;       // access ptr here, because it might be dead later...
      lvlist.add (old_link);    // <- this might "delete this;", since the LinkPtr is stored in a vector

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

static LeakDebugger leak_debugger ("BFSync::Link");

Link::Link() :
  ref_count (1),
  deleted (false)
{
  leak_debugger.add (this);
}

Link::Link (const Link& other) :
  ref_count (1)
{
  leak_debugger.add (this);

  vmin      = other.vmin;
  vmax      = other.vmax;
  dir_id    = other.dir_id;
  inode_id  = other.inode_id;
  name      = other.name;
  deleted   = other.deleted;
  updated   = other.updated;
}


Link::~Link()
{
  leak_debugger.del (this);
}

}
