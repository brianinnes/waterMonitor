#pragma once
#ifndef WATER_MONITOR_CONFIG_H
#define WATER_MONITOR_CONFIG_H

#include <string>

namespace waterMonitor
{

    class waterMonitorConfig {
    public:
        std::string getMqttHost() const;
        std::string getMqttUser() const;
        std::string getMqttPassword() const;
        std::string getMqttCARootCert() const;

        void loadConfig();
        void saveConfig() const;

    private:
        std::string mqttHost;
        std::string mqttUser;
        std::string mqttPassword;
        std::string mqttCARootCert;
    };

}

#endif // WATER_MONITOR_CONFIG_H