#include "http_conn.h"
#include <iostream>


// 定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

// 线程同步互斥锁
locker m_lock;
// 数据库用户名密码匹配表
std::map<std::string, std::string> users;

// 客户端数量计数
int http_conn::m_user_count = 0;
// epoll_create创建的实例对象
int http_conn::m_epollfd = -1;

/*******************数据库:函数需要补充*****************/
/*
 * @func: 初始化数据库读取数据库服务器中的user表
 *        将user表中已经存在的username以及passwd存入服务器本地的map中
 */
void http_conn::initmysql_result(connection_pool *connPool)
{
    // 先从数据库连接池中获取一个连接
    // 1. 创建一个MYSQL指针对象
    MYSQL *mysql = NULL;
    // 2.通过RAII机制，在其构造函数中，获取一个数据库连接，并且通过RAII机制对其进行管理
    // 2.当RAII对象析构时，自动将取出的数据库连接对象归化至连接池的队列中
    connectionRAII mysqlcon(&mysql,connPool);

    // 在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        // mysql_query 出错return 非0值
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }


    // 查询成功之后，需要将数据库服务器中的结果结保存至，客户端的内存中
    // 从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组(通过这个函数得到结果集中所有列的名字)
    MYSQL_FIELD *fields = mysql_fetch_fields(result);


    // 迭代遍历结果集中的每一行数据
    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        std::string temp1(row[0]);
        std::string temp2(row[1]);
        users[temp1] = temp2;
    }
}
/*******************数据库:函数需要补充*****************/


/*
 * @func:对文件描述符设置为非阻塞
 */
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}


/*
 * @func:向epoll实例中添加需要监听的文件描述符
 *      将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
 */
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    // TRIGMode 为 epoll工作模式选择
    // TRIGMode == 0 缺省为水平工作模式LT
    // TRIGMode == 1 为边沿工作模式 ET
    // EPOLLIN 写事件
    // EPOLLRDHUP 监测对端(客户端)套接字关闭事件
    // EPOLLET 边沿触发工作模式
    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP ;

    if (one_shot)
        // EPOLLONESHOT 件描述符被触发一次事件之后，自动将该文件描述符的监测事件从内核维护的事件表中进行删除
        // 确保某个连接在一个线程处理时不会被其他线程处理(防止同一个通信被不同的线程处理)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}


/*
 * @func:从内核事件表中删除文件描述符--从epoll树中移除文件描述符
 */
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}


/*
 * @func:修改文件描述符事件，将事件重置为EPOLLONESHOT,以确保下一次可读时，EPOLLIN事件能被触发
 */
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    // ET模式
    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        // LT模式
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}


/*
 * @func:关闭一个客户端http连接
 */
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        // printf("close %d\n", m_sockfd);
        // 将该套接字文件描述符从epoll实例中移除
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        // 客户端连接数量 -1
        m_user_count--;
    }
}


/*
 * @func:初始化http连接,外部调用初始化套接字地址
 */
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, std::string user, std::string passwd, std::string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;
    // 将该http连接用于通信的套接字文件描述符，添加到epoll实例上
    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    // 客户端连接数量+1
    m_user_count++;

    // 当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

/*
 * @func:初始化新接受的连接
 *      m_check_state默认为分析请求行状态(请求报文的第一行为请求行)
 */
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    // 初始化清空缓冲区
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}


