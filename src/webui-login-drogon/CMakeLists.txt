cmake_minimum_required(VERSION 3.10)
project(webui_login_drogon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Drogon REQUIRED)

add_executable(webui_login_drogon
    main.cpp
)

target_link_libraries(webui_login_drogon
    PRIVATE Drogon::Drogon
)

# ===================== 运行时软链接（构建后） =====================
# 在可执行文件目录下创建: layuimini -> <源码目录>/layuimini
add_custom_command(TARGET webui_login_drogon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_CURRENT_SOURCE_DIR}/layuimini"
            "$<TARGET_FILE_DIR:webui_login_drogon>/layuimini"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_CURRENT_SOURCE_DIR}/web_root"
            "$<TARGET_FILE_DIR:webui_login_drogon>/web_root"
    COMMENT "Symlink layuimini & web_root into runtime dir after build"
)

# ===================== 安装规则 =====================
# 1) 把静态资源装到 share/layuimini
install(DIRECTORY layuimini/ DESTINATION share/layuimini)
install(DIRECTORY web_root/  DESTINATION share/web_root)

# 2) 安装可执行文件到 bin
install(TARGETS webui_login_drogon RUNTIME DESTINATION bin)

# 3) 安装后在 bin 下创建软链接：
# layuimini -> ../share/layuimini
# web_root  -> ../share/web_root
install(CODE [[
  file(MAKE_DIRECTORY "${CMAKE_INSTALL_PREFIX}/bin")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "../share/layuimini"
            "${CMAKE_INSTALL_PREFIX}/bin/layuimini")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E create_symlink
                          "../share/web_root"
                          "${CMAKE_INSTALL_PREFIX}/bin/web_root")
]])
