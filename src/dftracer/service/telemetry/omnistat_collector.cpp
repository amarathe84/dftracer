#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/service/telemetry/omnistat_collector.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace dftracer {

OmnistatTelemetryCollector::OmnistatTelemetryCollector()
    : enabled(false),
      emitted(false),
      input_file(),
      timestamp_column("timestamp"),
      include_all_counters(false),
      attach_to_trace(true),
      hip_events_enabled(false),
      selected_counters(),
      samples(),
      hip_events() {}

void OmnistatTelemetryCollector::initialize() {
  auto config = Singleton<ConfigurationManager>::get_instance();
  enabled = config->omnistat_enable;
  emitted = false;
  input_file = config->omnistat_input_file;
  timestamp_column = config->omnistat_timestamp_column.empty()
                         ? "timestamp"
                         : config->omnistat_timestamp_column;
  include_all_counters = config->omnistat_include_all_counters;
  attach_to_trace = config->omnistat_attach_to_trace;
  hip_events_enabled = config->omnistat_enable &&
                       config->omnistat_attach_to_trace && !input_file.empty();
  selected_counters.clear();
  for (const auto& counter : config->omnistat_counters) {
    selected_counters.insert(counter);
  }
  samples.clear();
  hip_events.clear();

  if (!enabled) {
    return;
  }
  if (!attach_to_trace) {
    DFTRACER_LOG_INFO("Omnistat enabled but attach_to_trace is disabled");
    return;
  }
  if (input_file.empty()) {
    throw std::runtime_error(
        "Omnistat telemetry enabled but no input file was configured.");
  }
  load_samples();
}

void OmnistatTelemetryCollector::capture(
    std::shared_ptr<BufferManager> buffer_manager,
    std::shared_ptr<DFTLogger> /*logger*/, std::atomic<int>& index,
    TimeResolution /*timestamp*/) {
  if (!enabled || !attach_to_trace || emitted) {
    return;
  }

  // Emit CSV-based samples first
  for (const auto& sample : samples) {
    auto* metadata = new Metadata();
    metadata->insert_or_assign("source", std::string("omnistat"));
    metadata->insert_or_assign("metric", sample.metric_name);
    metadata->insert_or_assign("value", sample.metric_value,
                               MetadataType::MT_VALUE);
    if (!sample.hostname.empty()) {
      metadata->insert_or_assign("hostname", sample.hostname);
    }
    if (!sample.device_id.empty()) {
      metadata->insert_or_assign("device_id", sample.device_id);
    }
    if (!sample.original_timestamp.empty()) {
      metadata->insert_or_assign("original_timestamp",
                                 sample.original_timestamp);
    }
    int current_index = index.fetch_add(1, std::memory_order_relaxed);
    buffer_manager->log_counter_event(current_index, sample.metric_name.c_str(),
                                      "omnistat", sample.timestamp, 0, 0,
                                      metadata);
  }

  // Flush any HIP tracing events collected since last capture
  flush_hip_events(
      buffer_manager, index,
      samples.empty() ? TimeResolution(0) : samples.back().timestamp);
}

void OmnistatTelemetryCollector::finalize() {
  samples.clear();
  hip_events.clear();
}

void OmnistatTelemetryCollector::push_hip_event(
    TimeResolution timestamp, const std::string& event_name,
    const std::string& category, const std::string& metadata_str) {
  if (!hip_events_enabled) {
    return;
  }
  std::lock_guard<std::mutex> lock(hip_events_mutex);
  hip_events.push_back({timestamp, event_name, category, metadata_str});
}

void OmnistatTelemetryCollector::push_hip_kernel(TimeResolution timestamp,
                                                 const std::string& kernel_name,
                                                 uint64_t correlation_id,
                                                 uint32_t thread_id) {
  std::string meta = "correlation_id=" + std::to_string(correlation_id) +
                     " thread_id=" + std::to_string(thread_id);
  push_hip_event(timestamp, kernel_name, "HIP_KERNEL_DISPATCH", meta);
}

void OmnistatTelemetryCollector::push_hip_memory_copy(TimeResolution timestamp,
                                                      uint64_t src_agent,
                                                      uint64_t dst_agent,
                                                      uint64_t correlation_id,
                                                      uint32_t thread_id) {
  std::string meta = "src_agent=" + std::to_string(src_agent) +
                     " dst_agent=" + std::to_string(dst_agent) +
                     " correlation_id=" + std::to_string(correlation_id) +
                     " thread_id=" + std::to_string(thread_id);
  push_hip_event(timestamp, "hipMemcpy", "HIP_MEMORY_COPY", meta);
}

