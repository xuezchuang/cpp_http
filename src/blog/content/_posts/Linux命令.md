title: Linux命令
author: xuezc
tags:
  - linux
categories:
  - linux
abbrlink: 82734d8
description: linux常用的命令,防火墙命令,netstat命令等.....
date: 2023-03-30 20:47:00
---
# netstat命令各个参数说明如下：
```
-t : 指明显示TCP端口
-u : 指明显示UDP端口
-l : 仅显示监听套接字(所谓套接字就是使应用程序能够读写与收发通讯协议(protocol)与资料的程序)
-p : 显示进程标识符和程序名称，每一个套接字/端口都属于一个程序。
-n : 不进行DNS轮询，显示IP(可以加速操作)
```
即可显示当前服务器上所有端口及进程服务，于grep结合可查看某个具体端口及服务情况·· 
查看某一端口的连接数量,比如3306端口
```
netstat -pnt |grep :3306 |wc
```
查看某一端口的连接客户端IP 比如3306端口
```
netstat -anp |grep 3306
```
```
netstat -an 查看网络端口 
lsof -i :port，使用lsof -i :port就能看见所指定端口运行的程序，同时还有当前连接。 
nmap 端口扫描
netstat -nupl  (UDP类型的端口)
netstat -ntpl  (TCP类型的端口)
netstat -anp 显示系统端口使用情况
netstat -ntulp |grep 80   //查看所有80端口使用情况· 
netstat -an | grep 3306   //查看所有3306端口使用情况· 
查看一台服务器上面哪些服务及端口    netstat  -lanp
```
## 通信测试
```
nc –v -l 127.0.0.1 12345 启动侦听
nc -v 127.0.0.1 11连接
```
创建服务:
```
nc -l 12345 ,client:nc -v 192.168.65.131 12345
```
连接库的软连接
```
sudo ln -sv /usr/lib64/mysql/libmysqlclient.so.18 /usr/lib/libmysqlclient.so
```
## 网络
### 查看端口状态
```
lsof -i:20000
```
```
/etc/init.d/iptables status
查询某个服务的端口
netstat -tunlp |grep 
//
网络包:
tcpdump -i any 'port 12345' -XX -nn -vv -S
我们可以使用以下指令来统计中间状态信息：
netstat -n | awk '/^tcp/ {++S[$NF]} END {for(a in S) print a, S[a]}' 
-X 以 ASCII 和十六进制的形式输出捕获的数据包内容，减去链路层的包头信息；-XX 以 ASCII 和十六进制的形式输出捕获的数据包内容，包括链路层的包头信息。
-n 不要将 ip 地址显示成别名的形式；-nn 不要将 ip 地址和端口以别名的形式显示。
-S 以绝对值显示包的 ISN 号（包序列号），默认以上一包的偏移量显示。
-vv 抓包的信息详细地显示；-vvv 抓包的信息更详细地显示。
-w 将抓取的包的原始信息（不解析，也不输出）写入文件中，后跟文件名：
//
```
## 常用命令
查看当前执行文件依赖的库
```
objdump -x ./chateserver | grep NEEDED
```
### 后台运行程序
```
nohup 服务 &
```


