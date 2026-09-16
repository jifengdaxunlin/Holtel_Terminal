# 酒店 · 智能终端（Xihu Hotel Smart Terminal）

基于 Qt 5 的酒店客房智能控制终端，运行于 7 英寸 ARM 触控面板（X6818），提供房间状态、环境控制、入住登记、云服务接入等一体化操作界面。

> 各模块的功能细节、主题体系、样式钩子清单等开发记录见 [README-功能说明.md](README-功能说明.md)。

## 功能特性

- **房间控制**：空调温度调节、灯光开关、场景模式、勿扰（DND）与清扫状态
- **门禁**：按住开门、房卡认证
- **入住登记**：读卡、人脸识别（V4L2 摄像头 + LBPH 特征比对）、确认入住、打印小票
- **客房服务**：点单（毛巾 / 矿泉水 / 洗衣 / 叫醒 / 送餐 / 加床）、常用电话一键呼叫
- **华为云 IoTDA**：MQTT 接入配置、HMAC-SHA256 签名、一键生成密码与连接测试
- **WiFi 配置**：扫描 / 连接 / 断开网络、软键盘输入密码
- **NTP 时间同步**：开机自动对时
- **界面**：6 套主题（深海蓝 / 月光浅 / 曜石金 / 墨玉绿 / 幻紫 / 暖阳橙）、顶部实时时钟、Toast 轻提示
- **导航**：首页 / 客房 / 服务 / 入住 / 设置 多页面切换

## 技术栈

| 项目 | 说明 |
|------|------|
| 语言 | C++11 |
| 框架 | Qt 5（`core` `gui` `svg` `widgets` `network` `concurrent`） |
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

> 程序启动时强制北京时间（UTC+8）与 UTF-8 locale，避免嵌入式系统默认 UTC / C locale 导致界面时钟、日期、MQTT ClientID 时间戳慢 8 小时及中文文件名乱码。

## 目录结构

```
Holtel_Terminal/
├── main.cpp                  # 入口：全屏/窗口模式、时区与 locale 初始化
├── mainwindow.cpp/.h/.ui     # 主窗口、业务逻辑与 UI（800×480）
├── darktoast.cpp/.h          # Toast 轻提示组件
├── systemintegration.cpp/.h  # 系统集成
├── cloud/                    # 华为云 IoTDA MQTT（客户端 + 设置页）
├── wifi/                     # WiFi 管理、软键盘、设置页
├── face/                     # 人脸识别（摄像头线程、预览、LBPH 引擎）
├── time/                     # NTP 时间同步
├── deploy/                   # 部署脚本（run_hotel_terminal.sh）
├── hotel_terminal.pro        # qmake 工程文件
├── icons.qrc / icons/        # 图标资源清单与图标（.svg 源 + .png 渲染图）
├── faces_model.json          # 人脸识别模型配置
├── preview.html / preview_bg/ # UI 预览与主题背景预览
├── *.py                      # 图标渲染 / 模型校验等辅助脚本
└── LICENSE
```

## 许可

MIT License，详见 [LICENSE](LICENSE)。
