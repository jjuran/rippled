// Copyright (c) 2012 Facebook. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/ldb_cmd.h"
#include <dirent.h>

#include <sstream>
#include <string>
#include <stdexcept>

#include "leveldb/write_batch.h"
#include "db/dbformat.h"
#include "db/log_reader.h"
#include "db/filename.h"
#include "db/write_batch_internal.h"

namespace leveldb {

using namespace std;

const string LDBCommand::ARG_DB = "db";
const string LDBCommand::ARG_HEX = "hex";
const string LDBCommand::ARG_KEY_HEX = "key_hex";
const string LDBCommand::ARG_VALUE_HEX = "value_hex";
const string LDBCommand::ARG_TTL = "ttl";
const string LDBCommand::ARG_FROM = "from";
const string LDBCommand::ARG_TO = "to";
const string LDBCommand::ARG_MAX_KEYS = "max_keys";
const string LDBCommand::ARG_BLOOM_BITS = "bloom_bits";
const string LDBCommand::ARG_COMPRESSION_TYPE = "compression_type";
const string LDBCommand::ARG_BLOCK_SIZE = "block_size";
const string LDBCommand::ARG_AUTO_COMPACTION = "auto_compaction";
const string LDBCommand::ARG_WRITE_BUFFER_SIZE = "write_buffer_size";
const string LDBCommand::ARG_FILE_SIZE = "file_size";
const string LDBCommand::ARG_CREATE_IF_MISSING = "create_if_missing";

const char* LDBCommand::DELIM = " ==> ";

LDBCommand* LDBCommand::InitFromCmdLineArgs(
  int argc,
  char** argv,
  Options options
) {
  vector<string> args;
  for (int i = 1; i < argc; i++) {
    args.push_back(argv[i]);
  }
  return InitFromCmdLineArgs(args, options);
}

/**
 * Parse the command-line arguments and create the appropriate LDBCommand2
 * instance.
 * The command line arguments must be in the following format:
 * ./ldb --db=PATH_TO_DB [--commonOpt1=commonOpt1Val] ..
 *        COMMAND <PARAM1> <PARAM2> ... [-cmdSpecificOpt1=cmdSpecificOpt1Val] ..
 * This is similar to the command line format used by HBaseClientTool.
 * Command name is not included in args.
 * Returns nullptr if the command-line cannot be parsed.
 */
LDBCommand* LDBCommand::InitFromCmdLineArgs(
  const vector<string>& args,
  Options options
) {
  // --x=y command line arguments are added as x->y map entries.
  map<string, string> option_map;

  // Command-line arguments of the form --hex end up in this array as hex
  vector<string> flags;

  // Everything other than option_map and flags. Represents commands
  // and their parameters.  For eg: put key1 value1 go into this vector.
  vector<string> cmdTokens;

  const string OPTION_PREFIX = "--";

  for (vector<string>::const_iterator itr = args.begin();
      itr != args.end(); itr++) {
    string arg = *itr;
    if (arg[0] == '-' && arg[1] == '-'){
      vector<string> splits = stringSplit(arg, '=');
      if (splits.size() == 2) {
        string optionKey = splits[0].substr(OPTION_PREFIX.size());
        option_map[optionKey] = splits[1];
      } else {
        string optionKey = splits[0].substr(OPTION_PREFIX.size());
        flags.push_back(optionKey);
      }
    } else {
      cmdTokens.push_back(string(arg));
    }
  }

  if (cmdTokens.size() < 1) {
    fprintf(stderr, "Command not specified!");
    return nullptr;
  }

  string cmd = cmdTokens[0];
  vector<string> cmdParams(cmdTokens.begin()+1, cmdTokens.end());
  LDBCommand* command = LDBCommand::SelectCommand(
    cmd,
    cmdParams,
    option_map,
    flags
  );

  if (command) {
    command->SetOptions(options);
  }
  return command;
}

LDBCommand* LDBCommand::SelectCommand(
    const std::string& cmd,
    vector<string>& cmdParams,
    map<string, string>& option_map,
    vector<string>& flags
  ) {

  if (cmd == GetCommand::Name()) {
    return new GetCommand(cmdParams, option_map, flags);
  } else if (cmd == PutCommand::Name()) {
    return new PutCommand(cmdParams, option_map, flags);
  } else if (cmd == BatchPutCommand::Name()) {
    return new BatchPutCommand(cmdParams, option_map, flags);
  } else if (cmd == ScanCommand::Name()) {
    return new ScanCommand(cmdParams, option_map, flags);
  } else if (cmd == DeleteCommand::Name()) {
    return new DeleteCommand(cmdParams, option_map, flags);
  } else if (cmd == ApproxSizeCommand::Name()) {
    return new ApproxSizeCommand(cmdParams, option_map, flags);
  } else if (cmd == DBQuerierCommand::Name()) {
    return new DBQuerierCommand(cmdParams, option_map, flags);
  } else if (cmd == CompactorCommand::Name()) {
    return new CompactorCommand(cmdParams, option_map, flags);
  } else if (cmd == WALDumperCommand::Name()) {
    return new WALDumperCommand(cmdParams, option_map, flags);
  } else if (cmd == ReduceDBLevelsCommand::Name()) {
    return new ReduceDBLevelsCommand(cmdParams, option_map, flags);
  } else if (cmd == DBDumperCommand::Name()) {
    return new DBDumperCommand(cmdParams, option_map, flags);
  } else if (cmd == DBLoaderCommand::Name()) {
    return new DBLoaderCommand(cmdParams, option_map, flags);
  } else if (cmd == ManifestDumpCommand::Name()) {
    return new ManifestDumpCommand(cmdParams, option_map, flags);
  }
  return nullptr;
}


/**
 * Parses the specific integer option and fills in the value.
 * Returns true if the option is found.
 * Returns false if the option is not found or if there is an error parsing the
 * value.  If there is an error, the specified exec_state is also
 * updated.
 */
bool LDBCommand::ParseIntOption(const map<string, string>& options,
    string option, int& value, LDBCommandExecuteResult& exec_state) {

  map<string, string>::const_iterator itr = option_map_.find(option);
  if (itr != option_map_.end()) {
    try {
      value = stoi(itr->second);
      return true;
    } catch(const invalid_argument&) {
      exec_state = LDBCommandExecuteResult::FAILED(option +
                      " has an invalid value.");
    } catch(const out_of_range&) {
      exec_state = LDBCommandExecuteResult::FAILED(option +
                      " has a value out-of-range.");
    }
  }
  return false;
}

Options LDBCommand::PrepareOptionsForOpenDB() {

  Options opt = options_;
  opt.create_if_missing = false;

  map<string, string>::const_iterator itr;

  int bits;
  if (ParseIntOption(option_map_, ARG_BLOOM_BITS, bits, exec_state_)) {
    if (bits > 0) {
      opt.filter_policy = NewBloomFilterPolicy(bits);
    } else {
      exec_state_ = LDBCommandExecuteResult::FAILED(ARG_BLOOM_BITS +
                      " must be > 0.");
    }
  }

  int block_size;
  if (ParseIntOption(option_map_, ARG_BLOCK_SIZE, block_size, exec_state_)) {
    if (block_size > 0) {
      opt.block_size = block_size;
    } else {
      exec_state_ = LDBCommandExecuteResult::FAILED(ARG_BLOCK_SIZE +
                      " must be > 0.");
    }
  }

  itr = option_map_.find(ARG_AUTO_COMPACTION);
  if (itr != option_map_.end()) {
    opt.disable_auto_compactions = ! StringToBool(itr->second);
  }

  itr = option_map_.find(ARG_COMPRESSION_TYPE);
  if (itr != option_map_.end()) {
    string comp = itr->second;
    if (comp == "no") {
      opt.compression = kNoCompression;
    } else if (comp == "snappy") {
      opt.compression = kSnappyCompression;
    } else if (comp == "zlib") {
      opt.compression = kZlibCompression;
    } else if (comp == "bzip2") {
      opt.compression = kBZip2Compression;
    } else {
      // Unknown compression.
      exec_state_ = LDBCommandExecuteResult::FAILED(
                      "Unknown compression level: " + comp);
    }
  }

  int write_buffer_size;
  if (ParseIntOption(option_map_, ARG_WRITE_BUFFER_SIZE, write_buffer_size,
        exec_state_)) {
    if (write_buffer_size > 0) {
      opt.write_buffer_size = write_buffer_size;
    } else {
      exec_state_ = LDBCommandExecuteResult::FAILED(ARG_WRITE_BUFFER_SIZE +
                      " must be > 0.");
    }
  }

  int file_size;
  if (ParseIntOption(option_map_, ARG_FILE_SIZE, file_size, exec_state_)) {
    if (file_size > 0) {
      opt.target_file_size_base = file_size;
    } else {
      exec_state_ = LDBCommandExecuteResult::FAILED(ARG_FILE_SIZE +
                      " must be > 0.");
    }
  }

  return opt;
}

bool LDBCommand::ParseKeyValue(const string& line, string* key, string* value,
                              bool is_key_hex, bool is_value_hex) {
  size_t pos = line.find(DELIM);
  if (pos != string::npos) {
    *key = line.substr(0, pos);
    *value = line.substr(pos + strlen(DELIM));
    if (is_key_hex) {
      *key = HexToString(*key);
    }
    if (is_value_hex) {
      *value = HexToString(*value);
    }
    return true;
  } else {
    return false;
  }
}

/**
 * Make sure that ONLY the command-line options and flags expected by this
 * command are specified on the command-line.  Extraneous options are usually
 * the result of user error.
 * Returns true if all checks pass.  Else returns false, and prints an
 * appropriate error msg to stderr.
 */
bool LDBCommand::ValidateCmdLineOptions() {

  for (map<string, string>::const_iterator itr = option_map_.begin();
        itr != option_map_.end(); itr++) {
    if (find(valid_cmd_line_options_.begin(),
          valid_cmd_line_options_.end(), itr->first) ==
          valid_cmd_line_options_.end()) {
      fprintf(stderr, "Invalid command-line option %s\n", itr->first.c_str());
      return false;
    }
  }

  for (vector<string>::const_iterator itr = flags_.begin();
        itr != flags_.end(); itr++) {
    if (find(valid_cmd_line_options_.begin(),
          valid_cmd_line_options_.end(), *itr) ==
          valid_cmd_line_options_.end()) {
      fprintf(stderr, "Invalid command-line flag %s\n", itr->c_str());
      return false;
    }
  }

  if (!NoDBOpen() && option_map_.find(ARG_DB) == option_map_.end()) {
    fprintf(stderr, "%s must be specified\n", ARG_DB.c_str());
    return false;
  }

  return true;
}

CompactorCommand::CompactorCommand(const vector<string>& params,
      const map<string, string>& options, const vector<string>& flags) :
    LDBCommand(options, flags, false,
               BuildCmdLineOptions({ARG_FROM, ARG_TO, ARG_HEX, ARG_KEY_HEX,
                                   ARG_VALUE_HEX})),
    null_from_(true), null_to_(true) {

  map<string, string>::const_iterator itr = options.find(ARG_FROM);
  if (itr != options.end()) {
    null_from_ = false;
    from_ = itr->second;
  }

  itr = options.find(ARG_TO);
  if (itr != options.end()) {
    null_to_ = false;
    to_ = itr->second;
  }

  if (is_key_hex_) {
    if (!null_from_) {
      from_ = HexToString(from_);
    }
    if (!null_to_) {
      to_ = HexToString(to_);
    }
  }
}

void CompactorCommand::Help(string& ret) {
  ret.append("  ");
  ret.append(CompactorCommand::Name());
  ret.append(HelpRangeCmdArgs());
  ret.append("\n");
}

void CompactorCommand::DoCommand() {

  Slice* begin = nullptr;
  Slice* end = nullptr;
  if (!null_from_) {
    begin = new Slice(from_);
  }
  if (!null_to_) {
    end = new Slice(to_);
  }

  db_->CompactRange(begin, end);
  exec_state_ = LDBCommandExecuteResult::SUCCEED("");

  delete begin;
  delete end;
}

const string DBLoaderCommand::ARG_DISABLE_WAL = "disable_wal";
const string DBLoaderCommand::ARG_BULK_LOAD = "bulk_load";
const string DBLoaderCommand::ARG_COMPACT = "compact";

DBLoaderCommand::DBLoaderCommand(const vector<string>& params,
      const map<string, string>& options, const vector<string>& flags) :
    LDBCommand(options, flags, false,
               BuildCmdLineOptions({ARG_HEX, ARG_KEY_HEX, ARG_VALUE_HEX,
                                    ARG_FROM, ARG_TO, ARG_CREATE_IF_MISSING,
                                    ARG_DISABLE_WAL, ARG_BULK_LOAD,
                                    ARG_COMPACT})),
    create_if_missing_(false), disable_wal_(false), bulk_load_(false),
    compact_(false) {

  create_if_missing_ = IsFlagPresent(flags, ARG_CREATE_IF_MISSING);
  disable_wal_ = IsFlagPresent(flags, ARG_DISABLE_WAL);
  bulk_load_ = IsFlagPresent(flags, ARG_BULK_LOAD);
  compact_ = IsFlagPresent(flags, ARG_COMPACT);
}

void DBLoaderCommand::Help(string& ret) {
  ret.append("  ");
  ret.append(DBLoaderCommand::Name());
  ret.append(" [--" + ARG_CREATE_IF_MISSING + "]");
  ret.append(" [--" + ARG_DISABLE_WAL + "]");
  ret.append(" [--" + ARG_BULK_LOAD + "]");
  ret.append(" [--" + ARG_COMPACT + "]");
  ret.append("\n");
}

Options DBLoaderCommand::PrepareOptionsForOpenDB() {
  Options opt = LDBCommand::PrepareOptionsForOpenDB();
  opt.create_if_missing = create_if_missing_;
  if (bulk_load_) {
    opt.PrepareForBulkLoad();
  }
  return opt;
}

void DBLoaderCommand::DoCommand() {
  if (!db_) {
    return;
  }

  WriteOptions write_options;
  if (disable_wal_) {
    write_options.disableWAL = true;
  }

  int bad_lines = 0;
  string line;
  while (getline(cin, line, '\n')) {
    string key;
    string value;
    if (ParseKeyValue(line, &key, &value, is_key_hex_, is_value_hex_)) {
      db_->Put(write_options, Slice(key), Slice(value));
    } else if (0 == line.find("Keys in range:")) {
      // ignore this line
    } else if (0 == line.find("Created bg thread 0x")) {
      // ignore this line
    } else {
      bad_lines ++;
    }
  }

  if (bad_lines > 0) {
    cout << "Warning: " << bad_lines << " bad lines ignored." << endl;
  }
  if (compact_) {
    db_->CompactRange(nullptr, nullptr);
  }
}

// ----------------------------------------------------------------------------

const string ManifestDumpCommand::ARG_VERBOSE = "verbose";
const string ManifestDumpCommand::ARG_PATH    = "path";

void ManifestDumpCommand::Help(string& ret) {
  ret.append("  ");
  ret.append(ManifestDumpCommand::Name());
  ret.append(" [--" + ARG_VERBOSE + "]");
  ret.append(" [--" + ARG_PATH + "=<path_to_manifest_file>]");
  ret.append("\n");
}

ManifestDumpCommand::ManifestDumpCommand(const vector<string>& params,
      const map<string, string>& options, const vector<string>& flags) :
    LDBCommand(options, flags, false,
               BuildCmdLineOptions({ARG_VERBOSE,ARG_PATH})),
    verbose_(false),
    path_("")
{
  verbose_ = IsFlagPresent(flags, ARG_VERBOSE);

  map<string, string>::const_iterator itr = options.find(ARG_PATH);
  if (itr != options.end()) {
    path_ = itr->second;
    if (path_.empty()) {
      exec_state_ = LDBCommandExecuteResult::FAILED("--path: missing pathname");
    }
  }
}

void ManifestDumpCommand::DoCommand() {

  std::string manifestfile;

  if (!path_.empty()) {
    manifestfile = path_;
  } else {
    bool found = false;
    // We need to find the manifest file by searching the directory
    // containing the db for files of the form MANIFEST_[0-9]+
    DIR* d = opendir(db_path_.c_str());
    if (d == nullptr) {
      exec_state_ = LDBCommandExecuteResult::FAILED(
        db_path_ + " is not a directory");
      return;
    }
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
      unsigned int match;
      unsigned long long num;
      if (sscanf(entry->d_name, "MANIFEST-%llu%n", &num, &match)
          && match == strlen(entry->d_name)) {
        if (!found) {
          manifestfile = db_path_ + "/" + std::string(entry->d_name);
          found = true;
        } else {
          exec_state_ = LDBCommandExecuteResult::FAILED(
            "Multiple MANIFEST files found; use --path to select one");
          return;
        }
      }
    }
    closedir(d);
  }

