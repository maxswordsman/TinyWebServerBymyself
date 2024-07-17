#include "webserver.h"
#include <iostream>

/*
 * @func: 服务器初始化：http连接、设置资源根目录、创建定时器连接资源数组
 */
WebServer::WebServer()
{
    // http_conn连接对象数组，MAX_FD为最大的连接数量
    users = new http_conn[MAX_FD];

    // root资源文件根目录
    char server_path[200];
    // getcwd获取当前工作目录的绝对路径
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root  = (char *) malloc(strlen(server_path)+ strlen(root)+1);
    strcpy(m_root, server_path);
    strcat(m_root, root);
    // 此时m_root指向资源文件根目录

    // 定时器连接资源数组
    users_timer = new client_data[MAX_FD];
}


/*
 * @func: 服务器资源释放
 */
WebServer::~WebServer()
{
    // 关闭epoll实例
    close(m_epollfd);
    // 关闭用于监听的套接字
    close(m_listenfd);
    // 关闭管道套接字
    close(m_pipefd[0]);
    close(m_pipefd[1]);
    // 释放http连接对象数组资源
    delete[] users;
    // 释放定时器连接资源数组
    delete[] users_timer;
    // 释放线程池对象
    delete m_pool;
}


/*
 * @func: 初始化用户名、数据库等信息
 * @param: port 服务器端口
 * @param: user 数据库用户名
 * @param: passWord 数据库密码
 * @param: databaseName 数据库中数据库名称
 * @param: log_write 日志写入方式（异步或者同步）
 * @param: opt_linger 是否优雅下线
 * @param: trigmode epoll的工作模式（套接字的事件触发模式）
 * @param: sql_num 数据库连接池中的数据库连接数量
 * @param: thread_num 线程池中工作线程数量
 * @param: close_log 是否关闭日志
 * @param: actor_mode 事件处理模式
 * @param: db_port 数据库服务器端口，默认为3306
 */
void WebServer::init(int port, std::string user, std::string passWord,
                     std::string databaseName,int log_write,int opt_linger, int trigmode,
                     int sql_num, int thread_num, int close_log, int actor_model,int db_port)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
    m_db_port = db_port;
}


/*
 * @func: 设置epoll的工作模式（套接字事件触发模式）
 *        通过变量m_TRIMode的取值判断，设置用于监听套接字与用于通信套接字的事件触发模式
 */
