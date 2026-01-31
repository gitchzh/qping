/**
 * @file target.cpp
 * @brief 目标解析模块 - IP 地址解析、CIDR 展开、范围处理
 * @author mrchzh <gmrchzh@gmail.com>
 * @version 1.1.0
 * @date 2026
 * @copyright MIT License
 *
 * 本模块负责解析用户输入的目标字符串，支持多种格式：
 * - 单个 IPv4/IPv6 地址
 * - CIDR 表示法（如 192.168.1.0/24）
 * - IP 范围表示法（如 192.168.1.1-10 或 192.168.1-3）
 *
 * 还包含字符串处理和 IP 地址验证的工具函数。
 */

#include "qping.h"

namespace qping {

//=============================================================================
// 工具函数
//=============================================================================

/**
 * @brief 按指定分隔符拆分字符串
 *
 * 将输入字符串按照指定的分隔符拆分为多个子字符串。
 * 连续的分隔符会产生空字符串元素。
 *
 * @param s 要拆分的字符串
 * @param delim 分隔符字符
 * @return 拆分后的字符串向量
 *
 * @example
 * @code
 * auto parts = split("a.b.c.d", '.');
 * // parts = {"a", "b", "c", "d"}
 *
 * auto ips = split("192.168.1.1,192.168.1.2", ',');
 * // ips = {"192.168.1.1", "192.168.1.2"}
 * @endcode
 */
std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    size_t start = 0, end;

    // 循环查找分隔符并提取子字符串
    while ((end = s.find(delim, start)) != std::string::npos) {
        parts.push_back(s.substr(start, end - start));
        start = end + 1;
    }

    // 添加最后一个部分（分隔符之后的内容）
    if (start < s.length()) {
        parts.push_back(s.substr(start));
    }

    return parts;
}

/**
 * @brief 将字符串解析为整数
 *
 * 使用 strtol 进行安全的字符串到整数转换，
 * 会检查整个字符串是否都被成功解析。
 *
 * @param str 要解析的字符串（必须以 null 结尾）
 * @param[out] out 解析成功时存储结果
 * @return 解析成功返回 true，失败返回 false
 *
 * @note 如果字符串包含非数字字符（除了可选的前导符号），解析会失败
 *
 * @example
 * @code
 * int value;
 * if (parse_int("123", value)) {
 *     // value = 123
 * }
 * parse_int("12.3", value);  // 返回 false
 * parse_int("abc", value);   // 返回 false
 * @endcode
 */
bool parse_int(const char* str, int& out) {
    char* end;
    long v = strtol(str, &end, 10);

    // 检查是否解析了任何字符，以及是否到达字符串末尾
    if (end == str || *end != '\0') {
        return false;
    }

    out = (int)v;
    return true;
}

//=============================================================================
// IP 地址验证函数
//=============================================================================

/**
 * @brief 检查字符串是否为 IPv6 地址格式
 *
 * 通过检查字符串中是否包含冒号来判断是否可能是 IPv6 地址。
 * 这是一个快速的格式检测，不验证地址的有效性。
 *
 * @param s 要检查的字符串
 * @return 如果字符串包含冒号返回 true，否则返回 false
 *
 * @note IPv6 地址使用冒号分隔，而 IPv4 使用点号，因此冒号是 IPv6 的特征
 */
bool is_ipv6_address(const std::string& s) {
    return s.find(':') != std::string::npos;
}

/**
 * @brief 验证 IPv4 地址是否有效
 *
 * 使用 Windows API InetPtonA 验证 IPv4 地址格式。
 * 该函数会检查地址是否符合点分十进制格式，
 * 以及每个八位组是否在 0-255 范围内。
 *
 * @param s 要验证的 IPv4 地址字符串
 * @return 如果是有效的 IPv4 地址返回 true，否则返回 false
 *
 * @example
 * @code
 * is_valid_ipv4_address("192.168.1.1");    // true
 * is_valid_ipv4_address("256.1.1.1");      // false（超出范围）
 * is_valid_ipv4_address("192.168.1");      // false（格式不完整）
 * @endcode
 */
bool is_valid_ipv4_address(const std::string& s) {
    in_addr addr;
    return InetPtonA(AF_INET, s.c_str(), &addr) == 1;
}

