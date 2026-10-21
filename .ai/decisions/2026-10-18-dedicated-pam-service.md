# 认证用专用 PAM 服务，不复用 `login` 栈

**Status:** ✅ ACCEPTED · **Date:** 2026-10-18 · **Deciders:** 运维 AI（实测撞出来后定） · **Supersedes:** none

## Context

自绘登录页需要「验一个用户名+密码」这一件事。最省事的写法是让 setuid 助手调 `pam_start` 时用
`login` 或 `system-local-login` 的服务名 —— 然后机器被锁在 greeter 外面。

原因：那条链带 **`pam_faillock`**。而 `lightdm-autologin` 的第一行也是 `pam_faillock.so preauth`
⇒ 在自绘登录页上输错 3 次会把账号锁上 ⇒ **连坐 autologin 失败** ⇒ lightdm 起 greeter 并抢走 VT
⇒ 用户看到那个本该消失的旧登录页（正是我们花一整天干掉的东西）。

## Options

| Option | Upside | Downside | Reversibility |
| --- | --- | --- | --- |
| A 复用 `login` 栈 | 零配置 | 带 faillock ⇒ 锁屏输错 3 次锁机（实测） | — |
| B 复用 `system-auth` | 统一策略 | 同上，且引入更多不可控模块 | — |
| C **专用服务 `/etc/pam.d/hphelm-auth`，只留 `pam_unix`** | 特权面与副作用最小；行为可预测 | 两行配置要跟着机器走（重建机器时得记得） | easy |

## Decision

**C。** 文件内容固定为两行：

```text
auth     required pam_unix.so
account  required pam_unix.so
```

助手 `hphelm-auth` 是唯一特权代码（`4755 root:root`，~120 行），密码只走 stdin，不落盘、不进 argv。

## Consequences

- 「输错密码」只会失败，不会锁账号 —— 这是硬要求（否则一次误触就把机器锁在登录页外）。
- 没有 faillock 的限速 ⇒ 暴力破解防护靠 sshd 那一侧与本机物理安全（本机是工位机，可接受）。
- 重建机器时 `/etc/pam.d/hphelm-auth` 必须手工重建（`docs/deploy.md` 5.2 有原文）。

## Revisit when

需要多因素、需要失败的速率限制，或者有别的服务要复用这套认证（那时应抽一个只含 `pam_unix` + `pam_faillock`（带 `unlock_time`）的独立栈，并实测与 autologin 的相互影响）。
