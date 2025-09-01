title: 'blender_overlay_smoothwires '
author: xuezc
abbrlink: f7ad6359
tags:
  - graph
  - blender
categories:
  - blender
description: 选择物体外线
date: 2023-07-07 16:37:00
---
# bookmark
```
1.gpu_select_pick_load_id 选择对象
2.OVERLAY_outline_draw  绘制outline
3.OVERLAY_shader_outline_prepass
```

# shader
```
OVERLAY_shader_outline_prepass(overlay_outline_prepass_mesh)
//可以通过overlay_outline_prepass_mesh名字在overlay_edit_mode_info.hh文件中获取shader的代码
overlay_outline_detect(overlay_outline_detect_frag.glsl) // 绘制outline
```
在glsl中关闭aa
```
  if (!doAntiAliasing) {
    lineOutput = vec4(0.0);
    return;
  }
```
# ui-shader数据改变

<img src="/images/blender_0_0.png">
```
1.PreferencesSystem_use_overlay_smooth_wire_set //ui改变数据
2.OVERLAY_antialiasing_init 
  pd->antialiasing.enabled //对OVERLAY_Data赋值是否开启aa
3.OVERLAY_outline_cache_init
  //将变量传入uniform.
  DRW_shgroup_uniform_bool_copy(grp, "doAntiAliasing", pd->antialiasing.enabled);
4.OVERLAY_outline_draw 绘制outline线
```

# draw
shader的overlay_outline_prepass_mesh绘制选择对象的id
shader的overlay_outline_detect绘制outline的颜色和附近的偏移量
shader的overlay_antialiasing绘制 混合进行aa操作
```
//1.给outlines_prepass_ps和outlines_detect_ps赋值uniform和shader
OVERLAY_outline_cache_init
//2.outlines_prepass_fb和outlines_resolve_fb是上述两个pass的fbo.
//给fbo创建绑定纹理layout
OVERLAY_outline_init
//3.给antialiasing_ps赋值uniform和shader..此处应该是一个pass可以有多个fbo.
OVERLAY_antialiasing_cache_init
//4.overlay_color_only_fb等fbo创建绑定纹理layout
OVERLAY_antialiasing_init
```
## overlay_outline_prepass_mesh
shader:overlay_outline_prepass_vert.glsl
## overlay_outline_detect
shader:overlay_outline_detect_frag.glsl

## overlay_antialiasing
shader:overlay_antialiasing_frag.glsl
采样器绑定:OVERLAY_antialiasing_cache_init













































