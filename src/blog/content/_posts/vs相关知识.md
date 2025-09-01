title: vs相关知识
author: xuezc
abbrlink: 4dc2a904
tags:
  - vs
  - windows
description: vs
categories:
  - vs
date: 2023-04-20 19:28:00
---
# 链接
## 链接器命令
在链接器命令输入以下,编译时会打印出所查找的库的顺序.
```
/verbose:lib 
```

# 当前线程断点
设置变量,用变量断..目前只知道这个....
```
std::thread::id nId = std::this_thread::get_id();
```