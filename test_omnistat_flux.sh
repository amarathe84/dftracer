#!/bin/bash
#
# Test script for DFTracer omnistat + HIP tracing integration
# Runs on a pdebug flux node with AMD MI300A GPU
#
# Usage:
#   flux submit --queue=pdebug --time-limit=30m --job-name=omnistat_test \
#     /usr/WS2/marathe1/dldl/dftracer-omnistat/test_omnistat_flux.sh
#

set +u

export HOME=/p/vast1/marathe1/dldl
export VAST=$HOME/dldl/dftrcer-omnistat
export OUTPUT_DIR=$VAST/trace-output
export CONFIG_DIR=$VAST/config
export OUTPUT_FILE=$OUTPUT_DIR/omnistat_hip_test.pfw
export CONFIG_FILE=$CONFIG_DIR/omnistat_test.json

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

echo "=== Running DFTracer omnistat + HIP tracing test ==="

cd /usr/WS2/marathe1/dldl/dftracer-omnistat/build-omnistat-hip

python3 -c "
import sys
import os

sys.path.insert(0, '/usr/WS2/marathe1/dldl/dftracer-omnistat/build-omnistat-hip/lib64')
sys.path.insert(0, '/usr/WS2/marathe1/dldl/dftracer-omnistat/src/dftracer/python')

import dftracer

print('=== Test 1: Basic DFTracer initialization ===')
dftracer.initialize('$OUTPUT_FILE', None, -1)
print('DFTracer initialized')

print('=== Test 2: Log HIP runtime API events ===')
# hipLaunchKernel
dftracer.log_event('hipLaunchKernel', 'HIP_RUNTIME_API', 1000, 500,
    string_args={'kernel_name': (1, 'matrixMul'), 'hip_function': (1, 'hipLaunchKernel')})
print('  hipLaunchKernel logged')

# hipMemcpy
dftracer.log_event('hipMemcpy', 'HIP_RUNTIME_API', 2000, 200,
    string_args={'kernel_name': (1, 'memcpy_d2h'), 'hip_function': (1, 'hipMemcpy')})
print('  hipMemcpy logged')

# hipMemAlloc
dftracer.log_event('hipMemAlloc', 'HIP_RUNTIME_API', 2500, 50,
    string_args={'kernel_name': (1, 'alloc'), 'hip_function': (1, 'hipMemAlloc')})
print('  hipMemAlloc logged')

print('=== Test 3: Log HIP kernel dispatch events ===')
dftracer.log_event('kernel_0_matrixMul', 'HIP_KERNEL_DISPATCH', 3000, 100,
    string_args={'correlation_id': (1, '12345'), 'tid': (1, '42')})
print('  kernel dispatch logged')

print('=== Test 4: Log HIP memory copy events ===')
dftracer.log_event('hipMemcpy', 'HIP_MEMORY_COPY', 4000, 100,
    string_args={'src_agent': (1, '0'), 'dst_agent': (1, '1'),
                 'correlation_id': (1, '67890'), 'tid': (1, '42')})
print('  memory copy logged')

print('=== Test 5: Log page migration events ===')
dftracer.log_event('page_migration_page_migrate_start', 'PAGE_MIGRATION', 5000, 50,
    string_args={'start_addr': (1, '0x7f0000'), 'end_addr': (1, '0x7f1000'),
                 'trigger': (1, 'normal')})
print('  page migration start logged')

dftracer.log_event('page_migration_page_fault_start', 'PAGE_MIGRATION', 5500, 30,
    string_args={'agent_id': (1, '0'), 'address': (1, '0x7f0000'),
                 'read_fault': (1, '0')})
print('  page fault start logged')

dftracer.log_event('page_migration_queue_eviction', 'PAGE_MIGRATION', 6000, 20,
    string_args={'agent_id': (1, '1'), 'trigger': (1, 'normal')})
print('  queue eviction logged')

print('=== Test 6: Log HSA API events ===')
dftracer.log_event('hsaQueueSubmit', 'HSA_API', 7000, 10,
    string_args={'correlation_id': (1, '11111'), 'tid': (1, '42')})
print('  HSA API logged')

print('=== Test 7: Log RCCL API events ===')
dftracer.log_event('rcclBarrier', 'RCCL_API', 8000, 5,
    string_args={'correlation_id': (1, '22222'), 'tid': (1, '42')})
print('  RCCL API logged')

print('=== Test 8: Log metadata event ===')
dftracer.log_metadata_event('app_name', 'omnistat_hip_test')
dftracer.log_metadata_event('gpu_device', 'MI300A')
print('  metadata logged')

print('=== Test 9: Finalize ===')
dftracer.finalize()
print('Finalized. Output: $OUTPUT_FILE')
" 2>&1

echo "=== Test Output ==="
ls -la "$OUTPUT_DIR"/*.pfw "$OUTPUT_DIR"/*.csv 2>/dev/null || echo "No output files found"
ls -la /usr/WS2/marathe1/dldl/dftracer-omnistat/build-omnistat-hip/*.pfw 2>/dev/null || echo "No build dir output"

echo "=== Done ==="
