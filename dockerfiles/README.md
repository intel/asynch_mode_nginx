# Intel® QuickAssist Technology(QAT) Async Mode Nginx\* Container support

Async Mode Nginx Dockerfile contains Crypto and Compression acceleration with both QAT_HW and QAT_SW which can be built into docker images on the platforms with [Intel® QuickAssist 4xxx Series](https://www.intel.com/content/www/us/en/products/details/processors/xeon/scalable.html) QAT device.

This Dockerfile(qat_crypto_base+compression/Dockerfile) with qatengine is built and validated on top of OpenSSL-3.0.15, QAT_HW(qatlib intree driver) and QAT_SW with software versions mentioned in [software_requirements](../README.md#software_requirements) section.

## Docker setup and testing

Refer [here](https://intel.github.io/quickassist/AppNotes/Containers/setup.html)
for setting up the host for QAT_HW (qatlib intree) if the platform has QAT 4xxx Hardware
device. Stop QAT service if any running on the host.

Configure the [proxy settings](https://intel.github.io/quickassist/AppNotes/Containers/setup.html#proxy-settings).

### QAT_HW settings

Follow the below steps to enable required service. The service can be asym only, sym only or both
in step 2 depending on the particular use case. Configure the required service only to get best performance.

1. Bring down the QAT devices
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do echo down > /sys/bus/pci/devices/$i/qat/state;done
```

2. Set up the required crypto or compression service(s)
   To enable crypto service use "sym;asym"
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do echo "sym;asym" > /sys/bus/pci/devices/$i/qat/cfg_services;done
```
   To enable compression service use "dc" or both means "dc;sym" / "dc;asym" update accordingly in above command.

3. Bring up the QAT devices
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do echo up> /sys/bus/pci/devices/$i/qat/state;done
```

4. Check the status of the QAT devices
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do cat /sys/bus/pci/devices/$i/qat/state;done
```

5. Enable VF for the PF in the host
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do echo 16|sudo tee /sys/bus/pci/devices/$i/sriov_numvfs; done
```

6. Add QAT group and Permission to the VF devices in the host
```
    chown root.qat /dev/vfio/*
    chmod 660 /dev/vfio/*
```

### Generate certificates

Create the TLS key and certificate for enabling encryption. The `conf/nginx_qat.conf` sample expects the
files to be named `cert.pem` and `cert.key`, placed alongside `nginx.conf` in the bind-mounted conf
directory (see "Preparing the conf directory for bind-mounting" below):

```
openssl genrsa -out cert.key 2048
openssl req -new -x509 -key cert.key -out cert.pem -days 360 -subj "/C=US/ST=State/L=Locality/O=Company/OU=Section/CN=(2048 bit RSA)/emailAddress=xxx@company.com"
```

### Image creation

Docker images can be build using the below command with appropiate image name.

```
docker build --build-arg GID=$(getent group qat | cut -d ':' -f 3) -t <docker_image_name> -f <path-to-dockerfile> . --no-cache
```
Note: GID is the group id of qat group in the host.

### Preparing the conf directory for bind-mounting

The "Test using Async Nginx crypto utility" commands below bind-mount `/usr/share/nginx/conf` from the host, so
the host directory must first be populated with the image's default conf assets (mime.types, fastcgi_params, etc.)
plus the desired `nginx.conf` and certificate files:

```
mkdir -p /usr/share/nginx/conf
docker create --name tmp-extract <docker_image_name>
docker cp tmp-extract:/usr/share/nginx/conf/. /usr/share/nginx/conf/
docker rm tmp-extract
cp conf/nginx_qat.conf /usr/share/nginx/conf/nginx.conf
cp cert.pem cert.key /usr/share/nginx/conf/
```

### Test using Async Nginx\* crypto utility

```
Intree:
Server command: docker run --rm -it --cpuset-cpus <2-n+1> --cap-add=IPC_LOCK --security-opt seccomp=unconfined --security-opt apparmor=unconfined $(for i in /dev/vfio/*; do [ -e "$i" ] && echo "--device $i"; done) --env QAT_POLICY=1 --ulimit memlock=524288000:524288000 -v /usr/share/nginx/:/usr/share/nginx/ -v /var/www/html/:/var/www/html/ -v /var/www/logs/:/var/www/logs/ -d -p 80:80 -p 443:443 --name <container_name> <docker_image_name>

OOT:
Server command: docker run --rm -it --cpuset-cpus <2-n+1> --cap-add=IPC_LOCK --security-opt seccomp=unconfined --security-opt apparmor=unconfined  --device /dev/qat_adf_ctl --device  /dev/qat_dev_processes --device  /dev/usdm_dr $(for i in `ls /dev/uio*`; do echo --device $i; done) --ulimit memlock=524288000:524288000 -v /usr/share/nginx/:/usr/share/nginx/ -v /var/www/html/:/var/www/html/ -v /var/www/logs/:/var/www/logs/ -d -p 80:80 -p 443:443 <docker_image_name>

Client command: openssl  s_time -connect <server_ip>:443 -new -cipher AES128-GCM-SHA256 -www /10mb_file.html -time 5
```
Note: n is number of process or thread. Port 80 serves plain HTTP and port 443 serves TLS; certificates live alongside `nginx.conf` in the bind-mounted `/usr/share/nginx/` conf directory, so no separate `/etc/ssl/certs/` mount is needed. Use -v /usr/share/nginx/, /var/www/html/ and /var/www/logs/.

