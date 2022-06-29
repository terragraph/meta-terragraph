#
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

# This class is used to generate a manifest of recipes used in constructing
# an image, their versions and any patched vulnerabilities (CVEs only at
# present).
#
# To use this class, inherit the class in your "local.conf" file. Inheriting
# this class adds a new task, "fbvuln_track",  to every recipe you build.
# Intended use, however, is not per recipe, but at the image level.
#
# How it works
# ------------
#
# As Bitbake builds your recipes, the "fbvuln_track" task records the recipe
# name (BPN), version (PV), CVE product name (CVE_PRODUCT, defaulting to BPN)
# and cleaned up version (PV up to the first "+" character) to a file in TMPDIR.
# In addition to the package identification data, the task inspects the sources
# of the recipe. Any vulnerability patches are tracked alongside the package
# identification (the extraction code is based on from the Poky
# "cve-check.bbclass").
#
# This class then hooks into image postprocessing, which allows us to run after
# package manifest file generation.
#
# We build a package map from the files in TMPDIR/pkgdata, which maps recipe to
# package name.  We build this in reverse as package name to recipe name.  We
# then augment this data from TMPDIR/pkgdata/runtime to pick up any aliases,
# PROVIDES packages and packages built with Debian library names.
#
# Once we have the package map, we read the image package manifest for the
# packages used in constructing the image and build a set containing the
# recipes used to produce the packages included in the image.
#
# We now iterate through the recipes tracked in the global tracking file that
# was built by the recipes.  We extract the original recipe name and version,
# product name, cleaned up version and the list of vulnerabilities from
# this file.  If the original recipe name is not in the set of recipes used to
# construct the image we discard the record.
#
# We now iterate through all recipes used in constructing the image and create
# a dictionary of CVE product name to vulnerabilities addressed by (cleaned up)
# version.  Where more that one recipe built the same version sources of a
# common CVE product we take the intersection of all patched CVEs as the set of
# patched CVEs.  This means we insist that all variants are patched (use-case:
# consider a custom busybox build for an initrd).
#
# Once we have all of this data, we write out a vulnerability check manifest
# containing the CVE product, cleaned version and a list of addressed
# vulnerabilities as a comma-separated line in the manifest.
#
# It is intended that this manifest file is then associated with your
# project's notion of a release so that you can later query released product
# versions, then get the manifest for those versions and submit them to
# vulnerability analysis.
#
# Usage
# -----
#
# Interit this class in local.conf:
#   INHERIT += "fbvuln-manifest"
#
# Build an image as you normally would:
#   bitbake core-image-minimal
CVE_PRODUCT ??= "${BPN}"
CVE_VERSION ??= "${PV}"
FBVULN_EXCUDE_RECIPE ??= "0"

FBVULN_CHECK_TMP_FILE ?= "${TMPDIR}/fbvuln.tmp"
FBVULN_CHECK_MANIFEST ?= \
    "${DEPLOY_DIR_IMAGE}/${IMAGE_NAME}${IMAGE_NAME_SUFFIX}.fbvuln.csv"
FBVULN_PN_BLACKLIST ??= ""

python do_fbvuln_track () {
    """
    Write a record of the recipe and any patched vulnerabilities
    """

    if (bb.data.inherits_class('nativesdk', d) or
          bb.data.inherits_class('cross-canadian', d) or
          bb.data.inherits_class('native', d)):
        return

    if d.getVar("FBVULN_EXCUDE_RECIPE") == "1":
        return

    if d.getVar("PN") in d.getVar("FBVULN_PN_BLACKLIST").split():
        return

    patched_cves = get_patches_vulns(d)
    vuln_write_data(d, patched_cves)
}

addtask fbvuln_track after do_unpack before do_build
do_fbvuln_track[nostamp] = "1"

python fbvuln_track_cleanup () {
    """
    Delete the file used to gather all the per-recipe vulnerability information.
    """

    bb.utils.remove(e.data.getVar("FBVULN_CHECK_TMP_FILE"))
}

addhandler fbvuln_track_cleanup
fbvuln_track_cleanup[eventmask] = "bb.cooker.CookerExit"

