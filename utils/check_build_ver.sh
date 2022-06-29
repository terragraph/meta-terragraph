#!/bin/bash

ABS_OEROOT=${ABS_OEROOT:-$(dirname "$(dirname "$(realpath "$0")")")}
# Prefer e2e-image but use terragraph-image-x86 unit test image if available.
x86_img=${x86_img:-"$ABS_OEROOT/build-x86/tmp/deploy/images/tgx86/e2e-image-tgx86.tar.gz"}
if [ ! -f "$x86_img" ]; then
  x86_img="$ABS_OEROOT/build-x86/tmp/deploy/images/tgx86/terragraph-image-x86-tgx86.tar.gz"
fi
qca_img=${qca_img:-"$ABS_OEROOT/build-qca/tmp/deploy/images/qoriq/tg-update-qoriq.bin"}

ver_x86() {
  ver=$(tar -xOf "$x86_img" ./usr/lib/os-release 2> /dev/null | grep VERSION= | sed 's/VERSION="//' | sed 's/"$//')
  echo "$ver"
}

ver_qca() {
  long_ver=$("$qca_img" -m 2>&1 | grep '^"version":')
  short_ver=$(echo "$long_ver" | sed "s/^.*Release \\([^\\ ]*\\) .*/\\1/")
  echo "$short_ver"
}

check_all_build_vers() {
  echo "Checking all build versions..."
  ver_x86=$(ver_x86);     echo "X86   = $ver_x86"
  ver_qca=$(ver_qca);     echo "Qoriq   = $ver_qca"

  if [[ "$ver_x86" == "" ]] || [[ "$ver_qca" == "" ]]
  then
    echo "Build missing"
    return 1
  elif [[ "$ver_qca" != "$ver_x86" ]]
  then
    echo "Build mismatch"
    return 255
  else
    echo "Builds match"
    return 0
  fi
}

# Iff script is called standalone, then
# execute function provided or default to the main routine
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  if [[ "$#" -ge 1 ]]; then
    $@
  else
    check_all_build_vers
  fi
fi
