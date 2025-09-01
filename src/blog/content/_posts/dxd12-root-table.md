title: dxd12 root-table
author: xuezc
abbrlink: 90c7c94
tags:
  - dx12
  - graph
categories:
  - dx12
  - graph
description: dx12 数据的绑定
date: 2024-01-15 09:00:00
---
# 根签名 root signature
```
root signature只有64 DWORD的空间.
Descriptor tables cost 1 DWORD each.		
Root constants cost 1 DWORD each, since they are 32 - bit values
Root descriptors(64 - bit GPU virtual addresses) cost 2 DWORDs each.
```
[微软链接](https://learn.microsoft.com/en-us/windows/win32/direct3d12/example-root-signatures "查看root-table")
<img src="/images/root-tables-1.png">

## Constants绑定有3种方法
### 方法一:Root constants
D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS
dxd12的封装函数是InitAsConstants.
draw的时候是:SetGraphicsRoot32BitConstants
```
CD3DX12_ROOT_PARAMETER slotRootParameter[1];
slotRootParameter[0].InitAsConstants(16, 0);//绑定一个4x4的矩阵
XMFLOAT4X4 m;
XMStoreFloat4x4(&m, XMMatrixTranspose(worldViewProj)); // 转置矩阵
mCommandList->SetGraphicsRoot32BitConstants(
		0,                    // 根参数的索引
		sizeof(XMFLOAT4X4) / sizeof(float), // 要传递的32位浮点数的数量
		&m,                  // 指向常量的指针
		0                     // 常量缓冲区中的偏移量（以32位浮点数为单位）
	);
```
### 方法二:Root descriptors
直接绑定到根签名下,一般矩阵使用这种形式.
D3D12_ROOT_PARAMETER_TYPE_CBV
```
CD3DX12_ROOT_PARAMETER slotRootParameter[1];
slotRootParameter[0].InitAsConstantBufferView(0);
//draw
D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
mCommandList->SetGraphicsRootConstantBufferView(0, cbAddress);
```
### 方法三:Descriptor tables
使用根标识表绑定
```
CD3DX12_ROOT_PARAMETER slotRootParameter[1];
CD3DX12_DESCRIPTOR_RANGE cbvTable;
cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);
//draw
ID3D12DescriptorHeap* descriptorHeaps[] = {mCbvHeap.Get()};
mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
mCommandList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());
```