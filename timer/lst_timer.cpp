#include "lst_timer.h"
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include "../http/http_conn.h"


/*
 * func:定时器容器类的构造函数
 *      --初始化双向链表
 */
sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}


/*
 * func:定时器容器类的析构函数
 *      --从双向链表头节点开始依次释放各个节点
 */
sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}


/*
 * func: 添加定时器
 *      --定时器容器的双向链表添加定时器对象节点
 */
void sort_timer_lst::add_timer(util_timer *timer)
{
    if(!timer)
    {
        // 若传入的定时器对象为NULL，直接return
        return;
    }
    if(!head)
    {
        // 若头节点为空
        // 新添加的定时器为双向链表的第一个定时器节点
        head = tail = timer;
        return;
    }
    // 双向链表中各个定时器中是按照expire（定时器到期时间）从小到大排序
    // 若新定时器超时时间小于当前头部节点
    // 直接将当前定时器结点作为头部结点
    if(timer->expire < head->expire)
    {
        // 新插入节点的后驱指向头节点
        timer->next = head;
        // 头节点前驱指向新插入节点
        head->prev = timer;
        // 将头节点指针指向新插入节点
        head = timer;
        return;
    }
    // 非以上情况，就需要从头节点开始进行遍历双向链表
    // 将新节点插入到合适的位置
    add_timer(timer, head);
}


/*
 * func:调整定时器，任务发生变化时，调整定时器在链表中的位置
 *      -- 比如说，一个连接在上一次到期时间内，突然又有了信息通信，就需要将该连接的定时器超时时间进行更新(增加)
 */
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if(!timer)
    {
        // 若传入的定时器对象为NULL，直接return
        return;
    }

    util_timer *tmp = timer->next;
    // 被调整的定时器在链表尾部
    // 定时器超时（到期）值仍然小于下一个定时器超时（到期）时间,不调整
    if(!tmp || (timer->expire < tmp->expire))
    {
        return;
    }

    // 被调整定时器是链表头节点，将定时器取出，重新插入
    if(timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        // 取出的节点需要遍历双向链表进行插入
        add_timer(timer,head);
    }
    else
    {
        // 被调整定时器在内部，将定时器取出，重新插入
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        // 当前定时器节点前面的定时器到期时间均小于自己
        // 因此从自己的后驱节点进行遍历链表
        add_timer(timer,timer->next);
    }
}


/*
 * func:删除定时器:即是双向链表节点的删除
 */
