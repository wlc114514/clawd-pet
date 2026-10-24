# clawd-pet

Claude Code 吉祥物 Clawd 的 Windows / macOS 桌宠。Windows 版是纯 C + Win32 API，单个 exe（约 150 KB），不依赖任何运行库，常驻 CPU 占用约 0.2%；macOS 版共用同一套状态机和像素画，外壳换成 Cocoa，产物是一个 Clawd.app。

> 粉丝作品，与 Anthropic 无关。Clawd 形象归 Anthropic 所有。

## 功能

- 站在任务栏上，眨眼、张望、走来走去、偶尔蹦一下
- 点它会跳并冒爱心（偶尔来个后空翻）；拖起来会举手乱蹬腿，甩出去能扔，撞墙会反弹
- 5 分钟不碰电脑就趴下睡觉冒 zzz，你一动就醒
- 后空翻：闲着时偶尔自己翻一个，Claude Code 干完活庆祝时也可能翻；右键「后空翻」随时看（从托盘点就全体一起翻）
- 偶尔（或者右键「爬上去」）从任务栏里变出一段楼梯（悬空石板 / 实心石砖 / 轮廓线，在「设置 ▸ 楼梯样式」里选），一格一格跳上去；到顶后楼梯塌掉，它就在半空那个高度上走来走去，还会从那里接着往上爬（最高到离屏幕顶端不远），过 1–3 分钟再一口气跳回任务栏
- 围墙：右键「放置围墙」，在屏幕上按住鼠标拖一下就砌出一面红砖墙（横着拖是横墙，竖着拖是竖墙）。墙会一直在，右键它可以拆掉这面或全部拆掉，重启后还在。Clawd 走路撞上墙会「啪」地贴上去、冒星星、往后弹一下，晕乎乎地坐一会儿再掉头走；被扔出去撞墙、跳起来撞到头顶的墙也一样。横墙上面能站：掉下来或扔上去会落在墙上走来走去，走到头就掉下去。楼梯不会穿墙
- 你打字时它坐到键盘前跟着敲（轮询按键状态，不装键盘钩子，不记录按了什么）；有好几只时只有第一只敲，其他的照常溜达
- 鼠标互动：不理鼠标 / 跟着鼠标走 / 怕鼠标（被逼到墙角会从鼠标头上跳过去）
- CPU 越忙跑得越快
- 电脑热了会流汗：读 Windows 的「热区域信息」性能计数器（不用管理员权限），75°C 起偶尔滴汗，85°C 起不再乱跑，身边搬出一台小风扇对着自己吹（敲键盘时也吹），降到 82°C 以下收起来；读不到温度的电脑按 CPU 占用超过 80% 流汗
- 番茄钟：在 Clawd 头顶弹出像素风小窗，用滚轮 / 拖动 / 方向键选专注（5–90 分钟）和休息（1–30 分钟）时长，记住上次的选择
- 喝水提醒，用中文气泡提示
- 「设置 ▸ 功能开关」里可以把用不上的番茄钟、喝水提醒从菜单里藏起来，也可以关掉爬楼梯
- 最多同时 6 只；送走一只时它会随机来一段告别戏（吓一跳 / 发抖 / 大哭 😭 / 乱跑），然后从任务栏掉下去，其他 Clawd 会被吓得往后蹦
- 配饰：每只 Clawd 出生时随机戴上几件——头上（圆顶礼帽 / 小皇冠 / 毛线帽 / 头戴耳机）、脸上（单片眼镜 / 圆框眼镜 / 墨镜）、嘴上（八字胡 / 一字胡 / 大浓胡 / 山羊胡）、脖子上（围巾 / 蝴蝶结）、手上（咖啡 / 星巴克 / 薯片 / 手表），每个部位最多一件。右键它选「换装…」打开换装窗口：左边是它穿戴后的样子，右边每件配饰都画出来，点一下穿上、再点脱下，还能随机搭配或全部脱掉。戴着耳机打字会跟着点头，叠罗汉时会踩在帽子顶上
- 好几只在一起会互动：碰面先挥手打招呼，再头顶冒带气泡的像素表情（爱心、音符、笑脸、咖啡、灯泡、闪电……）轮流聊天；时不时玩一局追着跑、排成一队跟着走，或者一只跳到另一只头上叠罗汉；走到一起不会互相挡路；把一只扔到另一只身上会撞个满头星；撞墙晕了，旁边的会跑过来拍拍它；一只睡着了，旁边的会被传染打哈欠，有的干脆在旁边一起睡；送走一只时其他的会挥手、抹眼泪。你在打字或 Claude Code 在干活时，敲键盘的那只不参与，其他的照玩；电脑太热时都不玩。想看哪个随时右键「一起玩」点出来：打招呼聊天 / 追着玩 / 排队走 / 叠罗汉 / 晕倒求安慰
- 全屏游戏 / 视频时自动躲起来
- 托盘图标 + 右键菜单（常用动作在顶层，各种设置收在「设置」子菜单里），设置保存在 `HKCU\Software\ClawdPet`，可开机自启
- **联动 Claude Code**：Claude 干活时它敲键盘，等你批准权限时它挥手，回复完了它跳起来说「搞定啦！」

