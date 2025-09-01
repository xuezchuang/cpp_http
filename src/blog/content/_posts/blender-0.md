title: blender_0
author: xuezc
abbrlink: d46d1ea5
tags:
  - graph
  - blender
description: 对主要的宏的简单解释
categories:
  - blender
date: 2023-06-29 15:17:00
---
# Iter
## OBJECT
```
DEG_OBJECT_ITER_BEGIN (&deg_iter_settings, ob)
{
	DRW_batch_cache_free_old(ob, ctime);
}
DEG_OBJECT_ITER_END;
```
表数据为deg_iter_settings.depsgraph = depsgraph;
监视使用(blender::deg::Depsgraph *)depsgraph和((blender::deg::Depsgraph *)depsgraph)->id_nodes.begin_,14
ob的结构体为OBject*.数据为id_nodes的表数据Object *object = (Object *)id_node->id_cow;

# 数据库
ui设置but消息
```
1.RNA_property_boolean_set
2.ui_but_value_set
```