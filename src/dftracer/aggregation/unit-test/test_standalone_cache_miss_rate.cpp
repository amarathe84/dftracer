#include <iostream>
#include <cassert>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <shared_mutex>
#include <fstream>
#include <mutex>
#include <cstdint>
#include <cstdio>

/**
 * Standalone Unit Test: File Cache Miss Rate Calculation
 * 
 * This test demonstrates how to calculate file cache miss rate as:
 * count(access() calls) / count(unique_files_accessed)
 * 
 * Uses simplified aggregation logic without external dependencies
 * to show the concept and verify the metric calculation.
 */

namespace dftracer {
namespace test {

// Simplified types matching dftracer
using ProcessID = unsigned long;
using ThreadID = unsigned long;
using TimeResolution = unsigned long long;

// Simplified aggregation key structure
struct CacheAggregationKey {
    std::string node_id;
    ProcessID process_id;
    ThreadID thread_id;
    std::string category;
    TimeResolution start_time;
    
    bool operator==(const CacheAggregationKey& other) const {
        return node_id == other.node_id &&
               process_id == other.process_id &&
               thread_id == other.thread_id &&
               category == other.category &&
               start_time == other.start_time;
    }
};

// Hash function for the key
struct CacheAggregationKeyHash {
    std::size_t operator()(const CacheAggregationKey& key) const {
        std::size_t h1 = std::hash<std::string>{}(key.node_id);
        std::size_t h2 = std::hash<ProcessID>{}(key.process_id);
        std::size_t h3 = std::hash<ThreadID>{}(key.thread_id);
        std::size_t h4 = std::hash<std::string>{}(key.category);
        std::size_t h5 = std::hash<TimeResolution>{}(key.start_time);
        
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4);
    }
};

// Cache-specific aggregation data
struct CacheAggregationData {
    int total_access_calls = 0;
    std::unordered_set<std::string> unique_files;
    std::unordered_map<std::string, int> file_access_counts;
    std::string event_name;
    
    int get_unique_file_count() const {
        return static_cast<int>(unique_files.size());
    }
    
    double get_cache_miss_rate() const {
        return unique_files.size() > 0 ? 
               static_cast<double>(total_access_calls) / unique_files.size() : 0.0;
    }
};

// Cache miss rate calculation manager
class CacheMissRateManager {
private:
    std::unordered_map<CacheAggregationKey, CacheAggregationData, CacheAggregationKeyHash> cache_data;
    mutable std::shared_mutex mutex_;
    bool initialized = false;
    std::string output_filename;
    
public:
    bool initialize(const std::string& output_file) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        initialized = true;
        output_filename = output_file;
        return true;
    }
    
    bool aggregate_file_access(const CacheAggregationKey& key, const std::string& filename) {
        if (!initialized) return false;
        
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        auto& data = cache_data[key];
        data.total_access_calls++;
        data.unique_files.insert(filename);
        data.file_access_counts[filename]++;
        data.event_name = "access";
        
        return true;
    }
    
    struct CacheMetrics {
        int total_access_calls = 0;
        int unique_files_accessed = 0;
        double cache_miss_rate = 0.0;
        std::unordered_map<std::string, int> file_access_counts;
    };
    
    CacheMetrics get_cache_metrics(const CacheAggregationKey& key) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        
        CacheMetrics metrics;
        auto it = cache_data.find(key);
        if (it != cache_data.end()) {
            const CacheAggregationData& data = it->second;
            metrics.total_access_calls = data.total_access_calls;
            metrics.unique_files_accessed = data.get_unique_file_count();
            metrics.cache_miss_rate = data.get_cache_miss_rate();
            metrics.file_access_counts = data.file_access_counts;
        }
        