  if (verbose_) {
    printf("Processing Manifest file %s\n", manifestfile.c_str());
  }

  Options options;
  StorageOptions sopt;
  std::string file(manifestfile);
  std::string dbname("dummy");
  TableCache* tc = new TableCache(dbname, &options, sopt, 10);
  const InternalKeyComparator* cmp =
    new InternalKeyComparator(options.comparator);

  VersionSet* versions = new VersionSet(dbname, &options, sopt, tc, cmp);
  Status s = versions->DumpManifest(options, file, verbose_, is_key_hex_);
  if (!s.ok()) {
    printf("Error in processing file %s %s\n", manifestfile.c_str(),
           s.ToString().c_str());
  }
  if (verbose_) {
    printf("Processing Manifest file %s done\n", manifestfile.c_str());
  }
}

// ----------------------------------------------------------------------------

const string DBDumperCommand::ARG_COUNT_ONLY = "count_only";
const string DBDumperCommand::ARG_STATS = "stats";

DBDumperCommand::DBDumperCommand(const vector<string>& params,
      const map<string, string>& options, const vector<string>& flags) :
    LDBCommand(options, flags, true,
               BuildCmdLineOptions({ARG_TTL, ARG_HEX, ARG_KEY_HEX,
                                    ARG_VALUE_HEX, ARG_FROM, ARG_TO,
                                    ARG_MAX_KEYS, ARG_COUNT_ONLY, ARG_STATS})),
    null_from_(true),
    null_to_(true),
    max_keys_(-1),
    count_only_(false),
    print_stats_(false) {

  map<string, string>::const_iterator itr = options.find(ARG_FROM);
  if (itr != options.end()) {
    null_from_ = false;
    from_ = itr->second;
  }

  itr = options.find(ARG_TO);
  if (itr != options.end()) {
    null_to_ = false;
    to_ = itr->second;
  }

  itr = options.find(ARG_MAX_KEYS);
  if (itr != options.end()) {
    try {
      max_keys_ = stoi(itr->second);
    } catch(const invalid_argument&) {
      exec_state_ = LDBCommandExecuteResult::FAILED(ARG_MAX_KEYS +
                        " has an invalid value");
    } catch(const out_of_range&) {
      exec_state_ = LDBCommandExecuteResult::FAILED(ARG_MAX_KEYS +
                        " has a value out-of-range");
    }
  }

  print_stats_ = IsFlagPresent(flags, ARG_STATS);
  count_only_ = IsFlagPresent(flags, ARG_COUNT_ONLY);

  if (is_key_hex_) {
    if (!null_from_) {
      from_ = HexToString(from_);
    }
    if (!null_to_) {
      to_ = HexToString(to_);
    }
  }
}