/*
 * @func:从状态机，用于(在请求报文中)分析出一行内容,并且将一行的末尾\r\n变为\0\0
 *      解析一行，判断依据，每一行均以\r\n结束 空行则是仅仅是字符\r\n
 * @return:
 *      LINE_OK   读取到一个完整的行
 *      LINE_BAD  行出错
 *      LINE_OPEN 行数据尚且不完整
 */
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    // m_read_idx 为读缓冲区m_read_buf的数据字节数量（指向缓冲区m_read_buf的数据末尾的下一个字节索引）
    for(;m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        // m_checked_idx为当前分析的字符位置
        temp = m_read_buf[m_checked_idx];

        // 如果当前是\r字符，则有可能会读取到完整行
        if(temp == '\r')
        {
            if((m_checked_idx +1) == m_read_idx)
            {
                // 下一个字符达到了buffer结尾，则接收不完整，需要继续接收
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_idx+1] == '\n')
            {
                // 下一个字符是\n，将\r\n改为\0\0
                // m_checked_idx++ (后增)执行完这句代码之后，m_checked_idx才会自增
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                // 此时的m_check指向的 下一行开始的索引位置
                // 完整的接收一行
                return LINE_OK;
            }
            // 否则行出错
            return LINE_BAD;
        }

        // 如果当前读取的字符是\n，也有可能读取到完整行
        // 一般是上次读取到\r就到buffer末尾了，没有接收完整，再次接收时会出现这种情况
        else if(temp == '\n')
        {
            // 判断前一个字符是否是\r,若是则接收到完整行
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 否则行出错
            return LINE_BAD;
        }
    }
    // 并没有找到\r\n，行数据不完整，需要继续接收
    return LINE_OPEN;
}


/*
 * @func:循环读取客户数据，直到无数据可读或对方关闭连接
 *      从该http连接的通信套接字读缓冲区读取数据到程序的读缓冲区m_read_buf中
 *      也是用于更新m_read_idx
 * @note:非阻塞ET工作模式下，需要一次性将数据读完
 * @return:返回true成功
 *        返回false失败
 */
bool http_conn::read_once()
{
    // m_read_buf满
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

    // LT(水平工作模式)读取数据
    // LT模式下套接字读缓冲区有数据，就会一直有事件触发
    if(0 == m_TRIGMode)
    {
        // recv函数
        // <0 出错 =0 关闭连接 >0 接收到的数据字节数量
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        // 记录m_read_buf读了多少数据了
        m_read_idx += bytes_read;

        if(bytes_read <= 0)
        {
            // 出错
            return false;
        }
        return true;
    }

    // ET(边沿工作模式)读取数据，非阻塞的读，需要一次性将该http对象的通信套接字读缓冲区数据读取完
    else
    {
        while (true)
        {
            // 从通信套接字的读缓冲区，读取数据到m_read_buf
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1)
            {
                // 非阻塞ET模式下，需要一次性将数据读完，无错
                // 套接字属性为非阻塞情况下，判断errno == EAGAIN，表示套接字读缓冲区为空
                // 即套接字读缓冲区的数据全部读取完毕
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                // 读取出错
                return false;
            }
            else if (bytes_read == 0)
            {
                // 连接的客户端断开连接
                return false;
            }
            // 记录读了多少数据到m_read_buf中
            m_read_idx += bytes_read;
        }
        return true;
    }
}


