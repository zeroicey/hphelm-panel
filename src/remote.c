/* remote.c —— 生产服务器指标采集（ssh 长连接 + 远端 perl 探针）
 *
 * 链路：
 *   hphelm-panel ──fork/exec──▶ ssh -T <target> 'perl -'  ──▶ 被监控机的 perl
 *        ▲                         stdin = tools/probe-remote.pl（本地文件）
 *        └──── 非阻塞读 stdout ──── 1 Hz 的 S/C/P/E 行（见 remote.h）
 *
 * 设计约束（为什么长这样）：
 *   ① **不 fork 每帧**：建连一次、常驻，fd 直接进主循环的 poll —— 不引入线程、不打断 1 Hz 节奏
 *   ② **只在面板态跑**：桌面态/登录态把子进程停掉（没人看的时候不该有网络流量）
 *   ③ **半帧不提交**：`S`/`C`/`P` 先写进 stg，只有收到 `E` 才 memcpy 到 m
 *      ⇒ 视图永远读不到「CPU 是新的、进程榜是空的」这种撕裂状态
 *   ④ **退避重连**：2→4→8→16→30 秒封顶；断线期间面板显示「离线 Ns」，不阻塞主循环
 */
#define _GNU_SOURCE
#include "remote.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <unistd.h>

/* 状态变迁日志：只在**变化时**打一行（正常一天几行，不刷屏）。
 * ⚠️ 没有这个日志，线上出现「离线」时无从查起 —— 2026-10-21 就是靠猜才找到原因的。 */
static void rlog(const remote_t *r, const char *fmt, ...) {
    char t[32];
    time_t tt = time(NULL);
    strftime(t, sizeof t, "%H:%M:%S", localtime(&tt));
    printf("[%s] 远程 %s: ", t, r->target);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* 探针脚本找法：先环境变量，再安装路径，最后开发目录（按顺序命中即止） */
static const char *PROBE_ENV = "HPHELM_PROBE";
static const char *PROBE_INSTALLED = "/usr/local/share/hphelm-panel/probe-remote.pl";

static int probe_open(const char *argv0) {
    const char *cand[8];
    char dev1[1024], dev2[1024], dev3[1024];
    int n = 0;
    if (getenv(PROBE_ENV)) cand[n++] = getenv(PROBE_ENV);
    cand[n++] = PROBE_INSTALLED;
    /* argv0 的目录 → 那里找 ../tools/probe-remote.pl 与同目录 */
    if (argv0 && strchr(argv0, '/')) {
        char dir[900];
        snprintf(dir, sizeof dir, "%s", argv0);
        char *sl = strrchr(dir, '/');
        if (sl) {
            *sl = 0;
            snprintf(dev1, sizeof dev1, "%s/../tools/probe-remote.pl", dir);
            snprintf(dev2, sizeof dev2, "%s/probe-remote.pl", dir);
            cand[n++] = dev1;
            cand[n++] = dev2;
        }
    }
    snprintf(dev3, sizeof dev3, "tools/probe-remote.pl");
    cand[n++] = dev3;
    cand[n] = NULL;
    for (int i = 0; i < n; i++) {
        int fd = open(cand[i], O_RDONLY | O_CLOEXEC);
        if (fd >= 0) return fd;
    }
    return -1;
}

void remote_init(remote_t *r, const char *target) {
    memset(r, 0, sizeof *r);
    r->fd = -1;
    if (target && target[0]) {
        snprintf(r->target, sizeof r->target, "%s", target);
        r->enabled = 1;
    }
    r->stg.temp_c = -1;
    r->m.temp_c = -1;
}

/* ⚠️ 不能「kill 完就把 pid 清零」：那样子进程变僵尸且永远没人收。
 *    这里给 200ms 宽限（只在状态切换时发生，不影响主循环节奏），再从 SIGKILL 兜底。 */
static void stop_child(remote_t *r) {
    if (r->fd >= 0) {
        close(r->fd);
        r->fd = -1;
    }
    if (r->pid > 0) {
        int st;
        pid_t pid = (pid_t)r->pid;
        kill(pid, SIGTERM);
        int reaped = 0;
        for (int i = 0; i < 20; i++) {
            if (waitpid(pid, &st, WNOHANG) == pid) {
                reaped = 1;
                break;
            }
            usleep(10000);
        }
        if (!reaped) {
            kill(pid, SIGKILL);
            waitpid(pid, &st, 0);
        }
        r->pid = 0;
    }
    r->running = 0;
    r->off = 0;
}

int remote_fd(const remote_t *r) { return r->enabled ? r->fd : -1; }

double remote_age(const remote_t *r, double now) {
    if (!r->saw_frame) return -1;
    return now - r->last_frame;
}

int remote_online(const remote_t *r, double now) {
    if (!r->enabled) return 0;
    if (!r->running || !r->saw_frame) return 0;
    return remote_age(r, now) < 5.0;
}

void remote_start(remote_t *r, double now) {
    if (!r->enabled || r->running) return;
    if (now < r->next_start) return;

    int script = probe_open(NULL);
    if (script < 0) {
        rlog(r, "找不到 tools/probe-remote.pl（可用 $HPHELM_PROBE 指定）");
        r->failures++;
        r->next_start = now + 60.0;
        return;
    }
    int p[2];
    if (pipe2(p, O_CLOEXEC) != 0) {
        close(script);
        r->failures++;
        r->next_start = now + 5.0;
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(script);
        close(p[0]);
        close(p[1]);
        r->failures++;
        r->next_start = now + 5.0;
        return;
    }
    if (pid == 0) {
        /* ---- 子进程 ---- */
        /* 0 = 探针脚本（ssh 会把 stdin 转发到远端），1 = 管道回父进程，2 = 丢弃 */
        if (dup2(script, 0) < 0) _exit(127);
        if (dup2(p[1], 1) < 0) _exit(127);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, 2);
            if (devnull > 2) close(devnull);
        }
        /* 关掉其余继承来的 fd（X 连接、单实例锁…）—— 别让子进程替父进程「占着」它们 */
        for (int fd = 3; fd < 256; fd++) close(fd);
        execlp("ssh", "ssh", "-T", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
               "-o", "ServerAliveInterval=10", "-o", "ServerAliveCountMax=3", r->target,
               "perl -", (char *)NULL);
        _exit(127);
    }
    /* ---- 父进程 ---- */
    close(script);
    close(p[1]);
    fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
    r->fd = p[0];
    r->pid = (int)pid;
    r->running = 1;
    r->off = 0;
    r->started_at = now;
    rlog(r, "拉起 ssh（第 %d 次尝试）", r->failures + 1);
}

