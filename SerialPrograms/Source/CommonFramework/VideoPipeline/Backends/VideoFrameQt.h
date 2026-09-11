/*  Video Frame
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_VideoFrame_H
#define PokemonAutomation_VideoFrame_H

#include <QVideoFrame>
#include <QMediaFormat>
#include "Common/Cpp/Time.h"
#include "CommonFramework/VideoPipeline/VideoFormats.h"

namespace PokemonAutomation{


VideoFormat QVideoFrameFormat_to_VideoFormat(QVideoFrameFormat::PixelFormat format);
QVideoFrameFormat::PixelFormat VideoFormat_to_QVideoFrameFormat(VideoFormat format);

//  Map a stream's codec to the format we report. This is what the stream
//  carries, not the pixel format the decoder produces, matching what the camera
//  sources report. Codecs with no VideoFormat entry come back as OTHER.
VideoFormat QMediaFormat_to_VideoFormat(QMediaFormat::VideoCodec codec);



class VideoFrame{
public:
    WallClock timestamp;
    QVideoFrame frame;

    VideoFrame(VideoFrame&&) = default;
    VideoFrame& operator=(VideoFrame&&) = default;
    VideoFrame(const VideoFrame&) = delete;
    void operator=(const VideoFrame&) = delete;

    VideoFrame()
        : timestamp(WallClock::min())
    {}
    VideoFrame(WallClock p_timestamp, QVideoFrame p_frame)
        : timestamp(p_timestamp)
        , frame(std::move(p_frame))
    {}

    void clear(){
        frame = QVideoFrame();
    }
    bool is_valid() const{
        return frame.isValid();
    }
};


}
#endif
