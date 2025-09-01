title: blender_shader的建立过程
author: xuezc
tags:
  - blender
description: 一个shader的编译过程
categories:
  - blender
abbrlink: 899d4166
date: 2023-07-11 10:34:00
---
# 概述
一个shader通过参数name使用函数GPU_shader_create_from_info_name来获取,编译shader.

# 存储shader
一般通过以下格式,存储shader字符.
```
overlay_outline_info.hh
GPU_SHADER_CREATE_INFO(overlay_outline_prepass)
    .push_constant(Type::BOOL, "isTransform")
    .vertex_out(overlay_outline_prepass_iface)
    /* Using uint because 16bit uint can contain more ids than int. */
    .fragment_out(0, Type::UINT, "out_object_id")
    .fragment_source("overlay_outline_prepass_frag.glsl")
    .additional_info("draw_resource_handle", "draw_globals");
```

# 编译
在GPU_shader_create_from_info_name函数中编译

# overlay_outline_prepass_mesh的创建过程
在OVERLAY_shader_outline_prepass函数中,对sh_data->outline_prepass赋值的过程
## 11
在void ShaderCreateInfo::finalize()中,可以通过additional_infos_看到需要包含的着色器.
此时这里有3个附加的,名称是:draw_mesh,overlay_outline_prepass,draw_object_infos.
对应在overlay_outline_info.hh文件中是如下
```
GPU_SHADER_CREATE_INFO(overlay_outline_prepass_mesh)
    .do_static_compilation(true)
    .vertex_in(0, Type::VEC3, "pos")
    .vertex_source("overlay_outline_prepass_vert.glsl")
    .additional_info("draw_mesh", "overlay_outline_prepass")
    .additional_info("draw_object_infos");
```