void remote_stop(remote_t *r) { stop_child(r); }

/* ---------------- 解析 ---------------- */
/* 取下一个空格分隔的 token；返回 0 = 没有了 */
static int tok(char **pp, char **out) {
    char *p = *pp;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return 0;
    *out = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (*p) *p++ = 0;
    *pp = p;
    return 1;
}

static double tdouble(const char *s) { return s ? strtod(s, NULL) : 0; }
static long tlong(const char *s) { return s ? strtol(s, NULL, 10) : 0; }

/* S <ts> <host> <cpu%> <ncore> <uptime> <memT> <memU> <swapT> <swapU> <disk%:GB>
 *   <rxBps> <txBps> <tempC> <l1> <l5> <l15> <freqMHz> <dock> <nproc> <gov> <iface> <ip>
 *   [追加段，可缺] <memCachedMB> <psiCpu> <psiMem> <psiIo> <nthreads> <rxTotalMB>
 *                  <txTotalMB> <netErr> <netDrop>
 * ⚠️ 追加段一律**可选**：老探针（只发 22 个字段）也要能跑，缺的就留 0。 */
#define S_MAX_FIELDS 32
static void parse_S(metrics_t *m, char *rest) {
    char *t[S_MAX_FIELDS];
    int n = 0;
    char *p = rest, *o;
    while (n < S_MAX_FIELDS && tok(&p, &o)) t[n++] = o;
    if (n < 22) return; /* 基础字段不全 ⇒ 丢掉这行（探针与本程序的版本不匹配）
                           ⚠️ 参数顺序改了就改这里，同时改 tools/probe-remote.pl 的 printf */
    m->now = (time_t)tlong(t[0]);
    snprintf(m->host, sizeof m->host, "%s", t[1]);
    m->cpu_pct = tdouble(t[2]);
    m->ncore = (int)tlong(t[3]);
    if (m->ncore > MAX_CORE) m->ncore = MAX_CORE;
    m->uptime_s = tdouble(t[4]);
    m->mem_total_mb = tdouble(t[5]);
    m->mem_used_mb = tdouble(t[6]);
    m->mem_pct = m->mem_total_mb > 0 ? 100.0 * m->mem_used_mb / m->mem_total_mb : 0;
    m->swap_total_mb = tdouble(t[7]);
    m->swap_used_mb = tdouble(t[8]);
    m->swap_pct = m->swap_total_mb > 0 ? 100.0 * m->swap_used_mb / m->swap_total_mb : 0;
    /* "磁盘%" 与 "磁盘GB" 在探针里是复合字段 pct:GB */
    {
        char *c = strchr(t[9], ':');
        if (c) {
            *c = 0;
            m->disk_used_pct = tdouble(t[9]);
            m->disk_total_gb = tdouble(c + 1);
            m->disk_free_gb = m->disk_total_gb * (1.0 - m->disk_used_pct / 100.0);
        }
    }
    m->rx_bps = tdouble(t[10]);
    m->tx_bps = tdouble(t[11]);
    m->temp_c = tdouble(t[12]);
    if (m->temp_c > 200) m->temp_c = -1;
    m->load[0] = tdouble(t[13]);
    m->load[1] = tdouble(t[14]);
    m->load[2] = tdouble(t[15]);
    m->freq_mhz = tlong(t[16]);
    m->ndocker = (int)tlong(t[17]);
    m->nproc = (int)tlong(t[18]);
    snprintf(m->governor, sizeof m->governor, "%s", t[19]);
    snprintf(m->iface, sizeof m->iface, "%s", t[20]);
    snprintf(m->ip, sizeof m->ip, "%s", t[21]);
    m->net_up = m->ip[0] != 0;
    /* 远端不采集 NVMe 累计读写（要逐块读 /sys/block 下的 stat，收益不大） */
    m->have_ssd = 0;
}

