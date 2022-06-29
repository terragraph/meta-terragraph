#!/bin/bash

#
# Script that run a QEMU VM over a terragraph rootfs and runs provided tests scripts
#
# 
# Example:
# run_qemu_tests.sh -r /home/sgasmi/meta-terragraph/build-x86/tmp/work/tgx86-poky-linux/terragraph-image-x86/1.0-r0/rootfs \ 
#      /usr/sbin/tests/e2e/driver_if_test \
#      /usr/sbin/tests/e2e/ignition_app_test
#
# use -h for help

# We init our variables
base="/tmp/files_for_qemu_vm"
tmp="$base/$$"
rootfs="."
dry=0
boot_wait=30

# We create the base dir and the mnt point
test ! -d "$base/mnt" && mkdir -p "$base/mnt"

# We parse the command line
skip=0
while getopts "hdr:s:" arg; do
  case "$arg" in
    h)
      self=$(basename $0)
      echo "Usage: $self [options] [script....]"
      echo "  -r root_dir : The root dir to use (If not specified, . will be used)"
      echo "  -s seconds  : The number of seconds to wait the VM to boot (default is 30)"
      echo "  -d          : Dry mode, will not run the VM but will do all the tests and downloads"
      echo "  -h          : This help"
      exit 0
      ;;

    r)
      rootfs=$OPTARG
      skip=$((skip+2))
      ;;

    s)
      boot_wait=$OPTARG
      skip=$((skip+2))
      ;;

    d)
      dry=1
      skip=$((skip+1))
      ;;

    *)
      exit 1
      ;;

  esac
done

shift $skip

# Some variables you can play with:
info_head="INFO -"      # stdout messages header 
root_pass="facebook"    # The VM root password
vm_memory="2G"          # The VM memory
image_url="https://cloud.centos.org/centos/7/images/CentOS-7-x86_64-GenericCloud.qcow2"

# tags used internaly to detect the begin and end of script output
# Must not be present in the scripts output and start with a #
beg_tag="#cced28c6dc3f99c2396a5eaad732bf6b28142335892b1cd0e6af6cdb53f5ccfa"
end_tag="#361e48d0308f20e32dba5fb56328baf18d72ef0ccb43b84f5c262d2a6a1fc6c8"


# This function is fired when the script exit
# it kills the vm, unmount the image, free the used nbd device and delete the working dir
function cleanup {
  echo "$info_head Cleaning ...."
  test "$vmpid" != "" && sudo kill "$vmpid"
  test "$mounted" = "1" && sudo umount "$base/mnt" > /dev/null 2>&1
  test "$device" != "" && sudo qemu-nbd -d "$device" > /dev/null 2>&1
  sudo rm -rf $tmp
}


# testing rootfs
echo -n "$info_head Checking rootfs: "
test ! -f "$rootfs/usr/sbin/tests/e2e/ignition_app_test" && echo "$rootfs does not seems a valid rootfs" && exit 2
echo OK

# testing qemu binaries
echo -n "$info_head Checking QEMU binaries: "
for i in qemu-system-x86_64 qemu-nbd
do
  command -v "$i" > /dev/null 2>&1
  test $? -ne 0 && echo "$i does not seems installed, please install qemu first" && exit 3
done
echo OK

# testing qemu modules
echo -n "$info_head Checking QEMU modules: "
sudo modprobe kvm kvm_intel > /dev/null 2>&1
n=$(sudo lsmod | grep -c ^kvm)
test "$n" != "2" && echo "kvm modules does not seems to be loaded ... exiting" && exit 4
echo OK

# creating tmp work dir
if test ! -d $tmp
then
  echo -n "$info_head Creating tmp working dir: "
  mkdir $tmp
  test $? -ne 0 && echo "unable to create dir $tmp ... exiting" && exit 5
  echo OK
fi

# We register the Cleanup function
trap cleanup EXIT

