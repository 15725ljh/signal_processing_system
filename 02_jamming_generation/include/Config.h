// ============================================================================
//  Config.h — 外部 JSON 配置文件管理器
//  External JSON Configuration Manager (Singleton Pattern)
// ============================================================================
//
//  本文件为全系统统一的参数配置管理器,采用单例模式(Singleton),所有模块
//  通过 Config::instance() 获取唯一实例.支持从外部 JSON 文件加载参数,
//  找不到文件时自动使用代码中的默认值,不影响程序运行.
//
//  核心特性:
//    1. 智能寻址: 按优先级自动搜索配置文件(环境变量→当前目录→上级目录→用户配置)
//    2. 注释剥离: 支持 // 和 # 行注释,用户可在 JSON 中自由添加说明
//    3. 尾逗号容错: 自动移除 JSON 中的尾逗号(如 { "a": 1, } → { "a": 1 })
//    4. 点分路径: 支持 "system.fc" 形式的嵌套 key 访问
//    5. null 安全: key 不存在或值为 null 时自动返回默认值
//    6. 单头文件: 仅依赖 nlohmann/json.hpp(位于 third_party/),无其他第三方依赖
//
//  配置文件格式:
//    {
//      // 这是行注释(支持 // 和 #)
//      "system": {
//        "fc": 16e9,           // 载波频率
//        "Tp": 12e-6,          // 脉冲宽度
//      },
//      "waveform": { ... },
//      "jamming": { ... },
//      "processing": { ... },
//      "recognition": { ... },
//      "detection_suppression": { ... }
//    }
//
//  使用示例:
//    // 在程序入口(main.cpp)加载配置:
//    Config::instance().load();
//
//    // 在模块头文件中定义快捷宏:
//    #define CFG Config::instance()
//
//    // 读取参数(找不到或为 null 时返回默认值):
//    double fc = CFG.getDouble("system.fc", 16e9);
//    int nrn = CFG.getInt("system.nan1", 64);
//
//  智能寻址顺序(找到第一个即停止):
//    1. 环境变量 SPS_CONFIG 指定的绝对路径
//    2. 可执行文件同目录: ./config.json
//    3. 项目根目录:     ../config.json  (从 build_all/ 运行时)
//    4. 用户配置目录:   ~/.config/sps/config.json  (跨平台)
//    5. 以上均未找到 → loaded_=false,所有读取返回默认值
//
//  依赖:
//    - nlohmann/json.hpp  JSON 解析库(单头文件,位于 third_party/)
//    - C++17 filesystem   文件存在性检查
//
// ============================================================================

#ifndef CONFIG_H
#define CONFIG_H

#include <string>          // STL字符串(std::string, std::getline, find, substr等)
#include <vector>          // STL动态数组(std::vector,用于拆分点分路径)
#include <fstream>         // 文件输入输出(std::ifstream,读取config.json)
#include <sstream>         // 字符串流(std::stringstream,用于逐行解析和JSON读取)
#include <iostream>        // 标准输入输出(std::cout打印加载状态, std::cerr打印错误)
#include <filesystem>      // C++17文件系统(std::filesystem::exists, is_regular_file)
#include <algorithm>       // STL算法(std::find等)

namespace std_fs = std::filesystem; // filesystem命名空间别名,简化代码

#define NLOHMANN_JSON_HPP              // 宏定义:防止 json.hpp 内部的 include guard
                                     // 阻止在已被其他文件包含后再次包含
#include "nlohmann/json.hpp"           // nlohmann JSON 解析库(单头文件,位于 third_party/)

/**
 * @class Config
 * @brief 外部 JSON 配置文件管理器(单例模式)
 *
 * 提供从外部 JSON 文件加载参数、按点分路径读取 double/int 值的功能.
 * 所有方法线程安全(仅 const 读取操作,无内部可变状态在读取时修改).
 *
 * 生命周期:
 *   1. 程序启动时调用 Config::instance().load() 加载配置文件
 *   2. 各模块通过 CFG.getDouble("key.path", default) / CFG.getInt("key.path", default) 读取参数
 *   3. 程序结束时由 static 局部变量的析构自动释放
 */
class Config {
public:

    /**
     * @brief 获取 Config 单例的唯一实例
     * @return Config& 单例引用
     *
     * 使用 Meyers' Singleton 模式(C++11 保证线程安全的局部静态变量初始化)
     */
    static Config& instance() {
        static Config inst;
        return inst;
    }

    /**
     * @brief 智能寻址并加载配置文件
     * @return true 成功加载外部文件, false 未找到文件(使用默认值)
     *
     * 按优先级依次搜索配置文件,找到第一个可读文件即加载并返回 true.
     * 搜索失败时 loaded_ 设为 false,后续所有 getDouble/getInt 返回默认值.
     */
    bool load() {
        std::string path = findConfigFile();
        if (path.empty()) {
            std::cout << "[Config] 未找到外部配置文件，使用全部默认参数" << std::endl;
            loaded_ = false;
            return false;
        }
        return loadFromFile(path);
    }

