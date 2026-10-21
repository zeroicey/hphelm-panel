/* metrics.h —— hphelm 面板的数据采集层（只读 /proc 与 /sys） */
#ifndef METRICS_H
#define METRICS_H

#include <time.h>

#define RING_CAP 300 /* 1 Hz × 5 分钟 */
#define MAX_CORE 32
#define TOPN 40      /* 进程榜条数（4 列 × 10 行；机器上实测约 200 个进程，扫描成本不变） */

typedef struct {
    double v[RING_CAP];
    int len;
    int head;
} ring_t;

void ring_push(ring_t *r, double v);
double ring_at(const ring_t *r, int i); /* i=0 最旧 */
double ring_max(const ring_t *r, double floor_v);
double ring_avg(const ring_t *r);

typedef struct {
    int pid;
    char name[24];
    double rss_mb;
} proc_row_t;

/* ---- 一台主机的指标快照 ----
 * 本机与生产服务器共用这一个结构（同一个 draw_host() 画两遍 ⇒ 两台版面/口径必然对称）。
 * 本机由 metrics.c 填（直接读 /proc），远端由 remote.c 从探针输出解析后填。
 * ⚠️ 两个 ring 是「所有主机共用」的：一条曲线只有 ~1200 点，几 KB。 */
typedef struct {
    /* 身份 / 时间 */
    char host[64];
    time_t now;          /* 本机时间（远端用它自己的 ts，可能有时差） */
    double uptime_s;
    /* CPU */
    double cpu_pct;
    double core_pct[MAX_CORE];
    int ncore;
    double load[3];
    double temp_c;       /* <0 = 没有温度传感器 */
    char governor[32];
    long freq_mhz;
    /* 内存 */
    double mem_total_mb, mem_used_mb, mem_pct;
    double swap_total_mb, swap_used_mb, swap_pct;
    /* 磁盘 */
    double disk_used_pct, disk_total_gb, disk_free_gb;
    double ssd_read_gb, ssd_written_gb;
    int have_ssd;
    /* 网络 */
    char iface[16];
    char ip[40];
    int net_up;
    double rx_bps, tx_bps;
    char ts_ip[40];
    int ts_up;
    /* 进程 */
    int nproc;
    proc_row_t top[TOPN];
    int ntop;
    /* 容器（-1 = 不知道 / 没有 docker） */
    int ndocker;
    /* 历史曲线 */
    ring_t cpu_hist;
    ring_t mem_hist;
    ring_t rx_hist;
    ring_t tx_hist;
} host_metrics_t;

/* 兼容旧名字（本机采集层与视图本来都叫 metrics_t） */
typedef host_metrics_t metrics_t;

void metrics_init(metrics_t *m);
/* want_procs: 进程榜比较贵（要遍历 /proc），建议每 3 秒才置 1 */
void metrics_sample(metrics_t *m, int want_procs);

#endif
