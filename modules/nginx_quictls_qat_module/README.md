# Intel quictls QAT Module for Nginx

This module integrates the Intel QuickAssist Technology (QAT) engine into the Nginx framework to accelerate cryptographic operations via quictls.

## Installation

1. Add the following configuration line when running `./configure`:
    --add-dynamic-module=modules/nginx_quictls_qat_module/

2. Build Nginx with the required modules:
    ```
    ./configure \
        --with-http_v3_module \
        --with-http_ssl_module \
        --add-dynamic-module=modules/nginx_quictls_qat_module \
        --with-cc-opt="-I<quictls_install_path>/include -Wno-error=deprecated-declarations" \
        --with-ld-opt="-L<quictls_install_path>/lib64"
    make && make install
    ```

## Configuration

1. Load the dynamic module in `nginx.conf`:
    ```
    load_module modules/ngx_quictls_engine_qat_module.so;
    ```

2. Configure the QAT engine using the `ssl_engine` block:
    ```
    ssl_engine {
        use_engine qatengine;
        default_algorithms RSA,EC,PKEY;
        qat_engine {
            qat_offload_mode async;
            qat_notify_mode poll;
            qat_poll_mode internal;
        }
    }
    ```

## Directives

- **qat_offload_mode**
  *Syntax*: `qat_offload_mode async | sync;`
  *Default*: `async`
  *Dependency*: Consistent with the use of `ssl_asynch` directive
  *Description*: Sets synchronous or asynchronous mode

- **qat_notify_mode**
  *Syntax*: `qat_notify_mode event | poll;`
  *Default*: `poll`
  *Description*: Enables event-driven polling feature in QAT engine.

- **qat_poll_mode**
  *Syntax*: `qat_poll_mode inline | internal;`
  *Default*: `internal`
  *Description*:
    - `inline`: Uses a busy loop (only for synchronous RSA computation).
    - `internal`: QAT engine creates an internal polling thread.

- **qat_internal_poll_interval**
  *Syntax*: `qat_internal_poll_interval time;`
  *Default*: `10000`
  *Dependency*: Valid if `qat_poll_mode=internal`
  *Description*: Sets internal polling interval in nanoseconds (valid range: 1 ~ 10000000).

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

2. Changing the polling mode requires a full Nginx restart. It cannot be changed via a soft restart (e.g., `nginx -s reload` or `kill -HUP [master-pid]`).
