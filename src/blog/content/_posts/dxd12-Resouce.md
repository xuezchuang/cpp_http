title: dxd12 Resource
author: xuezc
tags:
  - dx12
description: Resource Create
categories:
  - dx12
abbrlink: 6d839bcb
date: 2024-02-22 19:12:00
---
# 创建资源
## CreateCommittedResource
该方式是通过调用ID3D12Device::CreateCommittedResource方法来创建资源。这个方法其实主要还是为了兼容旧的存储管理方式而添加的，D3D12的文档中也不推荐使用这个方法。

使用它时，被创建的资源是被放在系统默认创建的堆上的，这种堆在代码中是看不到的，因此也被称为隐式堆，所以对其存储所能做的控制就比较局限了。当然调用时只需要通过堆属性参数来指定资源被放到那个默认堆上即可。因此调用它就不用额外自己去创建堆了。
## CreatePlacedResource
## CreateReservedResource
# 默认堆和上传堆
# 资源屏障