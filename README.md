# Async Mode for NGINX\*

NGINX (pronounced "engine x") is the world's most popular Web Server, high performance Load Balancer, Reverse Proxy, API Gateway and Content Cache. NGINX is free and open source software, distributed under the terms of a simplified 2-clause BSD-like license, this project extends NGINX with asynchronous capabilities using the OpenSSL\* ASYNC infrastructure. Async Mode for NGINX\* with Intel&reg; QuickAssist Technology (QAT) Acceleration can deliver significant performance improvements.

## Table of Contents

- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [Choosing Your Modules](#choosing-your-modules)
- [Installation Instructions](#installation-instructions)
- [Testing](#testing)
    - [Official Unit Tests](#official-unit-tests)
    - [Performance Testing](#performance-testing)
- [QAT Configuration](#qat-configuration)
    - [SSL Engine (QAT Engine) Configuration](#ssl-engine-qat-engine-configuration)
    - [SSL Provider (QAT Provider) Configuration](#ssl-provider-qat-provider-configuration)
    - [Nginx Side Polling](#nginx-side-polling)
    - [QATzip Module Configuration](#qatzip-module-configuration)
    - [Module Configuration Reference](#module-configuration-reference)
- [Additional Information](#additional-information)
- [Limitations](#limitations)
- [Known Issues](#known-issues)
- [Licensing](#licensing)
- [Legal](#legal)

## Features

* Asynchronous SSL/TLS processing across HTTP, Stream, Mail, and Proxy modules
* Acceleration of public key and symmetric cryptography using QAT_HW and QAT_SW
* Hardware-accelerated GZIP compression via QAT_HW
* Flexible SSL Engine Framework for advanced engine configuration
* OpenSSL provider (`qatprovider`) support via `nginx_provider_qat_module`, as an alternative to the legacy ENGINE framework
* Support for both external and heuristic polling modes
* Automatic release of hardware resources during worker shutdown (see `modules/nginx_qat_module/README` for details)
* OpenSSL Cipher PIPELINE capability support
* Software fallback for asymmetric cryptography (Linux & FreeBSD) and symmetric algorithms (FreeBSD only)
* QUIC support using BoringSSL, quictls, and OpenSSL 3.5 libraries
* Key Protection Technology (KPT) support for enhanced private key security and offload of cryptographic operations using QAT hardware (requires special hardware support; Please refer to the [QAT Engine KPT documentation](https://github.com/intel/QAT_Engine/blob/master/docs/qat_hw_kpt.md)) 


## Hardware Requirements

Async Mode for NGINX\* supports QAT_HW Crypto and Compression acceleration on platforms with the following QAT devices:
* [Intel® QuickAssist 4xxx Series](https://www.intel.com/content/www/us/en/products/details/processors/xeon.html)
* [Intel® QuickAssist Adapter 8970](https://www.intel.com/content/www/us/en/products/sku/125200/intel-quickassist-adapter-8970/downloads.html)
* [Intel® QuickAssist Adapter 8960](https://www.intel.com/content/www/us/en/products/sku/125199/intel-quickassist-adapter-8960/downloads.html)
* [Intel® QuickAssist Adapter 8950](https://www.intel.com/content/www/us/en/products/sku/80371/intel-communications-chipset-8950/specifications.html)
* [Intel® Atom™ Processor C3000](https://www.intel.com/content/www/us/en/design/products-and-solutions/processors-and-chipsets/denverton/ns/atom-processor-c3000-series.html)
* [Intel® Atom™ Processor P5900](https://www.intel.com/content/www/us/en/ark/products/series/202693/intel-atom-processor-p-series.html)

QAT_SW Crypto acceleration is supported on platforms starting with the [3rd Generation Intel® Xeon® Scalable Processors](https://www.intel.com/content/www/us/en/products/docs/processors/xeon/3rd-gen-xeon-scalable-processors-brief.html) and later.

## Software Requirements

This release was validated on the following and its dependent libraries
mentioned QAT Engine and QATZip software Requirements section.

* [QAT engine v2.3.0](https://github.com/intel/QAT_Engine)
* [QATzip v2.0.0](https://github.com/intel/QATzip)
* [OpenSSL-3.0.21, OpenSSL-3.5.7 & OpenSSL-4.0.1](https://github.com/openssl/openssl)
* BoringSSL\* - [0.20250415.0](https://github.com/google/boringssl/releases/tag/0.20250415.0)
* quictls - [openssl-3.5.7](https://github.com/quictls/openssl/releases/tag/openssl-3.5.7)

## Choosing Your Modules

Before you start the installation steps below, decide which capabilities you
need. This repository ships four independent dynamic modules that can be
combined in the same build: choose exactly **one** crypto acceleration path
(Engine or Provider), and optionally add GZIP compression and/or QUIC support
alongside it. **Note:** GZIP and QUIC can currently be combined only with the
Engine path; the Provider path is TLS-crypto-only and must be built standalone
(see [Installation Instructions](#installation-instructions)).

| Capability | Module | Scope | README |
|---|---|---|---|
| TLS/crypto offload (legacy) | `nginx_qat_module` | Combinable with GZIP compression and/or QUIC | [modules/nginx_qat_module/README.md](modules/nginx_qat_module/README.md) |
| TLS/crypto offload (modern) | `nginx_provider_qat_module` | TLS crypto offload only — not yet combinable with GZIP compression, QUIC, or BoringSSL | [modules/nginx_provider_qat_module/README.md](modules/nginx_provider_qat_module/README.md) |
| GZIP compression offload | `nginx_qatzip_module` | Only supported with the Engine crypto path | [modules/nginx_qatzip_module/README.md](modules/nginx_qatzip_module/README.md) |
| QUIC/HTTP3 crypto offload | `nginx_quictls_qat_module` / `ngx_bssl_qat_module` | Only supported with the Engine crypto path | [modules/nginx_quictls_qat_module/README.md](modules/nginx_quictls_qat_module/README.md) / [modules/ngx_bssl_qat_module/README.md](modules/ngx_bssl_qat_module/README.md) |

Once you have picked a crypto path, follow [Installation Instructions](#installation-instructions)
below to build it into Nginx, then continue to [QAT Configuration](#qat-configuration)
to configure the module you chose (ENGINE vs PROVIDER).

## Installation Instructions

Pre-requisites: Install OpenSSL and QAT Engine(for Crypto using QAT_HW & QAT_SW) and QATZip(for Compression)
using the [QAT engine](https://github.com/intel/QAT_Engine#installation-instructions)
and [QATzip](https://github.com/intel/QATzip#installation-instructions) installation instructions. 

**Set the following environmental variables:**

`NGINX_INSTALL_DIR` is the directory where Nginx will be installed to

`OPENSSL_LIB` is the directory where the OpenSSL has been installed to

`QZ_ROOT` is the directory where the QATzip has been compiled to

**Configure and build nginx with QAT Engine and QATZip**

```bash
./configure \
  --prefix=$NGINX_INSTALL_DIR \
  --with-http_ssl_module \
  --add-dynamic-module=modules/nginx_qatzip_module \
  --add-dynamic-module=modules/nginx_qat_module/ \
  --with-cc-opt="-DNGX_SECURE_MEM -I$OPENSSL_LIB/include -I$ICP_ROOT/quickassist/include -I$ICP_ROOT/quickassist/include/dc -I$QZ_ROOT/include -Wno-error=deprecated-declarations" \
  --with-ld-opt="-Wl,-rpath=$OPENSSL_LIB/lib64 -L$OPENSSL_LIB/lib64 -L$QZ_ROOT/src -lqatzip -lz"
  make
  make install
```

* To enable QUIC support, include `--with-http_v3_module` in your configuration. Add either `--add-dynamic-module=modules/nginx_quictls_qat_module` for quictls/OpenSSL 3.5 QUIC, or `--add-dynamic-module=modules/ngx_bssl_qat_module` for BoringSSL, depending on your chosen QUIC library. For detailed instructions, refer to the `README.md` file in the corresponding module directory.

* To use the OpenSSL provider path (`qatprovider`) instead of the legacy ENGINE path above, use `nginx_provider_qat_module` in place of `nginx_qat_module`. This path currently covers TLS crypto offload only — QATzip compression, QUIC, and BoringSSL are not yet supported together with it, so build it standalone:

    ```bash
    ./configure \
      --prefix=$NGINX_INSTALL_DIR \
      --with-http_ssl_module \
      --add-dynamic-module=modules/nginx_provider_qat_module/ \
      --with-cc-opt="-DNGX_SECURE_MEM -I$OPENSSL_LIB/include -Wno-error=deprecated-declarations" \
      --with-ld-opt="-Wl,-rpath=$OPENSSL_LIB/lib64 -L$OPENSSL_LIB/lib64 -L$OPENSSL_LIB/lib64/ossl-modules/"
    make && make install
    ```

    At runtime, set `OPENSSL_MODULES=$OPENSSL_LIB/lib64/ossl-modules` so Nginx
    can locate `qatprovider.so`. See
    [modules/nginx_provider_qat_module/README.md](modules/nginx_provider_qat_module/README.md)
    for the full build and runtime instructions.

* Refer QAT Settings [here](https://intel.github.io/quickassist/GSG/2.X/installation.html#running-applications-as-non-root-user) for running Nginx
under non-root user.

Also there is dockerfile available for Async mode nginx with QATlib which can be built into docker images. Please refer [here](dockerfiles/README.md) for more details.

## Testing

### Official Unit tests

These instructions can be found on [Official test](https://github.com/intel/asynch_mode_nginx/blob/master/test/README.md)

### Performance Testing

Performance Testing with Async mode Nginx along with best known build and
runtime configuration along with scripts available [here](https://intel.github.io/quickassist/qatlib/asynch_nginx.html)

## QAT Configuration

Refer to the fully annotated sample configuration files below, which each
include HTTPS in asynchronous mode with TLS1.3 as default and the QAT Hardware
driver configured for crypto (and, for the engine sample, compression):

* [conf/nginx_qat.conf](conf/nginx_qat.conf) - SSL QAT **Engine** (`ssl_engine`/`qat_engine`) in heuristic mode, plus QATZip compression.
* [conf/nginx_qat_provider.conf](conf/nginx_qat_provider.conf) - SSL QAT **Provider** (`qat_provider`) in external polling mode.

Async Mode for NGINX supports two independent integration paths for QAT-accelerated
cryptography; pick one per deployment:

| Path | Module | Sample config |
|---|---|---|
| Engine | [modules/nginx_qat_module](modules/nginx_qat_module/README.md) | [conf/nginx_qat.conf](conf/nginx_qat.conf) |
| Provider | [modules/nginx_provider_qat_module](modules/nginx_provider_qat_module/README.md) | [conf/nginx_qat_provider.conf](conf/nginx_qat_provider.conf) |

Both paths support QAT_HW acceleration of asymmetric and symmetric TLS
operations, async offload, and Nginx-driven external/heuristic polling. See
each module's README (linked in [Module Configuration Reference](#module-configuration-reference))
for the full directive reference and worked examples.

### SSL Engine (QAT Engine) configuration
An SSL Engine Framework is introduced to provide a more powerful and flexible
mechanism to configure Nginx SSL engine directly in the Nginx configuration file
(`nginx.conf`).

Loading Engine via OpenSSL.cnf is not viable in Nginx. Please use the SSL Engine
Framework which provides a more powerful and flexible mechanism to configure
Nginx SSL engine such as Intel&reg; QAT Engine directly in the Nginx
configuration file (`nginx.conf`).

A new configuration block is introduced as `ssl_engine` which provides two
general directives:

Sets the engine module and engine id for OpenSSL async engine. For example:
```bash
Syntax:     use_engine [engine_module_name] [engine_id];
Default:    N/A
Context:    ssl_engine
Description:
            Specify the engine module name against engine id
```

where engine_module_name is optional and should be same as engine id if it is not used.

```bash
Syntax:     default_algorithms [ALL/RSA/EC/...];
Default:    ALL
Context:    ssl_engine
Description:
            Specify the algorithms need to be offloaded to the engine
            More details information please refer to OpenSSL engine
```
If Handshake only acceleration is preferred then default_algorithms should `RSA EC DH DSA PKEY`
and if symmetric only is preferred then use `CIPHERS`. If all algorithms are preferred then `ALL`
must be specified.

A following configuration sub-block can be used to set engine specific
configuration. The name of the sub-block should have a prefix using the
engine name specified in `use_engine`, such as `[engine_name]_engine`.

Any 3rd party modules can be integrated into this framework. By default, a
reference module `dasync_module` is provided in `src/engine/modules`
and a QAT module `nginx_qat_module` is provided in `modules/nginx_qat_modules`.

An example configuration in the `nginx.conf`:
```bash
    load_module modules/ngx_ssl_engine_qat_module.so;
    ...
    ssl_engine {
        use_engine qatengine;
        default_algorithms RSA,EC,DH,PKEY_CRYPTO;
        qat_engine {
            qat_sw_fallback on;
            qat_offload_mode async;
            qat_notify_mode poll;
            qat_poll_mode heuristic;
            qat_shutting_down_release on;
        }
    }
```
For more details directives of `nginx_qat_module`, please refer to
[modules/nginx_qat_module/README.md](modules/nginx_qat_module/README.md).

### SSL Provider (QAT Provider) Configuration

An OpenSSL provider framework is also supported for QAT acceleration through
`nginx_provider_qat_module`, as a modern alternative to the `ssl_engine`
framework above.

A new configuration block is introduced as `qat_provider`, a top-level block
analogous to `ssl_engine`:

```bash
    load_module modules/ngx_ssl_provider_qat_module.so;
    ...
    qat_provider {
        qat_offload_mode async;
        qat_notify_mode poll;
        qat_poll_mode external;
        qat_sw_fallback off;
    }
```

`load_module modules/ngx_ssl_provider_qat_module.so;` is required in
`nginx.conf` for every provider deployment. Configure `qat_provider` from
either `nginx.conf` or `openssl.cnf`; `openssl.cnf` takes priority when it
configures the provider. For details, see
[modules/nginx_provider_qat_module/README.md](modules/nginx_provider_qat_module/README.md).

### Nginx Side Polling

Both the Engine and Provider paths support Nginx-side external and heuristic
polling, but expose it through different directives and defaults. Full
directive references, examples, and notes live in each module's README:

* [modules/nginx_qat_module/README.md](modules/nginx_qat_module/README.md) - `qat_external_poll_interval`, `qat_heuristic_poll_asym_threshold`, `qat_heuristic_poll_sym_threshold` (also covers `inline` and event-driven polling, which are Engine-only).
* [modules/nginx_provider_qat_module/README.md](modules/nginx_provider_qat_module/README.md) - `qat_poll_interval`, `qat_hw_asym_threshold`, `qat_hw_sym_threshold`, `qat_sw_threshold` (also covers the `openssl.cnf` dual-configuration precedence).

### QATzip Module Configuration

The QATzip module enables hardware-accelerated GZIP compression using QAT
accelerators via the QATzip stream API (introduced in v0.2.6). It is
currently only supported alongside the Engine crypto path, not the Provider
path. Directives, notes on software fallback, and a worked example have
moved to
[modules/nginx_qatzip_module/README.md](modules/nginx_qatzip_module/README.md).

### Module Configuration Reference

Each acceleration path is implemented as a separate dynamic module with its
own directives, defaults, and examples. Use these links for installation and
configuration details of each:

* **QAT Engine** (crypto, legacy ENGINE API) - [modules/nginx_qat_module/README.md](modules/nginx_qat_module/README.md)
* **QAT Provider** (crypto, OpenSSL provider API) - [modules/nginx_provider_qat_module/README.md](modules/nginx_provider_qat_module/README.md)
* **QATzip** (GZIP compression) - [modules/nginx_qatzip_module/README.md](modules/nginx_qatzip_module/README.md)
* **QUIC** (quictls or BoringSSL) - [modules/nginx_quictls_qat_module/README.md](modules/nginx_quictls_qat_module/README.md) or [modules/ngx_bssl_qat_module/README.md](modules/ngx_bssl_qat_module/README.md)

## Additional Information

### Generating Async mode Patch
* Async Mode for NGINX\* is developed based on Nginx-1.29.3. Refer steps below
to generate patch to apply on top of official Nginx-1.29.3.

#### Generating the patch with package from Nginx download page
```bash
  git clone https://github.com/intel/asynch_mode_nginx.git asynch_mode_nginx
    wget http://nginx.org/download/nginx-1.29.3.tar.gz
    tar -xvzf nginx-1.29.3.tar.gz
    diff -Naru -x .git nginx-1.29.3 asynch_mode_nginx > async_mode_nginx_1.29.3.patch
  # Apply Patch
    cd nginx-1.29.3
    patch -p1 < ../async_mode_nginx_1.29.3.patch
```

#### Generating the patch with package from Github Mirror page

```bash
  git clone https://github.com/intel/asynch_mode_nginx.git
    wget https://github.com/nginx/nginx/archive/release-1.29.3.tar.gz
    tar -xvzf release-1.29.3.tar.gz
    diff -Naru -x .git -x .hgtags nginx-release-1.29.3 asynch_mode_nginx > async_mode_nginx_1.29.3.patch
  # Apply Patch
    patch -p0 < async_mode_nginx_1.29.3.patch
```

### New Async Directives

**Directives**
```bash
Syntax:     ssl_asynch on | off;
Default:    ssl_asynch off;
Context:    stream, mail, http, server

    Enables SSL/TLS asynchronous mode
```
**Example**

file: conf/nginx.conf

```bash
    http {
        ssl_asynch  on;
        server {...}
    }

    stream {
        ssl_asynch  on;
        server {...}
    }
```

**Directives**
```bash
Syntax:     proxy_ssl_asynch on | off;
Default:    proxy_ssl_asynch off;
Context:    stream, http, server

Enables the SSL/TLS protocol asynchronous mode for connections to a proxied server.
```

**Example**

file: conf/nginx.conf

```bash
    http {
        server {
            location /ssl {
                proxy_pass https://127.0.0.1:8082/outer;
                proxy_ssl_asynch on;
            }
        }
    }
```

**Directives**
```bash
Syntax:     grpc_ssl_asynch on | off;
Default:    grpc_ssl_asynch off;
Context:    http, server

Enables the SSL/TLS protocol asynchronous mode for connections to a grpc server.
```

**Example**

file: conf/nginx.conf

```bash
    http {
        server {
            location /grpcs {
                grpc_pass https://127.0.0.1:8082/outer;
                grpc_ssl_asynch on;
            }
        }
    }
```

* OpenSSL Cipher PIPELINE Capability. Refer [OpenSSL Docs](https://docs.openssl.org/1.1.1/man3/SSL_CTX_set_split_send_fragment)
for detailed information.

**Directives**
```bash
Syntax:     ssl_max_pipelines size;
Default:    ssl_max_pipelines 0;
Context:    server

Set MAX number of pipelines

Syntax:     ssl_split_send_fragment size;
Default:    ssl_split_send_fragment 0;
Context:    server

Set split size of sending fragment

Syntax:     ssl_max_send_fragment size;
Default:    ssl_max_send_fragment 0;
Context:    server

Set max number of sending fragment
```

## Limitations
* When using QAT hardware for crypto offload, ensure that the number of available QAT instances is at least twice the number of Nginx worker processes. For example, if your `nginx.conf` specifies:

    ```bash
    worker_processes 16;
    ```

    Then, in your QAT driver configuration file, set:

    ```bash
    [SHIM]
    NumberCyInstances = 1
    NumberDcInstances = 0
    NumProcesses = 32
    LimitDevAccess = 0
    ```

* If `worker_processes auto` is configured, the number of QAT instances should be equal to or greater than twice the number of CPU cores. Insufficient instances may cause issues due to resource shortages.

* The QAT engine and QATzip module use the User Space DMA-able Memory (USDM) component by default. USDM is located in `quickassist/utilities/libusdm_drv` within the Intel® QAT Driver source. Ensure USDM is enabled and sufficient memory is allocated before using `nginx_qat_module`, `nginx_provider_qat_module`, or `nginx_qatzip_module`. For example:

    ```bash
    echo 2048 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    insmod ./usdm_drv.ko max_huge_pages=2048 max_huge_pages_per_process=32
    ```

* The AES-CBC-HMAC-SHA algorithm is not offloaded to QAT_HW when Encrypt-then-MAC (ETM) mode is used for SSL connections (the default). See [QAT_Engine Limitations](https://github.com/intel/QAT_Engine/blob/master/docs/limitations.md#limitations) for details.

* The QATzip module currently supports GZIP compression acceleration but does not support user-defined dictionary compression.

* HTTP/2 protocol is not supported for asynchronous functionality.

* External and Heuristic polling is not supported with QUIC enabled through BoringSSL library.

## Known Issues

- **QAT instance allocation by cache manager/loader processes:**
    Due to the QAT engine's `atfork` hook, child processes (such as cache manager/loader) automatically initialize the QAT engine and allocate QAT instances, similar to worker processes. However, these processes do not invoke the modules' `exit process` method, which can result in "Orphan ring" messages in `dmesg`. This applies to both the Engine and Provider paths, since `qatprovider` links the same qatengine `atfork` hook.

- **Segmentation fault on HUP signal with insufficient QAT instances:**
    If the number of available QAT instances is less than twice the number of Nginx worker processes, sending a HUP signal may cause a segmentation fault. As a workaround, enable `qat_sw_fallback on;` in the `qat_engine` directive (Engine path) or the `qat_provider` directive (Provider path). Ensure sufficient QAT instances, especially when using `worker_processes auto;`.
    Running Nginx without adequate hardware resources (memory or disk space) can cause a core dump when a HUP signal is received during the handshake phase. Ensure enough resources are allocated before starting Nginx.

- **Performance degradation with OpenSSL 3.0:**
    ECDH and PRF operations may experience performance drops under OpenSSL 3.0. TLS handshake performance is significantly reduced due to framework changes. See [#21833](https://github.com/openssl/openssl/issues/21833) for details.

- **0-RTT (early data) not supported in async mode:**
    The 0-RTT (early data) feature is not supported with async mode in asynch_mode_nginx. Avoid using async offload to QAT hardware during early data processing.

- **CHACHA-POLY and AES-GCM throughput limitations:**
    Memory allocation bottlenecks can limit the throughput of CHACHA-POLY and AES-GCM algorithms when using four or more QAT devices.

## Licensing

Released under BSD License. Please see the `LICENSE` file contained in the top
level folder. Further details can be found in the file headers of the relevant files.

## Legal

Intel, Intel Atom, and Xeon are trademarks of Intel Corporation in the U.S. and/or other countries.

\*Other names and brands may be claimed as the property of others.

Copyright © 2014-2026, Intel Corporation. All rights reserved.
