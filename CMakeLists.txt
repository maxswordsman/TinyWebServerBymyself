cmake_minimum_required(VERSION 3.16)
project(TinyWebServerBymyself)

set(CMAKE_CXX_STANDARD 14)

# 指定可执行文件与CMakeLists.txt位于同一级目录
set(EXECUTABLE_FILE_OUTPUT ../)
set(EXECUTABLE_OUTPUT_PATH ${EXECUTABLE_FILE_OUTPUT})

# 查找JSONCPP库
find_package(jsoncpp REQUIRED)
# 添加JSON库头文件
include_directories(${JSONCPP_INCLUDE_DIRS})

# 手动指定MYSQL路径
include_directories(/usr/include/mysql)
# 指定MySQL库文件的位置
link_directories(/usr/lib/mysql)


add_executable(TinyWebServerBymyself main.cpp ./timer/lst_timer.cpp ./log/log.cpp
        http/http_conn.cpp ./CGImysql/sql_connection_pool.cpp
        ./config.cpp ./webserver.cpp)

# 链接 MySQL 客户端库
target_link_libraries(TinyWebServerBymyself mysqlclient)
# 链接JSONCPP库
target_link_libraries(TinyWebServerBymyself jsoncpp_lib)
# 链接线程库
target_link_libraries(TinyWebServerBymyself pthread)