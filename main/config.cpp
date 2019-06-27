#include "config.hpp"

using namespace waterMonitor;

std::string waterMonitorConfig::getMqttHost() const { return this->mqttHost; }
std::string waterMonitorConfig::getMqttUser() const {return this->mqttUser; }
std::string waterMonitorConfig::getMqttPassword() const {return this->mqttPassword; }
std::string waterMonitorConfig::getMqttCARootCert()const {return this->mqttCARootCert; }

void waterMonitorConfig::loadConfig() {

}

void waterMonitorConfig::saveConfig() const {

}

waterMonitorConfig *myConfig = NULL;