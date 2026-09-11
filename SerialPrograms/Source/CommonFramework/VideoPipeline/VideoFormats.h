/*  Video Formats
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_VideoFormats_H
#define PokemonAutomation_VideoFormats_H

#include "Common/Cpp/Options/EnumDropdownDatabase.h"

namespace PokemonAutomation{


//  These should be ordered by preference.
//  The raw formats come first. The compressed ones below are only reported by
//  network streams, and are listed last so they never displace a raw format when
//  picking a camera format.
enum class VideoFormat{
    P010,
    NV12,
    YUV420P,
    YUYV,
    MJPEG,

    H264,
    HEVC,
    VP9,
    AV1,

    OTHER,
};

const EnumDropdownDatabase<VideoFormat>& VideoFormat_database();


using FramesPerSecond = size_t;



}
#endif