/**
 * @brief 验证 IPv6 地址是否有效
 *
 * 使用 Windows API InetPtonA 验证 IPv6 地址格式。
 * 支持完整格式和压缩格式（使用 :: 省略连续的零）。
 *
 * @param s 要验证的 IPv6 地址字符串
 * @return 如果是有效的 IPv6 地址返回 true，否则返回 false
 *
 * @example
 * @code
 * is_valid_ipv6_address("2001:db8::1");           // true
 * is_valid_ipv6_address("::1");                   // true（回环地址）
 * is_valid_ipv6_address("2001:db8:85a3::8a2e:370:7334");  // true
 * @endcode
 */
bool is_valid_ipv6_address(const std::string& s) {
    in6_addr addr;
    return InetPtonA(AF_INET6, s.c_str(), &addr) == 1;
}

/**
 * @brief 获取 IP 地址的地址族
 *
 * 自动检测 IP 地址是 IPv4 还是 IPv6，并验证其有效性。
 *
 * @param s IP 地址字符串
 * @return 地址族常量：
 *         - AF_INET: 有效的 IPv4 地址
 *         - AF_INET6: 有效的 IPv6 地址
 *         - AF_UNSPEC: 无效地址
 */
int get_address_family(const std::string& s) {
    // 首先检查是否为 IPv6 格式（包含冒号）
    if (is_ipv6_address(s)) {
        return is_valid_ipv6_address(s) ? AF_INET6 : AF_UNSPEC;
    }
    // 否则尝试作为 IPv4 验证
    return is_valid_ipv4_address(s) ? AF_INET : AF_UNSPEC;
}

/**
 * @brief 将 32 位整数转换为点分十进制 IP 字符串
 *
 * 将主机字节序的 32 位 IP 地址转换为人类可读的点分十进制格式。
 *
 * @param ip 主机字节序的 32 位 IP 地址
 * @return 点分十进制格式的 IP 地址字符串
 *
 * @example
 * @code
 * ip_to_string(0xC0A80101);  // 返回 "192.168.1.1"
 * @endcode
 */
std::string ip_to_string(uint32_t ip) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             (ip >> 24) & 0xFF,   // 第一个八位组
             (ip >> 16) & 0xFF,   // 第二个八位组
             (ip >> 8) & 0xFF,    // 第三个八位组
             ip & 0xFF);          // 第四个八位组
    return buf;
}

//=============================================================================
// 目标枚举函数
//=============================================================================

/**
 * @brief 解析目标字符串并枚举所有 IP 地址
 *
 * 这是目标解析的核心函数，支持多种输入格式：
 *
 * 1. **单个 IPv4 地址**: `192.168.1.1`
 * 2. **单个 IPv6 地址**: `2001:db8::1`
 * 3. **CIDR 表示法**: `192.168.1.0/24`
 *    - 自动排除网络地址和广播地址（/31 和 /32 除外）
 * 4. **最后一段范围**: `192.168.1.1-10`
 *    - 展开为 192.168.1.1 到 192.168.1.10
 * 5. **第三段范围**: `192.168.1-3`
 *    - 展开为 192.168.1.1-254, 192.168.2.1-254, 192.168.3.1-254
 * 6. **逗号分隔格式**: `192.168.2.1,3,5` 或 `192.168.2.1,3-5,10`
 *    - 展开为 192.168.2.1, 192.168.2.3, 192.168.2.5 等
 *
 * @param tok 目标字符串（支持上述所有格式）
 * @param[out] out 输出的 IP 地址列表
 * @param max_hosts 最大主机数限制，防止意外生成过多目标
 * @return 解析成功返回 true，失败返回 false 并输出错误信息
 *
 * @warning 大型 CIDR 块可能生成大量 IP，请注意 max_hosts 限制
 *
 * @example
 * @code
 * std::vector<std::string> targets;
 *
 * // CIDR 解析
 * enumerate_targets("192.168.1.0/30", targets, 1000);
 * // targets = {"192.168.1.1", "192.168.1.2"}
 *
 * // 范围解析
 * enumerate_targets("10.0.0.1-3", targets, 1000);
 * // targets = {"10.0.0.1", "10.0.0.2", "10.0.0.3"}
 * @endcode
 */
