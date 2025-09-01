title: blender_Editmode下框选节点流程
author: xuezc
tags:
  - graph
  - blender
categories:
  - blender
description: draw pass
abbrlink: 3a637f89
date: 2023-06-29 14:28:00
---
# wm_draw_update框架
绘制mesh的流程
drw_engines_cache_init,drw_engines_cache_populate和drw_engines_draw_scene均会遍历engine.
init和draw都以engine遍历
populate先以object遍历后,后engine遍历.之后drw_batch_cache_generate_requested生成数据.
```
列举出以为重要的函数堆栈
1.wm_draw_update
2.wm_draw_window
3.wm_draw_window_offscreen
4.ED_region_do_draw
5.view3d_main_region_draw
6.view3d_draw_view
7.DRW_draw_view
8.DRW_draw_render_loop_ex
9.drw_engines_cache_init
9.drw_engines_cache_populate
10.drw_engines_draw_scene
```
## Engine loop
全局数据--->每个engine的数据
```c
DRWManager DST = 
{
  DRWData *vmempool;
  struct DRWViewData *view_data_active;
};

DRWViewData
{
  Vector<ViewportEngineData> engines;
  Vector<ViewportEngineData *> enabled_engines;
}

DRW_ENABLED_ENGINE_ITER (DST.view_data_active, engine, data)
{

}
```
上述data为ViewportEngineData* data;
迭代器为DRW_view_data_enabled_engine_iter_step.data的数据即为DRWViewData::enabled_engines数组.
传入每个engine后重新转换为每个engine的data.
### work_bench
### overlay
```c
OVERLAY_Data *data = static_cast<OVERLAY_Data *>(vedata);
OVERLAY_PrivateData *pd = data->stl->pd;
typedef struct OVERLAY_PassList 
{
  ...
  DRWPass *edit_mesh_verts_ps[2];
  ...
}
typedef struct OVERLAY_PrivateData
{
  ...
  DRWShadingGroup *edit_mesh_verts_grp[2];
  ...
}
typedef struct OVERLAY_StorageList 
{
  struct OVERLAY_PrivateData *pd;
} OVERLAY_StorageList;
typedef struct OVERLAY_Data 
{
  void *engine_type;
  OVERLAY_FramebufferList *fbl;
  OVERLAY_TextureList *txl;
  OVERLAY_PassList *psl;
  OVERLAY_StorageList *stl;

  void *instance;
} OVERLAY_Data;
```

# shader
overlay_edit_mesh_vert的创建.
在函数GPU_shader_create_from_info中:
typedef_source将定义的需要的.h头文件包含到shader中,
resources将结构体的名字重新赋予.类似这样:
```c
layout(binding = 7, std140) uniform globalsBlock { GlobalsUboStorage _globalsBlock; };
```
## shader_create的过程
在GPU_shader_create_from_info函数中,编译.
```
    Vector<const char *> sources;
    standard_defines(sources);
    sources.append("#define GPU_VERTEX_SHADER\n");
    if (!info.geometry_source_.is_empty()) {
      sources.append("#define USE_GEOMETRY_SHADER\n");
    }
    sources.append(defines.c_str());
    sources.extend(typedefs);
    sources.append(resources.c_str());
    sources.append(interface.c_str());
    sources.extend(code);
    sources.extend(info.dependencies_generated);
    sources.append(info.vertex_source_generated.c_str());

    shader->vertex_shader_from_glsl(sources);
```
在文件overlay_edit_mode_info.hh中初始化该示例shader
```c
GPU_SHADER_CREATE_INFO(draw_globals)
    .typedef_source("draw_common_shader_shared.h")
    .uniform_buf(7, "GlobalsUboStorage", "globalsBlock", Frequency::PASS);

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_common)
    .define("blender_srgb_to_framebuffer_space(a)", "a")
    .sampler(0, ImageType::DEPTH_2D, "depthTex")
    .fragment_out(0, Type::VEC4, "fragColor")
    .push_constant(Type::BOOL, "selectFaces")
    .push_constant(Type::BOOL, "selectEdges")
    .push_constant(Type::FLOAT, "alpha")
    .push_constant(Type::FLOAT, "retopologyOffset")
    .push_constant(Type::IVEC4, "dataMask")
    .vertex_source("overlay_edit_mesh_vert.glsl")
    .additional_info("draw_modelmat", "draw_globals");
        
GPU_SHADER_CREATE_INFO(overlay_edit_mesh_vert)
    .do_static_compilation(true)
    .builtins(BuiltinBits::POINT_SIZE)
    .define("VERT")
    .vertex_in(0, Type::VEC3, "pos")
    .vertex_in(1, Type::UVEC4, "data")
    .vertex_in(2, Type::VEC3, "vnor")
    .vertex_out(overlay_edit_mesh_vert_iface)
    .fragment_source("overlay_point_varying_color_frag.glsl")
    .additional_info("overlay_edit_mesh_common");
```
## 框选点颜色的shader流程
在框选顶点时,overlay_edit_mesh_vert.glsl中确定最后框选点的颜色.
此处使用的是data的y来确定该点是否被选中.
data数据即为1 slot中的data,在interface中被添加编译.
```
  uvec4 m_data = data & uvec4(dataMask);
  vertexCrease = float(m_data.z >> 4) / 15.0;
  finalColor = EDIT_MESH_vertex_color(m_data.y, vertexCrease);
```
在shader的overlay_edit_mesh_common_lib.glsl中根据选中/非选中状态确定颜色.
```
vec4 EDIT_MESH_vertex_color(uint vertex_flag, float vertex_crease)
{
  if ((vertex_flag & VERT_ACTIVE) != 0u) {
    return vec4(colorEditMeshActive.xyz, 1.0);
  }
  else if ((vertex_flag & VERT_SELECTED) != 0u) {
    return colorVertexSelect;
  }
  else {
    /* Full crease color if not selected nor active. */
    if (vertex_crease > 0.0) {
      return mix(colorVertex, colorEdgeCrease, vertex_crease);
    }
    return colorVertex;
  }
}
```


