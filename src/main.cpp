/**
 * @file main.cpp
 * @brief qping 主程序 - 命令行解析、工作线程管理和统计输出
 * @author mrchzh <gmrchzh@gmail.com>
 * @version 1.1.0
 * @date 2026
 * @copyright MIT License
 *
 * 本模块是 qping 工具的入口点，负责：
 * - 解析命令行参数
 * - 初始化 Winsock 和控制台处理器
 * - 创建和管理工作线程池
 * - 执行 Ping 操作并收集结果
 * - 输出统计信息
 *
 * 支持的特性：
 * - 多目标并发 Ping
 * - 持续 Ping 模式（-t）
 * - Ctrl+C 优雅退出
 * - Ctrl+Break 显示中间统计
 */

#include "qping.h"

namespace qping {

//=============================================================================
// 帮助函数实现
//=============================================================================

/**
 * @brief 打印程序版本信息
 *
 * 输出程序名称、版本号、简要描述和作者信息。
 */
void print_version() {
    printf("qping 版本 %s\n", VERSION);
    printf("Windows Ping 替代工具，支持高级扫描功能\n");
    printf("作者: mrchzh <gmrchzh@gmail.com>\n");
}

/**
 * @brief 打印程序使用帮助
 *
 * 输出完整的命令行使用说明，包括：
 * - 目标格式说明（单个IP、CIDR、范围等）
 * - 标准 Ping 选项（-t, -n, -l 等）
 * - 扩展选项（--concurrency, --force 等）
 * - 使用示例
 *
 * @param prog 程序名称（通常为 argv[0]）
 */
void print_usage(const char* prog) {
    printf("用法: %s [选项] 目标1 [目标2 ...]\n", prog);

    printf("\n目标格式:\n");
    printf("  192.168.0.1                    单个IP地址\n");
    printf("  google.com                     域名（自动DNS解析）\n");
    printf("  192.168.1.1/24                 CIDR表示法\n");
    printf("  192.168.1.1-10                 最后一段范围 (a.b.c.d-e)\n");
    printf("  192.168.1-6                    第三段范围，第四段枚举 1..254\n");
    printf("  2001:db8::1                    IPv6地址\n");

    printf("\n标准ping选项:\n");
    printf("  -t                             持续ping直到被停止\n");
    printf("  -a                             解析地址为主机名\n");
    printf("  -n count                       发送回显请求的次数\n");
    printf("  -l size                        发送缓冲区大小(字节，最大%d)\n", MAX_PAYLOAD_SIZE);
    printf("  -f                             设置不分段标志(仅IPv4)\n");
    printf("  -i TTL                         生存时间\n");
    printf("  -v TOS                         服务类型(仅IPv4)\n");
    printf("  -r count                       记录路由跳数(1-%d，仅IPv4)\n", MAX_RECORD_ROUTE);
    printf("  -s count                       时间戳跳数(1-%d，仅IPv4)\n", MAX_TIMESTAMP);
    printf("  -j host-list                   宽松源路由(仅IPv4)\n");
    printf("  -k host-list                   严格源路由(仅IPv4)\n");
    printf("  -w timeout                     等待每次回复的超时时间(毫秒)\n");
    printf("  -S srcaddr                     使用的源地址\n");
    printf("  -4                             强制使用IPv4\n");
    printf("  -6                             强制使用IPv6\n");

    printf("\n扩展选项:\n");
    printf("  --concurrency N                并发线程数(默认 %d)\n", DEFAULT_CONCURRENCY);
    printf("  --force                        允许扫描超过 %u 个目标\n", MAX_HOSTS_DEFAULT);
    printf("  --exclude ip[,ip...]           排除逗号分隔的IP列表\n");
    printf("  -h, --help                     显示此帮助信息\n");
    printf("  --version                      显示版本信息\n");

    printf("\n域名解析:\n");
    printf("  - 支持ping域名（如 google.com），自动进行DNS解析\n");
    printf("  - 使用 -4 强制解析为IPv4地址\n");
    printf("  - 使用 -6 强制解析为IPv6地址\n");

    printf("\n示例:\n");
    printf("  %s 192.168.0.1\n", prog);
    printf("  %s -t 192.168.0.1\n", prog);
    printf("  %s -n 5 -l 64 192.168.0.1\n", prog);
    printf("  %s 192.168.1.1/24\n", prog);
    printf("  %s --concurrency 200 192.168.1.1/24\n", prog);
}

//=============================================================================
// 环境变量自动配置函数实现
//=============================================================================

/**
 * @brief 获取当前可执行文件的完整路径
 */
std::string get_executable_path() {
    char buffer[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return "";
    }
    return std::string(buffer);
}

/**
 * @brief 获取可执行文件所在的目录路径
 */
std::string get_executable_directory() {
    std::string exe_path = get_executable_path();
    if (exe_path.empty()) {
        return "";
    }
    
    size_t last_slash = exe_path.find_last_of("\\/");
    if (last_slash == std::string::npos) {
        return "";
    }
    
    return exe_path.substr(0, last_slash);
}

/**
 * @brief 检查指定路径是否已在系统 PATH 环境变量中
 */
bool is_path_in_environment(const std::string& path) {
    DWORD size = GetEnvironmentVariableA("PATH", NULL, 0);
    if (size == 0) {
        return false;
    }
    
    std::vector<char> buffer(size);
    GetEnvironmentVariableA("PATH", buffer.data(), size);
    
    std::string current_path(buffer.data());
    
    std::vector<std::string> paths = split(current_path, ';');
    for (const auto& p : paths) {
        if (_stricmp(p.c_str(), path.c_str()) == 0) {
            return true;
        }
    }
    
    return false;
}

/**
 * @brief 将指定路径添加到系统 PATH 环境变量
 */
bool add_path_to_environment(const std::string& path) {
    DWORD size = GetEnvironmentVariableA("PATH", NULL, 0);
    if (size == 0) {
        return false;
    }
    
    std::vector<char> buffer(size);
    GetEnvironmentVariableA("PATH", buffer.data(), size);
    
    std::string current_path(buffer.data());
    
    std::string new_path = current_path + ";" + path;
    
    HKEY hKey;
    LONG result = RegOpenKeyExA(
        HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
        0,
        KEY_WRITE,
        &hKey
    );
    
    if (result != ERROR_SUCCESS) {
        return false;
    }
    
    result = RegSetValueExA(
        hKey,
        "Path",
        0,
        REG_EXPAND_SZ,
        (const BYTE*)new_path.c_str(),
        (DWORD)new_path.size() + 1
    );
    
    RegCloseKey(hKey);
    
    if (result == ERROR_SUCCESS) {
        DWORD_PTR result;
        SendMessageTimeoutA(
            HWND_BROADCAST,
            WM_SETTINGCHANGE,
            0,
            (LPARAM)"Environment",
            SMTO_ABORTIFHUNG,
            5000,
            &result
        );
        return true;
    }
    
    return false;
}

/**
 * @brief 自动将当前可执行文件目录添加到系统 PATH 环境变量
 */
bool auto_add_to_path() {
    std::string exe_dir = get_executable_directory();
    if (exe_dir.empty()) {
        return false;
    }
    
    if (is_path_in_environment(exe_dir)) {
        return false;
    }
    
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }
    
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    PSID AdministratorsGroup;
    BOOL isAdmin = AllocateAndInitializeSid(
        &NtAuthority,
        2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &AdministratorsGroup
    );
    
