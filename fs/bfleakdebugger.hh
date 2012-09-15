/*
 * Copyright (C) 2011 Stefan Westerfeld
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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
