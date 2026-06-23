#include <dftracer/core/buffer/buffer.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/service/telemetry/omnistat_collector.h>

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace dftracer;

namespace {

const std::string kSharedCsvPath = "/tmp/omnistat_runtime.csv";
const std::string kSharedConfPath = "/tmp/omnistat_runtime.yaml";

std::string write_file(const std::string& path, const std::string& contents) {
  std::ofstream out(path);
  out << contents;
  out.close();
  return path;
}

std::string read_text_file(const std::string& path) {
  std::ifstream input(path);
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void write_shared_config(
    bool include_all_counters = false,
    const std::string& counters_yaml =
        "      - rocm_gpu_utilization\n      - SQ_WAVES\n") {
  std::ofstream yaml_file(kSharedConfPath);
  yaml_file << "enable: true\n";
  yaml_file << "features:\n";
  yaml_file << "  omnistat:\n";
  yaml_file << "    enable: true\n";
  yaml_file << "    input: " << kSharedCsvPath << "\n";
  if (!include_all_counters) {
    yaml_file << "    counters:\n";
    yaml_file << counters_yaml;
  }
  yaml_file << "    include_all_counters: "
            << (include_all_counters ? "true" : "false") << "\n";
  yaml_file << "    attach_to_trace: true\n";
}

void prepare_common_env(const std::string& conf_path,
                        const std::string& log_prefix) {
  setenv("DFTRACER_CONFIGURATION", conf_path.c_str(), 1);
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_LOG_FILE", log_prefix.c_str(), 1);
  setenv("DFTRACER_TRACE_COMPRESSION", "0", 1);
  setenv("DFTRACER_INC_METADATA", "1", 1);
}

void cleanup_common_env() {
  unsetenv("DFTRACER_CONFIGURATION");
  unsetenv("DFTRACER_ENABLE");
  unsetenv("DFTRACER_LOG_FILE");
  unsetenv("DFTRACER_TRACE_COMPRESSION");
  unsetenv("DFTRACER_INC_METADATA");
}

void test_valid_csv_subset() {
  std::cout << "Testing Omnistat subset ingestion..." << std::endl;
  const std::string log_path = "/tmp/omnistat_subset_trace.pfw";

  write_file(kSharedCsvPath,
             "timestamp,hostname,gpu_id,rocm_gpu_utilization,rocm_average_"
             "socket_power_watts,SQ_WAVES\n"
             "1710000000.000,node001,0,85.5,420.1,98765\n"
             "1710000000.100,node001,0,88.0,430.2,99000\n"
             "1710000000.200,node001,1,79.0,390.0,97000\n");
  write_shared_config(false);

  prepare_common_env(kSharedConfPath, log_path);
  auto buffer = std::make_shared<BufferManager>();
  auto hostname_hash = strdup("hosthash");
  buffer->initialize(log_path.c_str(), hostname_hash);
  std::atomic<int> index{0};
  OmnistatTelemetryCollector collector;
  collector.initialize();
  collector.capture(buffer, nullptr, index, 0);
  buffer->finalize(index.load(), 0, true);

  const std::string output = read_text_file(log_path);
  assert(output.find("rocm_gpu_utilization") != std::string::npos);
  assert(output.find("SQ_WAVES") != std::string::npos);
  assert(output.find("rocm_average_socket_power_watts") == std::string::npos);
  assert(output.find("\"cat\":\"omnistat\"") != std::string::npos);

  collector.finalize();
  cleanup_common_env();
  std::filesystem::remove(log_path);
  free(hostname_hash);
  std::cout << "✓ Omnistat subset ingestion passed" << std::endl;
}

void test_include_all_counters() {
  std::cout << "Testing Omnistat include-all mode..." << std::endl;
  const std::string log_path = "/tmp/omnistat_all_trace.pfw";
  write_file(kSharedCsvPath,
             "timestamp,hostname,gpu_id,rocm_gpu_utilization,rocm_memory_usage,"
             "GRBM_COUNT,ignored_text\n"
             "1710000000.000,node001,0,85.5,65536,12345,hello\n"
             "1710000000.100,node001,0,88.0,66000,12400,world\n"
             "1710000000.200,node001,1,79.0,60000,12000,skip\n");
  write_shared_config(true);

  setenv("DFTRACER_CONFIGURATION", kSharedConfPath.c_str(), 1);
  setenv("DFTRACER_LOG_FILE", log_path.c_str(), 1);
  setenv("DFTRACER_TRACE_COMPRESSION", "0", 1);
  setenv("DFTRACER_INC_METADATA", "1", 1);
  auto buffer = std::make_shared<BufferManager>();
  auto hostname_hash = strdup("hosthash");
  buffer->initialize(log_path.c_str(), hostname_hash);
  std::atomic<int> index{0};
  OmnistatTelemetryCollector collector;
  collector.initialize();
  collector.capture(buffer, nullptr, index, 0);
  buffer->finalize(index.load(), 0, true);

  const std::string output = read_text_file(log_path);
  assert(output.find("rocm_gpu_utilization") != std::string::npos);
  assert(output.find("rocm_memory_usage") != std::string::npos);
  assert(output.find("GRBM_COUNT") != std::string::npos);
  assert(output.find("ignored_text") == std::string::npos);

  collector.finalize();

  unsetenv("DFTRACER_CONFIGURATION");
  unsetenv("DFTRACER_LOG_FILE");
  unsetenv("DFTRACER_TRACE_COMPRESSION");
  unsetenv("DFTRACER_INC_METADATA");
  std::filesystem::remove(log_path);
  free(hostname_hash);
  std::cout << "✓ Omnistat include-all mode passed" << std::endl;
}

void test_missing_file_error() {
  std::cout << "Testing Omnistat missing file handling..." << std::endl;
  const std::string conf_path = "/tmp/omnistat_missing.yaml";
  write_file(conf_path,
             "enable: true\n"
             "features:\n"
             "  omnistat:\n"
             "    enable: true\n"
             "    input: /tmp/does_not_exist_omnistat.csv\n"
             "    attach_to_trace: true\n");

  prepare_common_env(conf_path, "/tmp/omnistat_missing_trace.pfw");
  bool threw = false;
  try {
    OmnistatTelemetryCollector collector;
    collector.initialize();
  } catch (const std::runtime_error&) {
    threw = true;
  }
  assert(threw);

  cleanup_common_env();
  std::filesystem::remove(conf_path);
  std::cout << "✓ Omnistat missing file handling passed" << std::endl;
}

void test_missing_timestamp_error() {
  std::cout << "Testing Omnistat missing timestamp column handling..."
            << std::endl;
  const std::string csv_path = "/tmp/omnistat_no_timestamp.csv";
  const std::string conf_path = "/tmp/omnistat_no_timestamp.yaml";
  write_file(csv_path,
             "hostname,gpu_id,rocm_gpu_utilization\n"
             "node001,0,85.5\n");
  write_file(conf_path,
             "enable: true\n"
             "features:\n"
             "  omnistat:\n"
             "    enable: true\n"
             "    input: " +
                 csv_path +
                 "\n"
                 "    attach_to_trace: true\n");

  prepare_common_env(conf_path, "/tmp/omnistat_no_timestamp_trace.pfw");
  bool threw = false;
  try {
    OmnistatTelemetryCollector collector;
    collector.initialize();
  } catch (const std::runtime_error&) {
    threw = true;
  }
  assert(threw);

  cleanup_common_env();
  std::filesystem::remove(csv_path);
  std::filesystem::remove(conf_path);
  std::cout << "✓ Omnistat missing timestamp column handling passed"
            << std::endl;
}

void test_empty_csv_error() {
  std::cout << "Testing Omnistat empty CSV handling..." << std::endl;
  const std::string csv_path = "/tmp/omnistat_empty.csv";
  const std::string conf_path = "/tmp/omnistat_empty.yaml";
  write_file(csv_path, "");
  write_file(conf_path,
             "enable: true\n"
             "features:\n"
             "  omnistat:\n"
             "    enable: true\n"
             "    input: " +
                 csv_path +
                 "\n"
                 "    attach_to_trace: true\n");

  prepare_common_env(conf_path, "/tmp/omnistat_empty_trace.pfw");
  bool threw = false;
  try {
    OmnistatTelemetryCollector collector;
    collector.initialize();
  } catch (const std::runtime_error&) {
    threw = true;
  }
  assert(threw);

  cleanup_common_env();
  std::filesystem::remove(csv_path);
  std::filesystem::remove(conf_path);
  std::cout << "✓ Omnistat empty CSV handling passed" << std::endl;
}

void test_malformed_numeric_value_handling() {
  std::cout << "Testing Omnistat malformed numeric handling..." << std::endl;
  const std::string log_path = "/tmp/omnistat_bad_value_trace.pfw";

  write_file(kSharedCsvPath,
             "timestamp,hostname,gpu_id,rocm_gpu_utilization,SQ_WAVES\n"
             "1710000000.000,node001,0,bad,98765\n"
             "1710000000.100,node001,0,bad,99000\n"
             "1710000000.200,node001,1,bad,97000\n");
  write_shared_config(true);

  prepare_common_env(kSharedConfPath, log_path);
  auto buffer = std::make_shared<BufferManager>();
  auto hostname_hash = strdup("hosthash");
  buffer->initialize(log_path.c_str(), hostname_hash);
  std::atomic<int> index{0};
  OmnistatTelemetryCollector collector;
  collector.initialize();
  collector.capture(buffer, nullptr, index, 0);
  buffer->finalize(index.load(), 0, true);

  const std::string output = read_text_file(log_path);
  assert(output.find("SQ_WAVES") != std::string::npos);
  assert(output.find("rocm_gpu_utilization") == std::string::npos);

  collector.finalize();
  cleanup_common_env();
  std::filesystem::remove(log_path);
  free(hostname_hash);
  std::cout << "✓ Omnistat malformed numeric handling passed" << std::endl;
}

void test_timestamp_normalization() {
  std::cout << "Testing Omnistat timestamp normalization..." << std::endl;
  assert(OmnistatTelemetryCollector::normalize_timestamp("1710000000.0") ==
         1710000000000000ULL);
  assert(OmnistatTelemetryCollector::normalize_timestamp("1710000000000") ==
         1710000000000000ULL);
  assert(OmnistatTelemetryCollector::normalize_timestamp("1710000000000000") ==
         1710000000000000ULL);
  std::cout << "✓ Omnistat timestamp normalization passed" << std::endl;
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    std::filesystem::remove(kSharedCsvPath);
    std::filesystem::remove(kSharedConfPath);
    if (argc == 1) {
      test_valid_csv_subset();
      test_timestamp_normalization();
    } else {
      const std::string mode = argv[1];
      if (mode == "subset") {
        test_valid_csv_subset();
      } else if (mode == "include-all") {
        test_include_all_counters();
      } else if (mode == "missing-file") {
        test_missing_file_error();
      } else if (mode == "missing-timestamp") {
        test_missing_timestamp_error();
      } else if (mode == "empty") {
        test_empty_csv_error();
      } else if (mode == "malformed") {
        test_malformed_numeric_value_handling();
      } else if (mode == "timestamp") {
        test_timestamp_normalization();
      } else {
        std::cerr << "Unknown test mode: " << mode << std::endl;
        return 2;
      }
    }
    std::filesystem::remove(kSharedCsvPath);
    std::filesystem::remove(kSharedConfPath);
    std::cout << "All Omnistat tests passed" << std::endl;
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Omnistat test failed: " << ex.what() << std::endl;
    return 1;
  }
}
