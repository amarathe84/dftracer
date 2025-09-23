#include <iostream>
#include <cassert>
#include <string>
#include <unordered_map>
#include <any>
#include <memory>
#include <shared_mutex>
#include <fstream>
#include <sstream>
#include <vector>
#include <mutex>
#include <algorithm>
#include <cstdint>
#include <cstdio>

/**
 * Standalone Unit Test: Basic Aggregation Concepts
 * 
 * This test validates the core aggregation concepts without requiring
 * the full dftracer dependency chain. It focuses on:
 * 1. Key structure and hashing
 * 2. Data aggregation patterns
 * 3. Metadata merging logic
 * 4. Basic statistics calculation
 */

namespace dftracer {
namespace test {

// Simplified types matching dftracer
using ProcessID = unsigned long;
using ThreadID = unsigned long;
using TimeResolution = unsigned long long;

// Simplified aggregation key structure
struct SimpleAggregationKey {
    std::string node_id;
    ProcessID process_id;
    ThreadID thread_id;
    std::string category;
    TimeResolution start_time;
    
    bool operator==(const SimpleAggregationKey& other) const {
        return node_id == other.node_id &&
               process_id == other.process_id &&
               thread_id == other.thread_id &&
               category == other.category &&
               start_time == other.start_time;
    }
};

// Hash function for the key
struct SimpleAggregationKeyHash {
    std::size_t operator()(const SimpleAggregationKey& key) const {
        std::size_t h1 = std::hash<std::string>{}(key.node_id);
        std::size_t h2 = std::hash<ProcessID>{}(key.process_id);
        std::size_t h3 = std::hash<ThreadID>{}(key.thread_id);
        std::size_t h4 = std::hash<std::string>{}(key.category);
        std::size_t h5 = std::hash<TimeResolution>{}(key.start_time);
        
        // Combine hashes
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4);
    }
};

// Simplified aggregation data
struct SimpleAggregationData {
    int count = 0;
    uint64_t total_duration = 0;
    uint64_t min_duration = UINT64_MAX;
    uint64_t max_duration = 0;
    std::string event_name;
    std::unordered_map<std::string, std::any> aggregated_metadata;
    
    double get_average_duration() const {
        return count > 0 ? static_cast<double>(total_duration) / count : 0.0;
    }
};

// Simplified aggregation manager for testing
class SimpleAggregationManager {
private:
    std::unordered_map<SimpleAggregationKey, SimpleAggregationData, SimpleAggregationKeyHash> data_map;
    mutable std::shared_mutex mutex_;
    bool initialized = false;
    
public:
    bool initialize(const std::string& output_file) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        initialized = true;
        output_filename = output_file;
        return true;
    }
    
    bool aggregate_data_event(const SimpleAggregationKey& key, const std::string& event_name, 
                             uint64_t duration, const std::unordered_map<std::string, std::any>* metadata = nullptr) {
        if (!initialized) return false;
        
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        auto& data = data_map[key];
        data.count++;
        data.total_duration += duration;
        data.min_duration = std::min(data.min_duration, duration);
        data.max_duration = std::max(data.max_duration, duration);
        data.event_name = event_name;
        
        if (metadata) {
            merge_metadata(data.aggregated_metadata, *metadata);
        }
        
        return true;
    }
    
    bool aggregate_counter_event(const SimpleAggregationKey& key, const std::string& event_name,
                                const std::unordered_map<std::string, std::any>* metadata = nullptr) {
        if (!initialized) return false;
        
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        auto& data = data_map[key];
        data.count++;
        data.event_name = event_name;
        
        if (metadata) {
            merge_metadata(data.aggregated_metadata, *metadata);
        }
        
        return true;
    }
    