    if (isAdmin) {
        if (!CheckTokenMembership(NULL, AdministratorsGroup, &isAdmin)) {
            isAdmin = FALSE;
        }
        FreeSid(AdministratorsGroup);
    }
    
    CloseHandle(hToken);
    
    if (!isAdmin) {
        return false;
    }
    
    printf("检测到 qping 未在系统 PATH 中，正在自动添加...\n");
    printf("安装路径: %s\n", exe_dir.c_str());
    
    if (add_path_to_environment(exe_dir)) {
        printf("已成功添加到系统 PATH 环境变量！\n");
        printf("请关闭并重新打开命令行窗口以使更改生效。\n");
        printf("\n");
        return true;
    } else {
        printf("添加失败，请手动添加到环境变量。\n");
        printf("\n");
        return false;
    }
}

} // namespace qping

//=============================================================================
// 全局变量
//=============================================================================

/**
 * @brief 指向停止标志的指针
 *
 * 用于控制台处理器通知工作线程停止运行。
 * 当用户按下 Ctrl+C 时，该标志被设置为 true。
 */
static std::atomic<bool>* g_stop_ptr = nullptr;

/**
 * @brief 指向显示统计标志的指针
 *
 * 用于控制台处理器通知主线程显示中间统计信息。
 * 当用户按下 Ctrl+Break 时，该标志被设置为 true。
 */
