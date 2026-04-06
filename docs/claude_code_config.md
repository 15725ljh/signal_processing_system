# Claude Code 配置信息

## settings.json

路径: `C:\Users\15725\.claude\settings.json`

```json
{
  "$schema": "https://json.schemastore.org/claude-code-settings.json",
  "env": {
    "ANTHROPIC_AUTH_TOKEN": "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
    "ANTHROPIC_BASE_URL": "https://open.bigmodel.cn/api/anthropic",
    "API_TIMEOUT_MS": "300000",
    "BASH_MAX_TIMEOUT_MS": "600000",
    "CLAUDE_CODE_MAX_OUTPUT_TOKENS": "64000",
    "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC": "1",
    "ANTHROPIC_DEFAULT_HAIKU_MODEL": "glm-4.7",
    "ANTHROPIC_DEFAULT_SONNET_MODEL": "glm-5-turbo",
    "ANTHROPIC_DEFAULT_OPUS_MODEL": "glm-5.1",
    "HTTP_PROXY": "http://127.0.0.1:7890",
    "HTTPS_PROXY": "http://127.0.0.1:7890"
  },
  "permissions": {
    "defaultMode": "auto"
  },
  "model": "sonnet",
  "defaultShell": "bash",
  "statusLine": {
    "type": "command",
    "command": "bash ~/.claude/statusline-command.sh"
  },
  "enabledPlugins": {
    "glm-plan-usage@zai-coding-plugins": true,
    "glm-plan-bug@zai-coding-plugins": true
  },
  "extraKnownMarketplaces": {
    "zai-coding-plugins": {
      "source": {
        "source": "directory",
        "path": "C:\\Users\\15725\\AppData\\Local\\npm-cache\\_npx\\2f024689b4d0d3b0\\node_modules\\@z_ai\\coding-helper\\zai-coding-plugins"
      }
    }
  },
  "language": "chinese",
  "alwaysThinkingEnabled": true,
  "autoCompactWindow": 175000,
  "autoUpdatesChannel": "stable",
  "cleanupPeriodDays": 7,
  "effortLevel": "high",
  "feedbackSurveyRate": 0,
  "fastMode": true,
  "autoMemoryEnabled": true,
  "showThinkingSummaries": true,
  "skipDangerousModePermissionPrompt": true,
  "skipAutoPermissionPrompt": true
}
```

### 新增配置说明

| 配置项 | 值 | 说明 |
|--------|------|------|
| `$schema` | `json.schemastore.org/...` | IDE 自动补全与校验 |
| `BASH_MAX_TIMEOUT_MS` | `600000` | Bash 命令超时 10 分钟（默认 2 分钟） |
| `CLAUDE_CODE_MAX_OUTPUT_TOKENS` | `64000` | 最大输出 token 数（默认 32000） |
| `autoUpdatesChannel` | `"stable"` | 锁定稳定版更新通道 |
| `cleanupPeriodDays` | `7` | 7 天后自动清理历史会话（默认 30 天） |
| `effortLevel` | `"high"` | 高投入模式，提升复杂任务质量 |
| `feedbackSurveyRate` | `0` | 关闭满意度调查弹窗 |

## statusline-command.sh

路径: `~/.claude/statusline-command.sh`

```bash
#!/bin/bash
# Claude Code status line: model | cwd | real context tokens
# 从会话日志读取真实 token 数，避免 GLM 代理返回错误的 context window
PYTHONIOENCODING=utf-8 python -c "
import sys, json, os, re, glob

try:
    d = json.load(sys.stdin)
    model = d.get('model', {}).get('display_name', 'unknown')
    model = re.sub(r'(\x1b)?\[[0-9;]*[A-Za-z]]?', '', model)
    cwd = d.get('cwd', '?')
    home = os.path.expanduser('~')
    if cwd.startswith(home):
        cwd = '~' + cwd[len(home):]

    # 从 JSONL 日志读取最后一条 assistant 响应的真实 token
    session_dir = os.path.expanduser('~/.claude/projects/C--Code-C---signal-processing-system/')
    jsonl_files = glob.glob(os.path.join(session_dir, '*.jsonl'))
    real_ctx = None
    for jf in sorted(jsonl_files, key=os.path.getmtime, reverse=True):
        try:
            with open(jf, 'r', encoding='utf-8') as f:
                lines = f.readlines()
            for line in reversed(lines):
                obj = json.loads(line.strip())
                if obj.get('type') == 'assistant':
                    usage = obj.get('message', {}).get('usage', {})
                    inp = usage.get('input_tokens', 0)
                    cache_read = usage.get('cache_read_input_tokens', 0)
                    if inp > 0:
                        real_ctx = inp + cache_read
                        break
            if real_ctx is not None:
                break
        except:
            continue

    if real_ctx is not None:
        limit = 200000
        pct = real_ctx / limit * 100
        ctx = f'{round(real_ctx/1000, 1)}K/{limit//1000}K ({round(pct, 1)}%)'
    else:
        ctx = 'ctx:--'

    print(f'{model} | {cwd} | {ctx}')
except:
    print('---')
"
```

### 修复说明

旧版从 `context_window.used_percentage` 读取百分比，GLM 代理返回的上下文窗口参数与 Claude 原生 API 不一致，导致显示的 token 数和百分比严重偏差。新版改为直接从 JSONL 会话日志中读取最后一条 API 响应的 `input_tokens + cache_read_input_tokens`，得到真实的上下文 token 数。
