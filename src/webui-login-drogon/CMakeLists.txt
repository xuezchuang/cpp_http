cmake_minimum_required(VERSION 3.10)
project(webui_login_drogon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 找 Drogon （你之前已经 cmake --install 到 ~/drogon/install/... 了）
find_package(Drogon REQUIRED)

# 可执行程序
add_executable(webui_login_drogon
    "main.cpp"
)

# 链接 Drogon 库
target_link_libraries(webui_login_drogon
    PRIVATE Drogon::Drogon
)

# （可选）安装规则：方便 cmake --install
install(TARGETS webui_login_drogon RUNTIME DESTINATION bin)
install(DIRECTORY layuimini/ DESTINATION share/layuimini)