static std::atomic<bool>* g_show_ptr = nullptr;

//=============================================================================
// 控制台处理器
//=============================================================================

/**
 * @brief Windows 控制台控制事件处理函数
 *
 * 处理以下控制事件：
 * - CTRL_C_EVENT: 设置停止标志，优雅终止程序
 * - CTRL_BREAK_EVENT: 设置显示统计标志，输出中间结果
 *
 * @param ctrl_type 控制事件类型
 * @return TRUE 表示事件已处理，FALSE 表示使用默认处理
 *
 * @note 此函数由系统在控制台事件发生时调用，运行在单独的线程中
 */
static BOOL WINAPI win_console_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
            // Ctrl+C: 设置停止标志，准备退出
            if (g_stop_ptr) {
                g_stop_ptr->store(true);
            }
            return TRUE;

        case CTRL_BREAK_EVENT:
            // Ctrl+Break: 设置显示统计标志
            if (g_show_ptr) {
                g_show_ptr->store(true);
            }
            return TRUE;

        default:
            return FALSE;
    }
}

//=============================================================================
// 主函数
//=============================================================================

/**
 * @brief 程序入口点
 *
 * 执行以下步骤：
 * 1. 解析命令行参数
 * 2. 初始化 Winsock
 * 3. 枚举所有目标 IP 地址
 * 4. 注册控制台处理器
 * 5. 创建工作线程执行 Ping 操作
 * 6. 等待完成或用户中断
 * 7. 输出统计信息
 * 8. 清理资源并退出
 *
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return 退出码：
 *         - 0: 至少有一个目标响应
 *         - 1: 所有目标均无响应
 *         - 2: 参数错误
 *         - 3: 初始化失败
 */
/**
 * @brief 预热系统 DLL 和 API，减少首次运行的延迟
 * 
 * 在程序启动时提前加载和初始化所有需要的 DLL 和系统 API，
 * 避免在首次使用时才加载导致的延迟。这对于首次将程序拷贝到
 * 目标计算机时特别重要，可以显著减少首次运行的等待时间。
 * 
 * @note 此函数会快速初始化所有需要的系统组件，虽然首次调用
 *       可能仍需要一些时间（取决于系统），但可以避免在后续
 *       使用时的延迟。
 */
static void warmup_system_apis() {
    // 预热 Winsock API - 提前加载 ws2_32.dll 并初始化
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    WSACleanup();
    
    // 预热 ICMP API - 提前加载 icmp.dll 和 iphlpapi.dll
    // 通过创建和关闭句柄来触发 DLL 加载和初始化
    HANDLE h = IcmpCreateFile();
    if (h != INVALID_HANDLE_VALUE) {
        IcmpCloseHandle(h);
    }
    
    // 预热 IP Helper API - 确保 iphlpapi.dll 已加载
    // 只获取所需缓冲区大小，不实际分配内存，快速且轻量
    ULONG size = 0;
    GetAdaptersInfo(nullptr, &size);
}

