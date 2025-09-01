title: FXAA
author: xuezc
tags:
  - graph
  - opengl
categories:
  - opengl
description: 'FXAA 是一种后处理抗锯齿技术,依靠边缘检测来消除锯齿'
abbrlink: 152da8b7
date: 2023-05-28 11:02:00
---
FXAA3.11有两个版本，其中 Quality 版本是较注重抗锯齿质量的版本，Console 版本是较注重抗锯齿速度的版本.
这儿大致说一下Quality版本.
# Luma
首先，我们先来计算当前处理的像素点和周围像素点的亮度对比值，FXAA 通过确定水平和垂直方向上像素点的亮度差，来计算对比值。当对比度值较大时，我们认为需要进行抗锯齿处理。

求亮度可以使用常用的求亮度公式 L = 0.213 * R + 0.715 * G + 0.072 * B，也可以直接使用G分量的颜色值作为亮度值，因为绿色对整体亮度的贡献是最大的。
# 检测 AA 边缘
首先，我们先来计算当前处理的像素点和周围像素点的亮度对比值，FXAA 通过确定水平和垂直方向上像素点的亮度差，来计算对比值。当对比度值较大时，我们认为需要进行抗锯齿处理
<img src="/images/fxaa4.png">
```
	FxaaFloat lumaS = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(0, 1), fxaaQualityRcpFrame.xy));
	FxaaFloat lumaE = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(1, 0), fxaaQualityRcpFrame.xy));
	FxaaFloat lumaN = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(0, -1), fxaaQualityRcpFrame.xy));
	FxaaFloat lumaW = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(-1, 0), fxaaQualityRcpFrame.xy));
    
   FxaaFloat maxSM = max(lumaS, lumaM);
	FxaaFloat minSM = min(lumaS, lumaM);
	FxaaFloat maxESM = max(lumaE, maxSM);
	FxaaFloat minESM = min(lumaE, minSM);
	FxaaFloat maxWN = max(lumaN, lumaW);
	FxaaFloat minWN = min(lumaN, lumaW);
	FxaaFloat rangeMax = max(maxWN, maxESM);
	FxaaFloat rangeMin = min(minWN, minESM);
	FxaaFloat rangeMaxScaled = rangeMax * fxaaQualityEdgeThreshold;
	FxaaFloat range = rangeMax - rangeMin;
	FxaaFloat rangeMaxClamped = max(fxaaQualityEdgeThresholdMin, rangeMaxScaled);
	FxaaBool earlyExit = range < rangeMaxClamped;
```
# 基于亮度的混合
通过计算目标像素和周围像素点的平均亮度的差值，我们来确定将来进行颜色混合时的权重。因为对角像素距离中心像素比较远，所以计算平均亮度值时的权重会略微低一些。
<img src="/images/fxaa2.png">
混合因子可见:soble算子 https://blog.sciencenet.cn/blog-425437-1139187.html
```glsl
	FxaaFloat lumaNW = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(-1, -1), fxaaQualityRcpFrame.xy));
	FxaaFloat lumaSE = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(1, 1), fxaaQualityRcpFrame.xy));
	FxaaFloat lumaNE = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(1, -1), fxaaQualityRcpFrame.xy));
	FxaaFloat lumaSW = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(-1, 1), fxaaQualityRcpFrame.xy));
    
	FxaaFloat lumaNS = lumaN + lumaS;
	FxaaFloat lumaWE = lumaW + lumaE;
	FxaaFloat subpixNSWE = lumaNS + lumaWE;
	FxaaFloat edgeHorz1 = (-2.0 * lumaM) + lumaNS;
	FxaaFloat edgeVert1 = (-2.0 * lumaM) + lumaWE;
    
	FxaaFloat lumaNESE = lumaNE + lumaSE;
	FxaaFloat lumaNWNE = lumaNW + lumaNE;
	FxaaFloat edgeHorz2 = (-2.0 * lumaE) + lumaNESE;
	FxaaFloat edgeVert2 = (-2.0 * lumaN) + lumaNWNE;
    
	FxaaFloat lumaNWSW = lumaNW + lumaSW;
	FxaaFloat lumaSWSE = lumaSW + lumaSE;
	FxaaFloat edgeHorz4 = (abs(edgeHorz1) * 2.0) + abs(edgeHorz2);
	FxaaFloat edgeVert4 = (abs(edgeVert1) * 2.0) + abs(edgeVert2);
	FxaaFloat edgeHorz3 = (-2.0 * lumaW) + lumaNWSW;
	FxaaFloat edgeVert3 = (-2.0 * lumaS) + lumaSWSE;
	FxaaFloat edgeHorz = abs(edgeHorz3) + edgeHorz4;
	FxaaFloat edgeVert = abs(edgeVert3) + edgeVert4;
    
	FxaaFloat subpixNWSWNESE = lumaNWSW + lumaNESE;
	FxaaFloat lengthSign = fxaaQualityRcpFrame.x;
	FxaaBool horzSpan = edgeHorz >= edgeVert;
	FxaaFloat subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;    
	//计算出亮度差
	if(!horzSpan) lumaN = lumaW;
	if(!horzSpan) lumaS = lumaE;`
	if(horzSpan) lengthSign = fxaaQualityRcpFrame.y;
	FxaaFloat subpixB = (subpixA * (1.0 / 12.0)) - lumaM;