        return metrics;
    }
    
    std::unordered_map<CacheAggregationKey, CacheAggregationData, CacheAggregationKeyHash> 
    get_all_cache_data() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return cache_data;
    }
    
    void finalize() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        // Write cache analysis output
        std::ofstream file(output_filename);
        if (file.is_open()) {
            file << "[\n";
            bool first = true;
            for (const auto& [key, data] : cache_data) {
                if (!first) file << ",\n";
                first = false;
                
                file << "  {\n";
                file << "    \"node_id\": \"" << key.node_id << "\",\n";
                file << "    \"process_id\": " << key.process_id << ",\n"; 
                file << "    \"thread_id\": " << key.thread_id << ",\n";
                file << "    \"category\": \"" << key.category << "\",\n";
                file << "    \"start_time\": " << key.start_time << ",\n";
                file << "    \"total_access_calls\": " << data.total_access_calls << ",\n";
                file << "    \"unique_files_accessed\": " << data.get_unique_file_count() << ",\n";
                file << "    \"cache_miss_rate\": " << data.get_cache_miss_rate() << ",\n";
                file << "    \"files_accessed\": [";
                
                bool first_file = true;
                for (const auto& filename : data.unique_files) {
                    if (!first_file) file << ", ";
                    first_file = false;
                    file << "\"" << filename << "\"";
                }
                file << "]\n";
                file << "  }";
            }
            file << "\n]\n";
            file.close();
        }
        
        cache_data.clear();
        initialized = false;
    }
};

class StandaloneCacheMissRateTest {
public:
    static bool run_all_tests() {
        std::cout << "=== Standalone File Cache Miss Rate Unit Tests ===" << std::endl;
        
        bool all_passed = true;
        all_passed &= test_basic_cache_miss_calculation();
        all_passed &= test_multiple_process_cache_analysis();
        all_passed &= test_time_windowed_cache_metrics();
        all_passed &= test_query_based_cache_extraction();
        all_passed &= test_realistic_hpc_workload();
        
        if (all_passed) {
            std::cout << "All standalone cache miss rate tests PASSED" << std::endl;
        } else {
            std::cout << "Some standalone cache miss rate tests FAILED" << std::endl;
        }
        
        return all_passed;
    }

private:
    static bool test_basic_cache_miss_calculation() {
        std::cout << "Testing basic cache miss rate calculation..." << std::endl;
        
        CacheMissRateManager manager;
        assert(manager.initialize("test_basic_cache.json"));
        
        CacheAggregationKey key = {
            .node_id = "test_node",
            .process_id = 1000,
            .thread_id = 2000,
            .category = "file_access",
            .start_time = 1000000
        };
        
        // Simulate file access pattern:
        // file1.txt: 10 accesses
        // file2.txt: 5 accesses  
        // file3.txt: 3 accesses
        // Total: 18 access() calls, 3 unique files
        // Expected cache miss rate: 18/3 = 6.0
        
        std::vector<std::pair<std::string, int>> access_pattern = {
            {"file1.txt", 10},
            {"file2.txt", 5},
            {"file3.txt", 3}
        };
        
        int expected_total_accesses = 0;
        int expected_unique_files = access_pattern.size();
        
        for (const auto& [filename, access_count] : access_pattern) {
            for (int i = 0; i < access_count; ++i) {
                assert(manager.aggregate_file_access(key, filename));
                expected_total_accesses++;
            }
        }
        
        auto metrics = manager.get_cache_metrics(key);
        
        std::cout << "  Total access() calls: " << metrics.total_access_calls << std::endl;
        std::cout << "  Unique files accessed: " << metrics.unique_files_accessed << std::endl;
        std::cout << "  Cache miss rate: " << metrics.cache_miss_rate << std::endl;
        
        // Verify calculations
        assert(metrics.total_access_calls == expected_total_accesses);
        assert(metrics.unique_files_accessed == expected_unique_files);
        assert(std::abs(metrics.cache_miss_rate - 6.0) < 0.001);
        
        // Verify individual file access counts
        for (const auto& [filename, expected_count] : access_pattern) {
            auto it = metrics.file_access_counts.find(filename);
            assert(it != metrics.file_access_counts.end());
            assert(it->second == expected_count);
        }
        
        manager.finalize();
        
        std::cout << "  Basic cache miss rate calculation test passed" << std::endl;
        return true;
    }
    
