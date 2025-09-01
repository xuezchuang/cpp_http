title: opengl dsa
author: xuezc
tags:
  - opengl
  - graph
categories:
  - opengl
abbrlink: e83f7fae
description: 'opengl Direct State Access与之前的示例,使用到的api方便搜索'
date: 2023-05-10 08:32:00
---
# opengl api示例
without DSA
## vao&vbo
生成并绑定vao
```
unsigned int VBO, VAO;
glGenVertexArrays(1, &VAO);
glBindVertexArray(VAO);
```
生成并绑定vbo
```
glGenBuffers(1, &VBO);	
glBindBuffer(GL_ARRAY_BUFFER, VBO);
glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
```
设置顶点属性指针,vbo的glBufferData不一定需要绑定vao,但是下面的数据必须绑定vao
```
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
glEnableVertexAttribArray(0);
```
### 详细说下数据的存储
两种情况,数据格式1
```
float quadVertices[] = { // vertex attributes for a quad that fills the entire screen in Normalized Device Coordinates.
    // positions   // texCoords
    -1.0f,  1.0f,  0.0f, 1.0f,
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,

    -1.0f,  1.0f,  0.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f
};
```
设置属性是这样的
```
glEnableVertexAttribArray(0);
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
glEnableVertexAttribArray(1);
glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
```
数据格式2
```
	float quadVertices2[] =
	{ // vertex attributes for a quad that fills the entire screen in Normalized Device Coordinates.
	// positions   
		-0.5f,  0.5f,
		-0.5f,  -0.5f,
		0.5f, -0.5f,
		-0.5f, 0.5f,
		 0.5f, -0.5f,
		 0.5f,  0.5f,
		// // texCoords
		0.0f, 0.0f,
		0.0f, 1.0f,
		1.0f, 1.0f,
		0.0f, 0.0f,
		1.0f, 1.0f,
		1.0f, 0.0f,
	};
```
设置属性是这样的
```
glEnableVertexAttribArray(0);
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
glEnableVertexAttribArray(1);
glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void*)(12 * sizeof(float)));
```

