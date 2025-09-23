#include "../aggregation.h"
#include "../../core/singleton.h"
#include <iostream>
#include <cassert>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <any>
#include <vector>
#include <cmath>

/**
 * Unit Test: File Cache Miss Rate Calculation
 * 
 * This test demonstrates how to use the aggregation logic to calculate
 * file cache miss rate as: count(access() calls) / count(unique_files_accessed)
 * 
 * The test aggregates:
 * 1. Total access() system calls per process/thread/category/time_window
 * 2. Unique files accessed per process/thread/category/time_window
 * 3. Derives cache miss rate from aggregated data
 * 
 * Test scenarios:
 * 1. Single process/thread accessing multiple files
 * 2. Multiple processes accessing overlapping files
 * 3. Time-windowed cache miss rate analysis
 * 4. Category-based cache miss rate analysis
 * 5. Query-based metric extraction for specific keys
 */

namespace dftracer {
namespace test {

class FileCacheMissRateTest {
public:
    static bool run_all_tests() {
        std::cout << "=== File Cache Miss Rate Unit Tests ===" << std::endl;
        
        bool all_passed = true;
        all_passed &= test_basic_cache_miss_rate();
        all_passed &= test_multiple_processes_cache_rate();
        all_passed &= test_time_windowed_cache_analysis();
        all_passed &= test_category_based_cache_analysis();
        all_passed &= test_query_based_metric_extraction();
        all_passed &= test_realistic_workload_simulation();
        
        if (all_passed) {
            std::cout << "All file cache miss rate tests PASSED" << std::endl;
        } else {
            std::cout << "Some file cache miss rate tests FAILED" << std::endl;
        }
        
        return all_passed;
    }

private:
    // Helper struct to store cache metrics
    struct CacheMetrics {
        int total_access_calls = 0;
        int unique_files_accessed = 0;
        double cache_miss_rate = 0.0;
        
        void calculate_miss_rate() {
            if (unique_files_accessed > 0) {
                cache_miss_rate = static_cast<double>(total_access_calls) / unique_files_accessed;
            }
        }
    };
    
    // Helper function to extract cache metrics for a specific aggregation key
    static CacheMetrics extract_cache_metrics(
        const std::unordered_map<AggregationKey, AggregationData, AggregationKeyHash>& aggregated_data,
        const AggregationKey& target_key) {
        
        CacheMetrics metrics;
        
        auto it = aggregated_data.find(target_key);
        if (it != aggregated_data.end()) {
            const AggregationData& data = it->second;
            
            // Extract total access calls
            metrics.total_access_calls = data.count;
            
            // Extract unique files count from metadata
            auto unique_files_it = data.aggregated_metadata.find("unique_files_count");
            if (unique_files_it != data.aggregated_metadata.end()) {
                metrics.unique_files_accessed = std::any_cast<int>(unique_files_it->second);
            }
            
            metrics.calculate_miss_rate();
        }
        
        return metrics;
    }
    
    static bool test_basic_cache_miss_rate() {
        std::cout << "Testing basic file cache miss rate calculation..." << std::endl;
        
        auto manager = dftracer::Singleton<dftracer::AggregationManager>::get_instance();
        
        char hostname_hash[] = "cache_miss_hash";
        int result = manager->initialize("test_cache_miss_rate.json", hostname_hash);
        assert(result == 0);
        
        AggregationKey key = {
            .node_id = "cache_test_node",
            .process_id = 1000,
            .thread_id = 2000,
            .category = "file_access",
            .start_time = 1000000
        };
        
        // Simulate file access pattern:
        // - 10 access() calls to file1.txt (repeated accesses)
        // - 5 access() calls to file2.txt
        // - 3 access() calls to file3.txt
        // Total: 18 access() calls, 3 unique files
        // Expected cache miss rate: 18/3 = 6.0
        
        std::vector<std::string> files = {"file1.txt", "file2.txt", "file3.txt"};
        std::vector<int> access_counts = {10, 5, 3};
        std::unordered_set<std::string> unique_files;
        int total_accesses = 0;
        
        for (size_t i = 0; i < files.size(); ++i) {
            unique_files.insert(files[i]);
            for (int j = 0; j < access_counts[i]; ++j) {
                std::unordered_map<std::string, std::any> metadata;
                metadata["filename"] = files[i];
                metadata["access_type"] = std::string("read");
                metadata["file_size"] = static_cast<unsigned long long>(1024 * (i + 1));
                
                // Each access() call is aggregated as a data event
                manager->aggregate_data_event(key, "access", 100 + j, &metadata);
                total_accesses++;
            }
        }
        
        // Add unique files count as a separate metadata event
        manager->aggregate_metadata_event(key, "file_access", "unique_files_count", 
                                          std::to_string(unique_files.size()).c_str(), false);
        
        auto aggregated = manager->get_aggregated_data();
        assert(aggregated.size() == 1);
        
        // Extract cache metrics
        CacheMetrics metrics = extract_cache_metrics(aggregated, key);
        
        std::cout << "  Total access() calls: " << metrics.total_access_calls << std::endl;
        std::cout << "  Unique files accessed: " << metrics.unique_files_accessed << std::endl;
        std::cout << "  Cache miss rate: " << metrics.cache_miss_rate << std::endl;
        
        // Verify calculations
        assert(metrics.total_access_calls == total_accesses);
        assert(metrics.unique_files_accessed == static_cast<int>(unique_files.size()));
        assert(std::abs(metrics.cache_miss_rate - 6.0) < 0.001);
        
        manager->finalize(0, true);
        
        std::cout << "  Basic cache miss rate test passed" << std::endl;
        return true;
    }
    
