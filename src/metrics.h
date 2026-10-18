/* metrics.h —— hphelm 面板的数据采集层（只读 /proc 与 /sys） */
#ifndef METRICS_H
#define METRICS_H

#include <time.h>

#define RING_CAP 300 /* 1 Hz × 5 分钟 */
#define MAX_CORE 32
#define TOPN 6

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

typedef struct {
    /* 身份 / 时间 */
    char host[64];
    time_t now;
    double uptime_s;
    /* CPU */
    double cpu_pct;
    double core_pct[MAX_CORE];
    int ncore;
    double load[3];
    double temp_c;
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
    /* 历史曲线 */
    ring_t cpu_hist;
    ring_t mem_hist;
    ring_t rx_hist;
    ring_t tx_hist;
} metrics_t;

void metrics_init(metrics_t *m);
/* want_procs: 进程榜比较贵（要遍历 /proc），建议每 3 秒才置 1 */
void metrics_sample(metrics_t *m, int want_procs);

#endif