    static bool test_multiple_process_cache_analysis() {
        std::cout << "Testing multiple process cache analysis..." << std::endl;
        
        CacheMissRateManager manager;
        assert(manager.initialize("test_multi_process_cache.json"));
        
        // Test 3 different processes with different access patterns
        std::vector<ProcessID> processes = {1000, 1001, 1002};
        std::unordered_map<ProcessID, std::pair<int, double>> expected_metrics;
        
        for (ProcessID proc_id : processes) {
            CacheAggregationKey key = {
                .node_id = "multi_proc_node",
                .process_id = proc_id,
                .thread_id = 2000,
                .category = "file_access",
                .start_time = 1000000
            };
            
            // Each process has different patterns
            int base_files = 2 + (proc_id - 1000);  // 2, 3, 4 unique files
            int accesses_per_file = 5 + (proc_id - 1000) * 2;  // 5, 7, 9 accesses per file
            
            int total_accesses = 0;
            for (int file_idx = 0; file_idx < base_files; ++file_idx) {
                std::string filename = "proc_" + std::to_string(proc_id) + "_file_" + std::to_string(file_idx) + ".dat";
                
                for (int access = 0; access < accesses_per_file; ++access) {
                    assert(manager.aggregate_file_access(key, filename));
                    total_accesses++;
                }
            }
            
            double expected_miss_rate = static_cast<double>(total_accesses) / base_files;
            expected_metrics[proc_id] = {total_accesses, expected_miss_rate};
        }
        
        // Verify each process's cache metrics
        for (ProcessID proc_id : processes) {
            CacheAggregationKey query_key = {
                .node_id = "multi_proc_node",
                .process_id = proc_id,
                .thread_id = 2000,
                .category = "file_access",
                .start_time = 1000000
            };
            
            auto metrics = manager.get_cache_metrics(query_key);
            auto [expected_accesses, expected_miss_rate] = expected_metrics[proc_id];
            
            std::cout << "  Process " << proc_id << ": " 
                      << metrics.total_access_calls << " accesses, "
                      << metrics.unique_files_accessed << " unique files, "
                      << "miss rate: " << metrics.cache_miss_rate << std::endl;
            
            assert(metrics.total_access_calls == expected_accesses);
            assert(std::abs(metrics.cache_miss_rate - expected_miss_rate) < 0.001);
        }
        
        manager.finalize();
        
        std::cout << "  Multiple process cache analysis test passed" << std::endl;
        return true;
    }
    
    static bool test_time_windowed_cache_metrics() {
        std::cout << "Testing time-windowed cache metrics..." << std::endl;
        
        CacheMissRateManager manager;
        assert(manager.initialize("test_time_windowed_cache.json"));
        
        const uint64_t window_size = 1000000;  // 1 second windows
        const int num_windows = 3;
        
        // Different cache behavior in each time window
        for (int window = 0; window < num_windows; ++window) {
            uint64_t window_start = window * window_size;
            
            CacheAggregationKey key = {
                .node_id = "time_window_node",
                .process_id = 1000,
                .thread_id = 2000,
                .category = "time_series_access",
                .start_time = window_start
            };
            
            // Window 0: High locality (few unique files, many accesses)
            // Window 1: Medium locality
            // Window 2: Low locality (many unique files, fewer accesses each)
            
            int unique_files = (window == 0) ? 2 : (window == 1) ? 4 : 8;
            int accesses_per_file = (window == 0) ? 15 : (window == 1) ? 8 : 3;
            
            for (int file_idx = 0; file_idx < unique_files; ++file_idx) {
                std::string filename = "window_" + std::to_string(window) + "_data_" + std::to_string(file_idx) + ".bin";
                
                for (int access = 0; access < accesses_per_file; ++access) {
                    assert(manager.aggregate_file_access(key, filename));
                }
            }
        }
        
        // Analyze cache behavior across time windows
        std::cout << "  Time Window Cache Behavior Analysis:" << std::endl;
        
        for (int window = 0; window < num_windows; ++window) {
            uint64_t window_start = window * window_size;
            
            CacheAggregationKey query_key = {
                .node_id = "time_window_node",
                .process_id = 1000,
                .thread_id = 2000,
                .category = "time_series_access",
                .start_time = window_start
            };
            
            auto metrics = manager.get_cache_metrics(query_key);
            
            std::string locality = (metrics.cache_miss_rate > 10) ? "High" : 
                                 (metrics.cache_miss_rate > 5) ? "Medium" : "Low";
            
            std::cout << "    Window " << window << ": " 
                      << metrics.total_access_calls << " accesses, "
                      << metrics.unique_files_accessed << " unique files, "
                      << "miss rate: " << metrics.cache_miss_rate 
                      << " (" << locality << " locality)" << std::endl;
        }
        
        manager.finalize();
        
        std::cout << "  Time-windowed cache metrics test passed" << std::endl;
        return true;
    }
    