    static bool test_multiple_processes_cache_rate() {
        std::cout << "Testing cache miss rate across multiple processes..." << std::endl;
        
        auto manager = dftracer::Singleton<dftracer::AggregationManager>::get_instance();
        
        char hostname_hash[] = "multi_proc_cache_hash";
        int result = manager->initialize("test_multi_proc_cache.json", hostname_hash);
        assert(result == 0);
        
        // Test 3 different processes accessing files
        std::vector<ProcessID> processes = {1000, 1001, 1002};
        std::unordered_map<ProcessID, CacheMetrics> expected_metrics;
        
        for (ProcessID proc_id : processes) {
            AggregationKey key = {
                .node_id = "multi_proc_node",
                .process_id = proc_id,
                .thread_id = 2000,
                .category = "file_access",
                .start_time = 1000000
            };
            
            // Each process has different access patterns
            int base_accesses = 5 + (proc_id - 1000) * 3;  // 5, 8, 11 accesses
            int unique_files = 2 + (proc_id - 1000);       // 2, 3, 4 unique files
            
            // Simulate access pattern
            for (int file_idx = 0; file_idx < unique_files; ++file_idx) {
                std::string filename = "proc_" + std::to_string(proc_id) + "_file_" + std::to_string(file_idx) + ".txt";
                
                int accesses_per_file = base_accesses / unique_files + (file_idx == 0 ? base_accesses % unique_files : 0);
                
                for (int access = 0; access < accesses_per_file; ++access) {
                    std::unordered_map<std::string, std::any> metadata;
                    metadata["filename"] = filename;
                    metadata["process_id"] = static_cast<long long>(proc_id);
                    
                    manager->aggregate_data_event(key, "access", 100 + access, &metadata);
                }
            }
            
            // Add unique files count
            manager->aggregate_metadata_event(key, "file_access", "unique_files_count", 
                                              std::to_string(unique_files).c_str(), false);
            
            // Store expected metrics
            expected_metrics[proc_id].total_access_calls = base_accesses;
            expected_metrics[proc_id].unique_files_accessed = unique_files;
            expected_metrics[proc_id].calculate_miss_rate();
        }
        
        auto aggregated = manager->get_aggregated_data();
        assert(aggregated.size() == processes.size());
        
        // Verify each process's cache metrics
        for (ProcessID proc_id : processes) {
            AggregationKey query_key = {
                .node_id = "multi_proc_node",
                .process_id = proc_id,
                .thread_id = 2000,
                .category = "file_access",
                .start_time = 1000000
            };
            
            CacheMetrics actual_metrics = extract_cache_metrics(aggregated, query_key);
            CacheMetrics expected = expected_metrics[proc_id];
            
            std::cout << "  Process " << proc_id << ": " 
                      << actual_metrics.total_access_calls << " accesses, "
                      << actual_metrics.unique_files_accessed << " unique files, "
                      << "miss rate: " << actual_metrics.cache_miss_rate << std::endl;
            
            assert(actual_metrics.total_access_calls == expected.total_access_calls);
            assert(actual_metrics.unique_files_accessed == expected.unique_files_accessed);
            assert(std::abs(actual_metrics.cache_miss_rate - expected.cache_miss_rate) < 0.001);
        }
        
        manager->finalize(0, true);
        
        std::cout << "  Multiple processes cache rate test passed" << std::endl;
        return true;
    }
    
