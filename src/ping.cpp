/**
 * @file ping.cpp
 * @brief Ping 实现模块 - IPv4/IPv6 Ping 操作和主机名解析
 * @author mrchzh <gmrchzh@gmail.com>
 * @version 1.1.0
 * @date 2026
 * @copyright MIT License
 *
 * 本模块实现了 ICMP Echo 请求/回复功能，包括：
 * - IPv4 Ping（使用 IcmpSendEcho API）
 * - IPv6 Ping（使用 Icmp6SendEcho2 API）
 * - 支持记录路由、时间戳、源路由等高级 IP 选项
 * - 反向 DNS 解析
 *
 * 使用 Windows ICMP API，无需管理员权限即可运行。
 */

#include "qping.h"

namespace qping {

//=============================================================================
// 内部辅助函数
//=============================================================================

/**
 * @brief 将 IP 选项数据中的 4 字节转换为点分十进制字符串
 *
 * 用于解析记录路由选项返回的 IP 地址数据。
 *
 * @param data 指向 4 字节 IP 地址数据的指针（网络字节序）
 * @return 点分十进制格式的 IP 地址字符串
 */
static std::string format_route_ip(const unsigned char* data) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             data[0], data[1], data[2], data[3]);
    return buf;
}

//=============================================================================
// IPv4 Ping 实现
//=============================================================================

/**
 * @brief 执行 IPv4 ICMP Echo 请求
 *
 * 使用 Windows IcmpSendEcho API 向指定的 IPv4 地址发送 ICMP Echo 请求，
 * 并等待回复。支持多种高级 IP 选项，包括：
 * - TTL（生存时间）和 TOS（服务类型）设置
 * - DF（不分段）标志
 * - 记录路由选项（-r）
 * - 时间戳选项（-s）
 * - 宽松源路由（-j）和严格源路由（-k）
 *
 * @param ip 目标 IPv4 地址字符串（点分十进制格式）
 * @param opts Ping 配置选项，包含超时、负载大小、TTL 等参数
 * @return PingResult 结构，包含操作结果和统计信息
 *
 * @note 源地址选项（-S）在 IcmpSendEcho 中不支持，会显示警告
 *
 * @see PingOptions
 * @see PingResult
 *
 * @example
 * @code
 * PingOptions opts;
 * opts.timeout_ms = 1000;
 * opts.payload_size = 32;
 * opts.ttl = 64;
 *
 * PingResult result = ping_ipv4("192.168.1.1", opts);
 * if (result.success) {
 *     printf("RTT: %lu ms, TTL: %lu\n", result.rtt_ms, result.reply_ttl);
 * }
 * @endcode
 */
