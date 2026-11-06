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
    double cpu_pct;   /* 自上次采样以来的 CPU 占用（可为 100 以上：多线程进程） */
} proc_row_t;

/* 尾网上的一台设备（2026-11-06 第二版：先做过「容器清单」，用户指出
 * 「容器好像不就是进程吗 那算了」⇒ 换成 tailnet：谁在线、走直连还是中继、此刻在传多少）。
 * 数据只来自 tailscaled 的 localapi（/var/run/tailscale/tailscaled.sock，世界可读），
 * **不 fork tailscale CLI**，也不需要 root。 */
#define TSN 12
typedef struct {
    char name[24];
    char os[10];      /* linux / macOS / windows / iOS / android */
    char relay[12];   /* DERP 区域名（hpnet / hkg / syd）；空 = 不知道 */
    int online;
    int direct;       /* 1 = 有直连地址；0 = 纯 DERP 中继 */
    double last_s;    /* 离线了多久（秒）；在线为 0 */
    double rx_bps, tx_bps; /* 此刻速率（探针按相邻两次累计值求差） */
} ts_peer_t;

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
    double mem_cached_mb;   /* Buffers + Cached + SReclaimable（可回收内存） */
    /* 系统压力 PSI（读 /proc/pressure 下三个文件，取 avg10，百分比） */
    double psi_cpu, psi_mem, psi_io;
    int nthr;               /* 线程总数（来自 /proc/loadavg 的 1/N） */
    int nrunning;           /* 当前可运行线程数 */
    /* 磁盘 */
    double disk_used_pct, disk_total_gb, disk_free_gb;
    double disk_read_gb, disk_written_gb; /* 整盘累计（自开机；非 loop/ram/mtd 设备求和） */
    int have_disk;                        /* 找到过整盘 ⇒ 累计 GB 与速率才有意义 */
    /* 磁盘此刻的读写速率（B/s，所有整盘设备求和；本机与远端都采） */
    double io_rd_bps, io_wr_bps;
    /* 网络 */
    char iface[16];
    char ip[40];
    int net_up;
    double rx_bps, tx_bps;
    double rx_total_mb, tx_total_mb;   /* 累计（自开机） */
    long net_err, net_drop;            /* 错误包 / 丢弃包累计 */
    char ts_ip[40];
    int ts_up;
    /* 进程：两个榜（同一台机、两种排序）—— 面板右列上段「按内存」、下段「按 CPU」 */
    int nproc;
    proc_row_t top[TOPN];      /* 按 RSS 降序（2026-11-06 第二版面板已不画这段，留作未来/调试） */
    int ntop;
    proc_row_t top_cpu[TOPN];  /* 按 CPU% 降序（面板右列就是它） */
    int ntop_cpu;
    /* 容器个数（-1 = 不知道 / 没有 docker）。只数个数，不列清单。 */
    int ndocker;
    /* 尾网：ts_ok = 探针的 T 行到了；本机模式下恒为 0 ⇒ 面板不画这一块 */
    int ts_ok;
    int ts_total, ts_online;   /* 联网设备总数 / 在线数（含本机） */
    char ts_self_ip[24];       /* 本机 tailscale IPv4 */
    ts_peer_t ts[TSN];         /* 对端（不含本机），在线优先、再按流量降序 */
    int nts;                   /* ts[] 里有效几条 */
    /* 历史曲线 */
    ring_t cpu_hist;
    ring_t mem_hist;
    ring_t rx_hist;
    ring_t tx_hist;
    ring_t temp_hist;
    ring_t io_rd_hist;
    ring_t io_wr_hist;
} host_metrics_t;

/* 兼容旧名字（本机采集层与视图本来都叫 metrics_t） */
typedef host_metrics_t metrics_t;

void metrics_init(metrics_t *m);
/* want_procs: 进程榜比较贵（要遍历 /proc），建议每 3 秒才置 1 */
void metrics_sample(metrics_t *m, int want_procs);

#endif
