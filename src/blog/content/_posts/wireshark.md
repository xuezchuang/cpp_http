title: wireshark
author: xuezc
tags:
  - windows
  - net
categories:
  - windows
abbrlink: 503d1989
description: wireshark的使用
date: 2023-03-31 11:04:00
---
#1.过滤IP，如来源IP或者目标IP等于某个IP
    ip.src eq 192.168.1.107 or ip.dst eq 192.168.1.107
    或者
    ip.addr eq 192.168.1.107 // 都能显示来源IP和目标IP
#2.过滤端口
    tcp.port eq 80 // 不管端口是来源的还是目标的都显示
    tcp.port == 80
    tcp.port eq 2722
    tcp.port eq 80 or udp.port eq 80
    tcp.dstport == 80 // 只显tcp协议的目标端口80
    tcp.srcport == 80 // 只显tcp协议的来源端口80
    udp.port eq 15000
    过滤端口范围
    tcp.port >= 1 and tcp.port <= 80