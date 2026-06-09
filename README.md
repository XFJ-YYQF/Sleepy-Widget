# Sleepy Widget

基于 ESP8266 的 [sleepy-project/sleepy](https://github.com/sleepy-project) 硬件状态指示器。通过 OLED 屏幕实时显示当前状态，支持按键切换、网页配置、GPIO 电平联动和自动息屏。

~~PCB 及固件将在测试完毕后逐个开源~~

**固件感觉差不多了，PCB还没打样回来，预计还需要修改一次**

---

## 功能

- **OLED 实时显示** 本机 IP 与当前 Status 名称（支持中文）
- **网页配置后台** 访问设备 IP 即可配置 WiFi、服务器、密钥、按键行
- **多种触发方式** 单击 / 双击 / 长按，支持常开 (NO)、常闭 (NC) 开关和电平检测
- **GPIO13 电平联动** 检测外部电压，自动将状态设为「活着」或「似了」
- **自动息屏** 无操作超时后关闭屏幕；WiFi 断线或服务器异常时强制保持亮屏
- **HTTPS 支持** 自动识别服务器地址协议，无需额外配置
- **配网 AP 模式** 无配置时自动开热点，浏览器连接即可完成首次配网
- **服务端状态联动** 检测到服务器端状态被外部修改时自动唤醒屏幕

---

## 硬件需求

| 元器件 | 购买渠道 | 参考价格 |
|------|------|------|
| PCB | JLC | 0¥ |
| ESP8266（12F） | TB | 5.6¥ |
| 0.96" OLED模组 IIC | TB | 3.5¥ |
| AMS1117 成品模块 | TB | 0.945¥ |
| Type-C 6P 母座 | TB | 0.178¥ |
| 微动限位开关 | PDD | 1.08¥ |
| 米家 MHCB07P 模块（可选） | XY | 5¥ |
| 10k 电阻 | PDD | 0.06¥ |
| 5.1k 电阻（可选） | PDD | 0.06¥ |
| 24k 电阻（可选） | PDD | 0.06¥ |

元器件不多，应该不需要 BOM 表

支持[嘉立创](https://www.jlc.com)，建议在[立创商城](https://www.szlcsc.com/)购买元器件

**注意：打样PCB请选择 1mm 板厚！！！**

---

## 接线

```
OLED
  VCC  →  3.3V
  GND  →  GND
  SDA  →  D2 (GPIO4)
  SCL  →  D1 (GPIO5)

主按钮（切换状态）
  一脚  →  D5 (GPIO14)
  另一脚 →  GND
  （使用内部上拉，无需外接电阻）

重置按钮（长按 3s 清除 WiFi 配置）
  一脚  →  D6 (GPIO12)
  另一脚 →  GND

GPIO13 通道 2（电平检测模式）
  信号  →  D7 (GPIO13)
  GND   →  通过 10kΩ 电阻接 GND（外部下拉，必须）
```

> **注意**：D1 (GPIO5) 和 D2 (GPIO4) 已被 OLED 占用，不可用于其他功能。

---

## 依赖库

在 Arduino IDE **工具 → 管理库** 中搜索安装：

| 库 | 版本 |
|----|------|
| U8g2 | 最新版 |
| ArduinoJson | **6.x**（不兼容 7.x） |

ESP8266 板包：工具 → 开发板管理器 → 搜索 `esp8266 by ESP8266 Community` 安装。

---

## 烧录设置

| 选项 | 推荐值 |
|------|--------|
| 开发板 | NodeMCU 1.0 / LOLIN(Wemos) D1 mini |
| Flash Size | **4MB (FS:2MB OTA:~1019KB)** |
| Upload Speed | 115200 |

---

## 首次使用

1. 烧录后设备自动进入 AP 配网模式
2. 手机或电脑连接 WiFi：**`Sleepy-ESP8266`**（无密码）
3. 浏览器访问 **`http://192.168.4.1`**
4. 填写以下信息后点击 **Save & Reboot**：

| 字段 | 说明 |
|------|------|
| WiFi SSID | WiFi 名称（可访问外网） |
| WiFi Password | WiFi 密码 |
| Server URL | sleepy 服务器地址，如 `http://example.com`（末尾不加 `/`） |
| Secret | 服务器的 `SLEEPY_SECRET` |

5. 设备重启后连接 WiFi，OLED 显示 IP 地址和当前状态

---

## 网页后台

连接同一局域网后，浏览器访问设备 IP 即可打开配置页面。

### Current Status
显示当前状态名称、设备 IP 和服务器地址。右上角 `!` 表示最近一次请求失败。

### Set Status
选择状态后点击 Apply 立即生效（GPIO13 电平检测模式下此栏隐藏，由硬件控制）。

**Button / Switch**

| 字段 | 说明 |
|------|------|
| Switch Type | 开关类型，见下方说明 |
| Trigger Mode | 触发方式：单击 / 双击 / 长按 |
| Click Duration | 单击最大时长（ms），超过此值视为长按 |
| Double Click Interval | 双击间最大间隔（ms） |
| Long Press Duration | 长按触发时长（ms） |
| Screen Timeout | 息屏等待时间（秒） |

**Reset WiFi**：点击后清除 WiFi 配置并重启进入 AP 模式。

---

## Switch Type 说明

### Normally Open (NO) — 常开
默认模式。开关/继电器未触发时断开，触发时接通至 GND。适用于：
- 普通点动按钮
- 常开继电器

### Normally Closed (NC) — 常闭
开关/继电器未触发时接通至 GND，触发时断开。适用于：
- 常闭按钮
- 常闭继电器

### GPIO13 Level Sense — 电平检测
通过 D7 (GPIO13) 检测外部 0\~3.3V 电压，自动控制状态：

| GPIO13 电平 | 触发动作 |
|-------------|----------|
| HIGH（有电压） | 设置为「Alive Status」所选状态 |
| LOW（无电压） | 设置为「Dead Status」所选状态 |

**使用要求**：
- 若使用本项目配套 PCB，请注意根据模式短接功能选择点位
- GPIO13 必须接 **10kΩ 下拉电阻至 GND**，否则引脚浮空导致误触发
- 在网页 Configuration → Alive Status / Dead Status 中选择对应的状态 ID
- 此模式下 Set Status 栏隐藏，状态完全由硬件电平控制
- 主按钮仍可正常使用（手动切换或唤醒息屏）
- 默认取十次结果，触发两次改变状态，有需要可以在代码中修改

---

## 息屏说明

| 状态 | 屏幕行为 |
|------|----------|
| WiFi 未连接 | **强制亮屏** |
| 服务器请求失败 | **强制亮屏** |
| 正常运行中 | 无操作超过 Screen Timeout 秒后自动息屏 |

以下操作会**唤醒屏幕**并重置息屏计时器：
- 按下主按钮
- GPIO13 检测到电平变化
- 服务端状态被外部修改
- 从错误状态恢复正常

---

## 按钮操作

| 操作 | 功能 |
|------|------|
| 主按钮（触发方式取决于配置） | 循环切换到下一个 Status |
| 重置按钮长按 3s | 清除 WiFi 配置，重启进入 AP 配网模式 |

---

## OLED 显示说明

```
┌────────────────────┐
│ Sleepy Widget      │
│ 192.168.1.100    ! │  ← IP 地址；! 表示请求出错
│────────────────────│
│ Status             │
│                    │
│ 活着               │  ← 当前状态名称（支持中文）
└────────────────────┘
```

AP 配网模式：
```
┌────────────────────┐
│ -- AP Mode --      │
│ WiFi: Sleepy-      │
│ ESP8266            │
│ (no password)      │
│ -> 192.168.4.1     │
└────────────────────┘
```

---

## 常见问题

**Q: 配网后访问局域网 IP 提示 ERR_ADDRESS_UNREACHABLE**  
A: 保存配置后，需要将你的手机/电脑**切换回配网 WiFi**，再访问 OLED 显示的 IP。

**Q: 屏幕显示 `!`，状态不更新**  
A: 检查 Server URL 是否正确（含 `http://` 或 `https://`，末尾无 `/`）、Secret 是否与服务器一致、服务器是否可访问、网络能否正常访问外网。

**Q: GPIO13 模式下电平变化但状态不更新**  
A: 确认以下几点：① GPIO13 接了 10kΩ 下拉电阻至 GND；② 网页中已设置 Alive Status / Dead Status；③ Switch Type 已选择 GPIO13 Level Sense 并保存重启。

**Q: 编译报错 Flash 不足**  
A: 确认烧录设置中 Flash Size 选择了 `4MB (FS:2MB ...)`，不要选其他的。

**Q: 第一次上电卡在 Connecting... 不动**  
A: 等待约 20 秒超时后会自动进入 AP 模式。也可以长按重置按钮（D6）3 秒强制清除配置。

**Q: 做 GPIO13 电平检测触发是何意味**  
A: 买了一个米家模块，输出是靠电压差控制的，使用此功能可以将其接入米家，很方便。

**Q: 为什么不用 ADC 引脚测量电压**  
A: 经过测试，在连接米家模块后测量到的电压不稳，不过我在 PCB 上预留了分压电阻，后续可能会使用。

---

## 代码参考
- **[《如何优雅地实现每 5 秒轮询请求？》]([https://github.com/sleepy-project](https://www.jlc-bbs.com/platform/a/1474359))**


## 致谢

- **[sleepy-project/sleepy](https://github.com/sleepy-project)** — 状态服务器
- **[Claude](https://claude.ai)** & **[DeepSeek](https://deepseek.com)** — 代码编写协助
- **[MinecraftXFJ](https://www.minecraftxfj.top)** — 硬件设计与集成

---

*[wyf9](https://wyf9.top) is a cute little catgirl !!!*
