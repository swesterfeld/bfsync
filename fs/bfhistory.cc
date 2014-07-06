// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

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
History::current_version() const
{
  return m_version_exists.size() - 1;
}

unsigned int
History::vbegin() const
{
  return 1;
}

unsigned int
History::vend() const
{
  return m_version_exists.size();
}

bool
History::have_version (unsigned int version) const
{
  if (version > 0 && version < m_version_exists.size())
    {
      return m_version_exists[version];
    }
  else
    {
      return false;
    }
}

void
History::read()
{
  m_version_exists.clear();
  m_version_exists.push_back (false);   // version 0 never exists

  HistoryEntry history_entry;

  for (unsigned int version = 1; bdb->load_history_entry (version, history_entry); version++)
    {
      bool version_exists = true;

      vector<string> tags = bdb->list_tags (version);
      for (vector<string>::iterator ti = tags.begin(); ti != tags.end(); ti++)
        {
          if (*ti == "deleted")
            {
              version_exists = false;
              break;
            }
        }
      m_version_exists.push_back (version_exists);
    }
  m_version_exists.push_back (true);    // current_version always exists

  debug ("current version is %d\n", current_version());
}

}
