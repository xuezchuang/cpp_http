title: docker
author: xuezc
tags:
  - linux
categories:
  - linux
abbrlink: f255ffad
description: docker 的使用
date: 2023-03-31 11:03:00
---
# 查看所有容器
    1.查看所有容器
        docker ps -a
    2.查看所有运行的容器
        docker ps
    3.删除容器
        docker rm 容器id
        docker rm 容器名字
    4.查看镜像库
        docker images
    
    docker run  --name nginx -d -p 80:80 -v /nginx/html:/usr/share/nginx/html -v /nginx/conf/nginx.conf:/etc/nginx/nginx.conf -v /nginx/conf.d:/etc/nginx/conf.d  -v /nginx/logs:/var/log/nginx docker.io/nginx
    
    
#配置docker和nginx的反向代理
##配置nginx
    1.docker pull nginx
    2.使用nginx镜像来创建nginx容器实例，名字是nginx-test
        docker run --name nginx-test -p 80:80 -d nginx
    **也可以简单实用bt的docker安装nginx后来创建，这个镜像是临时的，用来拷贝配置文件**
    3.创建最后的nginx的配置文件的目录
        mkdir -p /root/nginx/www /root/nginx/logs /root/nginx/conf
    4.实用命令docker ps -a查看当前临时的nginx-test的容器id
    5.将nginx-test容器配置文件copy到本地
        docker cp 查到的nginx-test的容器id:/etc/nginx/nginx.conf /root/nginx/conf
    6.创建新nginx容器nginx-web,并将www,logs,conf目录映射到本地;因为端口会冲突，需要先删掉临时的nginx-test镜像
        docker run -d -p 80:80 --name nginx-web -v /root/nginx/www:/usr/share/nginx/html -v /root/nginx/conf/nginx.conf:/etc/nginx/nginx.conf -v /root/nginx/logs:/var/log/nginx nginx
    这些配置文件映射也可用bt的docker管理器配置
##nginx反向代理设置
**在/root/nginx/conf/目录下的默认的nginx是这样的：**
```
    user  nginx;
    worker_processes  1;
    
    error_log  /var/log/nginx/error.log warn;
    pid        /var/run/nginx.pid;
    
    
    events {
        worker_connections  1024;
    }
    
    
    http {
        include       /etc/nginx/mime.types;
        default_type  application/octet-stream;
    
        log_format  main  '$remote_addr - $remote_user [$time_local] "$request" '
                          '$status $body_bytes_sent "$http_referer" '
                          '"$http_user_agent" "$http_x_forwarded_for"';
    
        access_log  /var/log/nginx/access.log  main;
    
        sendfile        on;
        #tcp_nopush     on;
    
        keepalive_timeout  65;
    
        #gzip  on;
    
        include /etc/nginx/conf.d/*.conf;
```
**在最后加配置即可代理**

        server{
       listen 80;
       charset utf-8;
       server_name 192.168.112.135;
 
       location / {
          proxy_pass http://192.168.112.135:8080;
          proxy_redirect default;
       }
    }
#tomcat下同一个端口不同域名和不同端口访问不同项目；
```
     <Service name="Catalina">
    <Connector port="8080" protocol="HTTP/1.1" redirectPort="8443" />
    <Connector port="8009" protocol="AJP/1.3" redirectPort="8443" />
    <Engine defaultHost="localhost" name="Catalina">
      <Realm className="org.apache.catalina.realm.LockOutRealm">
        <Realm className="org.apache.catalina.realm.UserDatabaseRealm" resourceName="UserDatabase" />
      </Realm>
      <Host appBase="webapps" autoDeploy="true" name="localhost" unpackWARs="true">
        <Valve className="org.apache.catalina.valves.AccessLogValve" directory="logs" pattern="%h %l %u %t &quot;%r&quot; %s %b" prefix="localhost_access_log" suffix=".txt" />
	  	</Host>
	  	  <!--此处为同一个端口不同域名访问第二个项目test-->	
	  	<Host appBase="test" autoDeploy="true" name="test.snowsome.com" unpackWARs="true">
			<Valve className="org.apache.catalina.valves.AccessLogValve" directory="logs" pattern="%h %l %u %t &quot;%r&quot; %s %b" prefix="localhost_access_log" suffix=".txt" />
			<Alias>test.snowsome.com</Alias>
			<Context path="" docBase="/www/server/tomcat/test" debug="0" reloadable="true" />  
		</Host>
    </Engine>
  </Service>
   <!--此处为不同端口第三个项目test-->
  <Service name="Catalina2">
    <Connector port="8081" protocol="HTTP/1.1" redirectPort="8443" />
    <Connector port="8009" protocol="AJP/1.3" redirectPort="8443" />
    <Engine defaultHost="localhost" name="Catalina2">
      <Realm className="org.apache.catalina.realm.LockOutRealm">
        <Realm className="org.apache.catalina.realm.UserDatabaseRealm" resourceName="UserDatabase" />
      </Realm>
      <Host appBase="demo" autoDeploy="true" name="localhost" unpackWARs="true">
        <Valve className="org.apache.catalina.valves.AccessLogValve" directory="logs" pattern="%h %l %u %t &quot;%r&quot; %s %b" prefix="localhost_access_log" suffix=".txt" />
	  	</Host>
    </Engine>
  </Service>
```