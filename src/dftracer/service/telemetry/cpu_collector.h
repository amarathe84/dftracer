#ifndef DFTRACER_CPU_TELEMETRY_COLLECTOR_H
#define DFTRACER_CPU_TELEMETRY_COLLECTOR_H

#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/service/common/datastructure.h>
#include <dftracer/service/telemetry/telemetry_interface.h>

#include <atomic>
#include <memory>

namespace dftracer {

/**
 * @brief CPU metrics collector using /proc/stat
 */
class CPUTelemetryCollector : public TelemetryCollector {
 public:
  void initialize() override;
  void capture(std::shared_ptr<BufferManager> buffer_manager,
               std::shared_ptr<DFTLogger> logger, std::atomic<int>& index,
               TimeResolution timestamp) override;
  void finalize() override;
  std::string name() const override { return "cpu"; }

 private:
  void parseCpuMetrics(TimeResolution time,
                       std::shared_ptr<BufferManager> buffer_manager,
                       std::shared_ptr<DFTLogger> logger,
                       std::atomic<int>& index);
};

}  // namespace dftracer

#endif  // DFTRACER_CPU_TELEMETRY_COLLECTOR_H
