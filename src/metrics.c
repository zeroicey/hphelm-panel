/* metrics.c —— 数据采集：只读 /proc 与 /sys，不 fork 子进程（CPU 开销 ~0.1%） */
#define _GNU_SOURCE
#include "metrics.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

/* ---------------- 环形缓冲 ---------------- */
void ring_push(ring_t *r, double v) {
    r->v[r->head] = v;
    r->head = (r->head + 1) % RING_CAP;
    if (r->len < RING_CAP) r->len++;
}

double ring_at(const ring_t *r, int i) { /* i=0 最旧 */
    int start = (r->head - r->len + RING_CAP) % RING_CAP;
    return r->v[(start + i) % RING_CAP];
}

double ring_max(const ring_t *r, double floor_v) {
    double m = floor_v;
    for (int i = 0; i < r->len; i++) {
        double v = ring_at(r, i);
        if (v > m) m = v;
    }
    return m;
}

double ring_avg(const ring_t *r) {
    if (!r->len) return 0;
    double s = 0;
    for (int i = 0; i < r->len; i++) s += ring_at(r, i);
    return s / r->len;
}

/* ---------------- 小工具 ---------------- */
static int read_line(const char *path, char *buf, size_t n) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int ok = fgets(buf, n, f) != NULL;
    fclose(f);
    if (ok) buf[strcspn(buf, "\n")] = 0;
    return ok;
}

static int read_double(const char *path, double *out) {
    char b[64];
    if (!read_line(path, b, sizeof b)) return 0;
    char *end;
    double v = strtod(b, &end);
    if (end == b) return 0;
    *out = v;
    return 1;
}

static void human_rate(double bps, char *out, size_t n) {
    if (bps < 1024) snprintf(out, n, "%4.0fB/s", bps);
    else if (bps < 1024 * 1024) snprintf(out, n, "%4.1fK/s", bps / 1024);
    else snprintf(out, n, "%4.1fM/s", bps / 1048576);
}

void human_rate_pub(double bps, char *out, size_t n) { human_rate(bps, out, n); }

static void human_mb(double mb, char *out, size_t n) {
    if (mb >= 1024) snprintf(out, n, "%.1fG", mb / 1024.0);
    else snprintf(out, n, "%.0fM", mb);
}

void human_mb_pub(double mb, char *out, size_t n) { human_mb(mb, out, n); }

/* ---------------- CPU ----------------
 * ⚠️ 性能：不要「每个核打开一次 /proc/stat」—— 8 核就是 9 次 fopen/秒。
 *    单次遍历拿到汇总 + 所有核，CPU 开销降到 1/9，且保证是同一时刻的快照。 */
#define CPU_SLOT_TOTAL MAX_CORE

typedef struct {
    unsigned long long busy[MAX_CORE + 1];
    unsigned long long total[MAX_CORE + 1];
    int ncore;
} cpustat_t;

static void read_cpustat(cpustat_t *cs) {
    memset(cs, 0, sizeof *cs);
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "cpu", 3) != 0) break;
        int slot;
        if (line[3] == ' ') {
            slot = CPU_SLOT_TOTAL;
        } else {
            int c = atoi(line + 3);
            if (c < 0 || c >= MAX_CORE) continue;
            slot = c;
            if (c + 1 > cs->ncore) cs->ncore = c + 1;
        }
        unsigned long long v[10] = {0};
        int n = sscanf(line, "%*s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]);
        if (n < 4) continue;
        unsigned long long idle = v[3] + v[4]; /* idle + iowait */
        unsigned long long tot = 0;
        for (int i = 0; i < n && i < 10; i++) tot += v[i];
        cs->busy[slot] = tot - idle;
        cs->total[slot] = tot;
    }
    fclose(f);
}

static double pct_delta(unsigned long long b0, unsigned long long t0,
                        unsigned long long b1, unsigned long long t1) {
    if (t1 <= t0) return 0;
    double d = (double)(b1 - b0) / (double)(t1 - t0) * 100.0;
    if (d < 0) d = 0;
    if (d > 100) d = 100;
    return d;
}