#ifdef DFTRACER_HIP_TRACING_ENABLE
void OmnistatTelemetryCollector::push_hip_page_migration(
    TimeResolution timestamp, rocprofiler_page_migration_operation_t op,
    const std::string& details) {
  std::string op_name;
  switch (op) {
    case ROCPROFILER_PAGE_MIGRATION_PAGE_MIGRATE_START:
      op_name = "page_migration_page_migrate_start";
      break;
    case ROCPROFILER_PAGE_MIGRATION_PAGE_MIGRATE_END:
      op_name = "page_migration_page_migrate_end";
      break;
    case ROCPROFILER_PAGE_MIGRATION_PAGE_FAULT_START:
      op_name = "page_migration_page_fault_start";
      break;
    case ROCPROFILER_PAGE_MIGRATION_PAGE_FAULT_END:
      op_name = "page_migration_page_fault_end";
      break;
    case ROCPROFILER_PAGE_MIGRATION_QUEUE_EVICTION:
      op_name = "page_migration_queue_eviction";
      break;
    case ROCPROFILER_PAGE_MIGRATION_QUEUE_RESTORE:
      op_name = "page_migration_queue_restore";
      break;
    case ROCPROFILER_PAGE_MIGRATION_UNMAP_FROM_GPU:
      op_name = "page_migration_unmap_from_gpu";
      break;
    case ROCPROFILER_PAGE_MIGRATION_DROPPED_EVENT:
      op_name = "page_migration_dropped_event";
      break;
    default:
      op_name = "page_migration_unknown";
      break;
  }
  push_hip_event(timestamp, op_name, "PAGE_MIGRATION", details);
}
#endif

void OmnistatTelemetryCollector::push_hip_runtime_api(
    TimeResolution timestamp, const std::string& api_name,
    uint64_t correlation_id, uint32_t thread_id) {
  std::string meta = "correlation_id=" + std::to_string(correlation_id) +
                     " thread_id=" + std::to_string(thread_id);
  push_hip_event(timestamp, api_name, "HIP_RUNTIME_API", meta);
}

void OmnistatTelemetryCollector::push_hsa_api(TimeResolution timestamp,
                                              const std::string& api_name,
                                              uint64_t correlation_id,
                                              uint32_t thread_id) {
  std::string meta = "correlation_id=" + std::to_string(correlation_id) +
                     " thread_id=" + std::to_string(thread_id);
  push_hip_event(timestamp, api_name, "HSA_API", meta);
}

void OmnistatTelemetryCollector::flush_hip_events(
    std::shared_ptr<BufferManager> buffer_manager, std::atomic<int>& index,
    TimeResolution timestamp) {
  if (!hip_events_enabled) {
    return;
  }

  std::lock_guard<std::mutex> lock(hip_events_mutex);
  if (hip_events.empty()) {
    return;
  }

  for (const auto& event : hip_events) {
    auto* metadata = new Metadata();
    metadata->insert_or_assign("source", std::string("omnistat"));
    metadata->insert_or_assign("event", event.event_name);
    metadata->insert_or_assign("category", event.category);
    if (!event.metadata_str.empty()) {
      metadata->insert_or_assign("metadata", event.metadata_str);
    }
    int current_index = index.fetch_add(1, std::memory_order_relaxed);
    buffer_manager->log_counter_event(current_index, event.event_name.c_str(),
                                      event.category.c_str(), event.timestamp,
                                      0, 0, metadata);
  }

  hip_events.clear();
}

