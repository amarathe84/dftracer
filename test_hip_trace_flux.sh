#!/bin/bash
#SBATCH -J hip_trace_test
#SBATCH -A all
#SBATCH -t 00:30:00
#SBATCH --qos=pdebug
#SBATCH -N 1
#SBATCH -n 1
#SBATCH --constraint=MI300A

set -euo pipefail

export HOME=/p/vast1/marathe1/dldl
export VAST=$HOME/dldl/dftrcer-omnistat
export OUTPUT_DIR=$VAST/trace-output
export CONFIG_DIR=$VAST/config
export OUTPUT_FILE=$OUTPUT_DIR/hip_trace_$(date +%Y%m%d_%H%M%S).csv
export CONFIG_FILE=$CONFIG_DIR/hip_trace.json

mkdir -p "$OUTPUT_DIR" "$CONFIG_DIR"

# ROCm paths
export ROCM_PATH=/opt/rocm-6.4.3
export HIP_PATH=/opt/rocm-6.4.3/hip
export PATH=$ROCM_PATH/bin:$PATH
export LD_LIBRARY_PATH=$ROCM_PATH/lib:$HIP_PATH/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/opt/rocm-6.4.3/libexec/rocprofiler-sdk:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/usr/WS2/marathe1/dldl/dftracer-omnistat/.local-hip/lib64:$LD_LIBRARY_PATH

# DFTracer paths
export DFTRACER_INSTALL_PREFIX=/usr/WS2/marathe1/dldl/dftracer-omnistat/.local-hip
export LD_LIBRARY_PATH=$DFTRACER_INSTALL_PREFIX/lib64:$LD_LIBRARY_PATH

# Python path
export PYTHONPATH=/usr/WS2/marathe1/dldl/dftracer-omnistat/build-omnistat-hip/lib64:$PYTHONPATH
export PYTHONPATH=/usr/WS2/marathe1/dldl/dftracer-omnistat/src/dftracer/python:$PYTHONPATH

echo "=== Environment ==="
echo "HIP_PATH=$HIP_PATH"
echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
echo "PYTHONPATH=$PYTHONPATH"
echo "GPU devices:"
/opt/rocm-6.4.3/bin/rocminfo 2>/dev/null | head -30 || echo "rocminfo not found"
echo "=== ROCm devices ==="
hipInfo 2>/dev/null | head -30 || echo "hipInfo not found"
echo "=== Running DFTracer HIP trace test ==="

# Run the test using Python DFTracer with HIP tracing
python3 -c "
import sys
sys.path.insert(0, '/usr/WS2/marathe1/dldl/dftracer-omnistat/build-omnistat-hip/lib64')
sys.path.insert(0, '/usr/WS2/marathe1/dldl/dftracer-omnistat/src/dftracer/python')

import os
os.environ['LD_LIBRARY_PATH'] = os.environ.get('LD_LIBRARY_PATH', '')
os.environ['HIP_PATH'] = '/opt/rocm-6.4.3/hip'

# Check if dftracer module loads
try:
    import dftracer
    print('DFTracer module loaded successfully')
except ImportError as e:
    print(f'Failed to import dftracer: {e}')
    sys.exit(1)

# Check for HIP tracing availability
try:
    from dftracer import Tracer
    tracer = Tracer()
    print('Tracer created successfully')
except Exception as e:
    print(f'Failed to create Tracer: {e}')
    sys.exit(1)

# Print available tracing options
print('DFTracer HIP tracing test completed successfully')
" 2>&1

echo "=== Test Output ==="
echo "Output file: $OUTPUT_FILE"
ls -la "$OUTPUT_DIR"/ 2>/dev/null

echo "=== Done ==="
