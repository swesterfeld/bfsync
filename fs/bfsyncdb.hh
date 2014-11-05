// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#include "bfbdb.hh"
#include "bfdeduptable.hh"
#include "bfidsorter.hh"
#include <glib.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <queue>
#include <boost/unordered_set.hpp>

#undef major
#undef minor

struct ID {
  BFSync::ID id;

  bool       valid;

  ID();
  ID (const ID& id);
  ID (const std::string& id);
  ~ID();

  std::string
  no_prefix_str() const
  {
    return id.no_prefix_str();
  }
  std::string
  str() const
  {
    return id.str();
  }
  std::string
  pretty_str() const
  {
    return id.pretty_str();
  }
  bool
  operator== (const ID& other) const;
};

struct INode {
  INode();
  INode (const INode& inode);
  ~INode();

  bool         valid;

  unsigned int vmin, vmax;
  ID           id;
  unsigned int uid, gid;
  unsigned int mode, type;

  std::string hash;
  std::string link;

  uint64_t     size;
  unsigned int major, minor;
  unsigned int nlink;
  unsigned int mtime, mtime_ns;
  unsigned int ctime, ctime_ns;
  unsigned int new_file_number;
};

struct Link {
  Link();
  Link (const Link& link);
  ~Link();

  unsigned int vmin, vmax;
  ID dir_id;
  ID inode_id;
  std::string name;
};

struct HistoryEntry
{
  HistoryEntry();
  HistoryEntry (const HistoryEntry& he);
  ~HistoryEntry();

  bool          valid;

  unsigned int  version;
  std::string   hash;
  std::string   author;
  std::string   message;
  unsigned int  time;
};

struct TempFile {
  TempFile();
  TempFile (const TempFile& tf);
  ~TempFile();

  std::string   filename;
  unsigned int  pid;
};

struct JournalEntry {
  JournalEntry();
  JournalEntry (const JournalEntry& je);
  ~JournalEntry();

  std::string   operation;
  std::string   state;
};

struct Hash2FileEntry {
  Hash2FileEntry();
  Hash2FileEntry (const Hash2FileEntry& h2fe);
  ~Hash2FileEntry();

  bool          valid;

  std::string   hash;
  unsigned int  file_number;
};

class BDBWrapper
{
  unsigned int ref_count;
  BFSync::Mutex ref_mutex;
public:
  BDBWrapper();
  ~BDBWrapper();

  BFSync::BDB  *my_bdb;

  void
  ref()
  {
    BFSync::Lock lock (ref_mutex);

    g_return_if_fail (ref_count > 0);
    ref_count++;
  }

  void
  unref()
  {
    BFSync::Lock lock (ref_mutex);

    g_return_if_fail (ref_count > 0);
    ref_count--;
  }

  bool
  has_zero_refs()
  {
    BFSync::Lock lock (ref_mutex);

    return ref_count == 0;
  }
};

class BDBPtr {
  BDBWrapper *ptr;

public:
  BDBPtr (BDBWrapper *wrapper = NULL);
  BDBPtr (const BDBPtr& other);
  BDBPtr& operator= (const BDBPtr& other);

  ~BDBPtr();

  bool               open_ok();

  void               begin_transaction();
  void               commit_transaction();
  void               abort_transaction();

  INode              load_inode (const ID& id, unsigned int version);
  std::vector<INode> load_all_inodes (const ID& id);
  void               store_inode (const INode& inode);
  void               delete_inode (const INode& inode);

  unsigned int       clear_changed_inodes (unsigned int max_inodes);

  std::vector<Link>  load_links (const ID& id, unsigned int version);
  std::vector<Link>  load_all_links (const ID& id);
  void               store_link (const Link& link);
  void               delete_link (const Link& link);
  void               delete_links (const std::vector<Link>& links);

  void               walk();
  void               store_history_entry (int version,
                                          const std::string& hash,
                                          const std::string& author,
                                          const std::string& msg,
                                          int time);
  HistoryEntry       load_history_entry (int version);
  void               delete_history_entry (unsigned int version);

  void               store_hash2file (const std::string& hash, unsigned int file_number);
  unsigned int       load_hash2file (const std::string& hash);
  void               delete_hash2file (const std::string& hash);

  void               add_deleted_file (unsigned int file_number);
  std::vector<unsigned int>
                     load_deleted_files();
  unsigned int       clear_deleted_files (unsigned int max_files);

  void               add_temp_file (const std::string& filename, unsigned int pid);
  std::vector<TempFile> load_temp_files();
  void               delete_temp_file (const std::string& filename);

