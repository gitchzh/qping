/**
 * @file qping.h
 * @brief qping 公共头文件 - 常量定义、数据结构和函数声明
 * @author mrchzh <gmrchzh@gmail.com>
 * @version 1.1.0
 * @date 2026
 * @copyright MIT License
 *
 * 本文件定义了 qping 工具的所有公共接口，包括：
 * - 默认参数常量
 * - IP 选项常量
 * - RAII 句柄封装类
 * - Ping 结果和选项结构体
 * - 目标解析、Ping 执行等核心函数声明
 */

#ifndef QPING_H
#define QPING_H

#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <atomic>
#include <vector>
#include <string>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <chrono>
#include <algorithm>

#ifndef _WIN32
#error "本程序仅限Windows平台"
#endif

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

/**
 * @namespace qping
 * @brief qping 工具的命名空间，包含所有核心功能
 */
namespace qping {

//=============================================================================
// 版本信息
//=============================================================================

/** @brief 程序版本号 */
constexpr const char* VERSION = "1.1.0";

//=============================================================================
// 默认参数常量
//=============================================================================

/** @brief 默认并发线程数 */
constexpr int DEFAULT_CONCURRENCY = 100;

/** @brief 默认超时时间（毫秒） */
constexpr int DEFAULT_TIMEOUT_MS = 1000;

/** @brief 默认发送数据包大小（字节） */
constexpr int DEFAULT_PAYLOAD_SIZE = 32;

/** @brief 默认 TTL（生存时间）值 */
constexpr int DEFAULT_TTL = 128;

/** @brief 最大发送数据包大小（字节） */
constexpr int MAX_PAYLOAD_SIZE = 65500;

/** @brief 默认最大目标主机数限制 */
constexpr unsigned int MAX_HOSTS_DEFAULT = 65536;

//=============================================================================
// IP 选项常量
//=============================================================================

/** @brief 记录路由选项最大跳数 */
constexpr int MAX_RECORD_ROUTE = 9;

/** @brief 时间戳选项最大数量 */
constexpr int MAX_TIMESTAMP = 4;

/** @brief 源路由选项最大节点数 */
constexpr int MAX_SOURCE_ROUTE = 9;

/** @brief IP 选项类型：记录路由 (Record Route) */
constexpr UCHAR OPT_RR = 0x07;

/** @brief IP 选项类型：时间戳 (Timestamp) */
constexpr UCHAR OPT_TS = 0x44;

/** @brief IP 选项类型：宽松源路由 (Loose Source and Record Route) */
constexpr UCHAR OPT_LSRR = 0x83;

/** @brief IP 选项类型：严格源路由 (Strict Source and Record Route) */
constexpr UCHAR OPT_SSRR = 0x89;

//=============================================================================
// 类定义
//=============================================================================

/**
 * @class IcmpHandle
 * @brief ICMP 句柄的 RAII 封装类
 *
 * 该类使用 RAII（资源获取即初始化）模式管理 Windows ICMP API 句柄，
 * 确保句柄在对象生命周期结束时自动释放，防止资源泄漏。
 *
 * @note 该类禁用了拷贝构造和拷贝赋值，以防止句柄被多次关闭
 *
 * @example
 * @code
 * IcmpHandle handle(IcmpCreateFile());
 * if (handle.valid()) {
 *     // 使用 handle.get() 进行 ICMP 操作
 * }
 * // 句柄在作用域结束时自动关闭
 * @endcode
 */
class IcmpHandle {
public:
    /**
     * @brief 构造函数
     * @param h Windows ICMP 句柄
     */
    explicit IcmpHandle(HANDLE h) : handle_(h) {}

    /**
     * @brief 析构函数，自动关闭有效的 ICMP 句柄
     */
    ~IcmpHandle() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            IcmpCloseHandle(handle_);
        }
    }

    /**
     * @brief 获取底层句柄
     * @return ICMP 句柄
     */
    HANDLE get() const { return handle_; }

    /**
     * @brief 检查句柄是否有效
     * @return 如果句柄有效返回 true，否则返回 false
     */
    bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }

    // 禁用拷贝
    IcmpHandle(const IcmpHandle&) = delete;
    IcmpHandle& operator=(const IcmpHandle&) = delete;

private:
    HANDLE handle_;  ///< Windows ICMP 句柄
};

//=============================================================================
// 结构体定义
//=============================================================================

/**
 * @struct PingResult
 * @brief Ping 操作的结果数据结构
 *
 * 存储单次 Ping 操作的所有结果信息，包括是否成功、
 * 往返时间、TTL 值，以及可选的路由跟踪和时间戳信息。
 */
struct PingResult {
    bool success = false;                    ///< Ping 是否成功
    DWORD rtt_ms = 0;                        ///< 往返时间（毫秒）
    DWORD reply_ttl = 0;                     ///< 回复数据包的 TTL 值
    std::vector<std::string> route_hops;     ///< 记录路由的跳点 IP 列表
    std::vector<uint32_t> timestamps;        ///< 时间戳列表（毫秒）
};

/**
 * @struct PingOptions
 * @brief Ping 操作的配置选项
 *
 * 包含执行 Ping 操作所需的所有可配置参数，
 * 如超时时间、数据包大小、TTL、源路由等。
 */