bool enumerate_targets(const std::string& tok,
                       std::vector<std::string>& out,
                       unsigned int max_hosts) {

    //-------------------------------------------------------------------------
    // 处理 IPv6 地址（仅支持单个地址）
    //-------------------------------------------------------------------------
    if (is_ipv6_address(tok)) {
        if (is_valid_ipv6_address(tok)) {
            out.push_back(tok);
            return true;
        }
        fprintf(stderr, "无效的IPv6地址: %s\n", tok.c_str());
        return false;
    }

    //-------------------------------------------------------------------------
    // 处理 CIDR 表示法: a.b.c.d/prefix
    //-------------------------------------------------------------------------
    auto slash_pos = tok.find('/');
    if (slash_pos != std::string::npos) {
        std::string ip_part = tok.substr(0, slash_pos);
        std::string pref_part = tok.substr(slash_pos + 1);

        // 解析并验证前缀长度
        int prefix;
        if (!parse_int(pref_part.c_str(), prefix) || prefix < 0 || prefix > 32) {
            fprintf(stderr, "无效的CIDR前缀: %s\n", tok.c_str());
            return false;
        }

        // 验证 IP 地址部分
        in_addr addr;
        if (InetPtonA(AF_INET, ip_part.c_str(), &addr) != 1) {
            fprintf(stderr, "CIDR中的无效IP: %s\n", ip_part.c_str());
            return false;
        }
        uint32_t ip = ntohl(addr.S_un.S_addr);

        // /32 表示单个主机
        if (prefix == 32) {
            out.push_back(ip_part);
            return true;
        }

        // 计算网络掩码、网络地址和广播地址
        uint32_t mask = (prefix == 0) ? 0 : (~0u << (32 - prefix));
        uint32_t network = ip & mask;           // 网络地址
        uint32_t broadcast = network | (~mask); // 广播地址

        // 确定可用主机范围
        uint32_t start, end;
        if (prefix >= 31) {
            // /31 点对点链路，没有网络/广播地址概念
            start = network;
            end = broadcast;
        } else {
            // 排除网络地址（第一个）和广播地址（最后一个）
            start = network + 1;
            end = broadcast - 1;
        }

        // 枚举所有主机地址
        unsigned int added = 0;
        for (uint32_t cur = start; cur <= end && added < max_hosts; ++cur, ++added) {
            out.push_back(ip_to_string(cur));
        }
        return true;
    }

    //-------------------------------------------------------------------------
    // 处理范围表示法（但不处理包含逗号的格式，那由后面的代码处理）
    //-------------------------------------------------------------------------
    auto dashpos = tok.find('-');
    if (dashpos != std::string::npos && tok.find(',') == std::string::npos) {
        auto parts = split(tok, '.');

        //---------------------------------------------------------------------
        // 格式: a.b.c.d-e（最后一段范围）
        //---------------------------------------------------------------------
        if (parts.size() == 4) {
            const std::string& last = parts[3];
            auto dashpos2 = last.find('-');

            if (dashpos2 != std::string::npos) {
                // 解析范围的起始和结束值
                std::string left = last.substr(0, dashpos2);
                std::string right = last.substr(dashpos2 + 1);
                int d_start, d_end;

                if (!parse_int(left.c_str(), d_start) ||
                    !parse_int(right.c_str(), d_end) ||
                    d_start < 0 || d_start > 255 ||
                    d_end < 0 || d_end > 255) {
                    fprintf(stderr, "最后一段范围无效: %s\n", tok.c_str());
                    return false;
                }

                // 确保 start <= end
                if (d_start > d_end) {
                    std::swap(d_start, d_end);
                }

                // 解析前三个八位组
                int a, b, c;
                if (!parse_int(parts[0].c_str(), a) ||
                    !parse_int(parts[1].c_str(), b) ||
                    !parse_int(parts[2].c_str(), c) ||
                    a < 0 || a > 255 ||
                    b < 0 || b > 255 ||
                    c < 0 || c > 255) {
                    fprintf(stderr, "IP八位组无效: %s\n", tok.c_str());
                    return false;
                }

                // 枚举范围内的所有 IP
                unsigned int added = 0;
                for (int d = d_start; d <= d_end && added < max_hosts; ++d, ++added) {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%d.%d.%d.%d", a, b, c, d);
                    out.push_back(buf);
                }
                return true;
            }
        }
        //---------------------------------------------------------------------
        // 格式: a.b.c-e（第三段范围，第四段枚举 1-254）
        //---------------------------------------------------------------------
        else if (parts.size() == 3) {
            auto dash = parts[2].find('-');

            if (dash != std::string::npos) {
                // 解析第三段的范围
                std::string left = parts[2].substr(0, dash);
                std::string right = parts[2].substr(dash + 1);
                int c_start, c_end;

                if (!parse_int(left.c_str(), c_start) ||
                    !parse_int(right.c_str(), c_end) ||
                    c_start < 0 || c_start > 255 ||
                    c_end < 0 || c_end > 255) {
                    fprintf(stderr, "第三段范围无效: %s\n", tok.c_str());
                    return false;
                }

                // 确保 start <= end
                if (c_start > c_end) {
                    std::swap(c_start, c_end);
                }

                // 解析前两个八位组
                int oct1, oct2;
                if (!parse_int(parts[0].c_str(), oct1) ||
                    !parse_int(parts[1].c_str(), oct2) ||
                    oct1 < 0 || oct1 > 255 ||
                    oct2 < 0 || oct2 > 255) {
                    fprintf(stderr, "IP八位组无效: %s\n", tok.c_str());
                    return false;
                }

                // 枚举所有子网的可用主机（1-254，排除 0 和 255）
                unsigned int added = 0;
                for (int c = c_start; c <= c_end && added < max_hosts; ++c) {
                    for (int d = 1; d <= 254 && added < max_hosts; ++d, ++added) {
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%d.%d.%d.%d", oct1, oct2, c, d);
                        out.push_back(buf);
                    }
                }
                return true;
            }
        }
    }

    //-------------------------------------------------------------------------
    // 处理逗号分隔的最后一段格式: a.b.c.d1,d2,d3
    // 例如: 192.168.2.1,3,5 展开为 192.168.2.1, 192.168.2.3, 192.168.2.5
    //-------------------------------------------------------------------------
    {
        auto parts = split(tok, '.');
        if (parts.size() == 4) {
            // 检查最后一段是否包含逗号
            const std::string& last = parts[3];
            if (last.find(',') != std::string::npos) {
                // 解析前三个八位组
                int a, b, c;
                if (!parse_int(parts[0].c_str(), a) ||
                    !parse_int(parts[1].c_str(), b) ||
                    !parse_int(parts[2].c_str(), c) ||
                    a < 0 || a > 255 ||
                    b < 0 || b > 255 ||
                    c < 0 || c > 255) {
                    fprintf(stderr, "IP八位组无效: %s\n", tok.c_str());
                    return false;
                }

                // 解析逗号分隔的最后一段
                auto d_parts = split(last, ',');
                unsigned int added = 0;

                for (const auto& d_str : d_parts) {
                    if (d_str.empty()) continue;

                    // 检查是否是范围格式 (如 1-5)
                    auto dash_pos = d_str.find('-');
                    if (dash_pos != std::string::npos) {
                        // 范围格式: d1-d2
                        std::string left = d_str.substr(0, dash_pos);
                        std::string right = d_str.substr(dash_pos + 1);
                        int d_start, d_end;

                        if (!parse_int(left.c_str(), d_start) ||
                            !parse_int(right.c_str(), d_end) ||
                            d_start < 0 || d_start > 255 ||
                            d_end < 0 || d_end > 255) {
                            fprintf(stderr, "最后一段范围无效: %s\n", d_str.c_str());
                            return false;
                        }

                        if (d_start > d_end) std::swap(d_start, d_end);

                        for (int d = d_start; d <= d_end && added < max_hosts; ++d, ++added) {
                            char buf[16];
                            snprintf(buf, sizeof(buf), "%d.%d.%d.%d", a, b, c, d);
                            out.push_back(buf);
                        }
                    } else {
                        // 单个数字
                        int d;
                        if (!parse_int(d_str.c_str(), d) || d < 0 || d > 255) {
                            fprintf(stderr, "最后一段数值无效: %s\n", d_str.c_str());
                            return false;
                        }

                        if (added < max_hosts) {
                            char buf[16];
                            snprintf(buf, sizeof(buf), "%d.%d.%d.%d", a, b, c, d);
                            out.push_back(buf);
                            ++added;
                        }
                    }
                }

                return true;
            }
        }
    }

    //-------------------------------------------------------------------------
    // 处理单个 IPv4 地址
    //-------------------------------------------------------------------------
    if (is_valid_ipv4_address(tok)) {
        out.push_back(tok);
        return true;
    }

    // 无法识别的格式
    fprintf(stderr, "无效的IP或目标格式: %s\n", tok.c_str());
    return false;
}