    static bool test_time_windowed_cache_analysis() {
        std::cout << "Testing time-windowed cache miss rate analysis..." << std::endl;
        
        auto manager = dftracer::Singleton<dftracer::AggregationManager>::get_instance();
        
        char hostname_hash[] = "time_window_cache_hash";
        int result = manager->initialize("test_time_window_cache.json", hostname_hash);
        assert(result == 0);
        
        // Test cache behavior across 4 time windows
        const uint64_t window_size = 1000000;  // 1 second windows
        const int num_windows = 4;
        
        std::vector<CacheMetrics> expected_window_metrics(num_windows);
        
        for (int window = 0; window < num_windows; ++window) {
            uint64_t window_start = window * window_size;
            
            AggregationKey key = {
                .node_id = "time_window_node",
                .process_id = 1000,
                .thread_id = 2000,
                .category = "file_access",
                .start_time = window_start
            };
            
            // Each time window has different cache behavior
            int accesses_in_window = 15 + window * 5;  // 15, 20, 25, 30 accesses
            int unique_files_in_window = 3 + window;   // 3, 4, 5, 6 unique files
            
            // Simulate file accesses in this time window
            for (int file_idx = 0; file_idx < unique_files_in_window; ++file_idx) {
                std::string filename = "window_" + std::to_string(window) + "_file_" + std::to_string(file_idx) + ".txt";
                
                int accesses_per_file = accesses_in_window / unique_files_in_window;
                if (file_idx == 0) {
                    accesses_per_file += accesses_in_window % unique_files_in_window;
                }
                
                for (int access = 0; access < accesses_per_file; ++access) {
                    std::unordered_map<std::string, std::any> metadata;
                    metadata["filename"] = filename;
                    metadata["window_id"] = static_cast<long long>(window);
                    metadata["timestamp"] = static_cast<unsigned long long>(window_start + access * 1000);
                    
                    manager->aggregate_data_event(key, "access", 50 + access, &metadata);
                }
            }
            
            // Add unique files count for this window
            manager->aggregate_metadata_event(key, "file_access", "unique_files_count", 
                                             std::to_string(unique_files_in_window).c_str(), false);
            
            // Store expected metrics
            expected_window_metrics[window].total_access_calls = accesses_in_window;
            expected_window_metrics[window].unique_files_accessed = unique_files_in_window;
            expected_window_metrics[window].calculate_miss_rate();
        }
        
        auto aggregated = manager->get_aggregated_data();
        assert(aggregated.size() == num_windows);
        
        std::cout << "  Time Window Cache Analysis:" << std::endl;
        
        // Verify each time window's cache metrics
        for (int window = 0; window < num_windows; ++window) {
            uint64_t window_start = window * window_size;
            
            AggregationKey query_key = {
                .node_id = "time_window_node",
                .process_id = 1000,
                .thread_id = 2000,
                .category = "file_access",
                .start_time = window_start
            };
            
            CacheMetrics actual_metrics = extract_cache_metrics(aggregated, query_key);
            CacheMetrics expected = expected_window_metrics[window];
            
            std::cout << "    Window " << window << ": " 
                      << actual_metrics.total_access_calls << " accesses, "
                      << actual_metrics.unique_files_accessed << " unique files, "
                      << "miss rate: " << actual_metrics.cache_miss_rate << std::endl;
            
            assert(actual_metrics.total_access_calls == expected.total_access_calls);
            assert(actual_metrics.unique_files_accessed == expected.unique_files_accessed);
            assert(std::abs(actual_metrics.cache_miss_rate - expected.cache_miss_rate) < 0.001);
        }
        
        manager->finalize(0, true);
        
        std::cout << "  Time-windowed cache analysis test passed" << std::endl;
        return true;
    }
    
