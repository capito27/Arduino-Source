/*  Video Source (Network Stream)
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_VideoPipeline_VideoSource_NetworkStream_H
#define PokemonAutomation_VideoPipeline_VideoSource_NetworkStream_H

#include <memory>
#include <functional>
#include <QWidget>
#include "Common/Cpp/Concurrency/SpinLock.h"
#include "CommonFramework/VideoPipeline/Backends/QVideoFrameCache.h"
#include "CommonFramework/VideoPipeline/Backends/SnapshotManager.h"
#include "CommonFramework/VideoPipeline/FrameRateEstimator.h"
#include "CommonFramework/VideoPipeline/VideoSourceDescriptor.h"
#include "CommonFramework/VideoPipeline/VideoSource.h"

class QMediaPlayer;
class QVideoSink;

namespace PokemonAutomation{


//  A video stream served over the network (MJPEG over HTTP, RTSP, ...), played
//  through QMediaPlayer. Everything downstream of the QVideoFrame is shared with
//  the camera sources.
class VideoSourceDescriptor_NetworkStream : public VideoSourceDescriptor{
public:
    VideoSourceDescriptor_NetworkStream()
        : VideoSourceDescriptor(VideoSourceType::NetworkStream)
    {}
    VideoSourceDescriptor_NetworkStream(std::string url)
        : VideoSourceDescriptor(VideoSourceType::NetworkStream)
        , m_url(std::move(url))
    {}

public:
    std::string url() const;
    void set_url(std::string url);

    //  Optional. A named stream shows its name in the dropdown and counts as
    //  configured, so selecting it connects instead of prompting.
    std::string name() const;
    void set_name(std::string name);

    //  What the stream turned out to be serving. Remembered across runs so the
    //  resolution/format boxes are populated before the first frame arrives.
    //  Const because this is a cache, not part of the identity of the source.
    void set_observed_characteristics(
        Resolution resolution,
        VideoFormat format,
        FramesPerSecond fps
    ) const;


public:
    //  Re-selecting the entry should reconnect.
    virtual bool should_reload() const override{ return true; }

    virtual void run_post_select() override;
    virtual void run_reconfigure() override;

    virtual bool operator==(const VideoSourceDescriptor& x) const override;
    virtual std::string display_name() const override;

    virtual JsonValue to_json() const override;
    virtual void load_json(const JsonValue& json) override;

    virtual std::unique_ptr<VideoSource> make_VideoSource(
        Logger& logger,
        Resolution resolution,
        VideoFormat format,
        FramesPerSecond fps
    ) const override;


private:
    void open_setup_dialog();


private:
    mutable SpinLock m_lock;
    std::string m_url;
    std::string m_name;

    mutable Resolution m_resolution;
    mutable VideoFormat m_format = VideoFormat::OTHER;
    mutable FramesPerSecond m_fps = 0;
};




class VideoDisplay_NetworkStream;


class VideoSource_NetworkStream : public VideoSource{
public:
    virtual ~VideoSource_NetworkStream();

    //  "on_characteristics" is called with the best known values each time we
    //  learn something new, so the descriptor can remember them for next time.
    VideoSource_NetworkStream(
        Logger& logger,
        const std::string& url,
        Resolution expected_resolution,
        VideoFormat expected_format,
        FramesPerSecond expected_fps,
        std::function<void(Resolution, VideoFormat, FramesPerSecond)> on_characteristics = nullptr
    );


public:
    virtual Resolution current_resolution() const override;
    virtual VideoFormat current_format() const override;
    virtual FramesPerSecond current_fps() const override;
    virtual const VideoFormatSet& supported_formats() const override{
        return m_formats;
    }

    virtual VideoSnapshot snapshot_latest_blocking() override{
        return m_snapshot_manager.snapshot_latest_blocking();
    }
    virtual VideoSnapshot snapshot_recent_nonblocking(WallClock min_time) override{
        return m_snapshot_manager.snapshot_recent_nonblocking(min_time);
    }

    virtual QWidget* make_display_QtWidget(QWidget* parent) override;


private:
    void init(const std::string& url);

    //  Resolution and codec from the player's track metadata, available at
    //  LoadedMedia. Earlier and more accurate than waiting for a frame: it
    //  reports what the stream carries rather than what FFmpeg decodes it into,
    //  which is what the camera sources report.
    void on_metadata();

    //  Fallback for a stream whose metadata carried no resolution.
    void on_first_frame(const QVideoFrame& frame);

    //  Fallback for a descriptor with no frame rate. Called per frame from the
    //  Qt main thread.
    void update_fps_measurement(WallClock now);

    void set_characteristics(Resolution resolution, VideoFormat format);
    void set_measured_fps(FramesPerSecond fps);

    //  Caller must hold m_state_lock.
    void refresh_formats();

    void notify_characteristics();


private:
    friend class VideoDisplay_NetworkStream;

    std::unique_ptr<QObject> m_metaobject;

    Logger& m_logger;

    std::function<void(Resolution, VideoFormat, FramesPerSecond)> m_on_characteristics;

    mutable SpinLock m_state_lock;
    Resolution m_resolution;
    VideoFormat m_format;
    FramesPerSecond m_fps;
    bool m_got_characteristics = false;

    //  Only touched from the Qt main thread.
    FrameRateEstimator m_fps_estimator;

    std::unique_ptr<QMediaPlayer> m_player;
    std::unique_ptr<QVideoSink> m_video_sink;

    VideoFormatSet m_formats;


private:
    QVideoFrameCache m_last_frame;
    SnapshotManager m_snapshot_manager;
};




class VideoDisplay_NetworkStream : public QWidget, private VideoFrameListener{
public:
    ~VideoDisplay_NetworkStream();
    VideoDisplay_NetworkStream(QWidget* parent, VideoSource_NetworkStream& source);

private:
    virtual void on_frame(std::shared_ptr<const VideoFrame> frame) override;
    virtual void paintEvent(QPaintEvent* event) override;

private:
    VideoSource_NetworkStream& m_source;
    std::shared_ptr<const VideoFrame> m_last_frame;

    LifetimeSanitizer m_sanitizer;
};




}
#endif