//=============================================================================
// IP 范围压缩函数
//=============================================================================

/**
 * @brief 将 IPv4 地址字符串转换为 32 位整数
 *
 * 使用 InetPtonA 解析 IP 地址，然后转换为主机字节序的 32 位整数。
 * 这便于进行 IP 地址的数值比较和排序。
 *
 * @param ip IPv4 地址字符串（点分十进制格式）
 * @return 主机字节序的 32 位整数，转换失败返回 0
 *
 * @example
 * @code
 * uint32_t val = ip_to_uint32("192.168.1.1");
 * // val = 0xC0A80101 = 3232235777
 * @endcode
 */
uint32_t ip_to_uint32(const std::string& ip) {
    in_addr addr;
    if (InetPtonA(AF_INET, ip.c_str(), &addr) == 1) {
        return ntohl(addr.S_un.S_addr);
    }
    return 0;
}

/**
 * @brief 将 IP 地址列表压缩为范围格式字符串
 *
 * 该函数将一组 IP 地址压缩为更易读的范围格式：
 * - 连续的 IP 地址合并为 "起始IP-结束最后一段" 格式
 * - 不连续的 IP 用逗号分隔
 * - 单个 IP 直接显示
 *
 * 算法步骤：
 * 1. 将 IP 地址转换为 32 位整数并排序
 * 2. 扫描找出连续的 IP 范围
 * 3. 格式化输出
 *
 * @param ips IP 地址字符串列表（可以是无序的）
 * @return 压缩后的范围格式字符串
 *
 * @example
 * @code
 * std::vector<std::string> ips = {
 *     "192.168.1.1", "192.168.1.2", "192.168.1.3",
 *     "192.168.1.5", "192.168.1.10"
 * };
 * std::string result = compress_ip_ranges(ips);
 * // result = "192.168.1.1-3, 192.168.1.5, 192.168.1.10"
 * @endcode
 *
 * @note 仅支持 IPv4 地址，IPv6 地址将单独列出不做合并
 */