  std::vector<JournalEntry> load_journal_entries();
  void                      store_journal_entry (const JournalEntry& journal_entry);
  void                      clear_journal_entries();

  std::vector<std::string>  list_tags (unsigned int version);
  std::vector<std::string>  load_tag  (unsigned int version, const std::string& tag);
  void                      add_tag (unsigned int version, const std::string& tag, const std::string& value);
  void                      del_tag (unsigned int version, const std::string& tag, const std::string& value);

  std::vector<std::string>  get_variable (const std::string& variable);
  void                      set_variable (const std::string& variable, const std::vector<std::string>& value);

  unsigned int       gen_new_file_number();

  void               close();

  BFSync::BDB*
  get_bdb()
  {
    return ptr->my_bdb;
  }
};

extern BDBPtr             open_db (const std::string& db, int cache_size_mb, bool recover);
extern void               remove_db (const std::string& db);
extern bool               need_recover_db (const std::string& db);
extern ID                 id_root();
extern std::string        time_prof_result();
extern void               time_prof_reset();
extern void               print_leak_debugger_stats();

class DiffGenerator
{
  BFSync::DbcPtr dbc;

  BFSync::DataOutBuffer kbuf;
  BFSync::IDSorter      ids;
  size_t                id_pos;

  Dbt key;
  Dbt data;

  int dbc_ret;

  unsigned int v_old, v_new;

  BDBPtr bdb_ptr;
  std::queue< std::vector<std::string> > diffs;
public:
  DiffGenerator (BDBPtr bdb_ptr);
  ~DiffGenerator();

  std::vector<std::string> get_next();
};

class ChangedINodesIterator
{
  BFSync::DbcPtr dbc;
  int dbc_ret;
  BDBPtr bdb_ptr;

  BFSync::DataOutBuffer kbuf;
  Dbt key, data;
public:
  ChangedINodesIterator (BDBPtr bdb_ptr);
  ~ChangedINodesIterator();

  ID get_next();
};

class INodeHashIterator
{
  BFSync::DbcPtr              dbc;
  BFSync::AllRecordsIterator  db_it;
  BDBPtr                      bdb_ptr;

  boost::unordered_set<std::string> all_hashes;

  Dbt key, data;
public:
  INodeHashIterator (BDBPtr bdb_ptr);
  ~INodeHashIterator();

  std::string get_next();
};

struct IDHash
{
  static size_t
  size (unsigned char *)
  {
    return 20; /* 5 * uint32 */
  }
  static unsigned int
  hash (unsigned char *mem)
  {
    guint32 result;
    std::copy (mem, mem + 4, (unsigned char *) &result); // return id.a as hash
    return result;
  }
};

class AllINodesIterator
{
  BFSync::DbcPtr dbc;
  int dbc_ret;
  BDBPtr bdb_ptr;

  DedupTable<IDHash> known_ids;
  std::vector<ID>    ids;
  size_t             current_id_idx;

  Dbt key, data, multi_data;

  std::vector<char>  multi_data_buffer;
public:
  AllINodesIterator (BDBPtr bdb_ptr);
  ~AllINodesIterator();

  ID get_next();
};

class Hash2FileIterator
{
  Dbc            *cursor;
  BDBPtr          bdb_ptr;
  int             dbc_ret;
  Dbt             key, data;
public:
  Hash2FileIterator (BDBPtr bdb_ptr);
  ~Hash2FileIterator();

  Hash2FileEntry get_next();
};

class SortedArray
{
private:
  SortedArray (const SortedArray& other); // should not  be used

  std::vector<guint32> array;

public:
  SortedArray();
  ~SortedArray();

  void append (unsigned int n);
  void sort_unique();
  bool search (unsigned int n);

  unsigned int mem_usage();
};

class INodeRepoINode
{
  BFSync::INodePtr ptr;

public:
  INodeRepoINode (BFSync::INodePtr ptr);

  unsigned int              uid();
  void                      set_uid (unsigned int uid);

  unsigned int              gid();
  void                      set_gid (unsigned int gid);

  unsigned int              type();
  void                      set_type (unsigned int type);

  unsigned int              mode();
  void                      set_mode (unsigned int mode);

  std::string               hash();
  void                      set_hash (const std::string& hash);

  std::string               link();
  void                      set_link (const std::string& link);

  uint64_t                  size();
  void                      set_size (uint64_t size);

  unsigned int              major();
  void                      set_major (unsigned int major);

  unsigned int              minor();
  void                      set_minor (unsigned int minor);

