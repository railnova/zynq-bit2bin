# zynq-bit2bin

## Background

This repository provides a command line utility called `zynq-bit2bin`, to
convert FPGA bitstreams (at least for the Zynq7000 platform) from the `.bit`
format into the `.bin` format.

This is because Vivado produces `.bit` files, that can be used with
`/dev/xdevcfg` on Linux 4.9. With Linux 5.4, direct usage of xdevcfg is
deprecated, in favor the the fpga-manager framework.

Read more about the usage of fpga-manager to load Zynq bitstream at runtime
from Linux on the 
[Xilinx wiki](https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/18841645/Solution+Zynq+PL+Programming+With+FPGA+Manager)

## Usage

### pre-compiled version

A compiled version for Linux on amd64 is available on the
[latest release](https://github.com/railnova/zynq-bit2bin/releases/latest)
on this repository.

### Build it yourself

To build the program for your machine, simply run:

```bash
make
```

### Using zynq-bit2bin

The program reads from the standard input, and writes to the standard output.
For example, running it in the shell:

```bash
zynq-bit2bin < input-file.bit > output-file.bin
```

## License

This program is distributed under the MIT License.
