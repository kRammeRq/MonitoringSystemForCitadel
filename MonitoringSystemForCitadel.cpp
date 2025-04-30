#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <regex>
#include <map>
#include <set>
using json = nlohmann::json;

struct Config {
    int period;
    std::vector<int> cpu_ids;
    std::set<std::string> memory_specs;
    bool output_console = false;
    bool output_log = false;
    std::string log_path;
};

Config parse_config(const std::string& path) {
    std::ifstream file(path);
    json config_json;
    file >> config_json;
    Config config;
    config.period = std::stoi(config_json["settings"]["period"].get<std::string>());
    for (const auto& metric : config_json["metrics"]) {
        if (metric["type"] == "cpu") {
            for (int id : metric["ids"]) {
                config.cpu_ids.push_back(id);
            }
        } else if (metric["type"] == "memory") {
            for (const auto& spec : metric["spec"]) {
                config.memory_specs.insert(spec);
            }
        }
    }
    for (const auto& output : config_json["outputs"]) {
        if (output["type"] == "console") {
            config.output_console = true;
        } else if (output["type"] == "log") {
            config.output_log = true;
            config.log_path = output["path"];
        }
    }
    return config;
}

std::map<int, double> get_cpu_usage(const std::vector<int>& ids) {
    std::ifstream stat("/proc/stat");
    std::string line;
    std::map<int, double> cpu_load;
    while (std::getline(stat, line)) {
        if (line.find("cpu") == 0 && line[3] >= '0' && line[3] <= '9') {
            int id = std::stoi(line.substr(3, line.find(" ", 3)));
            if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
                std::istringstream iss(line);
                std::string label;
                long user, nice, system, idle;
                iss >> label >> user >> nice >> system >> idle;
                long total = user + nice + system + idle;
                cpu_load[id] = 100.0 * (user + nice + system) / total;
            }
        }
    }
    return cpu_load;
}

std::map<std::string, long> get_memory_usage(const std::set<std::string>& specs) {
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    std::map<std::string, long> memory_data;
    while (std::getline(meminfo, line)) {
        std::istringstream iss(line);
        std::string key;
        long value;
        std::string unit;
        iss >> key >> value >> unit;
        key = key.substr(0, key.length() - 1); 
        if (specs.count("used") || specs.count("free")) {
            if (key == "MemTotal") memory_data["total"] = value;
            else if (key == "MemFree") memory_data["free"] = value;
            else if (key == "Buffers") memory_data["buffers"] = value;
            else if (key == "Cached") memory_data["cached"] = value;
        }
    }

    if (specs.count("used")) {
        memory_data["used"] = memory_data["total"] - memory_data["free"] - memory_data["buffers"] - memory_data["cached"];
    }

    return memory_data;
}

void output_metrics(const Config& config) {
    std::ostringstream out;
    out << "[METRICS]" << std::endl;
    if (!config.cpu_ids.empty()) {
        auto cpu = get_cpu_usage(config.cpu_ids);
        for (const auto& [id, usage] : cpu) {
            out << "CPU" << id << ": " << usage << "%\n";
        }
    }
    if (!config.memory_specs.empty()) {
        auto mem = get_memory_usage(config.memory_specs);
        for (const auto& spec : config.memory_specs) {
            if (mem.count(spec)) {
                out << "Memory " << spec << ": " << mem[spec] << " kB\n";
            }
        }
    }
    std::string result = out.str();
    if (config.output_console) {
        std::cout << result << std::endl;
    }
    if (config.output_log && !config.log_path.empty()) {
        std::ofstream logfile(config.log_path, std::ios::app);
        logfile << result;
    }
}


int main() {
    Config config = parse_config("config.json"); 
    if (!config.output_console && !config.output_log) {
        std::cerr << "Error: no output method is specified (console or log)" << std::endl;
        return 1;
    }
    std::cout << "Monitoring every " << config.period << " seconds." << std::endl;
    while (true) {
        output_metrics(config);
        std::this_thread::sleep_for(std::chrono::seconds(config.period)); 
    }
    return 0;
}