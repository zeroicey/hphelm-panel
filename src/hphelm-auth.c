/* hphelm-auth.c —— setuid root 的 PAM 认证助手（**唯一的特权代码**）
 *
 * 只做一件事：从 stdin 读一行密码，用 PAM 的 `login` 栈验证指定用户，往 stdout 打 OK/FAIL。
 * 任何“顺带”的能力都不要加（不开 shell、不执行命令、不写文件）。
 *
 * 构建（必须 setuid root）：
 *   gcc -O2 -Wall -o hphelm-auth hphelm-auth.c -lpam -lpam_misc
 *   sudo install -m 4755 -o root -g root hphelm-auth /usr/local/libexec/hphelm-auth
 *
 * 安全要点：
 *   - 密码**只走 stdin**，绝不进 argv / env（否则 `ps` / `tr '\0' '\n' < /proc/pid/environ` 能看到）
 *   - 只允许验证「调用者自己」这个账号：user 参数必须等于 getuid() 对应的用户名
 *   - 服务名用 `login`（不是自建的 service），这样口令变更/锁定/过期策略与系统登录完全一致
 */
#define _GNU_SOURCE
#include <pwd.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

struct pw_ctx {
    const char *pw;
};

static int conv(int n, const struct pam_message **msg, struct pam_response **resp, void *data) {
    struct pw_ctx *ctx = (struct pw_ctx *)data;
    if (n <= 0) return PAM_SUCCESS;
    struct pam_response *r = calloc((size_t)n, sizeof *r);
    if (!r) return PAM_BUF_ERR;
    for (int i = 0; i < n; i++) {
        switch (msg[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF:
        case PAM_PROMPT_ECHO_ON:
            r[i].resp = strdup(ctx->pw);
            if (!r[i].resp) {
                for (int j = 0; j < i; j++) free(r[j].resp);
                free(r);
                return PAM_BUF_ERR;
            }
            r[i].resp_retcode = 0;
            break;
        default:
            r[i].resp = NULL;
            r[i].resp_retcode = 0;
            break;
        }
    }
    *resp = r;
    return PAM_SUCCESS;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        puts("FAIL usage: hphelm-auth <user>");
        return 2;
    }
    const char *want = argv[1];

    /* 只允许验证调用者自己的账号 */
    struct passwd *self = getpwuid(getuid());
    if (!self) {
        puts("FAIL 无法解析调用者 uid");
        return 2;
    }
    if (strcmp(self->pw_name, want) != 0) {
        printf("FAIL 只允许验证调用者本人（%s ≠ %s）\n", want, self->pw_name);
        return 2;
    }

    /* 读一行密码（最长 512），随即把缓冲区清掉 */
    char pw[512];
    if (!fgets(pw, sizeof pw, stdin)) {
        puts("FAIL 读不到密码");
        return 2;
    }
    pw[strcspn(pw, "\r\n")] = 0;

    struct pw_ctx ctx = {.pw = pw};
    struct pam_conv pc = {.conv = conv, .appdata_ptr = &ctx};
    pam_handle_t *pamh = NULL;

    int rc = pam_start("login", want, &pc, &pamh);
    if (rc != PAM_SUCCESS) {
        printf("FAIL pam_start: %s\n", pam_strerror(pamh, rc));
        explicit_bzero(pw, sizeof pw);
        return 1;
    }
    rc = pam_authenticate(pamh, 0);
    if (rc == PAM_SUCCESS) {
        /* 账户是否允许登录（过期/锁定/时段）——与系统登录同口径 */
        rc = pam_acct_mgmt(pamh, 0);
    }
    if (rc == PAM_SUCCESS) {
        puts("OK");
    } else {
        printf("FAIL %s\n", pam_strerror(pamh, rc));
    }
    pam_end(pamh, rc);
    explicit_bzero(pw, sizeof pw);
    return rc == PAM_SUCCESS ? 0 : 1;
}
