# 设置要cmake工具的最低版
cmake_minimum_required (VERSION 3.5.1)

# 设置编译器
set(CMAKE_C_COMPILER "gcc")

# 设置语法标准
set(CMAKE_C_STANDARD 99)

# 设置编译参数
set(CMAKE_C_FLAGS "-Wall -Werror")

# 设置要链接的库
link_libraries(tools)

# 设置安装位置
set(CMAKE_INSTALL_PREFIX ../)

# 设置要编译的目标
add_executable(ftp 
	../main.c 
	../ftp_client.c)

# 设置安装位置
install(TARGETS ftp
	RUNTIME DESTINATION bin)
