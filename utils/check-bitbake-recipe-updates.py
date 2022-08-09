#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

"""
Parse all BitBake recipes in the given directories and check for new upstream
versions. Only Git repositories are supported.
"""

import argparse
import logging
import os
import shutil
import subprocess
import sys
import urllib.request
from distutils.version import LooseVersion


LOG = logging.getLogger(__name__)
GIT_PROTOCOL = "git://"


def read_file(path):
    """Return file contents as a string."""
    with open(path, "r") as f:
        return f.read()


def find_bb_recipes(dirs):
    """Find all BitBake recipes in the given list of directories.

    Returns a list of tuples:
    `(basename, path)`
    """
    recipes = []
    for path in dirs:
        for root, _subdirs, files in os.walk(path):
            for f in files:
                if f.endswith(".bb"):
                    recipes.append((f, os.path.join(root, f)))
    return recipes


def parse_bb_recipe(path, includes):
    """Parse the given BitBake recipe and return a tuple for the first source:
    - `(http{s}_uri,)`
    - `(git_uri, src_rev, branch <optional>)`

    If "include"/"require" directives are found, also parse those recipes
    non-recursively, and add them to the "includes" map passed as input.

    If parsing failed, returns None.
    """
    # Read file
    data = read_file(path)

    # Parse file
    recipe_vars, recipe_includes = parse_recipe(data, path)

    # Parse includes
    relative_dir = os.path.dirname(path)
    for include in recipe_includes:
        include = replace_bitbake_vars(include, path)

        # Try to locate the file...
        possible_include_paths = [
            # relative to recipe
            os.path.abspath(os.path.join(relative_dir, include)),
            # relative to working dir? (assuming run from project root)
            os.path.abspath(include),
            # relative to <project root>/yocto/poky/meta/
            os.path.abspath(os.path.join("yocto/poky/meta/", include)),
        ]
        include_path = None
        for p in possible_include_paths:
            if os.path.isfile(p):
                include_path = p
        if not include_path:
            LOG.warning(f"Could not find included file '{include}' in: {path}")
            continue

        include_vars = None
        if include_path in includes:
            include_vars = includes[include_path]
        else:
            LOG.debug(f"Loading included file: {include_path}")
            include_vars, _ = parse_recipe(read_file(include_path), include_path)
            includes[include_path] = include_vars
        if include_vars:
            recipe_vars.update(include_vars)

    src_uri = recipe_vars.get("SRC_URI", None)
    src_rev = recipe_vars.get("SRCREV", None)

    if src_uri:
        # TODO support multiple sources?
        for token in src_uri.split():
            s = token.strip()
            if s.startswith(GIT_PROTOCOL):
                # Parse git:// URIs
                # Split on Yocto git fetcher parameters
                # (only support "protocol", "branch" parameters)
                uri_tokens = s.split(";")
                git_uri = uri_tokens[0]
                git_uri = replace_recipe_vars(git_uri, recipe_vars)
                git_uri = replace_bitbake_vars(uri_tokens[0], path)
                protocol = [
                    x[len("protocol=") :]
                    for x in uri_tokens[1:]
                    if x.startswith("protocol=")
                ]
                protocol = protocol[0] if protocol else None
                if protocol:
                    git_uri = git_uri.replace(GIT_PROTOCOL, f"{protocol}://", 1)
                branch = [
                    x[len("branch=") :]
                    for x in uri_tokens[1:]
                    if x.startswith("branch=")
                ]
                branch = branch[0] if branch else None
                return (git_uri, src_rev, branch)
            elif s.startswith("http://") or s.startswith("https://"):
                # Split on Yocto URL parameters
                uri_tokens = s.split(";")
                url = uri_tokens[0]
                url = replace_recipe_vars(url, recipe_vars)
                url = replace_bitbake_vars(url, path)
                return (url,)
    return None


