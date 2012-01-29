/*
  bfsync: Big File synchronization tool

  Copyright (C) 2012 Stefan Westerfeld

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

#ifndef BFSYNC_TIME_PROF_HH
#define BFSYNC_TIME_PROF_HH

#include <string>
#include <vector>

namespace BFSync
{

class TimeProfSection
{
  std::string m_name;
  double      m_time;
  int         m_calls;

public:
  TimeProfSection (const std::string& name);

  std::string     name();
  int             calls();
  double          time() const;
  void            add_time (double time);
  void            reset();
};

class TimeProfHandle
{
  TimeProfSection *m_section;
  double           start_t;

  void begin_profile();
  void end_profile();

public:
  TimeProfHandle (TimeProfSection& section);
  ~TimeProfHandle();
};

class TimeProf
{
  std::vector<TimeProfSection *> m_sections;

public:
  static TimeProf* the();

  void                            add_section (TimeProfSection *section);
  std::string                     result();
  void                            reset();
};

}

#endif /* BFSYNC_TIME_PROF_HH */