    static bool test_query_based_cache_extraction() {
        std::cout << "Testing query-based cache metric extraction..." << std::endl;
        
        CacheMissRateManager manager;
        assert(manager.initialize("test_query_cache.json"));
        
        // Create a complex scenario for querying
        std::vector<ProcessID> processes = {1000, 1001};
        std::vector<ThreadID> threads = {2000, 2001};
        std::vector<std::string> categories = {"config_access", "data_access"};
        
        // Generate data for all combinations
        for (ProcessID proc : processes) {
            for (ThreadID thread : threads) {
                for (const std::string& category : categories) {
                    CacheAggregationKey key = {
                        .node_id = "query_test_node",
                        .process_id = proc,
                        .thread_id = thread,
                        .category = category,
                        .start_time = 1000000
                    };
                    
                    // Different patterns based on category
                    int file_count = (category == "config_access") ? 2 : 5;
                    int base_accesses = (category == "config_access") ? 20 : 8;
                    
                    for (int file_idx = 0; file_idx < file_count; ++file_idx) {
                        std::string filename = category + "_p" + std::to_string(proc) + 
                                             "_t" + std::to_string(thread) + "_f" + std::to_string(file_idx) + ".conf";
                        
                        for (int access = 0; access < base_accesses; ++access) {
                            assert(manager.aggregate_file_access(key, filename));
                        }
                    }
                }
            }
        }
        
        std::cout << "  Query-based Cache Metric Results:" << std::endl;
        
        // Test specific queries
        struct QueryTest {
            ProcessID proc;
            ThreadID thread;
            std::string category;
            std::string description;
        };
        
        std::vector<QueryTest> queries = {
            {1000, 2000, "config_access", "Proc 1000, Thread 2000, Config Files"},
            {1001, 2001, "data_access", "Proc 1001, Thread 2001, Data Files"},
            {1000, 2001, "config_access", "Proc 1000, Thread 2001, Config Files"}
        };
        
        for (const auto& query : queries) {
            CacheAggregationKey query_key = {
                .node_id = "query_test_node",
                .process_id = query.proc,
                .thread_id = query.thread,
                .category = query.category,
                .start_time = 1000000
            };
            
            auto metrics = manager.get_cache_metrics(query_key);
            
            std::cout << "    " << query.description << ":" << std::endl;
            std::cout << "      Access calls: " << metrics.total_access_calls << std::endl;
            std::cout << "      Unique files: " << metrics.unique_files_accessed << std::endl;
            std::cout << "      Miss rate: " << metrics.cache_miss_rate << std::endl;
            
            // Verify we got meaningful results
            assert(metrics.total_access_calls > 0);
            assert(metrics.unique_files_accessed > 0);
            assert(metrics.cache_miss_rate > 0);
        }
        
        manager.finalize();
        
        std::cout << "  Query-based cache extraction test passed" << std::endl;
        return true;
    }
    