/*
 * @func:解析http请求行(主状态机的初始状态)，获得请求方法，目标url及http版本号
 *      解析成功，主状态机状态转移至 请求头
 *      text是指向m_read_buf中每一行数据的起始位置，此时的m_read_buf已经被处理了，\r\n均被处理为\0
 *      因此char *text就可以指向从该行起始位置至\0索引处截至的数据
 * @note:在HTTP报文中，请求行用来说明请求类型
 *      请求行组成: 请求方法 空格 URL(请求的资源) 空格 HTTP协议版本
 *      GET /index HTTP/1.1 ---> 此处的URL 若无 所以为 /
 *      要访问的资源以及所使用的HTTP版本，其中各个部分之间通过\t或空格分隔。
 *      请求行中最先含有空格和\t任一字符的位置并返回
 */
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // strpbrk 判断 空格 与 \t 哪一个在text最先出现,成功找到返回指向该字符的指针
    // 此处考虑制表符号 \t 可能是考虑服务器的容错能力
    m_url = strpbrk(text," \t");
    // 如果没有空格或\t，则报文格式有误
    if (!m_url)
    {
        return BAD_REQUEST;
    }

    // 将该位置改为\0，用于将前面数据取出
    // GET\0/index.html HTTP/1.1
    // 置位空字符，字符串结束符---向将m_url索引处字符置\0,然后再后移
    *m_url++ = '\0';

    // 取出数据，并通过与GET和POST比较，以确定请求方式
    char *method = text;
    // strcasecmp函数比较两个字符是否相等，会忽略大小写的差异
    if (strcasecmp(method, "GET") == 0)
    {
        // 此次请求为GET方法
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        // 此次请求为POST方法
        m_method = POST;
        // 当请求方法为POST方法，将cgi设置为1
        // 这种情况，请求报文会将用户名与密码放到请求体中
        // 而服务器初始化时，会读取数据库服务器中的user表，并且将user表中已经存在的username与passwd
        // 存储到webServer服务器(相较于数据库服务器就是客户端)本地map结构中，方便查询
        // 用于后续的用户注册与登陆
        cgi = 1;
    }
    else
    {
        // 请求方法不正确，则请求报文的请求语法有错误
        return BAD_REQUEST;
    }

    // 最好的情况:
    // /index.html HTTP/1.1
    // 以上面的实例: 此时的m_url指向 /index.html 的 / 位置
    // 其他情况: 如 GET\0    /index.html HTTP/1.1  请求方法与URL之间多了很多空格
    // m_url此时跳过了第一个空格或\t字符，但不知道之后是否还有
    // 将m_url向后偏移，通过查找
    // 继续跳过空格和\t字符，指向请求资源的第一个字符
    // strspn: 返回 str1 中连续包含 str2 中字符的最长前缀的长度
    m_url += strspn(m_url, " \t");
    // 找到URL 与 HTTP协议版本之间的 空格或者制表符 \t
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
    {
        // 如果没有空格或\t，则报文格式有误
        return BAD_REQUEST;
    }
    // /index.html\0HTTP/1.1
    // 置位空字符，字符串结束符
    *m_version++ = '\0';

    // 此时m_version指向 /index.html\0 后位置的字符，但是这个字符可能是空格或者\t,因此需要继续跳过
    m_version += strspn(m_version, " \t");
    //仅支持HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    // 对请求资源前7个字符进行判断
    // 可能的URL会出现如下情况: http://192.168.110.129:10000/index.html
    // 这里主要是有些报文的请求资源中会带有http://
    // 这里需要对这种情况进行单独处理
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        // 使用 strchr 查找第一个斜杠 /，这通常是URL路径的开始
        m_url = strchr(m_url, '/');
    }

    //同样增加https情况
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        // 使用 strchr 查找第一个斜杠 /，这通常是URL路径的开始
        m_url = strchr(m_url, '/');
    }

    // 一般的不会带有上述两种符号，直接是单独的/或/后面带访问资源
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;

    // 当url为/时，显示欢迎界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");

    //请求行处理完毕，将主状态机转移处理请求头
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}


/*
 * @func:解析http请求的一个头部信息
 *      在报文中，请求头和空行的处理使用的同一个函数
 *      通过判断当前的text首位是不是\0字符，若是，则表示当前处理的是空行，若不是，则表示当前处理的是请求头
 * @note:请求头可以有多行数据，而text每次只指向请求报文的一行数据
 */
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    // 判断是空行还是请求头
    // 解析空行
    if(text[0] == '\0')
    {
        // 判断是GET还是POST请求
        // GET请求，参数通过 URL 传递，无请求体数据
        // POST请求，参数放到请求体中
        // 判断是否有请求体数据，即m_content_length != 0
        if(m_content_length != 0)
        {
            // 主状态机状态转移到 请求体 CHECK_STATE_CONTENT
            m_check_state = CHECK_STATE_CONTENT;
            // 因为该请求报文，还有请求体数据，因此请求数据不完整，继续获取
            return NO_REQUEST;
        }
        // 该请求报文，无请求体数据，则得到一个完整的GET请求
        return GET_REQUEST;
    }

    // 解析请求头
    // 解析请求头部connection字段
    else if(strncasecmp(text, "Connection:", 11) == 0)
    {
        // 比较text中的前11个字符是否是Connection，忽略大小写
        text += 11;

        // 跳过空格和\t字符
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            // 如果是长连接，则将linger标志设置为true
            m_linger = true;
        }
    }
    // 解析请求头部Content-length字段
    else if(strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        // 跳过空格和\t字符
        text += strspn(text, " \t");
        // 获得请求体数据长度
        m_content_length = atol(text);
    }
    // 解析请求头部Host字段
    else if(strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        // 跳过空格和\t字符
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    // 请求不完整，继续获取请求数据
    return NO_REQUEST;
}


