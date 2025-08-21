cmake_minimum_required(VERSION 3.10)
project(wecom LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Drogon REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(wecom
    main.cpp
)



target_link_libraries(wecom
    PRIVATE
        Drogon::Drogon
        OpenSSL::Crypto     # 提供 SHA1 / EVP 系列
        OpenSSL::SSL		# 如果你后面需要 TLS/HTTPS，再加这一行
)

# ===================== 安装规则 =====================
# 1) 安装可执行文件到 bin
install(TARGETS wecom RUNTIME DESTINATION bin)