std::string compress_ip_ranges(const std::vector<std::string>& ips) {
    if (ips.empty()) {
        return "(无)";
    }

    // 分离 IPv4 和 IPv6 地址
    std::vector<std::pair<uint32_t, std::string>> ipv4_list;
    std::vector<std::string> ipv6_list;

    for (const auto& ip : ips) {
        if (is_ipv6_address(ip)) {
            ipv6_list.push_back(ip);
        } else {
            uint32_t val = ip_to_uint32(ip);
            if (val != 0) {
                ipv4_list.push_back({val, ip});
            }
        }
    }

    // 按 IP 数值排序
    std::sort(ipv4_list.begin(), ipv4_list.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::string result;

    // 处理 IPv4 地址，找出连续范围
    if (!ipv4_list.empty()) {
        size_t i = 0;
        while (i < ipv4_list.size()) {
            // 记录范围起始
            size_t range_start = i;
            uint32_t start_ip = ipv4_list[i].first;

            // 找出连续的 IP
            while (i + 1 < ipv4_list.size() &&
                   ipv4_list[i + 1].first == ipv4_list[i].first + 1) {
                ++i;
            }

            // 格式化输出
            if (!result.empty()) {
                result += ", ";
            }

            if (i == range_start) {
                // 单个 IP
                result += ipv4_list[i].second;
            } else {
                // IP 范围
                uint32_t end_ip = ipv4_list[i].first;

                // 检查是否在同一个 /24 子网内
                if ((start_ip & 0xFFFFFF00) == (end_ip & 0xFFFFFF00)) {
                    // 同一子网，使用简短格式：192.168.1.1-10
                    result += ipv4_list[range_start].second;
                    result += "-";
                    result += std::to_string(end_ip & 0xFF);
                } else {
                    // 不同子网，使用完整格式：192.168.1.1-192.168.2.10
                    result += ipv4_list[range_start].second;
                    result += "-";
                    result += ipv4_list[i].second;
                }
            }

            ++i;
        }
    }

    // 添加 IPv6 地址（不做范围合并）
    for (const auto& ip : ipv6_list) {
        if (!result.empty()) {
            result += ", ";
        }
        result += ip;
    }

    return result.empty() ? "(无)" : result;
}

} // namespace qping
