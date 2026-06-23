#ifndef DFTRACER_PAPI_COUNTERS_H
#define DFTRACER_PAPI_COUNTERS_H

#ifdef DFTRACER_DEBUG
#include <dftracer/core/dftracer_config_dbg.hpp>
#else
#include <dftracer/core/dftracer_config.hpp>
#endif

#ifdef DFTRACER_PAPI_TRACING_ENABLE

#include <dftracer/core/function/generic_function.h>
#include <dftracer/core/utils/configuration_manager.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <papi.h>

namespace dftracer {

class PAPICounterFunction : public dftracer::GenericFunction {
 private:
  struct ThreadState {
    int event_set;
    bool registered;
    bool active;
    TimeResolution last_sample_time;
    std::vector<std::string> event_names;
    std::vector<std::string> absolute_keys;
    std::vector<std::string> delta_keys;
    std::vector<long long> last_values;
    std::vector<long long> current_values;

    ThreadState();
    ~ThreadState();
  };

  std::shared_ptr<dftracer::ConfigurationManager> config;
  std::vector<std::string> requested_events;
  std::once_flag library_init_flag;
  std::atomic<bool> library_ready;
  std::atomic<bool> enabled;
  std::atomic<size_t> registered_threads;

  static PAPICounterFunction *active_instance;

  int initialize_library();
  static ThreadState &get_thread_state();
  ThreadState &ensure_thread_state();
  bool should_sample(const ThreadState &state, TimeResolution now,
                     bool force) const;
  void log_sample(ThreadState &state, const char *reason, bool force);
  void cleanup_thread_state(ThreadState &state, bool log_sample);
  static std::string sanitize_metadata_key(const std::string &event_name,
                                           const std::string &prefix);

 public:
  PAPICounterFunction();
  ~PAPICounterFunction() override;

  void initialize() override;
  void finalize() override;
  void sample(bool force = false, const char *reason = "event");
  bool is_enabled() const { return enabled.load(); }
};

}  // namespace dftracer

#endif
#endif