/*
 * @func:仅用于解析POST请求报文中的请求体
 *      用于保存post请求消息体，为后面的登录和注册做准备
 * @note:该项目将用于登陆与注册的用户名封装在POST请求报文的请求体中
 */
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        // 将请求体末尾字符的下一个位置标识为\0
        // 方便通过text直接获取请求体内容
        text[m_content_length] = '\0';
        // 获取请求体内容
        // POST请求中最后为输入的用户名和密码
        m_string = text;
        // 获得一个完整的客户请求，此时为POST请求
        return GET_REQUEST;
    }
    // 客户端请求体内容还没有完全得到，需要继续获取请求数据
    return NO_REQUEST;
}


/*
 * @func:（主状态机）有限状态机处理请求报文
 *      根据当前主状态机的状态对报文进行处理
 * @note:GET和POST请求报文的区别之一是有无消息体部分
 */
http_conn::HTTP_CODE http_conn::process_read()
{
    // 初始化从状态机状态、HTTP请求解析结果
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    // 两种情况均可以进入while
    // 1. 从状态机成功获取请求报文的一行 请求行/请求头部/空行/ 均以/r/n 结束
    // 2. 从状态机之前的请求行、请求头部、空行均成功获取，此时已经开始获取请求体信息，即:
    // m_check_state == CHECK_STATE_CONTENT 当前主状态机状态为请求体(请求数据)
    // parse_line为从状态机的具体实现
    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
          || ((line_status = parse_line()) == LINE_OK))
    {
        // 解析到了一行完整数据 或者 解析到了请求体，并且之前的解析没问题，即可进入while循环

        // 移动到当前处理行的初始位置
        // m_start_line是行在buffer中的起始位置，将该位置后面的数据赋给text
        // 此时从状态机已提前将一行的末尾字符\r\n变为\0\0，所以text可以直接取出完整的行进行解析
        text = get_line();

        // m_start_line是每一个数据行在m_read_buf中的起始位置
        // 进入循环表示parse_line==LINE_OK
        // 在parse_line()函数中，m_checked_idx成功移动到m_read_buf中下一次处理数据行的初始索引位置
        // 因此通过m_checked_idx可以对m_read_buf中数据下一次需要处理数据行的行首位置更新
        m_start_line = m_checked_idx;

        /***************************************日志**************************************/
        // printf("Got 1 http line: %s\n",text);
        // LOG_INFO("%s", text);
        /***************************************日志**************************************/

        // 主状态机三种状态转移逻辑，主状态机状态(请求行or请求头or请求体)
        switch (m_check_state)
        {
            // 请求行
            case CHECK_STATE_REQUESTLINE:
            {
                // 解析HTTP请求行
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST)
                {
                    // 客户端请求报文语法错误，返回
                    return BAD_REQUEST;
                }
                break;
            }
            // 请求头
            case CHECK_STATE_HEADER:
            {
                // 解析HTTP请求头
                ret = parse_headers(text);
                if(ret == BAD_REQUEST)
                {
                    // 客户端请求报文语法错误，返回
                    return BAD_REQUEST;
                }
                // 完整解析GET请求后，跳转到报文响应函数
                else if(ret == GET_REQUEST)
                {
                    // 表示获得了一个完整的客户请求
                    // 因为可能存在请求报文，没有请求体
                    // 因此当parse_headers返回GET_REQUEST，则表示当前请求报文无请求体
                    // 即 获得了一个完整的客户请求
                    // 解析具体的请求信息
                    // 调用do_request完成请求资源映射
                    return do_request();
                }
                break;
            }
            // 请求体
            case CHECK_STATE_CONTENT:
            {
                // 解析HTTP请求体
                ret = parse_content(text);
                // 完整解析客户的POST请求，跳转到报文响应函数
                if(ret == GET_REQUEST)
                {
                    // 解析具体请求信息
                    // 获取url 等请求资源
                    return do_request();
                }
                // 解析完消息体(请求体)即完成报文解析，避免再次进入循环，更新line_status
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                // 服务器内部错误
                return INTERNAL_ERROR;
            }
        }
    }
    // 请求不完整，需要继续获取客户端数据
    return NO_REQUEST;
}


