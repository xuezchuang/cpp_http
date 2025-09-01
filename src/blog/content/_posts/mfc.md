title: mfc
author: xuezc
tags:
  - mfc
  - c++
description: mfc框架知识
categories:
  - mfc
abbrlink: 58527da2
date: 2023-04-28 15:59:00
---
# 对话框
在父对话框中重新创建子对话框,会刷新父对话框的cwnd和hwnd的映射表
```
CObject* CHandleMap::FromHandle(HANDLE h)
{
	//此时会查找hwnd的映射cwnd.
	CObject* pObject = LookupPermanent(h);
	//如果找不到,会从mfc的分配器中m_alloc,分配一个.创建了新的cwnd 
	m_temporaryMap.SetAt((LPVOID)h, pTemp);
	AfxSetNewHandler(pnhOldHandler);    
}
```
CHandleMap剔除句柄在winhand.cpp中
```
void CHandleMap::RemoveHandle(HANDLE h)
{
	//
}
void CHandleMap::DeleteTemp()
{
}
```
所以如果在创建子对话框上下文中,不要保存CWnd,之前保存的可能析构掉.保存hwnd.
如果使用DoDataExchange在父类中将控件绑定起来,那么该hwnd的映射就会存在CHandleMap::m_permanentMap中,创建子对话框就不会重新绑定
```
void CHandleMap::SetPermanent(HANDLE h, CObject* permOb)
{
	BOOL bEnable = AfxEnableMemoryTracking(FALSE);
	m_permanentMap[(LPVOID)h] = permOb;
	AfxEnableMemoryTracking(bEnable);
}
```

问chatgpt的问题:mfc是如何重用现有的 CWnd 对象和 HWND 句柄.
下面是chatgpt的回答
```
在 MFC 中，重用现有的 CWnd 对象和 HWND 句柄的机制是通过使用 CHandleMap 类实现的。
CHandleMap 是一个映射表，用于将 HWND 句柄和 CWnd 对象进行关联。
在创建窗口时，MFC 首先会查找 HWND 句柄是否已经在 CHandleMap 中存在对应的 CWnd 对象，
如果存在，则直接返回该对象，否则就会创建一个新的 CWnd 对象，并将其与 HWND 句柄关联起来。

当创建子窗口时，MFC 会尝试重用父窗口的 CWnd 对象和 HWND 句柄，以提高程序的性能和效率。
具体而言，MFC 会将子窗口的 HWND 句柄添加到父窗口的 CHandleMap 中，
并将其关联到父窗口的 CWnd 对象上。这样，在父窗口的消息处理函数中，
可以直接使用子窗口的 CWnd 对象来处理消息，而无需重新创建对象和句柄。

当父窗口的 CWnd 对象被析构时，MFC 会自动将其关联的 HWND 句柄从 CHandleMap 中移除，
以确保不会访问已经失效的句柄。
如果在父窗口析构之前，子窗口已经被析构或者从父窗口中移除，
那么子窗口的 CWnd 对象和 HWND 句柄也会被从 CHandleMap 中移除，以确保不会访问已经失效的对象和句柄。

总之，MFC 通过使用 CHandleMap 来管理 CWnd 对象和 HWND 句柄的关系，
实现了对窗口对象和句柄的高效管理和重用，从而提高了程序的性能和效率。
```
## OnIdle
一般情况下创建父对话框模态对话框时,子对话框切换不会触发OnIdle消息.触发手动调用/或者使用Ontimer等.
同时OnIdle会清除对话框的HandlMap