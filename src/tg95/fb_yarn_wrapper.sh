#!/bin/sh

# Go into the current script directory
cd "${0%/*}" || exit 1

# Install nodejs locally, since the version on devservers is too old.
NODEJS_INSTALL_SCRIPT="../../docs/build/doc_utils.sh"
if [ -f "$NODEJS_INSTALL_SCRIPT" ]; then
    # shellcheck disable=SC1090
    . "$NODEJS_INSTALL_SCRIPT"
    install_nodejs
    export PATH="$LOCAL_NODEJS_DIR/bin:$PATH"
fi

if [ "$#" -eq 1 ] && [ "$1" = "build" ]; then
    yarn install
    yarn build

    # Delete everything except required JS files
    rm -vf ./build/static/js/*.js.*
else
    export HOST=::
    export PORT=8080
    yarn "$@"
fi
