# 部署改成「本机原地」而不是 rsync + 远端 make

**Status:** ✅ ACCEPTED · **Date:** 2026-10-21 · **Deciders:** 用户（交接要求）+ 运维 AI · **Supersedes:** 2026-10-18 的 `tools/deploy.sh` 远端模型

## Context

- 2026-10-18 时源码权威在 `hpcore:/data/dev/hphelm-panel`，部署靠 `rsync` 推到 `hphelm` 再 `ssh hphelm 'make && sudo make install'`。
- 2026-10-19 整仓迁到 hphelm：**源码目录 = 部署目录 = `/data/dev/hphelm-panel`**。
- 在 hphelm 上跑老脚本会**必然失败**：`~/.ssh/config` 里没有 `hphelm` 这个 Host，`ssh hphelm` ⇒ `Host key verification failed`。
- 交接目标（用户原话）：「他们开发完也知道如何进行更新系统屏保不用麻烦你了」。

## Options

| Option | Upside | Downside | Reversibility |
| --- | --- | --- | --- |
| A 保持远端模型，给本机加一个 `Host hphelm` 别名 | 脚本零改动 | 自己 ssh 自己：多一层网络与鉴权，失败模式更绕；仍是「两份目录」的心智 | easy |
| B 原地部署（构建/安装/重启/判据都在本机） | 没有 rsync、没有 ssh；源码⇄已装**可直接逐字节比对**；失败点少一半 | 只能在 hphelm 上跑（换机器要改） | easy |
| C 用 systemd 单元/包管理器装 | 更“正规” | 为单机方案引入打包负担，超出项目边界 | hard |

## Decision

**选 B。** `tools/deploy.sh` 只做七件本地事：环境自检 → `make` → 备份旧二进制到
`/usr/local/bin/hphelm-panel.prev` → `sudo make install` + 逐字节校验 → `--dump` 语义自检 →
`pkill -x hphelm-panel` 重启并确认 pid 变化 → 判据。`--check` 只读，`--no-restart` 不下线。

## Consequences

- 部署的「成功」有了可核对的凭据：源码与已装二进制逐字节一致、助手 `4755 root:root`、`sleep_off=off`、帧差分 0。
- 回滚从「git checkout + 重跑 rsync」变成一条 `install -m755 ...prev`。
- 文档里所有 `rsync`/`ssh hphelm` 的说法都必须删（本轮已删 `AGENTS.md`/`README.md`/`HANDOFF.md`/`docs/deploy.md`）。
- 部署只能在 hphelm 本机执行；跨机部署若再需要，要另写而不是复活 rsync 分支。

## Revisit when

项目要跑在第二台机器上，或者要交付给别人安装（那时应做成打包/单元，而不是把本机脚本改回远端模型）。
