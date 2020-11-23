# NGINX+OPENSSL+QATENGINE+QAT|CORE BENCHMARKING METHODOLOGY

The purpose of this README is to configure NGINX with OpenSSL+QATEngine+QAT|CORE, and measure Transport Layer Security (TLS) <b>1.2</b> algorithmic performance.  Where applicable, the algorithms can be offloaded to either Intel(R) Quickassist Technology(R) or Intel(R) Xeon(R) or Atom(TM)) core.

## System Topology

The general layout of the testing is as follows:

EACH NODE IS A PLATFORM. Ensure to use enough clients/backend systems to avoid being performance bound.

WEBSERVER: Basic use case
[CLIENT(s)][172.16.N.2/24]<------>[172.16.N.1/24][[port 4400]NGINX Server]

WEBPROXY: Most demanding both compute and IO use case
[CLIENT(s)][172.16.N.2/24]<------>[172.16.N.1/24][[port 4400]NGINX Server][192.168.N.1/24]<------>[192.168.N.2/24][[port 8210]Backend Server]

<i>Where N is between 1-255</i>

For these tests, we use subnet 172.16.<i>N</i>.<b>1</b> port 4400 for the DUT and 172.16.<i>N</i>.<b>2</b> for the client(s).

The general rule, is to use a single IP each 10GbE of Ethernet.  So if you have a 25GbE NIC port, in order to saturate each the port, it will be necessary to use more than one(1) IP per-port. For example:
```bash
#ON CLIENT
ifconfig eth0:1 172.16.1.2/24
ifconfig eth0:2 172.16.2.2/24
#ON DUT
ifconfig eth0:1 172.16.1.1/24
ifconfig eth0:2 172.16.2.1/24
```
Ensure to balance the total IO across the client(s) so as to get an even distribution of client compute power.

<b>The reason CLIENT(s) consists of an "s" is to ensure that the Client(s) are not the bottleneck.  So ensure to use enough clients with enough compute power to push requests to the NGINX Server.  If not, you could become client bound.</b>

<b>ENSURE THE THE SERVER AND CLIENT SYSTEMS CAN PING</b>
```bash
ping 172.16.1.2 #from the server (DUT) should return an ICMP Reply message
```


## Setup
You will need the following
1. HARDWARE
   1. A Server capable of maximizing performance of the QAT device under test.  So If your goal is to saturate a QAT device's maximum symmetric performance, you need to ensure it has enough PCIe slots available for network Input/Output (IO).
   1. Network Interface Cards.  Ideally you want optical network cards and not copper.  The number and type depends on what you are trying to test.  If you are trying to only test Public Key Encryption such as RSA and ECDSA, you need only a few Gigabit Ethernet (GbE).  But if you are trying to test symmetric performance such as CBC or GCM, you need at least 10% more GbE than what the QAT device is capable of.  For example, if the QAT device is capable of 100Gbps AES-128-CBC-HMAC-SHA1, then you want at least 110GbE. This is due to the nature of running Layer 4-7 applications, since the ACK mechanism in Transmission Control Protocol (TCP) will create bubbles of unused Ethernet capacity.