/*
 * @func:功能逻辑单元
 *       当得到一个完整、正确的HTTP请求时，就需要分析目标文件的属性
 *       如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
 *       映射到内存地址m_file_address处，并告诉调用者获取文件成功
 *       内存映射，将文件内容直接映射到进程地址空间
 *       这样服务端进程可以像访问普通内存一样访问数据文件
 * @note:doc_root网站根目录，文件夹内存放请求的资源和跳转的html文件
 */
http_conn::HTTP_CODE http_conn::do_request()
{
    // 网站目录（资源目录）
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);

    // strrchr 函数查询 m_url中最后一次出现 / 位置的指针
    // 找到m_url中/的位置
    const char *p = strrchr(m_url, '/');

    // 处理cgi
    // 实现登录和注册校验
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        // 根据标志判断是登录检测还是注册检测
        char flag = m_url[1];  // 等价于*(p+1)

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //eg:user=123&password=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        // 是初次注册情况
        if(*(p+1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            //构建一个用于插入数据到数据库的user表中的 SQL 语句
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            // find函数会返回一个迭代器，在map中找name键
            // 若找到了，迭代器会指向该键对应的键值对
            // 如果没有找到，迭代器将等于 users.end()，即指向 map 结尾的迭代器
            if (users.find(name) == users.end())
            {
                // 没有找到，则注册的为新用户
                m_lock.lock();
                // mysql_query 向数据库执行指令 sql_insert，成功return 0
                int res = mysql_query(mysql, sql_insert);
                // 将注册的新用户，账号密码也添加进本地的map中
                users.insert(std::pair<std::string, std::string>(name, password));
                m_lock.unlock();

                if (!res)
                    // 成功
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                // 注册失败，用户存在
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                // m_url指向登陆失败的页面
                strcpy(m_url, "/logError.html");
        }
    }

    //如果请求资源为/0，表示跳转注册界面
    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);

        // 将网站目录和/register.html进行拼接，更新到m_real_file中
        strcpy(m_url_real, "/register.html");
        // m_url_real字符串复制到m_real_file字符串的len索引位置，进行拼接
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }

    //如果请求资源为/1，表示跳转登录界面
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);

        // 将网站目录和/log.html进行拼接，更新到m_real_file中
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }

    //如果请求资源为/5，表示跳转pic
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);

        // 将网站目录和/picture.html进行拼接，更新到m_real_file中
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }

    //如果请求资源为/6，表示跳转video
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);

        // 将网站目录和/video.html进行拼接，更新到m_real_file中
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }

    //如果请求资源为/7，表示跳转weixin
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);

        // 将网站目录和/fans.html进行拼接，更新到m_real_file中
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else
        // 在parse_request_line处理请求行时，当url只有/则将m_url与judge.html界面进行拼接
        // 如果以上均不符合，即不是登录和注册，直接将url与网站目录拼接
        // 这里的情况是跳转到judge.html界面即欢迎界面，请求服务器上的一个图片
        // 是一个GET请求
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // 通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体
    // 失败返回NO_RESOURCE状态，表示资源不存在
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;

    // 判断文件的权限，是否可读，不可读则返回FORBIDDEN_REQUEST状态
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    // 判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    // 以只读方式获取文件描述符，通过mmap将该文件映射到内存中
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    //避免文件描述符的浪费和占用
    close(fd);

    //表示请求文件存在，且可以访问
    return FILE_REQUEST;
}


