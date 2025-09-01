title: Summed Area Tables
author: xuezc
tags:
  - graph
categories:
  - graph
description: Summed Area Table（SAT），也被称为积分图，是一种数据结构，用于高效地计算图像内给定区域的总和
abbrlink: cbcc0681
date: 2024-03-15 10:14:00
---
# Intro

<img src="/images/SAT_1.png">

<img src="/images/SAT_2.png">

[仓库中有论文出处](https://github.com/xuezchuang/Variance-Soft-Shadow-Mapping "查看")

# 基于ComputeShader构建

<img src="/images/SAT_3.png">

<img src="/images/SAT_4.png">

<img src="/images/SAT_5.png">

# glsl Code
```
#version 430 core

precision highp float;
precision highp int;


layout(local_size_x = 1024) in;

shared vec2 shared_data[gl_WorkGroupSize.x * 2];


layout(rg32f, binding = 0) readonly uniform image2D input_image;
layout(rg32f, binding = 1) writeonly uniform image2D output_image;


void main(void)
{
	uint id = gl_LocalInvocationID.x;
	uint rd_id;
	uint wr_id;
	uint mask;
	ivec2 P = ivec2(id * 2, gl_WorkGroupID.x);
	const uint steps = uint(log2(gl_WorkGroupSize.x)) + 1;
	uint step = 0;
	shared_data[id * 2] = imageLoad(input_image, P).rg;
	shared_data[id * 2 + 1] = imageLoad(input_image, P + ivec2(1, 0)).rg;

	barrier();
	memoryBarrierShared();

	for (step = 0; step < steps; step++)
	{
		mask = (1 << step) - 1;
		rd_id = ((id >> step) << (step + 1)) + mask;
		wr_id = rd_id + 1 + (id & mask);
		shared_data[wr_id] += shared_data[rd_id];

		barrier();
		memoryBarrierShared();
	}

	imageStore(output_image, P.yx, vec4(shared_data[id * 2], 0.0, 0.0));
	imageStore(output_image, P.yx + ivec2(0, 1), vec4(shared_data[id * 2 + 1], 0.0, 0.0));
}
```