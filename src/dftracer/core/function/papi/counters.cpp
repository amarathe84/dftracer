#include <dftracer/core/function/papi/counters.h>

#ifdef DFTRACER_PAPI_TRACING_ENABLE

#include <pthread.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <sstream>
#include <unordered_set>

template <>
std::shared_ptr<dftracer::PAPICounterFunction>
    dftracer::Singleton<dftracer::PAPICounterFunction>::instance = nullptr;
template <>
bool dftracer::Singleton<dftracer::PAPICounterFunction>::stop_creating_instances =
    false;

namespace {

unsigned long papi_thread_identifier() {
  return static_cast<unsigned long>(std::hash<pthread_t>{}(pthread_self()));
}

std::vector<std::string> normalize_events(
    const std::vector<std::string> &raw_events) {
  std::vector<std::string> normalized;
  std::unordered_set<std::string> seen;
  for (auto event : raw_events) {
    auto first = event.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) continue;
    auto last = event.find_last_not_of(" \t\n\r");
    event = event.substr(first, last - first + 1);
    if (event.empty() || seen.count(event) > 0) continue;
    seen.insert(event);
    normalized.push_back(event);
  }
  return normalized;
}

void log_papi_status(const char *operation, int retval) {
  DFTRACER_LOG_WARN("PAPI operation %s failed with %d (%s)", operation, retval,
                    PAPI_strerror(retval));
}

}  // namespace

