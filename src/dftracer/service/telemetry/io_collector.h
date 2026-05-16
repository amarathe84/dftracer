#ifndef DFTRACER_IO_TELEMETRY_COLLECTOR_H
#define DFTRACER_IO_TELEMETRY_COLLECTOR_H

#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/service/common/datastructure.h>
#include <dftracer/service/telemetry/telemetry_interface.h>

#include <atomic>
#include <map>
#include <memory>
#include <string>

namespace dftracer {

/**
 * @brief I/O metrics collector using /proc/diskstats
 */
class IOTelemetryCollector : public TelemetryCollector {
 public:
  void initialize() override;
  void capture(std::shared_ptr<BufferManager> buffer_manager,
               std::shared_ptr<DFTLogger> logger, std::atomic<int>& index,
               TimeResolution timestamp) override;
  void finalize() override;
  std::string name() const override { return "io"; }

 private:
  std::map<std::string, IOMetrics> previous_io_metrics;
  void parseIOMetrics(TimeResolution time,
                      std::shared_ptr<BufferManager> buffer_manager,
                      std::shared_ptr<DFTLogger> logger,
                      std::atomic<int>& index);
};

}  // namespace dftracer

#endif  // DFTRACER_IO_TELEMETRY_COLLECTOR_H
