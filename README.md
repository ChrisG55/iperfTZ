<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# iperfTZ: A GlobalPlatform compatible network performance tool for trusted applications

----

iperfTZ is a [GlobalPlatform](https://globalplatform.org/) compatible tool to measure I/O throughput within tursted applications in ARM TrustZone.
It uses GlobalPlatform's Generic Interface Socket API for setting up sockets which connect to a remote application.

----

## Building on Unix-like Systems

iperfTZ depends on the build environment provided by a trusted OS.

### OP-TEE

iperfTZ can be built out-of-tree with [OP-TEE](https://www.op-tee.org) using `make`. Three environment variables have to be defined, in order to cross compile and link the client and trusted applications:

* `CROSS_COMPILE`: path to the cross-compilation toolchain
* `TEEC_EXPORT`: path to the TEE Client API library
* `TA_DEV_KIT_DIR`: path to the trusted application development kit

Before `iperfTZ` can be cross-compiled, ensure that the `out/` folder has been generated for the `optee_client` repository. Change into the OP-TEE build repository directory and generate the output:

```
make optee-client-common
```

Afterwards, change back into the `iperfTZ` folder and run the following command.

```
make CROSS_COMPILE=/path/to/optee/toolchains/aarch64/bin/aarch64-linux-gnu- TEEC_EXPORT=/path/to/optee/optee_client/out/export/usr TA_DEV_KIT_DIR=/path/to/optee/optee_os/out/arm/export-ta_arm64
```

---
**NOTE**
The value of the environment variable `TEEC_EXPORT` has changed with
OP-TEE v3.5.
* with OP-TEE v3.4 and earlier:
```
TEEC_EXPORT=/path/to/optee/optee_client/out/export
```
* with OP-TEE v3.5 and later:
```
TEEC_EXPORT=/path/to/optee/optee_client/out/export/usr
```
---

The build output can then be found in the `out/` folder of iperfTZ's base folder.

## Acknowledgement

This work has been supported by EU H2020 ICT project LEGaTO, contract #780681 .
