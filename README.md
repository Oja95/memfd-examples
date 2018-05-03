## CPU time based timer
This forked project is a server for the [Honest Profiler shared memory experiment](https://github.com/Oja95/honest-profiler/tree/shared-memory-experiment).
It essentially keeps track of the CPU time consumed by the Java process that the Honest Profiler native agent is attached to and then sends signals accordingly.


### Requirements

- Linux Kernel 3.17 or higher
- Header files for such a kernel
  - Debian/Ubuntu: `sudo apt-get install linux-headers-$(uname -r)`
  - Redhat/Fedora: `sudo yum -y kernel-headers-$(uname -r)`
  - Arch Linux: `sudo pacman -S linux-headers`

### Building
Run the following to build the executable `server`:

`cmake . && make`