1. SOFTWARE (Be aware that the NGINX, OpenSSL, QAT Engine, and QAT Driver steps will be provided below.  This is just F.Y.I)
   1. Ubuntu Server [18.04](https://releases.ubuntu.com/18.04/ubuntu-18.04.4-live-server-amd64.iso) Operating System
   1. [NGINX](https://github.com/intel/asynch_mode_nginx.git)
   1. [OpenSSL](https://github.com/openssl/openssl.git)
   1. [QAT Engine](https://github.com/intel/QAT_Engine.git)
   1. [QAT Driver](https://01.org/intel-quickassist-technology): Look for "Intel(R) Quickassist Driver for Linux".

## Installation
* DUT SETUP
   * Power on the DUT system, with all required peripherals connected.  Plug in the networing cables required to connect to the management Internet port (SSH Port) and to the Clients (High performance NICs).
   * Set the correct date. The syntax is as follows: date -s "\<TIME DAY MONTH YEAR\>".  See example
```bash
date -s "0900 1 June 2020"
```
   * Log into the DUT via SSH as a root user.  Perform the following steps:
```bash
cd /root/

mkdir -p server/scripts/
mkdir -p server/sources/
mkdir -p server/sources/tls_build/qat_install/
mkdir -p server/sources/tls_build/certs/
mkdir -p server/sources/tls_build/openssl_install/
mkdir -p server/sources/tls_build/nginx_install/

cd /root/server/sources/tls_build/
git clone https://github.com/intel/asynch_mode_nginx.git
mv asynch_mode_nginx/ nginx/
cd nginx
git checkout v0.4.0
cd ../
git clone https://github.com/openssl/openssl.git
cd openssl
git checkout OpenSSL_1_1_0l
cd ../
git clone https://github.com/intel/QAT_Engine.git
mv QAT_Engine qat_engine
cd qat_engine
git checkout v0.5.44
cd ../
cd qat_install
wget --no-check-certificate https://01.org/sites/default/files/downloads/qat1.7.l.4.9.0-00008.tar.gz
tar xf qat1.7.l.4.9.0-00008.tar.gz
cd /root/server/scripts/

export KERNEL_SOURCE_ROOT=/usr/src/linux-headers-$(uname -r) #may change if using Red Hat
export ICP_ROOT=/root/server/sources/tls_build/qat_install/
export QAT_ENGINE=/root/server/sources/tls_build/qat_engine/
export OPENSSL_SOURCE=/root/server/sources/tls_build/openssl/
export OPENSSL_LIB=/root/server/sources/tls_build/openssl_install/
export OPENSSL_ENGINES=/root/server/sources/tls_build/openssl_install/engines-1.1/
export OPENSSL_ROOT=/root/server/sources/tls_build/openssl/
export PERL5LIB=/root/server/sources/tls_build/penssl/
export NGINX_SOURCE=/root/server/sources/tls_build/nginx/
export NGINX_INSTALL_DIR=/root/server/sources/tls_build/nginx_install/
export NGINX_CERTS=/root/server/sources/tls12_buid/certs/
export LD_LIBRARY_PATH=$OPENSSL_SOURCE:$ICP_ROOT/build/:$OPENSSL_LIB:/lib64/
export DISABLE_PARAM_CHECK=1
export DISABLE_STATS=1
export UPSTREAM_DRIVER=1
export LD_BIND_NOW=1
export ifaces="A list of your ethernet interface names such as eth0 or enps0f0" #ex) ifaces="eth0 eth1"
export QAT_COUNTER0=qat_c6xx_0000:??:00.0 #Bus Device Function (BDF) names of your qat device in /sys/kernel/debug/ . You can use lspci |grep Co- to find BDF.
export QAT_COUNTER1=qat_c6xx_0000:??:00.0
export QAT_COUNTER2=qat_c6xx_0000:??:00.0
export SIBLING_THREAD=`cat /sys/devices/system/cpu/cpu1/topology/thread_siblings_list |awk 'BEGIN{FS=",";}{print $2}'`


cd $ICP_ROOT
./configure --prefix=$ICP_ROOT
make -j 10
make install -j 10

cd  $OPENSSL_SOURCE
./config --prefix=$OPENSSL_LIB -Wl,-rpath,$OPENSSL_SOURCE
make update
make depend
make -j 10
make install -j 10

cd $QAT_ENGINE
./configure --with-qat_dir=$ICP_ROOT --with-openssl_dir=$OPENSSL_SOURCE --with-openssl_install_dir=$OPENSSL_LIB --enable-upstream_driver --enable-usdm  --disable-qat_lenstra_protection
make -j 10
make install -j 10

cd $NGINX_SOURCE
./configure --prefix=$NGINX_INSTALL_DIR --without-http_rewrite_module --with-http_ssl_module --add-dynamic-module=modules/nginx_qat_module  --with-cc-opt="-DNGX_SECURE_MEM -I$OPENSSL_LIB/include -Wno-error=deprecated-declarations -Wimplicit-fallthrough=0" --with-ld-opt="-Wl,-rpath=$OPENSSL_LIB/lib -L$OPENSSL_LIB/lib"
make -j 10
make install -j 10
cd NGINX_INSTALL_DIR/html
fallocate -l 10MB 10mb_file_1.html #You can add more than one if you want. for example 10mb_file_2.html 10mb_file_3.html... .  For our testing purposes, make 3000 10MB files.

cd $NGINX_CERTS
openssl req -x509 -sha256 -nodes -days 365 -newkey rsa:2048 -keyout server.key -out server.crt #RSA Cert
openssl ecparam -genkey -out key.pem -name prime256v1 #DSA Cert
openssl req -x509 -new -key key.pem -out cert.pem #finalize DSA Cert
```
   * Assuming everyting built correctly, you can now do a quick performance function check with QAT.  This step is known as "running OpenSSL Speed tests with QAT". The idea here is to ensure that QAT is functioning properly, and that expected performance is met before moving to a more complicated workload such as NGINX. Run the following:
      * Please Copy and Paste the following script into your environment.  This script will help build QAT config files for you dynamically rather than having to manually edit them.
```bash
#!/bin/bash
#This script was created by NPG Performance Measurement and Analysis Team.
#written by Jon Strang <jon.strang@intel.com>
main()
{
	parse_args $@
	calculate_processes
	create_configs
}
calculate_processes()
{
	PROCESES_PER_DEVICE=0
	AVAILABLE_PROCESES_PER_DEVICE=$(($PROCESSES/$DEVICES+$PROCESSES%$DEVICES))

	if [ $PROCESSES -lt $DEVICES ];
	then
		PROCESSES_PER_DEVICE=1
	elif [ $PROCESSES -ge $DEVICES ];
	then
		for i in `seq 0 1 $(($AVAILABLE_PROCESES_PER_DEVICE-1))`
		do
			PROCESSES_PER_DEVICE=$((PROCESSES_PER_DEVICE+1))
		done
	fi
}
create_configs()
{
	CORE_LEFT_OFF_AT=0
	CORE_STRING=""
	for i in `seq 0 1 $((DEVICES-1))`;
	do
		echo "[GENERAL]	"								>>device_"$i".conf
		echo "ServicesEnabled = cy,dc"							>>device_"$i".conf
		echo "ConfigVersion = 2"							>>device_"$i".conf
		echo "CyNumConcurrentSymRequests = 512"						>>device_"$i".conf
		echo "CyNumConcurrentAsymRequests = 64"						>>device_"$i".conf
		echo "statsGeneral = 1"								>>device_"$i".conf
		echo "statsDh = 1"								>>device_"$i".conf
		echo "statsDrbg = 1"								>>device_"$i".conf
		echo "statsDsa = 1"								>>device_"$i".conf
		echo "statsEcc = 1"								>>device_"$i".conf
		echo "statsKeyGen = 1"								>>device_"$i".conf
		echo "statsDc = 1"								>>device_"$i".conf
		echo "statsLn = 1"								>>device_"$i".conf
		echo "statsPrime = 1"								>>device_"$i".conf
		echo "statsRsa = 1"								>>device_"$i".conf
		echo "statsSym = 1"								>>device_"$i".conf
		echo "StorageEnabled = 0"							>>device_"$i".conf
		echo "PkeDisabled = 0"								>>device_"$i".conf
		echo "InterBuffLogVal = 14"							>>device_"$i".conf
		echo ""										>>device_"$i".conf
		echo "[KERNEL]"									>>device_"$i".conf
		echo "NumberCyInstances = 0"							>>device_"$i".conf
		echo "NumberDcInstances = 0"							>>device_"$i".conf
		echo "Cy0Name = "IPSec0""							>>device_"$i".conf
		echo "Cy0IsPolled = 0"								>>device_"$i".conf
		echo "Cy0CoreAffinity = 0"							>>device_"$i".conf
		echo "Dc0Name = "IPComp0""							>>device_"$i".conf
		echo "Dc0IsPolled = 0"								>>device_"$i".conf
		echo "Dc0CoreAffinity = 0"							>>device_"$i".conf
		echo ""										>>device_"$i".conf
		echo "[SHIM]"									>>device_"$i".conf
		echo "NumberCyInstances = 1"							>>device_"$i".conf
		echo "NumberDcInstances = 1"							>>device_"$i".conf
		echo "NumProcesses = $PROCESSES_PER_DEVICE"					>>device_"$i".conf
		echo "LimitDevAccess = 1"							>>device_"$i".conf
		echo "Cy0Name = "UserCY0""							>>device_"$i".conf
		echo "Cy0IsPolled = 1"								>>device_"$i".conf

		START_FROM=$(($OFFSET+$CORE_LEFT_OFF_AT))
		if [ $HT = 'n' ];
		then
			if [ $(($PROCESSES%$DEVICES)) -eq 0 ];
			then
				for j in `seq 0 1 $(($PROCESSES/$DEVICES-1))`;
				do
					CORE_STRING+="$(($j+$START_FROM)),"
					CORE_LEFT_OFF_AT=$(($CORE_LEFT_OFF_AT+1))
				done
			else
				for j in `seq 0 1 $(($PROCESSES/$DEVICES))`;
				do
					CORE_STRING+="$(($j+$START_FROM)),"
					CORE_LEFT_OFF_AT=$(($CORE_LEFT_OFF_AT+1))
				done
			fi
		else
			HT_CORES=""
			HT_OFFSET=$(($NUM_CORES/$NUMA+$OFFSET))
			if [ $(($PROCESSES%$DEVICES)) -eq 0 ];
			then
				for j in `seq 0 1 $((($PROCESSES/2)/$DEVICES))`;
				do
					HT_CORES+="$(($j+$START_FROM+$HT_OFFSET-1)),"
					CORE_STRING+="$(($j+$START_FROM)),"
					CORE_LEFT_OFF_AT=$(($CORE_LEFT_OFF_AT+1))
				done
			else
				for j in `seq 0 1 $((($PROCESSES/2)/$DEVICES))`;
				do
					HT_CORES+="$(($j+$START_FROM+$HT_OFFSET-1)),"
					CORE_STRING+="$(($j+$START_FROM)),"
					CORE_LEFT_OFF_AT=$(($CORE_LEFT_OFF_AT+1))
				done
			fi
			CORE_STRING+="$CORES_STRING$HT_CORES"
		fi
		echo "Cy0CoreAffinity = $CORE_STRING" |sed 's/,$//g'						>>device_"$i".conf
		CORE_STRING=""
		HT_CORES=""
	done
}
usage()
{
	echo "./create_qat_files.sh -d <number of qat devices> -p <number of processes e.g. nginx or haproxy> -o <core offset> -ht <hyper-threads y|n>"
	echo ""
	echo "Ex.) 18 nginx workers with no HT used and 3 devices and you want to pin nginx beginning with core 1"
	echo "./create_qat_files.sh -d 3 -p 18 -o 1 -ht n"
	echo "Ex.) 18 nginx workers !with! HT used and 3 devices and you want to pin nginx beginning with core 1"
	echo "./create_qat_files.sh -d 3 -p 36 -o 1 -ht y"
}
parse_args()
{
	CORRECTNESS=0
	while true;
	do
		case "$1" in
			-h)
				usage
				exit 0
				shift;shift;;
			-o)
				if [ $2 ];
				then
					OFFSET=$2
					CORRECTNESS=$((CORRECTNESS+1))
				fi
				shift;shift;;
			-d)
				if [ $2 ];
				then
					DEVICES=$2
					CORRECTNESS=$((CORRECTNESS+1))
				fi
				shift;shift;;
			-p)
				if [ $2 ];
				then
					PROCESSES=$2
					CORRECTNESS=$((CORRECTNESS+1))
				fi
				shift;shift;;
			-ht)
				if [ ! $2 = 'y' ] && [ ! $2 = 'n' ];
				then
					echo "Sorry, your -ht needs to be either y|n"
					exit -1
				fi
				if [ $2 ];
				then
					HT=$2
					CORRECTNESS=$((CORRECTNESS+1))
				fi
				NUMA=`lscpu |grep -i numa |grep -v ",\|-" |awk '{print $NF}'`
				NUM_CORES=`nproc`
				shift;shift;;
			*)
				break;
		esac
	done
	if [ $CORRECTNESS -lt 4 ];
	then
		usage
		exit -1
	fi
}
main $@
```
   * Once you have the create_qat_files.sh.  Run the following commands to prime QAT for OpenSSL speed tests.
```bash
./create_qat_files.sh -d 3 -p 3 -o 1 -ht n
```
   * Rename each file to qat_xxx_bdf.conf.  Refer to vim $ICP_ROOT/README or $QAT_ENGINE/README.md for more information. An example is
```bash
mv qat_dev0.conf c6xx_dev0.conf
mv c7xx_dev* /etc/  #This is where QAT looks for the QAT config files
```
   * Keep in mind that the name of this file is unique to the QAT device you are testing and will error out if your config file is not properly names.  Refer to $ICP_ROOT/README.md.
```bash
service qat_service restart
cd $OPENSSL_INSTALL/bin
taskset -c 1-3 ./openssl speed -elapsed -multi 3 rsa2048 #to run nonqat
taskset -c 1-3 ./openssl speed -engine qat -elapsed -multi 3 -async_jobs 72 rsa2048 #to run with qat
```
      * Your non-QAT vs QAT for RSA2K should be apparent based on the QAT device you are testing.
   * Once your data is matching expecations for the QAT device that is under test, it is now time to run full nginx application. Note that you can monitor your QAT, Ethernet, and OS TCP Time Wait stats with the below script. call this script stats.sh
```bash
#Written by Jon Strang jon.strang@intel.com


#Code from ethstats.sh courtesy of Jason N
net_snapshot ()
{
        rx=0;tx=0
        for i in $ifaces;
        do
                cntr="bytes"
                snap=`ethtool -S $i 2>/dev/null`
                `echo "$snap" | grep -q ${cntr}_nic` && cntr="${cntr}_nic"
                if [ -n "$snap" ];
                then
                        let rx+="`echo \"$snap\" | grep "^ *rx_$cntr:" | cut -f2 -d:`"
                        let tx+="`echo \"$snap\" | grep "^ *tx_$cntr:" | cut -f2 -d:`"
                fi
        done
}

newtest=0
oldtest=0
newtest1=0
oldtest1=0
newtest2=0
oldtest2=0
old_interrupt=0
new_interrupt=0
old_perf=0
new_perf=0
old_err=0
new_err=0

net_snapshot
while true; do
        tstamp1=`date +%s.%N`
	if [ $NUM_QAT_DEVICES -eq 1 ];
	then
	newtest=`cat /sys/kernel/debug/$QAT_COUNTER0/fw_counters |grep "s\[AE" |awk '{print $5}' | awk 'BEGIN {s=0} {s+=$1} END {print s}'`
	elif [ $NUM_QAT_DEVICES -eq 2 ];
	then
	newtest=`cat /sys/kernel/debug/$QAT_COUNTER0/fw_counters |grep "s\[AE" |awk '{print $5}' | awk 'BEGIN {s=0} {s+=$1} END {print s}'`
	newtest1=`cat /sys/kernel/debug/$QAT_COUNTER1/fw_counters |grep "s\[AE" |awk '{print $5}' | awk 'BEGIN {s=0} {s+=$1} END {print s}'`
	elif [ $NUM_QAT_DEVICES -eq 3 ];
	then
	newtest=`cat /sys/kernel/debug/$QAT_COUNTER0/fw_counters |grep "s\[AE" |awk '{print $5}' | awk 'BEGIN {s=0} {s+=$1} END {print s}'`
	newtest1=`cat /sys/kernel/debug/$QAT_COUNTER1/fw_counters |grep "s\[AE" |awk '{print $5}' | awk 'BEGIN {s=0} {s+=$1} END {print s}'`
	newtest2=`cat /sys/kernel/debug/$QAT_COUNTER2/fw_counters |grep "s\[AE" |awk '{print $5}' | awk 'BEGIN {s=0} {s+=$1} END {print s}'`
	new_interrupt=`for j in $ifaces;do cat /proc/interrupts  |grep "$j-" |awk '{for(i=3;i<NF;++i)s+=$i} END {printf "%f\n",s}'; done |awk '{t+=$1}END{printf "%f",t}'`
        waiting=`ss -a | grep TIME-WAIT | wc -l`
        connections=`ss -tp |grep nginx |wc -l`
        oldrx=$rx; oldtx=$tx;
	sleep .9
        tstamp2=`date +%s.%N`
        net_snapshot
        rx_avg=`echo "(8*($rx-$oldrx)/1000/1000/($tstamp2-$tstamp1))"|bc`
        tx_avg=`echo "(8*($tx-$oldtx)/1000/1000/($tstamp2-$tstamp1))"|bc`
	#calculate errors
	echo "[RX=$rx_avg |TX=$tx_avg |QAT0=$((($newtest-$oldtest))) |QAT1=$((($newtest1-$oldtest1))) |QAT2=$((($newtest2-$oldtest2))) |CONN=$connections |WAIT=$waiting |IRUPTS=`bc -l <<<  $new_interrupt-$old_interrupt |awk '{printf("%d",$1)}'` |ERRS=$(($new_err-$old_err))]"
        oldtest=$newtest
        oldtest1=$newtest1
        oldtest2=$newtest2
        old_interrupt=$new_interrupt
	old_err=$new_err
done
```
   * You can now monitor the stats using the command:
```bash
./stats.sh
```

## Testing
To begin testing, please do the following.
```bash
cd /root/server/sources/tls12_build/nginx_install/conf
```
   * Place the following NGINX configuration file into the folder and naming it nginx.conf
```bash
user root;  #The "user" directive must always be root for this bkm
worker_processes 2;     #This will be set to the maximum number of logical cores you are going to use. So if you are using Intel(R) Hyper-Threading, you would use 2, 1 worker for each logical thread.  If you were using 6 cores plus their Hyper-Threads, you would have 12 worker_proceses. If you are on an Intel(R) Atom(TM) part, you would not have Hyper-Threads, so if you use 1 core, then you would only have 1 worker_process.

ssl_engine{     #This directive enables the use of QAT offload. If "ssl_engine" is totally ommitted, then software will be used.
   use_engine qatengine;
   default_algorithms ALL;
   qat_engine{
      qat_notify_mode poll;
      qat_offload_mode async;
      qat_external_poll_interval 1;
}}
worker_rlimit_nofile 1000000;   #Set this to a high number, as this is an OS optimization that will ensure no file handle issues.

events  #The events block is where you will tell NGINX what behaviors to exhibit when dealing with an event.
{
  use epoll;    #The epoll module is important for performance.  It set the behavior to poll on events coming from IO.

  worker_connections 8192;  #This directive tells how many connections a worker_process can have.  Ensure to never set this too low or high. To low will prevent getting good performance, and too high may CPU starve connections.

  multi_accept on;  #This directive allows worker_processes to handle multiple connections at a time rather than dealing with only one at a time.

  accept_mutex on;  #This directive tells the worker_processes to get in a line rather than all rush for a new connection.
}

http    #This is the main HTTP block. This will have all HTTPS relevant directives.
{
          ssl_buffer_size 65536;    #This is telling nginx to use 64KB buffers when dealing with TLS records

          include       mime.types; #Since file extensions are meaningless on the web, we ensure we specify what content type we are sending
          default_type  application/octet-stream;   #This is the default value for a binary stream
          sendfile on;  #An optimization that allows for file data to transfer within the kernel from file descriptor to file descriptor rather than leaving the kernel, heading to user space, and then going back into kernel space.
          access_log off;   #Turns off logging, which consequently reduces operation latency to some degree.
        server #Here is your main server block that has all IP specific directives and behaviors. YOU WILL HAVE TO DUPLICATE THIS BLOCK FOR EACH IP YOU USE.  So if you have 172.16.1.1 .... 172.16.12.1 , you will need 12 server blocks.
        {
                listen       172.16.1.1:4400 reuseport backlog=131072 so_keepalive=off ; #Listen is the IP:PORT to listen to. Reuseport will provide the kernel behavior of load balancing incoming connections to the available NGINX socket listeners.  There is an NGINX socket listener per server block, the block we currently are in now. So if you have one server block with one IP:PORT pair, then you have one socket listener. If you have two server blocks with two IP:PORT pairs, you have two socket listeners so on and so fort. The backlog parameter tells NGINX how many connections can be in a wait queue when it cannot service the connection immediately. These connections are still amidst TCP handshake.  The so_keepalive directive tell NGINX to close the TCP connection once it is finished.
                sendfile on;  #Though already provided in parent block, continue to specify it.

                keepalive_timeout 0s;  #Even though TCP keepalives are disabled, we still set it to a value of 0
                tcp_nopush on;  #This directive tells NGINX to wait to send data once it has a payload the size of MSS or Max Segment Size.  This is a follow on to Nagles Algorithm.

                tcp_nodelay on;  #This works opposite of tcp_nopush, where here we do not delay when sending data. We set this to ensure packets get sent without delaying for some period of time. This is to reduce latency.
                ssl_verify_client off;  #Here we do not verify client certificates.
                ssl_session_tickets off;    #We do not cache ssl session information to ensure freshness of connections.
                access_log  off;  #turn off access log to reduce latency and overhead.
                lingering_close off; #We immediately close the TCP connection without waiting.
                lingering_time 1;  #We still set this even though we disabled lingering delay.
                server_name  server_1;  #Name of the server.

                ssl                  on;    #Ensure SSL is on
                ssl_asynch           on;    #Ensure SSL works asynchronously
                ssl_certificate      /root/server/sources/tls12_build/certs/server.crt;  #Path to your public certificate for RSA. For EC use cert.pem instead of server.crt
                ssl_certificate_key  /root/server/sources/tls12_build/certs/server.key;  #Path to your private key for RSA. For EC use key.pem instead of server.key

                ssl_session_timeout  300s;  #Even though we disable ssl session caching, we set a timeout for 300 seconds to preserve current productive sessions doing work.

                ssl_protocols  TLSv1.2; #We use TLSv1.2

                ssl_ciphers  AES128-SHA:AES256-SHA; #We specify the cipher to use.  AES128-SHA, AES128-GCM-SHA256, ECDHE-ECDSA-AES128-SHA, and ECDHE-RSA-AES128-SHA are the ciphers currently used.
                ssl_prefer_server_ciphers   on;  #During SSL handshake we use this to ensure server ciphers have precedence.

                location /  #Location of files to send. this location is relative to /root/server/sources/tls__build/nginx_install/html
                {
                          index  index.html index.htm;
                }
        }
}
```
   * Ensure to set the worker_processes accordingly.  The <b> rule with QAT when on an Intel(R) Xeon(R) platform, is to use 2 NGINX workers where one(1) is pinned to the physical core and the other to the sibling Hyperthread.</b>  So if you are running 1 core tests plus the Hyperthread, then "worker_processes 2;" will be used.  If not using Hyperthreads, then use "worker_processes 1;".  To run a 1 core with Hyperthread test we would do the following.
   * Create the necessary QAT config files.
```bash
./create_qat_files.sh -d 1 -p 1 -o 1 -ht y
```
   * Rename the qat_dev\*.conf files to the properly named files.  Then move to /etc/ for processing.
   * Ensure to tune the DUT. Call this script tune.sh and place in /root/server/scripts/
```bash
#!/bin/bash
ufw disable                                                                     #disable Debian firewall
iptables -F                                                                     #flush the iptables rules
#TCP Memory
echo 16777216                > /proc/sys/net/core/rmem_max
echo 16777216                > /proc/sys/net/core/wmem_max
echo 16777216                > /proc/sys/net/core/rmem_default
echo 16777216                > /proc/sys/net/core/wmem_default
echo 16777216 16777216 16777216  > /proc/sys/net/ipv4/tcp_rmem
echo 538750 538750 538750  > /proc/sys/net/ipv4/tcp_wmem
echo 16777216            > /proc/sys/net/core/optmem_max
echo 16777216 16777216  16777216 > /proc/sys/net/ipv4/tcp_mem
echo 65536       > /proc/sys/vm/min_free_kbytes                                  #ensure there will always be free
#TCP Behavior
echo 0                     > /proc/sys/net/ipv4/tcp_timestamps
echo 0                     > /proc/sys/net/ipv4/tcp_sack
echo 0                     > /proc/sys/net/ipv4/tcp_fack
echo 0                     > /proc/sys/net/ipv4/tcp_dsack
echo 0                     > /proc/sys/net/ipv4/tcp_moderate_rcvbuf
echo 1                     > /proc/sys/net/ipv4/tcp_rfc1337
echo 600        > /proc/sys/net/core/netdev_budget
echo 128                   > /proc/sys/net/core/dev_weight
echo 1                     > /proc/sys/net/ipv4/tcp_syncookies
echo 0                     > /proc/sys/net/ipv4/tcp_slow_start_after_idle
echo 1                     > /proc/sys/net/ipv4/tcp_no_metrics_save
echo 1                     > /proc/sys/net/ipv4/tcp_orphan_retries
echo 0                     > /proc/sys/net/ipv4/tcp_fin_timeout
echo 0                     > /proc/sys/net/ipv4/tcp_tw_reuse
echo 0                     > /proc/sys/net/ipv4/tcp_tw_recycle
echo 1                     > /proc/sys/net/ipv4/tcp_syncookies
echo 2                       > /proc/sys/net/ipv4/tcp_synack_retries
echo 2                     > /proc/sys/net/ipv4/tcp_syn_retries
echo cubic                   > /proc/sys/net/ipv4/tcp_congestion_control
echo 0                     > /proc/sys/net/ipv4/tcp_low_latency
echo 1                     > /proc/sys/net/ipv4/tcp_window_scaling
echo 1                     > /proc/sys/net/ipv4/tcp_adv_win_scale
#TCP Queueing
echo 0                > /proc/sys/net/ipv4/tcp_max_tw_buckets
echo 1025 65535            > /proc/sys/net/ipv4/ip_local_port_range
echo 131072                > /proc/sys/net/core/somaxconn
echo 262144            > /proc/sys/net/ipv4/tcp_max_orphans
echo 262144           > /proc/sys/net/core/netdev_max_backlog
echo 262144        > /proc/sys/net/ipv4/tcp_max_syn_backlog
echo 4000000             > /proc/sys/fs/nr_open

echo 4194304     > /proc/sys/net/ipv4/ipfrag_high_thresh
echo 3145728     > /proc/sys/net/ipv4/ipfrag_low_thresh
echo 30      > /proc/sys/net/ipv4/ipfrag_time
echo 0   > /proc/sys/net/ipv4/tcp_abort_on_overflow
echo 1       > /proc/sys/net/ipv4/tcp_autocorking
echo 31      > /proc/sys/net/ipv4/tcp_app_win
echo 1       > /proc/sys/net/ipv4/tcp_mtu_probing
set selinux=disabled
ulimit -n 1000000
```
   * To tune run the following:
```bash
source tune.sh
```

```bash
service qat_service restart
cd $NGINX_INSTALL_DIR/sbin
taskset -c 1-$SIBLING_THREAD ./nginx -c conf/nginx.conf
netstat -tulpn #to verify nginx started and listening to port 4400
```
   * Now that NGINX is up and running with QAT, you can monitor the statistics. But before that, lets set up the client(s).
```bash
./stats.sh
```
## Client Setup
Log into the client(s) and perform the following.

```bash
cd /root/
mkdir -p client/
mkdir -p client/sources/
mkdir -p client/scripts/
cd client/sources/
git clone https://github.com/openssl/openssl.git
cd openssl
git checkout OpenSSL_1_1_0l
./config -Wl,-rpath,/root/client/sources/openssl/
make -j 10 #NO MAKE INSTALL NECESSARY
cd /root/client/scripts/
```
Paste the following scripts into the scripts/ directory
   * Client Tuning (call it tune.sh)
```bash
#!/bin/bash
ufw disable
iptables -F
#TCP Memory
echo 16777216                > /proc/sys/net/core/rmem_max
echo 16777216                > /proc/sys/net/core/wmem_max
echo 16777216                > /proc/sys/net/core/rmem_default
echo 16777216                > /proc/sys/net/core/wmem_default
echo 16777216 16777216 16777216  > /proc/sys/net/ipv4/tcp_rmem
echo 538750 538750 538750  > /proc/sys/net/ipv4/tcp_wmem
echo 16777216   		 > /proc/sys/net/core/optmem_max
echo 16777216 16777216  16777216 > /proc/sys/net/ipv4/tcp_mem
echo 65536		 > /proc/sys/vm/min_free_kbytes
#TCP Behavior
echo 0                     > /proc/sys/net/ipv4/tcp_timestamps
echo 0                     > /proc/sys/net/ipv4/tcp_sack
echo 0                     > /proc/sys/net/ipv4/tcp_fack
echo 0                     > /proc/sys/net/ipv4/tcp_dsack
echo 0                     > /proc/sys/net/ipv4/tcp_moderate_rcvbuf
echo 1                     > /proc/sys/net/ipv4/tcp_rfc1337
echo 600        > /proc/sys/net/core/netdev_budget
echo 128                   > /proc/sys/net/core/dev_weight
echo 1                     > /proc/sys/net/ipv4/tcp_syncookies
echo 0                     > /proc/sys/net/ipv4/tcp_slow_start_after_idle
echo 1                     > /proc/sys/net/ipv4/tcp_no_metrics_save
echo 1                     > /proc/sys/net/ipv4/tcp_orphan_retries
echo 0                     > /proc/sys/net/ipv4/tcp_fin_timeout
echo 0                     > /proc/sys/net/ipv4/tcp_tw_reuse
echo 0                     > /proc/sys/net/ipv4/tcp_tw_recycle
echo 1                     > /proc/sys/net/ipv4/tcp_syncookies
echo 2                   	 > /proc/sys/net/ipv4/tcp_synack_retries
echo 2                     > /proc/sys/net/ipv4/tcp_syn_retries
echo cubic                   > /proc/sys/net/ipv4/tcp_congestion_control
echo 1                     > /proc/sys/net/ipv4/tcp_low_latency
echo 1                     > /proc/sys/net/ipv4/tcp_window_scaling
echo 1                     > /proc/sys/net/ipv4/tcp_adv_win_scale
#TCP Queueing
echo 0                > /proc/sys/net/ipv4/tcp_max_tw_buckets
echo 1025 65535            > /proc/sys/net/ipv4/ip_local_port_range
echo 131072                > /proc/sys/net/core/somaxconn
echo 262144            > /proc/sys/net/ipv4/tcp_max_orphans
echo 262144           > /proc/sys/net/core/netdev_max_backlog
echo 262144        > /proc/sys/net/ipv4/tcp_max_syn_backlog
echo 4000000	         > /proc/sys/fs/nr_open

echo 4194304	 > /proc/sys/net/ipv4/ipfrag_high_thresh
echo 3145728	 > /proc/sys/net/ipv4/ipfrag_low_thresh
echo 30	 	 > /proc/sys/net/ipv4/ipfrag_time
echo 0	 > /proc/sys/net/ipv4/tcp_abort_on_overflow
echo 1	 	 > /proc/sys/net/ipv4/tcp_autocorking
echo 31	 	 > /proc/sys/net/ipv4/tcp_app_win
echo 0	 	 > /proc/sys/net/ipv4/tcp_mtu_probing
set selinux=disabled
ulimit -n 1000000
```

   * Client Connection-per-Second Script (call it connection.sh)
```bash
#!/bin/bash


######################################
############# USER INPUT #############
######################################
#STEP 1
ip_address=YOUR_IP_HERE #Only one IP that maps to the DUT such as 172.16.1.1
_time=$8
clients=2000
portbase=4400
cipher=$4
OPENSSL_DIR=/root/client/sources/openssl/
######################################
############# USER INPUT #############
######################################

#Check for OpenSSL Directory
if [ ! -d $OPENSSL_DIR ];
then
    printf "\n$OPENSSL_DIR does not exist.\n\n"
    printf "Please modify the OPENSSL_DIR variable in the User Input section!\n\n"
    exit 0
fi

helpAndError () {
    printf " ex.) ./connection.sh --servers 1 --cipher AES128-GCM-SHA256 --clients 2000 --time 30\n\n"
    exit 0
}

#Check for h flag or no command line args
if [[ $1 == *"h"* ]]; then
    helpAndError
    exit 0
fi

#Check for emulation flag
if [[ $@ == **emulation** ]]
then
    emulation=1
fi

#cmd1 is the first part of the commandline and cmd2 is the second partrt
#The total commandline will be cmd1 + "192.168.1.1:4400" + cmd2
cmd1="$OPENSSL_DIR/apps/openssl s_time -connect"
if [[ $cipher =~ "TLS" ]];
then
	cmd2="-new -ciphersuites $cipher  -time $_time "
else
	cmd2="-new -cipher $cipher  -time $_time "
fi
#Print out variables to check
printf "\n Location of OpenSSL:           $OPENSSL_DIR\n"
printf " IP Addresses:                  $ip_address\n"
printf " Time:                          $_time\n"
printf " Clients:                       $clients\n"
printf " Port Base:                     $portbase\n"
printf " Cipher:                        $cipher\n\n"

printf "Press ENTER to continue"

#read

#Remove previous .test files
rm -rf ./.test_*

#Get starttime
starttime=$(date +%s)

#Kick off the tests after checking for emulation
if [[ $emulation -eq 1 ]]
then
    for (( i = 0; i < ${clients}; i++ )); do
        printf "$cmd1 $ip_address:$(($portbase)) $cmd2 > .test_$(($portbase))_$i &\n"
    done
    exit 0
else
    for (( i = 0; i < ${clients}; i++ )); do
        $cmd1 $ip_address:$(($portbase)) $cmd2 > .test_$(($portbase))_$i &
    done
fi

waitstarttime=$(date +%s)
# wait until all processes complete
while [ $(ps -ef | grep "openssl s_time" | wc -l) != 1 ];
do
    sleep 1
done

total=$(cat ./.test_$(($portbase))* | awk '(/^[0-9]* connections in [0-9]* real/){ total += $1/$4 } END {print total}')
echo $total >> .test_sum
sumTotal=$(cat .test_sum | awk '{total += $1 } END { print total }')
printf "Connections per second:      $sumTotal CPS\n"
printf "Finished in %d seconds (%d seconds waiting for procs to start)\n" $(($(date +%s) - $starttime)) $(($waitstarttime - $starttime))
rm -rf ./.test_*
```

   * Client Bulk Throughput Script (call it bulk.pl)
```bash
#!/usr/bin/perl

use strict;
use warnings;
use Getopt::Long;
use Data::Dumper;
$Data::Dumper::Indent = 1;

######################################
############# USER INPUT #############
#####################################
#STEP 1#
my @ipaddresses = qw( YOUR IPS HERE ); #Example qw( 172.16.1.1 172.16.2.1 .... 172.16.12.1 )) where "...." is a sequence of IPS.
my $time = 400 ;
my $clients = 250;
my $portbase = 4400;
my $file = "10mb_file";
my $cipher = "";
my $STIME_OUTPUT_FILE;
use constant OPENSSL_DIR => "/root/client/sources/openssl/";
######################################
############# USER INPUT #############
######################################

#Check OpenSSL Dir is set correctly.
if(! -d "@{[OPENSSL_DIR]}") {
    printf "\n\n@{[OPENSSL_DIR]} does not exist.\n\n";
    printf "Please modify the OPENSSL_DIR variable in the User Input section!\n\n";
    exit 1;
}

use constant STIME_APP                => "@{[OPENSSL_DIR]}/apps/openssl s_time";
my @IPs              = ('127.0.0.1');

###############################################################################
sub printVariables {
    my ($servers) = @_;

    #Print out variables for check
    printf "\n Location of OpenSSL:     @{[OPENSSL_DIR]}\n";
    printf " IP Addresses:              @ipaddresses\n";
    printf " Time:                      $time\n";
    printf " Clients:                   $clients\n";
    printf " Servers:                   $servers\n";
    printf " Port Base:                 $portbase\n";
    printf " File:                      $file\n";
    printf " Cipher:                    $cipher\n\n";


    print "Press ENTER to continue";
    #<STDIN>;
}

###############################################################################
sub usage {
  print "Error: $_[0] \n" if defined $_[0];
  print <<'EOF';
Usage:

  stimefork.pl [options]

Options:

  --emulation
                  It performs a dry-run showing how many stime
                  processes would be created and all its parameters.
                  Optional.

  --help
                  Shows this help.

  --servers=number
                  Number of servers listening in the server side.
                  Mandatory.

  time=number
                  Number minimum seconds to run the test.

  portbase=number
                  Starting port number where the web servers will
                  be listening at.

  clients=number
                  Number of clients to be created per server. It all of them
                  will be requesting to the same server.

  file=filename
                  Filename to be requested from the servers.

  ip=filename
                  Filename containing the list of IP adresses where
                  the web server will be listening at.

  cipher=string
                  Cipher suite tag to be used by the clients.
                  Run openssl ciphers -v to know more about this.

  ex.) ./bulk.pl --servers 1 --cipher AES128-GCM-SHA256 --clients 2000 --time 30
EOF

  exit 1;
}

###############################################################################
sub check_mandatory_args {
  foreach (@_) {
    return 0 unless defined ${$_};
  }
  return 1;
}

###############################################################################
sub getStimeOutputFilename {
  my ($server, $child) = @_;
  return $STIME_OUTPUT_FILE . "_server${server}_child${child}.log";
}

###############################################################################
sub myfork {
  my $pid = fork();
  die "fork() failed!" if (!defined $pid);
  return $pid;
}

###############################################################################
sub Kbps2Gbps {
  return $_[0] * 8 / 1024 / 1024;
}

###############################################################################
sub Bps2Gbps {
  return $_[0] * 8 / 1024 / 1024 / 1024;
}

###############################################################################
sub getServerPort {
  my ($portbase, $servers, $child) = @_;
  return $portbase + ( $child % $servers );
}

###############################################################################
sub readCompleteFile {
  my $filename = shift;
  open my $FILE, "<", $filename;
  local $/ = undef;
  my $content = <$FILE>;
  close $FILE;
  return $content;
}

###############################################################################
# -connect host:port - host:port to connect to (default is localhost:4433)
# -nbio         - Run with non-blocking IO
# -ssl2         - Just use SSLv2
# -ssl3         - Just use SSLv3
# -bugs         - Turn on SSL bug compatibility
# -new          - Just time new connections
# -reuse        - Just time connection reuse
# -www page     - Retrieve 'page' from the site
# -time arg     - max number of seconds to collect data, default 30
# -verify arg   - turn on peer certificate verification, arg == depth
# -cert arg     - certificate file to use, PEM format assumed
# -key arg      - RSA file to use, PEM format assumed, key is in cert file
#                 file if not specified by this option
# -CApath arg   - PEM format directory of CA's
# -CAfile arg   - PEM format file of CA's
# -cipher       - preferred cipher to use, play with 'openssl ciphers'
sub call_stime {

  my ($time, $cipher, $ip, $port,
      $requested_file, $server, $child, $emulation) = @_;

  my @cmd;
  push @cmd, STIME_APP;
  push @cmd, " -connect $ip:$port";
  #push @cmd, "-nbio";
  push @cmd, "-new";
  if ($requested_file) {
    push @cmd, "-www /$requested_file";
  }
  push @cmd, "-time $time";
  if ($cipher =~ "TLS")
  {
          push @cmd, "-ciphersuites $cipher";
  }
  else
  {
          push @cmd, "-cipher $cipher";
  }
  push @cmd, ">";
  push @cmd, getStimeOutputFilename($server, $child);

  my $cmd = join(' ', @cmd);
  if ($emulation) {
    print "$cmd\n";
    exit 0;
  }
  else {
    exec $cmd;
  }
}

###############################################################################
sub checkIfDefined {
  my $Input = shift;
  if (defined($Input)) {
    return $Input;
  }
  return 0;
}

###############################################################################
sub readRateFromChildOutput {
  my $output = shift;
  # Transfer rate:          4265.90 [Kbytes/sec] received
  #$output =~ m/Transfer rate:\s*([.\d]+).*/;
  #$output =~ /real seconds/;
  # 101 connections in 2 real seconds, 3251722 bytes read per connection
  $output =~ m/(\d*) connections in (\d*) real seconds, (\d*) bytes read per connection/;
  my $conns = checkIfDefined($1);
  my $secs = checkIfDefined($2);
  my $bytes = checkIfDefined($3);
  if ($conns == 0 or $secs == 0 or $bytes == 0) {
    return 0;
  }
#  if ($bytes != 10485975)
  if ($bytes < 10000000)
  {
        printf " Whole file not transferred\n\n";
  }
  return $bytes * $conns / $secs;
}

###############################################################################
sub readLatencyFromChildOutput {
  my $output = shift;
  #               min  mean[+/-sd] median   max
  # Connect:        0    0   0.0      0       0
  $output =~ m/Connect:\s+(\d+)\s+(\d+)\s+(\d+\.\d+)\s+(\d+)\s+(\d+)/;
  return checkIfDefined($2);
}

###############################################################################
sub readServerResponseTimeFromChildOutput {
  my $output = shift;
  #               min  mean[+/-sd] median   max
  # Processing:   281  286   3.6    287     293
  $output =~ m/Processing:\s+(\d+)\s+(\d+)\s+(\d+\.\d+)\s+(\d+)\s+(\d+)/;
  return checkIfDefined($2);
}

###############################################################################
sub readServerConnectionsPerSecondFromChildOutput {
  my $output = shift;
  # 79 connections in 21 real seconds, 3251722 bytes read per connection
  $output =~ m/(\d*) connections in (\d*) real seconds, (\d*) bytes read per connection/;
  my $conns = checkIfDefined($1);
  my $secs = checkIfDefined($2);
  my $bytes = checkIfDefined($3);
  if ($conns == 0 or $secs == 0 or $bytes == 0) {
    return 0;
  }
  return $conns / $secs;
}

###############################################################################
sub createResultsHash {
  my $servers = shift;
  my %results;
  map {
    $results{$_} = {
      latency => 0,
      stime => 0,
      rate => 0,
      cps => 0,
    }
  }
    0..($servers-1);
  return %results;
}

###############################################################################
sub readResultsFromChildren {
  my ($clients, $servers) = @_;
  my %results = createResultsHash($servers);

  for(my $child = 0; $child < $clients; $child++) {
    my $server = $child % $servers;

    my $childoutput =
      readCompleteFile(getStimeOutputFilename($server, $child));

    $results{$server}{rate} += readRateFromChildOutput($childoutput);
    $results{$server}{latency} += readLatencyFromChildOutput($childoutput);
    $results{$server}{stime} +=
      readServerResponseTimeFromChildOutput($childoutput);
    $results{$server}{cps} += readServerConnectionsPerSecondFromChildOutput($childoutput);
  }
  return %results;
}

###############################################################################
sub showAggregatedResults {
  my ($clients, %results) = @_;

  my $rate = 0.0;
  my $latency = 0.0;
  my $servertime = 0.0;
  my $cps = 0.0;

  map {
    $rate += $results{$_}{rate};
    $latency += $results{$_}{latency};
    $servertime += $results{$_}{stime};
    $cps += $results{$_}{cps};
  }
    keys %results;

  printf " Rate:$cipher:              %6.2f Gbps (total)\n", Bps2Gbps($rate);
  printf " Latency:           %6.2f ms   (mean)\n", $latency / $clients;
  printf " Processing:        %6.2f ms   (mean)\n", $servertime / $clients;
  printf " Conns Per Second:  %6.2f cps  (total)\n", $cps;
}

###############################################################################
sub showResultsPerServer {
  my ($clients, %results) = @_;

  printf "%16s%14s%13s%16s\n",
         'Server',
         'Rate(Gbps)',
         'Latency(ms)',
         'Processing(ms)';

  my $servers = scalar keys %results;
  foreach (sort keys %results) {

    printf "%16s%8.2f%15.2f%16.2f\n",
      "$results{$_}{ip}:$results{$_}{port}",
      Bps2Gbps($results{$_}{rate}),
      $results{$_}{latency} / $clients * $servers,
      $results{$_}{stime} / $clients * $servers;
  }
}

###############################################################################
sub showResults {

  my ($clients, $servers, $portbase, @ipaddresses) = @_;
  my %results = readResultsFromChildren($clients, $servers);

  foreach (sort keys %results) {
    $results{$_}{ip} = $ipaddresses[$_ % scalar @ipaddresses];
    $results{$_}{port} = $portbase + $_;
  }

  print "\n== Results per server =======================================\n\n";
  showResultsPerServer($clients, %results);

  print "\n== Total ====================================================\n\n";
  showAggregatedResults($clients, %results);
}

###############################################################################
sub calculate_number_clients {
  my ($servers, $clients_per_server) = @_;

  my $total_clients = $servers * $clients_per_server;
  my $cpus = `nproc`;
  chomp $cpus;
  if ($total_clients > $cpus) {
    print "WARNING: total_clients ($total_clients) exceeds num cpus ($cpus)\n";
  }

  return ($total_clients, 1);
}

# main
###############################################################################

# Mandatory arguments
my $servers;

# Optional arguments
my $emulation       = 0;
my $help            = 0;

my @mandatory_args = (\$clients, \$portbase, \$time,
                      \$servers, \$cipher);

GetOptions(
  'servers=i'   => \$servers,
  'emulation'   => \$emulation,
  'help'        => \$help,
  'cipher=s'	=> \$cipher,
  'clients=i'    => \$clients,
  'time=i'      => \$time,
)
  or usage();
`rm -r /tmp`;
`mkdir -p /tmp/$cipher`;
$STIME_OUTPUT_FILE="/tmp/" . $cipher .  "/stime_output";

usage() if $help;

usage('Mandatory argument missing')
  unless check_mandatory_args(@mandatory_args);

printVariables($servers);

unlink glob $STIME_OUTPUT_FILE.'*';

my ($processes, $concurrency) = calculate_number_clients($servers, $clients);

my $ipindex = 0;
my $portsperip = $servers / scalar @ipaddresses;
my $portremainder = $servers % scalar @ipaddresses;
my $portcntr = 0;
my $cntrbase = 0;
my $remUsed = 0;
my $increment = 0;


print "Ports per IP: $portsperip Remainder: $portremainder\n";

for (my $child = 0; $child < $processes; $child++) {
  my $ip = $ipaddresses[$ipindex];
  my $port = $portbase;

  $portcntr++;

  if ($portcntr > ($portsperip + $cntrbase)) {
    if ($portremainder != 0 && $remUsed == 0) {
      $portremainder--;
      $remUsed = 1;
    }
    else {
      $ipindex++;
      $ipindex = $ipindex % scalar @ipaddresses;
      $ip = $ipaddresses[$ipindex];
      $cntrbase = ($portcntr - 1);
      $remUsed = 0;
    }
  }

  # It's utilised all the available ports
  if ($portcntr == $servers) {
    $remUsed = 0;
    $portcntr = 0;
    $ipindex = 0;
    $cntrbase = 0;
    $portremainder = $servers % scalar @ipaddresses;

  }
      if ($increment == 1000)
        {
                $increment = 0;
        }

  my $pid = myfork();

  if ($pid == 0) {
	         call_stime($time, $cipher, $ip, $port, $file ."_" . ($increment) . ".html", $child % $servers, $child, $emulation);
  }
	$increment++
}

my $i = 0;
while ($i < $processes  && (my $childpid = wait()) != -1) {
  $i++;
}

unless ($emulation) {
  @ipaddresses = ('server_ip');
  showResults($processes, $servers,
              $portbase, @ipaddresses);
}
```
   * Before running any of the scripts, ensure to tune the clients.
```bash
source tune.sh
```


## Running Tests and Interpreting Results
Two types of tests are run:
   1. Connection-per-Second
      1. Tests Asymmetric Performance (TLS Hanshaking)
      1. No GET Request. Only connection establishment
      1. Measured in how many TLS Handshakes on a per-second basis. The higher the results the better.
```bash
./connection.sh --servers 1 --cipher AES128-GCM-SHA256 --clients 2000 --time 400 #example
```
   1. Bulk-Throughput
      1. Tests Symmetric Performance (16KB Record encryption)
      1. Multiple GET Requests for 10mb_file_1.html ... 10mb_file_N.html where N is between 1 and inf.
      1. Measured in how much data can be transferred with minimal TLS Handshaking. The higher the results the better.
```bash
./bulk.pl --servers 4 --cipher AES128-GCM-SHA256 --clients 250 --time 400 #example
```

# Mapping Test Types to Algorithms.
Test Type|NGINX Algorithm Setting
---------|-----------------------
Max RSA2048 CPS|AES128-GCM-SHA256
Max ECDHE-RSA2048 CPS|ECDHE-RSA-AES128-GCM-SHA256
Max ECDHE-ECDSA CPS|ECDHE-ECDSA-AES128-GCM-SHA256
Max AES128-CBC BULK| AES128-CBC
Max AES128-GCM BULK|AES128-GCM-SHA256
Max CHACHAPOLLY BULK|ECDHE-RSA-CHACHCA20-POLY1305



# Appendix
## Mellanox Debug
If you are using Mellanox 5 series, you may need these.
```bash
sudo mlxconfig -e -d b1:00.1 set ADVANCED_POWER_SETTINGS=True
sudo mlxconfig -e -d b1:00.1 set DISABLE_SLOT_POWER_LIMITER=True
sudo mlxconfig -e -d b1:00.0 set ADVANCED_POWER_SETTINGS=True
sudo mlxconfig -e -d b1:00.0 set DISABLE_SLOT_POWER_LIMITER=True
```

## Running OpenSSL Speed tests with Vectorized AES (VAES)
OpenSSL Speed tests are an easy way to determine the low level micro-benchmarking performance of OpenSSL+QATEngine with VAES optimized GCM library. Below are the steps.

### Prerequisites.
1. A Xeon Generation Icelake or above.  Cascade Lake and below will not work with VAES.
1. Ubuntu 18.04 or Ubuntu 20.04

### Run the following scripts

```bash
#PREAMBLE
cd /root/
mkdir openssl_speed_tests/
cd openssl_speed_tests/
git clone https://github.com/intel/QAT_Engine.git
git clone https://github.com/openssl/openssl.git
git clone https://github.com/intel/intel-ipsec-mb.git
wget --no-check-certificate https://www.nasm.us/pub/nasm/releasebuilds/2.14.02/nasm-2.14.02.tar.gz
mkdir openssl_install/
mv QAT_Engine qat_engine/
mv intel-ipsec-mb intel_ipsec_mb/
tar xf nasm-2.14.02.tar.gz
mv nasm-2.14.02 nasm

#NASM
cd /root/openssl_speed_tests/nasm/
./autogen.sh
./configure
make -j 20
make install -j 20

#OPENSSL
export OPENSSL_ENGINES=/root/openssl_speed_tests/openssl_install/lib/engines-1.1/
cd /root/openssl_speed_tests/openssl/
git checkout OpenSSL_1_1_1h
./config --prefix=/root/openssl_speed_tests/openssl_install
make update
make depend
make -j 20 && make install -j 20

#INTEL IPSEC
cd /root/openssl_speed_tests/intel_ipsec_mb/
git checkout v0.54
make -j 20
rm /usr/lib/libIPSec_MB.*
rm /lib/x86_64-linux-gnu/libIPSec_MB.*
ln -s /root/openssl_speed_tests/intel_ipsec_mb/intel-ipsec-mb.h  /root/openssl_speed_tests/intel_ipsec_mb/include/
ln -s  /root/openssl_speed_tests/intel_ipsec_mb/libIPSec_MB.so.0 /usr/lib/
ln -s  /root/openssl_speed_tests/intel_ipsec_mb/libIPSec_MB.so.0 /lib/x86_64-linux-gnu/
ln -s  /root/openssl_speed_tests/intel_ipsec_mb/libIPSec_MB.so /lib/x86_64-linux-gnu/

#QATENGINE
cd /root/openssl_speed_tests/qat_engine/
git checkout v0.6.1
./autogen.sh
./configure --enable-vaes_gcm  --enable-ipsec_offload --with-ipsec_install_dir=/root/openssl_speed_tests/intel_ipsec_mb/  --with-openssl_install_dir=/root/openssl_speed_tests/openssl_install/
make -j 20
make install -j 20
```

To run tests, follow the steps below.
```bash
cd /root/openssl_speed_tests/openssl_install/bin/
taskset -c 1 ./openssl speed -multi 1 -evp aes-128-gcm #NON-VAES tests
taskset -c 1 ./openssl speed -engine qatengine -multi 1 -evp aes-128-gcm #VAES tests
```

### Interpreting the Results.
The results provided by OpenSSL Speed are in KB/s.  There are a number of columns of output, and each column represents a different sized buffer that was looped for a period of time using a crypto algorithm.  The higher the outputted number, the better.  This means that some number of bytes of dummy data was encrypted with a particular algorithm. For non-VAES GCM, we should see some number of bytes encrypted, and for VAES we should see some number of bytes encrypted that is higher than non-VAES. Note, the "-multi" argument and "taskset -c ??" argument should increase in lock-step.  We do this to provide some determinism and to prevent any potential thread migration. Also it is a good idea to disable P and C states in the bios to have  a stable core frequency.

Below is an example of an output.
```bash
evp             100.0k  200.0k  300.0k  400.0k  500.0k  600.0k
```
The column performance will be mapped to buffer size as such...
16B=100.0k
64B=200.0k
256B=300.0k
1024B=400.0k
8192B=500.00k
16384B=600.0k