## texture
```
glGenTextures(1, &textureID);
glBindTexture(GL_TEXTURE_2D, textureID);
glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
glGenerateMipmap(GL_TEXTURE_2D);

glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, format == GL_RGBA ? GL_CLAMP_TO_EDGE : GL_REPEAT);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, format == GL_RGBA ? GL_CLAMP_TO_EDGE : GL_REPEAT);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
```
## framebuffer
```
glGenFramebuffers(1, &framebuffer);
glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
// create a color attachment texture
glGenTextures(1, &textureColorbuffer);
glBindTexture(GL_TEXTURE_2D, textureColorbuffer);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureColorbuffer, 0);
// create a renderbuffer object for depth and stencil attachment (we won't be sampling these)
unsigned int rbo;
glGenRenderbuffers(1, &rbo);
glBindRenderbuffer(GL_RENDERBUFFER, rbo);
glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, SCR_WIDTH, SCR_HEIGHT); // use a single renderbuffer object for both a depth AND stencil buffer.
glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo); 
// now actually attach it
// now that we actually created the framebuffer and added all attachments we want to check if it is actually complete now
if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << endl;
```
# opengl Direct State Access
传统的OpenGL API是基于“绑定”（Binding）概念的，即需要通过调用类似glBindBuffer、glBindTexture、glBindFramebuffer等函数将状态绑定到OpenGL上下文中，并且在操作状态前需要确保该状态已经被正确地绑定。而DSA则是通过直接访问OpenGL对象的状态，而不是将状态绑定到上下文中，从而减少了状态切换的开销和降低了代码的复杂度。
## vao&vbo
生成并绑定vao
```
glCreateVertexArrays(1, &vao);
glBindVertexArray(vao);
```
生成并绑定vbo,vbo的数据的绑定合并了之前的glBindBuffer和glBufferData两个函数
```
glCreateBuffers(1, &vbo_);
glNamedBufferData(vbo_, static_cast<GLsizeiptr>(size_in_byte_), init_data, GL_STATIC_DRAW);
```
将顶点属性数组与顶点缓冲对象进行绑定
```
glVertexArrayVertexBuffer(VAO, 0, VBO, 0, 3 * sizeof(float));
```
设置顶点属性指针
```
glVertexArrayAttribFormat(VAO, 0, 3, GL_FLOAT, GL_FALSE, 0);
glVertexArrayAttribBinding(VAO, 0, 0);
glEnableVertexArrayAttrib(VAO, 0);
```
示例1 数据格式
```
	float quadVertices[] = { // vertex attributes for a quad that fills the entire screen in Normalized Device Coordinates.
	// positions   // texCoords
		 -0.5,  0.5,  0.0f, 0.0f,
		 -0.5, -0.5,  0.0f, 1.0f,
		  0.5, -0.5,  1.0f, 1.0f,

		-0.5,  0.5,  0.0f, 0.0f,
		 0.5, -0.5,  1.0f, 1.0f,
		 0.5,  0.5,  1.0f, 0.0f,
	};
```
数据是这样偏移的
```
glCreateVertexArrays(1, &quadVAO);
glCreateBuffers(1, &quadVBO);
glNamedBufferData(quadVBO, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
glVertexArrayVertexBuffer(quadVAO, 0, quadVBO, 0, 4 * sizeof(float));
glVertexArrayAttribFormat(quadVAO, 0, 2, GL_FLOAT, GL_FALSE, 0);
glVertexArrayAttribBinding(quadVAO, 0, 0);
glEnableVertexArrayAttrib(quadVAO, 0);
glVertexArrayAttribFormat(quadVAO, 1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float));
glVertexArrayAttribBinding(quadVAO, 1, 0);
glEnableVertexArrayAttrib(quadVAO, 1);
```
示例2 数据格式
```
	float quadVertices2[] =
	{ // vertex attributes for a quad that fills the entire screen in Normalized Device Coordinates.
	// positions   
		-0.5f,  0.5f,
		-0.5f,  -0.5f,
		0.5f, -0.5f,
		-0.5f, 0.5f,
		 0.5f, -0.5f,
		 0.5f,  0.5f,
		// // texCoords
		0.0f, 0.0f,
		0.0f, 1.0f,
		1.0f, 1.0f,
		0.0f, 0.0f,
		1.0f, 1.0f,
		1.0f, 0.0f,
	};
```
数据是这样偏移的
```
glCreateVertexArrays(1, &quadVAO);
glCreateBuffers(1, &quadVBO);
glNamedBufferData(quadVBO, sizeof(quadVertices2), quadVertices2, GL_STATIC_DRAW);
glVertexArrayVertexBuffer(quadVAO, 0, quadVBO, 0, 2 * sizeof(float));
glVertexArrayAttribFormat(quadVAO, 0, 2, GL_FLOAT, GL_FALSE, 0);
glVertexArrayAttribBinding(quadVAO, 0, 0);
glEnableVertexArrayAttrib(quadVAO, 0);
glVertexArrayAttribFormat(quadVAO, 1, 2, GL_FLOAT, GL_FALSE, 12 * sizeof(float));
glVertexArrayAttribBinding(quadVAO, 1, 0);
glEnableVertexArrayAttrib(quadVAO, 1);
```
## texture
```
glCreateTextures(GL_TEXTURE_2D, 1, &textureID);
glBindTextureUnit(0, textureID);
glTextureStorage2D(textureID, 1, GL_RGBA8, width, height); // 分配纹理内存
glTextureSubImage2D(textureID, 0, 0, 0, width, height, format, GL_UNSIGNED_BYTE, data); // 上传纹理图像数据
glGenerateTextureMipmap(textureID); // 自动生成纹理的多级渐远纹理

glTextureParameteri(textureID, GL_TEXTURE_WRAP_S, format == GL_RGBA ? GL_CLAMP_TO_EDGE : GL_REPEAT); 
glTextureParameteri(textureID, GL_TEXTURE_WRAP_T, format == GL_RGBA ? GL_CLAMP_TO_EDGE : GL_REPEAT);
glTextureParameteri(textureID, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
glTextureParameteri(textureID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
```
## framebuffer
```
glCreateFramebuffers(1, &framebuffer);
// create a color attachment texture
glCreateTextures(GL_TEXTURE_2D,1, &textureColorbuffer);
glGenerateTextureMipmap(textureColorbuffer); // 自动生成纹理的多级渐远纹理
glTextureStorage2D(textureColorbuffer, 1, GL_RGB8, SCR_WIDTH, SCR_HEIGHT); // 分配纹理内存
glTextureParameteri(textureColorbuffer, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTextureParameteri(textureColorbuffer, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
// create a renderbuffer object for depth and stencil attachment (we won't be sampling these)
unsigned int rbo;
glCreateRenderbuffers(1, &rbo);
glNamedRenderbufferStorage(rbo, GL_DEPTH24_STENCIL8, SCR_WIDTH, SCR_HEIGHT);
//Attac color & stencil
glNamedFramebufferTexture(framebuffer, GL_COLOR_ATTACHMENT0, textureColorbuffer, 0);
glNamedFramebufferRenderbuffer(framebuffer, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
//// now that we actually created the framebuffer and added all attachments we want to check if it is actually complete now
if (glCheckNamedFramebufferStatus(framebuffer,GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << endl;
```
# Trivia
纹理的坐标是左上为0,0,右下为1,1


# 内置变量
记录一些内部的变量,比方gl_VertexID在glsl中的序号即为glVertexAttribPointer的序号.所以glVertexAttribPointer函数之前要绑定vao确定数据位置.
```
attribute vec4 gl_Color;              // 顶点颜色
attribute vec4 gl_SecondaryColor;     // 辅助顶点颜色
attribute vec3 gl_Normal;             // 顶点法线
attribute vec4 gl_Vertex;             // 顶点物体空间坐标（未变换）
attribute vec4 gl_MultiTexCoord[0-N]; // 顶点纹理坐标（N = gl_MaxTextureCoords）
attribute float gl_FogCoord;          // 顶点雾坐标
```