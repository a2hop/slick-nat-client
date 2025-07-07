#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <regex>
#include <chrono>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <nlohmann/json.hpp>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

using json = nlohmann::json;

enum class LogLevel {
    ERROR = 0,
    WARNING = 1,
    INFO = 2,
    DEBUG = 3
};

struct ListenConfig {
    std::string address;
    int port;
    int socket_fd;
};

class SlickNatDaemon {
private:
    std::vector<ListenConfig> listen_configs;
    bool running;
    std::mutex mappings_mutex;
    std::map<std::string, std::string> internal_to_external;
    std::map<std::string, std::string> external_to_internal;
    std::string proc_mappings_path;
    std::string config_file_path;
    
    size_t last_mapping_count;
    bool proc_file_warning_shown;
    LogLevel log_level;
    
    struct NatMapping {
        std::string interface;
        std::string internal_prefix;
        std::string external_prefix;
        int prefix_len;
    };
    
    std::vector<NatMapping> mappings;
    
public:
    SlickNatDaemon(const std::string& config_path = "/etc/slnatcd/config",
                   const std::string& proc_path = "/proc/net/slick_nat_mappings")
        : running(false), proc_mappings_path(proc_path), config_file_path(config_path),
          last_mapping_count(0), proc_file_warning_shown(false), log_level(LogLevel::INFO) {}
    
    ~SlickNatDaemon() {
        stop();
    }
    
    bool load_config() {
        std::ifstream config_file(config_file_path);
        if (!config_file.is_open()) {
            log_warning("Cannot open config file " + config_file_path);
            log_info("Using default configuration");
            
            ListenConfig default_config;
            default_config.address = "::1";
            default_config.port = 7001;
            default_config.socket_fd = -1;
            listen_configs.push_back(default_config);
            return true;
        }
        
        listen_configs.clear();
        std::string line;
        int line_number = 0;
        
        while (std::getline(config_file, line)) {
            line_number++;
            
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            std::istringstream iss(line);
            std::string directive;
            iss >> directive;
            
            if (directive == "listen") {
                std::string address;
                int port;
                
                if (iss >> address >> port) {
                    ListenConfig config;
                    config.address = address;
                    config.port = port;
                    config.socket_fd = -1;
                    listen_configs.push_back(config);
                    log_info("Config: Will listen on [" + address + "]:" + std::to_string(port));
                } else {
                    log_error("Error parsing config line " + std::to_string(line_number) + ": " + line);
                }
            } else if (directive == "proc_path") {
                std::string path;
                if (iss >> path) {
                    proc_mappings_path = path;
                    log_info("Config: Using proc path: " + path);
                }
            } else if (directive == "log_level") {
                std::string level_str;
                if (iss >> level_str) {
                    log_level = parse_log_level(level_str);
                    log_info("Config: Log level set to " + level_str);
                }
            } else {
                log_warning("Unknown config directive on line " + std::to_string(line_number) + ": " + directive);
            }
        }
        
        if (listen_configs.empty()) {
            log_warning("No valid listen configurations found, using default");
            ListenConfig default_config;
            default_config.address = "::1";
            default_config.port = 7001;
            default_config.socket_fd = -1;
            listen_configs.push_back(default_config);
        }
        
        return true;
    }
    
    bool start() {
        if (!load_config()) {
            return false;
        }
        
        for (auto& config : listen_configs) {
            if (!create_listen_socket(config)) {
                log_error("Failed to create socket for [" + config.address + "]:" + std::to_string(config.port));
                stop();
                return false;
            }
        }
        
        running = true;
        log_info("SlickNat daemon started, listening on " + std::to_string(listen_configs.size()) + " addresses");
        
        reload_mappings();
        
        std::thread reload_thread(&SlickNatDaemon::mapping_reload_loop, this);
        reload_thread.detach();
        
        std::vector<std::thread> accept_threads;
        for (auto& config : listen_configs) {
            accept_threads.emplace_back(&SlickNatDaemon::accept_loop, this, std::ref(config));
        }
        
        for (auto& thread : accept_threads) {
            thread.join();
        }
        
        return true;
    }
    