# 查看进程详细
查看一个服务有几个端口或者pid。比如要查看mysqld
```
ps -ef |grep mysqld
ps -mp 经常pid -o THREAD,tid,time
top -p 10997
```
```
//状态:
ps  aux|grep chatserver
D    uninterruptible sleep (usually IO)
I    Idle kernel thread
R    running or runnable (on run queue)
S    interruptible sleep (waiting for an event to complete)
T    stopped by job control signal
t    stopped by debugger during the tracing
W    paging (not valid since the 2.6.xx kernel)
X    dead (should never be seen)
Z    defunct ("zombie") process, terminated but not reaped by
    its parent
```
查看某个服务的CPU和内存
```
ps -aux | grep 服务名
```
查看当前用户的进程
```
ps -u;
ps -a；
ps -au;
```
# 查看端口
## 查看端口号信息
```
netstat -an | grep 端口号
```
一般会有tcp和tcp6对应的ip不同的情况
```
lsof -i:端口号
lsof -i -Pn | gren 端口号 查看端口号的连接状态
nc -v 127.0.0.1 20009 模拟客户端
```
# iptable
## 允许PING设置
```
A.临时允许PING操作的命令为：#echo 0 >/proc/sys/net/ipv4/icmp_echo_ignore_all
B.永久允许PING配置方法。
/etc/sysctl.conf中增加一行
net.ipv4.icmp_echo_ignore_all=1
#果已经有net.ipv4.icmp_echo_ignore_all这一行了，直接修改=号后面的值即可的（0表示允许，1表示禁止）。
修改完成后执行sysctl -p使新配置生效。
```
## 禁止Ping设置
```
A.临时禁止PING的命令为：#echo 1 >/proc/sys/net/ipv4/icmp_echo_ignore_all
B.永久允许PING配置方法。
/etc/sysctl.conf中增加一行
net.ipv4.icmp_echo_ignore_all=0
#果已经有net.ipv4.icmp_echo_ignore_all这一行了，直接修改=号后面的值即可的。（0表示允许，1表示禁止）
#改完成后执行sysctl -p使新配置生效。
```
## 防火墙设置
```
    1、允许PING设置
    iptables -A INPUT -p icmp --icmp-type echo-request -j ACCEPT
    iptables -A OUTPUT -p icmp --icmp-type echo-reply -j ACCEPT
    **或者也可以临时停止防火墙操作的。**
    service iptables stop
    2、禁止PING设置
    iptables -A INPUT -p icmp --icmp-type 8 -s 0/0 -j DROP
service iptables status
允许Ping
###配置文件位置###
/etc/rsyslog.conf
kern.warning /var/log/iptables.log
kern.warning ~
. /var/log/messages

 1、允许PING设置      
    iptables -A INPUT -p icmp --icmp-type echo-request -j ACCEPT
    iptables -A OUTPUT -p icmp --icmp-type echo-reply -j ACCEPT
 2、禁止PING设置
    iptables -A INPUT -p icmp --icmp-type 8 -s 0/0 -j DROP
#记录某个端口日志
iptables -N SILENCE_INPUT_LOG
iptables -I INPUT 1 -j SILENCE_INPUT_LOG
iptables -A SILENCE_INPUT_LOG -p tcp --dport 8080 -j LOG --log-prefix "iptables:"
记录icmp
INPUT 4
iptables -A  -d 108.61.247.94 -p icmp --icmp-type 8 -j LOG --log-prefix "< < this is ping > >"
iptables -A INPUT -p icmp --icmp-type 8 -j LOG --log-prefix "< < this is ping > >"
iptables -D INPUT -p icmp --icmp-type 8 -j LOG --log-prefix "< < this is ping > >"
iptables -nL --line-number 显示行数
iptables -D INPUT 2 删除
# 修改防火墙NAT表中的PREROUTING和POSTROUTING链，添加自定义log-prefix
vim /etc/sysconfig/iptables
*nat
:PREROUTING ACCEPT [0:0]
:INPUT ACCEPT [0:0]
:OUTPUT ACCEPT [0:0]
:POSTROUTING ACCEPT [0:0]
-A PREROUTING -p tcp -d <IP> --dport 443 -j LOG --log-prefix seatalk:
-A PREROUTING -p tcp -d <IP> --dport 443 -j DNAT --to-destination 10.71.19.142:443
-A POSTROUTING -j MASQUERADE
COMMIT
# 重启iptables
service iptables reload
```
## service iptables status
```
1、允许PING设置      
    iptables -A INPUT -p icmp --icmp-type echo-request -j ACCEPT
    iptables -A OUTPUT -p icmp --icmp-type echo-reply -j ACCEPT
 2、禁止PING设置
    iptables -A INPUT -p icmp --icmp-type 8 -s 0/0 -j DROP
#记录某个端口日志
iptables -N SILENCE_INPUT_LOG
iptables -I INPUT 1 -j SILENCE_INPUT_LOG
iptables -A SILENCE_INPUT_LOG -p tcp --dport 8080 -j LOG --log-prefix "iptables:"
记录icmp
INPUT 4
iptables -A  -d 108.61.247.94 -p icmp --icmp-type 8 -j LOG --log-prefix "< < this is ping > >"
iptables -A INPUT -p icmp --icmp-type 8 -j LOG --log-prefix "< < this is ping > >"
iptables -D INPUT -p icmp --icmp-type 8 -j LOG --log-prefix "< < this is ping > >"
iptables -nL --line-number 显示行数
iptables -D INPUT 2 删除
# 修改防火墙NAT表中的PREROUTING和POSTROUTING链，添加自定义log-prefix
vim /etc/sysconfig/iptables
*nat
:PREROUTING ACCEPT [0:0]
:INPUT ACCEPT [0:0]
:OUTPUT ACCEPT [0:0]
:POSTROUTING ACCEPT [0:0]
-A PREROUTING -p tcp -d <IP> --dport 443 -j LOG --log-prefix seatalk:
-A PREROUTING -p tcp -d <IP> --dport 443 -j DNAT --to-destination 10.71.19.142:443
-A POSTROUTING -j MASQUERADE
COMMIT
# 重启iptables
service iptables reload
```
### 例子
```
假设如下的规则：
iptables -A INPUT -p icmp -m limit --limit 6/m --limit-burst 5 -j ACCEPT
iptables -P INPUT DROP
然后从另一部主机上ping这部主机，就会发生如下的现象：
首先我们能看到前四个包的回应都非常正常，然后从第五个包开始，我们每10秒能收到一个正常的回应。这是因为我们设定了单位时间(在这里是每分钟)内允许通过的数据包的个数是每分钟6个，也即每10秒钟一个；其次我们又设定了事件触发阀值为5，所以我们的前四个包都是正常的，只是从第五个包开始，限制规则开始生效，故只能每10秒收到一个正常回应。
```
# 防火墙firewall
## 常用命令
```
firewall-cmd --query-port=8080/tcp              **查询端口是否开放**
firewall-cmd --list-ports                       **查看已经开放的端口**
firewall-cmd --permanent --add-port=80/tcp      **开放80端口**
firewall-cmd --permanent --remove-port=8080/tcp **移除端口**
firewall-cmd --reload                           **重启防火墙(修改配置后要重启防火墙)**
firewall-cmd --list-all                         **查看所有开发端口**
```
查看firewall服务状态	--permanent参数表示永久开放, 可以根据需要换成其他参数
```
systemctl status firewalld  **查看firewall的状态**
firewall-cmd --state    
service firewalld start     **开启**   
service firewalld restart   **重启**
service firewalld stop      **关闭**
```
测试端口开放没
```
telnet ip 端口
```
日志
```
firewall-cmd --get-log-denied
```
查看是否启用了在所有规则中启用拒绝日志记录。
```
firewall-cmd --set-log-denied=[all |unicast |broadcast |multicast |off]
**设置日志等同于在/etc/firewalld/firewalld.conf里设置LogDenied=off**
###firewall-cmd --get-icmptypes
###firewall-cmd  --list-icmp-blocks
```
允许PING设置
```
firewall-cmd --permanent --add-rich-rule='rule protocol value=icmp drop'
```
禁止PING设置
```
firewall-cmd --permanent --remove-rich-rule='rule protocol value=icmp drop'
```

