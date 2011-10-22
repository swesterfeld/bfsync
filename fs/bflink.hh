/*
  bfsync: Big File synchronization based on Git - FUSE filesystem

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

#ifndef BFSYNC_LINK_HH
#define BFSYNC_LINK_HH

#include "bfinode.hh"
#include "bfidhash.hh"

namespace BFSync
{

class Link
{
  unsigned int ref_count;
  Mutex        ref_mutex;

public:
  int          vmin, vmax;
  ID           dir_id;
  ID           inode_id;
  std::string  name;
  bool         deleted;
  bool         updated;

  void
  ref()
  {
    Lock lock (ref_mutex);

    g_return_if_fail (ref_count > 0);
    ref_count++;
  }

  void
  unref()
  {
    Lock lock (ref_mutex);

    g_return_if_fail (ref_count > 0);
    ref_count--;
  }

  bool
  has_zero_refs()
  {
    Lock lock (ref_mutex);

    return ref_count == 0;
  }

  Link();
  ~Link();
};

class LinkPtr
{
  Link *ptr;
public:
  LinkPtr (Link *link = NULL);
  ~LinkPtr();

  LinkPtr (const LinkPtr& other)
  {
    Link *new_ptr = other.ptr;

    if (new_ptr)
      new_ptr->ref();

    ptr = new_ptr;
  }

  LinkPtr&
  operator= (const LinkPtr& other)
  {
    Link *new_ptr = other.ptr;
    Link *old_ptr = ptr;

    if (new_ptr)
      new_ptr->ref();

    ptr = new_ptr;

    if (old_ptr)
      old_ptr->unref();

    return *this;
  }

  operator bool() const
  {
    return (ptr != 0);
  }
  const Link*
  operator->() const
  {
    return ptr;
  }
  Link*
  update() const
  {
    ptr->updated = true;
    return ptr;
  }
  static LinkPtr& null();
};

}

#endif