def parse_recipe(s, path):
    """Read variables and includes from the given bitbake recipe."""
    ret_vars = {}
    includes = set()
    tmp_k = None
    tmp_v = None
    operator = None
    in_function = False
    in_quote = None  # [', ", None]
    for line in s.splitlines():
        if line.strip().startswith("#"):
            continue
        if in_function:
            if line.strip().endswith("}"):
                in_function = False
            continue
        elif tmp_k is not None:
            k = tmp_k
            v = line.strip()
            if in_quote and v.endswith(in_quote):
                v = tmp_v + " " + v[:-1]
                in_quote = None
            elif v.endswith("\\"):
                # continue parsing multiline string value
                tmp_v += v[:-1]
                continue
            else:
                LOG.error(
                    f"read_to_vars error parsing multiline string in path: {path}"
                )
        else:
            # Only handling =, += operators
            OPERATORS = "=", "+="
            for op in OPERATORS:
                tokens = line.split(f" {op} ", 1)
                if len(tokens) == 2:
                    operator = op
                    k = tokens[0].strip()
                    v = tokens[1].strip()
                    break
            if operator is None:
                if line.strip().endswith("{"):
                    in_function = True
                elif line.strip().startswith("include "):
                    includes.add(line.strip()[len("include ") :])
                elif line.strip().startswith("require "):
                    includes.add(line.strip()[len("require ") :])
                continue

            if k.startswith("export "):
                k = k[len("export ") :]

            if v.startswith('"'):
                v = v[1:]
                in_quote = '"'
            elif v.startswith("'"):
                v = v[1:]
                in_quote = "'"

            if in_quote and v.endswith(in_quote):
                v = v[:-1]
                in_quote = None
            elif v.endswith("\\"):
                # multiline string value
                tmp_k = k
                tmp_v = v[:-1]
                continue

        if operator == "=":
            ret_vars[k] = v
        elif operator == "+=":
            if k in ret_vars:
                ret_vars[k] = ret_vars[k] + " " + v
            else:
                ret_vars[k] = v
        else:
            LOG.error(f"read_to_vars ended with no operator in path: {path}")
        operator = None
        tmp_k = tmp_v = None

    return ret_vars, includes


def replace_recipe_vars(s, recipe_vars):
    """Replace recipe variables in the given string."""
    for k, v in recipe_vars.items():
        s = s.replace("${" + k + "}", v)
    return s


def replace_bitbake_vars(s, path):
    """Replace a subset of bitbake recipe variables in the given string."""
    filename = os.path.splitext(os.path.basename(path))[0]
    tokens = filename.split("_")
    pn = tokens[0]
    pv = tokens[1] if len(tokens) > 1 else ""
    s = s.replace("${P}", "${PN}-${PV}")
    s = s.replace("${BP}", "${BPN}-${PV}")
    s = s.replace("${BPN}", f"{pn}")
    s = s.replace("${PN}", f"{pn}")
    s = s.replace("${PV}", f"{pv}")
    return s


def find_changelog(repo_dir, git_uri, branch):
    """Return a URL to the changelog file in the given directory, or None if not
    found.

    Only GitHub URLs are currently supported.
    """
    CHANGELOG_FILES = ["news", "changes", "changelog", "history"]

    changelog_url = None
    for f in os.listdir(repo_dir):
        filename = os.path.splitext(f)[0].lower()
        if filename in CHANGELOG_FILES and os.path.isfile(os.path.join(repo_dir, f)):
            # Only support GitHub URLs
            GIT_SUFFIX = ".git"
            if git_uri.startswith(GIT_PROTOCOL) and git_uri.startswith(
                "github.com", len(GIT_PROTOCOL)
            ):
                s = git_uri.replace(GIT_PROTOCOL, "https://", 1)
                if s.endswith(GIT_SUFFIX):
                    s = s[: -len(GIT_SUFFIX)]
                s = s + f"/blob/{branch if branch else 'master'}/{f}"
                changelog_url = s
            else:
                LOG.debug(f"Unsupported git URI: {git_uri}, {f}")
            break
    return changelog_url


