#pragma once

#include <ctime>
#include <assert.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>


// 连接资源结构体成员需要用到定时器类
// 因此定时器类,需要前向声明
class util_timer;


// 连接资源
struct client_data
{
    // 客户端socket地址
    sockaddr_in address;
    // socket文件描述符
    int sockfd;
    // 定时器对象指针
    util_timer *timer;
};


// 定时器类:每一个定时器均是双向链表上的一个节点
class util_timer
{
public:
    // 构造函数
    // 成员列表初始化的方式，将新创建的定时器对象的前向/后向节点都设置为NULL
    util_timer() : prev(NULL), next(NULL) {}

public:
    // 超时时间
    time_t expire;

    // 回调函数:从内核事件表删除事件，关闭文件描述符，释放连接资源
    // 回调函数的形参以连接资源传入
    // 函数指针
    void (* cb_func)(client_data *);

    // 连接资源
    client_data *user_data;
    // 前向定时器
    util_timer *prev;
    // 后继定时器
    util_timer *next;
};


// 定时器容器类--以双向链表实现，存储定时器对象
class sort_timer_lst
{
public:
    // 构造函数创建--存储定时器对象的升序双向链表
    sort_timer_lst();
    // 析构函数--释放
    ~sort_timer_lst();

    // 添加定时器，内部调用私有成员add_timer
    void add_timer(util_timer *timer);
    // 调整定时器，任务发生变化时，调整定时器在链表中的位置
    void adjust_timer(util_timer *timer);
    // 删除定时器
    void del_timer(util_timer *timer);
    // 定时任务处理函数
    void tick();

private:
    // 私有成员，被共有成员add_timer和adjust_timer调用
    void add_timer(util_timer *timer, util_timer *lst_head);
    // 双向链表的头尾节点
    util_timer *head;
    util_timer *tail;
};


// 该类是一个工具类
class Utils
{
public:
    Utils(){}
    ~Utils(){}

    void init(int timeslot);

    // 文件描述符设置非阻塞
    int setnonblocking(int fd);
    // 为文件描述符fd,在epoll实例的事件表上注册读事件，ET(边沿工作模式)，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIMode);
    // 信号处理函数
    static void sig_handler(int sig);
    // 设置信号处理函数
    void addsig(int sig, void(*handler)(int), bool restart = true);
    // 定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    // 管道id
    static int *u_pipefd;
    // 升序链表定时器容器
    sort_timer_lst m_timer_lst;
    // epoll实例(I/O复用)
    static int u_epollfd;
    // 最小时间间隔
    int m_TIMESLOT;
};

// 定时器回调函数
void cb_func(client_data *user_data);



