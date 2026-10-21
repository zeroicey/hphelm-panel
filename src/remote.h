/* remote.h —— 生产服务器（远端主机）指标采集
 *
 * 做法（2026-10-21 用户裁决）：**ssh 流式探针，不在被监控机上装任何东西**。
 *   · 常驻一条 `ssh <target>`，把 tools/probe-remote.pl 从 stdin 喂给远端的 `perl -`
 *   · 探针只读 /proc 与 /sys，1 Hz 往 stdout 打 k=v 行；`E` 行结尾才算一帧
 *   · 本进程非阻塞读它的 stdout（fd 直接进主循环的 poll），子进程死了就退避重连
 *
 * 为什么不用「每秒 ssh 一次」：每次建连要 ~100ms + 一次完整 SSH 握手，
 * 而且会让远端 sshd 每秒 fork 一个 perl —— 那是每秒一个进程的噪音。
 * 一条长连接 + 一个长驻 perl 的实测成本：远端 0.3% of one core，本机 <0.1%。
 */
#ifndef REMOTE_H
#define REMOTE_H

#include <stddef.h>
#include <time.h>

#include "metrics.h"

#define R_BUF 65536   /* 单帧最大字符数（24 条进程 + 16 核 ≈ 1.5KB，留足余量） */

typedef struct {
    char target[64];      /* ssh 目标（~/.ssh/config 里的 Host 别名）；空 = 未启用 */
    int enabled;
    int fd;               /* 子进程 stdout；-1 = 没有在跑 */
    int pid;              /* 子进程 pid（0 = 没有） */
    int running;          /* 子进程还活着 */
    int saw_frame;        /* 曾经收到过完整帧 */
    int failures;         /* 连续启动失败次数（退避用） */
    double next_start;    /* 下次允许启动的时刻（monotonic 秒） */
    double last_frame;    /* 最近一帧到达的时刻（monotonic 秒） */
    size_t off;           /* 接收缓冲里已写入的字节数 */
    char buf[R_BUF];
    metrics_t stg;        /* 正在拼的那一帧（收到 E 才提交） */
    metrics_t m;          /* 已提交的最新快照（视图只读这个） */
} remote_t;

/* target 为空或 NULL ⇒ 不启用（面板照常只显示本机） */
void remote_init(remote_t *r, const char *target);
/* 到点了就拉起子进程（幂等；next_start 未到则什么都不做） */
void remote_start(remote_t *r, double now);
/* 可 poll 的 fd；-1 = 当前没有连接 */
int remote_fd(const remote_t *r);
/* 有数据可读时调用；返回 1 = 提交了新的一帧 */
int remote_read(remote_t *r, double now);
/* 子进程退出/超时检查 + 安排重连（每秒调用一次即足） */
void remote_reap(remote_t *r, double now);
void remote_stop(remote_t *r);
/* 距上一帧的秒数；从未连上返回 -1 */
double remote_age(const remote_t *r, double now);
/* 采集是不是「健康」：在跑 + 帧不老于 5 秒 */
int remote_online(const remote_t *r, double now);

#endif