static void parse_C(metrics_t *m, char *rest) {
    char *p = rest, *o;
    int i = 0;
    while (i < MAX_CORE && tok(&p, &o)) m->core_pct[i++] = tdouble(o);
    while (i < MAX_CORE) m->core_pct[i++] = 0;
}

/* P <nproc> <pid>:<rssMiB>:<name>,...
 * ⚠️ 按**逗号**分段，不按空格 —— 进程名里常有空格（"searxng worker"），
 *    按空格分会把一条记录劈成两半（实测：名字里混进了 pid 和内存数字）。
 *    探针那边也把名字里的空白替换成 `_`，两头都防。 */
static void parse_P(metrics_t *m, char *rest) {
    char *p = rest, *o;
    if (!tok(&p, &o)) return;
    m->nproc = (int)tlong(o);
    m->ntop = 0;
    char *seg = p;
    while (*seg == ' ') seg++;
    while (*seg && m->ntop < TOPN) {
        char *end = strchr(seg, ',');
        if (end) *end = 0;
        char *c1 = strchr(seg, ':');
        char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
        if (c1 && c2) {
            *c1 = 0;
            *c2 = 0;
            proc_row_t *row = &m->top[m->ntop++];
            row->pid = (int)tlong(seg);
            row->rss_mb = tdouble(c1 + 1);
            snprintf(row->name, sizeof row->name, "%s", c2 + 1);
        }
        if (!end) break;
        seg = end + 1;
    }
}

