/* auth.c —— 通过 stdin/stdout 把密码交给 setuid 助手（**绝不放进 argv/env**） */
#include "auth.h"

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef HPHELM_AUTH_BIN
#define HPHELM_AUTH_BIN "/usr/local/libexec/hphelm-auth"
#endif

void auth_default_user(char *out, size_t n) {
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name) {
        snprintf(out, n, "%s", pw->pw_name);
    } else {
        snprintf(out, n, "oicey");
    }
}

int auth_check(const char *user, const char *password) {
    int pin[2], pout[2];
    if (pipe(pin) != 0) return -1;
    if (pipe(pout) != 0) {
        close(pin[0]);
        close(pin[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pin[0]); close(pin[1]); close(pout[0]); close(pout[1]);
        return -1;
    }
    if (pid == 0) {
        /* 子进程：stdin = 密码管道，stdout = 结果管道 */
        dup2(pin[0], STDIN_FILENO);
        dup2(pout[1], STDOUT_FILENO);
        /* 把 stderr 也接过去，方便把 PAM 的原因带回来 */
        dup2(pout[1], STDERR_FILENO);
        close(pin[0]); close(pin[1]); close(pout[0]); close(pout[1]);
        execl(HPHELM_AUTH_BIN, "hphelm-auth", user, (char *)NULL);
        _exit(127);
    }

    close(pin[0]);
    close(pout[1]);

    /* 写密码（含结尾换行），然后关掉让助手读到 EOF */
    size_t len = strlen(password);
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(pin[1], password + off, len - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
    if (len && write(pin[1], "\n", 1) < 0) { /* ignore */ }
    close(pin[1]);

    /* 读结果：第一行是 OK / FAIL，其余是原因 */
    char buf[512];
    size_t got = 0;
    ssize_t r;
    while (got < sizeof buf - 1 && (r = read(pout[0], buf + got, sizeof buf - 1 - got)) > 0)
        got += (size_t)r;
    buf[got] = 0;
    close(pout[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    if (!strncmp(buf, "OK", 2)) return 1;
    if (buf[0]) {
        /* 去掉换行后打日志（不含密码） */
        char *nl = strchr(buf, '\n');
        if (nl) *nl = 0;
        fprintf(stderr, "hphelm-panel: auth fail: %s\n", buf);
    } else {
        fprintf(stderr, "hphelm-panel: auth fail: 助手无输出（exit=%d）\n",
                WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    }
    return 0;
}