    static bool test_category_based_cache_analysis() {
        std::cout << "Testing category-based cache miss rate analysis..." << std::endl;
        
        auto manager = dftracer::Singleton<dftracer::AggregationManager>::get_instance();
        
        char hostname_hash[] = "category_cache_hash";
        int result = manager->initialize("test_category_cache.json", hostname_hash);
        assert(result == 0);
        
        // Test different categories of file access
        std::vector<std::string> categories = {"data_files", "config_files", "log_files", "temp_files"};
        std::unordered_map<std::string, CacheMetrics> expected_category_metrics;
        
        for (size_t cat_idx = 0; cat_idx < categories.size(); ++cat_idx) {
            const std::string& category = categories[cat_idx];
            
            AggregationKey key = {
                .node_id = "category_node",
                .process_id = 1000,
                .thread_id = 2000,
                .category = category,
                .start_time = 1000000
            };
            
            // Each category has different access patterns
            int accesses = 12 + cat_idx * 8;  // 12, 20, 28, 36 accesses
            int unique_files = 2 + cat_idx;   // 2, 3, 4, 5 unique files
            
            // Simulate category-specific file access patterns
            for (int file_idx = 0; file_idx < unique_files; ++file_idx) {
                std::string filename = category + "_" + std::to_string(file_idx);
                
                int accesses_per_file = accesses / unique_files;
                if (file_idx == 0) {
                    accesses_per_file += accesses % unique_files;
                }
                
                for (int access = 0; access < accesses_per_file; ++access) {
                    std::unordered_map<std::string, std::any> metadata;
                    metadata["filename"] = filename;
                    metadata["category"] = category;
                    metadata["file_type"] = category.substr(0, category.find("_"));
                    
                    manager->aggregate_data_event(key, "access", 75 + access, &metadata);
                }
            }
            
            // Add unique files count for this category
            manager->aggregate_metadata_event(key, "file_access", "unique_files_count", 
                                             std::to_string(unique_files).c_str(), false);
            
            // Store expected metrics
            expected_category_metrics[category].total_access_calls = accesses;
            expected_category_metrics[category].unique_files_accessed = unique_files;
            expected_category_metrics[category].calculate_miss_rate();
        }
        
        auto aggregated = manager->get_aggregated_data();
        assert(aggregated.size() == categories.size());
        
        std::cout << "  Category-based Cache Analysis:" << std::endl;
        
        // Verify each category's cache metrics
        for (const std::string& category : categories) {
            AggregationKey query_key = {
                .node_id = "category_node",
                .process_id = 1000,
                .thread_id = 2000,
                .category = category,
                .start_time = 1000000
            };
            
            CacheMetrics actual_metrics = extract_cache_metrics(aggregated, query_key);
            CacheMetrics expected = expected_category_metrics[category];
            
            std::cout << "    " << category << ": " 
                      << actual_metrics.total_access_calls << " accesses, "
                      << actual_metrics.unique_files_accessed << " unique files, "
                      << "miss rate: " << actual_metrics.cache_miss_rate << std::endl;
            
            assert(actual_metrics.total_access_calls == expected.total_access_calls);
            assert(actual_metrics.unique_files_accessed == expected.unique_files_accessed);
            assert(std::abs(actual_metrics.cache_miss_rate - expected.cache_miss_rate) < 0.001);
        }
        
        manager->finalize(0, true);
        
        std::cout << "  Category-based cache analysis test passed" << std::endl;
        return true;
    }
    
