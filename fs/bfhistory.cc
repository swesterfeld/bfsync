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

#include "bfhistory.hh"
#include "bfsyncfs.hh"
#include "bfbdb.hh"
#include <stdio.h>
#include <stdlib.h>

using std::max;
using std::vector;
using std::set;
using std::string;

namespace BFSync
{

History::History (BDB *bdb) :
  bdb (bdb)
{
}

unsigned int
History::current_version()
{
  return m_current_version;
}

const vector<unsigned int>&
History::all_versions()
{
  return m_all_versions;
}

const set<unsigned int>&
History::deleted_versions()
{
  return m_deleted_versions;
}

void
History::read()
{
  m_all_versions.clear();
  m_deleted_versions.clear();

  HistoryEntry history_entry;

  m_current_version = 1;
  for (unsigned int version = 1; bdb->load_history_entry (version, history_entry); version++)
    {
      m_current_version = max (version + 1, m_current_version);
      m_all_versions.push_back (version);

      vector<string> tags = bdb->list_tags (version);
      for (vector<string>::iterator ti = tags.begin(); ti != tags.end(); ti++)
        {
          if (*ti == "deleted")
            m_deleted_versions.insert (version);
        }
    }
  m_all_versions.push_back (m_current_version);

  debug ("current version is %d\n", m_current_version);
}

}