void WebServer::trig_mode()
{
    // LT + LT (监听套接字/通信套接字均为水平工作模式)
    if(0 == m_TRIGMode)
    {
        // 0表示水平工作模式
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    // LT + ET
    else if(1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        // 1表示边沿工作模式
        m_CONNTrigmode = 1;
    }
    // ET + LT
    else if(2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    else if(3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}


/*
 * @func: 初始化日志系统
 */
void WebServer::log_write()
{
    // 不关闭日志
    if(0 == m_close_log)
    {
        // 日志类型：异步日志
        if(1 == m_log_write)
        {
            Log::get_instance()->init("./ServerLog", m_close_log, 200, 800000, 800);
        }
        // 日志类型：同步日志
        else
        {
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
        }
    }
}


/*
 * @func: 初始化数据库连接池
 */
void WebServer::sql_pool()
{
    // 静态接口函数，获取单例模式的唯一实例对象
    m_connPool = connection_pool::GetInstance();
    // 初始化数据库连接池，数据库服务器默认端口为3306
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, m_db_port, m_sql_num, m_close_log);
    // 初始化数据库，读取user表，用于cgi注册登陆验证
    users->initmysql_result(m_connPool);

}


/*
 * @func: 创建线程池
 */
void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}


/*
 * @func: 网络编程，服务器用于监听的套接字ip等属性的设置
 *        IO复用系统epoll实例的创建与设置，将用于监听的套接字挂到epoll实例上，进行监测
 *        定时器系统用于统一事件源的管道套接字创建设置
 */
void WebServer::eventListen()
{
    // SOCK_STREAM 表示使用面向字节流的TCP协议
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 是否优雅下线
    // 控制套接字关闭时的行为
    // struct linger 是一个用于控制套接字关闭行为的结构体
    // struct linger 结构体中的字段 int l_onoff  表示是否开启 linger 选项
    // struct linger 结构体中的字段 int l_linger 套接字关闭时等待的时间（以秒为单位）
    if(0== m_OPT_LINGER)
    {
        // tmp.l_onoff 关闭，表示当这个套接字被关闭时
        // 它不会等待未发送的数据发送完毕，而是立即关闭套接字
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if(1 == m_OPT_LINGER)
    {
        // tmp.l_onoff 打开，表示当这个套接字被关闭时
        // 会等待1s再关闭套接字
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    // 设置服务器主机ip相关信息
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    // inet_pton(AF_INET, "192.168.126.128", &address.sin_addr.s_addr);
    address.sin_port = htons(m_port);

    // 设置套接字快速复用
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof flag);
    // 套接字绑定本地IP与端口
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof address);
    assert(ret >= 0);

    // 监听客户端连接请求
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    // 初始化定时器最小定时时间间隔
    utils.init(TIMESLOT);

    // 创建epoll实例，维护内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    // 将用于监听的套接字在内核事件表上注册读事件（挂到epoll实例上）
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    // socketpair()函数用于创建一对匿名的、相互连接的管道套接字，用于进程间通信
    // socketpait创建的管道套接字是双向通信的
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);

    // 设置管道写端为非阻塞
    // send是将信息发送给套接字缓冲区，如果缓冲区满了，
    // 则会阻塞，这时候会进一步增加信号处理函数的执行时间，为此，将其修改为非阻塞
    utils.setnonblocking(m_pipefd[1]);
    // 将读端的套接字文件描述符添加到m_epoll实例中，监听读事件
    // 而且是ET，边沿工作模式下，非阻塞，没有阻塞EPOLLONESHOT事件
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);
    // 如此完成了定时器设计中提到的统一事件源

    // 定时器alarm函数传递给主循环的信号值，这里只关注SIGALRM和SIGTERM
    // SIGALRM/SIGTERM 设置信号处理函数
    // SIGPIPE 信号忽视
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);
    // utils.sig_handler函数就是将alarm系统调用产生的定时信号SIGALRM，通过管道套接字发送给主循环
    // utils.sig_handler函数就是将终端发送的终止信号SIGTERM，通过管道套接字发送给主循环

    // alarm系统调用进行定时
    alarm(TIMESLOT);

    // 工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}


/*
 * @func: 创建一个定时器节点，将http连接信息挂载
 */
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    // 初始化http连接对象
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log,
                       m_user, m_passWord, m_databaseName);

    // 初始化定时器资源 client_data数据
    // 创建定时器，设置回调函数和超时事件，绑定用户数据，将定时器添加至定时器容器链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    // 为该http连接创建一个定时器
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];

    timer->cb_func = cb_func;
    // 设置该定时器的超时时间
    time_t cur = time(NULL);
    timer->expire = cur + 3*TIMESLOT;
    users_timer[connfd].timer = timer;
    // 将定时器添加至定时器容器链表中
    utils.m_timer_lst.add_timer(timer);
}


/*
 * @func: 若数据活跃，则将定时器节点往后延迟3个时间单位
 *        并对新的定时器在链表上的位置进行调整
 */
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3*TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}


/*
 * @func: 删除定时器节点，关闭连接
 */
void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    // 执行定时器回调函数
    //  从内核事件表删除事件，关闭文件描述符，释放连接资源
    timer->cb_func(&users_timer[sockfd]);
    if(timer)
    {
        // 从定时器容器中删除定时器，并且释放定时器对象
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}


/*
 * @func: http 处理客户端用户数据
 *        根据监听套接字事件触发方式，接收客户端的连接
 */
bool WebServer::dealclientdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof client_address;
    // LT水平工作模式
    if (0 == m_LISTENTrigmode)
    {
        // 接受客户端连接
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        // 创建一个定时器节点，将http连接信息挂载
        timer(connfd, client_address);
    }

    // ET 边沿工作模式
    else
    {
        // 边沿触发需要一直accept直到为空
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}


