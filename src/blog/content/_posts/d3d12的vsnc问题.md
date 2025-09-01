title: d3d12的vsnc问题
author: xuezc
abbrlink: d7f7d139
tags:
  - d3d12
description: 垂直同步的问题
categories:
  - d3d12
date: 2023-12-08 11:01:00
---
# 概述
mSwapChain->Present(1, 0);
mSwapChain->GetFrameStatistics(&stats);
mSwapChain->GetLastPresentCount(&PresentID);
这几个API的理解,不正确的地方请指正.

# vsnc
prenset(1,0)为开启垂直同步,(0,0)不开启
在调用完present的后的FlushCommandQueue中,UINT64 gpufence = mFence->GetCompletedValue();得到gpu的fence后和下次帧来比较.
在调用draw前,调用GetFrameStatistics和GetLastPresentCount查看当前的帧数据,发现开启了vsnc的时候.
fence和当前的fence分别是58 59;stats.PresentCount=53, PresentID=56的理解:
当前cpu和gpu完成的渲染队列确实是59帧,当前程序交换链buffcount是3.
```
static const int SwapChainBufferCount = 3u;
int mCurrBackBuffer = 0;
Microsoft::WRL::ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount];
```
也就是说完成了53帧,54 55 56帧在缓冲区,属于待present到屏幕的状态.
57 58 59帧属于交换链后的状态,present一个交换一个.

当前程序的状态就是present会阻塞,(目前的分析全部为cpu和gpu运行最快,超过屏幕的fps的情况)

当设置present(0,0)的时候,present不会阻塞,会出现屏幕撕裂的情况.(我们只分析cpu和gpu的状态)
fence分别是423 422;stats.PresentCount=385, PresentID=420的理解;
fence和presentID相差3帧就是3个缓冲的区的差.
stats.PresentCount应该为上一次屏幕扫描要输出的缓冲区,扫描到最后一行后,那个缓冲区是是第385帧.
PresentID是当前替换到缓冲区的帧,也就是当前屏幕画出的图像可能是386帧-420帧中混合的图像.

关键的问题不在于屏幕fps的问题,cpu和gpu的问题,后续分析在cpu和gpu的buffercount交换链数量和framecount的数量上如何确定https://www.intel.cn/content/www/cn/zh/developer/articles/code-sample/sample-application-for-direct3d-12-flip-model-swap-chains.html

分析此snc的fence的cpu和gpu本质上是同步的.gpu要等cpu提交的渲染队列才可以开始渲染
```
	UINT64 gpufence = mFence->GetCompletedValue();
	if (gpufence < mCurrentFence+1)
	{
		HANDLE eventHandle = CreateEvent(nullptr, false, false, nullptr);// EVENT_ALL_ACCESS);

		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));

		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
    }
```