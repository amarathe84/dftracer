//
// Created by haridev on 10/21/23.
//

#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/logging.h>
#include <fcntl.h>
#include <math.h>
#include <mpi.h>
#include <unistd.h>
#include <util.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

int main(int argc, char* argv[]) {
  MPI_Init(&argc, &argv);
  init_log();
  int my_rank, comm_size;
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  uint64_t num_operations = 1024;
  uint64_t transfer_size = 4096;
  bool use_distribution = false;
  char filename[4096], filename_primary[4096];
  if (argc < 4) {
    DFTRACER_LOG_ERROR(
        "usage: overhead FILENAME <NUM OPERATIONS> <TRANSFER SIZE> "
        "[--distribution]",
        "");
    exit(1);
  }
  fs::create_directories(argv[2]);
  sprintf(filename, "%s/file_%d-%d.bat", argv[2], my_rank, comm_size);
  num_operations = strtoll(argv[3], NULL, 10);
  transfer_size = strtoll(argv[4], NULL, 10);

  if (argc > 5 && std::string(argv[5]) == "--distribution") {
    use_distribution = true;
  }

  MPI_Barrier(MPI_COMM_WORLD);
  if (my_rank == 0) {
    DFTRACER_LOG_INFO(
        "Running with transfer size %lu  and num ops %lu (distribution: %s)",
        transfer_size, num_operations, use_distribution ? "yes" : "no");
  }
  if (num_operations > std::numeric_limits<uint64_t>::max() / transfer_size) {
    DFTRACER_LOG_ERROR("Requested workload too large: num_ops=%lu ts=%lu",
                       num_operations, transfer_size);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  uint64_t file_size = num_operations * transfer_size;
  size_t ts = static_cast<size_t>((file_size + comm_size - 1) /
                                  comm_size);  // ceil division
  if (ts == 0) {
    DFTRACER_LOG_ERROR("Per-rank size is zero after division", 0);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  if (my_rank == 0) {
    DFTRACER_LOG_INFO("Writing %zu per rank for data generation", ts);
  }
  sprintf(filename_primary, "%s/file_0-%d.bat", argv[2], comm_size);
  {
    MPI_File fh_orig;
    int status_orig =
        MPI_File_open(MPI_COMM_WORLD, filename_primary,
                      MPI_MODE_RDWR | MPI_MODE_CREATE, MPI_INFO_NULL, &fh_orig);
    assert(status_orig == 0);
    MPI_Offset base_offset = static_cast<MPI_Offset>(ts) * my_rank;
    assert(base_offset >= 0);
    const size_t chunk_cap = std::min<size_t>(
        4 * 1024 * 1024, ts);  // 4MB chunks to avoid huge buffers
    std::vector<char> write_data(chunk_cap, 'w');
    size_t remaining = ts;
    size_t offset_bytes = 0;
    while (remaining > 0) {
      int this_chunk = static_cast<int>(std::min(chunk_cap, remaining));
      MPI_Status stat_orig;
      auto ret_orig = MPI_File_write_at(
          fh_orig, base_offset + static_cast<MPI_Offset>(offset_bytes),
          write_data.data(), this_chunk, MPI_CHAR, &stat_orig);
      assert(ret_orig == 0);
      int written_bytes;
      MPI_Get_count(&stat_orig, MPI_CHAR, &written_bytes);
      if (written_bytes != this_chunk) {
        DFTRACER_LOG_ERROR("Write was unsuccessful written %d of %d bytes",
                           written_bytes, this_chunk);
      }
      assert(written_bytes == this_chunk);
      remaining -= static_cast<size_t>(written_bytes);
      offset_bytes += static_cast<size_t>(written_bytes);
    }
    status_orig = MPI_File_close(&fh_orig);
  }
  if (my_rank != 0) {
    std::string cmd =
        "cp " + std::string(filename_primary) + " " + std::string(filename);
    int status = system(cmd.c_str());
    assert(status != -1);
  }
  assert(fs::file_size(filename) >= file_size);
  MPI_Barrier(MPI_COMM_WORLD);
  if (my_rank == 0) {
    DFTRACER_LOG_INFO("Created dataset for test", "");
  }

  // Setup random distribution if enabled
  std::random_device rd;
  std::mt19937 gen(rd() + my_rank);
  std::vector<uint64_t> operation_sizes;

  if (use_distribution) {
    // Create a mixed distribution: 80% small ops (<64K), 20% large ops (>=64K)
    uint64_t small_ops = (num_operations * 80) / 100;
    uint64_t large_ops = num_operations - small_ops;

    std::uniform_int_distribution<uint64_t> small_dist(1024, 4096);
    std::uniform_int_distribution<uint64_t> large_dist(65536, transfer_size);

    for (uint64_t i = 0; i < small_ops; ++i) {
      operation_sizes.push_back(small_dist(gen));
    }
    for (uint64_t i = 0; i < large_ops; ++i) {
      operation_sizes.push_back(large_dist(gen));
    }
    // Shuffle the operations
    std::shuffle(operation_sizes.begin(), operation_sizes.end(), gen);
  }

  char* buf = (char*)malloc(transfer_size);
  std::vector<double> iteration_times;
  const int num_iterations = 25;

  for (int iter = 0; iter < num_iterations; ++iter) {
    MPI_Barrier(MPI_COMM_WORLD);
    if (my_rank == 0) {
      DFTRACER_LOG_INFO("Starting iteration %d", iter);
    }

    // Create per-iteration filename by copying base shared file
    char iter_filename[4096];
    sprintf(iter_filename, "%s/file_%d-%d_iter%d.bat", argv[2], my_rank,
            comm_size, iter);
    std::string cmd = "cp " + std::string(filename_primary) + " " +
                      std::string(iter_filename);
    int cp_status = system(cmd.c_str());
    assert(cp_status == 0);

    Timer operation_timer;
    operation_timer.resumeTime();
    int fd = open(iter_filename, O_RDONLY);
    operation_timer.pauseTime();
    assert(fd != -1);

    for (uint64_t i = 0; i < num_operations; ++i) {
      uint64_t read_size =
          use_distribution ? operation_sizes[i] : transfer_size;

      operation_timer.resumeTime();
      ssize_t read_bytes = read(fd, buf, read_size);
      operation_timer.pauseTime();
      assert(read_bytes > 0);
      assert(read_size == (uint64_t)read_bytes);
      MPI_Barrier(MPI_COMM_WORLD);
      if (i % 10000 == 0 && my_rank == 0) {
        DFTRACER_LOG_INFO("Completed %d loops", i);
      }
    }
    operation_timer.resumeTime();
    int status = close(fd);
    operation_timer.pauseTime();
    assert(status == 0);

    MPI_Barrier(MPI_COMM_WORLD);
    double elapsed_time = operation_timer.getElapsedTime();
    double total_time;
    MPI_Reduce(&elapsed_time, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0,
               MPI_COMM_WORLD);
    if (my_rank == 0) {
      iteration_times.push_back(total_time);
      DFTRACER_LOG_PRINT("Iteration %d time: %f", iter, total_time);
    }

    // Clean up iteration file to avoid caching
    if (fs::exists(iter_filename)) {
      fs::remove(iter_filename);
    }
  }
  free(buf);

  if (my_rank == 0) {
    // Sort for min, max, median, percentiles
    std::vector<double> sorted_times = iteration_times;
    std::sort(sorted_times.begin(), sorted_times.end());

    double min_time = sorted_times.front();
    double max_time = sorted_times.back();

    // Calculate median
    double median_time;
    if (num_iterations % 2 == 0) {
      median_time = (sorted_times[num_iterations / 2 - 1] +
                     sorted_times[num_iterations / 2]) /
                    2.0;
    } else {
      median_time = sorted_times[num_iterations / 2];
    }

    // Calculate 25th percentile (Q1)
    int p25_idx = (num_iterations - 1) * 25 / 100;
    double p25_time = sorted_times[p25_idx];

    // Calculate 75th percentile (Q3)
    int p75_idx = (num_iterations - 1) * 75 / 100;
    double p75_time = sorted_times[p75_idx];

    // Calculate mean and std_dev for values within [p25, p75]
    double sum_within = 0.0;
    int count_within = 0;
    for (int i = p25_idx; i <= p75_idx; ++i) {
      sum_within += sorted_times[i];
      count_within++;
    }
    double mean_within = sum_within / count_within;

    double variance_within = 0.0;
    for (int i = p25_idx; i <= p75_idx; ++i) {
      variance_within +=
          (sorted_times[i] - mean_within) * (sorted_times[i] - mean_within);
    }
    double std_dev_within = std::sqrt(variance_within / count_within);

    // Get log file stats if DFTRACER_LOG_FILE is set
    double size_mb = 0.0;
    long num_events = 0;
    const char* log_file = getenv("DFTRACER_LOG_FILE");
    if (log_file) {
      std::string cmd = "find $(dirname " + std::string(log_file) +
                        ") -maxdepth 1 -name \"$(basename " +
                        std::string(log_file) +
                        ")*\" -type f -exec du -c {} + 2>/dev/null | tail -1 | "
                        "awk '{print $1}'";
      FILE* pipe = popen(cmd.c_str(), "r");
      if (pipe) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
          size_mb = std::atoll(buffer) / 1024.0;
        }
        pclose(pipe);
      }

      cmd = "find $(dirname " + std::string(log_file) +
            ") -maxdepth 1 -name \"$(basename " + std::string(log_file) +
            ")*\" -type f -exec sh -c 'zcat {} 2>/dev/null || cat {}' \\; "
            "2>/dev/null | wc -l";
      pipe = popen(cmd.c_str(), "r");
      if (pipe) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
          num_events = std::atoll(buffer);
        }
        pclose(pipe);
      }
    }

    DFTRACER_LOG_INFO("Finishing all iterations", "");
    printf(
        "scale,ops,ts,min,p25,median,p75,max,mean,std_dev,distribution,size_mb,"
        "num_events\n%d,%lu,"
        "%lu,%f,%f,%f,%f,%f,%f,%f,%s,%f,%ld\n",
        comm_size, num_operations, transfer_size, min_time, p25_time,
        median_time, p75_time, max_time, mean_within, std_dev_within,
        use_distribution ? "yes" : "no", size_mb, num_events);
  }
  MPI_Barrier(MPI_COMM_WORLD);
  if (fs::exists(filename)) fs::remove(filename);
  MPI_Finalize();
  return 0;
}