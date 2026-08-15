cmake_minimum_required(VERSION 3.10)
project(test_wmf)
add_executable(test_wmf src/main.cpp)
target_compile_options(test_wmf PRIVATE /utf-8 /W4 /std:c++latest)
target_link_libraries(test_wmf PRIVATE
  dxva2 evr mf mfplat mfplay mfreadwrite mfuuid # Windows Media Foundation.
  ws2_32 # Windows Sockets 2.
)
target_include_directories(test_wmf PRIVATE src)
target_precompile_headers(test_wmf PRIVATE src/pch.hpp)