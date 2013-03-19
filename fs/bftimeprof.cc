// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#include "bftimeprof.hh"
#include "bfsyncfs.hh"
#include <assert.h>

using std::vector;
using std::string;

namespace BFSync {

static TimeProf *instance = NULL;

TimeProf *
TimeProf::the()
{
  if (!instance)
    instance = new TimeProf();

  return instance;
}

void
TimeProf::reset()
{
  for (size_t s = 0; s < m_sections.size(); s++)
    {
      m_sections[s]->reset();
    }
}

TimeProfSection::TimeProfSection (const string& name) :
  m_name (name),
  m_time (0),
  m_calls (0)
{
  TimeProf::the()->add_section (this);
}

string
TimeProfSection::name()
{
  return m_name;
}

double
TimeProfSection::time() const
{
  return m_time;
}

int
TimeProfSection::calls()
{
  return m_calls;
}

void
TimeProfSection::add_time (double time)
{
  m_time += time;
  m_calls++;
}

void
TimeProfSection::reset()
{
  m_time = 0;
  m_calls = 0;
}

void
TimeProf::add_section (TimeProfSection *section)
{
  m_sections.push_back (section);
}

TimeProfHandle::TimeProfHandle (TimeProfSection& section) :
  m_section (&section)
{
  begin_profile();
}

void
TimeProfHandle::begin_profile()
{
  timespec time_now;

  int r = clock_gettime (CLOCK_REALTIME, &time_now);
  assert (r == 0);

  start_t = double (time_now.tv_nsec) / double (1000 * 1000 * 1000) + double (time_now.tv_sec);
}

TimeProfHandle::~TimeProfHandle()
{
  end_profile();
}

void
TimeProfHandle::end_profile()
{
  timespec time_now;

  int r = clock_gettime (CLOCK_REALTIME, &time_now);
  assert (r == 0);

  double end_t = double (time_now.tv_nsec) / double (1000 * 1000 * 1000) + double (time_now.tv_sec);

  m_section->add_time (end_t - start_t);
}

string
TimeProf::result()
{
  string result;

  double total = 0;
  for (size_t s = 0; s < m_sections.size(); s++)
    {
      total += m_sections[s]->time();
    }
  result += string_printf ("total time accounted for: %f seconds\n\n", total);
  for (size_t s = 0; s < m_sections.size(); s++)
    {
      result += string_printf ("%8d %-22s %3.3f\n",
        m_sections[s]->calls(),
        m_sections[s]->name().c_str(),
        m_sections[s]->time() * 100 / total);
    }
  return result;
}

}
