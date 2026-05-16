#ifndef DFTRACER_AGGREGATOR_H
#define DFTRACER_AGGREGATOR_H
#include <dftracer/core/common/logging.h>
//
#include <dftracer/core/aggregator/rules.h>
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/core/utils/utils.h>

#include <any>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace dftracer {
typedef std::unordered_map<AggregatedKey, AggregatedValues *>
    AggregatedDataPair;
typedef std::map<TimeResolution, AggregatedDataPair> AggregatedDataType;
class Aggregator {
 private:
  AggregatedDataType aggregated_data_;
  std::shared_ptr<dftracer::ConfigurationManager> config;
  TimeResolution last_interval;
  TimeResolution cached_interval_us;  // Cache interval calculation
  bool is_first;
  std::shared_mutex mtx;
  Rules inclusion_rules;
  Rules exclusion_rules;
  bool always_aggregate;
  template <typename T>
  inline void insert_number_value(TimeResolution &time_interval,
                                  AggregatedKey &aggregated_key,
                                  const std::string &key, T value) {
    auto iter = aggregated_data_[time_interval].find(aggregated_key);
    auto num_value = new NumberAggregationValue<T>(value);
    if (iter != aggregated_data_[time_interval].end()) {
      iter->second->update(key, typeid(TimeResolution), num_value);
    } else {
      auto value = new AggregatedValues();
      value->update(key, typeid(TimeResolution), num_value);
      aggregated_data_[time_interval].insert_or_assign(aggregated_key, value);
      DFTRACER_LOG_INFO("Events in %llu are %d", time_interval,
                        aggregated_data_[time_interval].size());
    }
  }

  template <typename T>
  inline void insert_general_value(TimeResolution &time_interval,
                                   AggregatedKey &aggregated_key,
                                   const std::string &key, T value) {
    auto iter = aggregated_data_[time_interval].find(aggregated_key);
    auto num_value = new AggregatedValue<T>(value);
    if (iter != aggregated_data_[time_interval].end()) {
      iter->second->update(key, typeid(TimeResolution), num_value);
    } else {
      auto value = new AggregatedValues();
      value->update(key, typeid(TimeResolution), num_value);
      aggregated_data_[time_interval].insert_or_assign(aggregated_key, value);
      DFTRACER_LOG_INFO("Events in %llu are %d", time_interval,
                        aggregated_data_[time_interval].size());
    }
  }

 public:
  Aggregator() {
    config =
        dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
    for (const auto &rule : config->aggregation_inclusion_rules) {
      inclusion_rules.addRule(rule);
    }
    for (const auto &rule : config->aggregation_exclusion_rules) {
      exclusion_rules.addRule(rule);
    }
    always_aggregate = true;
    if (config->aggregation_inclusion_rules.size() > 0 ||
        config->aggregation_exclusion_rules.size() > 0) {
      always_aggregate = false;
    }
    last_interval = 0;
    is_first = true;
    cached_interval_us =
        config->trace_interval_ms * 1000;  // Cache to avoid per-event division
  }
  bool should_aggregate(const AggregatedKey *key) {
    if (always_aggregate) return true;
    if (inclusion_rules.satisfies(key) && !exclusion_rules.satisfies(key))
      return true;
    return false;
  }
  ~Aggregator() {}
  void finalize() {
    std::unique_lock<std::shared_mutex> lock(mtx);
    for (auto &interval_map : aggregated_data_) {
      for (auto &pair : interval_map.second) {
        for (auto &val_pair : pair.second->values) {
          delete val_pair.second;
        }
        delete pair.second;
      }
    }
  }
  bool aggregate(AggregatedKey &aggregated_key);
  int get_previous_aggregations(AggregatedDataType &data, bool all = false);
};
}  // namespace dftracer
#endif  // DFTRACER_AGGREGATOR_H