void sort_timer_lst::del_timer(util_timer *timer)
{
    if(!timer)
    {
        // 若传入的定时器对象为NULL，直接return
        return;
    }
    // 链表中只有一个定时器(头/尾节点均指向该定时器)，需要删除该定时器
    if((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    // 被删除的定时器为头节点
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    // 被删除的定时器为尾节点
    if(timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }

    // 被删除的定时器在链表内部，常规链表结点删除
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}


/*
 * func:定时器任务处理函数
 *      --使用统一事件源（alarm闹钟SIGALRM信号，通过管道发送给主循环，添加到epoll实例上，在管道读端进行监测）
 *      --SIGALRM信号每次被触发，主循环中调用一次定时任务处理函数，处理链表容器中到期的定时器
 * 处理逻辑:
 *      step1.遍历定时器升序链表容器，从头结点开始依次处理每个定时器，直到遇到尚未到期的定时器
 *          --因为链表是按照到期时间进行升序组织的，若当前定时器未到期，则后面的定时器更加没有到期
 *      step2.若当前时间小于定时器超时时间，跳出循环，即未找到到期的定时器
 *      step3.若当前时间大于定时器超时时间，即找到了到期的定时器，执行回调函数，然后将它从链表中删除，然后继续遍历
 */
void sort_timer_lst::tick()
{
    // 头节点为NULL，则链表为空
    if(!head)
    {
        return;
    }

    // 获取当前时间
    time_t cur = time(NULL);
    util_timer *tmp = head;

    // 遍历定时器链表
    while (tmp)
    {
        // 若当前时间小于当前定时器超时时间，后面的定时器也没有到期（因为链表是按照超时时间进行升序组织）
        if(cur < tmp->expire)
        {
            break;
        }

        // 当前定时器到期，则调用回调函数，执行定时事件
        tmp->cb_func(tmp->user_data);

        // 将处理后的定时器从链表容器中删除，并重置头节点
        head = tmp->next;
        if(head)
        {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}


/*
 * func:add_timer(util_timer *timer)/adjust_timer函数中添加定时器节点的私有函数
 *      --主要用于调整链表内部结点
 *      --当定时器节点的插入需要遍历双向链表才能完成时，调用
 */
void sort_timer_lst::add_timer(util_timer *timer,util_timer *lst_head)
{
    // 因为既然调用该函数，那么插入的定时器到期时间
    // 一定比头节点位置的定时器到期时间长
    util_timer *prev = lst_head;
    // 因此开始阶段直接与头节点的后驱节点进行比较
    util_timer *tmp = prev->next;

    // 从双向链表中找到定时器应该放置的位置
    // 即遍历一遍双向链表找到对应的位置
    // 优化:此处的时间复杂度为O(n) 可以考虑C++11的优先队列进行优化
    while (tmp)
    {
        // 比较定时器到期时间,若插入定时器的到期时间小于当前定时器节点的到期时间
        // 则插入到当前定时器节点的前一个位置
        if(timer->expire < tmp->expire)
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }

    // 遍历完发现，目标定时器需要放到链表尾节点处
    // 插入的定时器到期时间比链表中所有节点的到期时间都长
    if(!tmp)
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}


/*
 * func:初始化最小定时时间间隔
 */
void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}


/*
 * func:设置文件描述符为非阻塞
 */
int Utils::setnonblocking(int fd)
{
    // fcntl函数可以获取/设置文件描述符性质
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}


/*
 * func:向epoll实例中添加需要监听的文件描述符
 *      将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
 */
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
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
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        // EPOLLONESHOT 件描述符被触发一次事件之后，自动将该文件描述符的监测事件从内核维护的事件表中进行删除
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}


/*
 * func:信号处理函数
 */
void Utils::sig_handler(int sig)
{
    // 为保证函数的可重入性，保留原来的errno、
    // 可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;

    // 将信号值从管道写端写入，传输字符类型，而非整型
    // 管道u_pipefd[1]为写端、u_pipefd[0]为读端
    // 通过管道将信号值发送给主循环，主循环通过IO复用系统，对管道的读端的读事件进行监测
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}


/*
 * func:设置被捕捉信号的处理函数
 * parameter:
 *      sig：要捕捉和处理的信号编号
 *      handler：信号处理函数的指针 其类型为 void(int)
 *      restart：一个布尔值，指示是否使用 SA_RESTART 标志
 * */
void Utils::addsig(int sig, void(*handler)(int), bool restart)
{
    // 创建sigaction结构体变量
    struct sigaction sa;
    // 结构体变量内容清0
    memset(&sa, '\0', sizeof(sa));
    // 指定信号处理函数
    sa.sa_handler = handler;
    if (restart)
        // SA_RESTART 使被信号打断的系统调用自动重新发起
        sa.sa_flags |= SA_RESTART;
    // sa.sa_mask 在信号处理函数执行期间, 临时屏蔽某些信号, 将要屏蔽的信号设置到集合中即可
    // 假设在这个集合中不屏蔽任何信号, 默认也会屏蔽一个（捕捉的信号是谁, 就临时屏蔽谁）-- 当前就是这种情况
    // 用来将参数set信号集初始化，然后把所有的信号加入到此信号集里
    sigfillset(&sa.sa_mask);

    // 执行sigaction函数
    // assert检查sigaction函数是否调用成功，即sigaction返回是否非-1
    // 若sigaction函数为-1，则会输出一条错误信息到标准错误流、调用 abort 函数终止程序执行
    assert(sigaction(sig, &sa, NULL) != -1);
}


/*
 * func:定时处理任务，重新定时以不断触发SIGALRM信号
 */
void Utils::timer_handler()
{
    // 对双向链表进行检查，看是否有超时的定时器
    m_timer_lst.tick();
    // 最小的时间单位为5s
    // 重新进行闹钟定时
    alarm(m_TIMESLOT);
}


/*
 * func:向套接字connfd发送错误信息
 */
void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}


// 初始化类静态变量
int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;


/*
 * func: 定时器回调函数
 *       从内核事件表删除事件，关闭文件描述符，释放连接资源
 */
class Utils;
void cb_func(client_data *user_data)
{
    // 从epoll实例维护的事件表，删除非活动连接在socket上的注册事件
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    // assert断言函数，确保user_data指针是有效的
    assert(user_data);
    // 关闭文件描述符
    close(user_data->sockfd);
    // 减少连接数
    http_conn::m_user_count--;
    // ？？？？ 为什么没有将http对象的m_sockfd设置为-1
}



