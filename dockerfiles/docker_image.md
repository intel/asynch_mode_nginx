# Introduction
NGINX (pronounced "engine x") is the world's most popular Web Server, high performance Load Balancer, Reverse Proxy, API Gateway and Content Cache. NGINX is free and open source software, distributed under the terms of a simplified 2-clause BSD-like license, this project extends NGINX with asynchronous capabilities using the OpenSSL* ASYNC infrastructure. Async Mode for NGINX* with Intel® QuickAssist Technology (QAT) Acceleration can deliver significant performance improvements.

# Docker Image
- intel/intel-async-mode-nginx:1.0.0-1

# How to Use This Image

## BIOS Setting
- Intel VT-d and SR-IOV must be enabled in the platform (BIOS). 
For more details, refer to the link [Updating the BIOS settings](https://intel.github.io/quickassist/AppNotes/Containers/setup.html#updating-the-bios-settings).

## Grub Settings
- Intel IOMMU should be enabled and QAT VF to be bind to kernel. Follow the below steps.

### Ubuntu:
- Edit the grub file: `sudo vim /etc/default/grub`
- Modify `GRUB_CMDLINE_LINUX` to add `intel_iommu=on vfio-pci.ids=8086:4941` (SPR)
- Update grub: `sudo update-grub`
- Reboot the system

### CentOS/Fedora:
- Update the kernel: `grubby --update-kernel=/boot/vmlinuz-$(uname -r) --remove-args="intel_iommu=on" --args="intel_iommu=off vfio-pci.ids=8086:4941"`
- Reboot the system

- Ensure VT-d and SRIOV are enabled in the host. The steps can be referred from the link [Configuring the host operating system with SR-IOV IOMMU](https://intel.github.io/quickassist/AppNotes/Containers/setup.html#configuring-the-host-operating-system-with-sr-iov-iommu).

## Docker Environment Setup
- The environment must be set up to build and run containers. Certain packages will need to be loaded and configuration files created and/or updated, which can be system specific.
- Install Docker following the steps in [Docker Package Installation](https://intel.github.io/quickassist/AppNotes/Containers/setup.html#docker).
- Configure the proxy setting following [Proxy settings](https://intel.github.io/quickassist/AppNotes/Containers/setup.html#proxy-settings).

## Enable Intel® QAT Virtual Function (VF) Devices
- Enable the VF devices using the following script:
  ```sh
  for i in `(lspci -D -d :4940 || lspci -D -d :4942) | awk '{print $1}'`; do echo 16 | sudo tee /sys/bus/pci/devices/$i/sriov_numvfs; done
  ```

## Configure Services in the Host
- Stop the Intree or OOT services if any running on the host:
  ```sh
  systemctl stop qat
  ```
  ```sh
  systemctl stop qat_service
  ```
  ```sh
  systemctl disable qat_service
  ```
- Bring the QAT device down:
  ```sh
  for i in `lspci -D -d :4940 | awk '{print $1}'`; do echo down > /sys/bus/pci/devices/$i/qat/state; done
  ```
- Set the correct service in the QAT devices:
  ```sh
  for i in `lspci -D -d :4940 | awk '{print $1}'`; do echo "asym;dc" > /sys/bus/pci/devices/$i/qat/cfg_services; done
  ```
- Bring the QAT device up:
  ```sh
  for i in `lspci -D -d :4940 | awk '{print $1}'`; do echo up > /sys/bus/pci/devices/$i/qat/state; done
  ```
- Check the status:
  ```sh
  for i in `lspci -D -d :4940 | awk '{print $1}'`; do cat /sys/bus/pci/devices/$i/qat/state; done
  ```
- Ensure the GID using the below command in host. The GID of the QAT group on the host should be same as GID of the docker Image. The QAT group of the host should be ‘1001’ :
  ```sh
  getent group qat | cut -d ':' -f 3
  ```
- Enable the VF devices with the previously mentioned script and set correct permissions:
  ```sh
  chown root:qat /dev/vfio/*
  ```
  ```sh
  chmod 660 /dev/vfio/*
  ```
## Generate certificates
Create the TLS key and certificate for enabling encryption
  ```sh

  openssl genrsa -out rsa1k.key.pem 1024
  openssl req -new -x509 -key rsa1k.key.pem -out rsa1k.cert.pem -days 360 -subj "/C=US/ST=State/L=Locality/O=Company/OU=Section/CN=(1024 bit RSA)/emailAddress=xxx@company.com"

  ```

## Run Image
To run the Docker image, use the following command:
```sh

Server command: 
docker run --rm -it --cpuset-cpus <2-n+1> --cap-add=IPC_LOCK --security-opt seccomp=unconfined --security-opt apparmor=unconfined $(for i in `ls /dev/vfio/*`; do echo --device $i; done) --env QAT_POLICY=1 --ulimit memlock=524288000:524288000 -v /usr/share/nginx/:/usr/share/nginx/ -v /etc/ssl/certs/:/etc/ssl/certs/ -v /var/www/html/:/var/www/html/ -v /var/www/logs/:/var/www/logs/ -d -p 8080:8080 intel/intel-async-mode-nginx:1.0.0-1

Client command:
openssl  s_time -connect <server_ip>:8080 -new -cipher AES128-GCM-SHA256 -www /10mb_file.html -time 5

```

## Sample Test Output
```sh

Server: docker run --rm -it -p 8080:8080 --cpuset-cpus 2 $(for i in `ls /dev/vfio/*`; do echo --device $i; done)  --env QAT_POLICY=1 --ulimit memlock=524288000:524288000 --cap-add=IPC_LOCK --security-opt seccomp=unconfined --security-opt apparmor=unconfined  -v /etc/ssl/certs/TestServer.cert.pem:/etc/ssl/certs/TestServer.cert.pem -v /etc/ssl/certs/TestServer.key.pem:/etc/ssl/certs/TestServer.key.pem -v /var/www/logs/:/var/www/logs -v /var/www/html/:/var/www/html/ -d --env OPENSSL_CONF=/etc/ssl/openssl.cnf -v /etc/ssl/openssl.cnf:/etc/ssl/openssl.cnf  -v /usr/share/nginx/conf:/usr/share/nginx/conf -d amr-fm-registry.caas.intel.com/esh-user/intel-async-mode-nginx:1.0.0-1

Client: openssl s_time -connect localhost:8080 -new -cipher AES128-GCM-SHA256 -www /10mb_file.html -time 5

Successfull output will show Connection Statistics after client command execution for 5 seconds in the terminal.

```

## Legal
Intel, Intel Atom, and Xeon are trademarks of Intel Corporation in the U.S. and/or other countries.

*Other names and brands may be claimed as the property of others.

Copyright © 2014-2025, Intel Corporation. All rights reserved.