/* ---------------- 温度（按名字找 k10temp，别写死 hwmonN） ---------------- */
static double read_temp(void) {
    DIR *d = opendir("/sys/class/hwmon");
    if (!d) return -1;
    struct dirent *e;
    char path[512], name[64];
    double t = -1;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "hwmon", 5) != 0) continue;
        snprintf(path, sizeof path, "/sys/class/hwmon/%s/name", e->d_name);
        if (!read_line(path, name, sizeof name)) continue;
        if (strcmp(name, "k10temp") != 0) continue;
        snprintf(path, sizeof path, "/sys/class/hwmon/%s/temp1_input", e->d_name);
        double raw;
        if (read_double(path, &raw) && raw > 0) t = raw / 1000.0;
        break;
    }
    closedir(d);
    return t;
}

/* ---------------- 内存 ---------------- */
static void read_mem(metrics_t *m) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;
    char key[64], unit[32];
    long val;
    double total = 0, avail = 0, stot = 0, sfree = 0;
    while (fscanf(f, "%63s %ld %31s", key, &val, unit) == 3) {
        if (!strcmp(key, "MemTotal:")) total = val / 1024.0;
        else if (!strcmp(key, "MemAvailable:")) avail = val / 1024.0;
        else if (!strcmp(key, "SwapTotal:")) stot = val / 1024.0;
        else if (!strcmp(key, "SwapFree:")) sfree = val / 1024.0;
    }
    fclose(f);
    m->mem_total_mb = total;
    m->mem_used_mb = total - avail;
    m->mem_pct = total > 0 ? 100.0 * m->mem_used_mb / total : 0;
    m->swap_total_mb = stot;
    m->swap_used_mb = stot - sfree;
    m->swap_pct = stot > 0 ? 100.0 * m->swap_used_mb / stot : 0;
}

/* ---------------- 磁盘 ---------------- */
static void read_disk(metrics_t *m) {
    struct statvfs vfs;
    if (statvfs("/", &vfs) == 0 && vfs.f_blocks) {
        double total = (double)vfs.f_blocks * vfs.f_frsize;
        double freeb = (double)vfs.f_bavail * vfs.f_frsize;
        m->disk_total_gb = total / 1073741824.0;
        m->disk_free_gb = freeb / 1073741824.0;
        m->disk_used_pct = 100.0 * (total - freeb) / total;
    }
    /* NVMe 累计读写（扇区 × 512） */
    DIR *d = opendir("/sys/block");
    if (!d) return;
    struct dirent *e;
    char path[512], line[512];
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "nvme", 4) != 0) continue;
        if (strncmp(e->d_name, "nvme0n", 6) != 0) continue;
        snprintf(path, sizeof path, "/sys/block/%s/stat", e->d_name);
        if (!read_line(path, line, sizeof line)) continue;
        unsigned long long rd = 0, wr = 0;
        /* 字段：read_ios read_merges read_sectors ... write_ios write_merges write_sectors */
        unsigned long long f1, f2, f3, f4, f5, f6, f7;
        if (sscanf(line, "%llu %llu %llu %llu %llu %llu %llu",
                   &f1, &f2, &f3, &f4, &f5, &f6, &f7) == 7) {
            rd = f3;
            wr = f7;
        }
        m->ssd_read_gb = (double)rd * 512.0 / 1073741824.0;
        m->ssd_written_gb = (double)wr * 512.0 / 1073741824.0;
        m->have_ssd = 1;
        break;
    }
    closedir(d);
}

/* ---------------- 网络 ---------------- */
static void read_net(metrics_t *m, double dt) {
    char path[256], b[64];
    unsigned long long rx = 0, tx = 0;
    static unsigned long long p_rx, p_tx;
    static int first = 1;

    snprintf(path, sizeof path, "/sys/class/net/%s/statistics/rx_bytes", m->iface);
    if (!read_line(path, b, sizeof b)) return;
    rx = strtoull(b, NULL, 10);
    snprintf(path, sizeof path, "/sys/class/net/%s/statistics/tx_bytes", m->iface);
    if (!read_line(path, b, sizeof b)) return;
    tx = strtoull(b, NULL, 10);

    if (!first && dt > 0.05) {
        m->rx_bps = (double)(rx - p_rx) / dt;
        m->tx_bps = (double)(tx - p_tx) / dt;
    }
    p_rx = rx;
    p_tx = tx;
    first = 0;

    ring_push(&m->rx_hist, m->rx_bps);
    ring_push(&m->tx_hist, m->tx_bps);
}