    static bool test_query_based_metric_extraction() {
        std::cout << "Testing query-based cache metric extraction..." << std::endl;
        
        auto manager = dftracer::Singleton<dftracer::AggregationManager>::get_instance();
        
        char hostname_hash[] = "query_cache_hash";
        int result = manager->initialize("test_query_cache.json", hostname_hash);
        assert(result == 0);
        
        // Create a complex scenario with multiple processes, threads, and categories
        std::vector<ProcessID> processes = {1000, 1001};
        std::vector<ThreadID> threads = {2000, 2001};  
        std::vector<std::string> categories = {"system_files", "user_files"};
        std::vector<uint64_t> time_windows = {1000000, 2000000};
        
        // Store expected metrics for each combination
        std::unordered_map<std::string, CacheMetrics> expected_metrics;
        
        for (ProcessID proc : processes) {
            for (ThreadID thread : threads) {
                for (const std::string& category : categories) {
                    for (uint64_t time_window : time_windows) {
                        AggregationKey key = {
                            .node_id = "query_node",
                            .process_id = proc,
                            .thread_id = thread,
                            .category = category,
                            .start_time = time_window
                        };
                        
                        // Generate unique access pattern for this combination
                        int base_accesses = 8 + (proc - 1000) * 3 + (thread - 2000) * 2;
                        int base_files = 2 + (proc - 1000) + (category == "user_files" ? 1 : 0);
                        
                        // Simulate file accesses
                        for (int file_idx = 0; file_idx < base_files; ++file_idx) {
                            std::string filename = category + "_p" + std::to_string(proc) + 
                                                 "_t" + std::to_string(thread) + "_f" + std::to_string(file_idx);
                            
                            int accesses_per_file = base_accesses / base_files;
                            if (file_idx == 0) {
                                accesses_per_file += base_accesses % base_files;
                            }
                            
                            for (int access = 0; access < accesses_per_file; ++access) {
                                std::unordered_map<std::string, std::any> metadata;
                                metadata["filename"] = filename;
                                
                                manager->aggregate_data_event(key, "access", 50 + access, &metadata);
                            }
                        }
                        
                        // Add unique files count
                        manager->aggregate_metadata_event(key, "file_access", "unique_files_count", 
                                                         std::to_string(base_files).c_str(), false);
                        
                        // Store expected metrics with unique key
                        std::string metric_key = std::to_string(proc) + "_" + std::to_string(thread) + 
                                               "_" + category + "_" + std::to_string(time_window);
                        expected_metrics[metric_key].total_access_calls = base_accesses;
                        expected_metrics[metric_key].unique_files_accessed = base_files;
                        expected_metrics[metric_key].calculate_miss_rate();
                    }
                }
            }
        }
        
        auto aggregated = manager->get_aggregated_data();
        assert(aggregated.size() == expected_metrics.size());
        
        std::cout << "  Query-based Metric Extraction Results:" << std::endl;
        
        // Test specific queries
        std::vector<std::tuple<ProcessID, ThreadID, std::string, uint64_t, std::string>> test_queries = {
            {1000, 2000, "system_files", 1000000, "Process 1000, Thread 2000, System Files, Window 1"},
            {1001, 2001, "user_files", 2000000, "Process 1001, Thread 2001, User Files, Window 2"},
            {1000, 2001, "system_files", 2000000, "Process 1000, Thread 2001, System Files, Window 2"}
        };
        
        for (const auto& [proc, thread, category, time_window, description] : test_queries) {
            AggregationKey query_key = {
                .node_id = "query_node",
                .process_id = proc,
                .thread_id = thread,
                .category = category,
                .start_time = time_window
            };
            
            CacheMetrics actual_metrics = extract_cache_metrics(aggregated, query_key);
            
            std::string metric_key = std::to_string(proc) + "_" + std::to_string(thread) + 
                                   "_" + category + "_" + std::to_string(time_window);
            CacheMetrics expected = expected_metrics[metric_key];
            
            std::cout << "    " << description << ":" << std::endl;
            std::cout << "      Accesses: " << actual_metrics.total_access_calls 
                      << ", Unique files: " << actual_metrics.unique_files_accessed
                      << ", Miss rate: " << actual_metrics.cache_miss_rate << std::endl;
            
            assert(actual_metrics.total_access_calls == expected.total_access_calls);
            assert(actual_metrics.unique_files_accessed == expected.unique_files_accessed);
            assert(std::abs(actual_metrics.cache_miss_rate - expected.cache_miss_rate) < 0.001);
        }
        
        manager->finalize(0, true);
        
        std::cout << "  Query-based metric extraction test passed" << std::endl;
        return true;
    }
    
