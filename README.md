# TrafficMonitorPlugins

这是[TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)官方插件仓库（[zhongyang219/TrafficMonitorPlugins](https://github.com/zhongyang219/TrafficMonitorPlugins)）的一个 fork。

> **关于本仓库**：本仓库在保持原有功能的基础上，对**全部 10 个插件及 utilities 共享工具库**进行了一轮系统性代码审查与修复（共 89 项，详见[修复文档](修复文档.md)），并对 **KeyboardIndicator（键盘指示器）** 插件做了较大的功能增强。各插件均有改动，并非与上游完全一致。

## 插件下载

请点击以下链接转到插件下载页面：

[TrafficMonitor 插件下载](./download/plugin_download.md)

## 本仓库的更新

### KeyboardIndicator（键盘指示器）—— 功能增强

在原插件（用于显示 Caps Lock、Num Lock、Scroll Lock 状态）的基础上进行了功能增强，当前版本 **V1.10**，主要更新内容：

* **实时按键显示**：新增低级键盘钩子模块，在独立线程中实时捕获全系统按键，任务栏可实时显示当前按下的按键组合（如 `Ctrl+Shift+A`），按住时高亮显示。
* **按键松开后延时显示**：按键松开后可在设定时长内（默认 1000 毫秒）以灰色继续显示，避免快速敲击时看不到按键内容。
* **预留显示宽度**：按键显示区支持固定预留宽度，避免按键按下瞬间因主程序宽度重算延迟而被遮挡；也可设置为按内容自适应。
* **DPI 自适应**：绘制与文本测量按绘图 DC 的实际 DPI 自适应，修复高 DPI 屏幕下的显示偏差。
* **设置界面优化**：新增上述功能对应的设置项（显示开关、松开后显示时长、预留宽度等），并优化了设置对话框布局。

### 全部插件及共享库 —— 代码审查修复（FIX-001~089）

基于对全部 10 个插件及 utilities 共享工具库（约 1.6 万行 C++）的代码审查，按 P0（崩溃/数据损坏）、P1（功能错误/资源泄漏/数据竞争）、P2（边缘情况/代码质量）三级共修复 89 项问题，完整清单见[修复文档](修复文档.md)。主要包括：

* **崩溃类（P0）**：修复 DateTime 采样文本悬空指针、TextReader HTML 解析下溢越界、Stock 后台线程锁外写共享数据等可导致宿主进程崩溃的问题。
* **数据竞争与死锁**：为 TextReader、Stock、Weather 的后台线程共享数据补齐互斥锁；修复 Stock 持锁 `SendMessage` 与 UI 线程互等死锁。
* **资源管理**：修复多处 GDI+ 未初始化/未配对释放、HICON/定时器泄漏、`LoadImage` 失败结果被永久缓存、网络会话泄漏等。
* **功能错误**：修复 IpAddress 数据永不刷新、Weather 日期解析无长度守卫、HardwareMonitor 单位换算系数错误、传感器空值导致监控项冻结等。
* **兼容性修复**：`GetURL` 网络读取不再依赖系统区域设置，正确处理 BOM 与 UTF-16 编码。

所有修复均已通过编译验证：本机 VS2022 Release|x64 编译 11 个项目零错误，并通过 GitHub Actions CI（windows-2022）完成全部插件（含 C++/CLI 的 HardwareMonitor）的编译验证。

## 插件使用说明

根据TrafficMonitor的版本（x86为32位，x64为64位）选择对应版本的插件，下载后解压可得到dll文件，下载后将插件dll放到TrafficMonitor程序所在目录下的`plugins`目录下：

![image-20221013203124953](images/image-20221013203124953.png)

重新启动TrafficMonitor后可以在“选项”——“常规设置”——“插件管理”中看到所有的插件：

![image-20221013203353499](images/image-20221013203353499.png)

要使插件项目显示到任务栏中，请在任务栏窗口上点击鼠标右键，选择“显示设置”。

![image-20221013203527593](images/image-20221013203527593.png)

![image-20221013203621714](images/image-20221013203621714.png)

此时，“显示设置”中会显示已加载的插件项目，勾选你希望显示在任务栏上的项目，点击确定即可。

关于更多插件使用的详细说明，请参考以下链接：

[插件功能 · zhongyang219/TrafficMonitor Wiki (github.com)](https://github.com/zhongyang219/TrafficMonitor/wiki/插件功能)

## 如何开发插件

关于如何开发TrafficMonitor，请参考以下链接：

[插件开发指南 · zhongyang219/TrafficMonitor Wiki (github.com)](https://github.com/zhongyang219/TrafficMonitor/wiki/插件开发指南)

## 插件的提交

你可以向我发送电子邮件来提交你开发的插件，并附上插件简介和下载地址。也可以直接向此仓库提交pull request来更新插件下载页面：[plugin_download.md](download/plugin_download.md) 和 [plugin_download_en.md](download/plugin_download_en.md)，按照文档原来的格式添加插件介绍和下载链接。同时更新一下[plugins_version.xml](plugins_version.xml)文件，这个文件用于在TrafficMonitor的“插件管理”界面显示插件是否有更新，在`plugins_version.xml`中添加你插件的文件名和最新的版本号，当插件有更新时也要更新一下这个文件里的版本号。
