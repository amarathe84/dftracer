//
// Created by haridev on 10/27/23.
//

#ifndef DFTRACER_CONFIGURATION_MANAGER_H
#define DFTRACER_CONFIGURATION_MANAGER_H
#include <cpp-logger/logger.h>
#include <dftracer/core/common/enumeration.h>

#include <string>
#include <vector>
namespace dftracer {
class ConfigurationManager {
 private:
  void derive_configurations();
  std::string aggregation_file;

 public:
  bool enable;
  ProfileInitType init_type;
  std::string log_file;
  std::string data_dirs;
  bool metadata;
  bool core_affinity;
  int gotcha_priority;
  cpplogger::LoggerType logger_level;
  bool io;
  bool posix;
  bool stdio;
  bool compression;
  bool trace_all_files;
  bool tids;
  bool bind_signals;
  bool throw_error;
  size_t write_buffer_size;
  size_t trace_interval_ms;
  size_t libuv_thread_count;
  bool aggregation_enable;
  AggregationType aggregation_type;
  std::vector<std::string> aggregation_inclusion_rules;
  std::vector<std::string> aggregation_exclusion_rules;

  bool omnistat_enable;
  std::string omnistat_input_file;
  std::string omnistat_format;
  std::string omnistat_timestamp_column;
  std::string omnistat_timestamp_format;
  std::vector<std::string> omnistat_counters;
  bool omnistat_include_all_counters;
  bool omnistat_attach_to_trace;
  bool omnistat_export_raw;
  std::string omnistat_time_sync_mode;

  ConfigurationManager();
  void finalize() {}
};
}  // namespace dftracer
#endif  // DFTRACER_CONFIGURATION_MANAGER_H
