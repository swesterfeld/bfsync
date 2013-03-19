// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#ifndef BFSYNC_LINK_HH
#define BFSYNC_LINK_HH

#include "bfinode.hh"
#include "bfidhash.hh"

namespace BFSync
{

class Link
{
  unsigned int ref_count;
  Mutex        ref_mutex;

public:
  unsigned int vmin, vmax;
  ID           dir_id;
  ID           inode_id;
  std::string  name;
  bool         deleted;
  bool         updated;

  void
  ref()
  {
    Lock lock (ref_mutex);

    g_return_if_fail (ref_count > 0);
    ref_count++;
  }

  void
  unref()
  {
    Lock lock (ref_mutex);

    g_return_if_fail (ref_count > 0);
    ref_count--;
  }

  bool
  has_zero_refs()
  {
    Lock lock (ref_mutex);

    return ref_count == 0;
  }

  Link();
  Link (const Link& other);
  ~Link();
};

class LinkPtr
{
  Link *ptr;
public:
  LinkPtr (Link *link = NULL);
  ~LinkPtr();

  LinkPtr (const LinkPtr& other)
  {
    Link *new_ptr = other.ptr;

    if (new_ptr)
      new_ptr->ref();

    ptr = new_ptr;
  }
  LinkPtr&
  operator= (const LinkPtr& other)
  {
    Link *new_ptr = other.ptr;
    Link *old_ptr = ptr;

    if (new_ptr)
      new_ptr->ref();

    ptr = new_ptr;

    if (old_ptr)
      old_ptr->unref();

    return *this;
  }

  operator bool() const
  {
    return (ptr != 0);
  }
  const Link*
  operator->() const
  {
    g_return_val_if_fail (ptr, NULL);

    return ptr;
  }
  /* never use this if you can use update() instead */
  Link*
  get_ptr_without_update() const
  {
    return ptr;
  }
  Link* update() const;
  static LinkPtr& null();
};

}

#endif
