#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/utils/configuration_manager.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace dftracer;

namespace {

void clear_papi_environment() {
  unsetenv("DFTRACER_ENABLE");
  unsetenv("DFTRACER_CONFIGURATION");
  unsetenv("DFTRACER_TRACE_INTERVAL_MS");
  unsetenv("DFTRACER_ENABLE_PAPI_TRACING");
  unsetenv("DFTRACER_PAPI_MULTIPLEX");
  unsetenv("DFTRACER_PAPI_SAMPLE_INTERVAL_MS");
  unsetenv("DFTRACER_PAPI_EVENTS");
}

}  // namespace

void test_default_configuration() {
  std::cout << "Testing default configuration..." << std::endl;

  // Clear all DFTRACER environment variables to test defaults
  unsetenv("DFTRACER_ENABLE");
  unsetenv("DFTRACER_ENABLE_AGGREGATION");
  unsetenv("DFTRACER_LOG_LEVEL");
  unsetenv("DFTRACER_AGGREGATION_TYPE");
  unsetenv("DFTRACER_TRACE_COMPRESSION");
  unsetenv("DFTRACER_INC_METADATA");

  // Create a fresh configuration instance
  auto config = std::make_shared<ConfigurationManager>();

  // Check defaults
  assert(config->enable ==
         false);  // Defaults to false, only true if DFTRACER_ENABLE=1
  assert(config->aggregation_enable == false);
  assert(config->compression == true);  // Constructor default is true
  assert(config->metadata == false);
  assert(config->libuv_thread_count == 1);

  std::cout << "✓ Default configuration tests passed" << std::endl;
}

void test_environment_variables() {
  std::cout << "Testing environment variable configuration..." << std::endl;

  // Test DFTRACER_ENABLE
  setenv("DFTRACER_ENABLE", "0", 1);
  auto config1 = std::make_shared<ConfigurationManager>();
  assert(config1->enable == false);
  unsetenv("DFTRACER_ENABLE");

  // Test DFTRACER_ENABLE_AGGREGATION (requires DFTRACER_ENABLE=1)
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_ENABLE_AGGREGATION", "1", 1);
  auto config2 = std::make_shared<ConfigurationManager>();
  assert(config2->enable == true);
  assert(config2->aggregation_enable == true);
  assert(config2->aggregation_type == AggregationType::AGGREGATION_TYPE_FULL);
  unsetenv("DFTRACER_ENABLE_AGGREGATION");
  unsetenv("DFTRACER_ENABLE");

  // Test DFTRACER_AGGREGATION_TYPE (requires DFTRACER_ENABLE=1 and
  // DFTRACER_ENABLE_AGGREGATION=1)
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_ENABLE_AGGREGATION", "1", 1);
  setenv("DFTRACER_AGGREGATION_TYPE", "SELECTIVE", 1);
  auto config3 = std::make_shared<ConfigurationManager>();
  assert(config3->aggregation_enable == true);
  assert(config3->aggregation_type ==
         AggregationType::AGGREGATION_TYPE_SELECTIVE);
  unsetenv("DFTRACER_ENABLE_AGGREGATION");
  unsetenv("DFTRACER_AGGREGATION_TYPE");
  unsetenv("DFTRACER_ENABLE");

  // Test compression (requires DFTRACER_ENABLE=1)
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_TRACE_COMPRESSION", "0", 1);
  auto config4 = std::make_shared<ConfigurationManager>();
  assert(config4->compression == false);
  unsetenv("DFTRACER_TRACE_COMPRESSION");
  unsetenv("DFTRACER_ENABLE");

  // Test metadata (requires DFTRACER_ENABLE=1)
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_INC_METADATA", "1", 1);
  auto config5 = std::make_shared<ConfigurationManager>();
  assert(config5->metadata == true);
  unsetenv("DFTRACER_INC_METADATA");
  unsetenv("DFTRACER_ENABLE");

  // Test trace interval (requires DFTRACER_ENABLE=1)
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_TRACE_INTERVAL_MS", "2000", 1);
  auto config6 = std::make_shared<ConfigurationManager>();
  assert(config6->trace_interval_ms == 2000);
  unsetenv("DFTRACER_TRACE_INTERVAL_MS");
  unsetenv("DFTRACER_ENABLE");

  // Test libuv thread count (requires DFTRACER_ENABLE=1)
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_LIBUV_THREADS", "8", 1);
  auto config7 = std::make_shared<ConfigurationManager>();
  assert(config7->libuv_thread_count == 8);
  unsetenv("DFTRACER_LIBUV_THREADS");
  unsetenv("DFTRACER_ENABLE");

  std::cout << "✓ Environment variable configuration tests passed" << std::endl;
}

