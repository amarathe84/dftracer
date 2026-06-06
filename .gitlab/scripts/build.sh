#!/bin/bash

LOG_FILE="$PWD/build.log"

# Clear or initialize the log file
> "$LOG_FILE"

echo "Running build.sh on $(hostname)" | tee -a "$LOG_FILE"

# Resolve the python site-packages path safely
export site=$(ls -d "${CUSTOM_CI_ENV_DIR}/$ENV_NAME"/lib/python*/site-packages/ 2>>"$LOG_FILE")

# Verify that $site was found to prevent dangerous 'rm -rf /*' behavior
if [ -z "$site" ]; then
    echo "Error: Could not determine python site-packages directory." | tee -a "$LOG_FILE"
    exit 1
fi

echo "Remove preinstall version of dlio_benchmark" | tee -a "$LOG_FILE"
echo "Command: pip uninstall dlio_benchmark" | tee -a "$LOG_FILE"
if ! pip uninstall -y dlio_benchmark >>"$LOG_FILE" 2>&1; then
    echo "Failed to uninstall dlio_benchmark. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

if ! rm -rf "$site"/*dlio_benchmark* >>"$LOG_FILE" 2>&1; then
    echo "Failed to remove dlio_benchmark files. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

echo "Installing DLIO benchmark with url git+$DLIO_BENCHMARK_REPO@$DLIO_BENCHMARK_TAG" | tee -a "$LOG_FILE"
echo "Command: pip install --no-cache-dir git+$DLIO_BENCHMARK_REPO@$DLIO_BENCHMARK_TAG" | tee -a "$LOG_FILE"
if ! pip install --no-cache-dir "git+$DLIO_BENCHMARK_REPO@$DLIO_BENCHMARK_TAG" >>"$LOG_FILE" 2>&1; then
    echo "Failed to install DLIO benchmark. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

echo "Remove preinstall version of dftracer" | tee -a "$LOG_FILE"
echo "Command: pip uninstall dftracer" | tee -a "$LOG_FILE"
if ! pip uninstall -y dftracer >>"$LOG_FILE" 2>&1; then
    echo "Failed to uninstall dftracer. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

if ! rm -rf "$site"/*dftracer* >>"$LOG_FILE" 2>&1; then
    echo "Failed to remove dftracer files. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

echo "Installing DFTracer" | tee -a "$LOG_FILE"
echo "Command: pip install --no-cache-dir --force-reinstall git+${DFTRACER_REPO}@${CI_COMMIT_REF_NAME}" | tee -a "$LOG_FILE"
if ! pip install --no-cache-dir --force-reinstall "git+${DFTRACER_REPO}@${CI_COMMIT_REF_NAME}" >>"$LOG_FILE" 2>&1; then
    echo "Failed to install DFTracer. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

echo "Install gitlab requirements" | tee -a "$LOG_FILE"
echo "Command: pip install -r .gitlab/scripts/requirements.txt" | tee -a "$LOG_FILE"
if ! pip install -r .gitlab/scripts/requirements.txt >>"$LOG_FILE" 2>&1; then
    echo "Failed to install gitlab requirements. Showing log contents:" | tee -a "$LOG_FILE"
    cat "$LOG_FILE"
    exit 1
fi

python -c "import dftracer; import dftracer.python; print(dftracer.__version__);"
export PATH="$site/dftracer/bin:$PATH"