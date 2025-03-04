#pragma once
#include "hl_gcfg/hlgcfg.hpp"
#include "hlgcfg_log.hpp"
#include "hl_gcfg/string_utils.hpp"
namespace hl_gcfg{
HLGCFG_INLINE_NAMESPACE{

template<class T>
GcfgDefaultItem<T>::GcfgDefaultItem(const T& defaultVal)
: m_default(defaultVal)
{
}

template<class T>
GcfgDefaultItem<T>& GcfgDefaultItem<T>::operator<<(std::pair<uint32_t, T> const & deviceValuePair)
{
    if (deviceValuePair.first < m_defaultsPerDevice.size())
    {
        operator<<(std::tuple(deviceValuePair.first,NNExecutionMode::training,deviceValuePair.second));
        operator<<(std::tuple(deviceValuePair.first,NNExecutionMode::inference,deviceValuePair.second));
    }
    else
    {
        HLGCFG_LOG_CRITICAL("device type {} (value {}) is not supported for default item. ignore.",
                            (int32_t)deviceValuePair.first,
                            toString(deviceValuePair.second));
        m_default = deviceValuePair.second;
    }
    return *this;
}

template<class T>
GcfgDefaultItem<T>& GcfgDefaultItem<T>::operator<<(std::tuple<uint32_t, NNExecutionMode, T> const & deviceValueTuple)
{
    if (std::get<0>(deviceValueTuple) < m_defaultsPerDevice.size() && static_cast<unsigned>(std::get<1>(deviceValueTuple)) < 2)
    {
        m_defaultsPerDevice[std::get<0>(deviceValueTuple)][static_cast<unsigned>(std::get<1>(deviceValueTuple))] = std::get<2>(deviceValueTuple);
    }
    else
    {
        HLGCFG_LOG_CRITICAL("device type {}, mode type {}, (value {}) is not supported for default item. ignore.",
                            (int32_t)(std::get<0>(deviceValueTuple)),
                            static_cast<unsigned>(std::get<1>(deviceValueTuple)),
                            toString(std::get<2>(deviceValueTuple)));
        m_default = std::get<2>(deviceValueTuple);
    }
    return *this;
}

template<class T>
const T& GcfgDefaultItem<T>::value(uint32_t device) const
{
    if (device >= m_defaultsPerDevice.size() || (static_cast<int>(m_defaultsPerDevice[device][0].has_value()) + static_cast<int>(m_defaultsPerDevice[device][1].has_value()) == 0))
    {
        return m_default.value();
    }
    const auto & item = m_defaultsPerDevice[device][static_cast<unsigned>(getModeType())];
    return item.has_value() ? item.value() : m_default.value();
}

template<class T>
std::string GcfgDefaultItem<T>::getValueStr() const
{
    std::string out;
    bool first = true;
    auto processDefault = [&](std::optional<T> const & defaultValue, uint32_t deviceType){
        if (defaultValue.has_value())
        {
            if (!first)
            {
                out += " | ";
            }
            out += std::to_string((int32_t)deviceType) + ": " + toString(defaultValue.value());
            first = false;
        }
    };
    for (uint32_t deviceT = 0; deviceT < m_defaultsPerDevice.size(); ++deviceT)
    {
        processDefault(m_defaultsPerDevice[deviceT][static_cast<unsigned>(getModeType())], deviceT);
    }
    processDefault(m_default, InvalidDeviceType);
    return out;
}
}}