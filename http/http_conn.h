#pragma once
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"


class http_conn{
public:
    // 设置读取文件的名称m_real_file大小
    static const int FILENAME_LEN = 200;
    // 设置读缓冲区m_read_buf大小
    static const int READ_BUFFER_SIZE = 2048;
    // 设置读缓冲区m_read_buf大小
    static const int WRITE_BUFFER_SIZE = 1024;
    // HTTP方法名--本项目只使用到GET与POST
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    // 主状态机状态--检查请求报文中元素
    enum CHECK_STATE
    {
        // 请求行
        CHECK_STATE_REQUESTLINE = 0,
        // 请求头
        CHECK_STATE_HEADER,
        // 请求数据
        CHECK_STATE_CONTENT
    };
    // HTTP状态码--请求报文解析的结果
    enum HTTP_CODE
    {
        // 请求不完整，需要继续读取客户数据
        NO_REQUEST,
        // 获得了一个完成的客户请求
        GET_REQUEST,
        // 客户请求语法错误
        BAD_REQUEST,
        // 服务器没有资源
        NO_RESOURCE,
        // 客户对资源没有足够的访问权限
        FORBIDDEN_REQUEST,
        // 文件请求,获取文件成功
        FILE_REQUEST,
        // 服务器内部错误
        INTERNAL_ERROR,
        // 客户端已经关闭连接了
        CLOSED_CONNECTION
    };
    // 从状态机的状态--文本解析是否成功（请求报文的每一行的解析情况: 请求行-单独一行/请求头部-多行组成/）
    enum LINE_STATUS
    {
        // 读取到一个完整的行
        LINE_OK = 0,
        // 行出错
        LINE_BAD,
        // 行数据尚且不完整
        LINE_OPEN
    };

public:
    http_conn(){}
    ~http_conn(){}

public:
    // 初始化套接字--函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, std::string user, std::string passwd, std::string sqlname);
    // 关闭http连接
    void close_conn(bool real_close = true);
    // http处理函数
    void process();
    // 读取浏览器端发来的全部数据
    bool read_once();
    // 响应报文写入函数
    bool write();
    // 获取服务器ip信息
    sockaddr_in *get_address()
    {
        return &m_address;
    }

    /*******************数据库:函数需要补充*****************/
    // 初始化数据库读取线程
    void initmysql_result(connection_pool *connPool);
    /*******************数据库:函数需要补充*****************/

    // 是否关闭连接
    int timer_flag;
    // 是否正在处理数据中
    int improv;

private:
    // 对类私有的成员变量进行初始化
    void init();
    // 从m_read_buf读取，并处理请求报文
    HTTP_CODE process_read();
    // 向m_write_buf写入响应报文数据
    bool process_write(HTTP_CODE ret);
    // 主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char *text);
    // 主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char *text);
    // 主状态机解析报文中的请求内容
    HTTP_CODE parse_content(char *text);
    // 生成响应报文
    HTTP_CODE do_request();
    // 从状态机（从每个部分中--请求行/请求头/请求数据--获取一行）--分析是请求报文的哪一部分
    LINE_STATUS parse_line();
    // 移动到当前处理行的初始位置
    // m_start_line是已经解析的字符
    // get_line用于将指针向后偏移，指向未处理的字符
    char *get_line() { return m_read_buf + m_start_line; };
    // 撤销内存映射
    void unmap();

    // 下面一组函数用于填充HTTP应答
    // 根据响应报文格式，生成对应8个部分，以下函数均由process_write调用填充HTTP应答
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status,const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    // epoll_create创建的epoll树实例
    static int m_epollfd;
    // 当前的连接客户端计数
    static int m_user_count;
    /*******************数据库对象*****************/
    // 数据库对象
    MYSQL *mysql;
    /*******************数据库对象*****************/
    // IO 事件类别: 读事件为0，写事件为1
    int m_state;

private:
    // 该http对象用于通信的套接字
    int m_sockfd;
    // 客户端ip信息
    sockaddr_in m_address;
    // 存储读取的请求报文数据
    char m_read_buf[READ_BUFFER_SIZE];
    // 读缓冲区m_read_buf中数据最后一个字节的下一个位置
    int m_read_idx;
    // m_read_buf当前正在读取的位置m_checked_idx(正在分析的字符在读缓冲区的位置)
    int m_checked_idx;
    // m_read_buf中已经解析的字符个数(当前正在解析的行的起始位置)
    int m_start_line;
    // 存储发出的响应报文数据
    char m_write_buf[WRITE_BUFFER_SIZE];
    // 指示m_write_buf中的长度
    int m_write_idx;
    // 主状态机的状态
    CHECK_STATE m_check_state;
    // 请求方法
    METHOD m_method;

    // 以下为解析请求报文中对应的6个变量
    // m_real_file存储读取文件的名称
    // 存储客户请求文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    char m_real_file[FILENAME_LEN];
    // 请求目标文件的文件名，客户端想要访问的资源
    char *m_url;
    // HTTP协议版本,仅支持HTTP 1.1
    char *m_version;
    // 指定了服务器的域名或IP地址
    char *m_host;
    // 请求报文的请求数据(请求体)的总长度
    int m_content_length;
    // 判断HTTP请求是否保持连接
    bool m_linger;

    // 读取服务器上的文件地址(客户请求的资源文件被mmap（内存映射）到内存中的起始位置)
    char *m_file_address;
    // 客户端请求的资源文件状态(是否存在/是否为目录/是否可读/并获取文件大小等信息)
    struct stat m_file_stat;
    // io向量机制iovec
    struct iovec m_iv[2];
    int m_iv_count;
    // 是否启用的POST
    int cgi;
    // 存储请求头数据
    char *m_string;
    // 剩余发送字节数
    int bytes_to_send;
    // 已发送字节数
    int bytes_have_send;
    // 服务器根目录
    char *doc_root;

    /*******************数据库相关变量*****************/
    // 数据库用户名密码匹配表
    std::map<std::string, std::string> m_users;
    // 触发模式
    int m_TRIGMode;
    // 是否开启日志
    int m_close_log;
    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
    /*******************数据库相关变量*****************/
};
