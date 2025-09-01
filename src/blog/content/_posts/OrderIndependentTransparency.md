title: 半透明物体的渲染
author: xuezc
tags:
  - graph
  - opengl
description: 半透明渲染
categories:
  - graph
abbrlink: 2db854d1
date: 2023-05-15 15:53:00
---
# 常规半透明物体渲染
## 渲染顺序
1.首先渲染所有的不透明物体（正常是按材质分组渲染有利于动态batch以及减少状态切换，并在组内按由近到远排序以便减少overdraw）。开启深度写入和深度测试。
2.渲染半透明物体，按由远到近排序，先渲染远处的物体，再渲染近处的。开启深度测试，关闭深度写入。开启混合，设置混合公式和参数。
## 为什么需要先渲染不透明物体
<img src="/images/oit3.png">
A为不透明物体,B为透明玻璃.
如果先画B,后画A.

1.画B时开启了深度写入,A如果在B前面,则覆盖B.A如果在B后面,由于开启着深度测试,A不会绘制.深度测试不会去管alpha通道.
2.画B时关闭了深度写入,开启了颜色混合,则会实体A混合B.
## 为什么透明物体排序
首先透明物体的渲染是要关闭深度写入的,也可以不关...  
1.首先颜色A混合颜色B和颜色B混合验算A结果是不一样的,关闭深度写入时,不确定混合的顺序,结果不明确.  
2.如果开启了深度写入,先画了前面的透明物体,后面的透明物体不会在绘制,也就不会混合颜色了.  
## 结论
首先渲染所有的不透明物体,渲染半透明物体，按由远到近排序，先渲染远处的物体.  
<img src="/images/oit2.png">
问题:透明物体的相交问题.  
A与B相交,无论最后透明物体的深度写入关闭与否都有问题.  
1.开启深度写入的情况,假如先绘制A.绘制B的左半部分,会正常的混合,右半部分由于深度测试,B的右半部分丢弃,颜色只有A的透明颜色.  
2.关闭深度写入的情况,右半部分的情况,也仅仅变成了,B透明物体去混合A透明物体后的颜色,也就是效果相当于把B移动到A的前面,实际情况由于是异步的,A和B的绘制顺序不一定,结果会出现下面的情况.
<img src="/images/oit1.png">
<img src="/images/oit6.png">
还有像玻璃球等自交叉的情况.
要解决透明物体交叉问题,使用oit方法.
<img src="/images/oit7.png">
# Order-Independent Transparency什么是顺序无关渲染
在3D渲染中，物体的渲染是按一定的顺序渲染的，这也就可能导致半透明的物体先于不透明的物体渲染，结果就是可能出现半透明物体后的物体由于深度遮挡而没有渲染出来。对于这种情况通常会先渲染所有的不透明物体再渲染半透明物体或者按深度进行排序来解决。但这样仍然无法解决半透明物体之间的透明效果渲染错误问题，特别是物体之间存在交叉无法通过简单的排序来解决。于是就有一些用专门来解决半透明物体渲染算法，OIT算法即Order Independent Transparency（顺序无关的半透明渲染）。Depth Peeling是众多OIT算法里可以得到精确blending结果的一个，在非游戏的3d应用场景中应该还是很有价值的。  
常规排序是基于图元的排序,oit是基于像素的排序.
# Weighted Blended OIT
<img src="/images/oit4.png">
算颜色和权重.算法来自http://jcgt.org/published/0002/02/09/
具体可见NVIDA的Weighted Blended Order-Independent Transparency的pdf.
<img src="/images/oit5.png">
```
vec4 ShadeFragment(vec3 inUV, float alpha);

in vec3 UV;

uniform float depth_scale;
uniform float alpha;

layout(location=0) out vec4 SumColor;
layout(location=1) out vec4 SumWeight;

void main(void)
{
    vec4 color = ShadeFragment(UV, alpha);

    // Assuming that the projection matrix is a perspective projection
    // gl_FragCoord.w returns the inverse of the oPos.w register from the vertex shader
    float viewDepth = abs(1.0 / gl_FragCoord.w);

    // Tuned to work well with FP16 accumulation buffers and 0.001 < linearDepth < 2.5
    float linearDepth = viewDepth * depth_scale;
    float weight = clamp(0.03 / (1e-5 + pow(linearDepth, 4.0)), 1e-2, 3e3);

    SumColor = vec4(color.rgb * color.a, color.a) * weight;
    SumWeight = vec4(color.a);
}
```
最后和实体bgColor取值
```
in vec2 UV;

uniform sampler2D tex0;
uniform sampler2D tex1;
uniform vec3 bgColor;

out vec4 FragColor;

void main(void)
{
    vec4 sumColor = texture(tex0, UV);
    float transmittance = texture(tex1, UV).r;
    vec3 averageColor = sumColor.rgb / max(sumColor.a, 0.00001);

    FragColor.rgb = averageColor * (1 - transmittance) + bgColor * transmittance;
}
```




# Depth Peeling OIT
Single Depth Peeling 顾名思义，就是通过多次绘制，每次绘制剥离离相机最靠近的一层，像剥洋葱一样层层剥开，按顺序混合就得到了精确的混合结果。既然有Single Depth Peeling，还有一种优化版本就是Dual Depth Peeling，从前后两个方向剥离，不在本次讨论的范围

Depth peeling 的中文意思是深度剥离，本质也类似。先渲染最远的一层深度的像素，然后将它剥离掉，然后再重新渲染半透明物体。  
通过反复渲染场景，在每次传递时，算法使用深度缓冲区仅保留来自下一个最近层的贡献。然后，算法将贡献混合到已经渲染的结果上。  
算法的核心非常简单。事实上，一次半透明物体的渲染可以同时剥离掉两层深度，这种方法需要保存两个深度缓存，被称为 dual depth peeling。
算法比较简单,顺便写下.
先绘制一次,保存好当前的深度depth.
然后剥离,绘制的当前深度小于记录的深度,discard,去混合后面的颜色.
剥离的pass次数可以去查询,当没有记录后,剥离完成.
glGetQueryObjectuiv(query_, GL_QUERY_RESULT_AVAILABLE, &available);
```
uniform vec4 color;
uniform sampler2D depth_tex;

out vec4 FragColor;

void main(void)
{
    // Bit-exact comparison between FP32 z-buffer and fragment depth
    vec2 frontUV = gl_FragCoord.xy / textureSize(depth_tex, 0);
    float frontDepth = texture(depth_tex, frontUV).r;
    if (gl_FragCoord.z <= frontDepth + 0.0000000001)
	{
		//FragColor = color;//vec4(1.0,1.0,0.0,1.0);
        discard;
    }
	// Shade all the fragments behind the z-buffer
	FragColor = vec4(color.rgb * color.a, color.a);
	
}
```