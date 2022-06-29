#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# TODO: Clean up this script and extend it to allow different topologies
import argparse
import os
import subprocess
import time
from tempfile import NamedTemporaryFile
from typing import List


BYTES_IN_MB = 1 << 20

OOB_MAC_OFFSET = 0x525400000000
NET_MAC_OFFSET = 0x525600000000


class QemuVm:
    start_command: List[str]

    def __init__(self, kernel_path: str, qemu_image_file, base_mac_addr: int):
        # fmt: off
        self.start_command = [
            "qemu-system-aarch64",

            "-M", "virt",

            "-cpu", "cortex-a53",

            "-smp", "4",

            "-m", "3G",

            "-kernel", kernel_path,

            "-append", "console=ttyS0 root=/dev/mmcblk0p1 rw",
            "-nographic",
            "-chardev", "stdio,id=console,mux=on,signal=off",
            "-mon", "chardev=console,mode=readline",
            "-serial", "chardev:console",
            "-device", "pci-serial,chardev=console",

            "-device", "virtio-rng-pci,max-bytes=1024,period=1000",
        ]
        # fmt: on
        self.base_mac_addr = base_mac_addr
        self.add_cow_image(qemu_image_file)
        self.socket_netdev_idx = 0

    def add_oob_netdev(self, port: int):
        oob_mac = int_to_mac_str(self.base_mac_addr + OOB_MAC_OFFSET)
        self.start_command.extend(
            # fmt: off
            [
                "-netdev", f"user,id=oob,hostfwd=tcp:127.0.0.1:{port}-:22",

                "-device", "virtio-net,netdev=oob,"
                f"mac={oob_mac},romfile=",
            ]
            # fmt: on
        )

    def add_socket_netdev(self, listening=True, host_port_pair=("127.0.0.1", 12345)):
        netdev_mac = int_to_mac_str(
            NET_MAC_OFFSET + self.base_mac_addr + self.socket_netdev_idx
        )
        self.start_command.extend(
            [
                # fmt: off
                "-netdev",
                f"socket,id=net{self.socket_netdev_idx},{'listen' if listening else 'connect'}"
                f"={host_port_pair[0]}:{host_port_pair[1]}",

                "-device",
                f"virtio-net,netdev=net{self.socket_netdev_idx},mac={netdev_mac},romfile=",
                # fmt: on
            ]
        )
        self.socket_netdev_idx += 1

    def add_cow_image(self, base_image_file):
        self.cow_image_file = NamedTemporaryFile()
        create_cow_image_cmd = [
            # fmt: off
            "qemu-img", "create",
            "-o", f"backing_file={base_image_file.name},backing_fmt=raw",
            "-f", "qcow2",
            self.cow_image_file.name,
            # fmt: on
        ]
        subprocess.run(create_cow_image_cmd)
        self.start_command.extend(
            # fmt: off
            [
                "-drive", f"id=disk,file={self.cow_image_file.name},format=qcow2,if=none",
                "-device", "sdhci-pci",
                "-device", "sd-card,drive=disk,bus=sd-bus",
            ]
            # fmt: on
        )

    def start(self):
        self.vm_process = subprocess.Popen(self.start_command, stdin=subprocess.DEVNULL)

    def wait(self):
        self.vm_process.wait()


def int_to_mac_str(int_mac: int) -> str:
    """
    Converts an integer MAC address to a string of six groups of 2 hex digits

    >>> int_to_mac_str(1000)
    "00:00:00:00:03:e8"
    """
    # convert to 12 digit hex with zero padding
    mac_hex = f"{int_mac:012x}"
    # splits string into list of octets
    mac_octets = [mac_hex[i : i + 2] for i in range(0, len(mac_hex), 2)]
    return ":".join(mac_octets)


def create_base_qemu_image(compressed_image_path):
    base_image_file = NamedTemporaryFile()
    base_image_file.truncate(500 * BYTES_IN_MB)
    initial_offset = 2048
    subprocess.run(
        (
            # fmt: off
            "parted", "-s", base_image_file.name,
            "mklabel", "GPT", "mkpart", "rootfs", "ext4", f"{initial_offset}s", "450M",
            "mkpart", "data", "ext4", "450M", "100%",
            # fmt: on
        )
    )
    zstdcat_proc = subprocess.Popen(
        ("zstdcat", compressed_image_path), stdout=subprocess.PIPE
    )
    subprocess.run(
        (
            "dd",
            f"of={base_image_file.name}",
            "bs=1M",
            f"seek={initial_offset * 512}",
            "oflag=seek_bytes",
            "conv=notrunc",
            "status=none",
        ),
        stdin=zstdcat_proc.stdout,
    )
    zstdcat_proc.wait()
    return base_image_file


def start_n_qemu_pumas(images_path, n=2, start_ssh_port=2022, start_socket_port=12345):
    # TODO: generate e2e topology file
    compressed_image_path = f"{images_path}/terragraph-image-qoriq.ext4.zst"
    kernel_path = f"{images_path}/fitImage-linux.bin-qoriq"
    qemu_image_file = create_base_qemu_image(compressed_image_path)

    vms = [QemuVm(kernel_path, qemu_image_file, 0xEB + i * 0x10) for i in range(n)]

    for i, vm in enumerate(vms):
        vm.add_oob_netdev(start_ssh_port + i)

    for (i, (vm1, vm2)) in enumerate(zip(vms[:-1], vms[1:])):
        # the VM with the listening socket must come up first
        vm1.add_socket_netdev(
            listening=True, host_port_pair=("127.0.0.1", start_socket_port + i)
        )
        vm2.add_socket_netdev(
            listening=False, host_port_pair=("127.0.0.1", start_socket_port + i)
        )

    for vm in vms:
        vm.start()
        # there has to be a slight delay between starting VMs
        # so that the VM with the listening socket comes up first
        time.sleep(1)

    for vm in vms:
        vm.wait()


def main():
    parser = argparse.ArgumentParser()
    default_images_path = (
        f"/data/users/{os.environ['USER']}"
        "/meta-terragraph/build-qca/tmp/deploy/images/qoriq"
    )
    parser.add_argument(
        "--images-path",
        default=default_images_path,
        help=(
            "Path to images directory, containing kernel and image binaries."
            f" Defaults to {default_images_path}"
        ),
    )
    parser.add_argument("-n", default=2, help="Number of VMs to start", type=int)
    parser.add_argument(
        "--ssh_start_port",
        default=2022,
        help="The start of the port range for the SSH sockets",
        type=int,
    )
    parser.add_argument(
        "--vm_socket_start_port",
        default=12345,
        help=(
            "[Internal] The start of the port range for n TCP sockets."
            " Only needs to be changed if there is a conflicting port on the host"
        ),
        type=int,
    )
    args = parser.parse_args()

    start_n_qemu_pumas(
        args.images_path, args.n, args.ssh_start_port, args.vm_socket_start_port
    )


if __name__ == "__main__":
    main()
