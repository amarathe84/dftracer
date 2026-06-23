#!/bin/bash
# Run dlio_benchmark with dftracer + Omnistat for BERT workload
# This script:
#  1. Sets up the dftracer Python environment with Omnistat support
#  2. Generates training data (if not already present)
#  3. Launches dlio_benchmark with dftracer tracing enabled
#  4. Outputs trace files to /p/vast1/marathe1/dldl/dftracer-omnistat/
#
# Usage:
#   ./run_bert_dftracer.sh          # Interactive (salloc first)
#   ./run_bert_dftracer.sh --sbatch # Submit as batch job to pdebug

set -euo pipefail

WORKSPACE="/usr/WS2/marathe1/dldl/dftracer-omnistat"
DLIO_DIR="${WORKSPACE}/dlio_benchmark"
OUTPUT_DIR="/p/vast1/marathe1/dldl/dftracer-omnistat"
DATA_DIR="${OUTPUT_DIR}/data/bert"
CHECKPOINT_DIR="${OUTPUT_DIR}/checkpoints/bert"

# dftracer enable
export DFTRACER_ENABLE=1

# ---- Data generation (one-time setup) ----
if [ ! -d "${DATA_DIR}" ]; then
    echo "==> Generating BERT training data at ${DATA_DIR}"
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
    'framework': 'tensorflow',
    'workflow': {'generate_data': True, 'train': False},
    'dataset': {
        'data_folder': '${DATA_DIR}',
        'format': 'tfrecord',
        'num_files_train': 500,
        'num_samples_per_file': 313532,
        'record_length_bytes': 2500,
        'file_prefix': 'part',
    },
    'model': {'name': 'bert', 'model_size_bytes': 4034713312},
    'reader': {'data_loader': 'tensorflow', 'batch_size': 48, 'read_threads': 1},
    'storage_type': 'local_fs',
}
LoadConfig(args, cfg)
args.derive_configurations([], [])
args.validate()
gen = GeneratorFactory.get_generator(FormatType.TFRECORD)
gen.generate()
print('Data generation complete.')
"
    echo "==> BERT data generated at ${DATA_DIR}"
else
    echo "==> Data already exists at ${DATA_DIR}, skipping generation"
fi

mkdir -p "${CHECKPOINT_DIR}"

# ---- Build the command ----
HYDRA_OVERRIDES="hydra.run.dir=${OUTPUT_DIR}/hydra_log/bert_dftracer"
WORKLOAD_OVERRIDE="workload=bert_dftracer"
EXTRA_ARGS="output.folder=${OUTPUT_DIR}/hydra_log/bert_dftracer/traces"

CMD="dlio_benchmark ${HYDRA_OVERRIDES} ${WORKLOAD_OVERRIDE} ${EXTRA_ARGS}"

echo ""
echo "============================================================"
echo " dlio_benchmark BERT with dftracer + Omnistat"
echo "============================================================"
echo " DFTRACER_ENABLE   = ${DFTRACER_ENABLE}"
echo " Output directory    = ${OUTPUT_DIR}/hydra_log/bert_dftracer"
echo " Data directory      = ${DATA_DIR}"
echo " Trace files will be: ${OUTPUT_DIR}/hydra_log/bert_dftracer/traces/trace-*.pfw"
echo "============================================================"
echo ""
echo " Command:"
echo "  ${CMD}"
echo ""
echo " To run interactively (after salloc):"
echo "  ${CMD}"
echo ""
echo " To submit as batch job to pdebug:"
echo "  ${WORKSPACE}/run_bert_dftracer.sh --sbatch"
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
        --job-name=dlion-bert-dftracer \
        --output="${OUTPUT_DIR}/logs/bert-%j.out" \
        --error="${OUTPUT_DIR}/logs/bert-%j.err" \
        --wrap="DFTRACER_ENABLE=1 dlio_benchmark hydra.run.dir=${OUTPUT_DIR}/hydra_log/bert_dftracer workload=bert_dftracer output.folder=${OUTPUT_DIR}/hydra_log/bert_dftracer/traces"
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
