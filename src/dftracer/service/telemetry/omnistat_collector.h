#ifndef DFTRACER_OMNISTAT_TELEMETRY_COLLECTOR_H
#define DFTRACER_OMNISTAT_TELEMETRY_COLLECTOR_H

#include <dftracer/service/telemetry/telemetry_interface.h>

#ifdef DFTRACER_HIP_TRACING_ENABLE
#include <dftracer/core/function/hip/intercept.h>
#include <rocprofiler-sdk/buffer_tracing.h>
#endif

#include <string>
#include <unordered_set>
#include <vector>

namespace dftracer {

/**
 * @brief Represents a single omnistat metric sample.
 *
 * Can be populated from a CSV file (legacy) or from HIP tracing events.
 */
struct OmnistatMetricSample {
  TimeResolution timestamp;
  std::string metric_name;
  double metric_value;
  std::string hostname;
  std::string device_id;
  std::string original_timestamp;
};

/**
 * @brief Represents a HIP tracing event to be emitted as an omnistat metric.
 */
struct OmnistatHipEvent {
  TimeResolution timestamp;
  std::string event_name;
  std::string category;
  std::string metadata_str;  // JSON-like key=value pairs
};

/**
 * @brief Telemetry collector that combines CSV-based omnistat metrics
 * with real-time HIP tracing events from the rocprofiler SDK.
 *
 * This collector reads metric samples from a CSV file (legacy mode) and
 * also captures HIP runtime API calls, kernel dispatches, memory copies,
 * and page migration events from the rocprofiler SDK when HIP tracing is
 * enabled. All events are emitted via the buffer_manager as counter events.
 */
class OmnistatTelemetryCollector : public TelemetryCollector {
 public:
  OmnistatTelemetryCollector();

  void initialize() override;
  void capture(std::shared_ptr<BufferManager> buffer_manager,
               std::shared_ptr<DFTLogger> logger, std::atomic<int>& index,
               TimeResolution timestamp) override;
  void finalize() override;
  std::string name() const override { return "omnistat"; }

  /**
   * @brief Push a HIP tracing event into the omnistat event buffer.
   *
   * Called from the rocprofiler SDK tracing callback to record HIP events
   * that should be emitted as omnistat metrics.
   */
  void push_hip_event(TimeResolution timestamp, const std::string& event_name,
                      const std::string& category,
                      const std::string& metadata_str);

  /**
   * @brief Push a HIP kernel dispatch event.
   */
  void push_hip_kernel(TimeResolution timestamp, const std::string& kernel_name,
                       uint64_t correlation_id, uint32_t thread_id);

  /**
   * @brief Push a HIP memory copy event.
   */
  void push_hip_memory_copy(TimeResolution timestamp, uint64_t src_agent,
                            uint64_t dst_agent, uint64_t correlation_id,
                            uint32_t thread_id);

  /**
   * @brief Push a HIP page migration event.
   */
#ifdef DFTRACER_HIP_TRACING_ENABLE
  void push_hip_page_migration(TimeResolution timestamp,
                               rocprofiler_page_migration_operation_t op,
                               const std::string& details);
#endif

  /**
   * @brief Push a HIP runtime API event.
   */
  void push_hip_runtime_api(TimeResolution timestamp,
                            const std::string& api_name,
                            uint64_t correlation_id, uint32_t thread_id);

  /**
   * @brief Push a HSA API event.
   */
  void push_hsa_api(TimeResolution timestamp, const std::string& api_name,
                    uint64_t correlation_id, uint32_t thread_id);

  static TimeResolution normalize_timestamp(const std::string& raw);

 private:
  bool enabled;
  bool emitted;
  std::string input_file;
  std::string timestamp_column;
  bool include_all_counters;
  bool attach_to_trace;
  bool hip_events_enabled;
  std::unordered_set<std::string> selected_counters;
  std::vector<OmnistatMetricSample> samples;
  std::vector<OmnistatHipEvent> hip_events;
  std::mutex hip_events_mutex;

  void load_samples();
  void flush_hip_events(std::shared_ptr<BufferManager> buffer_manager,
                        std::atomic<int>& index, TimeResolution timestamp);
  static std::vector<std::string> split_csv_line(const std::string& line);
  static std::string trim(const std::string& value);
  static bool try_parse_double(const std::string& value, double& parsed);
};

}  // namespace dftracer

#endif  // DFTRACER_OMNISTAT_TELEMETRY_COLLECTOR_H
