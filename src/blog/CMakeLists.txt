cmake_minimum_required(VERSION 3.10)
project(hexo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Drogon REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(hexo
    main.cpp
)

target_compile_definitions(hexo PRIVATE USE_MD4C)

target_link_libraries(hexo
    PRIVATE
        Drogon::Drogon
        OpenSSL::Crypto
        OpenSSL::SSL
        md4c-html
)

# ---------- VS 调试器可视化（可选） ----------
set(NATVIS ${CMAKE_SOURCE_DIR}/jsoncpp.natvis)
if (MSVC)
  target_sources(hexo PRIVATE ${NATVIS})
  set_source_files_properties(${NATVIS}
    PROPERTIES HEADER_FILE_ONLY TRUE VS_TOOL_OVERRIDE "Natvis")
  source_group("Visualizers" FILES ${NATVIS})
endif()

# ---------- 资源与模板 ----------
set(BLOG_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(TEMPLATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/templates")

# 让 VS 在 hexo 目标下显示模板文件（仅影响 IDE 展示）
file(GLOB TEMPLATE_FILES "${TEMPLATE_DIR}/*")
if (TEMPLATE_FILES)
  target_sources(hexo PRIVATE ${TEMPLATE_FILES})
  source_group("templates" FILES ${TEMPLATE_FILES})
endif()

# 运行时常量：程序优先从运行目录的 templates/ 找模板
target_compile_definitions(hexo PRIVATE POST_TEMPLATE_PATH_RUNTIME="templates/post.html.tpl")

# 平台上的“目录链接”命令（Win 用 junction 免管理员，*nix 用 symlink）
if (WIN32)
  set(CMD_LINK_DIR create_junction)
else()
  set(CMD_LINK_DIR create_symlink)
endif()

# ---- 构建后：统一创建软链接 / junction（所有配置都这样做）----
# 先清理再创建，避免重复构建失败
add_custom_command(TARGET hexo POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:hexo>/public"
  COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:hexo>/content"
  COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:hexo>/hexo-admin"
  COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:hexo>/templates"

  COMMAND ${CMAKE_COMMAND} -E ${CMD_LINK_DIR}
          "${BLOG_SRC_DIR}/public"      "$<TARGET_FILE_DIR:hexo>/public"
  COMMAND ${CMAKE_COMMAND} -E ${CMD_LINK_DIR}
          "${BLOG_SRC_DIR}/content"     "$<TARGET_FILE_DIR:hexo>/content"
  COMMAND ${CMAKE_COMMAND} -E ${CMD_LINK_DIR}
          "${BLOG_SRC_DIR}/hexo-admin"  "$<TARGET_FILE_DIR:hexo>/hexo-admin"
  COMMAND ${CMAKE_COMMAND} -E ${CMD_LINK_DIR}
          "${BLOG_SRC_DIR}/templates"   "$<TARGET_FILE_DIR:hexo>/templates"
  VERBATIM
)

# ---------- 安装 ----------
# 可执行安装到 bin/
install(TARGETS hexo RUNTIME DESTINATION bin)

# 资源目录在安装时一律“拷贝”（Release 安装拷贝资源）
install(DIRECTORY
  "${BLOG_SRC_DIR}/public"
  "${BLOG_SRC_DIR}/content"
  "${BLOG_SRC_DIR}/hexo-admin"
  "${BLOG_SRC_DIR}/templates"
  DESTINATION bin
)
# 如需装到 share/hexo：
# install(DIRECTORY ... DESTINATION share/hexo)
