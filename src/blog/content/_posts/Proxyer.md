title: Proxyer
author: xuezc
abbrlink: b01b2ec1
tags:
  - Proxyer
categories:
  - Proxyer
description: 私有内网映射工具。
date: 2023-03-30 20:16:00
---
# 安装服务
服务器上面先安装Docker、Docker-Compose，然后使用下面的命令安装，不需要其它任何配置
## 1. 下载docker-compose.yml配置文件
```
curl -sSL https://gitee.com/guangleihe/proxyer/raw/master/docker-compose.yaml -o docker-compose.yml
```
## 2. 设置对外访问IP或者域名，启动服务
```
PROXYER_PUBLIC_HOST={你的公网IP地址或者域名} docker-compose up -d
```
安装完成之后，就可以使用浏览器访问6789端口(http://{你的公网IP地址或者域名}:6789/)来使用了。
服务器端口
服务器端口6789、6544命令端口需要开放
可以根据映射的情况，开放服务器映射的其它端口（客户端可以指定映射端口）
如果选择了随机映射端口，需要开放服务器的 30000 - 65530 端口
## Docker 和 Docker-compose安装
一键安装Docker
```
curl -sSL https://get.daocloud.io/docker | sh
sudo service docker restart
```
一键安装docker-compose
下载docker-compose到/usr/local/bin 目录
```
sudo curl -L "https://get.daocloud.io/docker/compose/releases/download/1.24.1/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
```
为docker-compose设置执行权限
```
sudo chmod +x /usr/local/bin/docker-compose
```
## 安装Proxyer服务
下载docker-compose.yml到本地
```
curl -sSL https://gitee.com/guangleihe/proxyer/raw/master/docker-compose.yaml -o docker-compose.yml
```
接下来就可以使用docker-compose.yml来下载镜像并且运行服务了。在运行服务之前proxyer需要你设置一个服务器对外的IP地址或者域名，通过环境变量PROXYER_PUBLIC_HOST来设置。您可以使用下面的方式简单的设置并且启动服务.
```
PROXYER_PUBLIC_HOST={你的公网IP地址或者域名} docker-compose up -d
```
假设您的公网服务器地址是121.11.111.111那么您的安装命令是
```
PROXYER_PUBLIC_HOST=121.11.111.111 docker-compose up -d
```
如果一切正常，您将会看到如下的打印
```
root@pdns-server:~# PROXYER_PUBLIC_HOST=121.11.111.111 docker-compose up -d
Starting root_etcd_1      ... done
Recreating root_install_1 ... done
Starting root_pdns_1      ... done
Recreating root_stp_1     ... done
```
## 使用并设置服务端
如果安装成功，那么您就可以通过浏览器来访问服务器的6789端口，并且设置服务端。以上面的例子为例，您可以通过
```
http://121.11.111.111:6789/
```
来设置服务端,打开界面设置，会首先让你设置一个服务端的访问密码
<img src="/images/Proxyer_1.png">
设置完成之后，您就可以下载客户端并且使用了
<img src="/images/Proxyer_2.png">
## 客户端使用
客户端使用也极其简单，客户端只有一个可执行程序，不会依赖其它配置和第三方文件。下载了对应的客户端之后，运行，可以通过本机的http://127.0.0.1:9876来打开界面。

Windows界面客户端会打开浏览器，然后最小化到右下脚。未来可以继续操作
控制台界面，会打印可以访问的地址，用浏览器访问这个地址即可
打开网页的第一件事，会提示您输入刚刚设置的服务器授权密码
<img src="/images/Proxyer_3.png">
每一台设备都会有一个唯一的设备序列号，每一个映射的端口都会有一查询序列号，您可以使用这个序号在服务器上面查询映射出来的地址。
所有的数据都会挂载到/mnt/data/proxyer下面，如果想清空服务器数据，重新安装，可以先将这个目录下面所有的数据删除
docket-compse stop可以停止服务

# Proxy不能使用时
```shell
sudo service docker restart
sudo chmod +x /usr/local/bin/docker-compose
```
## 还是不可以的话，下载docker-compose.yml到本地
```shell
curl -sSL https://gitee.com/guangleihe/proxyer/raw/master/docker-compose.yaml -o docker-compose.yml
```
## 一般直接重启
```shell
PROXYER_PUBLIC_HOST=112.126.62.233 docker-compose up -d
```