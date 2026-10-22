# 远程采集「连上了但没数据」会永久卡死（面板一直显示离线）

**状态：** ✅ 已解决（2026-10-21，用户报「hpcore 怎么离线了」）

## 症状

用户看到面板右列的红点（离线）。但 hpcore 本身**完全正常**：

```
ping 192.168.5.102      → 2 packets, 0% loss, 1.2ms
ssh -T hpcore hostname  → hpcore, up 2 days, load 0.15
```

面板进程还在、`ssh` 子进程却不存在了，而且**不会再起来**（只有重启面板才恢复）。

## 根因（两条，缺一不可）

### ① 超时判据漏了「从未收到帧」这条路径

```c
/* 旧代码 */
if (r->running && r->saw_frame && remote_age(r, now) > 30.0) { /* 掐掉重连 */ }
```

「ssh 建立成功、远端探针却没起来」（对方刚重启 / perl 挂了 / 卡在认证）时：

```
ssh 连上 → 等远端 perl 的 stdout → 永远没有数据 → saw_frame 一直是 0
        ⇒ 进不了上面的分支 ⇒ 既不重连、也不报错 ⇒ 永久离线
```

**修法**：

```c
double limit = r->saw_frame ? 30.0 : 20.0;
double base  = r->saw_frame ? r->last_frame : r->started_at;   /* 新增 started_at */
if (base > 0 && now - base > limit) { /* 掐掉 + 退避重连 */ }
```

### ② 运行态零日志 ⇒ 无法定位

远程采集原来只在 `--dump` 里输出，运行态的断开/重连**一声不响**。
所以当时能拿到的证据只有「进程在、屏幕上是红点」，查不下去，只能靠猜。

**修法**：`rlog()` 在每次**状态变迁**时打一行（拉起 / 已连上 / EOF / 进程退出 / 超时重连）。

## 判据

**用假 ssh 稳定复现**（不需要真的搞坏 hpcore）：

```sh
mkdir -p /tmp/fakebin
printf '#!/bin/sh\nsleep 3600\n' > /tmp/fakebin/ssh && chmod +x /tmp/fakebin/ssh
PATH=/tmp/fakebin:$PATH ./hphelm-panel --state panel --remote hpcore
# 期望 20 秒内：
#   连接后 20s 无数据 ⇒ 掐掉重连
#   拉起 ssh（第 2 次尝试）
```

**真实断线**（杀远端探针）应看到完整的证据链：

```
远程 hpcore: 拉起 ssh（第 1 次尝试）
远程 hpcore: 已连上（主机 hpcore，16 核，386 个进程）
远程 hpcore: ssh 已退出（收到 EOF）
远程 hpcore: 拉起 ssh（第 1 次尝试）
```

## 教训

> **「连上了」不等于「有数据」。** 任何带超时的采集链路，都要为「连接建立但数据从未到达」
> 单独设一条判据 —— 否则最坏情况（永久卡死、只能重启）恰好落在唯一没覆盖的分支上。
>
> **状态机必须有变迁日志。** 没有日志的采集层，出故障时只能靠猜；
> 而「只在 --dump 里能看见」等于运行态没有可观测性。

---

## 追加：修完形态 ③ 又引入形态 ②（同一天，第二轮）

修「连上但没数据」时我把基准写成：

```c
double base = r->saw_frame ? r->last_frame : r->started_at;   /* ❌ */
```

`saw_frame` 是**历史性**的（这台机连过），而 `last_frame` 可能是几十分钟前留下的。
于是「桌面 → 回面板」重新拉起 ssh 时，新连接在**同一秒**就被判成「连续 269 秒无数据」并杀掉：

```
[17:22:02] 状态 DESKTOP → PANEL
[17:22:02] 远程 hpcore: 拉起 ssh（第 1 次尝试）
[17:22:02] 远程 hpcore: 连续 269s 无数据 ⇒ 掐掉重连     ← 同一秒！
[17:22:04] 远程 hpcore: 拉起 ssh（第 2 次尝试）
...                                                    ← 无限循环，退避到 30s 一次
```

**正确写法**：基准 = 「**本次连接**的最后存活证据」= `max(started_at, last_frame)`：

```c
int got_frame = r->last_frame > r->started_at;   /* 本次连接收到过帧吗 */
double base = got_frame ? r->last_frame : r->started_at;
double limit = got_frame ? 30.0 : 20.0;
```

**教训（比 bug 本身重要）**：
- **「历史状态」和「本次连接状态」必须分开**。`saw_frame` 是「曾经成功过」，不是「这次活着」。
- **修 A 形态必须回归 B 形态**。我当时只测了「杀远端探针」（形态 1 的变体）和「假 ssh」（形态 3），
  恰好漏掉用户实际操作的形态 2（锁屏再回来）—— 而那才是这台机 7×24 的日常循环。
- 判据要写进 `docs/verify.md` 第十一节，三种形态每次必跑。