/*
 * @func: 处理定时器信号,set the timeout ture
 */
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    //从管道读端读出信号值，成功返回字节数，失败返回-1
    //正常情况下，这里的ret返回值总是1，只有14和15两个ASCII码对应的字符
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        // handle the error
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        //处理信号值对应的逻辑
        for (int i = 0; i < ret; ++i)
        {

            //这里面明明是字符
            switch (signals[i])
            {
                //这里是整型
                case SIGALRM:
                {
                    timeout = true;
                    break;
                }
                //关闭服务器
                case SIGTERM:
                {
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}


/*
 * @func: 处理客户连接上接收到的数据(通信套接字读操作)
 */
void WebServer::dealwithread(int sockfd)
{
    // 创建定时器临时变量，将该连接对应的定时器取出来
    util_timer *timer = users_timer[sockfd].timer;

    // Reactor事件处理模式
    // Reactor模式，仅负责文件描述符的事件监听
    if(1 == m_actormodel)
    {
        if(timer)
        {
            //将定时器(超时时间)往后延迟3个单位
            adjust_timer(timer);
        }

        // 若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);
        while(true)
        {
            // 是否正在处理中
            if (1 == users[sockfd].improv)
            {
                // 事件类型关闭连接
                if (1 == users[sockfd].timer_flag)
                {
                    // 删除定时器节点，关闭连接
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }

    // Proactor事件处理模式
    // Proactor模式，负责文件描述符的事件监听以及IO数据读写
    else
    {
        // 先读取数据，再放进请求队列
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            // 将该事件放入请求队列
            m_pool->append_p(users + sockfd);
            if (timer)
            {
                // 将该http连接对应的定时器超时时间延长3个单位
                adjust_timer(timer);
            }
        }
        else
        {
            // 删除定时器节点，关闭连接
            deal_timer(timer, sockfd);
        }
    }
}


/*
 * @func: 写操作
 */
void WebServer::dealwithwrite(int sockfd)
{
    // 创建定时器临时变量，将该连接对应的定时器取出来
    util_timer *timer = users_timer[sockfd].timer;
    // Reactor事件处理模式
    // Reactor模式，仅负责文件描述符的事件监听
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        // 将写事件放入线程池请求队列
        // state = 1
        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                // 是否关闭连接
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }

    // Proactor事件处理模式
    // Proactor模式，负责文件描述符的事件监听以及IO数据读写
    else
    {
        // 将响应报文写入到通信套接字的写缓冲区，发送给浏览器(客户)端
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                // 将该http连接对应的定时器超时时间延长3个单位
                adjust_timer(timer);
            }
        }
        else
        {
            // 删除定时器节点，关闭连接
            deal_timer(timer, sockfd);
        }
    }
}


/*
 * @func: 事件回环（即服务器主线程循环）
 */
void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        // 等待所监控文件描述符上有事件的产生
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        // EINTR错误的产生：当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。
        // 例如：在socket服务器端，设置了信号捕获机制，有子进程，
        // 当在父进程阻塞于慢系统调用时由父进程捕获到了一个有效信号时，
        // 在epoll_wait时，因为设置了alarm定时触发警告，导致每次返回-1，errno为EINTR，对于这种错误返回
        // 忽略这种错误，让epoll报错误号为4时，再次做一次epoll_wait
        // EINTR错误的产生(系统调用被打断，产生的假错误)---当发现是假错误，就需要重新epoll_wait再次进行系统调用
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        // 对所有就绪事件进行处理
        for(int i =0; i<number; i++)
        {
            int sockfd = events[i].data.fd;
            // 若就绪的负责监听的套接字，处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                // 根据监听套接字事件触发方式，接收客户端的连接
                bool flag = dealclientdata();
                if (false == flag)
                    continue;
            }
            // 处理异常事件
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 服务器端关闭该http连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 处理定时器信号(就绪的套接字是发送定时器信号的管道套接字读端)
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                // 接收到SIGALRM信号，timeout设置为True
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            // 处理客户连接上接收到的数据(通信套接字接收到的数据)
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                std::cout << "write event..." << std::endl;
                dealwithwrite(sockfd);
            }
        }

        // 处理定时器为非必须事件，收到信号并不是立马处理
        // 完成读写事件后，再进行处理
        if (timeout)
        {
            // 定时处理任务，重新定时以不断触发SIGALRM信号
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}