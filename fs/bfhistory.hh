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

#ifndef BFSYNC_HISTORY_HH
#define BFSYNC_HISTORY_HH

#include <vector>
#include <set>

namespace BFSync {

class BDB;

class History
{
  unsigned int                m_current_version;
  std::vector<unsigned int>   m_all_versions;
  std::set<unsigned int>      m_deleted_versions;
  BDB                        *bdb;
public:
  History (BDB *bdb);

  unsigned int            current_version();
  const std::vector<unsigned int>&
                          all_versions();
  const std::set<unsigned int>&
                          deleted_versions();

  void                    read();
};

}

#endif
