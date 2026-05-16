import sys
import logging
import os
from mpi4py import MPI
from time import time
from dftracer.python import dftracer, dft_fn
import random
import statistics
import subprocess
import glob

log_inst = dftracer.initialize_log(logfile=None, data_dir=None, process_id=-1)

class Timer:
    def __init__(self):
        self.elapsed_time = 0
        self.start = 0
    def resume_time(self):
        self.start = time()

    def pause_time(self):
        self.elapsed_time += time() - self.start


def main(argc, argv):
    if argc < 5:
        raise Exception("python overhead.py <TEST_DIR> <NUM_OPS> <TRANSFER_SIZE> [--distribution]")
    logging.info(f"{argv}")
    dir = argv[2]
    num_operations = int(argv[3])
    transfer_size = int(argv[4])
    use_distribution = argc > 5 and argv[5] == "--distribution"
    
    logging.basicConfig(filename=f'{os.getcwd()}/overhead_python_{MPI.COMM_WORLD.rank}.log', encoding='utf-8', level=logging.DEBUG)
    path = f"{dir}/file_{MPI.COMM_WORLD.rank}-{MPI.COMM_WORLD.size}.bat"
    
    # Setup operation sizes distribution if enabled
    operation_sizes = []
    if use_distribution:
        # Create a mixed distribution: 80% small ops (<64K), 20% large ops (>=64K)
        small_ops = (num_operations * 80) // 100
        large_ops = num_operations - small_ops
        
        # Generate small operations
        for i in range(small_ops):
            operation_sizes.append(random.randint(1024, 65536))
        
        # Generate large operations
        for i in range(large_ops):
            operation_sizes.append(random.randint(65536, transfer_size))
        
        # Shuffle the operations
        random.shuffle(operation_sizes)
    
    iteration_times = []
    num_iterations = 10
    
    for iter_num in range(num_iterations):
        MPI.COMM_WORLD.barrier()
        if MPI.COMM_WORLD.rank == 0:
            logging.info(f"Starting iteration {iter_num}")
        
        # Create per-iteration filename by copying base file
        iter_path = f"{dir}/file_{MPI.COMM_WORLD.rank}-{MPI.COMM_WORLD.size}_iter{iter_num}.bat"
        import shutil
        shutil.copy(path, iter_path)
        
        operation_time = Timer()
        operation_time.resume_time()
        f = open(iter_path, "r+b")
        operation_time.pause_time()
        
        for i in range(num_operations):
            read_size = operation_sizes[i] if use_distribution else transfer_size
            
            operation_time.resume_time()
            data = f.read(read_size)
            operation_time.pause_time()
            assert len(data) > 0
            assert read_size == len(data)

        operation_time.resume_time()
        f.close()
        operation_time.pause_time()
        
        total_time = MPI.COMM_WORLD.allreduce(operation_time.elapsed_time, op=MPI.SUM)
        if MPI.COMM_WORLD.rank == 0:
            iteration_times.append(total_time)
            logging.info(f"Iteration {iter_num} time: {total_time}")
        
        MPI.COMM_WORLD.barrier()
        # Clean up iteration file to avoid caching
        if os.path.exists(iter_path):
            os.remove(iter_path)
    
    if MPI.COMM_WORLD.rank == 0:
        sorted_times = sorted(iteration_times)
        min_time = min(iteration_times)
        max_time = max(iteration_times)
        
        # Calculate median
        if len(iteration_times) % 2 == 0:
            median_time = (sorted_times[len(iteration_times) // 2 - 1] + sorted_times[len(iteration_times) // 2]) / 2.0
        else:
            median_time = sorted_times[len(iteration_times) // 2]
        
        # Calculate 25th percentile
        p25_idx = int((len(iteration_times) - 1) * 0.25)
        p25_time = sorted_times[p25_idx]
        
        # Calculate 75th percentile
        p75_idx = int((len(iteration_times) - 1) * 0.75)
        p75_time = sorted_times[p75_idx]
        
        # Calculate mean and std_dev for values within [p25, p75]
        values_within = sorted_times[p25_idx:p75_idx+1]
        mean_within = statistics.mean(values_within)
        std_dev_within = statistics.stdev(values_within) if len(values_within) > 1 else 0.0
        
        # Get log file stats if DFTRACER_LOG_FILE is set
        size_mb = 0.0
        num_events = 0
        log_file = os.environ.get("DFTRACER_LOG_FILE")
        if log_file:
            # Find all files matching the log file pattern
            log_dir = os.path.dirname(log_file)
            log_prefix = os.path.basename(log_file)
            try:
                matching_files = glob.glob(os.path.join(log_dir, f"{log_prefix}*"))
                if matching_files:
                    # Calculate total size in MB
                    total_size = sum(os.path.getsize(f) for f in matching_files if os.path.isfile(f))
                    size_mb = total_size / (1024.0 * 1024.0)
                    
                    # Count total lines (events) using zcat/cat with wc -l
                    for log_f in matching_files:
                        try:
                            result = subprocess.run(f"zcat {log_f} 2>/dev/null || cat {log_f}", 
                                                  shell=True, capture_output=True, text=True)
                            if result.stdout:
                                num_events += len(result.stdout.strip().split('\n'))
                        except:
                            pass
            except Exception as e:
                logging.warning(f"Failed to get log file stats: {e}")
        
        print(f"scale,ops,ts,min,p25,median,p75,max,mean,std_dev,distribution,size_mb,num_events")
        print(f"{MPI.COMM_WORLD.size},{num_operations},{transfer_size},{min_time},{p25_time},{median_time},{p75_time},{max_time},{mean_within},{std_dev_within},{'yes' if use_distribution else 'no'},{size_mb},{num_events}")
    MPI.COMM_WORLD.barrier()
    log_inst.finalize()

if __name__ == "__main__":
    argc = len(sys.argv)
    argv = sys.argv
    main(argc, argv)