/* 返回 1 = 这一帧提交了 */
static int commit(remote_t *r, double now) {
    metrics_t *s = &r->stg;
    /* 历史曲线：连到 stg 上（提交后视图读到的是连续序列） */
    s->cpu_hist = r->m.cpu_hist;
    s->mem_hist = r->m.mem_hist;
    s->rx_hist = r->m.rx_hist;
    s->tx_hist = r->m.tx_hist;
    s->temp_hist = r->m.temp_hist;
    ring_push(&s->cpu_hist, s->cpu_pct);
    ring_push(&s->mem_hist, s->mem_pct);
    ring_push(&s->rx_hist, s->rx_bps);
    ring_push(&s->tx_hist, s->tx_bps);
    if (s->temp_c >= 0) ring_push(&s->temp_hist, s->temp_c);
    r->m = *s;
    r->last_frame = now;
    if (!r->saw_frame) rlog(r, "已连上（主机 %s，%d 核，%d 个进程）", s->host, s->ncore, s->nproc);
    r->saw_frame = 1;
    r->failures = 0;
    return 1;
}

int remote_read(remote_t *r, double now) {
    if (!r->enabled || r->fd < 0) return 0;
    int got = 0;
    for (;;) {
        ssize_t n = read(r->fd, r->buf + r->off, sizeof r->buf - r->off);
        if (n > 0) {
            r->off += (size_t)n;
            got = 1;
            if (r->off >= sizeof r->buf) r->off = 0; /* 异常长的一帧：丢掉缓冲，别让它撑爆 */
            continue;
        }
        if (n == 0) { /* EOF ⇒ ssh 退了 */
            rlog(r, "ssh 已退出（收到 EOF）");
            stop_child(r);
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        stop_child(r);
        break;
    }
    if (!got) return 0;

    /* 逐行消费（只处理以 \n 结尾的完整行；半行留在缓冲里等下次） */
    int committed = 0;
    size_t start = 0;
    for (size_t i = 0; i < r->off; i++) {
        if (r->buf[i] != '\n') continue;
        r->buf[i] = 0;
        char *line = r->buf + start;
        if (line[0] == 'S' && line[1] == ' ') parse_S(&r->stg, line + 2);
        else if (line[0] == 'C' && line[1] == ' ') parse_C(&r->stg, line + 2);
        else if (line[0] == 'P' && line[1] == ' ') parse_P(&r->stg, line + 2);
        else if (line[0] == 'E') {
            if (commit(r, now)) committed = 1;
        }
        /* 其它行（远端杂音）直接忽略 */
        start = i + 1;
    }
    if (start > 0) {
        memmove(r->buf, r->buf + start, r->off - start);
        r->off -= start;
    }
    return committed;
}

void remote_reap(remote_t *r, double now) {
    if (!r->enabled) return;
    if (r->pid > 0) {
        int st = 0;
        pid_t w = waitpid(r->pid, &st, WNOHANG);
        if (w == r->pid) {
            rlog(r, "ssh 进程退出（status=%d）", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
            r->pid = 0;
            r->running = 0;
            if (r->fd >= 0) {
                close(r->fd);
                r->fd = -1;
            }
            r->off = 0;
            r->failures++;
            double wait_s = 2.0 * (double)(r->failures > 4 ? 16 : (1 << (r->failures - 1)));
            if (wait_s > 30.0) wait_s = 30.0;
            r->next_start = now + wait_s;
        }
    }
    /* 连上了但迟迟没有数据 ⇒ 掐了重连。
     * ⚠️ 这里**必须**同时覆盖 saw_frame == 0 的情况：
     *    ssh 连上了、但远端探针没起来（对方刚重启 / perl 挂了 / 卡在认证），
     *    连接就一直挂着、一帧都收不到 ⇒ 面板永远停在「离线」。
     *    我第一版只判 `saw_frame && age > 30` ⇒ 首次连不上就永久卡死（真机踩到）。
     * 判据：从未收到帧时，连接存活超过 20 秒即视为失败；收到过帧后，30 秒无新帧视为失败。 */
    if (r->running) {
        double limit = r->saw_frame ? 30.0 : 20.0;
        double base = r->saw_frame ? r->last_frame : r->started_at;
        if (base > 0 && now - base > limit) {
            rlog(r, "%s %.0fs 无数据 ⇒ 掐掉重连", r->saw_frame ? "连续" : "连接后", now - base);
            stop_child(r);
            r->failures++;
            double wait_s = 2.0 * (double)(r->failures > 4 ? 16 : (1 << (r->failures - 1)));
            if (wait_s > 30.0) wait_s = 30.0;
            r->next_start = now + wait_s;
        }
    }
}