    /**
     * @brief 从指定路径加载配置文件
     * @param path 配置文件的绝对或相对路径
     * @return true 加载成功, false 文件无法打开或 JSON 解析失败
     *
     * 加载流程: 读取文件 → 剥离注释 → 去除尾逗号 → 解析 JSON → 存储到 data_
     */
    bool loadFromFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "[Config] 无法打开配置文件: " << path << std::endl;
            return false;
        }
        std::stringstream buf;
        buf << file.rdbuf();        // 将整个文件内容读入字符串流
        std::string raw = buf.str();
        file.close();

        std::string stripped = stripComments(raw);       // 第一步: 剥离 // 和 # 行注释
        stripped = removeTrailingCommas(stripped);        // 第二步: 去除尾逗号(兼容用户编辑习惯)
        try {
            data_ = nlohmann::json::parse(stripped);     // 第三步: 解析 JSON
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "[Config] JSON 解析错误 (" << path << "): " << e.what() << std::endl;
            return false;
        }
        configPath_ = path;
        loaded_ = true;
        std::cout << "[Config] 已加载配置文件: " << path << std::endl;
        return true;
    }

    /**
     * @brief 读取 double 类型参数
     * @param key          点分路径,如 "system.fc" 或 "detection_suppression.tsallis_q"
     * @param defaultValue 找不到 key 或值为 null 时的默认返回值
     * @return 配置文件中的值(double),或默认值
     *
     * 安全性: key 不存在/null/非数字 → 均返回 defaultValue
     */
    double getDouble(const std::string& key, double defaultValue) const {
        auto* val = findValue(key);             // 按点分路径查找 JSON 节点
        if (!val) return defaultValue;           // key 路径不存在
        if (val->is_null()) return defaultValue; // 值为 null(用户显式设为 null 表示用默认值)
        if (val->is_number()) return val->get<double>(); // 正常数值
        return defaultValue;                     // 非数字类型(如字符串)
    }

    /**
     * @brief 读取 int 类型参数
     * @param key          点分路径,如 "system.nan1" 或 "waveform.case1_freq_hop.N"
     * @param defaultValue 找不到 key 或值为 null 时的默认返回值
     * @return 配置文件中的值(int),或默认值
     *
     * 安全性: key 不存在/null/非数字 → 均返回 defaultValue
     * 注意: JSON 中 10.0 会被 get<int>() 截断为 10,这是预期行为
     */
    int getInt(const std::string& key, int defaultValue) const {
        auto* val = findValue(key);
        if (!val) return defaultValue;
        if (val->is_null()) return defaultValue;
        if (val->is_number()) return val->get<int>();
        return defaultValue;
    }

    /**
     * @brief 检查是否成功加载了外部配置文件
     * @return true 已加载外部文件, false 使用默认值模式
     */
    bool isLoaded() const { return loaded_; }

    /**
     * @brief 获取已加载的配置文件路径
     * @return 配置文件的绝对路径字符串,未加载时为空
     */
    const std::string& getConfigPath() const { return configPath_; }

    /**
     * @brief 获取底层 JSON 对象的只读引用
     * @return const nlohmann::json& JSON 数据引用
     *
     * 高级用途: 直接访问 JSON 树,如检查某个 key 是否存在、遍历数组等
     */
    const nlohmann::json& data() const { return data_; }

