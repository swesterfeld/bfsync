#include "bfinode.hh"
#include "bflink.hh"
#include "bfleakdebugger.hh"
#include <glib.h>
#include <string>

using std::string;

namespace BFSync
{

LinkPtr::LinkPtr (Link *link)
{
  ptr = link;
}

LinkPtr::~LinkPtr()
{
  if (ptr)
    {
      ptr->unref();
      /* eager deletion (inodes use lazy deletion) */
      if (ptr->has_zero_refs())
        delete ptr;
      ptr = NULL;
    }
}

/*-------------------------------------------------------------------------------------------*/

static LeakDebugger leak_debugger ("BFSync::Link");

Link::Link() :
  ref_count (1),
  deleted (false)
{
  leak_debugger.add (this);
}

Link::~Link()
{
  leak_debugger.del (this);
}

}
