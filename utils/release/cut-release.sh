#!/bin/sh

# ------------------------------------------------------------------------------
# cut-release.sh <commitid>
#
# This makes the release branch and appropriate tags at the <commitid>,
# as well as copies the E2E config files for the next release.
# However it does not push anything.
# ------------------------------------------------------------------------------

COMMIT=$1
MAIN_BRANCH="main"

git pull || exit

# ------------------------------------------------------------------------------
# On input commit:
# - Parse VERSION file
# - Store E2E config files
# - Create tag "RELEASE_M<nextver>_PRE"
# ------------------------------------------------------------------------------

git checkout "${COMMIT}" || exit

VER="$(sed s/RELEASE_M// VERSION | sed s/_PRE//)"
NEXTVER=$((VER + 1))

confloc="src/terragraph-e2e/e2e/config/base_versions"
conflochw="${confloc}/hw_versions/NXP"
oldconfig="RELEASE_M${VER}.json"
newconfig="RELEASE_M${NEXTVER}.json"

tmpdir=$(mktemp -d)
cp "${confloc}/${oldconfig}" "${tmpdir}"
mkdir "${tmpdir}/hw"
cp "${conflochw}/${oldconfig}" "${tmpdir}/hw"

git tag -a "RELEASE_M${NEXTVER}_PRE" -m "Create M${NEXTVER} prerelease tag."

# ------------------------------------------------------------------------------
# On new branch "releases/m<ver>":
# - Commit new "VERSION" file ("RELEASE_M<ver>")
# - Create tag "RELEASE_M<ver>"
# ------------------------------------------------------------------------------

git checkout -b "releases/m${VER}"
echo "RELEASE_M${VER}" > VERSION
git add VERSION
git commit -m "Bump VERSION to M${VER}"
git tag -a "RELEASE_M${VER}" -m "Create M${VER} release tag."

# ------------------------------------------------------------------------------
# On main branch:
# - Commit new E2E config files and "VERSION" file ("RELEASE_M<nextver>_PRE")
# ------------------------------------------------------------------------------

git checkout "${MAIN_BRANCH}" || exit

git mv "${confloc}/${oldconfig}" "${confloc}/${newconfig}"
cp "${tmpdir}/${oldconfig}" "${confloc}/${oldconfig}"
git mv "${conflochw}/${oldconfig}" "${conflochw}/${newconfig}"
cp "${tmpdir}/hw/${oldconfig}" "${conflochw}/${oldconfig}"
echo "RELEASE_M${NEXTVER}_PRE" > VERSION
git add VERSION "${confloc}/${oldconfig}" "${conflochw}/${oldconfig}"
git commit -m "Bump VERSION and configs for M${NEXTVER} prerelease."

# ------------------------------------------------------------------------------
# Go to new branch "releases/m<ver>"
# ------------------------------------------------------------------------------

git checkout "releases/m${VER}"
