#include <dftracer/core/function/hip/intercept.h>
#ifdef DFTRACER_HIP_TRACING_ENABLE

#include <dftracer/core/common/logging.h>
#include <rocprofiler-sdk/buffer.h>
#include <rocprofiler-sdk/buffer_tracing.h>
#include <rocprofiler-sdk/external_correlation.h>
#include <rocprofiler-sdk/fwd.h>
#include <rocprofiler-sdk/internal_threading.h>
#include <rocprofiler-sdk/registration.h>
#include <rocprofiler-sdk/rocprofiler.h>

#include <any>
#include <string>
#include <string_view>
#include <unordered_map>

#define MAX_EVENT_NAME_LENGTH 15

namespace conf {
extern "C" rocprofiler_tool_configure_result_t* roc_conf(
    uint32_t version, const char* runtime_version, uint32_t priority,
    rocprofiler_client_id_t* id) {
  DFTRACER_LOG_DEBUG("HIPFunction configured");
  void* client_tool_data = nullptr;
  rocprofiler_at_internal_thread_create(
      dftracer::HIPFunction::thread_precreate,
      dftracer::HIPFunction::thread_postcreate,
      ROCPROFILER_LIBRARY | ROCPROFILER_HSA_LIBRARY | ROCPROFILER_HIP_LIBRARY |
          ROCPROFILER_MARKER_LIBRARY,
      static_cast<void*>(client_tool_data));

  // create configure data
  static auto cfg = rocprofiler_tool_configure_result_t{
      sizeof(rocprofiler_tool_configure_result_t),
      &dftracer::HIPFunction::tool_init, &dftracer::HIPFunction::tool_fini,
      static_cast<void*>(client_tool_data)};

  // return pointer to configure data
  return &cfg;
}

}  // namespace conf

template <>
std::shared_ptr<dftracer::HIPFunction>
    dftracer::Singleton<dftracer::HIPFunction>::instance = nullptr;
template <>
bool dftracer::Singleton<dftracer::HIPFunction>::stop_creating_instances =
    false;