void DBDumperCommand::Help(string& ret) {
  ret.append("  ");
  ret.append(DBDumperCommand::Name());
  ret.append(HelpRangeCmdArgs());
  ret.append(" [--" + ARG_MAX_KEYS + "=<N>]");
  ret.append(" [--" + ARG_COUNT_ONLY + "]");
  ret.append(" [--" + ARG_STATS + "]");
  ret.append("\n");
}

void DBDumperCommand::DoCommand() {
  if (!db_) {
    return;
  }
  // Parse command line args
  uint64_t count = 0;
  if (print_stats_) {
    string stats;
    if (db_->GetProperty("leveldb.stats", &stats)) {
      fprintf(stdout, "%s\n", stats.c_str());
    }
  }

  // Setup key iterator
  Iterator* iter = db_->NewIterator(ReadOptions());
  Status st = iter->status();
  if (!st.ok()) {
    exec_state_ = LDBCommandExecuteResult::FAILED("Iterator error."
        + st.ToString());
  }

  if (!null_from_) {
    iter->Seek(from_);
  } else {
    iter->SeekToFirst();
  }

  int max_keys = max_keys_;
  for (; iter->Valid(); iter->Next()) {
    // If end marker was specified, we stop before it
    if (!null_to_ && (iter->key().ToString() >= to_))
      break;
    // Terminate if maximum number of keys have been dumped
    if (max_keys == 0)
      break;
    if (max_keys > 0) {
      --max_keys;
    }
    ++count;
    if (!count_only_) {
      string str = PrintKeyValue(iter->key().ToString(),
                                 iter->value().ToString(),
                                 is_key_hex_, is_value_hex_);
      fprintf(stdout, "%s\n", str.c_str());
    }
  }
  fprintf(stdout, "Keys in range: %lld\n", (long long) count);
  // Clean up
  delete iter;
}