# drw_draw_pass
```
1.DRW_draw_pass
2.drw_draw_pass_ex
3.draw_shgroup
```
draw_shgroup为绘图命令


# 监视
## 迭代Object
```
DEG_iterator_objects_begin 
```
## 获取meshcache
```
mesh_batch_cache_get
```
## 选择点
在DRW_mesh_batch_cache_create_requested函数中对cache->batch.edit_vertices地址取值.
在函数draw_shgroup中对(DRWShadingGroup)shgroup判断
```
shgroup->cmd.first &&shgroup->cmd.first->commands[0].draw.batch==0x00000204cfd5e030
```
## draw_cmd
DRW_shgroup_call_ex


# drw_engines_cache_init
创建DRWPass*结构体.
## workbench_cache_init

```
typedef struct WORKBENCH_PassList {
  struct DRWPass *opaque_ps;
  struct DRWPass *opaque_infront_ps;
  ...
  }

typedef struct WORKBENCH_Prepass {
  /** Hash storing shading group for each Material or GPUTexture to reduce state changes. */
  struct GHash *material_hash;
  /** First common (non-vertex-color and non-image-colored) shading group to created subgroups. */
  struct DRWShadingGroup *common_shgrp;
  /** First Vertex Color shading group to created subgroups. */
  struct DRWShadingGroup *vcol_shgrp;
  /** First Image shading group to created subgroups. */
  struct DRWShadingGroup *image_shgrp;
  /** First UDIM (tiled image) shading group to created subgroups. */
  struct DRWShadingGroup *image_tiled_shgrp;
} WORKBENCH_Prepass;

typedef struct WORKBENCH_PrivateData {
  WORKBENCH_Prepass prepass[2][2][WORKBENCH_DATATYPE_MAX];
}

```
以workbench中的workbench_opaque_cache_init为例说明
创建opaque_ps和opaque_infront_ps,这些pass生成DRWShadingGroup给到结构体WORKBENCH_PrivateData.


## OVERLAY_cache_init
创建DRWPass-->添加-->DRWShadingGroup    -->添加draw_command
```c
OVERLAY_edit_mesh_cache_init
//创建psl的DRWPass 
DRW_PASS_CREATE(psl->edit_mesh_verts_ps[i], state | pd->clipping_state);
//创建pd的DRWShadingGroup,同时加入到DRWPass的链表中
grp = pd->edit_mesh_verts_grp[i] = DRW_shgroup_create(sh, psl->edit_mesh_verts_ps[i]);
//之后便是对DRWShadingGroup的数据进行uniform赋值.
```
# cache_populate
函数drw_engines_cache_populate
对GPUBatch*和DRWShadingGroup*分配内存,添加command命令,然后把grp(DRWShadingGroup*)赋予到pass中.
同时对DRWShadingGroup通过uniform赋值opengl的值
## workbench_cache_populate
```
列举出以为重要的函数堆栈
cache_populate
workbench_cache_populate
workbench_cache_common_populate
workbench_object_surface_material_get
DRW_cache_mesh_surface_shaded_get
DRW_mesh_batch_cache_get_surface_shaded
```
mesh_batch_cache_request_surface_batches函数对MeshBatchCache中的batch.surface分配GPUBatch*;
然后DRWShadingGroup生成command,添加GPUBatch*
WORKBENCH_Prepass *prepass = &wpd->prepass[transp][infront][datatype];

