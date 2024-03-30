BLOCX Core
==========

This is the official reference wallet for BLOCX digital currency and comprises the backbone of the BLOCX peer-to-peer network. You can [download BLOCX Core](https://www.blocx.org/downloads/) or [build it yourself](#building) using the guides below.

Running
---------------------
The following are some helpful notes on how to run BLOCX Core on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/blocx-qt` (GUI) or
- `bin/blocxd` (headless)

### Windows

Unpack the files into a directory, and then run blocx-qt.exe.

### macOS

Drag BLOCX Core to your applications folder, and then run BLOCX Core.

### Need Help?

* See the [BLOCX documentation](https://docs.blocx.org)
for help and more information.
* Ask for help on [BLOCX Discord](http://stayblocxy.com)
* Ask for help on the [BLOCX Forum](https://blocx.org/forum)

Building
---------------------
The following are developer notes on how to build BLOCX Core on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [Dependencies](dependencies.md)
- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [NetBSD Build Notes](build-netbsd.md)
- [Gitian Building Guide](gitian-building.md)

Development
---------------------
The BLOCX Core repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Productivity Notes](productivity.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- Source Code Documentation ***TODO***
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [Travis CI](travis-ci.md)
- [JSON-RPC Interface](JSON-RPC-interface.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)

### Resources
* See the [BLOCX Developer Documentation](https://blocxcore.readme.io/)
  for technical specifications and implementation details.
* Discuss on the [BLOCX Forum](https://blocx.org/forum), in the Development & Technical Discussion board.
* Discuss on [BLOCX Discord](http://stayblocxy.com)
* Discuss on [BLOCX Developers Discord](http://chat.blocxdevs.org/)

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [blocx.conf Configuration File](blocx-conf.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [Reduce Memory](reduce-memory.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [ZMQ](zmq.md)
- [PSBT support](psbt.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