namespace dftracer {

PAPICounterFunction *PAPICounterFunction::active_instance = nullptr;

PAPICounterFunction::ThreadState::ThreadState()
    : event_set(PAPI_NULL),
      registered(false),
      active(false),
      last_sample_time(0),
      event_names(),
      absolute_keys(),
      delta_keys(),
      last_values(),
      current_values() {}

PAPICounterFunction::ThreadState::~ThreadState() {
  if (PAPICounterFunction::active_instance != nullptr) {
    PAPICounterFunction::active_instance->cleanup_thread_state(*this, false);
  }
}

PAPICounterFunction::PAPICounterFunction()
    : dftracer::GenericFunction(),
      config(dftracer::Singleton<dftracer::ConfigurationManager>::get_instance()),
      requested_events(normalize_events(config->papi_events)),
      library_ready(false),
      enabled(config->papi_tracing && !requested_events.empty()),
      registered_threads(0) {
  active_instance = this;
  if (!config->papi_tracing) {
    DFTRACER_LOG_DEBUG("PAPI tracing runtime option disabled", "");
  } else if (requested_events.empty()) {
    DFTRACER_LOG_WARN("PAPI tracing requested without any valid events", "");
  }
}

PAPICounterFunction::~PAPICounterFunction() { active_instance = nullptr; }

std::string PAPICounterFunction::sanitize_metadata_key(
    const std::string &event_name, const std::string &prefix) {
  std::string result = prefix;
  result.reserve(prefix.size() + event_name.size());
  for (char ch : event_name) {
    unsigned char value = static_cast<unsigned char>(ch);
    if (std::isalnum(value) != 0) {
      result.push_back(static_cast<char>(std::tolower(value)));
    } else {
      result.push_back('_');
    }
  }
  return result;
}

int PAPICounterFunction::initialize_library() {
  if (!enabled.load()) return PAPI_ENOEVNT;

  requested_events = normalize_events(config->papi_events);
  if (requested_events.empty()) {
    enabled.store(false);
    DFTRACER_LOG_WARN("PAPI tracing has no configured events", "");
    return PAPI_ENOEVNT;
  }

  int retval = PAPI_library_init(PAPI_VER_CURRENT);
  if (retval != PAPI_VER_CURRENT) {
    enabled.store(false);
    DFTRACER_LOG_WARN("PAPI_library_init failed with %d (%s)", retval,
                      PAPI_strerror(retval));
    return retval;
  }

  retval = PAPI_thread_init(papi_thread_identifier);
  if (retval != PAPI_OK) {
    enabled.store(false);
    log_papi_status("PAPI_thread_init", retval);
    return retval;
  }

  if (config->papi_multiplex) {
    retval = PAPI_multiplex_init();
    if (retval != PAPI_OK) {
      config->papi_multiplex = false;
      log_papi_status("PAPI_multiplex_init", retval);
    }
  }

  std::vector<std::string> validated_events;
  for (const auto &event_name : requested_events) {
    retval = PAPI_query_named_event(event_name.c_str());
    if (retval == PAPI_OK) {
      validated_events.push_back(event_name);
    } else {
      log_papi_status(event_name.c_str(), retval);
    }
  }

  requested_events = std::move(validated_events);
  if (requested_events.empty()) {
    enabled.store(false);
    DFTRACER_LOG_WARN("PAPI tracing disabled because no configured counters are available", "");
    return PAPI_ENOEVNT;
  }

  library_ready.store(true);
  return PAPI_OK;
}

void PAPICounterFunction::initialize() {
  std::call_once(library_init_flag,
                 [this]() { initialize_library(); });
  if (!library_ready.load()) return;
  auto &state = ensure_thread_state();
  if (!state.active) return;
  log_sample(state, "init", true);
}

PAPICounterFunction::ThreadState &PAPICounterFunction::get_thread_state() {
  static thread_local ThreadState thread_state;
  return thread_state;
}

PAPICounterFunction::ThreadState &PAPICounterFunction::ensure_thread_state() {
  auto &thread_state = get_thread_state();

  if (thread_state.active || !library_ready.load()) {
    return thread_state;
  }

  int retval = PAPI_register_thread();
  if (retval != PAPI_OK) {
    log_papi_status("PAPI_register_thread", retval);
    return thread_state;
  }
  thread_state.registered = true;
  registered_threads.fetch_add(1);

  retval = PAPI_create_eventset(&thread_state.event_set);
  if (retval != PAPI_OK) {
    log_papi_status("PAPI_create_eventset", retval);
    cleanup_thread_state(thread_state, false);
    return thread_state;
  }

  if (config->papi_multiplex) {
    retval = PAPI_assign_eventset_component(thread_state.event_set, 0);
    if (retval != PAPI_OK) {
      log_papi_status("PAPI_assign_eventset_component", retval);
    } else {
      retval = PAPI_set_multiplex(thread_state.event_set);
      if (retval != PAPI_OK) {
        log_papi_status("PAPI_set_multiplex", retval);
      }
    }
  }

  for (const auto &event_name : requested_events) {
    retval = PAPI_add_named_event(thread_state.event_set, event_name.c_str());
    if (retval != PAPI_OK) {
      log_papi_status(event_name.c_str(), retval);
      continue;
    }
    thread_state.event_names.push_back(event_name);
    thread_state.absolute_keys.push_back(
        sanitize_metadata_key(event_name, "abs_"));
    thread_state.delta_keys.push_back(sanitize_metadata_key(event_name, "delta_"));
  }

  if (thread_state.event_names.empty()) {
    DFTRACER_LOG_WARN("PAPI tracing could not add any counters to the current thread event set", "");
    cleanup_thread_state(thread_state, false);
    return thread_state;
  }

  thread_state.last_values.assign(thread_state.event_names.size(), 0);
  thread_state.current_values.assign(thread_state.event_names.size(), 0);

  retval = PAPI_start(thread_state.event_set);
  if (retval != PAPI_OK) {
    log_papi_status("PAPI_start", retval);
    cleanup_thread_state(thread_state, false);
    return thread_state;
  }

  retval = PAPI_read(thread_state.event_set, thread_state.last_values.data());
  if (retval != PAPI_OK) {
    log_papi_status("PAPI_read", retval);
    std::fill(thread_state.last_values.begin(), thread_state.last_values.end(), 0);
  }

  thread_state.last_sample_time = logger->get_time();
  thread_state.active = true;
  return thread_state;
}

bool PAPICounterFunction::should_sample(const ThreadState &state,
                                        TimeResolution now,
                                        bool force) const {
  if (!state.active) return false;
  if (force) return true;
  size_t interval_ms = config->papi_sample_interval_ms;
  if (interval_ms == 0) return true;
  TimeResolution interval_us = interval_ms * 1000;
  return (now >= state.last_sample_time) &&
         (now - state.last_sample_time >= interval_us);
}

void PAPICounterFunction::log_sample(ThreadState &state, const char *reason,
                                     bool force) {
  TimeResolution now = logger->get_time();
  if (!should_sample(state, now, force)) return;

  int retval = PAPI_read(state.event_set, state.current_values.data());
  if (retval != PAPI_OK) {
    log_papi_status("PAPI_read", retval);
    return;
  }

  auto metadata = new Metadata();
  metadata->insert_or_assign("reason", std::string(reason));
  metadata->insert_or_assign("num_counters",
                             static_cast<int>(state.event_names.size()));
  metadata->insert_or_assign("multiplex", config->papi_multiplex ? 1 : 0);

  std::ostringstream event_list;
  for (size_t idx = 0; idx < state.event_names.size(); ++idx) {
    if (idx > 0) event_list << ',';
    event_list << state.event_names[idx];
    metadata->insert_or_assign(state.absolute_keys[idx],
                               state.current_values[idx]);
    metadata->insert_or_assign(state.delta_keys[idx],
                               state.current_values[idx] - state.last_values[idx]);
  }
  metadata->insert_or_assign("events", event_list.str());

  TimeResolution sample_start = state.last_sample_time;
  TimeResolution sample_duration = 0;
  if (sample_start > 0 && now >= sample_start) {
    sample_duration = now - sample_start;
  } else {
    sample_start = now;
  }

  logger->enter_event();
  logger->log("papi_sample", "papi", sample_start, sample_duration,
              metadata);
  logger->exit_event();

  state.last_values = state.current_values;
  state.last_sample_time = now;
}

void PAPICounterFunction::sample(bool force, const char *reason) {
  if (!enabled.load()) return;
  std::call_once(library_init_flag,
                 [this]() { initialize_library(); });
  if (!library_ready.load()) return;

  auto &state = ensure_thread_state();
  if (!state.active) return;
  log_sample(state, reason, force);
}

void PAPICounterFunction::cleanup_thread_state(ThreadState &state,
                                               bool emit_sample) {
  if (!state.registered && state.event_set == PAPI_NULL) return;

  if (emit_sample && state.active) {
    log_sample(state, "finalize", true);
  }

  if (state.active) {
    int retval = PAPI_stop(state.event_set, state.current_values.data());
    if (retval != PAPI_OK) {
      log_papi_status("PAPI_stop", retval);
    }
    state.active = false;
  }

  if (state.event_set != PAPI_NULL) {
    int retval = PAPI_cleanup_eventset(state.event_set);
    if (retval != PAPI_OK) {
      log_papi_status("PAPI_cleanup_eventset", retval);
    }
    retval = PAPI_destroy_eventset(&state.event_set);
    if (retval != PAPI_OK) {
      log_papi_status("PAPI_destroy_eventset", retval);
    }
    state.event_set = PAPI_NULL;
  }

  if (state.registered) {
    int retval = PAPI_unregister_thread();
    if (retval != PAPI_OK) {
      log_papi_status("PAPI_unregister_thread", retval);
    }
    state.registered = false;
    if (registered_threads.load() > 0) {
      registered_threads.fetch_sub(1);
    }
  }

  state.last_sample_time = 0;
  state.event_names.clear();
  state.absolute_keys.clear();
  state.delta_keys.clear();
  state.last_values.clear();
  state.current_values.clear();
}

void PAPICounterFunction::finalize() {
  if (!enabled.load()) return;
  auto &state = get_thread_state();
  if (!state.registered && state.event_set == PAPI_NULL && !state.active) {
    return;
  }
  cleanup_thread_state(state, true);
}

}  // namespace dftracer

#endif