    static bool test_realistic_workload_simulation() {
        std::cout << "Testing realistic workload cache miss rate simulation..." << std::endl;
        
        auto manager = dftracer::Singleton<dftracer::AggregationManager>::get_instance();
        
        char hostname_hash[] = "realistic_cache_hash";
        int result = manager->initialize("test_realistic_cache.json", hostname_hash);
        assert(result == 0);
        
        // Simulate a realistic HPC workload with:
        // - Data processing application
        // - Multiple processes reading input files
        // - Repeated access to configuration files
        // - Temporary file creation and access
        
        AggregationKey workload_key = {
            .node_id = "hpc_node_001",
            .process_id = 12345,
            .thread_id = 67890,
            .category = "hpc_data_processing",
            .start_time = 1634567890000  // Realistic timestamp
        };
        
        // Simulate realistic file access patterns
        std::vector<std::pair<std::string, int>> file_access_pattern = {
            {"config.yaml", 50},         // Config file accessed frequently
            {"input_data_0001.dat", 25}, // Large data files accessed multiple times
            {"input_data_0002.dat", 25},
            {"input_data_0003.dat", 25},
            {"temp_work_001.tmp", 15},   // Temporary files
            {"temp_work_002.tmp", 15},
            {"temp_work_003.tmp", 15},
            {"metadata.json", 30},       // Metadata accessed often
            {"output_buffer.bin", 20},   // Output staging
            {"checkpoint_001.chk", 8},   // Checkpoint files
            {"checkpoint_002.chk", 8},
            {"checkpoint_003.chk", 8}
        };
        
        int total_expected_accesses = 0;
        int expected_unique_files = file_access_pattern.size();
        
        // Simulate the file access workload
        for (const auto& [filename, access_count] : file_access_pattern) {
            for (int access = 0; access < access_count; ++access) {
                std::unordered_map<std::string, std::any> metadata;
                metadata["filename"] = filename;
                metadata["workload_type"] = std::string("hpc_data_processing");
                metadata["access_pattern"] = std::string("sequential");
                metadata["file_size"] = static_cast<unsigned long long>(
                    filename.find(".dat") != std::string::npos ? 1024*1024*100 :  // 100MB for data files
                    filename.find(".tmp") != std::string::npos ? 1024*1024*10 :   // 10MB for temp files
                    1024*10  // 10KB for config/metadata files
                );
                
                manager->aggregate_data_event(workload_key, "access", 150 + access, &metadata);
                total_expected_accesses++;
            }
        }
        
        // Add unique files count
        manager->aggregate_metadata_event(workload_key, "file_access", "unique_files_count", 
                                         std::to_string(expected_unique_files).c_str(), false);
        
        auto aggregated = manager->get_aggregated_data();
        assert(aggregated.size() == 1);
        
        CacheMetrics workload_metrics = extract_cache_metrics(aggregated, workload_key);
        
        std::cout << "  Realistic HPC Workload Cache Analysis:" << std::endl;
        std::cout << "    Node: " << workload_key.node_id << std::endl;
        std::cout << "    Process ID: " << workload_key.process_id << std::endl;
        std::cout << "    Thread ID: " << workload_key.thread_id << std::endl;
        std::cout << "    Category: " << workload_key.category << std::endl;
        std::cout << "    Time Window: " << workload_key.start_time << std::endl;
        std::cout << "    Total access() calls: " << workload_metrics.total_access_calls << std::endl;
        std::cout << "    Unique files accessed: " << workload_metrics.unique_files_accessed << std::endl;
        std::cout << "    Cache miss rate: " << workload_metrics.cache_miss_rate << std::endl;
        std::cout << "    Interpretation: On average, each unique file was accessed " 
                  << workload_metrics.cache_miss_rate << " times" << std::endl;
        
        // Verify the realistic workload metrics
        assert(workload_metrics.total_access_calls == total_expected_accesses);
        assert(workload_metrics.unique_files_accessed == expected_unique_files);
        
        // Cache miss rate should be reasonable for this workload (around 22.4)
        double expected_miss_rate = static_cast<double>(total_expected_accesses) / expected_unique_files;
        assert(std::abs(workload_metrics.cache_miss_rate - expected_miss_rate) < 0.001);
        
        manager->finalize(0, true);
        
        std::cout << "  Realistic workload simulation test passed" << std::endl;
        return true;
    }
};

} // namespace test  
} // namespace dftracer

int main() {
    std::cout << "Running File Cache Miss Rate Unit Tests..." << std::endl << std::endl;
    
    bool success = dftracer::test::FileCacheMissRateTest::run_all_tests();
    
    std::cout << std::endl;
    if (success) {
        std::cout << "All cache miss rate tests PASSED!" << std::endl;
        std::cout << std::endl;
        std::cout << "Summary: This test suite demonstrates how to use dftracer aggregation" << std::endl;
        std::cout << "to calculate file cache miss rates by aggregating access() calls and" << std::endl;
        std::cout << "unique file counts, then deriving the miss rate metric from the" << std::endl;
        std::cout << "aggregated data using specific process ID, thread ID, category, and" << std::endl;
        std::cout << "start time combinations." << std::endl;
        return 0;
    } else {
        std::cout << "Some cache miss rate tests FAILED!" << std::endl;
        return 1;
    }
}
