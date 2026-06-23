#!/bin/bash
#SBATCH -J hip_trace_test
#SBATCH -A all
#SBATCH -t 00:30:00
#SBATCH --qos=pdebug
#SBATCH -N 1
#SBATCH -n 1
#SBATCH --constraint=MI300A

set +u

export HOME=/p/vast1/marathe1/dldl
export VAST=$HOME/dldl/dftrcer-omnistat
export OUTPUT_DIR=$VAST/trace-output
export CONFIG_DIR=$VAST/config
export OUTPUT_FILE=$OUTPUT_DIR/hip_trace_$(date +%Y%m%d_%H%M%S).pfw
export CONFIG_FILE=$CONFIG_DIR/hip_trace.json

mkdir -p "$OUTPUT_DIR" "$CONFIG_DIR"

# ROCm paths
export ROCM_PATH=/opt/rocm-6.4.3
export HIP_PATH=$ROCM_PATH/hip
export PATH=$ROCM_PATH/bin:$PATH
export LD_LIBRARY_PATH=/opt/cray/pe/lib64/cce:/opt/cray/pe/lib64
export LD_LIBRARY_PATH=$ROCM_PATH/lib:$ROCM_PATH/libexec/rocprofiler-sdk:$HIP_PATH/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/usr/WS2/marathe1/dldl/dftracer-omnistat/build-omnistat-hip/lib64:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/usr/WS2/marathe1/dldl/dftracer-omnistat/.local-hip/lib64:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/usr/WS2/marathe1/dldl/dftracer-omnistat/dependency/lib64:$LD_LIBRARY_PATH

export PYTHONPATH=/usr/WS2/marathe1/dldl/dftracer-omnistat/build-omnistat-hip/lib64:$PYTHONPATH
export PYTHONPATH=/usr/WS2/marathe1/dldl/dftracer-omnistat/src/dftracer/python:$PYTHONPATH

echo "=== Environment ==="
echo "HIP_PATH=$HIP_PATH"
echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"

echo "=== ROCm devices ==="
hipInfo 2>&1 | head -40 || echo "hipInfo not found"

echo "=== GPU device nodes ==="
ls /dev/dri/ /dev/char/ 2>/dev/null || echo "No /dev/dri or /dev/char"

echo "=== Running DFTracer HIP trace test ==="

cd /usr/WS2/marathe1/dldl/dftracer-omnistat/build-omnistat-hip

python3 -c "
import sys
sys.path.insert(0, '/usr/WS2/marathe1/dldl/dftracer-omnistat/build-omnistat-hip/lib64')
sys.path.insert(0, '/usr/WS2/marathe1/dldl/dftracer-omnistat/src/dftracer/python')
import dftracer

print('Initializing DFTracer...')
dftracer.initialize('$OUTPUT_FILE', None, -1)
print('DFTracer initialized')

# Log synthetic HIP trace events to simulate GPU telemetry
dftracer.log_event('hipLaunchKernel', 'HIP_RUNTIME_API', 1000, 500,
    string_args={'kernel_name': (1, 'matrixMul'), 'hip_function': (1, 'hipLaunchKernel')})
dftracer.log_event('hipMemcpy', 'HIP_RUNTIME_API', 2000, 200,
    string_args={'kernel_name': (1, 'memcpy_d2h'), 'hip_function': (1, 'hipMemcpy')})
dftracer.log_event('hipMemAlloc', 'HIP_RUNTIME_API', 2500, 50,
    string_args={'kernel_name': (1, 'alloc'), 'hip_function': (1, 'hipMemAlloc')})
dftracer.log_event('page_migration_page_migrate_start', 'PAGE_MIGRATION', 3000, 100,
    string_args={'start_addr': (1, '0x7f0000'), 'end_addr': (1, '0x7f1000'), 'trigger': (1, 'normal')})
dftracer.log_event('page_migration_page_fault_start', 'PAGE_MIGRATION', 3500, 50,
    string_args={'agent_id': (1, '0'), 'address': (1, '0x7f0000'), 'read_fault': (1, '0')})
dftracer.log_event('page_migration_queue_eviction', 'PAGE_MIGRATION', 4000, 30,
    string_args={'agent_id': (1, '1'), 'trigger': (1, 'normal')})

print('Events logged')
dftracer.finalize()
print('Finalized. Output: $OUTPUT_FILE')
" 2>&1

echo "=== Test Output ==="
ls -la "$OUTPUT_DIR"/*.pfw "$OUTPUT_DIR"/*.csv 2>/dev/null || echo "No output files found"

echo "=== Done ==="
