/* auth.h —— 与 setuid 助手 hphelm-auth 通信 */
#ifndef AUTH_H
#define AUTH_H

#include <stddef.h>

/* 返回 1 = 认证成功；0 = 失败；-1 = 无法启动助手 */
int auth_check(const char *user, const char *password);

/* 当前登录用户（失败时返回 "oicey"） */
void auth_default_user(char *out, size_t n);

#endif
