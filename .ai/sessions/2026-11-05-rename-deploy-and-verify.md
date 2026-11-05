# 改名 hpopi5b-panel + 生产迁移与验收 — handoff

**Date:** 2026-11-05 · **Branch/Commit:** main @ 2dc6613 · **Duration:** ~1.5h

## Where we ended

项目已从 `hphelm-panel` 全链路改名为 `hpopi5b-panel`，并归位 `/data/dev/project/`；
hpcmd 的推送通路恢复（HTTPS push 成功）。生产端（hpopi5b）已按新名迁移、旧单元 disable，
kiosk 与 trending 定时器全部跑通，验收数字见 `.ai/CURRENT_TASK.md` 第十一轮。

## What changed

| File / area | Change | Why |
| --- | --- | --- |
| 全仓 36 文件 | `hphelm-panel`→`hpopi5b-panel`、`hphelm-auth`→`hpopi5b-auth`、`hphelm-trending`→`hpopi5b-trending`、`hphelm-kiosk`→`hpopi5b-kiosk`；`HPHELM_*` 环境变量 → `HPOPI5B_*` | 用户裁决改名；机器是 hpopi5b，旧名会误导 |
| `systemd/hpopi5b-kiosk.sh` | 新增（原为 Pi 上临时脚本，957 B） | 让启动脚本进仓库，别人能复现 |
| `README.md` / `AGENTS.md` / `docs/deploy.md` | 加「现役 kiosk / 历史锁屏」横幅；部署节改为 hpcmd push → Pi `make panel` → install → restart | 旧文档默认 hphelm 本机部署，已失真 |
| `.ai/CURRENT_TASK.md` | 新增第十轮（改名/归位）、第十一轮（生产迁移与验收） | 交接 |
| GitHub | `gh repo rename hpopi5b-panel`；remote 改 `https://github.com/zeroicey/hpopi5b-panel.git` | 旧 URL 301 但仍改干净 |

## Verified / unverified

- [x] 真机 `make panel` 零错误，产物 102272 B 与 hpcmd 打包源码一致（逐字节未验，仅大小/运行判据）
- [x] 两远程列连上、`probe-frames` 0 闪帧、X 错误 0、CPU 5.7%、RSS 17.5 MiB、1 线程
- [x] `hpopi5b-trending.timer` 14:56 跑通 16 条，下次 17:56
- [x] 旧单元 `hphelm-kiosk` / `hphelm-trending.*` 已 disable 且 inactive，文件保留
- [ ] 未测：连续 13 分钟不黑屏（DPMS 判据）—— 需盯屏或看 `xset q` 前后
- [ ] 未测：拔网/断链自愈（旧补丁未变，风险低）

## Loose ends

- [ ] 旧二进制目录 `/usr/local/lib/hphelm-panel/`、`/usr/local/share/hphelm-panel/`、`/usr/local/bin/hphelm-kiosk` 与旧单元文件待稳定后删除（现作回滚）
- [ ] `~/.ssh/config` 的 `Host hpopi5b` 仍指失效的 `~/.ssh/hpopi5b`；未改（影响其他机条目，先问）
- [ ] 旧日志 `/var/log/hphelm-panel.log` 与旧 cache 目录未删

## Do not repeat

- 打包时**先确认新文件在 tar 里**：第一次 tar 漏了 `systemd/hpopi5b-kiosk.sh`，install 中断在 `cannot stat`。
- 不要用裸 `ssh hpopi5b` 判断「钥/授权坏了」——本机 IdentityFile 失效，先看 `ssh -v` 有没有 `Offering public key`。
- `systemctl is-active` 对已 disable 的旧单元可能报 `failed`（上次被 stop 的残留）⇒ `reset-failed` 再判，别误判成迁移失败。

## Next step

1) 盯 13 分钟黑屏判据（`xset q` 的 `Screen Saver` / `DPMS` 段落前后对比）；
2) 稳定运行 1–2 天后删旧路径与旧单元文件（`rm -rf /usr/local/lib/hphelm-panel ...` + `systemctl daemon-reload`）；
3) 处理 `~/.ssh/config` 的 `Host hpopi5b`（改用 `id_ed25519` 或补钥文件）。