def git_ls(git_uri, branch, timeout_s):
    """Run 'git ls-remote' for the given URI, returning True if the command
    returns success and the given branch exists (if non-empty).
    """
    try:
        ret = subprocess.run(
            ["git", "ls-remote", "--heads", git_uri],
            capture_output=True,
            encoding="utf8",
            timeout=timeout_s,
        )
    except subprocess.TimeoutExpired:
        LOG.error(f"Timeout while running 'git ls-remote' on {git_uri}")
        return False
    if ret.returncode != 0:
        LOG.error(f"Failed to query {git_uri}:\n{ret.stdout}\n{ret.stderr}")
        return False

    # Parse remote branches
    if branch:
        found_branches = set()
        for line in ret.stdout.splitlines():
            tokens = line.split("\t")
            if len(tokens) >= 2:
                ref = tokens[1]
                REF_PREFIX = "refs/heads/"
                if ref.startswith(REF_PREFIX):
                    ref = ref[len(REF_PREFIX) :]
                found_branches.add(ref)
        if branch not in found_branches:
            LOG.error(
                f"Branch '{branch}' not found in repository [{git_uri}], "
                + f"remote branch list: {found_branches}"
            )
            return False

    return True


def git_clone(git_uri, output_dir, timeout_s):
    """Clone the given Git repository, returning True upon success."""
    # Delete existing directory (if any)
    shutil.rmtree(output_dir, ignore_errors=True)

    # Clone the repo
    try:
        ret = subprocess.run(
            ["git", "clone", git_uri, output_dir],
            capture_output=True,
            timeout=timeout_s,
        )
    except subprocess.TimeoutExpired:
        LOG.error(f"Timeout while cloning {git_uri}")
        return False
    if ret.returncode != 0:
        LOG.error(f"Failed to clone {git_uri}:\n{ret.stdout}\n{ret.stderr}")
        return False
    return True


def get_versions(git_uri, src_rev, branch, output_dir):
    """Clone the given Git repository and return a tuple:
    `(latest_ver, given_ver)`

    A "version" is the result of `git describe --tags --abrev=0`.

    Upon any failures, returns None.
    """
    # Try to find changelog file
    changelog_url = find_changelog(output_dir, git_uri, branch)

    # Fetch latest version
    fetch_git_ver = (
        lambda: subprocess.run(
            ["git", "-C", output_dir, "describe", "--tags", "--abbrev=0"],
            stdout=subprocess.PIPE,
        )
        .stdout.decode("utf-8")
        .strip()
    )
    latest_ver = fetch_git_ver()

    # Fetch the given version
    ret = subprocess.run(
        ["git", "-C", output_dir, "checkout", src_rev], capture_output=True
    )
    if ret.returncode != 0:
        LOG.error(f"Failed to checkout rev {src_rev}:\n{ret.stdout}\n{ret.stderr}")
        return None
    given_ver = fetch_git_ver()

    if not latest_ver or not given_ver:
        LOG.error(f"No versions could be parsed from Git: {git_uri}")
        return None

    # Delete repo
    shutil.rmtree(output_dir, ignore_errors=True)

    return (latest_ver, given_ver, changelog_url)


def is_newer(a, b):
    """Compare two version strings, returning True if `a` is newer than `b`."""
    if a == b:
        return False
    return LooseVersion(a) > LooseVersion(b)


