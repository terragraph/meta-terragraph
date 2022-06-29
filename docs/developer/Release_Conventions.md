# Release Conventions
This document describes Terragraph's software release conventions. Note that the
NMS stack follows a different procedure and is not described here.

## Tagging & Branching
Terragraph uses Git to manage its code repository. *Tags* and *branches* are
used to mark releases as follows:

* `main` is the sole development branch.
* A "pre-release" tag is created at the start of development for a major
  release, e.g. `RELEASE_M77_PRE`. This tag is not synced externally.
* A "major release" tag is created when a release is finalized, e.g.
  `RELEASE_M77` ("Release M77").
* A release branch is cut at every major release tag, e.g. `releases/m77`.
* If necessary, bug fixes will be committed on top of a release branch, and a
  "minor release" tag will be created when finalized, e.g. `RELEASE_M77_1`
  ("Release M77.1").
* The `VERSION` file at the root of the repository will contain the current tag.

Only the top of a release branch should be considered stable, as these commits
have gone through regression, integration, and/or field testing prior to being
tagged.

## Release Schedule
There is no set schedule for Terragraph releases. Release frequency will depend
on the volume of changes as well as available testing capacity.

Major releases which pass rigorous testing may be promoted to long-term support
(LTS) releases no less than 3 months apart. Each LTS release will be supported
for 6 months or until the next LTS release is made (whichever is longer), and
will have critical fixes cherry-picked into the branch until its end of life.
LTS releases are shown in the table below.

| LTS Version | Initial Release Date | End of Life |
| ----------- | -------------------- | ----------- |
| M77         | Aug 29, 2021         | -           |
| M60         | Sep 28, 2020         | Yes         |

## Backwards Compatibility
In general, every effort should be made to preserve backwards compatibility for
node and cloud software, both between sequential releases and LTS releases. Any
breaking changes that require user intervention or special upgrade steps should
be highlighted in release notes. When possible, any breaking changes in
configuration should be migrated automatically to avoid user intervention (see
[Breaking Changes](Configuration_Management.md#configuration-management-breaking-changes)).