    static bool test_realistic_hpc_workload() {
        std::cout << "Testing realistic HPC workload cache analysis..." << std::endl;
        
        CacheMissRateManager manager;
        assert(manager.initialize("test_hpc_workload_cache.json"));
        
        // Simulate realistic HPC workload
        CacheAggregationKey hpc_key = {
            .node_id = "hpc_compute_node_042",
            .process_id = 98765,
            .thread_id = 543210,
            .category = "scientific_computation",
            .start_time = 1634567890000
        };
        
        // Realistic HPC file access pattern
        std::vector<std::pair<std::string, int>> hpc_pattern = {
            // Config and parameter files (accessed frequently)
            {"simulation_config.yaml", 45},
            {"material_properties.dat", 35},
            {"boundary_conditions.inp", 25},
            
            // Input data files (medium access frequency)
            {"mesh_geometry_001.vtk", 12},
            {"mesh_geometry_002.vtk", 12},
            {"initial_conditions_001.dat", 8},
            {"initial_conditions_002.dat", 8},
            
            // Checkpoint and output files (lower access frequency)
            {"checkpoint_t001.chk", 4},
            {"checkpoint_t002.chk", 4},
            {"checkpoint_t003.chk", 4},
            {"output_results_001.h5", 6},
            {"output_results_002.h5", 6},
            
            // Temporary working files
            {"temp_work_buffer_001.tmp", 15},
            {"temp_work_buffer_002.tmp", 15},
            {"temp_work_buffer_003.tmp", 15}
        };
        
        int total_expected_accesses = 0;
        int expected_unique_files = hpc_pattern.size();
        
        // Simulate the HPC workload
        for (const auto& [filename, access_count] : hpc_pattern) {
            for (int access = 0; access < access_count; ++access) {
                assert(manager.aggregate_file_access(hpc_key, filename));
                total_expected_accesses++;
            }
        }
        
        auto hpc_metrics = manager.get_cache_metrics(hpc_key);
        
        std::cout << "  Realistic HPC Workload Cache Analysis:" << std::endl;
        std::cout << "    Node: " << hpc_key.node_id << std::endl;
        std::cout << "    Process ID: " << hpc_key.process_id << std::endl;
        std::cout << "    Thread ID: " << hpc_key.thread_id << std::endl;
        std::cout << "    Category: " << hpc_key.category << std::endl;
        std::cout << "    Time Window: " << hpc_key.start_time << std::endl;
        std::cout << "    Total access() calls: " << hpc_metrics.total_access_calls << std::endl;
        std::cout << "    Unique files accessed: " << hpc_metrics.unique_files_accessed << std::endl;
        std::cout << "    Cache miss rate: " << hpc_metrics.cache_miss_rate << std::endl;
        
        // Calculate expected miss rate
        double expected_miss_rate = static_cast<double>(total_expected_accesses) / expected_unique_files;
        
        std::cout << "    Analysis: Each unique file accessed ~" << hpc_metrics.cache_miss_rate 
                  << " times on average" << std::endl;
        std::cout << "    Cache efficiency: " << (1.0 / hpc_metrics.cache_miss_rate) * 100 
                  << "% (lower is better for caching)" << std::endl;
        
        // Verify the calculations
        assert(hpc_metrics.total_access_calls == total_expected_accesses);
        assert(hpc_metrics.unique_files_accessed == expected_unique_files);
        assert(std::abs(hpc_metrics.cache_miss_rate - expected_miss_rate) < 0.001);
        
        manager.finalize();
        
        std::cout << "  Realistic HPC workload test passed" << std::endl;
        return true;
    }
};

} // namespace test
} // namespace dftracer

int main() {
    std::cout << "Running Standalone File Cache Miss Rate Unit Tests..." << std::endl << std::endl;
    
    bool success = dftracer::test::StandaloneCacheMissRateTest::run_all_tests();
    
    std::cout << std::endl;
    if (success) {
        std::cout << "All standalone cache miss rate tests PASSED!" << std::endl;
        std::cout << std::endl;
        std::cout << "Summary: This test demonstrates calculating file cache miss rates" << std::endl;
        std::cout << "using the formula: count(access() calls) / count(unique_files_accessed)" << std::endl;
        std::cout << "The test shows how to query specific cache metrics by providing:" << std::endl;
        std::cout << "- Process ID" << std::endl;
        std::cout << "- Thread ID" << std::endl;
        std::cout << "- Category" << std::endl;
        std::cout << "- Start Time" << std::endl;
        std::cout << "This enables fine-grained cache analysis for performance optimization." << std::endl;
        return 0;
    } else {
        std::cout << "Some standalone cache miss rate tests FAILED!" << std::endl;
        return 1;
    }
}