python fbvuln_track_write_manifest () {
    """
    Create vulnerability manifest when building an image
    """

    # Create a database of packages and the recipe they
    # map to.  This is slightly complicated by the fact
    # that some packages are aliases, created by PROVIDES
    # statements in the recipe.
    pkgmap = {}
    for dn in os.listdir(d.getVar("PKGDATA_DIR")):
        p = os.path.join(d.getVar("PKGDATA_DIR"), dn)
        if not os.path.isfile(p):
            continue
        with open(p, 'r') as fh:
            for line in fh:
                if not line.startswith('PACKAGES: '):
                    continue
                for pkg in line[10:].strip().split():
                    pkgmap[pkg] = dn
                    p = os.path.join(d.getVar("PKGDATA_DIR"), 'runtime', pkg)
                    needle = 'PKG_%s: ' % pkg
                    if os.path.isfile(p):
                        with open(p, 'r') as f:
                            for line in f:
                                if not line.startswith(needle):
                                    continue
                                for rpkg in line[len(needle):].strip().split():
                                    pkgmap[rpkg] = dn
                                break
                break

    # Recipes used in constructing the image, based on the
    # packages installed as recorded in the image manifest.
    recipes_used = set()
    pkg_manifest_file = d.getVar('IMAGE_MANIFEST')
    if not os.path.exists(pkg_manifest_file):
        # No manifest file? The image didn't need to be reconstructed, so we
        # need to look for the latest file (the one that the symlink points
        # to.
        pkg_manifest_file = os.path.realpath(
            os.path.join(
                d.getVar("IMGDEPLOYDIR"),
                "%s.manifest" % d.getVar("IMAGE_LINK_NAME")
            )
        )
        # Tragic...
        if not os.path.isfile(pkg_manifest_file):
            bb.fatal('Package manifest not found at %s' % pkg_manifest_file)

    with open(pkg_manifest_file, 'r') as fh:
        for line in fh:
            pkg = line.strip().split()[0]
            if pkg in pkgmap:
                recipes_used.add(pkgmap[pkg])
            else:
                bb.warn("Unmappable package: %s" % pkg)

    if os.path.exists(d.getVar("FBVULN_CHECK_TMP_FILE")):
        bb.note("Writing rootfs CVE manifest")
        deploy_dir = d.getVar("DEPLOY_DIR_IMAGE")
        link_name = d.getVar("IMAGE_LINK_NAME")
        manifest_name = d.getVar("FBVULN_CHECK_MANIFEST")
        fbvuln_tmp_file = d.getVar("FBVULN_CHECK_TMP_FILE")

        opnv = {}
        with open(fbvuln_tmp_file, 'r') as ifh:
            for line in ifh:
                opn, opv, ven, pn, pv, sw, hw, v = line.strip().split(',', 8)
                if len(opn.strip()) == 0:
                    # Infrastructural virtuals, such as packagegroups
                    # have an intentionally empty PN - skip them.
                    continue
                if opn in recipes_used:
                    if opn in opnv:
                        opnv[opn].append((ven, pn, pv, sw, hw, {x.strip() for x in v.split(' ')},))
                    else:
                        opnv[opn] = [(ven, pn, pv, sw, hw, {x.strip() for x in v.split(' ')},)]

        vulns = {}
        for k in opnv:
            for record in opnv[k]:
                try:
                    ven, pn, pv, sw, hw, v = record
                    meta = vulns.get(pn, {})
                    versions = meta.get('versions', {})
                    if pv in versions:
                        versions[pv] = versions[pv].intersection(v)
                    else:
                        versions[pv] = v
                    vulns[pn] = {'ven': ven, 'sw': sw, 'hw': hw, 'versions': versions}
                except ValueError as ve:
                    bb.warn('{}'.format(ve))

        if manifest_name:
            manifest_dir = manifest_name.rsplit('/',1)
            if manifest_dir:
                manifest_dir = manifest_dir[0]
                if not os.path.exists(manifest_dir):
                    os.makedirs(manifest_dir)

        with open(manifest_name, 'w') as ofh:
            for pn in sorted(vulns.keys()):
                meta = vulns[pn]
                ven = meta['ven']
                sw = meta['sw']
                hw = meta['hw']
                versions = meta['versions']
                for pv in sorted(versions.keys()):
                    v = sorted(versions[pv])
                    # vendor,product,version,target_sw,target_hw,patched_vulns
                    ofh.write('{0},{1},{2},{3},{4},{5}\n'.format(ven, pn, pv, sw, hw, ' '.join(v)))

        manifest_link = os.path.join(deploy_dir, "%s.fbvuln.csv" % link_name)
        # If we already have another manifest, update symlinks
        if os.path.exists(os.path.realpath(manifest_link)):
            os.remove(manifest_link)
        os.symlink(os.path.basename(manifest_name), manifest_link)
        bb.debug(2, "Image vulnerability manifest saved to: %s" % manifest_name)
}

