title: mysql
author: xuezc
tags:
  - mysql
  - linux
categories:
  - mysql
abbrlink: 9520183a
description: mysql的使用
date: 2023-03-31 10:54:00
---
# 创建
mysql -u root -p
```
CREATE TABLE table_name (column_name column_type);
create table tb3(
id smallint unsigned auto_increment primary key,
username varchar(20) not null
);
```
insert tb3 (id,username) values (4,'jona');
select id,username from tb3;
## 1

# 命令
查看当前的数据库
```
select databases();
```
删除数据库
```
drop database;
```
使用数据库
```
use database;
```
查看某个DB的表
show tables from Db;
查看当前数据库所有表
```
show tables;
#查询数据
select * from tb3;
#查询表中的表头数据 == des
show columns from tb;
#删除player表
drop table player;
#更改表名字
rename table tablename1 to tablename2;
#设置显示的长度
set print elements 0 
#更改数据
UPDATE 表 SET username = '11' WHERE id = 3;
#跳过mysql密码
#locate my.cnf 找到这个文件，打开写入数据，skip-grant-tables
```
修改数据库编码
```
alter database chivalrousman CHARACTER SET utf8 COLLATE utf8_general_ci;
```
查询数据库创建的字符集
```
show create database chivalrousman;
```
```
//控制用户权限
#在window下登录服务器mysql,可视化安装  https://www.cnblogs.com/wei9593/p/11907307.html
    安装完成后登录;将服务器的mysql的user下的root改为%;<这样表示所有用户>
    创建用户:CREATE USER 'username'@'localhost' IDENTIFIED BY 'desired_password';<在User表中添加数据>
    授权登录:GRANT ALL PRIVILEGES ON *.* TO 'xue'@'%'  WITH GRANT OPTION;
    删除权限:revoke  all on *.* from 'root'@'192.168.0.197' ;<权限表应该是informatio_schema>
    查看所有授权:select * from information_schema.user_privileges;
    flush privileges;
    创建用户的某个DB的权限保存在mysql.db里
```
```
-----service mysqld restart//重启mysql
在mysql里，查看表user可修改密码
远程登录服务器mysql的时候需要改root的host为%
//在已有表中添加字段
alter table teacher add gradeId int(11) ； alter英文意思是改变
//删除表中一行数据
delete from playerinfo where playername = 'xue';
//显示mysql用的编码格式
show variables like 'char%';
character-set-server=utf8 //在my.cnf里加入更改编码格式
latin1
一次性修改表中所有字段的字符集语句：alter table 表名 convert to character set utf8;
```