```

# 计算方向
e.g.
以此举个例子说明每一步
<img src="/images/fxaa1.png">
horizontal = |-2x0+0+1| + 2x|-2x0+0+1| + |-2x0+1+0| = 4  
vertical = |-2x0+0+0| + 2x|-2x1+1+1| + |-2x0+0+0| = 0
horizontal > vertical,所以是水平边缘.

```glsl
   //subpixC 亮度比值
	FxaaFloat gradientN = lumaN - lumaM;
	FxaaFloat gradientS = lumaS - lumaM;
	FxaaFloat lumaNN = lumaN + lumaM;
	FxaaFloat lumaSS = lumaS + lumaM;
	FxaaBool pairN = abs(gradientN) >= abs(gradientS);
	FxaaFloat gradient = max(abs(gradientN), abs(gradientS));
	if(pairN) lengthSign = -lengthSign;
   FxaaFloat subpixRcpRange = 1.0 / range;
	FxaaFloat subpixC = FxaaSat(abs(subpixB) * subpixRcpRange);
    
	FxaaFloat2 posB;
	posB.x = posM.x;
	posB.y = posM.y;
	FxaaFloat2 offNP;
	offNP.x = (!horzSpan) ? 0.0 : fxaaQualityRcpFrame.x;
	offNP.y = (horzSpan) ? 0.0 : fxaaQualityRcpFrame.y;
	if(!horzSpan) posB.x += lengthSign * 0.5;
	if(horzSpan) posB.y += lengthSign * 0.5;
	//subpixE 计算平滑亮度差    
	FxaaFloat2 posN;
	posN.x = posB.x - offNP.x * FXAA_QUALITY__P0;
	posN.y = posB.y - offNP.y * FXAA_QUALITY__P0;
	FxaaFloat2 posP;
	posP.x = posB.x + offNP.x * FXAA_QUALITY__P0;
	posP.y = posB.y + offNP.y * FXAA_QUALITY__P0;
	FxaaFloat subpixD = ((-2.0) * subpixC) + 3.0;
	FxaaFloat lumaEndN = FxaaLuma(FxaaTexTop(tex, posN));
	FxaaFloat subpixE = subpixC * subpixC;
	FxaaFloat lumaEndP = FxaaLuma(FxaaTexTop(tex, posP));    
```
# 找到边界点
上述在我们的例子中,开始水平混合. 
gradientN从w开始往左查找,s向右查找.   
个人观点:大部分说0.25 x gradient的0.25是经验值.分析,s-n如果都看做中心点,s-n是4个半个像素.lumaEndN -= lumaNN * 0.5;这个差值看做2个end半像素-1个nn半像素,就是半个像素..所以0.25是线性相关的,并不是经验值.  
```
	if(!pairN) lumaNN = lumaSS;
   //梯度
	FxaaFloat gradientScaled = gradient * 1.0 / 4.0;
	FxaaFloat lumaMM = lumaM - lumaNN * 0.5;
	FxaaFloat subpixF = subpixD * subpixE;
	FxaaBool lumaMLTZero = lumaMM < 0.0;
    
	lumaEndN -= lumaNN * 0.5;
	lumaEndP -= lumaNN * 0.5;
	FxaaBool doneN = abs(lumaEndN) >= gradientScaled;
	FxaaBool doneP = abs(lumaEndP) >= gradientScaled;
	if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P1;
	if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P1;
	FxaaBool doneNP = (!doneN) || (!doneP);
	if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P1;
	if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P1; 

