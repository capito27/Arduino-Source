/*  Video Frame
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include <QVideoFrameFormat>
#include "VideoFrameQt.h"

namespace PokemonAutomation{


VideoFormat QVideoFrameFormat_to_VideoFormat(QVideoFrameFormat::PixelFormat format){
    switch (format){
    case QVideoFrameFormat::Format_YUV420P:
        return VideoFormat::YUV420P;
    case QVideoFrameFormat::Format_YUYV:
        return VideoFormat::YUYV;
    case QVideoFrameFormat::Format_NV12:
        return VideoFormat::NV12;
    case QVideoFrameFormat::Format_P010:
        return VideoFormat::P010;
    case QVideoFrameFormat::Format_Jpeg:
        return VideoFormat::MJPEG;
    default:
        return VideoFormat::OTHER;
    }
}
QVideoFrameFormat::PixelFormat VideoFormat_to_QVideoFrameFormat(VideoFormat format){
    switch (format){
    case VideoFormat::OTHER:
        return QVideoFrameFormat::Format_Invalid;
    case VideoFormat::YUV420P:
        return QVideoFrameFormat::Format_YUV420P;
    case VideoFormat::YUYV:
        return QVideoFrameFormat::Format_YUYV;
    case VideoFormat::NV12:
        return QVideoFrameFormat::Format_NV12;
    case VideoFormat::P010:
        return QVideoFrameFormat::Format_P010;
    case VideoFormat::MJPEG:
        return QVideoFrameFormat::Format_Jpeg;
    case VideoFormat::H264:
    case VideoFormat::HEVC:
    case VideoFormat::VP9:
    case VideoFormat::AV1:
        //  Compressed stream formats. There is no pixel format to request: the
        //  decoder chooses what it hands back.
        return QVideoFrameFormat::Format_Invalid;
    }
    return QVideoFrameFormat::Format_Invalid;
}

VideoFormat QMediaFormat_to_VideoFormat(QMediaFormat::VideoCodec codec){
    switch (codec){
    case QMediaFormat::VideoCodec::MotionJPEG:
        return VideoFormat::MJPEG;
    case QMediaFormat::VideoCodec::H264:
        return VideoFormat::H264;
    case QMediaFormat::VideoCodec::H265:
        return VideoFormat::HEVC;
    case QMediaFormat::VideoCodec::VP9:
        return VideoFormat::VP9;
    case QMediaFormat::VideoCodec::AV1:
        return VideoFormat::AV1;
    default:
        //  Everything else decodes fine, we just have no name for it. VP8
        //  belongs here once there is a working stream to confirm it with.
        return VideoFormat::OTHER;
    }
}



}
