<div align="center">

# TrafficMonitor Plugins

**[TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 官方插件仓库的功能增强 Fork**

[![Build](https://img.shields.io/github/actions/workflow/status/Arimayuki03/TrafficMonitorPlugins/build.yml?branch=main&label=Build&logo=github)](https://github.com/Arimayuki03/TrafficMonitorPlugins/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/Arimayuki03/TrafficMonitorPlugins?label=Release&logo=github)](https://github.com/Arimayuki03/TrafficMonitorPlugins/releases/latest)
[![Platform](https://img.shields.io/badge/platform-Windows-blue?logo=windows)](https://github.com/Arimayuki03/TrafficMonitorPlugins)
[![Language](https://img.shields.io/badge/language-C%2B%2B%20%2F%20MFC-00599C?logo=cplusplus&logoColor=white)](https://github.com/Arimayuki03/TrafficMonitorPlugins)
[![License](https://img.shields.io/github/license/Arimayuki03/TrafficMonitorPlugins)](LICENSE)
[![Upstream](https://img.shields.io/badge/upstream-zhongyang219%2FTrafficMonitorPlugins-8A2BE2)](https://github.com/zhongyang219/TrafficMonitorPlugins)

[插件下载](#-插件下载) · [本仓库的更新](#-本仓库的更新) · [安装与使用](#-安装与使用) · [插件开发](#-插件开发) · [参与贡献](#-参与贡献)

**简体中文** | English 文档见各插件下载页

</div>

---

## 📦 插件下载

**[前往插件下载页面 →](./download/plugin_download.md)**（[English](./download/plugin_download_en.md)）

[GitHub Releases](https://github.com/Arimayuki03/TrafficMonitorPlugins/releases) 提供本仓库构建的最新插件包（x64 / Release）。

## 📋 插件列表

本仓库包含以下插件（均为 x64，需配合 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 使用）：

| 插件 | 版本 | 说明 |
|---|---|---|
| KeyboardIndicator 键盘指示器 | V1.14 | 显示 Caps Lock / Num Lock / Scroll Lock 状态；本仓库增强版支持**实时按键显示**与**按键统计**，显示项顺序与颜色可自定义 |
| HardwareMonitor 硬件监控 | V1.0.1 | 基于 LibreHardwareMonitor 显示 CPU / GPU / 内存 / 硬盘等传感器信息 |
| Stock 股票 | V1.14 | 新浪接口实时股票行情 |
| Weather / WeatherPro 天气 | V1.03 | 天气显示，支持中国城市手动选择 |
| DateTime 时间日期 | V1.00 | 任务栏日期与时间 |
| IpAddress IP 地址 | V1.00 | 显示本地 IP 地址 |
| Battery 电池电量 | V1.03 | 电池电量、剩余电量与电压信息 |
| TextReader 文本阅读器 | V1.02 | 在任务栏阅读文本文件，支持章节、书签与自动翻页 |
| LoudnessMeter 响度计 | V1.00 | 显示当前系统音量 |
| PomodoroTimer 番茄钟、PowerMonPlugin 功率监控等 | — | 由第三方作者维护，见[下载页](./download/plugin_download.md) |

## ✨ 本仓库的更新

本仓库是 [zhongyang219/TrafficMonitorPlugins](https://github.com/zhongyang219/TrafficMonitorPlugins) 的 fork，在保持原有功能的基础上，对**全部 10 个插件及 utilities 共享工具库**进行了系统性代码审查与修复，并对 **KeyboardIndicator（键盘指示器）** 插件做了较大的功能增强。各插件均有改动，并非与上游完全一致。

### KeyboardIndicator（键盘指示器）—— 功能增强 V1.14

在原插件（用于显示 Caps Lock、Num Lock、Scroll Lock 状态）的基础上进行了功能增强：

- **实时按键显示**：新增低级键盘钩子模块，在独立线程中实时捕获全系统按键，任务栏可实时显示当前按下的按键组合（如 `Ctrl+Shift+A`），按住时高亮显示。
- **按键松开后延时显示**：按键松开后可在设定时长内（默认 1000 毫秒）以灰色继续显示，避免快速敲击时看不到按键内容。
- **区域宽度（最小宽度）**：按键区、计数区、KPM 区均可设置宽度——区域至少保持该宽度（任务栏宽度不随内容长短抖动），内容更长时自动扩展，0 表示完全按内容自适应。**V1.12 修复**：此前计数区/KPM 区的宽度设置只用于"占位"，不会改变实际画出的框宽，看起来"设置无效"；现在宽度会真正作用于显示框。
- **计数统计范围（V1.14 新增）**：任务栏的累计计数可切换显示 **累计（全部）/ 今日 / 近7天 / 近30天 / 自定义天数**（自定义 1~90 天，含今日，按自然日计算）；「每日目标达标变绿」只在统计范围为「今日」时生效。
- **显示项顺序**：Caps / Num / Scroll / 累计计数 / KPM / 实时按键 六个显示项在任务栏中**自左向右的顺序可调**（设置对话框「显示项」页用上移/下移调整，未显示的项目标注"已隐藏"）。
- **显示项颜色**：每个显示项可**单独指定颜色**（作用于该项目的点亮状态：文字与边框；也可一键恢复默认色）。锁定键未开启、按键已松开时仍显示为灰色，保持状态可辨；计数达到每日目标时仍显示绿色。**V1.13 修复**：设置页列表按颜色预览时，若颜色为白色等与背景接近的浅色会自动回退为默认文字色，避免整行看不见（选中行同样不套用自定义色，交由系统高亮绘制）。
- **按键统计（V1.11 新增）**：
  - 可选开启的**按键统计功能**，统计每个按键的累计按下次数与总按键数（自动重复与脚本/远程注入的按键不计入，左右修饰键归并计数）。
  - 统计数据**持久化保存**在插件配置目录的 `KeyboardIndicator.ini` 中：累计总数、当日各按键明细与最多 90 天的**每日按键明细**历史，跨天自动归档。
  - 任务栏可**显示累计总次数**，计数框旁可选显示**当前打字速度 KPM**（独立开关，60 秒滑动窗口统计）；鼠标提示中显示"累计/当日"摘要。
  - 设置对话框采用**页面切换式设计**：「显示设置」页集中显示开关与参数；「按键统计」页提供统计开关、每日目标、摘要与打字速度（当前/峰值/平均 KPM）；「显示项」页调整各显示项的顺序与颜色。
  - 统计页内置**按键排行**（今日/近7天/近30天/全部 时间范围筛选，含排名、次数、占比）与**每日统计**两个页签；排行页顶部有**键位覆盖图**（60 键完整小键盘布局，覆盖修饰键、数字符号排、方向键等，按过的键按次数深浅高亮、随筛选范围联动）；每日统计页有**迷你柱状图**（最近 30 天趋势，柱顶数值、柱下日期，达标日绿色柱、目标虚线）；**双击每日统计某天可跳转查看该天明细**。
  - 支持**导出 CSV**（每日统计含明细，UTF-8 BOM，Excel 直开）与**清零统计**；任务栏计数达到每日目标后**变绿色**。
- **DPI 自适应**：绘制与文本测量按绘图 DC 的实际 DPI 自适应，修复高 DPI 屏幕下的显示偏差。
- **设置界面优化**：新增上述功能对应的设置项（显示开关、松开后显示时长、区域宽度、顺序与颜色等），并优化了设置对话框布局。

### 全部插件及共享库 —— 代码审查修复（FIX-001~114）

基于对全部 10 个插件及 utilities 共享工具库（约 1.6 万行 C++）的五轮代码审查与交叉验证，按 **P0（崩溃/数据损坏）→ P1（功能错误/资源泄漏/数据竞争）→ P2（边缘情况/代码质量）** 三级共修复 200 余项问题，完整清单见[修复文档](修复文档.md)。主要包括：

- **崩溃类（P0）**：DateTime 采样文本悬空指针、TextReader HTML 解析下溢越界、base64 误判普通文本与越界读、IniHelper 单引号值堆破坏、Stock 主窗口点击崩溃、HardwareMonitor 老宿主通知崩溃与跨线程重建竞态等。
- **死锁与数据竞争（P1）**：为 TextReader、Stock、Weather、HardwareMonitor 等的后台线程共享数据补齐互斥锁；修复 Stock 持锁 `SendMessage` 与 UI 线程互等死锁；tooltip / 数值文本跨线程写读全部改为“锁内拷贝副本”或值快照。
- **资源管理**：修复多处 GDI+ 未初始化/未配对释放、HICON/定时器泄漏、网络会话泄漏等。
- **功能正确性**：IpAddress 数据永不刷新、HardwareMonitor 单位换算系数错误与 LoadConfig 数值钳制、Weather 日期解析无长度守卫、TextReader 章节定位 off-by-one 等。
- **显示宽度（P1，V1.12）**：键盘指示器计数区/KPM 区/按键区的宽度设置此前只影响测量值、不影响实际绘制框（表现为“设置宽度无效”），改为测量与绘制共用同一份区域计划，宽度按“区域最小宽度”生效。
- **兼容性**：`GetURL` 网络读取不再依赖系统区域设置，正确处理 BOM 与 UTF-16 编码。

### ✅ 验证

- 本机 VS2022 Release|x64 全解决方案 Rebuild **12/12 项目零错误**（含 C++/CLI 的 HardwareMonitor），编译警告与整改前基线完全一致（零新增）。
- 通过 GitHub Actions CI（windows-2022）完成全部插件编译验证，构建产物见 [Releases](https://github.com/Arimayuki03/TrafficMonitorPlugins/releases)。
- base64 / IniHelper 等关键模块以真实源码编译执行 **58/58** 行为测试用例。
- 键盘指示器显示宽度/顺序/颜色：直接加载插件 DLL 用内存 DC 调用 `GetItemWidthEx` / `DrawItem`，扫描绘制结果的边框像素做行为级实测，并用修复前（HEAD）与修复后的 DLL 对照验证（计数宽度 60：修复前框宽 48 px 不变 → 修复后 90 px；KPM 宽度 200：39 px → 300 px），同时验证顺序调整、每个显示项取色与非法配置回退。

## 🔧 编译

- **环境**：Visual Studio 2022（v143 工具集，需安装 MFC 组件）；HardwareMonitor 额外需要 .NET Framework 4.7.2 目标包与 C++/CLI 支持。
- **步骤**：打开 `TrafficMonitorPlugins.sln`，选择 `Release | x64`，生成解决方案即可；产物输出至 `bin/x64/Release/`。
- 也可以直接fork本仓库后使用 GitHub Actions（`.github/workflows/build.yml`）云端编译。

## 📖 安装与使用

1. 根据 TrafficMonitor 的版本（x86 为 32 位，x64 为 64 位）选择对应版本的插件，下载后解压得到 dll 文件。
2. 将插件 dll 放到 TrafficMonitor 程序所在目录下的 `plugins` 目录（没有则新建）：

   ![plugins 目录](images/image-20221013203124953.png)

3. 重新启动 TrafficMonitor，在 **“选项” → “常规设置” → “插件管理”** 中可以看到所有插件：

   ![插件管理](images/image-20221013203353499.png)

4. 要使插件项目显示到任务栏中，请在任务栏窗口上点击鼠标右键，选择 **“显示设置”**：

   ![显示设置](images/image-20221013203527593.png)

   ![勾选插件项目](images/image-20221013203621714.png)

5. 此时“显示设置”中会显示已加载的插件项目，勾选你希望显示在任务栏上的项目，点击确定即可。

关于更多插件使用的详细说明，请参考 [插件功能 · TrafficMonitor Wiki](https://github.com/zhongyang219/TrafficMonitor/wiki/插件功能)。

## 💡 插件开发

关于如何开发 TrafficMonitor 插件，请参考：

- [插件开发指南 · TrafficMonitor Wiki](https://github.com/zhongyang219/TrafficMonitor/wiki/插件开发指南)
- 本仓库的 [PluginTemplate](Plugins/PluginTemplate) 项目可作为插件工程模板，[PluginTester](PluginTester) 可用于脱离主程序独立调试插件。

## 🤝 参与贡献

欢迎通过 Pull Request 或 Issue 参与贡献：

- 修复插件缺陷或增强功能：直接修改对应插件目录并提交 PR。
- 提交新插件：向 [plugin_download.md](download/plugin_download.md) 和 [plugin_download_en.md](download/plugin_download_en.md) 按原有格式添加插件介绍与下载链接，同时更新 [plugins_version.xml](plugins_version.xml)（该文件用于 TrafficMonitor “插件管理”界面的更新检查）。
- 上游插件提交插件请发送邮件至 zhongyang219@hotmail.com，并附上插件简介与下载地址。

## 📄 许可证

本项目沿用上游 [LICENSE](LICENSE)。

## 🙏 致谢

- [zhongyang219/TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) —— 优秀的任务栏流量监控工具
- [zhongyang219/TrafficMonitorPlugins](https://github.com/zhongyang219/TrafficMonitorPlugins) —— 上游官方插件仓库
- 所有插件的原作者：[CListery](https://github.com/CListery)、[Haojia521](https://github.com/Haojia521)、[AzulEterno](https://github.com/AzulEterno) 等