```

```
FxaaBool pairN = abs(gradientN) >= abs(gradientS)
if(pairN) lengthSign = -lengthSign;
```
后面的代码就是迭代的过程了,循环找到开始点和结束点.  
e.g.
最后我们找到了开始2个点,结束4个点
<img src="/images/fxaa5.png">


# 混合
```
	FxaaFloat dstN = posM.x - posN.x;
	FxaaFloat dstP = posP.x - posM.x;
	if(!horzSpan) dstN = posM.y - posN.y;
	if(!horzSpan) dstP = posP.y - posM.y;
    
	FxaaBool goodSpanN = (lumaEndN < 0.0) != lumaMLTZero;
	FxaaFloat spanLength = (dstP + dstN);
	FxaaBool goodSpanP = (lumaEndP < 0.0) != lumaMLTZero;
	FxaaFloat spanLengthRcp = 1.0 / spanLength;
    
	FxaaBool directionN = dstN < dstP;
	FxaaFloat dst = min(dstN, dstP);
	FxaaBool goodSpan = directionN ? goodSpanN : goodSpanP;
	FxaaFloat subpixG = subpixF * subpixF;
	FxaaFloat pixelOffset = (dst * (-spanLengthRcp)) + 0.5;
	FxaaFloat subpixH = subpixG * fxaaQualitySubpix;
    
	FxaaFloat pixelOffsetGood = goodSpan ? pixelOffset : 0.0;
	FxaaFloat pixelOffsetSubpix = max(pixelOffsetGood, subpixH);
	if(!horzSpan) posM.x += pixelOffsetSubpix * lengthSign;
	if(horzSpan) posM.y += pixelOffsetSubpix * lengthSign;  
	//返回像素
	return FxaaFloat4(FxaaTexTop(tex, posM).xyz, lumaM);    
```

e.g.对于示例像素   
```
pixelOffset = FxaaFloat dst = min(dstN, dstP) = 2
spanLength = 6
FxaaFloat pixelOffset = (dst * (-spanLengthRcp)) + 0.5 = -2/6+0.5 = 0.1666
```
下面给出所有的混合系数 
<img src="/images/fxaa3.png">
# 子像素抗锯齿
当栅格化后的物体小于一像素时，就产生了子像素失真。子像素失真最常见于非常细小的物体，例如场景中的塔尖、电话线或电线，甚至是距离屏幕足够远的一把剑。虽然这类失真也可以算是几何失真的一种，但在抗锯齿算法的设计中需要被特殊对待.

这些情况下，平均亮度是在3x3邻域上计算的。在从中减去中心亮度并除以第一步的亮度范围后，就得到了一个子像素偏移。与整个邻域的范围相比，平均值和中心值之间的对比度差越小，则区域越均匀(即没有单个像素点)，偏移量越小。然后对这个偏移量进行细化，我们保留上一步和这一步中较大的偏移量。

```
	//NVIDIA给出的子像素偏移
	// Only used on FXAA Quality.
	// This used to be the FXAA_QUALITY__SUBPIX define.
	// It is here now to allow easier tuning.
	// Choose the amount of sub-pixel aliasing removal.
	// This can effect sharpness.
	//   1.00 - upper limit (softer)
	//   0.75 - default amount of filtering
	//   0.50 - lower limit (sharper, less sub-pixel aliasing removal)
	//   0.25 - almost off
	//   0.00 - completely off
	FxaaFloat fxaaQualitySubpix,
    
FxaaFloat subpixG = subpixF * subpixF;
FxaaFloat subpixH = subpixG * fxaaQualitySubpix;
```

补充个知识点,glsl中的平滑函数为它.
```glsl
float smoothstep(float edge0, float edge1, float x)
{
float t = clamp((x - edge0) / (edge1 - edge0),0.0,1.0);
return t * t * (3.0 -2.0 * t);
}
```

e.g. 当SUBPIXEL_QUALITY = 0.75.上述中求子像素梯度为
```
subpixA = 2*(1+0+0+0)+1+1+0+0);
subpixB = (subpixA * (1.0 / 12.0)) - lumaM = 4/12 = 0.33;
FxaaFloat subpixC = FxaaSat(abs(subpixB) * subpixRcpRange);
FxaaFloat subpixD = ((-2.0) * subpixC) + 3.0;
FxaaFloat subpixE = subpixC * subpixC;
//平滑处理函数
FxaaFloat subpixF = subpixD * subpixE;

FxaaFloat subpixG = subpixF * subpixF;
FxaaFloat subpixH = subpixG * fxaaQualitySubpix =  0.75*((-2*0.333+3.0)*(0.3333)^2)^2 = 0.0503

```
所以此例子不进行子像素抗锯齿