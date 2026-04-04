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
# Claude Code status line: model | cwd | context
PYTHONIOENCODING=utf-8 python -c "
import sys, json, os, re
try:
    d = json.load(sys.stdin)
    model = d.get('model', {}).get('display_name', 'unknown')
    model = re.sub(r'(\x1b)?\[[0-9;]*[A-Za-z]]?', '', model)
    cwd = d.get('cwd', '?')
    home = os.path.expanduser('~')
    if cwd.startswith(home):
        cwd = '~' + cwd[len(home):]
    cw = d.get('context_window', {})
    pct = cw.get('used_percentage')
    if pct is not None:
        used_k = round(pct / 100 * 200, 1)
        ctx = f'{used_k}K/200K ({round(pct,1)}%)'
    else:
        ctx = '已用:--'
    print(f'{model} | {cwd} | {ctx}')
except:
    print('---')
"
```