void test_aggregation_rules_from_file() {
  std::cout << "Testing aggregation rules from file..." << std::endl;

  // Create a temporary YAML file with rules
  // Note: Keys are "inclusion" and "exclusion" at the root level
  std::string yaml_path = "/tmp/test_rules.yaml";
  std::ofstream yaml_file(yaml_path);
  yaml_file << "inclusion:\n";
  yaml_file << "  - \"cat == 'posix'\"\n";
  yaml_file << "  - \"name LIKE 'read%'\"\n";
  yaml_file << "exclusion:\n";
  yaml_file << "  - \"name == 'stat'\"\n";
  yaml_file.close();

  // Set environment variables (requires DFTRACER_ENABLE=1)
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_ENABLE_AGGREGATION", "1", 1);
  setenv("DFTRACER_AGGREGATION_TYPE", "SELECTIVE", 1);
  setenv("DFTRACER_AGGREGATION_FILE", yaml_path.c_str(), 1);

  auto config = std::make_shared<ConfigurationManager>();

  // Check that rules were loaded
  assert(config->aggregation_inclusion_rules.size() == 2);
  assert(config->aggregation_exclusion_rules.size() == 1);
  assert(config->aggregation_inclusion_rules[0] == "cat == 'posix'");
  assert(config->aggregation_inclusion_rules[1] == "name LIKE 'read%'");
  assert(config->aggregation_exclusion_rules[0] == "name == 'stat'");

  // Cleanup
  std::filesystem::remove(yaml_path);
  unsetenv("DFTRACER_ENABLE");
  unsetenv("DFTRACER_ENABLE_AGGREGATION");
  unsetenv("DFTRACER_AGGREGATION_TYPE");
  unsetenv("DFTRACER_AGGREGATION_FILE");

  std::cout << "✓ Aggregation rules from file tests passed" << std::endl;
}

void test_log_file_configuration() {
  std::cout << "Testing log file configuration..." << std::endl;

  const char* test_log_file = "/tmp/test_trace.pfw";
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_LOG_FILE", test_log_file, 1);

  auto config = std::make_shared<ConfigurationManager>();
  assert(config->log_file == test_log_file);

  unsetenv("DFTRACER_ENABLE");
  unsetenv("DFTRACER_LOG_FILE");

  std::cout << "✓ Log file configuration tests passed" << std::endl;
}

void test_data_dirs_configuration() {
  std::cout << "Testing data dirs configuration..." << std::endl;

  const char* test_dirs = "/data1:/data2:/data3";
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_DATA_DIR", test_dirs, 1);

  auto config = std::make_shared<ConfigurationManager>();
  assert(config->data_dirs == test_dirs);

  unsetenv("DFTRACER_ENABLE");
  unsetenv("DFTRACER_DATA_DIR");

  std::cout << "✓ Data dirs configuration tests passed" << std::endl;
}

void test_io_flags() {
  std::cout << "Testing I/O flags configuration..." << std::endl;

  // Test POSIX disabled (default is true)
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_DISABLE_POSIX", "1", 1);
  auto config1 = std::make_shared<ConfigurationManager>();
  assert(config1->posix == false);
  unsetenv("DFTRACER_DISABLE_POSIX");
  unsetenv("DFTRACER_ENABLE");

  // Test STDIO disabled (default is true)
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_DISABLE_STDIO", "1", 1);
  auto config2 = std::make_shared<ConfigurationManager>();
  assert(config2->stdio == false);
  unsetenv("DFTRACER_DISABLE_STDIO");
  unsetenv("DFTRACER_ENABLE");

  // Test generic IO disabled (default is true)
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_DISABLE_IO", "1", 1);
  auto config3 = std::make_shared<ConfigurationManager>();
  assert(config3->io == false);
  unsetenv("DFTRACER_DISABLE_IO");
  unsetenv("DFTRACER_ENABLE");

  std::cout << "✓ I/O flags configuration tests passed" << std::endl;
}

void test_buffer_size_configuration() {
  std::cout << "Testing buffer size configuration..." << std::endl;

  const size_t test_size = 1024 * 1024;  // 1MB
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_WRITE_BUFFER_SIZE", "1048576", 1);

  auto config = std::make_shared<ConfigurationManager>();
  assert(config->write_buffer_size == test_size);

  unsetenv("DFTRACER_ENABLE");
  unsetenv("DFTRACER_WRITE_BUFFER_SIZE");

  std::cout << "✓ Buffer size configuration tests passed" << std::endl;
}

