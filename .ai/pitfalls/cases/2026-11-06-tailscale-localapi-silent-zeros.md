# tailscaled localapi：写错一个键名就「0 台设备」，不报错、不 4xx

**日期：** 2026-11-06 · **作者：** 第十五轮（尾网块） · **影响：** 任何读 tailscaled localapi 的代码

## 症状

探针里 `GET /localapi/v0/status` 返回 `HTTP/1.0 200 OK` + 一份合法 JSON，
但遍历 `$j->{Peers}` 得到 **0 台设备**；`BackendState=Running`、`Self.HostName` 都正常，
所以看起来「连上了、只是没有对端」。同一台机 `sudo tailscale status` 明明列出 9 台。

## 根因

`ipnstate.Status` 的 Go 结构里，对端 map 的 JSON tag 是 **`Peer`（单数）**：

```
Peer: map[key.NodePublic]*PeerStatus `json:"Peer"`
```

写成 `Peers` 在 Perl 里只是取到一个 `undef`，`keys %{undef}` ⇒ 空列表。
**HTTP 200 + 语义空**，整条链路上没有任何一处会报错。

## 修法

先打印顶层键看名字，再写解析：

```sh
sudo -n perl -MIO::Socket::UNIX -MJSON::PP -e '
  my $s = IO::Socket::UNIX->new(Type=>Socket::SOCK_STREAM(),Peer=>"/var/run/tailscale/tailscaled.sock");
  print $s "GET /localapi/v0/status HTTP/1.0\r\nHost: local-tailscaled.sock\r\nConnection: close\r\n\r\n";
  my $b = do { local $/; <$s> }; my ($h,$body)=split /\r\n\r\n/,$b,2;
  print join(",", sort keys %{ JSON::PP->new->utf8->decode($body) }), "\n"'
```

判据（写进 `tools/probe-remote.pl` 的注释）：拿到 JSON 后**先看 `T` 行的总台数**——
`T 0 0 …` 或整行缺失就是解析或 socket 出了问题，不要当成「尾网没有设备」。

## 守卫

- 探针注释里写明「键是 `Peer`（单数），写成 `Peers` 会静默得到 0 台」。
- 面板侧 `ts_ok` 只在真的解析出 `T` 行时置 1；`nts == 0` 但 `ts_total > 0` 会显示「（没有对端设备）」，
  而不是把整块藏起来当没事发生。
- 同类教训：**HTTP 200 ≠ 语义正确**。凡是从 JSON 里取集合，先断言集合非空或打印键名。
