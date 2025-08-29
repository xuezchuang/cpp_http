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

# 假设你的可执行/库目标名为 hexo（自行替换）
set(NATVIS ${CMAKE_SOURCE_DIR}/jsoncpp.natvis)

if (MSVC)  # 只有在 Windows 的 VS 前端才需要
  target_sources(hexo PRIVATE ${NATVIS})
  # 让 VS 以“Natvis”对待它，并且不参与编译
  set_source_files_properties(${NATVIS}
    PROPERTIES
      HEADER_FILE_ONLY TRUE
      VS_TOOL_OVERRIDE "Natvis")
  # （可选）把它归到“Visualizers”分组，方便在 VS 里看到
  source_group("Visualizers" FILES ${NATVIS})
endif()


# ===================== 安装规则 =====================
install(TARGETS hexo RUNTIME DESTINATION bin)

# 源目录（与本 CMakeLists 同级）——生成期展开为绝对路径
set(BLOG_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}")

# 注意：用普通引号而不是 [[...]]，让 ${BLOG_SRC_DIR} 在生成期展开
install(CODE "
  set(bin_dir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin\")
  file(MAKE_DIRECTORY \"\${bin_dir}\")
  foreach(name hexo-admin content public)
    file(REMOVE_RECURSE \"\${bin_dir}/\${name}\")
    execute_process(
      COMMAND \"\${CMAKE_COMMAND}\" -E create_symlink
              \"${BLOG_SRC_DIR}/\${name}\"
              \"\${bin_dir}/\${name}\"
    )
  endforeach()
")

# ===================== Debug 调试用软链接 =====================
# 在可执行文件同级目录创建 3 个软链，确保调试时 ./public 可用
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_custom_command(TARGET hexo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${BLOG_SRC_DIR}/public" "$<TARGET_FILE_DIR:hexo>/public"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${BLOG_SRC_DIR}/content" "$<TARGET_FILE_DIR:hexo>/content"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${BLOG_SRC_DIR}/hexo-admin" "$<TARGET_FILE_DIR:hexo>/hexo-admin"
    VERBATIM
  )
endif()
