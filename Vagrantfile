# -*- mode: ruby -*-
#
# A build machine for the Kestrel and the Kestrel 16.
#
#   vagrant up          # make the machine, install the tools, build and test
#   vagrant ssh         # a shell in it, with the tree at /vagrant
#   vagrant halt        # stop it
#   vagrant destroy -f  # throw it away
#
# The guest is Ubuntu 22.04: g++ 12 (C++20), CMake 3.22, and Qt 6 Widgets, which
# is everything the tree asks for. The window needs a display, so it is built and
# its tests are run, but it is not started -- the command line emulator and the
# seven suites are what the provisioning proves.
#
# With libvirt rather than VirtualBox:
#
#   vagrant up --provider=libvirt
#
# and change the box below to "generic/ubuntu2204".

VAGRANTFILE_API_VERSION = "2"

# The packages the tree needs, as root, then the build as the person who owns the
# tree. Both strings are ordinary shell: a machine without Vagrant can run them
# by hand.
$packages = <<~SHELL
  set -euo pipefail
  export DEBIAN_FRONTEND=noninteractive
  apt-get update
  apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build qt6-base-dev
SHELL

# Run as the vagrant user, not as root: the tree is the synced folder, and a
# build owned by root is one a person cannot clean up.
$build = <<~SHELL
  set -euo pipefail
  cd /vagrant
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
  cmake --build build -j "$(nproc)"
  ctest --test-dir build --output-on-failure

  echo
  echo "The machines are built. From here:"
  echo "  ./start.sh cli --model kestrel --type \"PRINT 2+2\""
  echo "  ./start.sh cli --model kestrel16 --file samples/kestrel16/03-division.bas"
SHELL

Vagrant.configure(VAGRANTFILE_API_VERSION) do |config|
  config.vm.box = "ubuntu/jammy64"
  config.vm.hostname = "kestrel"

  # Enough for a parallel build, and the machine has no other job.
  config.vm.provider "virtualbox" do |vb|
    vb.memory = 4096
    vb.cpus = 2
    vb.gui = false
  end

  config.vm.provider "libvirt" do |lv|
    lv.memory = 4096
    lv.cpus = 2
  end

  # The window is built but never shown, so no X forwarding and no ports: the
  # machines have no network of their own to offer.
  config.ssh.forward_x11 = false

  config.vm.provision "shell", inline: $packages
  config.vm.provision "shell", inline: $build, privileged: false
end
