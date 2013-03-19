// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#ifndef BFSYNC_LEAK_DEBUGGER_HH
#define BFSYNC_LEAK_DEBUGGER_HH

#include "bfsyncfs.hh"

#include <string>
#include <boost/unordered_map.hpp>

namespace BFSync
{

class LeakDebugger
{
  Mutex                              mutex;
  boost::unordered_map<void *, int>  ptr_map;
  std::string                        type;

  void ptr_add (void *p);
  void ptr_del (void *p);

public:
  LeakDebugger (const std::string& name);
  ~LeakDebugger();

  template<class T> void add (T *instance) { ptr_add (static_cast<void *> (instance)); }
  template<class T> void del (T *instance) { ptr_del (static_cast<void *> (instance)); }
  static void print_stats();
};

}

#endif /* BFSYNC_LEAK_DEBUGGER_HH */
