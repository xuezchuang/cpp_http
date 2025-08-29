# CMakeList.txt : CMake project for cpp-http, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.10)

project ("cpp_http")

add_subdirectory(src/webui-login-drogon)
add_subdirectory(src/netmon-server)
add_subdirectory(src/wecom)
add_subdirectory(src/speed_receiver)
add_subdirectory(src/blog)