## OVERLAY_cache_populate
mesh点数据通过函数DRW_mesh_batch_cache_get_edit_vertices添加到绘图命令中.
创建GPUBatch*,并且添加到DRWShadingGroup*中.
```
drw_engines_cache_populate
OVERLAY_cache_populate
OVERLAY_edit_mesh_cache_populate
overlay_edit_mesh_add_ob_to_pass
DRW_mesh_batch_cache_get_edit_vertices
```
DRW_mesh_batch_cache_get_edit_vertices生成object的mesh,并且给到pd->edit_mesh_verts_grp[in_front];绘制命令中.
## drw_batch_cache_generate_requested
在每一个engine完成后,开始检查模型,生成数据.
下面生成task_graph_work,后使用线程填充数据
```
DRW_mesh_batch_cache_create_requested
mesh_buffer_cache_create_requested
extract_edit_data_init
extract_edit_data_iter_poly_bm
```
在函数extract_edit_data_iter_poly_bm中修改每个顶点是否被选中的状态
### EditLoopData数据
EditLoopData可以由GPUVertBuf *vbo来获取,也就是每个vbo可以得到EditLoopData的数据
init函数就是把vbo中的数据提取处理,交给extract_edit_data_iter_poly_bm等函数处理数据.
```
static void extract_edit_data_init(const MeshRenderData *mr,
                                   MeshBatchCache * /*cache*/,
                                   void *buf,
                                   void *tls_data)
{
  GPUVertBuf *vbo = static_cast<GPUVertBuf *>(buf);
  GPUVertFormat *format = get_edit_data_format();
  GPU_vertbuf_init_with_format(vbo, format);
  GPU_vertbuf_data_alloc(vbo, mr->loop_len + mr->loop_loose_len);
  EditLoopData *vbo_data = (EditLoopData *)GPU_vertbuf_get_data(vbo);
  *(EditLoopData **)tls_data = vbo_data;
}
```
编辑数据.有l_iter->v渲染的结果,改变data的状态.
```
  const ExtractorIterData *data = static_cast<ExtractorIterData *>(userdata);
  const MeshRenderData *mr = data->mr;
  const BMFace *f = ((const BMFace **)data->elems)[iter];
  
extract_edit_data_iter_poly_bm
{
  mesh_render_data_vert_flag(mr, l_iter->v, data);
}
```
提取器的数据结构.
extractor表示该提取器的方法类,方法指向每种vbo的实现,buffer即表示vbo数据.
```
struct ExtractorRunData {
  /* Extractor where this run data belongs to. */
  const MeshExtract *extractor;
  /* During iteration the VBO/IBO that is being build. */
  void *buffer = nullptr;
  uint32_t data_offset = 0;
}  
```
在MeshBufferList中包含每种vbo数据.
```
Object *ob
Mesh *me = (Mesh *)ob->data
MeshBatchCache *cache = mesh_batch_cache_get(me);
MeshBufferCache *mbc =  &cache->final;
mesh_buffer_cache_create_requested
{
 MeshBufferCache *mbc,
 MeshBufferList *mbuflist = &mbc->buff;
}
```
## 总结
mr->bm = me->edit_mesh->bm;
本质上就是me来改变vbo的数据.操作改变me,改变显存数据vbo.
### mesh_buffer_cache_create_requested
在mesh_buffer_cache_create_requested中创建了MeshRenderData *mr,有mr来取改变mesh中的vbo的数据.
mr->bm->ftable;
# drw_engines_draw_scene
```
1.drw_engines_draw_scene
2.engine->draw_scene(data);
```
## workbench_draw_scene
```c
3.workbench_draw_scene
```
## OVERLAY_draw_scene
调试选择mesh点的数据绘制
```c
3.OVERLAY_draw_scene
4.OVERLAY_edit_mesh_draw
5.overlay_edit_mesh_draw_components
6.DRW_draw_pass(psl->edit_mesh_verts_ps[in_front]);
```
## OVERLAY_PassList
调试选择mesh点的数据在数据psl->edit_mesh_verts_ps[in_front]中.
```c
struct DRWPass 
{
  /* Linked list */
  struct {
    DRWShadingGroup *first;
    DRWShadingGroup *last;
  } shgroups;
}
OVERLAY_PassList
{
  DRWPass* ;
}
typedef struct OVERLAY_Data
{
  void *engine_type;
  OVERLAY_FramebufferList *fbl;
  OVERLAY_TextureList *txl;
  OVERLAY_PassList *psl;
  OVERLAY_StorageList *stl;

  void *instance;
} OVERLAY_Data;
```