const string ReduceDBLevelsCommand::ARG_NEW_LEVELS = "new_levels";
const string  ReduceDBLevelsCommand::ARG_PRINT_OLD_LEVELS = "print_old_levels";

ReduceDBLevelsCommand::ReduceDBLevelsCommand(const vector<string>& params,
      const map<string, string>& options, const vector<string>& flags) :
    LDBCommand(options, flags, false,
               BuildCmdLineOptions({ARG_NEW_LEVELS, ARG_PRINT_OLD_LEVELS})),
    old_levels_(1 << 16),
    new_levels_(-1),
    print_old_levels_(false) {


  ParseIntOption(option_map_, ARG_NEW_LEVELS, new_levels_, exec_state_);
  print_old_levels_ = IsFlagPresent(flags, ARG_PRINT_OLD_LEVELS);

  if(new_levels_ <= 0) {
    exec_state_ = LDBCommandExecuteResult::FAILED(
           " Use --" + ARG_NEW_LEVELS + " to specify a new level number\n");
  }
}

vector<string> ReduceDBLevelsCommand::PrepareArgs(const string& db_path,
    int new_levels, bool print_old_level) {
  vector<string> ret;
  ret.push_back("reduce_levels");
  ret.push_back("--" + ARG_DB + "=" + db_path);
  ret.push_back("--" + ARG_NEW_LEVELS + "=" + to_string(new_levels));
  if(print_old_level) {
    ret.push_back("--" + ARG_PRINT_OLD_LEVELS);
  }
  return ret;
}

