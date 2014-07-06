// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#ifndef BFSYNC_HISTORY_HH
#define BFSYNC_HISTORY_HH

#include <vector>
#include <set>

namespace BFSync {

class BDB;

class History
{
  std::vector<bool>           m_version_exists;
  BDB                        *bdb;

public:
  History (BDB *bdb);

  void          read();

  unsigned int  current_version() const;
  bool          have_version (unsigned int version) const;

  // for iterating over each history entry
  unsigned int  vbegin() const;
  unsigned int  vend() const;
};

}

#endif