    void stop() {
        running = false;
        for (auto& config : listen_configs) {
            if (config.socket_fd != -1) {
                close(config.socket_fd);
                config.socket_fd = -1;
            }
        }
        log_info("SlickNat daemon stopped");
    }
    
private:
    void log(LogLevel level, const std::string& message) {
        if (level <= log_level) {
            const char* level_str;
            switch (level) {
                case LogLevel::ERROR:   level_str = "ERROR"; break;
                case LogLevel::WARNING: level_str = "WARN"; break;
                case LogLevel::INFO:    level_str = "INFO"; break;
                case LogLevel::DEBUG:   level_str = "DEBUG"; break;
            }
            
            if (level == LogLevel::ERROR || level == LogLevel::WARNING) {
                std::cerr << "[" << level_str << "] " << message << std::endl;
            } else {
                std::cout << "[" << level_str << "] " << message << std::endl;
            }
        }
    }
    
    void log_error(const std::string& message) { log(LogLevel::ERROR, message); }
    void log_warning(const std::string& message) { log(LogLevel::WARNING, message); }
    void log_info(const std::string& message) { log(LogLevel::INFO, message); }
    void log_debug(const std::string& message) { log(LogLevel::DEBUG, message); }
    
    LogLevel parse_log_level(const std::string& level_str) {
        std::string lower_level = level_str;
        std::transform(lower_level.begin(), lower_level.end(), lower_level.begin(), ::tolower);
        
        if (lower_level == "error") return LogLevel::ERROR;
        if (lower_level == "warning" || lower_level == "warn") return LogLevel::WARNING;
        if (lower_level == "info") return LogLevel::INFO;
        if (lower_level == "debug") return LogLevel::DEBUG;
        
        return LogLevel::INFO;
    }
    
    bool create_listen_socket(ListenConfig& config) {
        config.socket_fd = socket(AF_INET6, SOCK_STREAM, 0);
        if (config.socket_fd == -1) {
            log_error("Failed to create socket for " + config.address);
            return false;
        }
        
        int ipv6only = 1;
        if (setsockopt(config.socket_fd, IPPROTO_IPV6, IPV6_V6ONLY, &ipv6only, sizeof(ipv6only)) == -1) {
            log_error("Failed to set IPv6 only for " + config.address);
            close(config.socket_fd);
            return false;
        }
        
        int reuse = 1;
        if (setsockopt(config.socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
            log_error("Failed to set socket reuse for " + config.address);
            close(config.socket_fd);
            return false;
        }
        
        struct sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(config.port);
        
        if (inet_pton(AF_INET6, config.address.c_str(), &addr.sin6_addr) != 1) {
            log_error("Invalid IPv6 address: " + config.address);
            close(config.socket_fd);
            return false;
        }
        
        if (bind(config.socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            log_error("Failed to bind to " + config.address + ":" + std::to_string(config.port));
            close(config.socket_fd);
            return false;
        }
        
        if (listen(config.socket_fd, 5) == -1) {
            log_error("Failed to listen on " + config.address + ":" + std::to_string(config.port));
            close(config.socket_fd);
            return false;
        }
        
        log_info("Listening on [" + config.address + "]:" + std::to_string(config.port));
        return true;
    }
    
    void accept_loop(ListenConfig& config) {
        while (running) {
            struct sockaddr_in6 client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_socket = accept(config.socket_fd, (struct sockaddr*)&client_addr, &client_len);
            
            if (client_socket == -1) {
                if (running) {
                    log_error("Accept failed on " + config.address + ":" + std::to_string(config.port));
                }
                continue;
            }
            
            char client_ip[INET6_ADDRSTRLEN];
            if (inet_ntop(AF_INET6, &client_addr.sin6_addr, client_ip, sizeof(client_ip))) {
                log_debug("Client connected from [" + std::string(client_ip) + "]:" + 
                         std::to_string(ntohs(client_addr.sin6_port)) + " to [" + config.address + "]:" + 
                         std::to_string(config.port));
            }
            
            std::thread client_thread(&SlickNatDaemon::handle_client, this, client_socket);
            client_thread.detach();
        }
    }
    
    void mapping_reload_loop() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (running) {
                reload_mappings();
            }
        }
    }
    
