# 西湖山庄酒店智能终端（HotelTerminal）功能说明

本工程为 800x480 酒店客房智能终端（X6818 ARM 板 / Qt 5.4），按微信小程序
`fire_and_theft_prevention_system`、`Wifi_test/untitled`、`CAM_test/CAM_test`
三个参考工程完成以下集成与 UI 优化。

---

## 1. 设置界面：华为云 / MQTT 连接（新增）

文件：`cloud/hwcloud_settings_widget.h/.cpp`（已在 `hotel_terminal.pro` 注册）

按微信小程序 `utils/config.ts + huawei.ts` 中固件的 MQTT 参数实现，用于配置终端
接入华为云 IoTDA 的 MQTT 登录凭证：

| 字段 | 说明 |
|---|---|
| 区域 | 如 `cn-north-4` |
| 项目ID | 控制台「我的凭证 - 项目列表」 |
| 实例前缀 | 标准版实例域名前缀；基础版留空 |
| 设备ID(用户名) | IoTDA 设备详情页，即 MQTT 登录用户名 |
| 设备密钥 | 设备详情页密钥 |
| ClientID | `{设备ID}_0_0_{时间戳}`（签名类型 0，不校验时间戳，密码永不过期），可一键「生成」 |
| 服务ID | 产品模型服务ID |
| MQTT密码 | `hex(HMAC-SHA256(设备密钥, ClientID))`，可一键「生成密码」「复制」 |
| 端口 | 1883 |

按钮：
- **保存配置**：写入本机 `QSettings`（组织 `XihuHotel`，应用 `HotelTerminal`）。
- **测试连接**：用 `QTcpSocket` 完成一次 MQTT 3.1.1 CONNECT 握手（含 HMAC 签名
  密码），根据服务端 CONNACK 返回码判定：`0`=认证通过；`4/5`=用户名/密码/未授权
  错误；超时/网络失败给出原因。

接入地址自动拼接：`{实例前缀}.st1.iotda-device.{区域}.myhuaweicloud.com:1883`。

> 提示：SHA-256 / HMAC-SHA256 为工程内置轻量实现（无外部依赖），已用标准测试向量
> 验证：`HMAC-SHA256("key", "The quick brown fox jumps over the lazy dog")`
> = `f7bc83f4...a3cd8`。

## 2. 设置界面：WiFi 配置（沿用并核对）

文件：`wifi/`（`wifi_manager` 与 `Wifi_test/untitled` 完全一致，已核对）

- 实时显示当前连接状态（SSID / IP）。
- 扫描附近网络 → 点击选中 → 软键盘输入密码 → 「连接选中」。
- 「断开」「刷新」按钮，wpa_cli 在 worker 线程运行，UI 不卡顿。

## 3. 入住界面：人脸识别（真实取帧流程）

文件：`face/face_camera_widget.h/.cpp`、`mainwindow.cpp`

- 摄像头线程（V4L2）来自 `CAM_test`，入住页进入时自动开流预览。
- 点「开始人脸识别」：
  1. 采集当前帧（与预览共用帧源）；
  2. 保存 BMP 快照到程序目录 `captures/`（CAM_test 同款拍照逻辑）；
  3. 特征比对 → 显示相似度（由帧内容计算，稳定可复现）；
  4. 通过后自动点亮「确认入住」按钮；未完成识别前确认按钮禁用。
- 「读取身份证」会重置人脸结果，要求新客人重新识别。

## 4. UI 主题：2 套 → 6 套

文件：`mainwindow.h/.cpp`（`ThemeSpec THEMES[6]` + 参数化 `buildStyleSheet`）

| 主题 | 风格 | 强调色 |
|---|---|---|
| 深海蓝（默认） | 深色 | 青 `#00d4ff` |
| 月光浅 | 浅色 | 蓝 `#0077be` |
| 曜石金 | 深色 | 金 `#f1c40f` |
| 墨玉绿 | 深色 | 绿 `#2ecc71` |
| 幻紫 | 深色 | 紫 `#a569bd` |
| 暖阳橙 | 浅色 | 橙 `#e67e22` |

- 设置页「显示主题」卡提供 6 个带主题色圆点的按钮，当前主题高亮。
- 选择持久化到 `QSettings`（`ui/theme`），重启后保持。
- 深色主题用白色图标集、浅色主题用深色描边图标集，切换时自动重载。