namespace dftracer {

TimeResolution HIPFunction::transform_time(rocprofiler_timestamp_t end_time,
                                           rocprofiler_timestamp_t start_time) {
  // Convert from nanoseconds to microseconds
  // Convert to float and use floor
  return std::floor(end_time / 1000.0) - std::floor(start_time / 1000.0);
}

TimeResolution HIPFunction::transform_timestamp(
    rocprofiler_timestamp_t timestamp) {
  // Timestamp refers to number of nanoseconds since last system restart
  if (time_diff == 0) {
    // I removed the if statement and tested that the time_diff remains the same
    // across all calls max variation = 1 microsecond
    // This means that rocprofiler_get_timestamp is consistent across calls ->
    // is in sync with logger->get_time()
    rocprofiler_timestamp_t roctime;
    rocprofiler_get_timestamp(&roctime);
    time_diff = logger->get_time() - std::floor(roctime / 1000.0);
  }
  // roctime and get_time point to the current time - we are transforming the
  // timestamp from the rocm timeline to the dftracer timeline
  TimeResolution start_time = std::floor(timestamp / 1000.0) + time_diff;
  // Convert to absolute timestamp
  // System restart time
  return start_time;
}
void HIPFunction::tool_code_object_callback(
    rocprofiler_callback_tracing_record_t record,
    rocprofiler_user_data_t* user_data, void* callback_data) {
  DFTRACER_LOG_DEBUG("HIPFunction::tool_code_object_callback");
  auto function = dftracer::Singleton<dftracer::HIPFunction>::get_instance();
  if (record.kind == ROCPROFILER_CALLBACK_TRACING_CODE_OBJECT &&
      record.operation == ROCPROFILER_CODE_OBJECT_LOAD) {
    if (record.phase == ROCPROFILER_CALLBACK_PHASE_UNLOAD) {
      // flush the buffer to ensure that any lookups for the client kernel names
      // for the code object are completed
      auto flush_status = rocprofiler_flush_buffer(function->client_buffer);
      if (flush_status != ROCPROFILER_STATUS_ERROR_BUFFER_BUSY)
        DFTRACER_LOG_ERROR(
            "HIPFunction::tool_code_object_callback flush failed status: %d",
            flush_status);
    }
  } else if (record.kind == ROCPROFILER_CALLBACK_TRACING_CODE_OBJECT &&
             record.operation ==
                 ROCPROFILER_CODE_OBJECT_DEVICE_KERNEL_SYMBOL_REGISTER) {
    auto* data = static_cast<kernel_symbol_data_t*>(record.payload);
    if (record.phase == ROCPROFILER_CALLBACK_PHASE_LOAD) {
      function->client_kernels.emplace(data->kernel_id, *data);
    } else if (record.phase == ROCPROFILER_CALLBACK_PHASE_UNLOAD) {
      function->client_kernels.erase(data->kernel_id);
    }
  }

  (void)user_data;
  (void)callback_data;
}

// This is the callback that is called with multiple recorded tracing events
// The events are inside the headers, all the different APIS funnel to the same
// buffer, due to the definition in the tool_init function Each record
// corresponds to a specific API call - Disable APIS in tool_init, do
// not edit this to disable APIS
void HIPFunction::tool_tracing_callback(rocprofiler_context_id_t context,
                                        rocprofiler_buffer_id_t buffer_id,
                                        rocprofiler_record_header_t** headers,
                                        size_t num_headers, void* user_data,
                                        uint64_t drop_count) {
  DFTRACER_LOG_DEBUG("HIPFunction::tool_tracing_callback");
  auto function = dftracer::Singleton<dftracer::HIPFunction>::get_instance();
  auto client_name_info = function->client_name_info;
  assert(user_data != nullptr);
  assert(drop_count == 0 && "drop count should be zero for lossless policy");

  if (num_headers == 0)
    return;  // No headers to process, just return
  else if (headers == nullptr)
    return;  // No headers to process, just return

  for (size_t i = 0; i < num_headers; ++i) {
    auto* header = headers[i];

    auto kind_name = std::string{};
    if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING) {
      const char* _name = nullptr;
      auto _kind = static_cast<rocprofiler_buffer_tracing_kind_t>(header->kind);
      rocprofiler_query_buffer_tracing_kind_name(_kind, &_name, nullptr);

      if (_name) {
        kind_name = std::string{_name};
      }
    }

    if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
        (header->kind == ROCPROFILER_BUFFER_TRACING_HSA_CORE_API ||
         header->kind == ROCPROFILER_BUFFER_TRACING_HSA_AMD_EXT_API ||
         header->kind == ROCPROFILER_BUFFER_TRACING_HSA_IMAGE_EXT_API ||
         header->kind == ROCPROFILER_BUFFER_TRACING_HSA_FINALIZE_EXT_API)) {
      auto* record = static_cast<rocprofiler_buffer_tracing_hsa_api_record_t*>(
          header->payload);

      // Create metadata for HSA API calls
      auto metadata = new Metadata();
      metadata->insert_or_assign("context", context.handle);
      metadata->insert_or_assign("buffer_id", buffer_id.handle);
      metadata->insert_or_assign("extern_cid",
                                 record->correlation_id.external.value);
      metadata->insert_or_assign("kind", record->kind);
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("tid", record->thread_id);

      std::string event_name =
          std::string(client_name_info[record->kind][record->operation]);

      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(),
          function->transform_timestamp(record->start_timestamp),
          function->transform_time(record->end_timestamp,
                                   record->start_timestamp),
          metadata);
      function->logger->exit_event();

    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind == ROCPROFILER_BUFFER_TRACING_HIP_RUNTIME_API) {
      auto* record = static_cast<rocprofiler_buffer_tracing_hip_api_record_t*>(
          header->payload);

      // Create metadata for HIP API calls
      auto metadata = new Metadata();
      metadata->insert_or_assign("context", context.handle);
      metadata->insert_or_assign("buffer_id", buffer_id.handle);
      metadata->insert_or_assign("extern_cid",
                                 record->correlation_id.external.value);
      metadata->insert_or_assign("kind", record->kind);
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("tid", record->thread_id);

      std::string event_name =
          std::string(client_name_info[record->kind][record->operation]);
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(),
          function->transform_timestamp(record->start_timestamp),
          function->transform_time(record->end_timestamp,
                                   record->start_timestamp),
          metadata);
      function->logger->exit_event();

    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind == ROCPROFILER_BUFFER_TRACING_KERNEL_DISPATCH) {
      auto* record =
          static_cast<rocprofiler_buffer_tracing_kernel_dispatch_record_t*>(
              header->payload);

      // Create metadata for kernel dispatch
      auto metadata = new Metadata();
      metadata->insert_or_assign("tid", record->thread_id);
      metadata->insert_or_assign("correlation_id",
                                 record->correlation_id.external.value);
      // Instead of long names
      std::string event_name = std::string(
          function->client_kernels.at(record->dispatch_info.kernel_id)
              .kernel_name);

      // Resize to max length
      if (event_name.length() > MAX_EVENT_NAME_LENGTH) {
        event_name = event_name.substr(0, MAX_EVENT_NAME_LENGTH);
      }
      // Prepend with string of kernel_id
      event_name = std::to_string(record->dispatch_info.kernel_id) + event_name;
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(),
          function->transform_timestamp(record->start_timestamp),
          function->transform_time(record->end_timestamp,
                                   record->start_timestamp),
          metadata);
      function->logger->exit_event();

    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind == ROCPROFILER_BUFFER_TRACING_MEMORY_COPY) {
      auto* record =
          static_cast<rocprofiler_buffer_tracing_memory_copy_record_t*>(
              header->payload);

      // Create metadata for memory copy
      auto metadata = new Metadata();
      metadata->insert_or_assign("context", context.handle);
      metadata->insert_or_assign("buffer_id", buffer_id.handle);
      metadata->insert_or_assign("extern_cid",
                                 record->correlation_id.external.value);
      metadata->insert_or_assign("kind", record->kind);
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("src_agent_id", record->src_agent_id.handle);
      metadata->insert_or_assign("dst_agent_id", record->dst_agent_id.handle);
      metadata->insert_or_assign("tid", record->thread_id);

      std::string event_name =
          std::string(client_name_info.at(record->kind, record->operation));
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(),
          function->transform_timestamp(record->start_timestamp),
          function->transform_time(record->end_timestamp,
                                   record->start_timestamp),
          metadata);
      function->logger->exit_event();

    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind == ROCPROFILER_BUFFER_TRACING_PAGE_MIGRATION) {
      auto* record =
          static_cast<rocprofiler_buffer_tracing_page_migration_record_t*>(
              header->payload);

      // Create metadata for page migration
      auto metadata = new Metadata();
      metadata->insert_or_assign("kind", record->kind);
      metadata->insert_or_assign("operation", record->operation);

      // Add operation-specific details to metadata
      switch (record->operation) {
        case ROCPROFILER_PAGE_MIGRATION_PAGE_MIGRATE: {
          // metadata->insert_or_assign("read_fault",
          //                            record->page_fault.read_fault);
          // metadata->insert_or_assign("migrated",
          // record->page_fault.migrated);
          metadata->insert_or_assign("node_id", record->page_fault.node_id);
          metadata->insert_or_assign("address", record->page_fault.address);
          break;
        }
        case ROCPROFILER_PAGE_MIGRATION_PAGE_FAULT: {
          metadata->insert_or_assign("start_addr",
                                     record->page_migrate.start_addr);
          metadata->insert_or_assign("end_addr", record->page_migrate.end_addr);
          metadata->insert_or_assign("from_node",
                                     record->page_migrate.from_node);
          metadata->insert_or_assign("to_node", record->page_migrate.to_node);
          metadata->insert_or_assign("prefetch_node",
                                     record->page_migrate.prefetch_node);
          metadata->insert_or_assign("preferred_node",
                                     record->page_migrate.preferred_node);
          metadata->insert_or_assign("trigger", record->page_migrate.trigger);
          break;
        }
        case ROCPROFILER_PAGE_MIGRATION_QUEUE_SUSPEND: {
          // metadata->insert_or_assign("rescheduled",
          //                            record->queue_suspend.rescheduled);
          metadata->insert_or_assign("node_id", record->queue_suspend.node_id);
          metadata->insert_or_assign("trigger", record->queue_suspend.trigger);
          break;
        }
        case ROCPROFILER_PAGE_MIGRATION_UNMAP_FROM_GPU: {
          metadata->insert_or_assign("node_id", record->unmap_from_gpu.node_id);
          metadata->insert_or_assign("start_addr",
                                     record->unmap_from_gpu.start_addr);
          metadata->insert_or_assign("end_addr",
                                     record->unmap_from_gpu.end_addr);
          metadata->insert_or_assign("trigger", record->unmap_from_gpu.trigger);
          break;
        }
        default:
          // DFTRACER_LOG_ERROR("unexpected page migration operation: ")
          continue;  // Skip this record if operation is unknown
      }

      // Note: page migration uses record->pid instead of getpid()
      std::string event_name =
          std::string(client_name_info.at(record->kind, record->operation));
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(),
          function->transform_timestamp(record->start_timestamp),
          function->transform_time(record->end_timestamp,
                                   record->start_timestamp),
          metadata);
      function->logger->exit_event();

    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind == ROCPROFILER_BUFFER_TRACING_SCRATCH_MEMORY) {
      auto* record =
          static_cast<rocprofiler_buffer_tracing_scratch_memory_record_t*>(
              header->payload);

      // Create metadata for scratch memory
      auto metadata = new Metadata();
      metadata->insert_or_assign("context", context.handle);
      metadata->insert_or_assign("buffer_id", buffer_id.handle);
      metadata->insert_or_assign("extern_cid",
                                 record->correlation_id.external.value);
      metadata->insert_or_assign("kind", record->kind);
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("agent_id", record->agent_id.handle);
      metadata->insert_or_assign("queue_id", record->queue_id.handle);
      metadata->insert_or_assign("flags", record->flags);
      metadata->insert_or_assign("tid", record->thread_id);
      std::string event_name =
          std::string(client_name_info.at(record->kind, record->operation));
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(),
          function->transform_timestamp(record->start_timestamp),
          function->transform_time(record->end_timestamp,
                                   record->start_timestamp),
          metadata);
      function->logger->exit_event();
    } else if (header->kind == ROCPROFILER_BUFFER_TRACING_RCCL_API) {
      auto* record = static_cast<rocprofiler_buffer_tracing_rccl_api_record_t*>(
          header->payload);

      auto metadata = new Metadata();
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("tid", record->thread_id);
      metadata->insert_or_assign("correlation_id",
                                 record->correlation_id.external.value);

      std::string event_name =
          std::string(client_name_info.at(record->kind, record->operation));
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(),
          function->transform_timestamp(record->start_timestamp),
          function->transform_time(record->end_timestamp,
                                   record->start_timestamp),
          metadata);
      function->logger->exit_event();
    } else {
      continue;  // Skip this record if category or kind is unknown
    }
  }
}

