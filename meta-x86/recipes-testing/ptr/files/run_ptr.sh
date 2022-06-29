#!/bin/bash

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

REQUIREMENTS="\
black \
coverage \
flake8 \
flake8-bugbear \
flake8-comprehensions \
mypy \
ptr"

VENV_NAME="tg_ptr"
VENV_PATH="/tmp/$VENV_NAME"
if [ "$TEMP" != "" ]; then
  VENV_PATH="$TEMP/$VENV_NAME"
fi

mkdir -p /tmp/home
export HOME="/tmp/home"

# Clear, create venv with access to image's system site-packages
python3 -m venv --system-site-packages "$VENV_PATH"
# shellcheck disable=1090
. "$VENV_PATH/bin/activate"

# Point pip @ mirror if PYPI_DOMAIN env var set
# We will not SSL verify it and implicitly trust that domain
# Example: PYPI_DOMAIN=pypi.org
TRUSTED_HOST=""
if [ "$PYPI_DOMAIN" != "" ]; then
  echo -e "[global]
index-url = https://${PYPI_DOMAIN}/simple
timeout = 10" > "${VENV_PATH}/pip.conf"
  TRUSTED_HOST="--trusted-host ${PYPI_DOMAIN}"
fi

pip install --upgrade pip setuptools
# shellcheck disable=2086
pip $TRUSTED_HOST install --upgrade $REQUIREMENTS

base_dir="repo/"
if [ "$1" != "" ]
then
  base_dir="${base_dir}${1}"
fi

ptr -b "$base_dir" --venv "$VENV_PATH" --stats-file "/tmp/ptr_stats_$$"
exit $?