## 下载

在 [Releases](https://github.com/wlc114514/clawd-pet/releases/latest) 里：

- **Windows**：下载 `clawd.exe` 直接运行
- **macOS**（11 以上，M 系列和 Intel 都行）：下载 `Clawd-mac.zip`，解压后把 `Clawd.app` 拖进「应用程序」。app 没有苹果开发者签名，第一次打开会被拦：右键 ▸ 打开；较新的 macOS 上要去「系统设置 ▸ 隐私与安全性」点「仍要打开」

## 编译

### Windows

需要 [w64devkit](https://github.com/skeeto/w64devkit)（便携 gcc）。`build.bat` 会先找 PATH 里的 gcc，找不到就用 `%USERPROFILE%\w64devkit\bin`。

```bat
build.bat
```

产物是根目录的 `clawd.exe`。编译前先退出正在运行的桌宠，否则 exe 被占用无法覆盖。

### macOS

需要 Xcode 命令行工具（`xcode-select --install`），没有别的依赖。

```sh
cd mac
make        # 产物是 mac/build/Clawd.app
make run    # 编译并启动
```

`src/pet.c`（状态机、物理）和 `src/render.c`（像素画）两边共用，平台相关的部分分别在 `src/platform_win.c` 和 `mac/plat_mac.m` 里，所以两个平台的行为是一致的。

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

macOS 上把命令换成 app 里的可执行文件，其余一样：

```json
{
  "hooks": {
    "UserPromptSubmit":   [{ "hooks": [{ "type": "command", "command": "/Applications/Clawd.app/Contents/MacOS/Clawd", "args": ["--cc", "prompt"], "async": true }] }],
    "PostToolUse":        [{ "matcher": "*", "hooks": [{ "type": "command", "command": "/Applications/Clawd.app/Contents/MacOS/Clawd", "args": ["--cc", "tool"], "async": true }] }],
    "PostToolUseFailure": [{ "matcher": "*", "hooks": [{ "type": "command", "command": "/Applications/Clawd.app/Contents/MacOS/Clawd", "args": ["--cc", "tool"], "async": true }] }],
    "PermissionRequest":  [{ "matcher": "*", "hooks": [{ "type": "command", "command": "/Applications/Clawd.app/Contents/MacOS/Clawd", "args": ["--cc", "permission"], "async": true }] }],
    "Stop":               [{ "hooks": [{ "type": "command", "command": "/Applications/Clawd.app/Contents/MacOS/Clawd", "args": ["--cc", "stop"], "async": true }] }],
    "SessionEnd":         [{ "hooks": [{ "type": "command", "command": "/Applications/Clawd.app/Contents/MacOS/Clawd", "args": ["--cc", "end"], "async": true }] }]
  }
}
```

## 命令行

| 命令 | 作用 |
|---|---|
| `clawd.exe` | 启动桌宠（只允许一个实例） |
| `clawd` | 右键菜单「设置」里勾上「终端里可用 clawd 命令」后，新开的终端里直接敲 `clawd` 就能启动（参数照传）。它在 `%LOCALAPPDATA%\ClawdPet\bin` 放一个 `clawd.cmd`，只把这个目录加进用户 PATH；取消勾选会全部删掉 |
| `clawd.exe --cc <prompt\|tool\|permission\|stop\|end>` | 给运行中的桌宠发 Claude Code 事件 |
| `clawd.exe --sheet out.bmp` | 把所有姿势导出成一张图，调像素画用 |

macOS 上对应的是 `Clawd.app/Contents/MacOS/Clawd`，参数完全一样（没有 `.exe`）。「终端里可用 clawd 命令」会在 `~/Library/Application Support/ClawdPet/bin` 放一个 `clawd` 脚本，并往 `~/.zshrc`（有 `~/.bash_profile` 的话也加）追加一段带 `# >>> Clawd >>>` 标记的 PATH，取消勾选会连脚本带这段一起删掉。

## macOS 版说明

行为跟 Windows 版一样，只是把几个系统概念换了个说法：

| Windows | macOS |
|---|---|
| 站在任务栏上 | 站在 Dock 上（Dock 自动隐藏时站在屏幕底边） |
| 托盘图标 | 菜单栏图标 |
| 设置存在 `HKCU\Software\ClawdPet` | 设置存在 `NSUserDefaults`（`~/Library/Preferences/com.github.wlc114514.clawd-pet.plist`） |
| 开机自启写注册表 Run 键 | 开机自启写 `~/Library/LaunchAgents/com.github.wlc114514.clawd-pet.plist` |
| 读热区域温度 | 读系统报告的热压力（`NSProcessInfo.thermalState`）：正常、偏热、过热；读不到就按 CPU 占用流汗 |
| 全屏游戏时躲起来 | 窗口不加入全屏 Space，另外每秒两次检查有没有别的窗口盖满整块屏幕 |
| 用窗口消息发 Claude Code 事件 | 往 `~/Library/Application Support/ClawdPet/cc.sock` 发一个字节的 unix 数据报 |

「打字时陪你敲」靠轮询按键状态（`CGEventSourceKeyState`），和 Windows 版一样不装键盘钩子、不记录按了什么。macOS 需要「系统设置 ▸ 隐私与安全性 ▸ 辅助功能」里允许 Clawd，没允许的话它只是不动手，别的不受影响。

## 代码结构

| 文件 | 内容 |
|---|---|
| `src/render.c` | 像素画：Clawd 本体、键盘道具、小图标字形，放大到屏幕 |
| `src/pet.c` | 状态机和物理：走、跳、拖、睡、敲键盘、逃跑、爬楼梯、撞墙…… |
| `src/bubble.c` | 中文文字气泡（独立的分层窗口） |
| `src/picker.c` | 番茄钟的滚轮选时间小窗 |
| `src/wall.c` | 围墙：拖动画墙、每面墙的窗口、右键拆墙、保存 |
| `src/wardrobe.c` | 换装窗口 |
| `src/main.c` | 窗口、托盘、菜单、设置、番茄钟、全屏检测、Claude Code 事件 |
| `src/platform.h` | 核心用到的那几个平台接口（鼠标、空闲时间、屏幕工作区） |
| `src/platform_win.c` | 上面这些接口的 Win32 实现 |
| `tools/mkicon.c` | 编译时生成 exe 图标 |
| `mac/app.m` | macOS 的外壳：菜单栏图标和菜单、设置、传感、番茄钟、Claude Code 事件、`clawd` 命令 |
| `mac/pet_window.m` | 一只 Clawd 的窗口（透明、不抢焦点）和它爬的楼梯 |
| `mac/bubble.m` | 中文气泡窗口 |
| `mac/picker.m` | 番茄钟的滚轮选时间小窗 |
| `mac/walls.m` | 围墙（同上） |
| `mac/wardrobe.m` | 换装窗口（同上） |
| `mac/plat_mac.m` | 平台接口的 Cocoa 实现、屏幕坐标换算、位图转换 |