    bool reload_mappings() {
        std::ifstream proc_file(proc_mappings_path);
        if (!proc_file.is_open()) {
            if (!proc_file_warning_shown) {
                log_warning("Cannot open " + proc_mappings_path);
                proc_file_warning_shown = true;
            }
            return false;
        }
        
        if (proc_file_warning_shown) {
            log_info("Successfully reopened " + proc_mappings_path);
            proc_file_warning_shown = false;
        }
        
        std::lock_guard<std::mutex> lock(mappings_mutex);
        mappings.clear();
        internal_to_external.clear();
        external_to_internal.clear();
        
        std::string line;
        std::regex mapping_regex(R"(^(\S+)\s+([a-fA-F0-9:]+)/(\d+)\s+->\s+([a-fA-F0-9:]+)/(\d+)$)");
        
        while (std::getline(proc_file, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            std::smatch match;
            if (std::regex_match(line, match, mapping_regex)) {
                NatMapping mapping;
                mapping.interface = match[1];
                mapping.internal_prefix = match[2];
                mapping.external_prefix = match[4];
                mapping.prefix_len = std::stoi(match[3]);
                
                mappings.push_back(mapping);
                
                build_lookup_maps(mapping);
            }
        }
        
        if (mappings.size() != last_mapping_count) {
            log_info("Loaded " + std::to_string(mappings.size()) + " NAT mappings");
            last_mapping_count = mappings.size();
        }
        
        return true;
    }
    
    void build_lookup_maps(const NatMapping& mapping) {
        std::string internal_key = mapping.internal_prefix + "/" + std::to_string(mapping.prefix_len);
        std::string external_key = mapping.external_prefix + "/" + std::to_string(mapping.prefix_len);
        
        internal_to_external[internal_key] = external_key;
        external_to_internal[external_key] = internal_key;
    }
    
    void handle_client(int client_socket) {
        char buffer[1024];
        ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_read <= 0) {
            close(client_socket);
            return;
        }
        
        buffer[bytes_read] = '\0';
        
        try {
            json request = json::parse(buffer);
            json response = process_request(request);
            
            std::string response_str = response.dump();
            send(client_socket, response_str.c_str(), response_str.length(), 0);
            
        } catch (const std::exception& e) {
            json error_response = {{"error", e.what()}};
            std::string error_str = error_response.dump();
            send(client_socket, error_str.c_str(), error_str.length(), 0);
        }
        
        close(client_socket);
    }
    
    json process_request(const json& request) {
        std::string command = request.value("command", "");
        
        if (command == "resolve_ip") {
            std::string ip = request.value("ip", "");
            if (ip.empty()) {
                return {{"error", "Missing IP parameter"}};
            }
            return resolve_ip(ip);
        } else if (command == "get_global_ip" || command == "get2kip") {
            std::string ip = request.value("ip", "");
            if (ip.empty()) {
                return {{"error", "Missing IP parameter"}};
            }
            return get_global_ip(ip);
        } else if (command == "ping") {
            return {{"status", "pong"}};
        } else {
            return {{"error", "Unknown command: " + command}};
        }
    }
    
    json resolve_ip(const std::string& ip) {
        if (!is_valid_ipv6(ip)) {
            return {{"error", "Invalid IPv6 address format"}};
        }
        
        std::lock_guard<std::mutex> lock(mappings_mutex);
        
        for (const auto& mapping : mappings) {
            if (ip_matches_prefix(ip, mapping.internal_prefix, mapping.prefix_len)) {
                std::string public_ip = remap_address(ip, mapping.internal_prefix, 
                                                     mapping.external_prefix, mapping.prefix_len);
                return {
                    {"internal_ip", ip},
                    {"public_ip", public_ip},
                    {"interface", mapping.interface},
                    {"status", "success"}
                };
            }
        }
        
        for (const auto& mapping : mappings) {
            if (ip_matches_prefix(ip, mapping.external_prefix, mapping.prefix_len)) {
                std::string internal_ip = remap_address(ip, mapping.external_prefix, 
                                                       mapping.internal_prefix, mapping.prefix_len);
                return {
                    {"external_ip", ip},
                    {"internal_ip", internal_ip},
                    {"interface", mapping.interface},
                    {"status", "success"}
                };
            }
        }
        
        return {
            {"ip", ip},
            {"error", "IP not found in mappings"},
            {"status", "not_found"}
        };
    }
    