/*
 * @func:取消内存映射
 */
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}


/*
 * @func:将响应报文写入到通信套接字的写缓冲区，发送给浏览器(客户)端
 * @note:proActor模式下，是主线程进行I/O操作数据完成，将m_read_buf/m_write_buf数据准备好
 */
bool http_conn::write()
{
    int temp = 0;
    // 表示响应报文为空，一般不会出现这种情况
    if (bytes_to_send == 0)
    {
        // 重新为套接字注册EPOLLONESHOT事件，并且监听读事件
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1)
    {
        // 将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        // 从process_write函数中指定的iovec向量缓冲区，写入数据到M_socket文件描述符的写缓冲区
        // 发送数据给浏览器端
        temp = writev(m_sockfd, m_iv, m_iv_count);
        // 发送数据失败
        if(temp < 0)
        {
            // 非阻塞模式下，判断errno == EAGAIN 即写缓冲区满
            if(errno == EAGAIN)
            {
                // m_sockfd重新注册EPOLLONESHOT事件，监听写事件
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            // 取消do_request函数中开启的内存映射
            unmap();
            // return false,之后关闭连接
            return false;
        }

        // 更新已经发送的字节数量
        bytes_have_send += temp;
        // 更新还未发送字节
        bytes_to_send -= temp;

        // iovec向量缓冲区第一个头部信息的数据已发送完，发送iovec第二个数据
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            // 不再继续发送头部信息
            m_iv[0].iov_len = 0;
            // iovec向量缓冲区 第二个指向URL访问资源文件的指针索引进行偏移
            // iovec向量缓冲区 第一个指针指向的是m_write_buf，此时m_write_buf数据已经发送完毕
            // 因此用 整体已经发送的字节数量bytes_have_send - m_write_buf中的字节数量可以得到 第二个指向URL访问资源文件的指针索引进行偏移
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            // 缓冲区剩余未发送数据的大小
            m_iv[1].iov_len = bytes_to_send;
        }
        // 继续发送iovec向量缓冲区第一个头部信息的数据
        else
        {
            // iovec向量缓冲区 第一个指向m_write_buf的指针索引进行偏移
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            // 缓冲区剩余未发送数据的大小
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        // 判断条件，数据已全部发送完
        if (bytes_to_send <= 0)
        {
            // 取消内存映射
            unmap();
            // 重新注册写事件
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            // 浏览器的请求为长连接
            if (m_linger)
            {
                // 重新初始化HTTP对象
                init();
                return true;
            }
            else
            {
                // return false,之后关闭连接
                return false;
            }
        }
    }
}


/*
 *  @func:添加响应报文的公共函数
 *       --为响应报文按照format格式添加一行数据，写入到m_write_buf中，并且更新m_write_idx
 */
bool http_conn::add_response(const char *format, ...)
{
    //如果写入内容超出m_write_buf大小则报错
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    // 定义可变参数列表
    va_list arg_list;
    // 将变量arg_list初始化为传入参数(初始化 va_list 变量，使其指向变长参数列表的第一个参数)
    va_start(arg_list, format);
    // 将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len=vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);
    //如果写入的数据长度超过缓冲区剩余空间，则报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        // va_end结束 va_list 的使用
        va_end(arg_list);
        return false;
    }
    //更新m_write_idx位置
    m_write_idx += len;
    // 清空可变参列表
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}


/*
 * @func:为响应报文添加状态行
 *      --响应报文的第一部分(仅一行)
 */
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}


/*
 * @func:为响应报文添加消息报头--具体的添加文本长度、连接状态和空行
 *      --消息报头为响应报文第二部分(可以由多行组成)
 *      --空行为响应报文第三部分(仅一行)
 */
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) &&
           add_linger() &&
           add_blank_line();
}


