# 西湖酒店 · 智能终端（Xihu Hotel Smart Terminal）

基于 Qt 5 的酒店客房智能控制终端，运行于 7 英寸 ARM 触控面板（X6818），提供房间状态、环境控制、入住登记等一体化操作界面。

## 功能特性

- **房间控制**：空调温度调节、灯光开关、场景模式、勿扰（DND）与清扫状态
- **门禁**：按住开门、房卡认证
- **入住登记**：读卡、人脸识别、确认入住、打印小票
- **服务**：呼叫服务、信息查询、影音播放
- **界面**：暗色 / 亮色双主题、顶部实时时钟、Toast 轻提示
- **导航**：首页 / 房间 / 入住 / 服务 / 设置 多页面切换

## 技术栈

| 项目 | 说明 |
|------|------|
| 语言 | C++11 |
| 框架 | Qt 5（`core` `gui` `svg` `widgets`） |
| 目标系统 | Ubuntu 16.04 ARM Linux |
| 目标硬件 | 7 英寸 X6818 触控面板，**800×480** 分辨率 |

## 构建

使用 qmake 构建：

```bash
qmake hotel_terminal.pro
make
```

> 说明：项目在 ARM 板上使用系统自带的 Qt 5.4.1 编译。SVG 图标仅作为可编辑源文件，运行时加载预渲染的 PNG——因为 ARM 板上的 QtSvg 不可靠（会报 `Cannot open file ':/icons/xxx.svg'`）。

## 运行

```bash
./hotel_terminal           # 全屏模式（部署到面板上）
./hotel_terminal --windowed   # 窗口模式（桌面调试）
```

## 目录结构

```
Holtel_Terminal/
├── main.cpp              # 入口：全屏/窗口模式判断
├── mainwindow.cpp/.h     # 主窗口与业务逻辑
├── mainwindow.ui         # UI 布局（800×480）
├── darktoast.cpp/.h      # Toast 轻提示组件
├── hotel_terminal.pro    # qmake 工程文件
├── icons.qrc             # 图标资源清单
├── icons/                # 图标（.svg 源 + .png 渲染图）
├── render_icons.py       # 渲染图标（SVG → PNG）
├── resize_ui.py          # 调整 UI 尺寸（1280×800 → 800×480）
├── make_ico.py           # 生成 Windows 应用图标 .ico
├── preview.html          # UI 预览
└── LICENSE
```

## 许可

MIT License，详见 [LICENSE](LICENSE)。