    std::unordered_map<SimpleAggregationKey, SimpleAggregationData, SimpleAggregationKeyHash> 
    get_aggregated_data() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return data_map;
    }
    
    void finalize() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        // Write simple JSON output
        std::ofstream file(output_filename);
        if (file.is_open()) {
            file << "[\n";
            bool first = true;
            for (const auto& [key, data] : data_map) {
                if (!first) file << ",\n";
                first = false;
                
                file << "  {\n";
                file << "    \"node_id\": \"" << key.node_id << "\",\n";
                file << "    \"process_id\": " << key.process_id << ",\n";
                file << "    \"thread_id\": " << key.thread_id << ",\n";
                file << "    \"category\": \"" << key.category << "\",\n";
                file << "    \"start_time\": " << key.start_time << ",\n";
                file << "    \"event_name\": \"" << data.event_name << "\",\n";
                file << "    \"count\": " << data.count << ",\n";
                file << "    \"total_duration\": " << data.total_duration << ",\n";
                file << "    \"min_duration\": " << data.min_duration << ",\n";
                file << "    \"max_duration\": " << data.max_duration << ",\n";
                file << "    \"average_duration\": " << data.get_average_duration() << "\n";
                file << "  }";
            }
            file << "\n]\n";
            file.close();
        }
        
        data_map.clear();
        initialized = false;
    }
    
private:
    std::string output_filename;
    
    void merge_metadata(std::unordered_map<std::string, std::any>& target, 
                       const std::unordered_map<std::string, std::any>& source) {
        for (const auto& [key, value] : source) {
            auto target_it = target.find(key);
            if (target_it == target.end()) {
                target[key] = value;
            } else {
                // Try to aggregate numeric values
                try {
                    if (value.type() == typeid(unsigned long long) && 
                        target_it->second.type() == typeid(unsigned long long)) {
                        auto existing = std::any_cast<unsigned long long>(target_it->second);
                        auto new_val = std::any_cast<unsigned long long>(value);
                        target_it->second = existing + new_val;
                    } else if (value.type() == typeid(long long) && 
                              target_it->second.type() == typeid(long long)) {
                        auto existing = std::any_cast<long long>(target_it->second);
                        auto new_val = std::any_cast<long long>(value);
                        target_it->second = existing + new_val;
                    } else if (value.type() == typeid(double) && 
                              target_it->second.type() == typeid(double)) {
                        auto existing = std::any_cast<double>(target_it->second);
                        auto new_val = std::any_cast<double>(value);
                        target_it->second = existing + new_val;
                    } else {
                        // For non-numeric or mixed types, keep latest value
                        target_it->second = value;
                    }
                } catch (...) {
                    // If casting fails, keep latest value
                    target_it->second = value;
                }
            }
        }
    }
};

class StandaloneAggregationTest {
public:
    static bool run_all_tests() {
        std::cout << "=== Standalone Aggregation Unit Tests ===" << std::endl;
        
        bool all_passed = true;
        all_passed &= test_key_functionality();
        all_passed &= test_basic_aggregation();
        all_passed &= test_metadata_merging();
        all_passed &= test_statistics_calculation();
        all_passed &= test_multiple_keys();
        all_passed &= test_file_output();
        
        if (all_passed) {
            std::cout << "tests passeed" << std::endl;
        } else {
            std::cout << "tests failed" << std::endl;
        }
        
        return all_passed;
    }
//AM: Test this first as a building block
private:
    static bool test_key_functionality() {
        std::cout << "Testing aggregation key functionality..." << std::endl;
        
        SimpleAggregationKey key1 = {
            .node_id = "test_node",
            .process_id = 1000,
            .thread_id = 2000,
            .category = "test_category",
            .start_time = 1234567890
        };
        
        SimpleAggregationKey key2 = key1;
        SimpleAggregationKey key3 = key1;
        key3.process_id = 1001;  // Different process ID
        
        // Test equality 
        assert(key1 == key2);
        assert(!(key1 == key3));
        
        // Test hashing
        SimpleAggregationKeyHash hasher;
        std::size_t hash1 = hasher(key1);
        std::size_t hash2 = hasher(key2);
        std::size_t hash3 = hasher(key3);
        
        assert(hash1 == hash2);  // Same keys should have same hash
        assert(hash1 != hash3);  // Different keys should have different hash (usually)
        
        std::cout << "Key functionality test passed" << std::endl;
        return true;
    }
    