/* ---------------- IP ---------------- */
static void read_ips(metrics_t *m) {
    struct ifaddrs *ifa, *p;
    m->net_up = 0;
    m->ts_up = 0;
    if (getifaddrs(&ifa) != 0) return;
    for (p = ifa; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        char ip[INET_ADDRSTRLEN] = {0};
        struct sockaddr_in *sin = (struct sockaddr_in *)p->ifa_addr;
        inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof ip);
        if (!strcmp(p->ifa_name, m->iface)) {
            /* 用完整 IP（原来只留后两段 ⇒ 顶栏显示「5.6」这种看不出来是什么的东西） */
            snprintf(m->ip, sizeof m->ip, "%s", ip);
            m->net_up = 1;
        } else if (!strncmp(p->ifa_name, "tailscale", 9)) {
            snprintf(m->ts_ip, sizeof m->ts_ip, "%s", ip);
            m->ts_up = 1;
        }
    }
    freeifaddrs(ifa);
}

/* ---------------- 进程榜（按 RSS） ---------------- */
static int cmp_proc(const void *a, const void *b) {
    const proc_row_t *x = a, *y = b;
    if (y->rss_mb > x->rss_mb) return 1;
    if (y->rss_mb < x->rss_mb) return -1;
    return 0;
}

static void read_procs(metrics_t *m) {
    DIR *d = opendir("/proc");
    if (!d) return;
    struct dirent *e;
    long pagesize = sysconf(_SC_PAGESIZE);
    static proc_row_t all[512]; /* static：别把 20KB 放栈上（这函数每秒都会被调） */
    int n = 0;
    m->nproc = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        int pid = atoi(e->d_name);
        if (pid <= 0) continue;
        char path[128], line[1024];
        /* ⚠️ 只读一次 /proc/<pid>/stat：它同时含 (comm) 与 rss 字段（第 24 个），
           比「comm + statm 读两次」少一半 fopen —— 进程榜是面板里最贵的一项。 */
        snprintf(path, sizeof path, "/proc/%d/stat", pid);
        if (!read_line(path, line, sizeof line)) continue;
        m->nproc++;
        if (n >= 512) continue;
        char *lp = strchr(line, '(');
        char *rp = strrchr(line, ')');
        if (!lp || !rp || rp <= lp) continue;
        size_t nlen = (size_t)(rp - lp - 1);
        if (nlen == 0 || nlen > 63) continue;
        char comm[64];
        memcpy(comm, lp + 1, nlen);
        comm[nlen] = 0;
        /* ')' 之后是 state(3) 起的字段；rss 是第 24 个 ⇒ 在剩余 token 里下标 21 */
        char *tok[32];
        int nt = 0;
        for (char *p = rp + 1; *p && nt < 32; p++) {
            while (*p == ' ') p++;
            if (!*p) break;
            tok[nt++] = p;
            while (*p && *p != ' ') p++;
        }
        if (nt < 22) continue;
        long rss_pages = atol(tok[21]);
        double rss_mb = (double)rss_pages * pagesize / 1048576.0;
        if (rss_mb < 0.5) continue; /* 太小的不显示 */
        all[n].pid = pid;
        snprintf(all[n].name, sizeof all[n].name, "%.*s", (int)sizeof(all[n].name) - 1, comm);
        all[n].rss_mb = rss_mb;
        n++;
    }
    closedir(d);
    qsort(all, n, sizeof all[0], cmp_proc);
    m->ntop = n < TOPN ? n : TOPN;
    for (int i = 0; i < m->ntop; i++) m->top[i] = all[i];
}

/* ---------------- 网卡选择 ----------------
 * ⚠️ 不能靠 readdir(/sys/class/net) 的顺序 —— 实测它先返回了没接线的 wlp1s0，
 *    于是 ip 为空、net_up=0（面板上一片 down）。判据必须是「有全局 IPv4」，
 *    优先级 en* > wl* > 其它，并排除 lo / tailscale / docker / veth / br-。 */
static int iface_ignored(const char *n) {
    return !strcmp(n, "lo") || !strncmp(n, "tailscale", 9) || !strncmp(n, "docker", 6) ||
           !strncmp(n, "veth", 4) || !strncmp(n, "br-", 3) || !strncmp(n, "virbr", 5);
}