IMAGE_POSTPROCESS_COMMAND_prepend = "${@'fbvuln_track_write_manifest; '}"
do_image_complete[recrdeptask] += "${@'do_fbvuln_track'}"

def _scan_patch_file(f, matchers):
    for line in f:
        for matcher in matchers:
            match = matcher.search(line)
            if match:
                cves = line[match.start()+5:match.end()]
                for cve in cves.split():
                    yield cve

def get_patches_vulns(d):
    """
    Get patches that solve CVEs using the "CVE: " tag.
    """

    import re

    pn = d.getVar("PN")
    cve_match = re.compile("CVE:( CVE\-\d{4}\-\d+)+")
    vuln_matchers = (cve_match,)

    # Matches last CVE-1234-211432 in the file name, also if written
    # with small letters. Not supporting multiple CVE id's in a single
    # file name.
    cve_file_name_match = re.compile(".*([Cc][Vv][Ee]\-\d{4}\-\d+)")

    patched_cves = set()
    bb.debug(2, "Looking for patches that solves CVEs for %s" % pn)
    for url in src_patches(d):
        patch_file = bb.fetch.decodeurl(url)[2]

        # Check patch file name for CVE ID
        fname_match = cve_file_name_match.search(patch_file)
        if fname_match:
            cve = fname_match.group(1).upper()
            patched_cves.add(cve)
            bb.debug(
                2,
                "Found CVE %s from patch file name %s" % (cve, patch_file)
            )

        try:
            with open(patch_file, "r", encoding="utf-8") as f:
                try:
                    for cve in _scan_patch_file(f, vuln_matchers):
                        patched_cves.add(cve)
                except UnicodeDecodeError:
                    bb.debug(1, "Failed to read patch %s using UTF-8 encoding"
                            " trying with iso8859-1" %  patch_file)
                    f.close()
                    with open(patch_file, "r", encoding="iso8859-1") as f:
                        for cve in _scan_patch_file(f, vuln_matchers):
                            patched_cves.add(cve)
        except:
            pass

    return patched_cves

def vuln_write_data(d, patched):
    """
    Write vulnerability information into the temporary manifest.
    """

    # vendor,product,version,target_sw,target_hw,patched_vulns

    opn = d.getVar('BPN')
    opv = d.getVar("PV")
    cpe_sw = d.getVar("CPE_TARGET_SW") or ''
    cpe_hw = d.getVar("CPE_TARGET_HW") or ''
    patched_vulns = sorted(patched)

    entries = []

    for instance in d.getVar("CVE_PRODUCT").split(' '):
        instance = instance.strip()
        if not instance:
            continue
        pncve_split = instance.split(':')
        if len(pncve_split) == 2:
            pnven = pncve_split[0]
            pncve = pncve_split[1]
        else:
            pnven = d.getVar("CVE_VENDOR") or ''
            pncve = pncve_split[0]
        pvcve = d.getVar("CVE_VERSION").split("+git")[0]
        entry = '{0},{1},{2},{3},{4},{5},{6},{7}'.format(
            opn,
            opv,
            pnven,
            pncve,
            pvcve,
            cpe_sw,
            cpe_hw,
            " ".join(patched_vulns),
        )
        entries.append(entry)

    if entries:
        with open(d.getVar("FBVULN_CHECK_TMP_FILE"), "a") as f:
            for entry in entries:
                if entry:
                    f.write("{0}\n".format(entry))
