// Created by druva on 6/9/25

#ifndef DFTRACER_HIP_INTERCEPT_H
#define DFTRACER_HIP_INTERCEPT_H

#ifdef DFTRACER_DEBUG
#include <dftracer/core/dftracer_config_dbg.hpp>
#else
#include <dftracer/core/dftracer_config.hpp>
#endif
#ifdef DFTRACER_HIP_TRACING_ENABLE

#include <dftracer/core/common/logging.h>
#include <dftracer/core/function/generic_function.h>
#include <rocprofiler-sdk/buffer.h>
#include <rocprofiler-sdk/buffer_tracing.h>
#include <rocprofiler-sdk/registration.h>
#include <rocprofiler-sdk/rocprofiler.h>

#include <rocprofiler-sdk/cxx/name_info.hpp>

namespace conf {

extern "C" rocprofiler_tool_configure_result_t* roc_conf(
    uint32_t version, const char* runtime_version, uint32_t priority,
    rocprofiler_client_id_t* id);
}

namespace dftracer {

using kernel_symbol_data_t =
    rocprofiler_callback_tracing_code_object_kernel_symbol_register_data_t;

// Used to trace AMD GPU APIS - HIP, HSA, RCCL, and the memory APIS - counters
// not implemented
class HIPFunction : public dftracer::GenericFunction {
 private:
  rocprofiler::sdk::buffer_name_info client_name_info;
  rocprofiler_buffer_id_t client_buffer;
  rocprofiler_context_id_t client_ctx;
  std::unordered_map<rocprofiler_kernel_id_t, kernel_symbol_data_t>
      client_kernels;

  TimeResolution time_diff;
  TimeResolution transform_timestamp(rocprofiler_timestamp_t timestamp);
  TimeResolution transform_time(rocprofiler_timestamp_t end_time,
                                rocprofiler_timestamp_t start_time);

 public:
  HIPFunction() : dftracer::GenericFunction() {
    DFTRACER_LOG_DEBUG("Creating HIPFunction instance",
                       "");  // Initialize parent
    time_diff = 0;
  }

  static void tool_code_object_callback(
      rocprofiler_callback_tracing_record_t record,
      rocprofiler_user_data_t* user_data, void* callback_data);
  static void tool_tracing_callback(rocprofiler_context_id_t context,
                                    rocprofiler_buffer_id_t buffer_id,
                                    rocprofiler_record_header_t** headers,
                                    size_t num_headers, void* user_data,
                                    uint64_t drop_count);

  static void thread_precreate(rocprofiler_runtime_library_t lib,
                               void* tool_data);

  static void thread_postcreate(rocprofiler_runtime_library_t lib,
                                void* tool_data);

  static int tool_init(rocprofiler_client_finalize_t fini_func,
                       void* tool_data);
  static void tool_fini(void* tool_data);

  void initialize() override {
    DFTRACER_LOG_DEBUG("Initializing HIPFunction instance");
    rocprofiler_force_configure(&conf::roc_conf);
    rocprofiler_status_t status;
    status = rocprofiler_start_context(client_ctx);
    if (status != ROCPROFILER_STATUS_SUCCESS) {
      DFTRACER_LOG_ERROR("HIP Intercept context start failed: status, %d\n",
                         status);
    }
  }

  void finalize() override {
    DFTRACER_LOG_DEBUG("Finalizing HIPFunction instance");
    rocprofiler_stop_context(client_ctx);
    rocprofiler_flush_buffer(client_buffer);
  }
};

}  // namespace dftracer

#endif
#endif