PingResult ping_ipv4(const std::string& ip, const PingOptions& opts) {
    PingResult result;

    //-------------------------------------------------------------------------
    // 解析目标地址
    //-------------------------------------------------------------------------
    IN_ADDR dest;
    if (InetPtonA(AF_INET, ip.c_str(), &dest) != 1) {
        return result;  // 地址解析失败
    }

    //-------------------------------------------------------------------------
    // 创建 ICMP 句柄（使用 RAII 自动管理）
    //-------------------------------------------------------------------------
    IcmpHandle handle(IcmpCreateFile());
    if (!handle.valid()) {
        return result;  // 句柄创建失败
    }

    //-------------------------------------------------------------------------
    // 准备发送数据（负载）
    //-------------------------------------------------------------------------
    std::vector<char> payload(opts.payload_size);
    const char pattern[] = "QPING_PAYLOAD_";
    for (int i = 0; i < opts.payload_size; ++i) {
        payload[i] = pattern[i % (sizeof(pattern) - 1)];
    }

    //-------------------------------------------------------------------------
    // 配置 IP 选项
    //-------------------------------------------------------------------------
    IP_OPTION_INFORMATION ipopt = {};
    ipopt.Ttl = (UCHAR)opts.ttl;      // 生存时间
    ipopt.Tos = (UCHAR)opts.tos;      // 服务类型
    ipopt.Flags = opts.dont_fragment ? 0x2 : 0x0;  // DF 标志

    // 选项数据缓冲区
    std::vector<unsigned char> options_buffer(64, 0);
    bool use_options = false;

    //-------------------------------------------------------------------------
    // 设置严格源路由选项（-k）
    // 数据包必须按照指定的路由节点顺序传输
    //-------------------------------------------------------------------------
    if (!opts.strict_source_route.empty()) {
        int route_count = std::min((int)opts.strict_source_route.size(), MAX_SOURCE_ROUTE);

        // 选项格式: [类型][长度][指针][IP地址列表...]
        options_buffer[0] = OPT_SSRR;                      // 严格源路由类型
        options_buffer[1] = (UCHAR)(3 + route_count * 4);  // 总长度
        options_buffer[2] = 4;                              // 指针（指向第一个地址）

        // 填充路由节点 IP 地址
        for (int i = 0; i < route_count; ++i) {
            in_addr addr;
            if (InetPtonA(AF_INET, opts.strict_source_route[i].c_str(), &addr) != 1) {
                fprintf(stderr, "源路由中的无效IP: %s\n", opts.strict_source_route[i].c_str());
                return result;
            }
            memcpy(&options_buffer[3 + i * 4], &addr.S_un.S_addr, 4);
        }

        ipopt.OptionsSize = options_buffer[1];
        ipopt.OptionsData = options_buffer.data();
        use_options = true;
    }
    //-------------------------------------------------------------------------
    // 设置宽松源路由选项（-j）
    // 数据包可以经过指定节点，但中间可以有其他路由器
    //-------------------------------------------------------------------------
    else if (!opts.loose_source_route.empty()) {
        int route_count = std::min((int)opts.loose_source_route.size(), MAX_SOURCE_ROUTE);

        // 选项格式: [类型][长度][指针][IP地址列表...]
        options_buffer[0] = OPT_LSRR;                      // 宽松源路由类型
        options_buffer[1] = (UCHAR)(3 + route_count * 4);  // 总长度
        options_buffer[2] = 4;                              // 指针

        // 填充路由节点 IP 地址
        for (int i = 0; i < route_count; ++i) {
            in_addr addr;
            if (InetPtonA(AF_INET, opts.loose_source_route[i].c_str(), &addr) != 1) {
                fprintf(stderr, "源路由中的无效IP: %s\n", opts.loose_source_route[i].c_str());
                return result;
            }
            memcpy(&options_buffer[3 + i * 4], &addr.S_un.S_addr, 4);
        }

        ipopt.OptionsSize = options_buffer[1];
        ipopt.OptionsData = options_buffer.data();
        use_options = true;
    }
    //-------------------------------------------------------------------------
    // 设置时间戳选项（-s）
    // 记录数据包经过的路由器的时间戳
    //-------------------------------------------------------------------------
    else if (opts.timestamp > 0) {
        int ts_count = std::min(opts.timestamp, MAX_TIMESTAMP);

        // 选项格式: [类型][长度][指针][溢出/标志][时间戳列表...]
        options_buffer[0] = OPT_TS;                        // 时间戳类型
        options_buffer[1] = (UCHAR)(4 + ts_count * 4);     // 总长度
        options_buffer[2] = 5;                              // 指针
        options_buffer[3] = 0;                              // 溢出计数和标志

        ipopt.OptionsSize = options_buffer[1];
        ipopt.OptionsData = options_buffer.data();
        use_options = true;
    }
    //-------------------------------------------------------------------------
    // 设置记录路由选项（-r）
    // 记录数据包经过的路由器 IP 地址
    //-------------------------------------------------------------------------
    else if (opts.record_route > 0) {
        int rr_count = std::min(opts.record_route, MAX_RECORD_ROUTE);

        // 选项格式: [类型][长度][指针][IP地址列表...]
        options_buffer[0] = OPT_RR;                        // 记录路由类型
        options_buffer[1] = (UCHAR)(3 + rr_count * 4);     // 总长度
        options_buffer[2] = 4;                              // 指针

        ipopt.OptionsSize = options_buffer[1];
        ipopt.OptionsData = options_buffer.data();
        use_options = true;
    }

    // 如果没有使用任何选项，清空选项指针
    if (!use_options) {
        ipopt.OptionsSize = 0;
        ipopt.OptionsData = nullptr;
    }

    //-------------------------------------------------------------------------
    // 源地址警告（IcmpSendEcho 不支持指定源地址）
    //-------------------------------------------------------------------------
    static bool source_addr_warned = false;
    if (!opts.source_address.empty() && !source_addr_warned) {
        fprintf(stderr, "注意: -S 选项在IcmpSendEcho中不支持，将使用系统默认源地址\n");
        source_addr_warned = true;
    }

    //-------------------------------------------------------------------------
    // 发送 ICMP Echo 请求并等待回复
    //-------------------------------------------------------------------------
    DWORD reply_size = sizeof(ICMP_ECHO_REPLY) + opts.payload_size + 64;
    std::vector<char> reply_buf(reply_size);

    DWORD res = IcmpSendEcho(
        handle.get(),           // ICMP 句柄
        dest.S_un.S_addr,       // 目标地址
        payload.data(),         // 发送数据
        (WORD)opts.payload_size,// 数据大小
        &ipopt,                 // IP 选项
        reply_buf.data(),       // 回复缓冲区
        reply_size,             // 缓冲区大小
        opts.timeout_ms         // 超时时间
    );

    //-------------------------------------------------------------------------
    // 处理回复
    //-------------------------------------------------------------------------
    if (res != 0) {
        PICMP_ECHO_REPLY reply = (PICMP_ECHO_REPLY)reply_buf.data();

        if (reply->Status == IP_SUCCESS) {
            result.success = true;
            result.rtt_ms = reply->RoundTripTime;
            result.reply_ttl = reply->Options.Ttl;

            //------------------------------------------------------------------
            // 解析记录路由选项返回的数据
            //------------------------------------------------------------------
            if (opts.record_route > 0 &&
                reply->Options.OptionsSize > 0 &&
                reply->Options.OptionsData) {

                unsigned char* opt_data = reply->Options.OptionsData;

                // 验证选项类型并提取路由信息
                if (opt_data[0] == OPT_RR && reply->Options.OptionsSize >= 3) {
                    int ptr = opt_data[2];          // 指针位置
                    int count = (ptr - 4) / 4;      // 已记录的 IP 数量

                    for (int i = 0; i < count &&
                         (3 + (i + 1) * 4) <= reply->Options.OptionsSize; ++i) {
                        result.route_hops.push_back(
                            format_route_ip(&opt_data[3 + i * 4])
                        );
                    }
                }
            }

            //------------------------------------------------------------------
            // 解析时间戳选项返回的数据
            //------------------------------------------------------------------
            if (opts.timestamp > 0 &&
                reply->Options.OptionsSize > 0 &&
                reply->Options.OptionsData) {

                unsigned char* opt_data = reply->Options.OptionsData;

                // 验证选项类型并提取时间戳
                if (opt_data[0] == OPT_TS && reply->Options.OptionsSize >= 4) {
                    int ptr = opt_data[2];          // 指针位置
                    int count = (ptr - 5) / 4;      // 已记录的时间戳数量

                    for (int i = 0; i < count &&
                         (4 + (i + 1) * 4) <= reply->Options.OptionsSize; ++i) {
                        uint32_t ts;
                        memcpy(&ts, &opt_data[4 + i * 4], 4);
                        result.timestamps.push_back(ntohl(ts));
                    }
                }
            }
        }
    }

    return result;
}