    static bool test_basic_aggregation() {
        std::cout << "Testing basic aggregation functionality..." << std::endl;
        
        SimpleAggregationManager manager;
        bool init_result = manager.initialize("test_basic.json");
        assert(init_result);
        
        SimpleAggregationKey key = {
            .node_id = "test_node",
            .process_id = 1000,
            .thread_id = 2000,
            .category = "basic_test",
            .start_time = 1000000
        };
        
        // Add some events
        assert(manager.aggregate_data_event(key, "test_event", 100));
        assert(manager.aggregate_data_event(key, "test_event", 200));
        assert(manager.aggregate_data_event(key, "test_event", 150));
        
        auto results = manager.get_aggregated_data();
        assert(results.size() == 1);
        
        auto it = results.find(key);
        assert(it != results.end());
        
        const SimpleAggregationData& data = it->second;
        assert(data.count == 3);
        assert(data.total_duration == 450);
        assert(data.min_duration == 100);
        assert(data.max_duration == 200);
        assert(data.get_average_duration() == 150.0);
        assert(data.event_name == "test_event");
        
        manager.finalize();
        
        std::cout << "Basic aggregation test passed" << std::endl;
        return true;
    }
    
    static bool test_metadata_merging() {
        std::cout << "Testing metadata merging functionality..." << std::endl;
        
        SimpleAggregationManager manager;
        assert(manager.initialize("test_metadata.json"));
        
        SimpleAggregationKey key = {
            .node_id = "metadata_test",
            .process_id = 1000,
            .thread_id = 2000,
            .category = "metadata_test",
            .start_time = 1000000
        };
        
        // First event with metadata
        std::unordered_map<std::string, std::any> metadata1;
        metadata1["bytes_processed"] = 1024ULL;
        metadata1["operations"] = 10LL;
        metadata1["filename"] = std::string("file1.txt");
        
        // Second event with overlapping metadata
        std::unordered_map<std::string, std::any> metadata2;
        metadata2["bytes_processed"] = 2048ULL;
        metadata2["operations"] = 15LL;
        metadata2["filename"] = std::string("file2.txt");
        
        assert(manager.aggregate_data_event(key, "metadata_event", 100, &metadata1));
        assert(manager.aggregate_data_event(key, "metadata_event", 200, &metadata2));
        
        auto results = manager.get_aggregated_data();
        assert(results.size() == 1);
        
        const SimpleAggregationData& data = results.begin()->second;
        assert(data.count == 2);
        
        // Check metadata aggregation
        auto bytes_it = data.aggregated_metadata.find("bytes_processed");
        assert(bytes_it != data.aggregated_metadata.end());
        assert(std::any_cast<unsigned long long>(bytes_it->second) == 3072ULL);  // 1024 + 2048
        
        auto ops_it = data.aggregated_metadata.find("operations");
        assert(ops_it != data.aggregated_metadata.end());
        assert(std::any_cast<long long>(ops_it->second) == 25LL);  // 10 + 15
        
        auto filename_it = data.aggregated_metadata.find("filename");
        assert(filename_it != data.aggregated_metadata.end());
        assert(std::any_cast<std::string>(filename_it->second) == "file2.txt");  // Latest value
        
        manager.finalize();
        
        std::cout << "Metadata merging test passed" << std::endl;
        return true;
    }
    