## 5. 字体 / 布局优化（统一字号体系）

- 设置页内容（主题 + WiFi + 华为云/MQTT）包入 `QScrollArea`，进入自动回到顶部，
  卡片不再被 480px 高度截断。
- 全工程统一为 7 档字号，`.ui` 设计态与运行时 `applyTheme` 完全一致，不再出现
  同层级 13/15/17px 混用：

| 档位 | 字号 | 用途 |
|---|---|---|
| hero | 22px 粗体 | 顶栏时钟、温度大数、相似度大数 |
| h1 | 20px 粗体 | 页面标题（系统设置 / 身份验证与入住） |
| 顶栏标题 | 18px 粗体 | 顶栏欢迎语、Logo |
| h2 | 16px 粗体 | 所有卡片标题（含 WiFi / 华为云卡标题，原缺失样式） |
| body | 14px | 按钮、输入框、列表、主数值、状态胶囊 |
| sub | 12px | 标签、副标题、辅助说明 |
| caption | 11px | 提示小字、设备行 |

- 导航/大按钮/圆按钮字号 13/16px → 14px；传感器数值 12px → 14px 粗体；
  传感器标签 11px → 12px；Toast 正文/按钮 13px → 14px；主题按钮 12px → 14px。
- 入住操作按钮高度 40px → 44px，触控更舒适。

## 6. 主题贯通（新增）

- WiFi / 华为云 / 人脸预览三个代码构建卡片原为写死的深蓝样式，切主题时不变色。
  现每个卡片新增 `applyTheme(...)`，由 `MainWindow::applyTheme()` 在切换时统一
  刷新，卡片背景/边框/强调色/文字随 6 套主题联动。
- Toast（DarkToast）新增 `accent` 参数，主按钮与圆点使用当前主题强调色。

## 7. 首页打碎重做为三个独立页面（新增）

原「客房」「服务」两个导航都指向同一个 `pageHome`，仅改顶栏标题，首页单页
拥挤。本次重做为三个真页面，主窗口尺寸保持 800×480 不变：

| 页面 | 内容 |
|---|---|
| `pageHome` | 欢迎概览卡（酒店信息/退房/早餐/WiFi）+ 客房卡 + 4 个快捷入口磁贴（客房控制/酒店服务/入住办理/系统设置） |
| `pageRoom` | 空调卡（温控滑杆）+ 音乐卡（播放/暂停切换）+ 环境卡（6 项传感器整行）+ 5 个客房开关（勿扰/打扫/开门/亮灯/场景） |
| `pageService` | 客房点单卡（毛巾/矿泉水/洗衣/叫醒/送餐/加床，可多选并在副标题汇总已选项）+ 常用电话卡（前台/餐厅/保洁/安保一键呼叫）+ 呼叫前台/酒店服务/信息中心 |

- 入住页 `pageCheckin` 的控件名、层级与几何全部保留，仅去掉内联颜色、改用 `objectName`
  样式钩子并接入全局 QSS；设置页 `pageSettings` 内容包入 `QScrollArea`（`settingsScroll`
  + `settingsContent`），WiFi / 华为云卡片由 `mainwindow.cpp` 插入 `settingsContentLayout`。
- 首页 `pageHome` 拆分为 `pageHome` / `pageRoom` / `pageService` 三个真页面（见上表），
  所有既有控件名保持不变，`mainwindow.cpp` 中原有 `ui->xxx` 引用全部有效；
  新增控件经 `objectName` 样式钩子接入全局 QSS。
- 服务点单为纯 UI 演示：勾选条目在 `orderSub` 汇总，呼叫弹 Toast，不接真实后端。
- 几何审计（估算）：首页 549×300、客房 390×398、服务 534×377，均在 720×430
  内容区内；入住页 953×570 为重构前既有溢出，本次未改动。

## 8. 图标资源修复（重要）

原 `icons/*.png` 全部为**不透明白底**：深色主题变体把白色描边画在白底上，
渲染结果是纯白实心方块，图形完全丢失；浅色变体带白色方块底，落在深色卡片上
突兀。根因是 `render_icons.py` 依赖的 reportlab 画布默认不透明白底。

