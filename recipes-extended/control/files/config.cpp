#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <yaml-cpp/yaml.h>
#include <string.h>
#include "config.h"
#include "crsf_protocol.h"

static int initialized = 0;

Config parse_yaml(const std::string &filepath)
{
    try {
        YAML::Node config_root = YAML::LoadFile(filepath);
        Config cfg;

        cfg.vrx_switch.channel = config_root["vrx_switch"]["channel"].as<int>();
        cfg.vrx_switch.pwm = config_root["vrx_switch"]["pwm"].as<std::vector<int>>();

        for (const auto &node : config_root["vrx_table"]) {
            cfg.vrx_table.push_back(node.as<VrxBand>());
        }

        for (const auto &node : config_root["vtx_table_58"]) {
            cfg.vtx_table_58.push_back(node.as<VtxBand>());
        }

        for (const auto &node : config_root["power_table"]) {
            cfg.power_table.push_back(node.as<PowerSetting>());
        }

        return cfg;
    }
    catch (const YAML::BadFile &e) {
        std::cerr << "Can't open file " << filepath << std::endl;
        throw;
    }
    catch (const YAML::Exception &e) {
        std::cerr << "Can't parse file " << e.what() << std::endl;
        throw;
    }
}
static Config config;

extern "C" int load_config(const char *conf_name)
{
    try {
        config = parse_yaml(conf_name);
        initialized = 1;
    }
    catch (const std::exception &e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        return -1;
    }
    return 0;
}

static uint16_t get_channel_us(crsf_channels_t *crsf, int chan_num)
{
    uint16_t ticks;

    if (!crsf)
        return 0;

    switch (chan_num) {
    case 0:
        ticks = crsf->ch0;
        break;
    case 1:
        ticks = crsf->ch1;
        break;
    case 2:
        ticks = crsf->ch2;
        break;
    case 3:
        ticks = crsf->ch3;
        break;
    case 4:
        ticks = crsf->ch4;
        break;
    case 5:
        ticks = crsf->ch5;
        break;
    case 6:
        ticks = crsf->ch6;
        break;
    case 7:
        ticks = crsf->ch7;
        break;
    case 8:
        ticks = crsf->ch8;
        break;
    case 9:
        ticks = crsf->ch9;
        break;
    case 10:
        ticks = crsf->ch10;
        break;
    case 11:
        ticks = crsf->ch11;
        break;
    case 12:
        ticks = crsf->ch12;
        break;
    case 13:
        ticks = crsf->ch13;
        break;
    case 14:
        ticks = crsf->ch14;
        break;
    case 15:
        ticks = crsf->ch15;
        break;
    default:
        return 0;
    }
    return TICKS_TO_US(ticks);
}

int get_chan_info(crsf_channels_t *crsf, struct channel_data *data, unsigned int *size)
{
    uint16_t usval;
    int cnt = 0;
    int active = -1;
    if (!initialized) {
        *size = 0;
        return 0;
    }
    usval = get_channel_us(crsf, config.vrx_switch.channel - 1);
    if (!usval)
        return -1;
    for (unsigned int i = 0; i < config.vrx_switch.pwm.size(); i++)
        if (usval < config.vrx_switch.pwm[i]) {
            active = i;
            break;
        }
    for (unsigned int i = 0; i < config.vrx_table.size(); i++, cnt++) {
        if (i >= *size)
            break;
        usval = get_channel_us(crsf, config.vrx_table[i].channel - 1);
        data[cnt].selected = 0;
        if (config.vrx_table[i].id == active) {
            data[cnt].selected = 1;
        }
        for (unsigned int j = 0; j < config.vrx_table[i].freqs.size(); j++) {
            if (usval <= config.vrx_table[i].pwm[j]) {
                data[cnt].freq = config.vrx_table[i].freqs[j];
                if (config.vrx_table[i].bands.size() > j) {
                    snprintf(data[cnt].band_name, sizeof(data[cnt].band_name), "%s:%s",
                            config.vrx_table[i].name.c_str(), config.vrx_table[i].bands[j].c_str());
                } else {
                    snprintf(data[cnt].band_name, sizeof(data[cnt].band_name), "%s:%d",
                             config.vrx_table[i].name.c_str(), j+1);
                }
                break;
            }
        }
    }
    *size = cnt;
    return *size;
}
