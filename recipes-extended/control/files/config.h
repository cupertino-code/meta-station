#ifndef _CONFIG_H_INCLUDED
#define _CONFIG_H_INCLUDED
#include <stdint.h>

#include "crsf_protocol.h"

#if defined(cplusplus) || defined(__cplusplus)
#include <yaml-cpp/yaml.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

struct VtxBand {
    int id;
    std::string name;
    std::vector<int> freq;
};

struct VrxBand {
    int id;
    std::string name;
    int channel;
    std::vector<int> freqs;
    std::vector<std::string> bands;
    std::vector<int> pwm;
};

struct Switch {
    int channel;
    std::vector<int> pwm;
};

struct PowerSetting {
    int id;
    std::string name;
};

struct Config {
    Switch vrx_switch;
    Switch tx_switch;
    std::vector<VrxBand> vrx_table;
    std::vector<VtxBand> vtx_table_58;
    std::vector<PowerSetting> power_table;
};

namespace YAML {
template <>
struct convert<PowerSetting> {
    static bool decode(const Node &node, PowerSetting &setting)
    {
        setting.id = node["id"].as<int>();
        setting.name = node["name"].as<std::string>();
        return true;
    }
};

template <>
struct convert<VtxBand> {
    static bool decode(const Node &node, VtxBand &band)
    {
        band.id = node["id"].as<int>();
        band.name = node["name"].as<std::string>();
        band.freq = node["freq"].as<std::vector<int>>();
        return true;
    }
};

template <>
struct convert<VrxBand> {
    static bool decode(const Node &node, VrxBand &band)
    {
        band.id = node["id"].as<int>();
        band.name = node["name"].as<std::string>();
        band.channel = node["channel"].as<int>();
        band.freqs = node["freqs"].as<std::vector<int>>();
        band.pwm = node["pwm"].as<std::vector<int>>();

        if (node["bands"]) {
            band.bands = node["bands"].as<std::vector<std::string>>();
        }
        return true;
    }
};

template <>
struct convert<Switch> {
    static bool decode(const Node &node, Switch &sw)
    {
        sw.channel = node["channel"].as<int>();
        sw.pwm = node["pwm"].as<std::vector<int>>();
        return true;
    }
};
}  // namespace YAML
#endif

#if defined(cplusplus) || defined(__cplusplus)
extern "C" {
#endif

#define CHANNELS_CNT 3
#define BAND_NAME_LEN 12
struct channel_data {
    uint16_t freq;
    char band_name[BAND_NAME_LEN];
    int selected;
    int tx_selected;
};

int load_config(const char *conf_name);
int get_chan_info(crsf_channels_t *crsf, struct channel_data *data, unsigned int *size);
#if defined(cplusplus) || defined(__cplusplus)
}
#endif

#endif  // _CONFIG_H_INCLUDED
