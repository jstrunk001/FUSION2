#ifndef FUSION_CLI_ARGUMENTPARSER_H
#define FUSION_CLI_ARGUMENTPARSER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <iostream>
#include <algorithm>
#include <sstream>

namespace fusion::cli {

class ArgumentParser {
public:
    ArgumentParser(std::string programName, std::string description)
        : m_programName(std::move(programName)), m_description(std::move(description)) {}

    void AddFlag(const std::string& name, const std::string& description) {
        m_flags[name] = description;
    }

    void AddOption(const std::string& name, const std::string& description, const std::string& defaultValue = "") {
        m_options[name] = {description, defaultValue};
    }

    bool Parse(int argc, char* argv[]) {
        m_positionals.clear();
        m_parsedFlags.clear();
        m_parsedOptions.clear();

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "/?" || arg == "-h" || arg == "--help") {
                PrintHelp();
                return false;
            }

            if (arg.rfind("/", 0) == 0 || arg.rfind("-", 0) == 0) {
                std::string key = arg.substr(1);
                std::string value = "";
                size_t pos = key.find_first_of(":=");
                if (pos != std::string::npos) {
                    value = key.substr(pos + 1);
                    key = key.substr(0, pos);
                }
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);

                if (!value.empty()) {
                    m_parsedOptions[key] = value;
                } else if (i + 1 < argc && argv[i + 1][0] != '/' && argv[i + 1][0] != '-') {
                    m_parsedOptions[key] = argv[++i];
                } else {
                    m_parsedFlags.insert(key);
                }
            } else {
                m_positionals.push_back(arg);
            }
        }
        return true;
    }

    bool HasFlag(const std::string& name) const {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return m_parsedFlags.find(lower) != m_parsedFlags.end();
    }

    std::optional<std::string> GetOption(const std::string& name) const {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        auto it = m_parsedOptions.find(lower);
        if (it != m_parsedOptions.end()) return it->second;
        auto optIt = m_options.find(lower);
        if (optIt != m_options.end() && !optIt->second.defaultValue.empty()) {
            return optIt->second.defaultValue;
        }
        return std::nullopt;
    }

    const std::vector<std::string>& GetPositionalArgs() const {
        return m_positionals;
    }

    void SetPositionalArgsUsage(const std::string& usage) {
        m_positionalArgsUsage = usage;
    }

    void PrintHelp() const {
        std::cout << "Usage: " << m_programName << " ";
        if (!m_positionalArgsUsage.empty()) {
            std::cout << m_positionalArgsUsage << " ";
        }
        std::cout << "[other /options]\n";
        std::cout << m_description << "\n\nOptions:\n";
        for (const auto& [name, desc] : m_flags) {
            std::cout << "  /" << name << "\t" << desc << "\n";
        }
        for (const auto& [name, opt] : m_options) {
            std::cout << "  /" << name << ":<value>\t" << opt.description << " (default: " << opt.defaultValue << ")\n";
        }
    }

private:
    struct OptionMeta {
        std::string description;
        std::string defaultValue;
    };

    std::string m_programName;
    std::string m_description;
    std::string m_positionalArgsUsage{"<arguments>"};
    std::unordered_map<std::string, std::string> m_flags;
    std::unordered_map<std::string, OptionMeta> m_options;

    std::unordered_set<std::string> m_parsedFlags;
    std::unordered_map<std::string, std::string> m_parsedOptions;
    std::vector<std::string> m_positionals;
};

} // namespace fusion::cli

#endif // FUSION_CLI_ARGUMENTPARSER_H