void ReduceDBLevelsCommand::Help(string& ret) {
  ret.append("  ");
  ret.append(ReduceDBLevelsCommand::Name());
  ret.append(" --" + ARG_NEW_LEVELS + "=<New number of levels>");
  ret.append(" [--" + ARG_PRINT_OLD_LEVELS + "]");
  ret.append("\n");
}

Options ReduceDBLevelsCommand::PrepareOptionsForOpenDB() {
  Options opt = LDBCommand::PrepareOptionsForOpenDB();
  opt.num_levels = old_levels_;
  opt.max_bytes_for_level_multiplier_additional.resize(opt.num_levels, 1);
  // Disable size compaction
  opt.max_bytes_for_level_base = 1UL << 50;
  opt.max_bytes_for_level_multiplier = 1;
  opt.max_mem_compaction_level = 0;
  return opt;
}

Status ReduceDBLevelsCommand::GetOldNumOfLevels(Options& opt,
    int* levels) {
  StorageOptions soptions;
  TableCache tc(db_path_, &opt, soptions, 10);
  const InternalKeyComparator cmp(opt.comparator);
  VersionSet versions(db_path_, &opt, soptions, &tc, &cmp);
  // We rely the VersionSet::Recover to tell us the internal data structures
  // in the db. And the Recover() should never do any change
  // (like LogAndApply) to the manifest file.
  Status st = versions.Recover();
  if (!st.ok()) {
    return st;
  }
  int max = -1;
  for (int i = 0; i < versions.NumberLevels(); i++) {
    if (versions.NumLevelFiles(i)) {
      max = i;
    }
  }

  *levels = max + 1;
  return st;
}

void ReduceDBLevelsCommand::DoCommand() {
  if (new_levels_ <= 1) {
    exec_state_ = LDBCommandExecuteResult::FAILED(
        "Invalid number of levels.\n");
    return;
  }

  Status st;
  Options opt = PrepareOptionsForOpenDB();
  int old_level_num = -1;
  st = GetOldNumOfLevels(opt, &old_level_num);
  if (!st.ok()) {
    exec_state_ = LDBCommandExecuteResult::FAILED(st.ToString());
    return;
  }

  if (print_old_levels_) {
    fprintf(stdout, "The old number of levels in use is %d\n", old_level_num);
  }

  if (old_level_num <= new_levels_) {
    return;
  }

  old_levels_ = old_level_num;

  OpenDB();
  if (!db_) {
    return;
  }
  // Compact the whole DB to put all files to the highest level.
  fprintf(stdout, "Compacting the db...\n");
  db_->CompactRange(nullptr, nullptr);
  CloseDB();

  StorageOptions soptions;
  TableCache tc(db_path_, &opt, soptions, 10);
  const InternalKeyComparator cmp(opt.comparator);
  VersionSet versions(db_path_, &opt, soptions, &tc, &cmp);
  // We rely the VersionSet::Recover to tell us the internal data structures
  // in the db. And the Recover() should never do any change (like LogAndApply)
  // to the manifest file.
  st = versions.Recover();
  if (!st.ok()) {
    exec_state_ = LDBCommandExecuteResult::FAILED(st.ToString());
    return;
  }

  port::Mutex mu;
  mu.Lock();
  st = versions.ReduceNumberOfLevels(new_levels_, &mu);
  mu.Unlock();

  if (!st.ok()) {
    exec_state_ = LDBCommandExecuteResult::FAILED(st.ToString());
    return;
  }
}

class InMemoryHandler : public WriteBatch::Handler {
 public:

  virtual void Put(const Slice& key, const Slice& value) {
    putMap_[key.ToString()] = value.ToString();
  }
  virtual void Delete(const Slice& key) {
    deleteList_.push_back(key.ToString(true));
  }
  virtual ~InMemoryHandler() { };

  map<string, string> PutMap() {
    return putMap_;
  }
  vector<string> DeleteList() {
    return deleteList_;
  }

 private:
  map<string, string> putMap_;
  vector<string> deleteList_;
};

const string WALDumperCommand::ARG_WAL_FILE = "walfile";
const string WALDumperCommand::ARG_PRINT_VALUE = "print_value";
const string WALDumperCommand::ARG_PRINT_HEADER = "header";

