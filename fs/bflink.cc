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

/*-------------------------------------------------------------------------------------------*/

static LeakDebugger leak_debugger ("BFSync::Link");

Link::Link() :
  deleted (false)
{
  leak_debugger.add (this);
}

Link::~Link()
{
  leak_debugger.del (this);
}

}
