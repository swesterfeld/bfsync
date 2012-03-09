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

#include <vector>
#include <string.h>

static const unsigned int primes[] = /* from glib */
{
  61,
  127,
  251,
  509,
  1021,
  2039,
  4093,
  8191,
  16381,
  32749,
  65521,
  131071,
  262139,
  524287,
  1048573,
  2097143,
  4194301,
  8388593,
  16777213,
  33554393,
  67108859,
  134217689,
  268435399,
  536870909,
  1073741789,
  2147483647
};

const size_t FREE = ~0;

template<class T>
class DedupTable
{
  std::vector<unsigned char> data;
  std::vector<size_t>        buckets;
  size_t                     n_entries;
  double                     resize_factor;

  size_t insert_bucket (unsigned char *buffer, size_t buffer_size);
  void   grow();

public:
  DedupTable();

  void set_resize_factor (double f);

  bool insert (unsigned char *buffer);
  void print_mem_usage();
  size_t size();
};

template<class T>
DedupTable<T>::DedupTable() :
  buckets (primes[0], FREE),
  n_entries (0),
  resize_factor (0.5)
{
}

template<class T> void
DedupTable<T>::set_resize_factor (double f)
{
  assert (f > 0.001 && f < 0.999);

  resize_factor = f;
}

template<class T> size_t
DedupTable<T>::insert_bucket (unsigned char *buffer, size_t buffer_size)
{
  size_t bucket = T::hash (buffer) % buckets.size();

  // search in bucket
  size_t data_pos = 0;
  for (size_t i = 0; i < buckets.size(); i++)
    {
      data_pos = buckets[bucket];
      if (data_pos == FREE)
        {
          break;
        }
      else
        {
          size_t data_size = T::size (&data[data_pos]);
          if (data_size == buffer_size)
            if (memcmp (&data[data_pos], buffer, data_size) == 0)
              return buckets.size();
        }
      bucket++;
      if (bucket == buckets.size())
        bucket = 0;
    }
  assert (data_pos == FREE);

  return bucket;
}

template<class T> void
DedupTable<T>::grow()
{
  size_t i = 0;
  while (buckets.size() >= primes[i])
    i++;

  buckets.resize (primes[i]);
  std::fill (buckets.begin(), buckets.end(), FREE);

  unsigned char *entry = &data[0];
  while (entry < &data[data.size()])
    {
      size_t buffer_size = T::size (entry);
      size_t bucket = insert_bucket (entry, buffer_size);

      buckets[bucket] = entry - &data[0];
      entry += buffer_size;
    }
}


template<class T> bool
DedupTable<T>::insert (unsigned char *buffer)
{
  size_t buffer_size = T::size (buffer);
  size_t bucket = insert_bucket (buffer, buffer_size);

  if (bucket == buckets.size()) // already present
    return true;

  // add new entry

  buckets[bucket] = data.size();
  data.insert (data.end(), buffer, buffer + buffer_size);

  // resize if necessary
  n_entries++;
  if (n_entries > (buckets.size() * resize_factor)) /* grow if too full */
    grow();
  return false;
}

template<class T> void
DedupTable<T>::print_mem_usage()
{
  printf ("data:     %zd  - capacity %zd\n", data.size(), data.capacity());
  printf ("buckets:  %zd  - capacity %zd\n", buckets.size() * sizeof (buckets[0]),
                                             buckets.capacity() * sizeof (buckets[0]));
  printf ("per-item: %.2f\n", double (data.size() + buckets.size() * sizeof (buckets[0])) / n_entries);
}

template<class T> size_t
DedupTable<T>::size()
{
  return n_entries;
}