    static bool test_statistics_calculation() {
        std::cout << "Testing statistics calculation..." << std::endl;
        
        SimpleAggregationManager manager;
        assert(manager.initialize("test_statistics.json"));
        
        SimpleAggregationKey key = {
            .node_id = "stats_test",
            .process_id = 1000,
            .thread_id = 2000,
            .category = "stats_test",
            .start_time = 1000000
        };
        
        // Add events with various durations
        std::vector<uint64_t> durations = {50, 100, 150, 200, 250, 300, 350, 400, 450, 500};
        
        for (uint64_t duration : durations) {
            assert(manager.aggregate_data_event(key, "stats_event", duration));
        }
        
        auto results = manager.get_aggregated_data();
        const SimpleAggregationData& data = results.begin()->second;
        
        assert(data.count == 10);
        assert(data.total_duration == 2750);  // Sum of durations
        assert(data.min_duration == 50);
        assert(data.max_duration == 500);
        assert(data.get_average_duration() == 275.0);  // 2750 / 10
        
        manager.finalize();
        
        std::cout << "Statistics calculation test passed" << std::endl;
        return true;
    }
    
    static bool test_multiple_keys() {
        std::cout << "Testing multiple aggregation keys..." << std::endl;
        
        SimpleAggregationManager manager;
        assert(manager.initialize("test_multiple_keys.json"));
        
        // Create multiple different keys
        std::vector<SimpleAggregationKey> keys = {
            {"node1", 1000, 2000, "category1", 1000000},
            {"node1", 1001, 2000, "category1", 1000000},  // Different process
            {"node1", 1000, 2001, "category1", 1000000},  // Different thread
            {"node1", 1000, 2000, "category2", 1000000},  // Different category
            {"node1", 1000, 2000, "category1", 2000000}   // Different time
        };
        
        // Add events for each key
        for (size_t i = 0; i < keys.size(); ++i) {
            for (int j = 0; j < 5; ++j) {
                assert(manager.aggregate_data_event(keys[i], "multi_event", 100 + i * 10 + j));
            }
        }
        
        auto results = manager.get_aggregated_data();
        assert(results.size() == keys.size());
        
        // Verify each key has correct aggregation
        for (size_t i = 0; i < keys.size(); ++i) {
            auto it = results.find(keys[i]);
            assert(it != results.end());
            
            const SimpleAggregationData& data = it->second;
            assert(data.count == 5);
            assert(data.event_name == "multi_event");
        }
        
        manager.finalize();
        
        std::cout << "Multiple keys test passed" << std::endl;
        return true;
    }
    
    static bool test_file_output() {
        std::cout << "Testing file output functionality..." << std::endl;
        
        const std::string output_file = "test_output.json";
        
        {
            SimpleAggregationManager manager;
            assert(manager.initialize(output_file));
            
            SimpleAggregationKey key = {
                .node_id = "output_test",
                .process_id = 1000,
                .thread_id = 2000,
                .category = "output_category",
                .start_time = 1000000
            };
            
            assert(manager.aggregate_data_event(key, "output_event", 100));
            assert(manager.aggregate_data_event(key, "output_event", 200));
            
            manager.finalize();
        }
        
        // Verify file was created and has content
        std::ifstream file(output_file);
        assert(file.is_open());
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        file.close();
        
        // Basic checks for JSON content
        assert(content.find("output_test") != std::string::npos);
        assert(content.find("output_event") != std::string::npos);
        assert(content.find("\"count\": 2") != std::string::npos);
        assert(content.find("\"total_duration\": 300") != std::string::npos);
        
        // Clean up
        std::remove(output_file.c_str());
        
        std::cout << "  File output test passed" << std::endl;
        return true;
    }
};

} // namespace test
} // namespace dftracer

int main() {
    std::cout << "Running Standalone Aggregation Unit Tests..." << std::endl << std::endl;
    
    bool success = dftracer::test::StandaloneAggregationTest::run_all_tests();
    
    std::cout << std::endl;
    if (success) {
        std::cout << "All tests passed" << std::endl;
        return 0;
    } else {
        std::cout << "failed" << std::endl;
        return 1;
    }
}