WALDumperCommand::WALDumperCommand(const vector<string>& params,
      const map<string, string>& options, const vector<string>& flags) :
    LDBCommand(options, flags, true,
               BuildCmdLineOptions(
                {ARG_WAL_FILE, ARG_PRINT_HEADER, ARG_PRINT_VALUE})),
    print_header_(false), print_values_(false) {

  wal_file_.clear();

  map<string, string>::const_iterator itr = options.find(ARG_WAL_FILE);
  if (itr != options.end()) {
    wal_file_ = itr->second;
  }


  print_header_ = IsFlagPresent(flags, ARG_PRINT_HEADER);
  print_values_ = IsFlagPresent(flags, ARG_PRINT_VALUE);
  if (wal_file_.empty()) {
    exec_state_ = LDBCommandExecuteResult::FAILED(
                    "Argument " + ARG_WAL_FILE + " must be specified.");
  }
}

void WALDumperCommand::Help(string& ret) {
  ret.append("  ");
  ret.append(WALDumperCommand::Name());
  ret.append(" --" + ARG_WAL_FILE + "=<write_ahead_log_file_path>");
  ret.append(" --[" + ARG_PRINT_HEADER + "] ");
  ret.append(" --[ " + ARG_PRINT_VALUE + "] ");
  ret.append("\n");
}

void WALDumperCommand::DoCommand() {
  struct StdErrReporter : public log::Reader::Reporter {
    virtual void Corruption(size_t bytes, const Status& s) {
      cerr<<"Corruption detected in log file "<<s.ToString()<<"\n";
    }
  };

  unique_ptr<SequentialFile> file;
  Env* env_ = Env::Default();
  StorageOptions soptions;
  Status status = env_->NewSequentialFile(wal_file_, &file, soptions);
  if (!status.ok()) {
    exec_state_ = LDBCommandExecuteResult::FAILED("Failed to open WAL file " +
      status.ToString());
  } else {
    StdErrReporter reporter;
    log::Reader reader(move(file), &reporter, true, 0);
    string scratch;
    WriteBatch batch;
    Slice record;
    stringstream row;
    if (print_header_) {
      cout<<"Sequence,Count,ByteSize,Physical Offset,Key(s)";
      if (print_values_) {
        cout << " : value ";
      }
      cout << "\n";
    }
    while(reader.ReadRecord(&record, &scratch)) {
      row.str("");
      if (record.size() < 12) {
        reporter.Corruption(
            record.size(), Status::Corruption("log record too small"));
      } else {
        WriteBatchInternal::SetContents(&batch, record);
        row<<WriteBatchInternal::Sequence(&batch)<<",";
        row<<WriteBatchInternal::Count(&batch)<<",";
        row<<WriteBatchInternal::ByteSize(&batch)<<",";
        row<<reader.LastRecordOffset()<<",";
        InMemoryHandler handler;
        batch.Iterate(&handler);
        row << "PUT : ";
        if (print_values_) {
          for (auto& kv : handler.PutMap()) {
            string k = StringToHex(kv.first);
            string v = StringToHex(kv.second);
            row << k << " : ";
            row << v << " ";
          }
        }
        else {
          for(auto& kv : handler.PutMap()) {
            row << StringToHex(kv.first) << " ";
          }
        }
        row<<",DELETE : ";
        for(string& s : handler.DeleteList()) {
          row << StringToHex(s) << " ";
        }
        row<<"\n";
      }
      cout<<row.str();
    }
  }
}


GetCommand::GetCommand(const vector<string>& params,
      const map<string, string>& options, const vector<string>& flags) :
  LDBCommand(options, flags, true, BuildCmdLineOptions({ARG_TTL, ARG_HEX,
                                                        ARG_KEY_HEX,
                                                        ARG_VALUE_HEX})) {

  if (params.size() != 1) {
    exec_state_ = LDBCommandExecuteResult::FAILED(
                    "<key> must be specified for the get command");
  } else {
    key_ = params.at(0);
  }

  if (is_key_hex_) {
    key_ = HexToString(key_);
  }
}

void GetCommand::Help(string& ret) {
  ret.append("  ");
  ret.append(GetCommand::Name());
  ret.append(" <key>");
  ret.append("\n");
}

void GetCommand::DoCommand() {
  string value;
  Status st = db_->Get(ReadOptions(), key_, &value);
  if (st.ok()) {
    fprintf(stdout, "%s\n",
              (is_value_hex_ ? StringToHex(value) : value).c_str());
  } else {
    exec_state_ = LDBCommandExecuteResult::FAILED(st.ToString());
  }
}


ApproxSizeCommand::ApproxSizeCommand(const vector<string>& params,
      const map<string, string>& options, const vector<string>& flags) :
  LDBCommand(options, flags, true,
             BuildCmdLineOptions({ARG_HEX, ARG_KEY_HEX, ARG_VALUE_HEX,
                                  ARG_FROM, ARG_TO})) {

  if (options.find(ARG_FROM) != options.end()) {
    start_key_ = options.find(ARG_FROM)->second;
  } else {
    exec_state_ = LDBCommandExecuteResult::FAILED(ARG_FROM +
                    " must be specified for approxsize command");
    return;
  }

  if (options.find(ARG_TO) != options.end()) {
    end_key_ = options.find(ARG_TO)->second;
  } else {
    exec_state_ = LDBCommandExecuteResult::FAILED(ARG_TO +
                    " must be specified for approxsize command");
    return;
  }

  if (is_key_hex_) {
    start_key_ = HexToString(start_key_);
    end_key_ = HexToString(end_key_);
  }
}

