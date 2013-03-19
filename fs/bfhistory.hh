// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

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
