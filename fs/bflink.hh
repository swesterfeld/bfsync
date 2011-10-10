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

namespace BFSync
{

struct Link
{
  int          vmin, vmax;
  std::string  dir_id;
  std::string  inode_id;
  std::string  name;
  bool         deleted;
  bool         updated;

  Link();
  ~Link();

  bool save();
};

class LinkPtr
{
  Link *ptr;
public:
  LinkPtr (Link *link);

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

};

}

#endif
