/*  Audio Stream Info
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include "AudioStreamInfo.h"

namespace PokemonAutomation{


AudioStreamInfo::AudioStreamInfo(std::string url, AudioChannelFormat format, std::string name)
    : m_url(std::move(url))
    , m_name(std::move(name))
    , m_format(format)
{}

std::string AudioStreamInfo::display_name() const{
    if (m_url.empty()){
        return "Network Stream";
    }
    if (!m_name.empty()){
        return "Network Stream: " + m_name;
    }
    return "Network Stream: " + m_url;
}


}