struct PingOptions {
    int timeout_ms = DEFAULT_TIMEOUT_MS;     ///< 超时时间（毫秒）
    int payload_size = DEFAULT_PAYLOAD_SIZE; ///< 发送数据包大小（字节）
    int ttl = DEFAULT_TTL;                   ///< TTL（生存时间）值
    int tos = 0;                             ///< TOS（服务类型）值
    bool dont_fragment = false;              ///< 是否设置不分段标志
    int record_route = 0;                    ///< 记录路由跳数（0 表示禁用）
    int timestamp = 0;                       ///< 时间戳数量（0 表示禁用）
    std::vector<std::string> loose_source_route;   ///< 宽松源路由节点列表
    std::vector<std::string> strict_source_route;  ///< 严格源路由节点列表
    std::string source_address;              ///< 源地址（可选）
};

//=============================================================================
// 工具函数声明
//=============================================================================

/**
 * @brief 按指定分隔符拆分字符串
 * @param s 要拆分的字符串
 * @param delim 分隔符字符
 * @return 拆分后的字符串向量
 *
 * @example
 * @code
 * auto parts = split("192.168.1.1,192.168.1.2", ',');
 * // parts = {"192.168.1.1", "192.168.1.2"}
 * @endcode
 */
std::vector<std::string> split(const std::string& s, char delim);

/**
 * @brief 将字符串解析为整数
 * @param str 要解析的字符串
 * @param[out] out 解析结果输出
 * @return 解析成功返回 true，失败返回 false
 */
bool parse_int(const char* str, int& out);

//=============================================================================
// IP 地址函数声明
//=============================================================================

/**
 * @brief 检查字符串是否为 IPv6 地址格式
 * @param s 要检查的字符串
 * @return 如果包含冒号（IPv6 特征）返回 true
 * @note 此函数仅做格式检测，不验证地址有效性
 */
bool is_ipv6_address(const std::string& s);

/**
 * @brief 验证 IPv4 地址是否有效
 * @param s 要验证的 IPv4 地址字符串
 * @return 如果是有效的 IPv4 地址返回 true
 */
bool is_valid_ipv4_address(const std::string& s);

/**
 * @brief 验证 IPv6 地址是否有效
 * @param s 要验证的 IPv6 地址字符串
 * @return 如果是有效的 IPv6 地址返回 true
 */
bool is_valid_ipv6_address(const std::string& s);

/**
 * @brief 获取 IP 地址的地址族
 * @param s IP 地址字符串
 * @return AF_INET（IPv4）、AF_INET6（IPv6）或 AF_UNSPEC（无效）
 */
int get_address_family(const std::string& s);

/**
 * @brief 将 32 位整数转换为点分十进制 IP 字符串
 * @param ip 网络字节序的 32 位 IP 地址
 * @return 点分十进制格式的 IP 地址字符串
 */
std::string ip_to_string(uint32_t ip);

/**
 * @brief 解析目标字符串并枚举所有 IP 地址
 *
 * 支持以下格式：
 * - 单个 IP：192.168.1.1
 * - CIDR：192.168.1.0/24
 * - 最后一段范围：192.168.1.1-10
 * - 第三段范围：192.168.1-3
 * - 逗号分隔：192.168.2.1,3,5 或 192.168.2.1,3-5,10
 * - IPv6 地址：2001:db8::1
 *
 * @param token 目标字符串
 * @param[out] out 输出的 IP 地址列表
 * @param max_hosts 最大主机数限制
 * @return 解析成功返回 true，失败返回 false
 */
bool enumerate_targets(const std::string& token,
                       std::vector<std::string>& out,
                       unsigned int max_hosts);

/**
 * @brief 将 IPv4 地址字符串转换为 32 位整数
 * @param ip IPv4 地址字符串（点分十进制格式）
 * @return 主机字节序的 32 位整数，转换失败返回 0
 */
uint32_t ip_to_uint32(const std::string& ip);

/**
 * @brief 将 IP 地址列表压缩为范围格式字符串
 *
 * 将连续的 IP 地址合并为范围表示，不连续的用逗号分隔。
 * 例如：{"192.168.1.1", "192.168.1.2", "192.168.1.3", "192.168.1.5"}
 * 输出："192.168.1.1-3, 192.168.1.5"
 *
 * @param ips IP 地址字符串列表
 * @return 压缩后的范围格式字符串
 *
 * @note 仅支持 IPv4 地址，IPv6 地址将单独列出
 */
std::string compress_ip_ranges(const std::vector<std::string>& ips);

//=============================================================================
// Ping 函数声明
//=============================================================================

/**
 * @brief 执行 IPv4 Ping 操作
 * @param ip 目标 IPv4 地址
 * @param opts Ping 配置选项
 * @return Ping 结果
 */
PingResult ping_ipv4(const std::string& ip, const PingOptions& opts);

/**
 * @brief 执行 IPv6 Ping 操作
 * @param ip 目标 IPv6 地址
 * @param opts Ping 配置选项
 * @return Ping 结果
 */
PingResult ping_ipv6(const std::string& ip, const PingOptions& opts);

/**
 * @brief 反向 DNS 解析，获取 IP 地址对应的主机名
 * @param ip IP 地址字符串
 * @param af 地址族（AF_INET 或 AF_INET6）
 * @return 主机名，解析失败返回空字符串
 */
std::string resolve_hostname(const std::string& ip, int af);

//=============================================================================
// 帮助函数声明
//=============================================================================

/**
 * @brief 打印程序版本信息
 */
void print_version();

/**
 * @brief 打印程序使用帮助
 * @param prog 程序名称（通常为 argv[0]）
 */
void print_usage(const char* prog);

} // namespace qping

#endif // QPING_H