void ApproxSizeCommand::Help(string& ret) {
  ret.append("  ");
  ret.append(ApproxSizeCommand::Name());
  ret.append(HelpRangeCmdArgs());
  ret.append("\n");
}

void ApproxSizeCommand::DoCommand() {

  Range ranges[1];
  ranges[0] = Range(start_key_, end_key_);
  uint64_t sizes[1];
  db_->GetApproximateSizes(ranges, 1, sizes);
  fprintf(stdout, "%ld\n", sizes[0]);
  /* Wierd that GetApproximateSizes() returns void, although documentation
   * says that it returns a Status object.
  if (!st.ok()) {
    exec_state_ = LDBCommandExecuteResult::FAILED(st.ToString());
  }
  */
}


BatchPutCommand::BatchPutCommand(const vector<string>& params,
      const map<string, string>& options, const vector<string>& flags) :
  LDBCommand(options, flags, false,
             BuildCmdLineOptions({ARG_TTL, ARG_HEX, ARG_KEY_HEX, ARG_VALUE_HEX,
                                  ARG_CREATE_IF_MISSING})) {

  if (params.size() < 2) {
    exec_state_ = LDBCommandExecuteResult::FAILED(
        "At least one <key> <value> pair must be specified batchput.");
  } else if (params.size() % 2 != 0) {
    exec_state_ = LDBCommandExecuteResult::FAILED(
        "Equal number of <key>s and <value>s must be specified for batchput.");
  } else {
    for (size_t i = 0; i < params.size(); i += 2) {
      string key = params.at(i);
      string value = params.at(i+1);
      key_values_.push_back(pair<string, string>(
                    is_key_hex_ ? HexToString(key) : key,
                    is_value_hex_ ? HexToString(value) : value));
    }
  }
}

void BatchPutCommand::Help(string& ret) {
  ret.append("  ");
  ret.append(BatchPutCommand::Name());
  ret.append(" <key> <value> [<key> <value>] [..]");
  ret.append("\n");
}

void BatchPutCommand::DoCommand() {
  WriteBatch batch;

  for (vector<pair<string, string>>::const_iterator itr
        = key_values_.begin(); itr != key_values_.end(); itr++) {
      batch.Put(itr->first, itr->second);
  }
  Status st = db_->Write(WriteOptions(), &batch);
  if (st.ok()) {
    fprintf(stdout, "OK\n");
  } else {
    exec_state_ = LDBCommandExecuteResult::FAILED(st.ToString());
  }
}

Options BatchPutCommand::PrepareOptionsForOpenDB() {
  Options opt = LDBCommand::PrepareOptionsForOpenDB();
  opt.create_if_missing = IsFlagPresent(flags_, ARG_CREATE_IF_MISSING);
  return opt;
}


ScanCommand::ScanCommand(const vector<string>& params,
      const map<string, string>& options, const vector<string>& flags) :
    LDBCommand(options, flags, true,
               BuildCmdLineOptions({ARG_TTL, ARG_HEX, ARG_KEY_HEX,
                                    ARG_VALUE_HEX, ARG_FROM, ARG_TO,
                                    ARG_MAX_KEYS})),
    start_key_specified_(false),
    end_key_specified_(false),
    max_keys_scanned_(-1) {

  map<string, string>::const_iterator itr = options.find(ARG_FROM);
  if (itr != options.end()) {
    start_key_ = itr->second;
    if (is_key_hex_) {
      start_key_ = HexToString(start_key_);
    }
    start_key_specified_ = true;
  }
  itr = options.find(ARG_TO);
  if (itr != options.end()) {
    end_key_ = itr->second;
    if (is_key_hex_) {
      end_key_ = HexToString(end_key_);
    }
    end_key_specified_ = true;
  }

  itr = options.find(ARG_MAX_KEYS);
  if (itr != options.end()) {
    try {
      max_keys_scanned_ = stoi(itr->second);
    } catch(const invalid_argument&) {
      exec_state_ = LDBCommandExecuteResult::FAILED(ARG_MAX_KEYS +
                        " has an invalid value");
    } catch(const out_of_range&) {
      exec_state_ = LDBCommandExecuteResult::FAILED(ARG_MAX_KEYS +
                        " has a value out-of-range");
    }
  }
}

void ScanCommand::Help(string& ret) {
  ret.append("  ");
  ret.append(ScanCommand::Name());
  ret.append(HelpRangeCmdArgs());
  ret.append("--" + ARG_MAX_KEYS + "=N] ");
  ret.append("\n");
}

void ScanCommand::DoCommand() {

  int num_keys_scanned = 0;
  Iterator* it = db_->NewIterator(ReadOptions());
  if (start_key_specified_) {
    it->Seek(start_key_);
  } else {
    it->SeekToFirst();
  }
  for ( ;
        it->Valid() && (!end_key_specified_ || it->key().ToString() < end_key_);
        it->Next()) {
    string key = it->key().ToString();
    string value = it->value().ToString();
    fprintf(stdout, "%s : %s\n",
          (is_key_hex_ ? StringToHex(key) : key).c_str(),
          (is_value_hex_ ? StringToHex(value) : value).c_str()
        );
    num_keys_scanned++;
    if (max_keys_scanned_ >= 0 && num_keys_scanned >= max_keys_scanned_) {
      break;
    }
  }
  if (!it->status().ok()) {  // Check for any errors found during the scan
    exec_state_ = LDBCommandExecuteResult::FAILED(it->status().ToString());
  }
  delete it;
}


