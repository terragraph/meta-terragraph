# Terragraph

<p align="center">
  <img src="./docs/media/logo/terragraph-logo-full-RGB.svg" width="320" />
</p>

Terragraph is a gigabit wireless technology designed to meet the growing demand
for reliable high-speed internet access. Documentation for the project can be
found at [terragraph.com](https://terragraph.com).

This repository contains a software framework to build a complete Linux image
for the radio nodes.

Terragraph uses the [Yocto Project] as the underlying build framework.

## File Structure
The Yocto naming pattern is used in this repository. A "`meta-layer`" is used to
name a layer or a category of layers. A "`recipe-abc`" is used to name a bitbake
recipe. The project itself exists as a meta layer on top of [Poky], the
reference distribution of the Yocto Project.

* **recipes-backports** - Backported recipes.
* **recipes-bsp** - Extensions to BSP (Board Support Package) recipes.
* **recipes-connectivity** - Extensions to various Poky networking recipes.
* **recipes-core** - Extensions to various core recipes.
* **recipes-extended** - Various tweaks to standard Poky recipes.
* **recipes-facebook** - Meta's recipes for end-to-end networking setup and monitoring.
* **recipes-kernel** - Build a Linux kernel.
* **recipes-radio** - 60GHz radio drivers and firmware.
* **recipes-support** - Various third-party helper libraries and utilities.
* **recipes-utils** - Various helper scripts.
* **recipes-wifi** - Recipes specific to Puma's ESP32 wifi module.

* **meta-x86** - Terragraph layer for building specific to x86 processors and emulation.
* **meta-qoriq** - Terragraph layer for building specific to NXP QorIQ processors.
* **meta-qca** - Terragraph layer for building specific to QTI-based radios on top of meta-qoriq.

* **conf** - Various bitbake configuration files.
* **docs** - Terragraph documentation.
* **docusaurus** - Terragraph static website.
* **licenses** - Additional open source licenses for code used in the project.
* **src** - Sources for various Meta components.
* **utils** - Miscellaneous utility scripts.

## Building
### General Build Setup
The build process automatically fetches all necessary packages and builds the
complete Yocto image.

Building complete Yocto images from scratch can be time-consuming. It is
recommended that you set up a rather beefy machine or server with a flash
storage device for clean builds. To greatly reduce disk usage requirements
during builds, add `INHERIT += "rm_work"` to `conf/local.conf` in the build
directory.

The Yocto Project's [Quick Start Guide] covers how to set up a machine for
various platforms. Several Meta components require C++17 support and require a
modern GCC to compile. There are also a couple of different embedded firmwares
that can require additonal toolchains installed in `/opt`. The
`utils/docker/Dockerfile` script contains an example for setting up an Ubuntu
container to do meta-qca Puma-based builds. For other builds, the setup is
similar except that the ESP32 toolchain is not required.

By default, the builds are set to run with number of CPUs as both the number of
tasks and as the number of build threads. We have found that this can create
race conditions while building various components which results in mysterious
`do_compilation` task failures. These can be resolved by rerunning the bitbake
command to retry the failed compilations.

### Building the E2E Image
The e2e-image target builds a tarball for an x86 chroot environment that can be
used as the Terragraph E2E controller. This build target is available for all
hardware platforms and is a good introduction to the Terragraph E2E stack and
userland.

1. Set up the build environment based on the Yocto Project's
   [Quick Start Guide].

2. Clone the Terragraph sources.
 ```bash
 $ git clone https://github.com/terragraph/meta-terragraph.git
 $ cd meta-terragraph
 ```

3. Clone poky and meta-openembedded repositories into the `yocto` subdirectory.
 ```bash
 $ ./sync_yocto.sh
 ```

4. Initialize a build directory. After this step you will be dropped into the
   `build-x86` build directory.
 ```bash
 $ source tg-init-build-env meta-x86 build-x86
 ```

5. Start the build within the build directory. The build process automatically
   fetches all necessary packages and builds the complete image.
 ```bash
 $ bitbake e2e-image
 ```

6. Optionally run unit tests.
 ```bash
 $ MY_BUILD_DIR=`pwd` ../utils/run_tests.sh
 ```

The final build result is
`build-x86/tmp/deploy/images/tgx86/e2e-image-tgx86.tar.gz`. This is an x86
chroot environment suitable for use with the E2E controller.

### Building the Terragraph Puma Radio Image
Puma is an NXP/QTI-based radio platform and is available to build if there is a
top-level meta-qca directory. It requires radio firmware from QTI to build.

1. Set up the build environment based on the Yocto Project's
   [Quick Start Guide].

2. Install the [ESP32 toolchain] which is required to build the firmware for
   Puma's [ESP32 wifi module]. The expected location of the ESP32 toolchain is
   `/opt/esp32-toolchain/xtensa-esp32-elf`. Set `ESP32_TOOLCHAIN_PREFIX` in
   `esp-fw_0.1.bb` for a different toolchain location. Note that the wifi
   firmware binaries and the script (`flash_esp32`) required to flash the wifi
   module are all part of the Puma image.

    If you don't want to build the ESP32 wifi firmware, then skip the ESP32
    toolchain installation and remove the `esp-fw` package from any
    `IMAGE_INSTALL` variables or pass `-R ../conf/no-esp-fw.conf` to bitbake
    commands in later steps.

3. Clone or untar the Terragraph sources.
 ```bash
 $ git clone https://github.com/terragraph/meta-terragraph.git
 $ cd meta-terragraph
 ```

4. Clone poky and meta-openembedded repositories into the `yocto` subdirectory.
 ```bash
 $ ./sync_yocto.sh
 ```

5. Acquire the radio firmware from QTI. This will generally be the latest
   available OEM (original equipment manufacturer) firmware. Terragraph uses the
   3pp file to install both the firmware blobs as well as several utilities.
   This file will have a form similar to
   `qca6430-tg-1-0_qca_oem_3pp-r00083.1-0f0e6cdd976b960c43e4eb78e10def7470e82130.tar.gz`
   but with a different hash. Drop this file into the `yocto/source_mirrors`
   directory created by `./sync_yocto.sh`.

   Because the 3pp files vary from OEM to OEM, you may also have to edit the
   corresponding bitbake recipe include file to point at your filename and
   checksums, for example in
   `meta-qca/recipes-radio/wigig-utils/tg-qca6430-DPDK-OEMR2-CSU4-2022-01-20-OEM-ver-10-11-0-99-3pp.inc`.

6. Initialize a build directory. After this step you will be dropped into the
   `build-qca` build directory.
 ```bash
 $ source tg-init-build-env meta-qca build-qca
 ```

7. Start the build within the build directory. The build process automatically
   fetches all necessary packages and builds the complete image.
 ```bash
 $ bitbake terragraph-image
 ```

The final build result is
`build-qca/tmp/deploy/images/qoriq/tg-update-qoriq.bin`. This file is a
self-extracting image for the Puma radios. Run `./tg-update-qoriq.bin -h` for
usage instructions.

## Community
Please review our [Code of Conduct](CODE_OF_CONDUCT.md) and
[Contributing Guidelines](CONTRIBUTING.md).

General discussions are held on our
[Discord server](https://discord.gg/HQaxCevzus).

![](https://discordapp.com/api/guilds/982440743765409822/widget.png?style=banner2)

## License
Terragraph is made up of different packages. Each package contains recipe files
that detail where to fetch source code from third party sources or local
directories. The recipe files themselves are provided under the MIT license in
`licenses/Meta-MIT`, but your use of the code fetched by each recipe file is
subject to the licenses of each respective third-party project. Local components
are also specified under this MIT license unless otherwise stated.


[Yocto Project]: https://www.yoctoproject.org
[Poky]: https://www.yoctoproject.org/software-item/poky/
[Quick Start Guide]: https://docs.yoctoproject.org/3.1/brief-yoctoprojectqs/brief-yoctoprojectqs.html
[ESP32 toolchain]: https://docs.espressif.com/projects/esp-idf/en/stable/get-started/linux-setup.html
[ESP32 wifi module]: https://www.espressif.com/en/products/hardware/esp32/overview
