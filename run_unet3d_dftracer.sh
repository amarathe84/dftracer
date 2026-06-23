#!/bin/bash
# Run dlio_benchmark with dftracer + Omnistat for Unet3D workload
# This script:
#  1. Sets up the dftracer Python environment with Omnistat support
#  2. Generates training data (if not already present)
#  3. Launches dlio_benchmark with dftracer tracing enabled
#  4. Outputs trace files to /p/vast1/marathe1/dldl/dftracer-omnistat/
#
# Usage:
#   ./run_unet3d_dftracer.sh          # Interactive (salloc first)
#   ./run_unet3d_dftracer.sh --sbatch # Submit as batch job to pdebug

set -euo pipefail

WORKSPACE="/usr/WS2/marathe1/dldl/dftracer-omnistat"
DLIO_DIR="${WORKSPACE}/dlio_benchmark"
OUTPUT_DIR="/p/vast1/marathe1/dldl/dftracer-omnistat"
DATA_DIR="${OUTPUT_DIR}/data/unet3d"
CHECKPOINT_DIR="${OUTPUT_DIR}/checkpoints/unet3d"

# dftracer enable
export DFTRACER_ENABLE=1

# ---- Data generation (one-time setup) ----
if [ ! -d "${DATA_DIR}" ]; then
    echo "==> Generating Unet3D training data at ${DATA_DIR}"
    mkdir -p "${DATA_DIR}"
    cd "${DLIO_DIR}"
    python3 -c "
import sys, os
sys.path.insert(0, 'dlio_benchmark')
from dlio_benchmark.data_generator.generator_factory import GeneratorFactory
from dlio_benchmark.utils.config import ConfigArguments, LoadConfig
from dlio_benchmark.common.enumerations import FormatType, StorageType

args = ConfigArguments.get_instance()
cfg = {
    'framework': 'pytorch',
    'workflow': {'generate_data': True, 'train': False},
    'dataset': {
        'data_folder': '${DATA_DIR}',
        'format': 'npz',
        'num_files_train': 168,
        'num_samples_per_file': 1,
        'record_length_bytes': 146600628,
        'record_length_bytes_stdev': 68341808,
        'record_length_bytes_resize': 2097152,
        'num_subfolders_train': 2,
    },
    'model': {'name': 'unet3d', 'model_size_bytes': 499153191},
    'reader': {'data_loader': 'pytorch', 'batch_size': 7, 'read_threads': 4},
    'storage_type': 'local_fs',
}
LoadConfig(args, cfg)
args.derive_configurations([], [])
args.validate()
gen = GeneratorFactory.get_generator(FormatType.NPZ)
gen.generate()
print('Data generation complete.')
"
    echo "==> Unet3D data generated at ${DATA_DIR}"
else
    echo "==> Data already exists at ${DATA_DIR}, skipping generation"
fi

mkdir -p "${CHECKPOINT_DIR}"

# ---- Build the command ----
HYDRA_OVERRIDES="hydra.run.dir=${OUTPUT_DIR}/hydra_log/unet3d_dftracer"
WORKLOAD_OVERRIDE="workload=unet3d_dftracer"
EXTRA_ARGS="output.folder=${OUTPUT_DIR}/hydra_log/unet3d_dftracer/traces"

CMD="dlio_benchmark ${HYDRA_OVERRIDES} ${WORKLOAD_OVERRIDE} ${EXTRA_ARGS}"

echo ""
echo "============================================================"
echo " dlio_benchmark Unet3D with dftracer + Omnistat"
echo "============================================================"
echo " DFTRACER_ENABLE   = ${DFTRACER_ENABLE}"
echo " Output directory    = ${OUTPUT_DIR}/hydra_log/unet3d_dftracer"
echo " Data directory      = ${DATA_DIR}"
echo " Trace files will be: ${OUTPUT_DIR}/hydra_log/unet3d_dftracer/traces/trace-*.pfw"
echo "============================================================"
echo ""
echo " Command:"
echo "  ${CMD}"
echo ""
echo " To run interactively (after salloc):"
echo "  ${CMD}"
echo ""
echo " To submit as batch job to pdebug:"
echo "  ${WORKSPACE}/run_unet3d_dftracer.sh --sbatch"
echo "============================================================"
echo ""

# ---- Batch submission mode ----
if [[ "${1:-}" == "--sbatch" ]]; then
    echo "==> Submitting batch job to pdebug queue..."
    sbatch \
        --account=dlio \
        --partition=pdebug \
        --nodes=1 \
        --ntasks=1 \
        --cpus-per-task=16 \
        --time=02:00:00 \
        --job-name=dlion-unet3d-dftracer \
        --output="${OUTPUT_DIR}/logs/unet3d-%j.out" \
        --error="${OUTPUT_DIR}/logs/unet3d-%j.err" \
        --wrap="DFTRACER_ENABLE=1 dlio_benchmark hydra.run.dir=${OUTPUT_DIR}/hydra_log/unet3d_dftracer workload=unet3d_dftracer output.folder=${OUTPUT_DIR}/hydra_log/unet3d_dftracer/traces"
    echo "==> Job submitted."
    exit 0
fi

# ---- Interactive mode ----
echo "To run now (after 'salloc -p pdebug'):  ${CMD}"
echo ""
echo "Waiting for user input..."
read -r -p "Run now? [y/N]: " answer
if [[ "${answer}" == [yY] ]]; then
    eval ${CMD}
else
    echo "Aborted."
fi