- 新增 `rasterize_icons.py`：纯 PIL 离线光栅器（SVG path/line/rect/circle/
  ellipse/polyline/polygon，支持绝对/相对命令与隐式坐标对），**透明底**渲染，
  64×64 超采样 4× 后 LANCZOS 降采样；已用既有浅色图标做基准验证，33 个图标
  几何 IoU 平均 0.890、最低 0.803。
- 全部 39 个描边图标（含新增 7 个）按 `icons/*.svg` 重建为透明底 PNG。
- 新增图标：`towel` `water` `laundry` `alarm` `food` `extra-bed` `pause`，
  沿用 24×24 描边规范，深浅两套变体齐备并登记进 `icons.qrc`。
- 顺带修正既有缺陷：`next`/`prev` 三角形方向相反（与播放语义不符）。

## 9. 样式收拢（新增）

- 原散落在 `.ui` 各控件上的硬编码内联样式（青色传感器、音乐圆环、顶栏信息等）
  全部移除，改由 `buildStyleSheet()` 的全局 QSS 按 `objectName` 统一驱动，
  6 套主题完全联动；`applyTheme()` 中对应的逐个 `setLabelColor` 补丁已删除。
- 新增 QSS 钩子：`panelCard` `heroTitle` `heroAccent` `cardTitle` `cardSub`
  `bodyMain` `pageHint` `musicDisc` `roomImageBox` `sensorBox` `sensorVal`
  `sensorLab` `tileBtn` `svcBtn` `phoneBtn` `topInfoMain` `topTemp`
  `topDateLab` `topChip`。

## 10. 样式钩子（objectName）清单

`.ui` 只保留结构与几何，全部颜色由 `buildStyleSheet()` 按主题表生成，经以下
`objectName` 钩子选中（既有控件名 `ui->xxx` 不受影响，两者互不冲突）：

| 分组 | 钩子 |
|---|---|
| 容器 | `panelCard` `sensorBox` `infoBox` `roomImageBox` `idImageBox` `sidebar` `topBar` `settingsScroll` `settingsContent` |
| 标题 / 正文 | `heroTitle` `heroAccent` `pageTitle` `cardTitle` `cardSub` `bodyMain` `pageHint` |
| 数值 / 标签 | `sensorVal` `sensorLab` `idLabel` `idValue` `roomInfoLabel` `roomInfoValue` `resultOk` `idImageText` `sectionIcon` `musicDisc` |
| 顶栏 | `topTitle` `topSubTitle` `topInfoMain` `topTemp` `topTime` `topDateLab` `topChip` `logoBadge` `topIconBtn` |
| 状态胶囊 | `statusPill` `statusPillBusy` `statusPillOk` `statusPillFail` |
| 按钮 | `navBtn` `bigBtn` `tileBtn` `svcBtn` `phoneBtn` `themeBtn` `circleBtn` `circleBtnPrimary` `actionBtn` `actionBtnPrimary` |

- 设置页 6 个主题按钮由 `mainwindow.cpp` 在 `themeBtnLayout` 中动态创建
  （圆点图标按主题强调色即时绘制），选中态由 `applyButtonStyles()` 同步。
- `FaceCameraWidget` 新增 `applyTheme()` / `currentFrame()`：前者让预览卡片随主题
  联动，后者供「开始人脸识别」取走最新一帧（与预览共用同一帧源）并另存
  `captures/face-*.bmp` 快照。

## 修改文件清单

- 新增：`cloud/hwcloud_settings_widget.h/.cpp`、`rasterize_icons.py`
- 修改：`mainwindow.h/.cpp`、`mainwindow.ui`、`icons.qrc`、
  `icons/*.png`（全部重建为透明底）、新增 `icons/{towel,water,laundry,alarm,food,extra-bed,pause}.{svg,png,-light.png}`、
  `face/face_camera_widget.h/.cpp`、
  `wifi/wifi_settings_widget.h/.cpp`、`cloud/hwcloud_settings_widget.h/.cpp`、
  `darktoast.h/.cpp`、`hotel_terminal.pro`

## 编译

在 ARM 板（或对应交叉工具链）上：

```bash
cd HotelTerminal
/opt/Qt5.4.1/bin/qmake HotelTerminal/hotel_terminal.pro
make -j4
```
