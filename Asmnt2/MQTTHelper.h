#pragma once

// Shared helpers for the MQTT publisher and subscriber apps.
//
// No `using namespace std;` here: this is a header, and a using-directive
// at global scope leaks into every file that includes it. Nor does it
// include MQTTPublisher.h - a helper should not depend on the class that
// uses it.

#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#define CPU_TEMP "/sys/class/thermal/thermal_zone0/temp" // virtual file holding the SoC temperature
#define MQTT_TIMEOUT 10000L

// Reads the SoC temperature in degrees Celsius.
// Returns false if the sysfs file cannot be read, rather than silently
// reporting 0 degrees - a plausible-looking zero on a dashboard is worse
// than an error.
inline bool readCPUTemperature(float& celsius)
{
    std::ifstream fs(CPU_TEMP);
    if (!fs) {
        return false;
    }

    int milliDegrees = 0;
    fs >> milliDegrees;
    if (!fs) {
        return false;
    }

    celsius = static_cast<float>(milliDegrees) / 1000.0f;
    return true;
}

// Parses "--key value" pairs from the command line into a map, so nothing
// has to be hardcoded in the source (and no credentials end up in git).
inline std::map<std::string, std::string> parseArgs(int argc, char* argv[])
{
    std::map<std::string, std::string> args;
    for (int i = 1; i < argc; i++) {
        const std::string key = argv[i];
        if (key.rfind("--", 0) == 0 && i + 1 < argc) {
            args[key] = argv[i + 1];
            i++;
        }
    }
    return args;
}

// Returns true when every named option is present and non-empty, and
// prints a usage line naming the ones that are missing.
inline bool requireArgs(const std::map<std::string, std::string>& args,
                        const std::vector<std::string>& required,
                        const char* programName)
{
    std::string missing;
    for (const std::string& key : required) {
        const std::map<std::string, std::string>::const_iterator it = args.find(key);
        if (it == args.end() || it->second.empty()) {
            missing += (missing.empty() ? "" : " ");
            missing += key;
        }
    }

    if (missing.empty()) {
        return true;
    }

    std::fprintf(stderr, "%s: missing required option(s): %s\n", programName, missing.c_str());
    std::fprintf(stderr,
                 "usage: %s --address tcp://<host>:1883 --client <id> "
                 "--user <username> --pword <password> --topic <topic> "
                 "[--qos 0|1|2] [--cleansession 0|1]\n",
                 programName);
    std::fprintf(stderr,
                 "note: pass the password on the command line or via your shell's "
                 "history-free input; never hardcode it in the source.\n");
    return false;
}
