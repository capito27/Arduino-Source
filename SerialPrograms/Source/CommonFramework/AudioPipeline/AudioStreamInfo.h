/*  Audio Stream Info
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_AudioPipeline_AudioStreamInfo_H
#define PokemonAutomation_AudioPipeline_AudioStreamInfo_H

#include <string>
#include "AudioInfo.h"

namespace PokemonAutomation{


//  Describes an audio stream served over the network. The streams we support are
//  raw PCM over TCP, which carry no header. Nothing about the format can be read
//  off the stream itself, so the channel format is part of the identity of the
//  stream and must come from the user.
class AudioStreamInfo{
public:
    AudioStreamInfo() = default;
    AudioStreamInfo(std::string url, AudioChannelFormat format, std::string name = "");

    explicit operator bool() const{ return !m_url.empty(); }

    const std::string& url() const{ return m_url; }
    const std::string& name() const{ return m_name; }
    AudioChannelFormat format() const{ return m_format; }

    //  What the dropdown shows. Named streams show their name, the rest show
    //  their URL.
    std::string display_name() const;

private:
    std::string m_url;
    std::string m_name;
    AudioChannelFormat m_format = AudioChannelFormat::NONE;
};



}
#endif
