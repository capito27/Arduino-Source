/*  Video Formats
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include "VideoFormats.h"

namespace PokemonAutomation{



const EnumDropdownDatabase<VideoFormat>& VideoFormat_database(){
    static EnumDropdownDatabase<VideoFormat> database{
        {VideoFormat::OTHER,    "other",    "Unknown"},
        {VideoFormat::YUV420P,  "YUV420P",  "YUV420P"},
        {VideoFormat::YUYV,     "YUYV",     "YUYV"},
        {VideoFormat::NV12,     "NV12",     "NV12"},
        {VideoFormat::P010,     "P010",     "P010"},
        {VideoFormat::MJPEG,    "MJPEG",    "MJPEG"},
        {VideoFormat::H264,     "H264",     "H.264"},
        {VideoFormat::HEVC,     "HEVC",     "H.265 / HEVC"},
        {VideoFormat::VP9,      "VP9",      "VP9"},
        {VideoFormat::AV1,      "AV1",      "AV1"},
    };
    return database;
}



}
