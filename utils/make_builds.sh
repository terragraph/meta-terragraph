#!/bin/bash
ABS_OEROOT="$(dirname "$(dirname "$(realpath "$0")")")"
# shellcheck disable=SC1091
source /opt/rh/devtoolset-7/enable
# shellcheck disable=SC1090
source "$ABS_OEROOT/utils/check_build_ver.sh"

EXTRA_BITBAKE_ARGS=""
if [ "$1" == "--allow-all-networks" ];
then
  echo "Allowing bitbake to access all networks"
  EXTRA_BITBAKE_ARGS="-R $ABS_OEROOT/facebook/conf/allow-all-networks.conf"
  # shellcheck disable=SC1090
  source "$ABS_OEROOT/facebook/utils/fb-proxy-settings.sh"
fi

echo ----
PATH=$(echo "$PATH" | sed -e 's/:.:/:/g' | sed -e 's/::/:/g')
export PATH
stty -isig
tempfile=$(mktemp)
if [ ! -d "$ABS_OEROOT/yocto/poky" ]; then
  cd "$ABS_OEROOT" || exit 42
  ./sync_yocto.sh
fi
time (
  cd "$ABS_OEROOT"
  # shellcheck disable=SC1091
  source tg-init-build-env meta-qca build-qca;
  # shellcheck disable=SC2086
  bitbake $EXTRA_BITBAKE_ARGS terragraph-image
) || rm -f "$tempfile" &
time (
  cd "$ABS_OEROOT"
  # shellcheck disable=SC1091
  source tg-init-build-env meta-x86 build-x86;
  # shellcheck disable=SC2086
  bitbake $EXTRA_BITBAKE_ARGS e2e-image
) || rm -f "$tempfile" &
wait
stty isig
echo ----

if [[ -f "$tempfile" ]]; then
  echo "Build success"
else
  echo "Failures while building targets"
fi
rm -f "$tempfile"
check_all_build_vers
exit $?
