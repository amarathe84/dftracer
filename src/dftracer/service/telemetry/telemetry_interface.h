#ifndef DFTRACER_TELEMETRY_INTERFACE_H
#define DFTRACER_TELEMETRY_INTERFACE_H

#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/df_logger.h>

#include <atomic>
#include <memory>
#include <string>

namespace dftracer {

/**
 * @brief Base interface for node-local telemetry collectors
 *
 * Defines the contract for collecting various types of system metrics.
 * Implementations should override initialize(), capture(), and finalize().
 */
class TelemetryCollector {
 public:
  virtual ~TelemetryCollector() = default;

  /**
   * @brief Initialize the telemetry collector
   *
   * This method is called once when the collector is created.
   * It should set up any necessary resources or state.
   */
  virtual void initialize() = 0;

  /**
   * @brief Capture current telemetry data
   *
   * This method is called periodically to collect metrics.
   * It should read the current metric values from the system.
   *
   * @param buffer_manager The buffer manager for logging events
   * @param logger The logger instance
   * @param index Reference to the event index counter
   * @param timestamp The current timestamp
   */
  virtual void capture(std::shared_ptr<BufferManager> buffer_manager,
                       std::shared_ptr<DFTLogger> logger,
                       std::atomic<int>& index, TimeResolution timestamp) = 0;

  /**
   * @brief Finalize the telemetry collector
   *
   * This method is called when the collector is destroyed.
   * It should clean up any resources.
   */
  virtual void finalize() = 0;

  /**
   * @brief Get the name of this telemetry collector
   *
   * @return String identifier for this collector
   */
  virtual std::string name() const = 0;
};

}  // namespace dftracer

#endif  // DFTRACER_TELEMETRY_INTERFACE_H