/*
 * @func:为响应报文添加消息报头(第二部分)
 *      --添加Content-Length,表示响应报文的长度
 */
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}

/*
 * @func:为响应报文添加消息报头(第二部分)
 *       --添加文本类型，这里是html
 */
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}


/*
 * @func:为响应报文添加消息报头(第二部分)
 *      --添加连接状态，通知浏览器端是保持连接还是关闭
 */
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}


/*
 * @func:为响应报文添加消息报头(第二部分)
 *      --添加空行
 */
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}


/*
 * @func:为响应报文添加响应正文(第四部分)
 *      --添加响应正文文本
 */
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}


/*
 * @func:生成响应报文
 *      （根据process函数中调用process_read解析请求报文，返回的HTTP_CODE状态码,
 *      传入process_write函数判断进行对应响应报文的生成）
 *      服务器子线程调用process_write向m_write_buf中写入响应报文
 * note:响应报文组成-状态行-消息报头-空行-响应正文
 */
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
        // 服务器内部错误
        case INTERNAL_ERROR:
        {
            // 状态行--500 Internal Server Error:服务器在执行请求时出现错误
            add_status_line(500, error_500_title);
            // 消息报头
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
                return false;
            break;
        }

        // 报文语法错误，404
        case BAD_REQUEST:
        {
            // 状态行--400 Bad Request:请求报文存在语法错误
            add_status_line(404, error_404_title);
            // 消息报头
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
                return false;
            break;
        }

        // 资源没有访问权限，403
        case FORBIDDEN_REQUEST:
        {
            // 状态行--403 Forbidden：请求被服务器拒绝
            add_status_line(403, error_403_title);
            // 消息报头
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
                return false;
            break;
        }

        // 文件存在且可以访问，200
        case FILE_REQUEST:
        {
            // 状态行--200 OK:客户端请求被正常处理
            add_status_line(200, ok_200_title);
            // GET请求，请求访问的文件有内容
            if (m_file_stat.st_size != 0)
            {
                // 消息报头
                add_headers(m_file_stat.st_size);

                // 将向量缓冲区分别指向 m_write_buf与m_file_address
                // 之后可以通过writev函数将两个缓冲区的数据写入到用于通信的套接字写缓冲区，返回给客户端
                // 第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                // 第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                // 发送的全部数据为响应报文头部信息和文件大小
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            break;
        }

        default:
            return false;
    }
    // 除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}


/*
 * @func:由线程池的工作线程调用，对任务进行处理，这是处理HTTP请求入口函数
 *      process_read()/process_write()分别处理m_read_buf/m_write_buf中的数据进行报文的解析/响应
 */
void http_conn::process()
{
    // 解析HTTP请求报文
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST)
    {
        // NO_REQUEST请求报文，不完整，需要继续获取客户端数据
        // 修改该通信套接字事件，重新注册EPOLLONESHOT事件，继续监听该通信套接字读事件
        modfd(m_epollfd,m_sockfd,EPOLLIN,m_TRIGMode);
        // 跳转主线程继续监测读事件
        return;
    }

    // 当read_ret返回为其他情况，则会调用process_write 完成报文响应
        // NO_RESOURCE  请求资源不存在
        // BAD_REQUEST  HTTP请求报文有语法错误或请求资源为目录
        // FORBIDDEN_REQUEST  请求资源禁止访问，没有读取权限
        // FILE_REQUEST  请求资源可以正常访问
        // INTERNAL_ERROR 服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
    // 调用process_write()生成响应报文
    bool write_ret = process_write(read_ret);
    if(!write_ret)
    {
        // 生成响应报文失败，关闭套接字连接
        close_conn();
    }
    // 写成功了，为该套接字重新注册EPOLLONESHOT事件，监听写事件
    // 之后在proactor模式下，服务器主线程检测写事件，并调用http_conn::write函数将响应报文发送给浏览器端
    modfd( m_epollfd, m_sockfd, EPOLLOUT,m_TRIGMode);
}