void OmnistatTelemetryCollector::load_samples() {
  if (!std::filesystem::exists(input_file)) {
    throw std::runtime_error("Omnistat input file does not exist: " +
                             input_file);
  }

  std::ifstream input(input_file);
  if (!input.is_open()) {
    throw std::runtime_error("Unable to open Omnistat input file: " +
                             input_file);
  }

  std::string header_line;
  if (!std::getline(input, header_line)) {
    throw std::runtime_error("Omnistat CSV is empty: " + input_file);
  }

  auto headers = split_csv_line(header_line);
  if (headers.empty()) {
    throw std::runtime_error("Omnistat CSV header is empty: " + input_file);
  }

  std::unordered_map<std::string, size_t> header_index;
  for (size_t i = 0; i < headers.size(); ++i) {
    header_index.emplace(trim(headers[i]), i);
  }

  auto timestamp_it = header_index.find(timestamp_column);
  if (timestamp_it == header_index.end()) {
    throw std::runtime_error("Omnistat CSV missing timestamp column: " +
                             timestamp_column);
  }
  const size_t timestamp_idx = timestamp_it->second;
  const auto hostname_it = header_index.find("hostname");
  const auto gpu_it = header_index.find("gpu_id");
  const auto device_it = header_index.find("device_id");

  std::vector<size_t> metric_indices;
  std::vector<std::string> metric_names;
  for (size_t i = 0; i < headers.size(); ++i) {
    const std::string name = trim(headers[i]);
    if (i == timestamp_idx || name == "hostname" || name == "gpu_id" ||
        name == "device_id") {
      continue;
    }
    if (!include_all_counters && !selected_counters.empty() &&
        selected_counters.find(name) == selected_counters.end()) {
      continue;
    }
    metric_indices.push_back(i);
    metric_names.push_back(name);
  }

  std::string line;
  size_t line_number = 1;
  while (std::getline(input, line)) {
    ++line_number;
    if (trim(line).empty()) {
      continue;
    }
    auto columns = split_csv_line(line);
    if (columns.size() < headers.size()) {
      DFTRACER_LOG_WARN("Skipping malformed Omnistat row %zu from %s",
                        line_number, input_file.c_str());
      continue;
    }

    const std::string raw_timestamp = trim(columns[timestamp_idx]);
    TimeResolution ts = 0;
    try {
      ts = normalize_timestamp(raw_timestamp);
    } catch (const std::exception& ex) {
      DFTRACER_LOG_WARN(
          "Skipping Omnistat row %zu due to timestamp parse failure: %s",
          line_number, ex.what());
      continue;
    }

    std::string hostname;
    if (hostname_it != header_index.end()) {
      hostname = trim(columns[hostname_it->second]);
    }

    std::string device_id;
    if (gpu_it != header_index.end()) {
      device_id = trim(columns[gpu_it->second]);
    } else if (device_it != header_index.end()) {
      device_id = trim(columns[device_it->second]);
    }

    for (size_t idx = 0; idx < metric_indices.size(); ++idx) {
      double parsed_value = 0.0;
      const std::string raw_value = trim(columns[metric_indices[idx]]);
      if (!try_parse_double(raw_value, parsed_value)) {
        continue;
      }
      samples.push_back({ts, metric_names[idx], parsed_value, hostname,
                         device_id, raw_timestamp});
    }
  }
}

std::vector<std::string> OmnistatTelemetryCollector::split_csv_line(
    const std::string& line) {
  std::vector<std::string> values;
  std::string current;
  bool in_quotes = false;
  for (char ch : line) {
    if (ch == '"') {
      in_quotes = !in_quotes;
      continue;
    }
    if (ch == ',' && !in_quotes) {
      values.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  values.push_back(current);
  return values;
}

std::string OmnistatTelemetryCollector::trim(const std::string& value) {
  size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start])) != 0) {
    ++start;
  }
  size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return value.substr(start, end - start);
}

bool OmnistatTelemetryCollector::try_parse_double(const std::string& value,
                                                  double& parsed) {
  if (value.empty()) {
    return false;
  }
  char* end_ptr = nullptr;
  parsed = std::strtod(value.c_str(), &end_ptr);
  if (end_ptr == value.c_str() || *end_ptr != '\0' || !std::isfinite(parsed)) {
    return false;
  }
  return true;
}

TimeResolution OmnistatTelemetryCollector::normalize_timestamp(
    const std::string& raw) {
  double parsed = 0.0;
  if (!try_parse_double(raw, parsed)) {
    throw std::runtime_error(
        "Only numeric epoch timestamps are currently supported");
  }

  const double abs_value = std::fabs(parsed);
  if (abs_value < 1.0e11) {
    return static_cast<TimeResolution>(parsed * 1000000.0);
  }
  if (abs_value < 1.0e14) {
    return static_cast<TimeResolution>(parsed * 1000.0);
  }
  if (abs_value < 1.0e17) {
    return static_cast<TimeResolution>(parsed);
  }
  return static_cast<TimeResolution>(parsed / 1000.0);
}

}  // namespace dftracer