  unsigned int              nlink();
  void                      set_nlink (unsigned int nlink);

  unsigned int              mtime();
  void                      set_mtime (unsigned int mtime);

  unsigned int              mtime_ns();
  void                      set_mtime_ns (unsigned int mtime_ns);

  unsigned int              ctime();
  void                      set_ctime (unsigned int ctime);

  unsigned int              ctime_ns();
  void                      set_ctime_ns (unsigned int ctime_ns);


  bool                      valid();
  void                      add_link (INodeRepoINode& child, const std::string& name, unsigned int version);
  void                      add_link_raw (INodeRepoINode& child, const std::string& name, unsigned int version);

  void                      unlink (const std::string& name, unsigned int version);
  void                      unlink_raw (const std::string& name, unsigned int version);

  std::vector<std::string>  get_child_names (unsigned int version);
  INodeRepoINode            get_child (unsigned int version, const std::string& name);
};

class INodeRepo
{
  BFSync::INodeRepo *inode_repo;

public:
  INodeRepo (BDBPtr bdb);
  ~INodeRepo();

  INodeRepoINode load_inode (const ID& id, unsigned int version);
  INodeRepoINode create_inode (const std::string& path, unsigned int version);
  INodeRepoINode create_inode_with_id (const ID& id, unsigned int version);
  void           save_changes();
  void           save_changes_no_txn();
  void           delete_unused_keep_count (unsigned int count);
};

class SQLExport
{
  unsigned int version;
  BDBPtr       bdb_ptr;
  int          transaction_ops;
  int          scan_ops;
  double       last_status_time;
  double       start_time;
  bool         sig_interrupted;

  unsigned int status_version;
  unsigned int status_max_version;

  BFSync::DataOutBuffer               out_buffer;
  BFSync::DataOutBuffer               len_buffer;
  std::map<unsigned int, std::string> filelist_map;
  std::string                         m_repo_id;

  BFSync::BDBError walk (const ID& id, const ID& parent_id, const std::string& name, FILE *file);
  void maybe_split_transaction();
  BFSync::BDBError build_filelist (unsigned int version, std::string& filename);

public:
  SQLExport (BDBPtr bdb);
  ~SQLExport();

  void export_version (unsigned int version, unsigned int max_version,
                       const std::string& insert_filename, const std::string& delete_filename);
  std::string repo_id();
  void update_status (const std::string& op_name, bool force_update);
};

struct HashCacheEntry
{
  HashCacheEntry();
  HashCacheEntry (const HashCacheEntry& he);
  ~HashCacheEntry();

  bool         valid;
  std::string  stat_hash;
  std::string  file_hash;
  unsigned int expire_time;
};

class HashCacheDict
{
public:
  struct DictKey
  {
    unsigned int a, b, c, d, e;
  };

  struct DictValue
  {
    unsigned int a, b, c, d, e;
    unsigned int expire_time;
  };

  boost::unordered_map<HashCacheDict::DictKey, HashCacheDict::DictValue> hc_dict;

  void              insert (const std::string& stat_hash, const std::string& file_hash, unsigned int expire_time);
  HashCacheEntry    lookup (const std::string& stat_hash);
  void              save (const std::string& filename);
  void              load (const std::string& filename, unsigned int load_time);
};

static inline size_t
hash_value (const HashCacheDict::DictKey& dk)
{
  return dk.a;
}

class HashCacheIterator
{
  boost::unordered_map<HashCacheDict::DictKey, HashCacheDict::DictValue>::iterator it;
  HashCacheDict& dict;

public:
  HashCacheIterator (HashCacheDict& dict);

  HashCacheEntry get_next();
};

std::vector<std::string> check_inodes_links_integrity (BDBPtr bdb);

class BDBException
{
private:
  BFSync::BDBError m_error;

public:
  BDBException (BFSync::BDBError error);

  BFSync::BDBError  error() const;
  std::string       error_string() const;
};

std::string repo_version();

const unsigned int VERSION_INF = 0xffffffff;

const unsigned int FILE_REGULAR     = BFSync::FILE_REGULAR;
const unsigned int FILE_SYMLINK     = BFSync::FILE_SYMLINK;
const unsigned int FILE_DIR         = BFSync::FILE_DIR;
const unsigned int FILE_FIFO        = BFSync::FILE_FIFO;
const unsigned int FILE_SOCKET      = BFSync::FILE_SOCKET;
const unsigned int FILE_BLOCK_DEV   = BFSync::FILE_BLOCK_DEV;
const unsigned int FILE_CHAR_DEV    = BFSync::FILE_CHAR_DEV;
