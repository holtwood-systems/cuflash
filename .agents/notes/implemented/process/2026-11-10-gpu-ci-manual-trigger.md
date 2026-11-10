# Agent Note: GPU CI 移除 weekly cron，仅保留手动触发

Status: implemented

## Problem

`.github/workflows/gpu.yml` 依赖 `[self-hosted, linux, gpu]` runner，
但该 runner 从未在 holtwood-systems 组织注册。weekly cron
（`17 5 * * 1`）每周一照常触发，job 在队列中等待约 24 小时后被
取消——`gh run list` 显示连续多次 scheduled run 均为
`cancelled`、`24h0m` 时长，最近一条已 queued 17h+。公开 Actions
页因此留下一条从未执行过的「假门禁」轨迹，与本仓「每个声明可回溯」
的证据纪律冲突。

## Decision

移除 `schedule` 触发器，仅保留 `workflow_dispatch`，与 trifuse /
cuda-foundations 的 GPU workflow 口径一致。文件头注释同步更新：
runner 注册前本 workflow 只能手动触发；获得已授权 GPU 硬件时在
Settings → Actions → Runners 注册带 `self-hosted,linux,gpu` 标签的
runner，再考虑是否恢复定时。

## Alternatives considered

- **保留 cron 等 runner 上线** — 不需要这次改动；但每周产生一条必然
  被取消的公开 run 记录，审计者会误读为「GPU 门禁存在但 flaky」，
  比没有门禁更差，否决。
- **改用 GitHub 托管 GPU larger runner** — 能立刻获得 runner；但
  需要 Team/Enterprise 计划并按分钟计费（唯一规格为 Tesla T4，
  sm_75），公开仓也计费；T4 也无法复现 sm_89 性能口径，成本与
  收益不符，否决。
- **对 PR 触发 GPU job** — 公开仓的 self-hosted runner 若被 fork PR
  触发，等于在自有硬件上执行任意代码，安全风险不可接受，否决。

## Consequences

- 公开 Actions 页不再产生每周 queued→cancelled 记录；GPU 正确性验证
  的诚实口径回到「本地快照 + 手动触发」。
- 代价：忘记手动触发时 GPU 回归不会被 CI 自动发现；由本地门禁命令、
  ctest 串行套件与结果包归档制度兜底。
- runner 日后上线时可单独恢复 schedule，本决策不构成障碍。

## Verification

- `git show HEAD:.github/workflows/gpu.yml` 中 `on:` 仅含
  `workflow_dispatch`。
- 下一个周一之后 `gh run list --workflow gpu.yml` 不再新增
  schedule 记录。
