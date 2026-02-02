# qping - Windows Ping 替代工具

**作者**: mrchzh  
**邮箱**: gmrchzh@gmail.com  
**GitHub**: [github.com/gitchzh/qping](https://github.com/gitchzh/qping)

qping 是一个专为 Windows 平台设计的命令行 ping 工具，支持 CIDR 扫描、IP 范围和 IPv4/IPv6 双栈。

## 项目结构

```
qping/
├── src/
│   ├── qping.h      # 公共头文件
│   ├── target.cpp   # 目标解析
│   ├── ping.cpp     # Ping 实现
│   └── main.cpp     # 主程序
├── CMakeLists.txt
├── LICENSE
└── README.md
```

## 特性

- 兼容 Windows ping 命令的常用选项
- 支持 CIDR 表示法（如 `192.168.1.0/24`）
- 支持 IP 范围扫描（如 `192.168.1.1-10`）
- 可配置并发线程数的高性能扫描
- IPv4/IPv6 双栈支持
- 记录路由和时间戳选项
- 无需管理员权限

## 快速开始

```cmd
# 单个 IP
qping 192.168.0.1

# CIDR 扫描
qping 192.168.1.0/24

# IP 范围
qping 192.168.1.1-10

# 持续 ping
qping -t 192.168.0.1

# 发送 5 次，超时 500ms
qping -n 5 -w 500 192.168.0.1

# 高并发扫描
qping --concurrency 200 192.168.1.0/24
```

## 编译

### 前提条件

- Windows 操作系统
- C++17 编译器（MSVC、MinGW 或 Clang）
- Windows SDK

### 使用 MinGW

```bash
# 静态链接运行时库，避免依赖 libgcc_s_dw2-1.dll 等 DLL
# 如果源代码是 UTF-8 编码，使用：
g++ -std=c++17 -O2 -I src -finput-charset=utf-8 -fexec-charset=gbk -static-libgcc -static-libstdc++ src/main.cpp src/ping.cpp src/target.cpp -o qping.exe -lIphlpapi -lWs2_32

# 如果源代码是 GBK 编码，使用：
g++ -std=c++17 -O2 -I src -finput-charset=gbk -fexec-charset=gbk -static-libgcc -static-libstdc++ src/main.cpp src/ping.cpp src/target.cpp -o qping.exe -lIphlpapi -lWs2_32
```

### 使用 MSVC

```cmd
cl /EHsc /O2 /std:c++17 /I src src/main.cpp src/ping.cpp src/target.cpp /link Iphlpapi.lib Ws2_32.lib
```

### 使用 CMake + Ninja

```bash
mkdir build && cd build
cmake -G "Ninja" ..
cmake --build . --config Release
```

### 使用 CMake + Visual Studio

```cmd
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

## 命令行选项

### 标准选项

| 选项 | 说明 |
|------|------|
| `-t` | 持续 ping 直到 Ctrl+C 停止 |
| `-a` | 解析地址为主机名 |
| `-n count` | 发送回显请求的次数 |
| `-l size` | 发送缓冲区大小（字节，最大 65500） |
| `-f` | 设置不分段标志（仅 IPv4） |
| `-i TTL` | 生存时间（0-255） |
| `-v TOS` | 服务类型（仅 IPv4） |
| `-w timeout` | 超时时间（毫秒） |
| `-4` | 强制使用 IPv4 |
| `-6` | 强制使用 IPv6 |
| `-r count` | 记录路由跳数（1-9，仅 IPv4） |
| `-s count` | 时间戳跳数（1-4，仅 IPv4） |
| `-j host-list` | 宽松源路由（仅 IPv4） |
| `-k host-list` | 严格源路由（仅 IPv4） |
| `-S srcaddr` | 源地址 |

### 扩展选项

| 选项 | 说明 |
|------|------|
| `--concurrency N` | 并发线程数（默认 100） |
| `--force` | 允许扫描超过 65536 个目标 |
| `--exclude ip[,ip...]` | 排除指定 IP |
| `--version` | 显示版本信息 |
| `-h, --help` | 显示帮助信息 |

### 目标格式

| 格式 | 示例 | 说明 |
|------|------|------|
| 单个 IP | `192.168.0.1` | 单个 IPv4 地址 |
| CIDR | `192.168.1.0/24` | 扫描 192.168.1.1-254 |
| 最后一段范围 | `192.168.1.1-10` | 扫描 192.168.1.1 到 192.168.1.10 |
| 第三段范围 | `192.168.1-3` | 扫描 192.168.1.1-254 到 192.168.3.1-254 |
| 逗号分隔 | `192.168.2.1,3,5` | 扫描 192.168.2.1, .3, .5 |
| 混合格式 | `192.168.2.1,3-5,10` | 扫描 192.168.2.1, .3, .4, .5, .10 |
| IPv6 | `2001:db8::1` | 单个 IPv6 地址 |

## 输出示例

```
总目标数: 6
来自 192.168.1.1 的回复: 字节=32 时间=1ms TTL=64
来自 192.168.1.2 的回复: 字节=32 时间=2ms TTL=64
来自 192.168.1.3 的回复: 字节=32 时间=1ms TTL=64
请求超时 192.168.1.5
来自 192.168.1.10 的回复: 字节=32 时间=3ms TTL=64
请求超时 192.168.1.20

--- 统计信息 ---
192.168.1.1 : 已发送=1, 已接收=1, 丢失=0 (0.0%)
192.168.1.2 : 已发送=1, 已接收=1, 丢失=0 (0.0%)
192.168.1.3 : 已发送=1, 已接收=1, 丢失=0 (0.0%)
192.168.1.5 : 已发送=1, 已接收=0, 丢失=1 (100.0%)
192.168.1.10 : 已发送=1, 已接收=1, 丢失=0 (0.0%)
192.168.1.20 : 已发送=1, 已接收=0, 丢失=1 (100.0%)

数据包统计: 发送=6, 接收=4, 丢失=2 (33.3%)

在线设备 (4): 192.168.1.1-3, 192.168.1.10
失败设备 (2): 192.168.1.5, 192.168.1.20
```

## 快捷键

- **Ctrl+C**: 停止 ping 并显示统计信息
- **Ctrl+Break**: 显示中间统计信息（不停止）

## 注意事项

- 扫描大型网络会产生大量 ICMP 流量
- 扫描前请确保有适当的授权
- 默认目标限制 65536 是安全特性，可用 `--force` 覆盖
- 某些高级选项（如源路由）可能受网络设备限制

## 版本历史

### v1.0.1

- 修复在无环境变量的计算机中运行缺少 `libgcc_s_dw2-1.dll` 的问题（静态链接运行时库）
- 修复中文系统中文显示乱码问题（设置控制台代码页为 GBK）
- 优化首次运行速度（添加系统 API 预热机制）
- 优化 DNS 解析性能（添加 2 秒超时机制，避免无网络环境下的长时间等待）
- 修复头文件包含顺序问题（winsock2.h 必须在 windows.h 之前）

### v1.1.0

- 重构为多文件结构，便于维护
- 新增逗号分隔目标格式（如 `192.168.2.1,3,5` 或 `192.168.2.1,3-5,10`）
- 优化统计输出，自动压缩连续 IP 为范围格式
- 修复 IPv6 源地址默认值问题
- 修复 IPv6 回复解析类型
- 使用 RAII 管理 ICMP 句柄
- 使用 `InetPtonA` 替代 `inet_addr`
- 使用 `getnameinfo` 替代已弃用的 `gethostbyaddr`
- 添加 `--version` 选项
- 显示记录路由和时间戳结果
- 使用命名空间组织代码

### v1.0.0

- 初始版本
- 基础 ping 功能
- CIDR 和 IP 范围扫描
- 并发线程池模型

## 许可证

MIT License - 详见 [LICENSE](LICENSE) 文件