static void pick_iface(metrics_t *m) {
    struct ifaddrs *ifa, *p;
    char best[16] = "";
    int best_rank = 0;
    if (getifaddrs(&ifa) == 0) {
        for (p = ifa; p; p = p->ifa_next) {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
            const char *n = p->ifa_name;
            if (iface_ignored(n)) continue;
            int rank = 1;
            if (!strncmp(n, "en", 2)) rank = 3;
            else if (!strncmp(n, "wl", 2)) rank = 2;
            if (rank > best_rank) {
                best_rank = rank;
                snprintf(best, sizeof best, "%.15s", n);
            }
        }
        freeifaddrs(ifa);
    }
    if (best[0]) snprintf(m->iface, sizeof m->iface, "%s", best);
}

/* ---------------- 对外 ---------------- */
void metrics_init(metrics_t *m) {
    memset(m, 0, sizeof *m);
    gethostname(m->host, sizeof m->host - 1);
    pick_iface(m);
    if (!m->iface[0]) snprintf(m->iface, sizeof m->iface, "eno1");
    /* 先采一次 CPU 基准，免得第一次是 0 */
    { cpustat_t cs; read_cpustat(&cs); (void)cs; }
}

static double mono_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

void metrics_sample(metrics_t *m, int want_procs) {
    /* ⚠️ dt 用单调时钟（亚秒）。原来用 time_t 的整秒差：采样快于 1Hz 时
       dt 恒为 0 ⇒ 网络速率永远算不出来（实测踩到，当时是主循环空转 35Hz）。 */
    static double last_mono;
    double tnow = mono_now();
    double dt = last_mono > 0 ? tnow - last_mono : 0;
    last_mono = tnow;
    time_t now = time(NULL);
    m->now = now;
    read_double("/proc/uptime", &m->uptime_s);

    /* CPU：单次快照，汇总 + 所有核一起算 */
    static cpustat_t prev;
    cpustat_t cur;
    read_cpustat(&cur);
    if (prev.total[CPU_SLOT_TOTAL]) {
        m->cpu_pct = pct_delta(prev.busy[CPU_SLOT_TOTAL], prev.total[CPU_SLOT_TOTAL],
                               cur.busy[CPU_SLOT_TOTAL], cur.total[CPU_SLOT_TOTAL]);
    }
    m->ncore = cur.ncore > MAX_CORE ? MAX_CORE : cur.ncore;
    for (int c = 0; c < m->ncore; c++) {
        m->core_pct[c] = prev.total[c]
                             ? pct_delta(prev.busy[c], prev.total[c], cur.busy[c], cur.total[c])
                             : 0;
    }
    prev = cur;
    FILE *f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%lf %lf %lf", &m->load[0], &m->load[1], &m->load[2]) != 3) {}
        fclose(f);
    }
    m->temp_c = read_temp();
    if (!read_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor",
                   m->governor, sizeof m->governor))
        snprintf(m->governor, sizeof m->governor, "-");
    double khz;
    if (read_double("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", &khz))
        m->freq_mhz = (long)(khz / 1000);

    read_mem(m);
    read_disk(m);
    /* 容器数：数 cgroup 里的 docker-*.scope（不 fork docker，几乎零成本，10 秒一次）。
       -1 = 这个核没有 docker（面板不显示这一项，绝不显示假的「0 个」）。 */
    static double t_dock;
    if (tnow - t_dock >= 10.0 || m->ndocker == 0) {
        t_dock = tnow;
        DIR *cg = opendir("/sys/fs/cgroup/system.slice");
        if (cg) {
            int n = 0;
            struct dirent *ce;
            while ((ce = readdir(cg)))
                if (!strncmp(ce->d_name, "docker-", 7) && ce->d_name[7]) n++;
            closedir(cg);
            m->ndocker = n;
        } else {
            m->ndocker = -1;
        }
    }
    /* ⚠️ getifaddrs 每次都开 netlink 转储全部地址，是这里最贵的一项；
       IP 几乎不变 ⇒ 每 10 秒才刷一次（按时间，不按调用次数：
       否则循环一快就会狂刷）。net_up 会保留上次的值。 */
    static double t_ips;
    if (tnow - t_ips >= 10.0) {
        t_ips = tnow;
        read_ips(m);
    }
    read_net(m, dt);
    if (want_procs) read_procs(m);

    ring_push(&m->cpu_hist, m->cpu_pct);
    ring_push(&m->mem_hist, m->mem_pct);
}
