## TinyWebServer

***

Linux下基于C++的轻量级Web服务器注释、修改版本. 原项目来自:https://github.com/qinguoyi/TinyWebServer

**项目特点**：

* 使用 **线程池 + 非阻塞socket + epoll(ET和LT均实现) + 事件处理(Reactor和同步IO模拟Proactor均实现)** 的并发模型
* 使用**主-从状态机模式**解析HTTP请求报文，支持解析**GET和POST**请求
* 访问服务器数据库实现web端用户**注册、登录**功能，可以请求播放服务器**图片和视频文件**
* 实现**单例模式的同步/异步日志系统**，记录服务器运行状态
* 使用定时器系统处理非活跃的 HTTP 连接
* 使用**单例模式的数据库连接池**，管理数据库连接资源
* 经 Webbench 压力测试可以实现**上万的并发连接**数据交换

***

### 快速运行

***

* 服务器测试环境

  > Ubuntu版本 20.01.4 LTS
  >
  > MYSQL版本 8.0.37

* 浏览器测试环境

  > Windows、Linux均可
  >
  > Chrome

* 测试前请确保已安装JSONCPP库

  ```bash
  $ sudo apt-get update
  $ sudo apt-get install libjsoncpp-dev
  ```

* 测试前请确保已安装MYSQL

  安装以及简单的操作，可以进行跳转至链接：`UBUNTU`下安装`MYSQL`

  ```mysql
  // 建立yourdb库
  create database yourdb;
  
  // 创建user表
  USE yourdb;
  CREATE TABLE user(
      username char(50) NULL,
      passwd char(50) NULL
  )ENGINE=InnoDB;
  
  // 添加数据
  INSERT INTO user(username, passwd) VALUES('name', 'passwd');
  ```

* 在`dbconf.json`文件中初始化数据库相关信息

  ```json
  // 数据库服务器端口、用户名、密码、库名
  {
    "db_port": 3306,
    "userName": "zxz",
    "password": "123456",
    "dbName": "serverdb"
  }
  ```

* 编译项目

  在项目目录`TinyWebServerBymyself`下的`build`目录中

  ```bash
  $ cmake ..
  $ make
  ```

  生成的可执行文件在`TinyWebServerBymyself`目录下

* 默认运行服务器

  在项目目录`TinyWebServerBymyself`,运行

  ```bash
  $ ./TinyWebServerBymyself 
  ```

* 浏览器端，打开指定网页

  ```bash
  http://127.0.0.1:9006
  ```



### 个性化运行

***

```bash
x $ ./TinyWebServerBymyself [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] [-t thread_num] [-c close_log] [-a actor_model]
```

以上参数不是非必须，不用全部使用，根据个人情况搭配选用即可

> `p`，自定义端口号
>
> * 默认9006
>
> `l`，选择日志写入方式，默认同步写入
>
> * 0，同步写入
> * 1，异步写入
>
> `m`，监听套接字和通信套接字的事件触发模式组合，默认使用LT + LT
>
> * 0，表示使用LT + LT
> * 1，表示使用LT + ET
> * 2，表示使用ET + LT
> * 3，表示使用ET + ET
>
> `o`，优雅关闭连接，默认不使用
>
> * 0，不使用
> * 1，使用
>
> `s`，数据库连接数量
>
> * 默认为8
>
> `t`，线程数量
>
> * 默认为8
>
> `c`，关闭日志，默认打开
>
> - 0，打开日志
> - 1，关闭日志
>
> `a`，选择反应堆模型，默认`Proactor`
>
> * 0，Proactor模型
> * 1，Reactor模型

**测试用例命令**

```bash
$ ./TinyWebServerBymyself -p 9007 -l 1 -m 3 -o 1 -s 10 -t 10 -c 1 -a 0
```

> 端口默认 9007
>
> 异步写入日志
>
> 使用ET + ET的事件触发模式
>
> 使用优雅关闭连接
>
> 数据库连接池中有10条连接
>
> 线程池中有10条工作线程
>
> 打开日志
>
> Proactor事件处理模型



### 压力测试

***

**服务器主机机器环境**，4核CPU，单个核心的线程数为1

`webbench.c`源码编译，在`TinyWebServerBymyself`目录下的压力测试目录`test_presure`目录下存在，压力测试工具源码，目录终端输入如下指令，编译

```bash
$ gcc webbench.c -o webbench
```

**使用指令进行测试**，终端输入如下指令

```bash
./webbench -c 10500 -t 10 http://192.168.126.128:9006/
```

`-c`表示客户端数， `-t`表示时间

* `Proactor`，`LT + LT`，19753`QPS`

![image-20240716112824735](https://gitee.com/zhou-xuezhi/mypic2/raw/master/img/20240716112824.png)

* `Proactor`，`LT + ET`，20164`QPS`

![image-20240716113008217](https://gitee.com/zhou-xuezhi/mypic2/raw/master/img/20240716113008.png)

* `Proactor`，`ET + LT`，18084`QPS`

![image-20240716113242320](https://gitee.com/zhou-xuezhi/mypic2/raw/master/img/20240716113242.png)

* `Proactor`，`ET + ET`，17892`QPS`

![image-20240716113421004](https://gitee.com/zhou-xuezhi/mypic2/raw/master/img/20240716113421.png)

* `Reactor`，`LT + ET`，15965`QPS`

![image-20240716113559496](https://gitee.com/zhou-xuezhi/mypic2/raw/master/img/20240716113559.png)

> 并发总连接数：10500
>
> 访问服务器时间：5s
>
> 所有访问均成功



### 致谢

***

Linux高性能服务器编程，游双著.源码来自：https://github.com/qinguoyi/TinyWebServer# TinywebserverBymyself