def main():
    # Parse arguments
    parser = argparse.ArgumentParser(
        description="Check for BitBake recipes updates.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("dirs", nargs="+", help="Directories to scan")
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Enable verbose logging"
    )
    parser.add_argument(
        "--tmpdir",
        type=str,
        default="/tmp",
        help="Temporary directory to store git repositories",
    )
    parser.add_argument(
        "--fetch_timeout_s",
        type=int,
        default=30,
        help="Timeout for HTTP download and git commands, in seconds",
    )
    parser.add_argument("--skip_git", action="store_true", help="Skip Git sources")
    parser.add_argument(
        "--skip_http", action="store_true", help="Skip HTTP/HTTPS sources"
    )
    parser.add_argument(
        "--skip_url", type=str, nargs="*", help="Source URL substrings to skip"
    )
    parser.add_argument(
        "--rewrite_git_url",
        action="store_true",
        help="Rewrite all git:// URLs to https://",
    )
    args = parser.parse_args()

    # Configure logging
    if args.verbose:
        logging.basicConfig(level=logging.DEBUG, format="%(message)s")
    else:
        logging.basicConfig(level=logging.INFO, format="%(message)s")

    # Locate recipes
    recipes = find_bb_recipes(args.dirs)
    includes = {}  # path -> parse_recipe(path)[0]
    total_found = len(recipes)
    total_skipped = 0
    total_git_pass = 0
    total_http_pass = 0
    error_list = []
    outdated_list = []
    for name, path in recipes:
        # Parse recipe
        results = parse_bb_recipe(path, includes)
        if not results:
            LOG.debug(f"Skipping recipe: {path}\n")
            total_skipped += 1
            continue
        LOG.debug("%s: %s", name, results)

        if len(results) == 1:  # HTTP/HTTPS
            url = results[0]

            if args.skip_http:
                total_skipped += 1
                continue

            if any(s in url for s in args.skip_url):
                total_skipped += 1
                continue

            if url[url.index("://") + 3 :].startswith("localhost/"):
                LOG.info(f"<{name}> package source URL is localhost, skipping...\n")
                total_skipped += 1
                continue

            # Download file
            try:
                LOG.debug(f"Fetching: {url}")
                response = urllib.request.urlopen(url, timeout=args.fetch_timeout_s)
                response.read()
                total_http_pass += 1
                LOG.info(f"<{name}> package source URL is valid.\n")
            except Exception as e:
                LOG.error(
                    f"Failed to fetch sources for recipe: {path} "
                    + f"(threw {type(e).__name__} for {url})\n"
                )
                error_list.append(name)
        elif len(results) == 3:  # Git
            (git_uri, src_rev, branch) = results

            if args.skip_git:
                total_skipped += 1
                continue

            if any(s in git_uri for s in args.skip_url):
                total_skipped += 1
                continue

            # Clone repo and fetch versions
            if args.rewrite_git_url and git_uri.startswith(GIT_PROTOCOL):
                git_uri = git_uri.replace(GIT_PROTOCOL, "https://", 1)
            output_dir = os.path.join(args.tmpdir, name.split("_")[0])
            if not git_ls(git_uri, branch, args.fetch_timeout_s):
                LOG.error(
                    f"Failed to query git repository for recipe: {path} ({git_uri})\n"
                )
                error_list.append(name)
                continue
            if not git_clone(git_uri, output_dir, args.fetch_timeout_s):
                LOG.error(f"Failed to fetch sources for recipe: {path} ({git_uri})\n")
                continue
            total_git_pass += 1
            if not src_rev:
                LOG.warning(
                    f"Skipping version checks as no SRCREV parsed for recipe: {path}\n"
                )
                continue
            vers = get_versions(git_uri, src_rev, branch, output_dir)
            if not vers:
                LOG.warning(f"Failed to get versions for recipe: {path}\n")
                continue
            LOG.debug("%s: latest = %s / current = %s", name, vers[0], vers[1])

            # Compare versions
            if is_newer(vers[0], vers[1]):
                s = (
                    f"<{name}> newer version is available:\n"
                    + f"     Current version: {vers[1]}\n"
                    + f"    Upstream version: {vers[0]}"
                )
                if vers[2]:
                    s = s + f"\n           Changelog: {vers[2]}"
                else:
                    s = s + f"\n          Repository: {git_uri}"
                LOG.info(s + "\n")
                outdated_list.append(name)
            else:
                LOG.info(f"<{name}> package is up to date.\n")

    LOG.info(
        "Results:\n"
        + f"- {total_found} packages parsed (+{len(includes)} includes)\n"
        + f"- {total_skipped} packages skipped\n"
        + f"- {total_git_pass} git sources validated\n"
        + f"- {total_http_pass} http sources validated\n"
        + f"- {len(error_list)} packages with unfetchable sources: {error_list}\n"
        + f"- {len(outdated_list)} packages outdated: {outdated_list}\n"
    )
    return len(error_list)


if __name__ == "__main__":
    sys.exit(main())
