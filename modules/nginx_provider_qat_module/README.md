# Intel QAT Provider Module for Nginx

`nginx_provider_qat_module` integrates Intel® QuickAssist Technology (QAT)
acceleration into Nginx through the **OpenSSL provider API** (`qatprovider`),
as an alternative to the legacy ENGINE-based [nginx_qat_module](../nginx_qat_module/README.md).
It loads `qatprovider` (from the QAT Engine project) into the OpenSSL provider
stack and, for external/heuristic polling, drives the poll loop from
Nginx's event loop and SSL hot path.

## Table of Contents

- [Installation](#installation)
- [Quick Start](#quick-start)
- [Directives](#directives)
- [Scope](#scope)
- [Polling Modes](#polling-modes)
- [Nginx Side Polling](#nginx-side-polling)
    - [External Polling Mode](#external-polling-mode)
    - [Heuristic Polling Mode](#heuristic-polling-mode)
- [Dual Configuration Mode: nginx.conf vs openssl.cnf](#dual-configuration-mode-nginxconf-vs-opensslcnf)
- [Configuring qatprovider via openssl.cnf (qatengine)](#configuring-qatprovider-via-opensslcnf-qatengine)
- [Differences from the QAT Engine Module](#differences-from-the-qat-engine-module)
- [Software Fallback and Shutdown](#software-fallback-and-shutdown)
- [Reload Rule](#reload-rule)
- [Verification](#verification)
- [Related Files](#related-files)

## Installation

1. Add the following line to your `configure`/`config.example` invocation:
    ```
    --add-dynamic-module=modules/nginx_provider_qat_module/
    ```

2. Build Nginx with the module, pointing at an OpenSSL install that ships
   `qatprovider` (built from the [QAT Engine](https://github.com/intel/QAT_Engine)
   project — follow its [README](https://github.com/intel/QAT_Engine/blob/master/README.md) for build instructions):
    ```bash
    ./configure \
      --prefix=$NGINX_INSTALL_DIR \
      --with-http_ssl_module \
      --add-dynamic-module=modules/nginx_provider_qat_module/ \
      --with-cc-opt="-DNGX_SECURE_MEM -I$OPENSSL_LIB/include -Wno-error=deprecated-declarations" \
      --with-ld-opt="-Wl,-rpath=$OPENSSL_LIB/lib64 -L$OPENSSL_LIB/lib64 -L$OPENSSL_LIB/lib64/ossl-modules/"
    make && make install
    ```

3. At runtime, `qatprovider.so` must be discoverable by OpenSSL. Set:
    ```bash
    export OPENSSL_MODULES=$OPENSSL_LIB/lib64/ossl-modules
    export LD_LIBRARY_PATH=$OPENSSL_LIB/lib64
    ```
    If this is not set, Nginx fails to start with:
    `"QAT provider: qatprovider could not be loaded (preload); ... ensure
    qatprovider.so is in the OpenSSL modules directory (OPENSSL_MODULES)"`.

4. The worker user needs to both read `qatprovider.so` **and** access the QAT
   device nodes (e.g. `/dev/vfio/*`, `/dev/qat_adf_ctl`) — `user root;` covers
   both but isn't required. For non-root setup, see the QAT engine's
   [non-root usage guide](https://intel.github.io/quickassist/GSG/2.X/installation.html#running-applications-as-non-root-user)
   and the device permission example in
   [dockerfiles/README.md](../../dockerfiles/README.md).

## Quick Start

A ready-to-run sample configuration is provided at
[conf/nginx_qat_provider.conf](../../conf/nginx_qat_provider.conf) (default:
external polling).

Minimal configuration:

```nginx
load_module modules/ngx_ssl_provider_qat_module.so;

qat_provider {
    qat_offload_mode async;
    qat_notify_mode poll;
    qat_poll_mode external;
    qat_sw_fallback off;
}

events {
    worker_connections 8192;
}

http {
    server {
        listen 443 ssl;
        ssl_asynch on;
        ssl_certificate     cert.pem;
        ssl_certificate_key  key.pem;
    }
}
```

For more details, see [Directives](#directives).

## Directives

All directives below are set inside the `qat_provider { ... }` block, which is
a top-level (main context) block, similar to `ssl_engine { ... }` for the
engine module.

| Directive | Syntax / Default | Description |
|---|---|---|
| **qat_offload_mode** | `async` \| `sync`<br>Default: `async` | Synchronous or asynchronous offload. `sync` is only valid with `qat_poll_mode internal`. |
| **qat_notify_mode** | `poll`<br>Default: `poll` | Notification mode. Only `poll` is supported; `event` is rejected at startup (see [Differences from the QAT Engine Module](#differences-from-the-qat-engine-module)). |
| **qat_poll_mode** | `internal` \| `external` \| `heuristic`<br>Default: `internal` | Polling mode for collecting hardware/software-fallback completions. There is **no `inline` mode** in this module (see [Differences](#differences-from-the-qat-engine-module)). |
| **qat_poll_interval** | integer<br>No fixed module default | Poll interval. Units and range depend on `qat_poll_mode`:<br>• `internal`: **nanoseconds**, effective range `1..1000000` — this is qatprovider's own contract (the value is stored directly as `tv_nsec`); values above `1000000` are rejected by qatprovider with a warning and the previous/built-in interval is kept, even though this module's own parser currently accepts up to `10000000` at parse time. Stay within `1..1000000`. If omitted, qatprovider's built-in default of `10000` ns (10 µs) applies.<br>• `external` / `heuristic`: milliseconds, range `1..1000`, default `1`. |
| **qat_hw_asym_threshold** | integer, `1..512`<br>Default: `48` | In-flight HW asymmetric (PKE) request count that triggers an immediate poll. Used only in `heuristic` mode. |
| **qat_hw_sym_threshold** | integer, `1..512`<br>Default: `24` | In-flight HW symmetric (cipher) request count that triggers an immediate poll. Used only in `heuristic` mode. |
| **qat_sw_threshold** | integer, `1..512`<br>Default: `8` | In-flight QAT_SW (multi-buffer) queue depth that triggers an immediate poll. Governs the shared asymmetric, symmetric, and KDF multi-buffer queues. Used only in `heuristic` mode. |
| **qat_sw_fallback** | `on` \| `off`<br>Default: `off` | Enables qatprovider software fallback on QAT hardware failures. When on with `external`/`heuristic` polling, the module also enables a 1-second heartbeat poll for device-health detection. |
| **qat_shutting_down_release** | `on` \| `off`<br>Default: `off` | Retained for compatibility with the engine module's directive of the same name. Requires `external` or `heuristic` polling if enabled, but `qatprovider` always offloads CIPHERS, so graceful release is unsupported: the module logs an error and forces this back to `off` whenever it is set to `on`. |
| **qat_small_pkt_offload_threshold** | string<br>No module default | Same syntax as the engine module's directive of the same name (a bare size, or `CIPHER:size` pairs). Optional — when unset, qatprovider's own built-in threshold applies. Only takes effect in nginx-owned configuration; ignored when `openssl.cnf` owns the configuration (see below). |

## Scope

This module covers TLS crypto offload only. QATzip compression, QUIC, and
BoringSSL are not yet supported together with it.

## Polling Modes

- **internal**: `qatprovider` owns a dedicated polling thread. Simplest to
  configure; no Nginx-side poll loop is involved.
- **external**: Nginx polls `qatprovider` from its event loop and from the SSL
  hot path on a timer (see [External Polling Mode](#external-polling-mode)).
- **heuristic**: an optimization over `external` — Nginx polls opportunistically
  based on in-flight request thresholds, with the timer kept as a backstop
  drain mechanism (see [Heuristic Polling Mode](#heuristic-polling-mode)).

All three modes work with `qat_offload_mode async` and `qat_notify_mode poll`;
`internal` additionally works with `qat_offload_mode sync`.

| offload | notify | internal | external | heuristic |
|---|---|---|---|---|
| sync  | poll  | Y | N | N |
| async | poll  | Y | Y | Y |

**Changing Polling Mode:** Polling mode changes require a full Nginx restart.
Soft restarts (e.g. `nginx -s reload` or `kill -HUP [master-pid]`) do **not**
apply polling mode changes — see [Reload Rule](#reload-rule).

## Nginx Side Polling

The provider module supports the same two kinds of Nginx-side polling as the
engine module:

* external polling mode
* heuristic polling mode

### External Polling Mode

A timer is armed in each Nginx worker process, and the module also polls
`qatprovider` inline from the SSL read/write hot path, to collect accelerator
responses.

**Directive**
```bash
Syntax:     qat_poll_interval time;
Default:    1
Dependency: Valid if (qat_poll_mode=external), unit = milliseconds
Context:    qat_provider
Description:
            External polling time interval (ms)
            Valid value: 1 ~ 1000
```

**Example**

file: `conf/nginx_qat_provider.conf`

```nginx
    load_module modules/ngx_ssl_provider_qat_module.so;
    ...
    qat_provider {
        qat_offload_mode async;
        qat_notify_mode poll;

        qat_poll_mode external;
    }
```

### Heuristic Polling Mode

Heuristic polling is an enhancement over timer-based external polling: it
polls opportunistically based on in-flight request volume so that response
collection tracks request submission, maximizing throughput while bounding
latency. The external-polling timer stays active as a backstop drain.

Polling is triggered when either condition is met:

* The number of in-flight offload requests reaches the configured threshold
  for that request class (`qat_hw_asym_threshold`, `qat_hw_sym_threshold`, or
  `qat_sw_threshold`).
* All active SSL connections have submitted their cryptographic requests and
  are awaiting responses.

**Directives**
```bash
Syntax:     qat_hw_asym_threshold num;
Default:    48
Dependency: Valid if (qat_poll_mode=heuristic)
Context:    qat_provider
Description:
            Threshold of the number of in-flight HW asymmetric requests to
            trigger a polling operation.
            Valid value: 1 ~ 512

Syntax:     qat_hw_sym_threshold num;
Default:    24
Dependency: Valid if (qat_poll_mode=heuristic)
Context:    qat_provider
Description:
            Threshold of the number of in-flight HW symmetric requests to
            trigger a polling operation.
            Valid value: 1 ~ 512

Syntax:     qat_sw_threshold num;
Default:    8
Dependency: Valid if (qat_poll_mode=heuristic)
Context:    qat_provider
Description:
            Threshold of the number of in-flight QAT_SW (multi-buffer) items
            to trigger a polling operation.
            Valid value: 1 ~ 512
```

**Example**

file: `conf/nginx_qat_provider.conf`

```nginx
    load_module modules/ngx_ssl_provider_qat_module.so;
    ...
    qat_provider {
        qat_offload_mode async;
        qat_notify_mode poll;

        qat_poll_mode heuristic;
        qat_hw_asym_threshold 48;
        qat_hw_sym_threshold 24;
        qat_sw_threshold 8;
    }
```

**Note**: unlike the engine module (which exposes separate
`qat_heuristic_poll_asym_threshold` / `qat_heuristic_poll_sym_threshold`
directives, plus a separate `qat_external_poll_interval`), the provider module
uses a single `qat_poll_interval` directive shared by external and heuristic
mode, and splits the in-flight threshold into three classes
(`qat_hw_asym_threshold`, `qat_hw_sym_threshold`, `qat_sw_threshold`) because
qatprovider tracks HW and QAT_SW (multi-buffer) work separately. Do not reuse
the engine module's directive names in a `qat_provider {}` block.

## Dual Configuration Mode: nginx.conf vs openssl.cnf

`qatprovider` can be configured from **either** the `qat_provider {}` block in
`nginx.conf` **or** from `openssl.cnf`. These are not independent, mergeable
layers — exactly one of them is authoritative per worker:

| Source | Priority | Notes |
|---|---|---|
| `openssl.cnf` | **Highest** | If `openssl.cnf` pins a QAT polling mode (`qat_poll_mode=...` in `[qatprovider_sect]`), it is authoritative for **all** provider polling settings, including selecting `internal` polling. Any `qat_provider {}` block in `nginx.conf` is ignored (a warning is logged). |
| `nginx.conf` (`qat_provider {}`) | Second | Used whenever `openssl.cnf` does not pin a polling mode (e.g. it is absent, or only sets `activate = 1` without `qat_poll_mode`). |

## Configuring qatprovider via openssl.cnf (qatengine)

`openssl.cnf` syntax and semantics are owned by the QAT Engine project, not
this module. Follow the QAT Engine project's own setup and configuration
documentation for the qatprovider version you built, and use this module only
for the Nginx-side external/heuristic polling directives described above.

## Differences from the QAT Engine Module

`nginx_provider_qat_module` and `nginx_qat_module` both accelerate TLS crypto
with QAT, but they are not drop-in equivalents:

| Capability | `nginx_qat_module` (ENGINE) | `nginx_provider_qat_module` (PROVIDER) |
|---|---|---|
| OpenSSL API | Legacy `ENGINE` | OpenSSL provider |
| Config block | `ssl_engine { qat_engine { ... } }` | `qat_provider { ... }` |
| `inline` poll mode | Yes (busy-loop, sync RSA only) | **Not available** |
| `qat_notify_mode event` (event-driven) | Yes | **Not exposed** — rejected at config time |
| Poll interval directive(s) | Separate `qat_internal_poll_interval` / `qat_external_poll_interval` | Single `qat_poll_interval` (units depend on `qat_poll_mode`) |
| Heuristic threshold directive(s) | `qat_heuristic_poll_asym_threshold` / `qat_heuristic_poll_sym_threshold` | `qat_hw_asym_threshold` / `qat_hw_sym_threshold` / `qat_sw_threshold` (adds a QAT_SW multi-buffer class) |
| `openssl.cnf` configuration | Not supported (must use `ssl_engine {}`) | Supported and can take priority over `nginx.conf` (see above) |
| Dynamic release on shutdown | Configurable via `qat_shutting_down_release` | Directive accepted for compatibility but always forced to `off` (CIPHERS are always offloaded by qatprovider) |
| `use_engine` (selects ENGINE + id) | Required in `ssl_engine {}` | **Not needed** — no equivalent directive |
| `default_algorithms` (selects offloaded algorithms) | Required in `ssl_engine {}` | **Not available** via `nginx.conf`; restrict negotiated algorithms instead via `Ciphersuites`/`CipherString` in `openssl.cnf`'s `[system_default_sect]` (only takes effect if `nginx.conf`'s own `ssl_ciphers`/`ssl_conf_command` does not already set them) |

## Software Fallback and Shutdown

`qat_sw_fallback on;` enables qatprovider software fallback. With `external` or
`heuristic` polling, the module also enables a one-second heartbeat poll used
for device-health detection while running on software fallback.

`qat_shutting_down_release` is retained for compatibility, but since cipher
offload is always available through qatprovider, the module disables graceful
QAT release whenever it would be unsafe (see the directive table above). The
external/heuristic shutdown path still drains outstanding requests (bounded
poll attempts) before worker exit.

## Reload Rule

Do not change an existing `external` or `heuristic` deployment to `internal`
polling through a live reload (`nginx -s reload` / `SIGHUP`). The module
detects the attempted switch, keeps the previously-committed mode, and logs
the rejected change. Restart Nginx to change polling mode.

## Verification

`error` is the production default (the shipped sample uses it) and does not
print the messages below. Switch to `notice` to confirm which file
(`nginx.conf` or `openssl.cnf`) is driving the configuration, including
whether `openssl.cnf` was adopted; use `debug` for full debug-level logging
(including polling traces), which requires Nginx built with `--with-debug`.
Revert to `error` afterwards. Check which file is driving the configuration:

```text
QAT provider: configuration source = nginx.conf (qat_provider block)
```

or:

```text
QAT provider: configuration source = openssl.cnf [/path/to/openssl.cnf]
QAT provider: adopted configuration from openssl.cnf (external=1, heuristic=0, internal=0, ...)
```

A successful TLS handshake alone does **not** prove hardware offload. Also
confirm qatprovider's hardware-init/offload log messages, or check the
platform's QAT telemetry counters (`/sys/kernel/debug/qat_*/fw_counters`)
while the workload runs.

## Related Files

- [conf/nginx_qat_provider.conf](../../conf/nginx_qat_provider.conf) — annotated sample, default external polling
- [Main README](../../README.md)
- [QAT Engine module README](../nginx_qat_module/README.md) — ENGINE-based equivalent
- [QAT Engine project](https://github.com/intel/QAT_Engine) — `openssl.cnf` setup and configuration for `qatprovider`