# If files are not here download and generate everything
if test ! -f $base/kernel -o ! -f $base/init
then
  echo "$info_head Needed files are not present ..." 
  rm -f $base/init $base/kernel $base/os.qcow 
  echo -n "Downloading CentOS image .... : "
  https_proxy=fwdproxy:8080 wget -qq  "$image_url" -O $base/os.qcow
  test $? -ne 0 && echo "Failed" && exit 20
  echo OK

  echo -n "$info_head Extracting boot files from CentOS image ... : "
  sudo modprobe nbd 
  found=""
  for i in $(seq 0 15)
  do
    test ! -e "/dev/nbd${i}p1" && found="$i" && break
  done

  test "$found" = "" && echo "No free nbd device found ... exiting" && exit 7
  sudo qemu-nbd -c "/dev/nbd$found" "$base/os.qcow"
  test $? -ne 0 && echo "Nbd device attach failed ... exiting" && exit 8

  device="/dev/nbd$found"
  sudo mount "${device}p1" "$base/mnt"
  test $? -ne 0 && echo "OS image mount failed ... exiting" && exit 9

  mounted=1
  version=$(sudo find "$base/mnt/boot" -maxdepth 1 -name 'System.map-*' -print | xargs basename | cut -f2- -d'-')
  init="$base/mnt/boot/initramfs-$version.img"
  kern="$base/mnt/boot/vmlinuz-$version"

  test ! -f "$init" && echo "Unable to find initramfs file ... exiting" && exit 10
  test ! -f "$kern" && echo "Unable to find kernel file ... exiting" && exit 11

  sudo chmod 644 "$init" "$kern"
  sudo mv "$kern" $base/kernel
  sudo mv "$init" $base/init
  sudo rm -f $base/os.qcow

  echo "OK"
  echo "$info_head The kernel version is: $version"

else
  echo "$info_head All needed files found !"
fi

# Creation of vm disk from rootfs
echo -n "$info_head Creating VM disk from rootfs: "
dd if=/dev/zero of="$tmp/rootfs.img" bs=1 count=0 seek=800M > /dev/null 2>&1
sudo mke2fs -t ext4 -d "$rootfs" -F "$tmp/rootfs.img" > /dev/null 2>&1
test $? -ne 0 && echo "Failed ... exiting" && exit 12
echo OK

# if in dry mode, stop here
test $dry -eq 1 && echo "$info_head Dry mode in action, exiting" && exit 0

# Let's boot the VM
echo -n "$info_head Starting QEMU VM: "

# We create a named pipe
mkfifo "$tmp/vm.in" "$tmp/vm.out" > /dev/null 2>&1
test $? -ne 0 && echo "named pipe creation failed .. exiting" && exit 18

# We run the VM with it's console binded to the named pipe
sudo qemu-system-x86_64 -m "$vm_memory" \
    -kernel "$base/kernel" \
    -initrd "$base/init" \
    -serial pipe:$tmp/vm -nographic \
    -append 'console=ttyS0 root=/dev/sda rw init=/sbin/init.sysvinit debug=1' \
    --enable-kvm -nographic -hda "$tmp/rootfs.img" > /dev/null 2>&1 &

ret=$?
vmpid=$!

test $ret -ne 0 && echo "Failed with return code $ret" && exit 13
echo "OK VM has pid $vmpid"

# Sleeping .....
echo "$info_head Sleeping $boot_wait seconds waiting for the VM to boot"
sleep "$boot_wait"

# log in as root
echo "$info_head Logging as root ..."
printf "root\\n\\r" > $tmp/vm.in
sleep 1
printf "%s\\n\\r" "$root_pass" > $tmp/vm.in
sleep 1
printf "ls -al /\\n\\r" > $tmp/vm.in
sleep 1

# run scripts
echo "$info_head Running script(s) ..."
echo "$beg_tag" > $tmp/vm.in

# if scripts are provided on command line, run them
# otherwise run all tests
if test $# -ne 0
then
  for script in "$@"
  do
    echo "# --- start $script ---" > $tmp/vm.in
    echo "$script" > $tmp/vm.in
    echo "# --- end $script ---" > $tmp/vm.in
  done
else
  echo 'for file in /usr/sbin/tests/e2e/*test*; do echo "--- start $file ---"; $file; echo "--- end $file ---";  done' > $tmp/vm.in
fi

printf "%s\\n\\r" "$end_tag" > $tmp/vm.in

# We loop on the named pipe, start displaying when start tag is read and exit when end tag is read
out=0
while read -r line
do
  # We clean the line from any carriage return
  line=$(echo "$line" | tr -d '\n\r')
  test "$DEBUG" != "" && echo "DEBUG: out=$out ($line)"

  if test $out -eq 0
  then
    # if line contains begin tag, set out to 1
    echo "$line" | grep -q "$beg_tag"
    test $? -eq 0 && out=1
  else
    # if line contains end tag, just exit and the cleanup function will do the job
    echo "$line" | grep -q "$end_tag"
    test $? -eq 0 && exit 0

    # this line must be shown
    echo "$line"
  fi
done < $tmp/vm.out

# This line is never reached