private:
    // ---- 禁止拷贝和赋值(保证单例唯一性) ----
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    nlohmann::json data_;          // 解析后的 JSON 数据树
    bool loaded_ = false;          // 是否成功加载了外部配置文件
    std::string configPath_;       // 已加载的配置文件路径

    /**
     * @brief 剥离 JSON 字符串中的行注释(支持 // 和 #)
     * @param jsonStr 含注释的原始 JSON 字符串
     * @return 去除注释后的纯净 JSON 字符串
     *
     * 算法: 逐字符扫描,跟踪是否在字符串字面量("...")内.
     * 在字符串外遇到 // 或 # 时,跳过直到行尾的所有字符.
     * 字符串内的 // 和 # 不被当作注释(正确处理 "url": "https://..." 等场景).
     */
    static std::string stripComments(const std::string& jsonStr) {
        std::string result;
        result.reserve(jsonStr.size());
        bool inString = false;      // 标记当前是否在 JSON 字符串字面量内
        for (size_t i = 0; i < jsonStr.size(); ++i) {
            char c = jsonStr[i];
            // 检测字符串边界: 遇到未转义的 " 时切换 inString 状态
            if (c == '"' && (i == 0 || jsonStr[i - 1] != '\\')) {
                inString = !inString;
            }
            if (!inString) {
                // 在字符串外: // 或 # 开启行注释,跳过直到换行符
                if ((c == '/' && i + 1 < jsonStr.size() && jsonStr[i + 1] == '/') ||
                    c == '#') {
                    while (i < jsonStr.size() && jsonStr[i] != '\n') ++i;
                    continue;
                }
            }
            result += c;
        }
        return result;
    }

    /**
     * @brief 去除 JSON 字符串中的尾逗号(trailing comma)
     * @param jsonStr 可能含尾逗号的 JSON 字符串
     * @return 去除尾逗号后的合法 JSON 字符串
     *
     * JSON 标准不允许尾逗号(如 { "a": 1, } 或 [1, 2, ]),
     * 但用户编辑配置文件时经常会留下尾逗号导致解析失败.
     * 本方法在逗号后紧跟 } 或 ] 时(忽略空白)删除该逗号.
     */
    static std::string removeTrailingCommas(const std::string& jsonStr) {
        std::string result = jsonStr;
        bool inString = false;
        for (size_t i = 0; i < result.size(); ++i) {
            char c = result[i];
            if (c == '"' && (i == 0 || result[i - 1] != '\\')) inString = !inString;
            if (!inString && c == ',') {
                // 检查逗号后(跳过空白)是否紧跟 } 或 ]
                size_t j = i + 1;
                while (j < result.size() && (result[j] == ' ' || result[j] == '\t' || result[j] == '\n' || result[j] == '\r')) ++j;
                if (j < result.size() && (result[j] == '}' || result[j] == ']')) {
                    result.erase(i, 1);  // 删除该逗号
                    --i;                // 调整索引(因为字符串变短了一位)
                }
            }
        }
        return result;
    }

    /**
     * @brief 按点分路径在 JSON 树中查找值
     * @param key 点分路径,如 "system.fc" 或 "detection_suppression.tsallis_q"
     * @return 指向对应 JSON 节点的指针,路径不存在时返回 nullptr
     *
     * 算法: 将 key 按 '.' 拆分为多个部分(如 "system" 和 "fc"),
     * 依次在 JSON 树中向下查找.任何一级不存在即返回 nullptr.
     */
    const nlohmann::json* findValue(const std::string& key) const {
        if (data_.is_null()) return nullptr;    // JSON 数据为空(未加载)

        // 按点分 '.' 拆分路径为多个部分
        std::vector<std::string> parts;
        std::stringstream ss(key);
        std::string part;
        while (std::getline(ss, part, '.')) {
            if (!part.empty()) parts.push_back(part);
        }

        // 逐级向下查找
        const nlohmann::json* current = &data_;
        for (const auto& p : parts) {
            if (!current->is_object() || !current->contains(p)) return nullptr;
            current = &((*current)[p]);
        }
        return current;
    }

    /**
     * @brief 按优先级搜索配置文件
     * @return 找到的配置文件绝对路径,未找到时返回空字符串
     *
     * 搜索顺序:
     *   1. 环境变量 SPS_CONFIG (如 export SPS_CONFIG=/path/to/config.json)
     *   2. 当前工作目录: ./config.json
     *   3. 上级目录: ../config.json (从 build_all/ 运行时定位到项目根目录)
     *   4. 用户配置目录: ~/.config/sps/config.json (Linux/Mac) 或 %USERPROFILE%\.config\sps\config.json (Windows)
     */
    std::string findConfigFile() const {
        // 优先级1: 环境变量 SPS_CONFIG
        const char* env = std::getenv("SPS_CONFIG");
        if (env && std::ifstream(env).good()) return env;

        // 候选路径列表(按优先级排序)
        std::vector<std::string> candidates = {
            "config.json",          // 优先级2: 当前工作目录
            "../config.json",       // 优先级3: 上级目录(从 build_all/ 运行时)
        };

        // 优先级4: 用户配置目录(跨平台)
#ifdef _WIN32
        const char* home = std::getenv("USERPROFILE");     // Windows: %USERPROFILE%
#else
        const char* home = std::getenv("HOME");             // Linux/Mac: $HOME
#endif
        if (home) {
            candidates.push_back(std::string(home) + "/.config/sps/config.json");
        }

        // 依次检查每个候选路径,返回第一个存在的文件
        for (const auto& c : candidates) {
            try {
                if (std_fs::exists(c) && std_fs::is_regular_file(c)) {
                    std_fs::path abs = std_fs::absolute(c); // 转为绝对路径
                    return abs.string();
                }
            } catch (...) {}   // 文件系统异常(如权限不足)时静默跳过
        }
        return "";  // 所有候选路径均未找到
    }
};

#endif // CONFIG_H
