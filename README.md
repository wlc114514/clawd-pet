# clawd-pet

Claude Code 吉祥物 Clawd 的 Windows 桌宠。纯 C + Win32 API，单个 exe（约 150 KB），不依赖任何运行库，常驻 CPU 占用约 0.2%。

> 粉丝作品，与 Anthropic 无关。Clawd 形象归 Anthropic 所有。

## 功能

- 站在任务栏上，眨眼、张望、走来走去、偶尔蹦一下
- 点它会跳并冒爱心；拖起来会举手乱蹬腿，甩出去能扔，撞墙会反弹
- 5 分钟不碰电脑就趴下睡觉冒 zzz，你一动就醒
- 你打字时它坐到键盘前跟着敲（轮询按键状态，不装键盘钩子，不记录按了什么）
- 鼠标互动：不理鼠标 / 跟着鼠标走 / 怕鼠标（被逼到墙角会从鼠标头上跳过去）
- CPU 越忙跑得越快，超过 80% 会冒汗
- 番茄钟、喝水提醒，用中文气泡提示
- 最多同时 6 只；送走一只时它会随机来一段告别戏（吓一跳 / 发抖 / 大哭 😭 / 乱跑），然后从任务栏掉下去，其他 Clawd 会被吓得往后蹦
- 全屏游戏 / 视频时自动躲起来
- 托盘图标 + 右键菜单，设置保存在 `HKCU\Software\ClawdPet`，可开机自启
- **联动 Claude Code**：Claude 干活时它敲键盘，等你批准权限时它挥手，回复完了它跳起来说「搞定啦！」

## 编译

需要 [w64devkit](https://github.com/skeeto/w64devkit)（便携 gcc）。`build.bat` 会先找 PATH 里的 gcc，找不到就用 `%USERPROFILE%\w64devkit\bin`。

```bat
build.bat
```

产物是根目录的 `clawd.exe`。编译前先退出正在运行的桌宠，否则 exe 被占用无法覆盖。

## 联动 Claude Code

在 `~/.claude/settings.json` 里加上下面的 hooks（把路径换成你的 `clawd.exe` 所在位置）。它们都是 `async`，不会拖慢 Claude。桌宠没在运行时，这些命令什么也不做。

```json
{
  "hooks": {
    "UserPromptSubmit":   [{ "hooks": [{ "type": "command", "command": "C:\\path\\to\\clawd.exe", "args": ["--cc", "prompt"], "async": true }] }],
    "PostToolUse":        [{ "matcher": "*", "hooks": [{ "type": "command", "command": "C:\\path\\to\\clawd.exe", "args": ["--cc", "tool"], "async": true }] }],
    "PostToolUseFailure": [{ "matcher": "*", "hooks": [{ "type": "command", "command": "C:\\path\\to\\clawd.exe", "args": ["--cc", "tool"], "async": true }] }],
    "PermissionRequest":  [{ "matcher": "*", "hooks": [{ "type": "command", "command": "C:\\path\\to\\clawd.exe", "args": ["--cc", "permission"], "async": true }] }],
    "Stop":               [{ "hooks": [{ "type": "command", "command": "C:\\path\\to\\clawd.exe", "args": ["--cc", "stop"], "async": true }] }],
    "SessionEnd":         [{ "hooks": [{ "type": "command", "command": "C:\\path\\to\\clawd.exe", "args": ["--cc", "end"], "async": true }] }]
  }
}
```

按 Esc 打断 Claude 时 Claude Code 不会发 `Stop`，桌宠会继续敲最多 3 分钟后自己停下。

## 命令行

| 命令 | 作用 |
|---|---|
| `clawd.exe` | 启动桌宠（只允许一个实例） |
| `clawd.exe --cc <prompt\|tool\|permission\|stop\|end>` | 给运行中的桌宠发 Claude Code 事件 |
| `clawd.exe --sheet out.bmp` | 把所有姿势导出成一张图，调像素画用 |

## 代码结构

| 文件 | 内容 |
|---|---|
| `src/render.c` | 像素画：Clawd 本体、键盘道具、小图标字形，放大到屏幕 |
| `src/pet.c` | 状态机和物理：走、跳、拖、睡、敲键盘、逃跑…… |
| `src/bubble.c` | 中文文字气泡（独立的分层窗口） |
| `src/main.c` | 窗口、托盘、菜单、设置、番茄钟、全屏检测、Claude Code 事件 |
| `tools/mkicon.c` | 编译时生成 exe 图标 |