int main(int argc, char** argv) {
    using namespace qping;

    //=========================================================================
    // 初始化控制台代码页，确保中文正确显示
    //=========================================================================
    // 强制设置控制台代码页为 GBK (936)，确保中文正确显示
    // 这是最可靠的方法，因为程序内部使用 GBK 编码（通过 -fexec-charset=gbk）
    SetConsoleOutputCP(936);  // GBK
    SetConsoleCP(936);         // GBK

    //=========================================================================
    // 自动添加到系统 PATH 环境变量
    //=========================================================================
    // 检测当前可执行文件目录是否已在系统 PATH 中
    // 如果不在且具有管理员权限，则自动添加
    // 这样用户只需双击运行即可完成安装
    bool auto_added = auto_add_to_path();

    //=========================================================================
    // 参数检查（快速路径，避免不必要的预热）
    //=========================================================================
    if (argc < 2) {
        print_usage(argv[0]);
        return 2;
    }
    
    // 快速检查帮助和版本选项，避免在这些情况下预热
    if (argc == 2) {
        std::string arg = argv[1];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--version") {
            print_version();
            return 0;
        }
    }

    //=========================================================================
    // 预热系统 API，减少首次运行的延迟
    //=========================================================================
    // 在程序启动时提前加载所有需要的 DLL，避免首次使用时延迟
    // 这对于首次将程序拷贝到目标计算机时特别重要
    warmup_system_apis();

    //=========================================================================
    // 初始化配置变量
    //=========================================================================

    // 运行参数
    int concurrency = DEFAULT_CONCURRENCY;  ///< 并发线程数
    int count_per_target = 1;               ///< 每个目标的 Ping 次数（0=无限）
    bool force = false;                     ///< 是否强制允许大量目标
    bool resolve_names = false;             ///< 是否解析主机名
    bool force_ipv4 = false;                ///< 强制使用 IPv4
    bool force_ipv6 = false;                ///< 强制使用 IPv6

    // Ping 配置选项
    PingOptions opts;

    // 排除列表和目标列表
    std::unordered_set<std::string> exclude_set;
    std::vector<std::string> tokens;

    //=========================================================================
    // 解析命令行参数
    //=========================================================================
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        //---------------------------------------------------------------------
        // 帮助和版本选项
        //---------------------------------------------------------------------
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--version") {
            print_version();
            return 0;
        }

        //---------------------------------------------------------------------
        // 扩展选项
        //---------------------------------------------------------------------
        if (arg == "--concurrency" && i + 1 < argc) {
            int v;
            if (!parse_int(argv[++i], v) || v <= 0) {
                fprintf(stderr, "无效的并发数\n");
                return 2;
            }
            concurrency = v;
            continue;
        }
        if (arg == "--force") {
            force = true;
            continue;
        }
        if (arg == "--exclude" && i + 1 < argc) {
            auto eps = split(argv[++i], ',');
            for (auto& e : eps) {
                if (!e.empty()) {
                    exclude_set.insert(e);
                }
            }
            continue;
        }

        //---------------------------------------------------------------------
        // 标准 Ping 选项
        //---------------------------------------------------------------------
        if (arg == "-t") {
            // 持续 Ping 模式
            count_per_target = 0;
            continue;
        }
        if (arg == "-n" && i + 1 < argc) {
            // 指定 Ping 次数
            int v;
            if (!parse_int(argv[++i], v) || v <= 0) {
                fprintf(stderr, "无效的计数\n");
                return 2;
            }
            count_per_target = v;
            continue;
        }
        if (arg == "-w" && i + 1 < argc) {
            // 超时时间
            int v;
            if (!parse_int(argv[++i], v) || v <= 0) {
                fprintf(stderr, "无效的超时时间\n");
                return 2;
            }
            opts.timeout_ms = v;
            continue;
        }
        if (arg == "-a") {
            // 解析主机名
            resolve_names = true;
            continue;
        }
        if (arg == "-l" && i + 1 < argc) {
            // 负载大小
            int v;
            if (!parse_int(argv[++i], v) || v < 0 || v > MAX_PAYLOAD_SIZE) {
                fprintf(stderr, "无效的缓冲区大小(0-%d)\n", MAX_PAYLOAD_SIZE);
                return 2;
            }
            opts.payload_size = v;
            continue;
        }
        if (arg == "-i" && i + 1 < argc) {
            // TTL
            int v;
            if (!parse_int(argv[++i], v) || v < 0 || v > 255) {
                fprintf(stderr, "无效的TTL(0-255)\n");
                return 2;
            }
            opts.ttl = v;
            continue;
        }
        if (arg == "-v" && i + 1 < argc) {
            // TOS
            int v;
            if (!parse_int(argv[++i], v) || v < 0 || v > 255) {
                fprintf(stderr, "无效的TOS(0-255)\n");
                return 2;
            }
            opts.tos = v;
            continue;
        }
        if (arg == "-f") {
            // 不分段标志
            opts.dont_fragment = true;
            continue;
        }
        if (arg == "-4") {
            // 强制 IPv4
            force_ipv4 = true;
            force_ipv6 = false;
            continue;
        }
        if (arg == "-6") {
            // 强制 IPv6
            force_ipv6 = true;
            force_ipv4 = false;
            continue;
        }
        if (arg == "-r" && i + 1 < argc) {
            // 记录路由
            int v;
            if (!parse_int(argv[++i], v) || v < 1 || v > MAX_RECORD_ROUTE) {
                fprintf(stderr, "无效的记录路由计数(1-%d)\n", MAX_RECORD_ROUTE);
                return 2;
            }
            opts.record_route = v;
            continue;
        }
        if (arg == "-s" && i + 1 < argc) {
            // 时间戳
            int v;
            if (!parse_int(argv[++i], v) || v < 1 || v > MAX_TIMESTAMP) {
                fprintf(stderr, "无效的时间戳计数(1-%d)\n", MAX_TIMESTAMP);
                return 2;
            }
            opts.timestamp = v;
            continue;
        }
        if (arg == "-j" && i + 1 < argc) {
            // 宽松源路由
            auto routes = split(argv[++i], ',');
            for (auto& route : routes) {
                if (!route.empty()) {
                    opts.loose_source_route.push_back(route);
                }
            }
            continue;
        }
        if (arg == "-k" && i + 1 < argc) {
            // 严格源路由
            auto routes = split(argv[++i], ',');
            for (auto& route : routes) {
                if (!route.empty()) {
                    opts.strict_source_route.push_back(route);
                }
            }
            continue;
        }
        if (arg == "-S" && i + 1 < argc) {
            // 源地址
            opts.source_address = argv[++i];
            continue;
        }

        //---------------------------------------------------------------------
        // 目标参数处理
        // 支持三种逗号用法：
        // 1. 多个独立目标：192.168.1.1,192.168.2.1（每部分都是完整IP或域名）
        // 2. 最后一段列表：192.168.2.1,3,5（只有第一部分是完整IP）
        // 3. 多个域名：google.com,localhost,yahoo.com（每个都是域名）
        //---------------------------------------------------------------------
        if (arg.find(',') != std::string::npos) {
            auto parts = split(arg, ',');
            // 检查是否所有部分都是完整的 IP 格式（包含点号）或域名
            bool all_complete_targets = true;
            for (const auto& p : parts) {
                // 完整目标应该是：IP地址（包含点号或冒号）或域名
                if (!p.empty() && p.find('.') == std::string::npos &&
                    p.find(':') == std::string::npos) {
                    // 可能是数字（如最后一段列表）或域名
                    // 检查是否是纯数字（最后一段列表格式）
                    bool is_number = true;
                    for (char c : p) {
                        if (c < '0' || c > '9') {
                            is_number = false;
                            break;
                        }
                    }
                    if (is_number) {
                        // 纯数字，可能是最后一段列表格式的一部分
                        all_complete_targets = false;
                        break;
                    } else {
                        // 不是纯数字，可能是域名
                        // 检查是否包含字母，可能是域名
                        bool has_letter = false;
                        for (char c : p) {
                            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                                has_letter = true;
                                break;
                            }
                        }
                        if (!has_letter) {
                            // 既不是数字也不是字母，无效格式
                            all_complete_targets = false;
                            break;
                        }
                    }
                }
            }

            if (all_complete_targets) {
                // 多个独立目标，分别添加
                for (auto& p : parts) {
                    if (!p.empty()) {
                        tokens.push_back(p);
                    }
                }
            } else {
                // 最后一段列表格式（如 192.168.2.1,3,5），作为整体处理
                tokens.push_back(arg);
            }
        } else {
            // 无逗号，直接添加
            tokens.push_back(arg);
        }
    }

    //=========================================================================
    // 验证参数
    //=========================================================================
    if (tokens.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    //=========================================================================
    // 初始化 Winsock
    //=========================================================================
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup失败\n");
        return 3;
    }

    //=========================================================================
    // 枚举所有目标 IP 地址（支持域名解析）
    //=========================================================================
    std::vector<std::string> all_targets;
    for (auto& tok : tokens) {
        // 检查是否是可能的主机名（域名）
        if (is_possible_hostname(tok)) {
            // 解析域名为IP地址
            std::vector<std::string> resolved_ips;
            if (force_ipv6) {
                // 只解析IPv6地址
                resolved_ips = resolve_to_ips(tok, true);
                // 过滤掉IPv4地址（如果有）
                std::vector<std::string> ipv6_only;
                for (const auto& ip : resolved_ips) {
                    if (ip.find(':') != std::string::npos) {
                        ipv6_only.push_back(ip);
                    }
                }
                resolved_ips = ipv6_only;
            } else if (force_ipv4) {
                // 只解析IPv4地址
                resolved_ips = resolve_to_ips(tok, false);
                // 过滤掉IPv6地址（如果有）
                std::vector<std::string> ipv4_only;
                for (const auto& ip : resolved_ips) {
                    if (ip.find(':') == std::string::npos) {
                        ipv4_only.push_back(ip);
                    }
                }
                resolved_ips = ipv4_only;
            } else {
                // 解析所有地址
                resolved_ips = resolve_to_ips(tok, false);
            }
            
            if (resolved_ips.empty()) {
                fprintf(stderr, "无法解析域名: %s\n", tok.c_str());
                WSACleanup();
                return 2;
            }
            
            // 添加解析到的IP地址
            for (auto& ip : resolved_ips) {
                if (exclude_set.find(ip) == exclude_set.end()) {
                    all_targets.push_back(ip);
                }
            }
        } else {
            // 不是域名，使用原来的IP/CIDR/范围解析逻辑
            std::vector<std::string> gen;
            if (!enumerate_targets(tok, gen, force ? UINT_MAX : MAX_HOSTS_DEFAULT)) {
                WSACleanup();
                return 2;
            }
            // 添加不在排除列表中的目标
            for (auto& ip : gen) {
                if (exclude_set.find(ip) == exclude_set.end()) {
                    all_targets.push_back(ip);
                }
            }
        }
    }

    // 检查是否有有效目标
    if (all_targets.empty()) {
        fprintf(stderr, "未生成任何目标\n");
        WSACleanup();
        return 2;
    }

    // 检查目标数量限制
    if (!force && all_targets.size() > MAX_HOSTS_DEFAULT) {
        fprintf(stderr, "目标数量(%zu)超过限制。使用 --force 覆盖\n", all_targets.size());
        WSACleanup();
        return 2;
    }

    printf("总目标数: %zu\n", all_targets.size());
    size_t N = all_targets.size();

    //=========================================================================
    // 初始化统计数据
    //=========================================================================

    /**
     * @struct Stat
     * @brief 每个目标的统计数据
     */
    struct Stat {
        std::atomic<uint64_t> sent{0};  ///< 已发送数据包数
        std::atomic<uint64_t> recv{0};  ///< 已接收数据包数
    };
    std::vector<Stat> stats(N);

    //=========================================================================
    // 初始化同步原语
    //=========================================================================
    std::atomic<bool> stop_flag{false};   ///< 停止标志
    std::atomic<bool> show_stats{false};  ///< 显示统计标志
    std::mutex print_mtx;                 ///< 输出互斥锁

    // 注册控制台处理器
    g_stop_ptr = &stop_flag;
    g_show_ptr = &show_stats;
    SetConsoleCtrlHandler(win_console_handler, TRUE);

    //=========================================================================
    // 创建工作线程
    //=========================================================================
    size_t worker_count = std::min<size_t>(std::max<int>(1, concurrency), N);
    std::atomic<size_t> rr_idx{0};  ///< 轮询索引
    std::vector<std::thread> workers;
    workers.reserve(worker_count);

    int per_target = count_per_target;
    const std::chrono::milliseconds ping_interval(1000);  ///< Ping 间隔

    // 启动工作线程
    for (size_t w = 0; w < worker_count; ++w) {
        workers.emplace_back([&]() {
            //=================================================================
            // 工作线程主循环
            //=================================================================
            while (!stop_flag.load()) {
                // 轮询选择目标
                size_t idx = rr_idx.fetch_add(1) % N;

                //---------------------------------------------------------
                // 检查是否已达到每个目标的 Ping 次数限制
                //---------------------------------------------------------
                if (per_target > 0) {
                    uint64_t prev = stats[idx].sent.fetch_add(1);
                    if ((int)prev >= per_target) {
                        // 已达限制，撤销计数
                        stats[idx].sent.fetch_sub(1);

                        // 检查是否所有目标都已完成
                        bool all_done = true;
                        for (size_t t = 0; t < N; ++t) {
                            if ((int)stats[t].sent.load() < per_target) {
                                all_done = false;
                                break;
                            }
                        }
                        if (all_done) {
                            stop_flag.store(true);
                            break;
                        }
                        // 短暂等待后重试
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        continue;
                    }
                } else {
                    // 无限模式，直接计数
                    stats[idx].sent.fetch_add(1);
                }

                //---------------------------------------------------------
                // 执行 Ping 操作
                //---------------------------------------------------------
                const std::string& target = all_targets[idx];
                int af = get_address_family(target);
                PingResult result;

                if (af == AF_INET && !force_ipv6) {
                    // IPv4 Ping
                    result = ping_ipv4(target, opts);
                } else if (af == AF_INET6 && !force_ipv4) {
                    // IPv6 Ping
                    result = ping_ipv6(target, opts);
                }

                // 更新接收计数
                if (result.success) {
                    stats[idx].recv.fetch_add(1);
                }

                //---------------------------------------------------------
                // 输出结果
                //---------------------------------------------------------
                {
                    std::lock_guard<std::mutex> lk(print_mtx);

                    // 可选：解析主机名
                    std::string hostname;
                    if (resolve_names) {
                        hostname = resolve_hostname(target, af);
                    }

                    if (result.success) {
                        // 成功回复
                        if (!hostname.empty()) {
                            printf("来自 %s [%s] 的回复: 字节=%d 时间=%lums TTL=%lu\n",
                                   hostname.c_str(), target.c_str(), opts.payload_size,
                                   (unsigned long)result.rtt_ms, (unsigned long)result.reply_ttl);
                        } else {
                            printf("来自 %s 的回复: 字节=%d 时间=%lums TTL=%lu\n",
                                   target.c_str(), opts.payload_size,
                                   (unsigned long)result.rtt_ms, (unsigned long)result.reply_ttl);
                        }

                        // 输出记录路由信息
                        if (!result.route_hops.empty()) {
                            printf("    路由: ");
                            for (size_t i = 0; i < result.route_hops.size(); ++i) {
                                if (i > 0) printf(" -> ");
                                printf("%s", result.route_hops[i].c_str());
                            }
                            printf("\n");
                        }

                        // 输出时间戳信息
                        if (!result.timestamps.empty()) {
                            printf("    时间戳: ");
                            for (size_t i = 0; i < result.timestamps.size(); ++i) {
                                if (i > 0) printf(", ");
                                printf("%ums", result.timestamps[i]);
                            }
                            printf("\n");
                        }
                    } else {
                        // 请求超时
                        if (!hostname.empty()) {
                            printf("请求超时 %s [%s]\n", hostname.c_str(), target.c_str());
                        } else {
                            printf("请求超时 %s\n", target.c_str());
                        }
                    }
                }

                //---------------------------------------------------------
                // 检查是否所有目标都已完成
                //---------------------------------------------------------
                if (per_target > 0) {
                    bool all_done = true;
                    for (size_t t = 0; t < N; ++t) {
                        if ((int)stats[t].sent.load() < per_target) {
                            all_done = false;
                            break;
                        }
                    }
                    if (all_done) {
                        stop_flag.store(true);
                        break;
                    }
                }

                // 等待后进行下一次 Ping
                std::this_thread::sleep_for(ping_interval);
            }
        });
    }

    //=========================================================================
    // 主线程等待循环
    //=========================================================================
    while (!stop_flag.load()) {
        // 检查是否需要显示中间统计（Ctrl+Break）
        if (show_stats.load()) {
            std::lock_guard<std::mutex> lk(print_mtx);
            printf("\n--- 中间统计 ---\n");

            uint64_t ts = 0, tr = 0;
            for (size_t i = 0; i < N; ++i) {
                ts += stats[i].sent.load();
                tr += stats[i].recv.load();
            }
            printf("总计: 已发送=%llu, 已接收=%llu\n",
                   (unsigned long long)ts, (unsigned long long)tr);

            show_stats.store(false);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    //=========================================================================
    // 等待所有工作线程结束
    //=========================================================================
    for (auto& th : workers) {
        if (th.joinable()) {
            th.join();
        }
    }

    //=========================================================================
    // 输出最终统计信息
    //=========================================================================
    printf("\n--- 统计信息 ---\n");

    uint64_t total_sent = 0, total_recv = 0;
    std::vector<std::string> online_ips;   // 在线设备列表
    std::vector<std::string> failed_ips;   // 失败设备列表

    // 收集统计数据并分类设备
    for (size_t i = 0; i < N; ++i) {
        uint64_t s = stats[i].sent.load();
        uint64_t r = stats[i].recv.load();
        uint64_t lost = (s > r) ? (s - r) : 0;
        double pct = (s > 0) ? (100.0 * lost / s) : 0.0;

        printf("%s : 已发送=%llu, 已接收=%llu, 丢失=%llu (%.1f%%)\n",
               all_targets[i].c_str(), (unsigned long long)s,
               (unsigned long long)r, (unsigned long long)lost, pct);

        total_sent += s;
        total_recv += r;

        // 分类：至少收到一个回复为在线，否则为失败
        if (r > 0) {
            online_ips.push_back(all_targets[i]);
        } else {
            failed_ips.push_back(all_targets[i]);
        }
    }

    // 输出汇总统计
    uint64_t total_lost = (total_sent > total_recv) ? (total_sent - total_recv) : 0;
    double total_pct = (total_sent > 0) ? (100.0 * total_lost / total_sent) : 0.0;

    printf("\n数据包统计: 发送=%llu, 接收=%llu, 丢失=%llu (%.1f%%)\n",
           (unsigned long long)total_sent, (unsigned long long)total_recv,
           (unsigned long long)total_lost, total_pct);

    // 输出在线/失败设备列表（使用范围压缩格式）
    printf("\n在线设备 (%zu): %s\n",
           online_ips.size(), compress_ip_ranges(online_ips).c_str());
    printf("失败设备 (%zu): %s\n",
           failed_ips.size(), compress_ip_ranges(failed_ips).c_str());

    //=========================================================================
    // 清理并退出
    //=========================================================================
    WSACleanup();

    // 返回码：至少有一个响应返回 0，否则返回 1
    return (total_recv > 0) ? 0 : 1;
}
