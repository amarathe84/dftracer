#ifndef DFTRACER_TELEMETRY_FACTORY_H
#define DFTRACER_TELEMETRY_FACTORY_H

#include <dftracer/service/telemetry/telemetry_interface.h>

#include <memory>
#include <string>
#include <vector>

namespace dftracer {

/**
 * @brief Factory for creating telemetry collectors
 *
 * Uses the factory pattern to instantiate the appropriate telemetry
 * collector based on type. Supports:
 * - "cpu": CPU metrics collection
 * - "memory": Memory metrics collection
 * - "io": Disk I/O metrics collection
 * - "network": Network metrics collection
 */
class TelemetryCollectorFactory {
 public:
  /**
   * @brief Create a telemetry collector of the specified type
   *
   * @param type The type of collector to create ("cpu", "memory", "io",
   * "network")
   * @return A unique pointer to the created collector
   * @throws std::invalid_argument if type is not recognized
   */
  static std::unique_ptr<TelemetryCollector> create(const std::string& type);

  /**
   * @brief Create collectors for all available types
   *
   * @return A vector of unique pointers to all available collectors
   */
  static std::vector<std::unique_ptr<TelemetryCollector>> create_all();

  /**
   * @brief Get list of supported collector types
   *
   * @return Vector of supported type names
   */
  static std::vector<std::string> get_supported_types();

  /**
   * @brief Check if a collector type is supported
   *
   * @param type The type to check
   * @return true if the type is supported, false otherwise
   */
  static bool is_supported(const std::string& type);
};

}  // namespace dftracer

#endif  // DFTRACER_TELEMETRY_FACTORY_H
