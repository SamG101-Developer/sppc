#!/usr/bin/env bash
# Runs an x32 build's tests in a Debian VM, as no x32 binary runs on
# GitHub's runners: Ubuntu's kernels leave the x32 syscalls out, and
# qemu-user has no x32 target. Debian's standard amd64 kernel has
# them, off unless booted with "syscall.x32=y" (its "-cloud" kernel,
# in the genericcloud image, has none, hence the generic image).
# cloud-init adds a user, the x32 libc and cmake, sets the flag, and
# reboots; the build tree is copied to the same path, which ctest
# needs, and its tests run there, all but those labelled "host",
# which need the build host (run those there: "ctest -L host").
#
# Usage: tools/x32_vm_test.sh <build dir> [debian image]
# Needs qemu-system-x86_64 with KVM, qemu-img, xorriso and ssh. The
# image is downloaded when not given.

set -euo pipefail

DEBIAN_IMAGE=https://cloud.debian.org/images/cloud/trixie/20261026-2643/debian-13-generic-amd64-20261026-2643.qcow2
DEBIAN_IMAGE_SHA512=5bd891c6e62d01639f52a5587b72a4607421e445732214be787dba6043cf9797a6ff90bed982aada6a6c7e619f1f956d961c4fad76757171abd607920aac098a
SSH_PORT=2232

build=$(realpath "$1")
vm_dir=$(mktemp -d)
image=${2:-$vm_dir/debian.qcow2}

if [ -z "${2:-}" ]; then
  curl -fsSL -o "$image" "$DEBIAN_IMAGE"
fi
echo "$DEBIAN_IMAGE_SHA512  $image" | sha512sum -c -

ssh-keygen -q -t ed25519 -N '' -f "$vm_dir/key"
cat > "$vm_dir/meta-data" <<EOF
instance-id: sppc-x32
local-hostname: sppc-x32
EOF
cat > "$vm_dir/user-data" <<EOF
#cloud-config
users:
  - name: tester
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    ssh_authorized_keys: [$(cat "$vm_dir/key.pub")]
packages: [cmake, libc6-x32, libx32gcc-s1]
write_files:
  - path: /etc/default/grub.d/99-x32.cfg
    content: |
      GRUB_CMDLINE_LINUX_DEFAULT="\$GRUB_CMDLINE_LINUX_DEFAULT syscall.x32=y"
runcmd: [update-grub]
power_state: { mode: reboot }
EOF
xorriso -as mkisofs -quiet -o "$vm_dir/seed.iso" -V cidata -J -r \
  "$vm_dir/user-data" "$vm_dir/meta-data" 2>/dev/null
qemu-img create -q -f qcow2 -F qcow2 -b "$(realpath "$image")" "$vm_dir/disk.qcow2" 8G

qemu-system-x86_64 -enable-kvm -cpu host -smp "$(nproc)" -m 4G -display none -daemonize \
  -pidfile "$vm_dir/qemu.pid" -serial "file:$vm_dir/console.log" \
  -drive "file=$vm_dir/disk.qcow2,if=virtio" -drive "file=$vm_dir/seed.iso,media=cdrom" \
  -nic "user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22"
trap 'kill "$(cat "$vm_dir/qemu.pid")" 2>/dev/null; rm -rf "$vm_dir"' EXIT

vm() {
  ssh -q -i "$vm_dir/key" -p "$SSH_PORT" -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -o BatchMode=yes tester@127.0.0.1 "$@"
}

# Up when back from cloud-init's reboot, with x32 on.
for _ in $(seq 120); do
  if vm 'grep -q syscall.x32=y /proc/cmdline' 2>/dev/null; then break; fi
  sleep 5
done
if ! vm 'grep -q syscall.x32=y /proc/cmdline'; then
  echo "::error::the VM did not come up with x32 on"
  tail -n 50 "$vm_dir/console.log"
  exit 1
fi
vm 'cloud-init status --wait > /dev/null; uname -r; cmake --version | head -n 1'

vm "sudo mkdir -p '$build' && sudo chown tester '$build'"
tar -C "$build" -cf - . | vm "tar -C '$build' -xf -"
vm "ctest --test-dir '$build' --output-on-failure -LE host"