//=============================================================================
// IPv6 Ping 实现
//=============================================================================

/**
 * @brief 执行 IPv6 ICMPv6 Echo 请求
 *
 * 使用 Windows Icmp6SendEcho2 API 向指定的 IPv6 地址发送 ICMPv6 Echo 请求。
 * 与 IPv4 相比，IPv6 Ping 的选项较为有限：
 * - 支持 TTL（跳数限制）设置
 * - 不支持 DF 标志（IPv6 不在中间节点分片）
 * - 不支持记录路由、时间戳等 IP 选项
 *
 * @param ip 目标 IPv6 地址字符串
 * @param opts Ping 配置选项
 * @return PingResult 结构，包含操作结果和统计信息
 *
 * @note IPv6 使用 in6addr_any 作为源地址，让系统自动选择合适的接口
 *
 * @see PingOptions
 * @see PingResult
 */
PingResult ping_ipv6(const std::string& ip, const PingOptions& opts) {
    PingResult result;

    //-------------------------------------------------------------------------
    // 解析目标地址
    //-------------------------------------------------------------------------
    sockaddr_in6 dest_addr = {};
    dest_addr.sin6_family = AF_INET6;
    if (InetPtonA(AF_INET6, ip.c_str(), &dest_addr.sin6_addr) != 1) {
        return result;  // 地址解析失败
    }

    //-------------------------------------------------------------------------
    // 创建 ICMPv6 句柄
    //-------------------------------------------------------------------------
    IcmpHandle handle(Icmp6CreateFile());
    if (!handle.valid()) {
        return result;  // 句柄创建失败
    }

    //-------------------------------------------------------------------------
    // 配置源地址
    // 使用 in6addr_any 让系统自动选择合适的源地址
    //-------------------------------------------------------------------------
    sockaddr_in6 src_addr = {};
    src_addr.sin6_family = AF_INET6;

    if (!opts.source_address.empty()) {
        // 尝试使用用户指定的源地址
        if (InetPtonA(AF_INET6, opts.source_address.c_str(), &src_addr.sin6_addr) != 1) {
            // 解析失败，回退到自动选择
            src_addr.sin6_addr = in6addr_any;
        }
    } else {
        // 默认使用 any 地址，让系统选择
        src_addr.sin6_addr = in6addr_any;
    }

    //-------------------------------------------------------------------------
    // 准备发送数据
    //-------------------------------------------------------------------------
    std::vector<char> payload(opts.payload_size);
    const char pattern[] = "QPING_PAYLOAD_";
    for (int i = 0; i < opts.payload_size; ++i) {
        payload[i] = pattern[i % (sizeof(pattern) - 1)];
    }

    //-------------------------------------------------------------------------
    // 配置 IPv6 选项（仅支持 TTL/跳数限制）
    //-------------------------------------------------------------------------
    IP_OPTION_INFORMATION ipopt = {};
    ipopt.Ttl = (UCHAR)opts.ttl;

    //-------------------------------------------------------------------------
    // 发送 ICMPv6 Echo 请求
    //-------------------------------------------------------------------------
    DWORD reply_size = sizeof(ICMP_ECHO_REPLY) + opts.payload_size + 64;
    std::vector<char> reply_buf(reply_size);

    DWORD res = Icmp6SendEcho2(
        handle.get(),           // ICMP 句柄
        nullptr,                // 事件句柄（同步模式）
        nullptr,                // APC 回调
        nullptr,                // APC 上下文
        &src_addr,              // 源地址
        &dest_addr,             // 目标地址
        payload.data(),         // 发送数据
        (WORD)opts.payload_size,// 数据大小
        &ipopt,                 // IP 选项
        reply_buf.data(),       // 回复缓冲区
        reply_size,             // 缓冲区大小
        opts.timeout_ms         // 超时时间
    );

    //-------------------------------------------------------------------------
    // 处理回复
    //-------------------------------------------------------------------------
    if (res != 0) {
        PICMP_ECHO_REPLY reply = (PICMP_ECHO_REPLY)reply_buf.data();

        if (reply->Status == IP_SUCCESS) {
            result.success = true;
            result.rtt_ms = reply->RoundTripTime;
            // IPv6 回复中没有 TTL 字段，使用请求时的 TTL 值
            result.reply_ttl = (DWORD)opts.ttl;
        }
    }

    return result;
}

