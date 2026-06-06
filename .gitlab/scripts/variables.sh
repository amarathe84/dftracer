#!/bin/bash
hostname=$(hostname)
clean_hostname="${hostname//[0-9]/}"
export CUSTOM_CI_ENV_DIR=$(eval "echo ${CUSTOM_CI_ENV_DIR}-${clean_hostname}")
export CUSTOM_CI_OUTPUR_DIR=$(eval "echo ${CUSTOM_CI_OUTPUR_DIR}-${clean_hostname}")
export DATA_PATH=$(eval "echo $DATA_PATH")
export PROJECT_PATH=$PWD