void test_logger_level() {
  std::cout << "Testing logger level configuration..." << std::endl;

  // Test DEBUG level
  setenv("DFTRACER_LOG_LEVEL", "DEBUG", 1);
  auto config1 = std::make_shared<ConfigurationManager>();
  assert(config1->logger_level == cpplogger::LoggerType::CPP_LOGGER_DEBUG);
  unsetenv("DFTRACER_LOG_LEVEL");

  // Test INFO level
  setenv("DFTRACER_LOG_LEVEL", "INFO", 1);
  auto config2 = std::make_shared<ConfigurationManager>();
  assert(config2->logger_level == cpplogger::LoggerType::CPP_LOGGER_INFO);
  unsetenv("DFTRACER_LOG_LEVEL");

  // Test ERROR level
  setenv("DFTRACER_LOG_LEVEL", "ERROR", 1);
  auto config3 = std::make_shared<ConfigurationManager>();
  assert(config3->logger_level == cpplogger::LoggerType::CPP_LOGGER_ERROR);
  unsetenv("DFTRACER_LOG_LEVEL");

  std::cout << "✓ Logger level configuration tests passed" << std::endl;
}

void test_papi_environment_configuration() {
  std::cout << "Testing PAPI environment configuration..." << std::endl;

  clear_papi_environment();
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_ENABLE_PAPI_TRACING", "1", 1);
  setenv("DFTRACER_PAPI_MULTIPLEX", "1", 1);
  setenv("DFTRACER_TRACE_INTERVAL_MS", "2500", 1);
  setenv("DFTRACER_PAPI_EVENTS", "PAPI_TOT_CYC,PAPI_TOT_INS", 1);

  auto inherited_interval = std::make_shared<ConfigurationManager>();
  assert(inherited_interval->papi_tracing == true);
  assert(inherited_interval->papi_multiplex == true);
  assert(inherited_interval->papi_sample_interval_ms == 2500);
  assert(inherited_interval->papi_events.size() == 2);
  assert(inherited_interval->papi_events[0] == "PAPI_TOT_CYC");
  assert(inherited_interval->papi_events[1] == "PAPI_TOT_INS");

  setenv("DFTRACER_PAPI_SAMPLE_INTERVAL_MS", "125", 1);
  auto explicit_interval = std::make_shared<ConfigurationManager>();
  assert(explicit_interval->papi_sample_interval_ms == 125);

  clear_papi_environment();

  std::cout << "✓ PAPI environment configuration tests passed" << std::endl;
}

void test_papi_yaml_configuration() {
  std::cout << "Testing PAPI YAML configuration..." << std::endl;

  clear_papi_environment();

  std::string yaml_path = "/tmp/test_papi_configuration.yaml";
  std::ofstream yaml_file(yaml_path);
  yaml_file << "enable: true\n";
  yaml_file << "features:\n";
  yaml_file << "  interval: 2000\n";
  yaml_file << "  papi:\n";
  yaml_file << "    enable: true\n";
  yaml_file << "    multiplex: true\n";
  yaml_file << "    interval: 333\n";
  yaml_file << "    events:\n";
  yaml_file << "      - PAPI_TOT_CYC\n";
  yaml_file << "      - PAPI_L3_TCM\n";
  yaml_file.close();

  setenv("DFTRACER_CONFIGURATION", yaml_path.c_str(), 1);
  auto config = std::make_shared<ConfigurationManager>();

  assert(config->enable == true);
  assert(config->trace_interval_ms == 2000);
  assert(config->papi_tracing == true);
  assert(config->papi_multiplex == true);
  assert(config->papi_sample_interval_ms == 333);
  assert(config->papi_events.size() == 2);
  assert(config->papi_events[0] == "PAPI_TOT_CYC");
  assert(config->papi_events[1] == "PAPI_L3_TCM");

  std::filesystem::remove(yaml_path);
  clear_papi_environment();

  std::cout << "✓ PAPI YAML configuration tests passed" << std::endl;
}

int main(int argc, char* argv[]) {
  std::cout << "=== Running Configuration Manager Unit Tests ===" << std::endl;

  try {
    test_default_configuration();
    test_environment_variables();
    test_aggregation_rules_from_file();
    test_log_file_configuration();
    test_data_dirs_configuration();
    test_io_flags();
    test_buffer_size_configuration();
    test_logger_level();
    test_papi_environment_configuration();
    test_papi_yaml_configuration();

    std::cout << "\n✓ All Configuration Manager tests passed!" << std::endl;
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "✗ Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