    json get_global_ip(const std::string& ip) {
        if (!is_valid_ipv6(ip)) {
            return {{"error", "Invalid IPv6 address format"}};
        }
        
        std::lock_guard<std::mutex> lock(mappings_mutex);
        
        log_debug("Looking for global IP mapping for: " + ip);
        
        for (const auto& mapping : mappings) {
            log_debug("Checking if " + ip + " matches prefix " + mapping.internal_prefix + 
                     "/" + std::to_string(mapping.prefix_len));
            
            if (ip_matches_prefix(ip, mapping.internal_prefix, mapping.prefix_len)) {
                std::string global_ip = remap_address(ip, mapping.internal_prefix, 
                                                     mapping.external_prefix, mapping.prefix_len);
                
                log_debug("Found match! Mapped to: " + global_ip);
                
                struct in6_addr addr;
                if (inet_pton(AF_INET6, global_ip.c_str(), &addr) == 1) {
                    if ((addr.s6_addr[0] & 0xE0) == 0x20) {
                        return {
                            {"internal_ip", ip},
                            {"global_ip", global_ip},
                            {"interface", mapping.interface},
                            {"status", "success"}
                        };
                    } else {
                        log_debug("Mapped IP " + global_ip + " is not in global unicast range (2000::/3)");
                    }
                } else {
                    log_debug("Failed to parse mapped IP: " + global_ip);
                }
            }
        }
        
        return {
            {"ip", ip},
            {"error", "No global unicast mapping found for " + ip},
            {"status", "not_found"},
            {"available_mappings", mappings.size()}
        };
    }
    
    bool is_valid_ipv6(const std::string& ip) {
        struct in6_addr addr;
        return inet_pton(AF_INET6, ip.c_str(), &addr) == 1;
    }
    
    bool ip_matches_prefix(const std::string& ip, const std::string& prefix, int prefix_len) {
        struct in6_addr ip_addr, prefix_addr;
        
        if (inet_pton(AF_INET6, ip.c_str(), &ip_addr) != 1 ||
            inet_pton(AF_INET6, prefix.c_str(), &prefix_addr) != 1) {
            return false;
        }
        
        int bytes = prefix_len / 8;
        int bits = prefix_len % 8;
        
        for (int i = 0; i < bytes; i++) {
            if (ip_addr.s6_addr[i] != prefix_addr.s6_addr[i]) {
                return false;
            }
        }
        
        if (bits > 0 && bytes < 16) {
            uint8_t mask = (0xFF << (8 - bits)) & 0xFF;
            if ((ip_addr.s6_addr[bytes] & mask) != (prefix_addr.s6_addr[bytes] & mask)) {
                return false;
            }
        }
        
        return true;
    }
    
    std::string remap_address(const std::string& ip, const std::string& old_prefix, 
                             const std::string& new_prefix, int prefix_len) {
        struct in6_addr ip_addr, old_prefix_addr, new_prefix_addr;
        
        if (inet_pton(AF_INET6, ip.c_str(), &ip_addr) != 1 ||
            inet_pton(AF_INET6, old_prefix.c_str(), &old_prefix_addr) != 1 ||
            inet_pton(AF_INET6, new_prefix.c_str(), &new_prefix_addr) != 1) {
            return ip;
        }
        
        int bytes = prefix_len / 8;
        int bits = prefix_len % 8;
        
        for (int i = 0; i < bytes && i < 16; i++) {
            ip_addr.s6_addr[i] = new_prefix_addr.s6_addr[i];
        }
        
        if (bits > 0 && bytes < 16) {
            uint8_t mask = (0xFF << (8 - bits)) & 0xFF;
            ip_addr.s6_addr[bytes] = (new_prefix_addr.s6_addr[bytes] & mask) | 
                                    (ip_addr.s6_addr[bytes] & ~mask);
        }
        
        char result[INET6_ADDRSTRLEN];
        if (inet_ntop(AF_INET6, &ip_addr, result, sizeof(result))) {
            return std::string(result);
        }
        
        return ip;
    }
};

SlickNatDaemon* g_daemon = nullptr;

void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << std::endl;
    if (g_daemon) {
        g_daemon->stop();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    std::string config_path = "/etc/slnatcd/config";
    std::string proc_path = "/proc/net/slick_nat_mappings";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--proc" && i + 1 < argc) {
            proc_path = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [--config PATH] [--proc PATH]\n";
            std::cout << "  --config PATH   Configuration file path (default: /etc/slnatcd/config)\n";
            std::cout << "  --proc PATH     Kernel proc file path (default: /proc/net/slick_nat_mappings)\n";
            std::cout << "\nConfig file options:\n";
            std::cout << "  listen <address> <port>   Listen on specified address and port\n";
            std::cout << "  proc_path <path>          Set kernel proc file path\n";
            std::cout << "  log_level <level>         Set log level (error, warning, info, debug)\n";
            return 0;
        }
    }
    
    SlickNatDaemon daemon(config_path, proc_path);
    g_daemon = &daemon;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    if (!daemon.start()) {
        std::cerr << "Failed to start daemon" << std::endl;
        return 1;
    }
    
    return 0;
}