DeleteCommand::DeleteCommand(const vector<string>& params,
      const map<string, string>& options, const vector<string>& flags) :
  LDBCommand(options, flags, false,
             BuildCmdLineOptions({ARG_HEX, ARG_KEY_HEX, ARG_VALUE_HEX})) {

  if (params.size() != 1) {
    exec_state_ = LDBCommandExecuteResult::FAILED(
                    "KEY must be specified for the delete command");
  } else {
    key_ = params.at(0);
    if (is_key_hex_) {
      key_ = HexToString(key_);
    }
  }
}

void DeleteCommand::Help(string& ret) {
  ret.append("  ");
  ret.append(DeleteCommand::Name() + " <key>");
  ret.append("\n");
}

void DeleteCommand::DoCommand() {
  Status st = db_->Delete(WriteOptions(), key_);
  if (st.ok()) {
    fprintf(stdout, "OK\n");
  } else {
    exec_state_ = LDBCommandExecuteResult::FAILED(st.ToString());
  }
}


PutCommand::PutCommand(const vector<string>& params,
      const map<string, string>& options, const vector<string>& flags) :
  LDBCommand(options, flags, false,
             BuildCmdLineOptions({ARG_TTL, ARG_HEX, ARG_KEY_HEX, ARG_VALUE_HEX,
                                  ARG_CREATE_IF_MISSING})) {

  if (params.size() != 2) {
    exec_state_ = LDBCommandExecuteResult::FAILED(
                    "<key> and <value> must be specified for the put command");
  } else {
    key_ = params.at(0);
    value_ = params.at(1);
  }

  if (is_key_hex_) {
    key_ = HexToString(key_);
  }

  if (is_value_hex_) {
    value_ = HexToString(value_);
  }
}

void PutCommand::Help(string& ret) {
  ret.append("  ");
  ret.append(PutCommand::Name());
  ret.append(" <key> <value> ");
  ret.append("\n");
}

void PutCommand::DoCommand() {
  Status st = db_->Put(WriteOptions(), key_, value_);
  if (st.ok()) {
    fprintf(stdout, "OK\n");
  } else {
    exec_state_ = LDBCommandExecuteResult::FAILED(st.ToString());
  }
}

Options PutCommand::PrepareOptionsForOpenDB() {
  Options opt = LDBCommand::PrepareOptionsForOpenDB();
  opt.create_if_missing = IsFlagPresent(flags_, ARG_CREATE_IF_MISSING);
  return opt;
}


const char* DBQuerierCommand::HELP_CMD = "help";
const char* DBQuerierCommand::GET_CMD = "get";
const char* DBQuerierCommand::PUT_CMD = "put";
const char* DBQuerierCommand::DELETE_CMD = "delete";

DBQuerierCommand::DBQuerierCommand(const vector<string>& params,
    const map<string, string>& options, const vector<string>& flags) :
  LDBCommand(options, flags, false,
             BuildCmdLineOptions({ARG_TTL, ARG_HEX, ARG_KEY_HEX,
                                  ARG_VALUE_HEX})) {

}

void DBQuerierCommand::Help(string& ret) {
  ret.append("  ");
  ret.append(DBQuerierCommand::Name());
  ret.append("\n");
  ret.append("    Starts a REPL shell.  Type help for list of available "
             "commands.");
  ret.append("\n");
}

void DBQuerierCommand::DoCommand() {
  if (!db_) {
    return;
  }

  ReadOptions read_options;
  WriteOptions write_options;

  string line;
  string key;
  string value;
  while (getline(cin, line, '\n')) {

    // Parse line into vector<string>
    vector<string> tokens;
    size_t pos = 0;
    while (true) {
      size_t pos2 = line.find(' ', pos);
      if (pos2 == string::npos) {
        break;
      }
      tokens.push_back(line.substr(pos, pos2-pos));
      pos = pos2 + 1;
    }
    tokens.push_back(line.substr(pos));

    const string& cmd = tokens[0];

    if (cmd == HELP_CMD) {
      fprintf(stdout,
              "get <key>\n"
              "put <key> <value>\n"
              "delete <key>\n");
    } else if (cmd == DELETE_CMD && tokens.size() == 2) {
      key = (is_key_hex_ ? HexToString(tokens[1]) : tokens[1]);
      db_->Delete(write_options, Slice(key));
      fprintf(stdout, "Successfully deleted %s\n", tokens[1].c_str());
    } else if (cmd == PUT_CMD && tokens.size() == 3) {
      key = (is_key_hex_ ? HexToString(tokens[1]) : tokens[1]);
      value = (is_value_hex_ ? HexToString(tokens[2]) : tokens[2]);
      db_->Put(write_options, Slice(key), Slice(value));
      fprintf(stdout, "Successfully put %s %s\n",
              tokens[1].c_str(), tokens[2].c_str());
    } else if (cmd == GET_CMD && tokens.size() == 2) {
      key = (is_key_hex_ ? HexToString(tokens[1]) : tokens[1]);
      if (db_->Get(read_options, Slice(key), &value).ok()) {
        fprintf(stdout, "%s\n", PrintKeyValue(key, value,
              is_key_hex_, is_value_hex_).c_str());
      } else {
        fprintf(stdout, "Not found %s\n", tokens[1].c_str());
      }
    } else {
      fprintf(stdout, "Unknown command %s\n", line.c_str());
    }
  }
}


}