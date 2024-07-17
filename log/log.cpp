#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <cstring>


using namespace std;

/*
 * func:构造函数
 * note:初始化日志记录行数/是否异步写入日志
 */
Log::Log()
{
    m_count = 0;
    m_is_async = 0;
}


/*
 * func:析构函数
 * note:关闭日志文件
 */
Log::~Log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
}


/*
 * func:静态接口函数
 * note:获取唯一的日志实例对象
 */
Log* Log::get_instance()
{
    static Log instance;
    return &instance;
}


/*
 * func:异步写日志公有方法，调用私有方法async_write_log
 */
void *Log::flush_log_thread(void *args)
{
    Log::get_instance()->async_write_log();
    return NULL;
}


/*
 * func:私有的异步写日志方法
 */
void *Log::async_write_log()
{
    string single_log;
    // 从阻塞队列中取出一个日志string，写入文件
    while (m_log_queue->pop(single_log))
    {
        m_mutex.lock();
        fputs(single_log.c_str(), m_fp);
        m_mutex.unlock();
    }
    return NULL;
}


/*
 * func:初始化日志信息
 * note:可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
 *      1.创建日志文件（命名方式：当前年份_当前月份_当前日_日志文件名）
 *      2.创建写缓冲区
 *      3.异步,还需要设置阻塞队列,且创建一个写线程，从阻塞队列中pop日志信息写入到日志文件中
 */
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    //如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        // string类型日志的循环队列
        m_log_queue = new block_queue<std::string>(max_queue_size);
        pthread_t tid;
        // flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;
    // 初始化缓冲区大小
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    // 初始化日志文件的最大行数
    m_split_lines = split_lines;

    // 获取当前时间，获取自 1970 年 1 月 1 日以来的秒数
    time_t t = time(NULL);
    // 转换时间为本地时间格式
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    // 从后往前找到第一个/的位置---file_name可能传入的是路径，如:LOG/log_today
    const char *p = strrchr(file_name, '/');
    char log_full_name[512] = {0};

    if (p == NULL)
    {
        // file_name直接是日志文件
        // 向log_full_name数组中写入，当前的"当前年份_当前月份_当前日_日志文件名"
        snprintf(log_full_name, 511, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);

    }
    else
    {
        // log_name为日志文件名
        strncpy(log_name, p + 1, sizeof log_name);
        // 日志文件的上层目录的路径信息
        strncpy(dir_name, file_name, p - file_name + 1);
        // 向log_full_name数组中写入，当前的"目录+当前年份_当前月份_当前日期_日志文件名"
        snprintf(log_full_name, 511, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    // 当前日期
    m_today = my_tm.tm_mday;

    // 以追加模式打开日志文件
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}


/*
 * func:写日志文件
 * note:1.若为同步，直接将日志信息写入日志文件
 *      2.若为异步，则将日志信息push阻塞队列,init函数中创建了写子线程，用于从阻塞队列中pop日志信息，异步写入日志文件
 */
void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    // 获取当前精确的时间信息
    gettimeofday(&now, NULL);
    // 提取从1970年1月1日 00:00:00 UTC 至今的秒数
    time_t t = now.tv_sec;
    // 转换为当地时间格式
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level)
    {
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }
    //写入一个log，对m_count++, m_split_lines最大行数
    m_mutex.lock();
    m_count++;

    // 日志不是今天或写入的日志行数是最大行的倍数
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) //everyday log
    {

        char new_log[512] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        //格式化日志名中的时间部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        //如果是时间不是今天,则创建今天的日志，更新m_today和m_count
        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 511, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else
        {
            //超过了最大行，在之前的日志名基础上加后缀, m_count/m_split_lines
            snprintf(new_log, 511, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        // 均会打开一个新的日志文件
        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();

    va_list valst;
    //将传入的format参数赋值给valst，便于格式化输出
    va_start(valst, format);

    string log_str;
    m_mutex.lock();

    //写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);

    // 传入的日志信息，已经按照format赋值给valst
    // 将日志信息写入m_buf
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    //若m_is_async为true表示异步，false为同步
    //若异步,则将日志信息加入阻塞队列,同步则加锁向文件中写
    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}



