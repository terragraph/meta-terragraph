#!/bin/sh
# Builds the apidoc for the project

# This script takes one optional argument (absolute output html directory)
OUTPUT_PATH="${1:-./src/terragraph-api/apidoc/}"

# Go into meta-terragraph directory
cd "${0%/*}" && cd ../../ || exit 1

# Install nodejs locally, since the version on devservers is too old.
NODEJS_INSTALL_SCRIPT="./docs/build/doc_utils.sh"
if [ -f "$NODEJS_INSTALL_SCRIPT" ]; then
  # shellcheck disable=SC1090
  . "$NODEJS_INSTALL_SCRIPT"
  install_nodejs
  export PATH="$LOCAL_NODEJS_DIR/bin:$PATH"

  # Install apidoc
  install_apidoc
fi
if [ ! -x "$(command -v apidoc)" ]; then
  echo "ERROR: apidoc command not found."
  exit 1
fi

# Run apidoc command
echo "Building apidoc..."
apidoc \
  -i ./src/ \
  -c ./src/terragraph-api/ \
  -o "${OUTPUT_PATH}" \
  -f 'ApiClient.cpp' \
  -f 'StreamApiClient.cpp' \
  -f 'Controller.thrift' \
  -f 'Topology.thrift' \
  -f 'BWAllocation.thrift' \
  -f 'Aggregator.thrift'