//=============================================================================
// 主机名解析
//=============================================================================

/**
 * @brief 反向 DNS 解析，获取 IP 地址对应的主机名（带超时）
 *
 * 使用 getnameinfo API 执行反向 DNS 查询，将 IP 地址解析为主机名。
 * 此函数是线程安全的，并包含超时机制以避免在无网络环境下的长时间等待。
 *
 * @param ip IP 地址字符串（IPv4 或 IPv6 格式）
 * @param af 地址族（AF_INET 或 AF_INET6）
 * @return 解析到的主机名；如果解析失败或超时返回空字符串
 *
 * @note 使用线程+超时机制，避免在无网络环境下等待过久（默认超时 2 秒）
 *
 * @example
 * @code
 * std::string hostname = resolve_hostname("8.8.8.8", AF_INET);
 * // 可能返回 "dns.google" 或空字符串（如果超时或失败）
 * @endcode
 */
std::string resolve_hostname(const std::string& ip, int af) {
    const int DNS_TIMEOUT_MS = 2000;  // DNS 查询超时时间（2秒）
    std::string result;
    std::atomic<bool> done{false};
    std::exception_ptr exception_ptr = nullptr;

    // 在单独线程中执行 DNS 查询，以便实现超时
    std::thread resolver([&]() {
        try {
            char hostname[NI_MAXHOST] = {};

            if (af == AF_INET) {
                // IPv4 地址解析
                sockaddr_in sa = {};
                sa.sin_family = AF_INET;
                InetPtonA(AF_INET, ip.c_str(), &sa.sin_addr);

                if (getnameinfo(
                        (sockaddr*)&sa,
                        sizeof(sa),
                        hostname,
                        sizeof(hostname),
                        nullptr,        // 不需要服务名
                        0,
                        NI_NAMEREQD     // 要求返回主机名（否则返回错误）
                    ) == 0) {
                    result = hostname;
                }
            }
            else if (af == AF_INET6) {
                // IPv6 地址解析
                sockaddr_in6 sa = {};
                sa.sin6_family = AF_INET6;
                InetPtonA(AF_INET6, ip.c_str(), &sa.sin6_addr);

                if (getnameinfo(
                        (sockaddr*)&sa,
                        sizeof(sa),
                        hostname,
                        sizeof(hostname),
                        nullptr,
                        0,
                        NI_NAMEREQD
                    ) == 0) {
                    result = hostname;
                }
            }
        } catch (...) {
            exception_ptr = std::current_exception();
        }
        done.store(true);
    });

    // 等待完成或超时
    auto start = std::chrono::steady_clock::now();
    while (!done.load()) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= DNS_TIMEOUT_MS) {
            // 超时，分离线程（让它继续运行但不等待结果）
            resolver.detach();
            return "";  // 超时返回空字符串
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 等待线程完成
    if (resolver.joinable()) {
        resolver.join();
    }

    // 检查是否有异常
    if (exception_ptr) {
        try {
            std::rethrow_exception(exception_ptr);
        } catch (...) {
            // 忽略异常，返回空字符串
        }
    }

    return result;
}

} // namespace qping
