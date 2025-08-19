cmake_minimum_required(VERSION 3.10)
project(netmon-server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Drogon REQUIRED)

add_executable(netmon-server
    main.cpp
)

target_link_libraries(netmon-server
    PRIVATE Drogon::Drogon
)

# ===================== 运行时软链接（构建后） =====================
add_custom_command(TARGET netmon-server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_CURRENT_SOURCE_DIR}/www"
            "$<TARGET_FILE_DIR:netmon-server>/www"
    COMMENT "Symlink www into runtime dir after build"
)

# ===================== 安装规则 =====================
install(DIRECTORY www/ DESTINATION share/www)


# 2) 安装可执行文件到 bin
install(TARGETS netmon-server RUNTIME DESTINATION bin)

# 3) 安装后在 bin 下创建软链接：
install(CODE [[
  file(MAKE_DIRECTORY "${CMAKE_INSTALL_PREFIX}/bin")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "../share/www"
            "${CMAKE_INSTALL_PREFIX}/bin/www")
]])