void HIPFunction::thread_precreate(rocprofiler_runtime_library_t lib,
                                   void* tool_data) {
  DFTRACER_LOG_DEBUG("internal thread about to be created by rocprofiler",
                     "lib=" + std::to_string(lib));
}

void HIPFunction::thread_postcreate(rocprofiler_runtime_library_t lib,
                                    void* tool_data) {
  DFTRACER_LOG_DEBUG("internal thread was created by rocprofiler",
                     "lib=" + std::to_string(lib));
}

// Tool initialization
// Attach callbacks to rocprofiler APIS
// callbacks are populated in buffer and processeed in tool_tracing_callback
// Disable APIS by commenting out the corresponding lines
// TODO: Enable/Disable specific APIs using ENV variables
int HIPFunction::tool_init(rocprofiler_client_finalize_t fini_func,
                           void* tool_data) {
  DFTRACER_LOG_DEBUG("HIP Intercept class initialized");
  auto function = dftracer::Singleton<dftracer::HIPFunction>::get_instance();
  function->client_ctx = {0};
  function->client_name_info = rocprofiler::sdk::get_buffer_tracing_names();

  rocprofiler_status_t status =
      rocprofiler_create_context(&function->client_ctx);
  if (status != ROCPROFILER_STATUS_SUCCESS) {
    DFTRACER_LOG_ERROR("HIP Intercept context creation failed: status, %d\n",
                       status);
    return -1;
  }

  auto code_object_ops = std::vector<rocprofiler_tracing_operation_t>{
      ROCPROFILER_CODE_OBJECT_DEVICE_KERNEL_SYMBOL_REGISTER};

  rocprofiler_configure_callback_tracing_service(
      function->client_ctx, ROCPROFILER_CALLBACK_TRACING_CODE_OBJECT,
      code_object_ops.data(), code_object_ops.size(), tool_code_object_callback,
      nullptr);
  constexpr auto buffer_size_bytes = 4096;
  constexpr auto buffer_watermark_bytes =
      buffer_size_bytes - (buffer_size_bytes / 8);

  rocprofiler_create_buffer(function->client_ctx, buffer_size_bytes,
                            buffer_watermark_bytes,
                            ROCPROFILER_BUFFER_POLICY_LOSSLESS,
                            dftracer::HIPFunction::tool_tracing_callback,
                            tool_data, &function->client_buffer);

  // Disabled HSA APIs
  // for (auto itr : {ROCPROFILER_BUFFER_TRACING_HSA_CORE_API,
  //                  ROCPROFILER_BUFFER_TRACING_HSA_AMD_EXT_API}) {
  //   rocprofiler_configure_buffer_tracing_service(function->client_ctx, itr,
  //   nullptr, 0,
  //                                                function->client_buffer);
  // }

  rocprofiler_configure_buffer_tracing_service(
      function->client_ctx, ROCPROFILER_BUFFER_TRACING_HIP_RUNTIME_API, nullptr,
      0, function->client_buffer);

  rocprofiler_configure_buffer_tracing_service(
      function->client_ctx, ROCPROFILER_BUFFER_TRACING_KERNEL_DISPATCH, nullptr,
      0, function->client_buffer);

  rocprofiler_configure_buffer_tracing_service(
      function->client_ctx, ROCPROFILER_BUFFER_TRACING_MEMORY_COPY, nullptr, 0,
      function->client_buffer);

  rocprofiler_configure_buffer_tracing_service(
      function->client_ctx, ROCPROFILER_BUFFER_TRACING_PAGE_MIGRATION, nullptr,
      0, function->client_buffer);

  rocprofiler_configure_buffer_tracing_service(
      function->client_ctx, ROCPROFILER_BUFFER_TRACING_SCRATCH_MEMORY, nullptr,
      0, function->client_buffer);

  // RCCL tracing
  rocprofiler_configure_buffer_tracing_service(
      function->client_ctx, ROCPROFILER_BUFFER_TRACING_RCCL_API, nullptr, 0,
      function->client_buffer);

  auto client_thread = rocprofiler_callback_thread_t{};
  rocprofiler_create_callback_thread(&client_thread);

  rocprofiler_assign_callback_thread(function->client_buffer, client_thread);

  int valid_ctx = 0;
  rocprofiler_context_is_valid(function->client_ctx, &valid_ctx);

  if (valid_ctx == 0) {
    // notify rocprofiler that initialization failed
    // and all the contexts, buffers, etc. created
    // should be ignored
    //   throw std::runtime_error("HIP Intercept initialization failed");
    DFTRACER_LOG_DEBUG("HIP Intercept initialization failed");
    return -1;
  }

  status = rocprofiler_start_context(function->client_ctx);
  if (status != ROCPROFILER_STATUS_SUCCESS) {
    DFTRACER_LOG_ERROR("HIP Intercept context start failed: status, %d\n",
                       status);
    return -1;
  }
  return 0;
}

void HIPFunction::tool_fini(void* tool_data) {
  DFTRACER_LOG_DEBUG("HIP Intercept class finalized");
  return;
}

}  // namespace dftracer

#endif