# 杂项
vmstat 是一个常用的系统性能分析工具，主要用来分析系统的内存使用情况，也常用来分析 CPU 上下文切换和中断的次数。
```
每隔5秒输出1组数据
$ vmstat 5
procs -----------memory---------- ---swap-- -----io---- -system-- ------cpu-----
 r  b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st
 0  0      0 7005360  91564 818900    0    0     0     0   25   33  0  0 100  0  0
 cs（context switch）是每秒上下文切换的次数。
 in（interrupt）则是每秒中断的次数。
 r（Running or Runnable）是就绪队列的长度，也就是正在运行和等待 CPU 的进程数。
 b（Blocked）则是处于不可中断睡眠状态的进程数。
```
pidstat 给它加上 -w 选项，你就可以查看每个进程上下文切换的情况了。
```
每隔5秒输出1组数据
$ pidstat -w 5
Linux 4.15.0 (ubuntu)  09/23/18  _x86_64_  (2 CPU)
08:18:26      UID       PID   cswch/s nvcswch/s  Command
08:18:31        0         1      0.20      0.00  systemd
08:18:31        0         8      5.40      0.00  rcu_sched
...
 cswch ，表示每秒自愿上下文切换（voluntary context switches）的次数
 nvcswch ，表示每秒非自愿上下文切换（non voluntary context switches）的次数。
 ```
 



















