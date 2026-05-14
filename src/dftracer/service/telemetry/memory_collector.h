#ifndef DFTRACER_MEMORY_TELEMETRY_COLLECTOR_H
#define DFTRACER_MEMORY_TELEMETRY_COLLECTOR_H

#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/service/common/datastructure.h>
#include <dftracer/service/telemetry/telemetry_interface.h>

#include <atomic>
#include <memory>

namespace dftracer {

/**
 * @brief Memory metrics collector using /proc/meminfo
 */
class MemoryTelemetryCollector : public TelemetryCollector {
 public:
  void initialize() override;
  void capture(std::shared_ptr<BufferManager> buffer_manager,
               std::shared_ptr<DFTLogger> logger, std::atomic<int>& index,
               TimeResolution timestamp) override;
  void finalize() override;
  std::string name() const override { return "memory"; }

 private:
  unsigned long long mem_available = 0;
  void parseMemMetrics(TimeResolution time,
                       std::shared_ptr<BufferManager> buffer_manager,
                       std::shared_ptr<DFTLogger> logger,
                       std::atomic<int>& index);
};

}  // namespace dftracer

#endif  // DFTRACER_MEMORY_TELEMETRY_COLLECTOR_H
