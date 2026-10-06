# Intel QAT Module for Nginx

This module integrates the Intel QuickAssist Technology (QAT) engine into the Nginx framework to accelerate cryptographic operations via OpenSSL.

## Installation

1. Add the following line to your `config.example`:
    ```
    --add-dynamic-module=modules/nginx_qat_module/
    ```
2. Build Nginx with the added module:
    ```
    $ ./config.example
    $ make && make install
    ```

## Configuration

1. Load the dynamic module in your `nginx.conf`:
    ```
    load_module modules/ngx_ssl_engine_qat_module.so;
    ```
2. Configure the QAT engine using the `ssl_engine` block:
    ```nginx
    ssl_engine {
        use_engine qatengine;
        default_algorithms RSA,EC,DH,DSA,PKEY;
        qat_engine {
            qat_offload_mode async;
            qat_notify_mode poll;
            qat_poll_mode heuristic;
            qat_sw_fallback on;
        }
    }
    ```
3. The worker user needs to both read `qatengine.so` **and** access the QAT
   device nodes (e.g. `/dev/vfio/*`, `/dev/qat_adf_ctl`) — `user root;` covers
   both but isn't required. For non-root setup, see the QAT engine's
   [non-root usage guide](https://intel.github.io/quickassist/GSG/2.X/installation.html#running-applications-as-non-root-user)
   and the device permission example in
   [dockerfiles/README.md](../../dockerfiles/README.md).

## Directives

| Directive                          | Syntax / Default         | Description                                                                                                   |
|-------------------------------------|-------------------------|---------------------------------------------------------------------------------------------------------------|
| **qat_sw_fallback**                 | `on` \| `off`<br>Default: `off` | Enables software fallback for supported TLS1.2 ciphers if QAT hardware fails.                                 |
| **qat_offload_mode**                | `async` \| `sync`<br>Default: `async` | Sets synchronous or asynchronous offload mode. Consistent with `ssl_asynch` directive.                        |
| **qat_notify_mode**                 | `event` \| `poll`<br>Default: `poll` | Enables event-driven or polling notification for QAT engine.                                                  |
| **qat_poll_mode**                   | `inline` \| `internal` \| `external` \| `heuristic`<br>Default: `internal` | Sets polling mode for hardware accelerator messages. See below for details.                                   |
| **qat_shutting_down_release**       | `on` \| `off`<br>Default: `off` | Releases QAT instance on worker shutdown for keep-alive connections. Only valid with `external` or `heuristic` poll modes. |
| **qat_internal_poll_interval**      | *time* (ns)<br>Default: `10000` | Sets internal polling interval (valid if `qat_poll_mode=internal`). Range: 1–1,000,000.                       |
| **qat_external_poll_interval**      | *time* (ms)<br>Default: `1` | Sets external polling interval (valid if `qat_poll_mode=external`). Range: 1–1,000.                           |
| **qat_heuristic_poll_asym_threshold** | *num*<br>Default: `48` | Sets in-flight asymmetric request threshold for polling (valid if `qat_poll_mode=heuristic`). Range: 1–512.   |
| **qat_heuristic_poll_sym_threshold**  | *num*<br>Default: `24` | Sets in-flight symmetric request threshold for polling (valid if `qat_poll_mode=heuristic`). Range: 1–512.    |
| **qat_heuristic_poll_asym_multibuff_threshold** | *num*<br>Default: `8` | Sets in-flight QAT_SW (multi-buffer) asymmetric queue threshold for polling (valid if `qat_poll_mode=heuristic`). Range: 1–512. |
| **qat_heuristic_poll_sym_multibuff_threshold**  | *num*<br>Default: `8` | Sets in-flight QAT_SW (multi-buffer) symmetric queue threshold for polling (valid if `qat_poll_mode=heuristic`). Range: 1–512. |
| **qat_small_pkt_offload_threshold**   | *string ...*<br>Default: `2048` | Sets per-cipher offload threshold. Example:<br>`2048`<br>`AES-128-CBC-HMAC-SHA1:4096`<br>`AES-128-CBC-HMAC-SHA1:4096 AES-256-CBC-HMAC-SHA1:8192` |

### Polling Modes

- **inline**: Busy loop (only for synchronous RSA).
- **internal**: QAT engine creates a polling thread.
- **external**: Timer-based polling in each Nginx worker.
- **heuristic**: Improved external polling, adapts based on application state.

## Nginx Side Polling

The qat_module provides two kinds of Nginx side polling for QAT engine,

* external polling mode
* heuristic polling mode

### External Polling Mode

A timer-based polling is employed in each Nginx worker process to collect
accelerator's response.

**Directives in the qat_module**
```bash
Syntax:     qat_external_poll_interval time;
Default:    1
Dependency: Valid if (qat_poll_mode=external)
Context:    qat_engine
Description:
            External polling time interval (ms)
            Valid value: 1 ~ 1000
```
**Example**
file: conf/nginx.conf

```bash
    load_module modules/ngx_ssl_engine_qat_module.so;
    ...
    ssl_engine {
        use_engine qatengine;
        default_algorithms ALL;
        qat_engine {
            qat_offload_mode async;
            qat_notify_mode poll;

            qat_poll_mode external;
            qat_external_poll_interval 1;
        }
    }
```

### Heuristic Polling Mode
Heuristic polling mode is an enhancement over timer-based external polling: it
polls opportunistically based on live in-flight request/queue counts against
the configured thresholds below, rather than on a fixed timer alone, keeping
response collection closely aligned with request submission. The thresholds
are static, configured values — this mode does not adjust them dynamically
based on traffic patterns.

**Notes:**

* Heuristic polling mode requires QAT engine version v0.5.35 or later.
* When heuristic polling is enabled, the external polling timer is also active by default.

In heuristic polling mode, polling operations are triggered only under specific conditions to balance efficiency and responsiveness:

* The number of in-flight offload requests reaches a configurable threshold.
* All active SSL connections have submitted their cryptographic requests and are awaiting responses.

**Directives in the qat_module**
```bash
Syntax:     qat_heuristic_poll_asym_threshold num;
Default:    48
Dependency: Valid if (qat_poll_mode=heuristic)
Context:    qat_engine
Description:
            Threshold of the number of in-flight requests to trigger a polling
            operation when there are in-flight asymmetric crypto requests
            Valid value: 1 ~ 512


Syntax:     qat_heuristic_poll_sym_threshold num;
Default:    24
Dependency: Valid if (qat_poll_mode=heuristic)
Context:    qat_engine
Description:
            Threshold of the number of in-flight requests to trigger a polling
            operation when there is no in-flight asymmetric crypto request
            Valid value: 1 ~ 512

Syntax:     qat_heuristic_poll_asym_multibuff_threshold num;
Default:    8
Dependency: Valid if (qat_poll_mode=heuristic)
Context:    qat_engine
Description:
            Threshold of the number of in-flight QAT_SW (multi-buffer)
            asymmetric queue items to trigger a polling operation
            Valid value: 1 ~ 512

Syntax:     qat_heuristic_poll_sym_multibuff_threshold num;
Default:    8
Dependency: Valid if (qat_poll_mode=heuristic)
Context:    qat_engine
Description:
            Threshold of the number of in-flight QAT_SW (multi-buffer)
            symmetric queue items to trigger a polling operation
            Valid value: 1 ~ 512
```
**Example**
file: `conf/nginx.conf`

```bash
    load_module modules/ngx_ssl_engine_qat_module.so;
    ...
    ssl_engine {
        use_engine qatengine;
        default_algorithms ALL;
        qat_engine {
            qat_offload_mode async;
            qat_notify_mode poll;

            qat_poll_mode heuristic;
            qat_heuristic_poll_asym_threshold 48;
            qat_heuristic_poll_sym_threshold 24;
            qat_heuristic_poll_asym_multibuff_threshold 8;
            qat_heuristic_poll_sym_multibuff_threshold 8;
        }
    }
```

## Notes

1. **Valid Combinations of Offload, Notify, and Poll Modes**

    | Mode   | inline | internal | external | heuristic |
    |--------|--------|----------|----------|-----------|
    | sync   |   N    |    Y     |    N     |     N     |
    | event  |   N    |    Y     |    N     |     N     |
    | poll   |   Y    |    Y     |    N     |     N     |
    | async  |   N    |    Y     |    Y     |     Y     |
    | event  |   N    |    Y     |    N     |     N     |
    | poll   |   N    |    Y     |    Y     |     Y     |

2. **Changing Polling Mode**

    - Polling mode changes require a full Nginx restart.
    - Soft restarts (e.g., `nginx -s reload` or `kill -HUP [master-pid]`) do **not** apply polling mode changes.

