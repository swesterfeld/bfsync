// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

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
