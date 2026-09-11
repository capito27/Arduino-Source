/*  Video Source (Network Stream)
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include <QUrl>
#include <QPainter>
#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QMediaFormat>
#include <QVideoSink>
#include "Common/Cpp/Json/JsonValue.h"
#include "Common/Cpp/Json/JsonArray.h"
#include "Common/Cpp/Json/JsonObject.h"
#include "Common/Qt/Redispatch.h"
#include "CommonFramework/Logging/Logger.h"
#include "CommonFramework/VideoPipeline/Backends/VideoFrameQt.h"
#include "CommonFramework/VideoPipeline/Backends/FFmpegLogging.h"
#include "CommonFramework/VideoPipeline/NetworkStreamTuning.h"
#include "CommonFramework/VideoPipeline/UI/NetworkStreamSetupDialog.h"
#include "VideoSource_NetworkStream.h"

//#include <iostream>
//using std::cout;
//using std::endl;

namespace PokemonAutomation{


std::string VideoSourceDescriptor_NetworkStream::url() const{
    ReadSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
    return m_url;
}
void VideoSourceDescriptor_NetworkStream::set_url(std::string url){
    WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
    m_url = std::move(url);
}
std::string VideoSourceDescriptor_NetworkStream::name() const{
    ReadSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
    return m_name;
}
void VideoSourceDescriptor_NetworkStream::set_name(std::string name){
    WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
    m_name = std::move(name);
}
void VideoSourceDescriptor_NetworkStream::set_observed_characteristics(
    Resolution resolution,
    VideoFormat format,
    FramesPerSecond fps
) const{
    WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
    m_resolution = resolution;
    m_format = format;
    m_fps = fps;
}

bool VideoSourceDescriptor_NetworkStream::operator==(const VideoSourceDescriptor& x) const{
    if (typeid(*this) != typeid(x)){
        return false;
    }

    std::string other_url = static_cast<const VideoSourceDescriptor_NetworkStream&>(x).url();

    ReadSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
    return m_url == other_url;
}
std::string VideoSourceDescriptor_NetworkStream::display_name() const{
    ReadSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
    if (!m_name.empty()){
        return "Network Stream: " + m_name;
    }
    if (m_url.empty()){
        return "Network Stream";
    }
    return "Network Stream: " + m_url;
}



JsonValue VideoSourceDescriptor_NetworkStream::to_json() const{
    ReadSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
    JsonObject obj;
    obj["URL"] = m_url;
    obj["Name"] = m_name;
    {
        JsonArray res;
        res.push_back(m_resolution.width);
        res.push_back(m_resolution.height);
        obj["Resolution"] = std::move(res);
    }
    const EnumEntry* entry = VideoFormat_database().find(m_format);
    if (entry){
        obj["Format"] = entry->slug;
    }
    obj["Fps"] = m_fps;
    return obj;
}
void VideoSourceDescriptor_NetworkStream::load_json(const JsonValue& json){
    const JsonObject* obj = json.to_object();
    if (obj == nullptr){
        return;
    }

    WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);

    const std::string* url = obj->get_string("URL");
    if (url != nullptr){
        m_url = *url;
    }

    const std::string* name = obj->get_string("Name");
    if (name != nullptr){
        m_name = *name;
    }

    const JsonArray* res = obj->get_array("Resolution");
    if (res != nullptr && res->size() == 2){
        size_t width, height;
        if ((*res)[0].read_integer(width) && (*res)[1].read_integer(height)){
            m_resolution = Resolution(width, height);
        }
    }

    const std::string* format = obj->get_string("Format");
    if (format != nullptr){
        const EnumEntry* entry = VideoFormat_database().find_slug(*format);
        if (entry != nullptr){
            m_format = (VideoFormat)entry->enum_value;
        }
    }

    obj->read_integer(m_fps, "Fps");
}



void VideoSourceDescriptor_NetworkStream::run_post_select(){
    //  A named stream is a configured entry, like a camera in the list. Picking
    //  it should just connect. "Reset Video" reopens the dialog.
    if (!name().empty()){
        return;
    }
    open_setup_dialog();
}
void VideoSourceDescriptor_NetworkStream::run_reconfigure(){
    open_setup_dialog();
}
void VideoSourceDescriptor_NetworkStream::open_setup_dialog(){
    NetworkStreamSetupDialog dialog(nullptr, url(), name());
    if (dialog.exec() != QDialog::Accepted){
        return;
    }

    set_url(dialog.url());
    set_name(dialog.name());
    if (dialog.probe_succeeded()){
        set_observed_characteristics(dialog.resolution(), dialog.format(), dialog.fps());
    }
}

std::unique_ptr<VideoSource> VideoSourceDescriptor_NetworkStream::make_VideoSource(
    Logger& logger,
    Resolution resolution,
    VideoFormat format,
    FramesPerSecond fps
) const{
    //  The desired resolution/format/fps are ignored. A network stream serves
    //  whatever it serves.
    //  Copy out and release the lock before constructing. The constructor
    //  dispatches to the main thread and blocks, and the main thread can call
    //  back into this descriptor while we wait.
    std::string stream_url;
    Resolution stream_resolution;
    VideoFormat stream_format;
    FramesPerSecond stream_fps;
    {
        ReadSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
        stream_url = m_url;
        stream_resolution = m_resolution;
        stream_format = m_format;
        stream_fps = m_fps;
    }

    return std::make_unique<VideoSource_NetworkStream>(
        logger, stream_url, stream_resolution, stream_format, stream_fps,
        [this](Resolution resolution, VideoFormat format, FramesPerSecond fps){
            set_observed_characteristics(resolution, format, fps);
        }
    );
}





VideoSource_NetworkStream::~VideoSource_NetworkStream(){
    if (!m_player){
        return;
    }
    try{
        m_logger.log("Stopping Network Stream...");
    }catch (...){}

    run_on_main_thread_and_wait([&]{
        m_metaobject.reset();
        m_player->stop();
        m_player.reset();
        m_video_sink.reset();
    });
}
VideoSource_NetworkStream::VideoSource_NetworkStream(
    Logger& logger,
    const std::string& url,
    Resolution expected_resolution,
    VideoFormat expected_format,
    FramesPerSecond expected_fps,
    std::function<void(Resolution, VideoFormat, FramesPerSecond)> on_characteristics
)
    : VideoSource(logger, true)
    , m_logger(logger)
    , m_on_characteristics(std::move(on_characteristics))
    , m_resolution(expected_resolution)
    , m_format(expected_format)
    , m_fps(expected_fps)
    , m_last_frame(logger)
    , m_snapshot_manager(logger, m_last_frame)
{
    if (url.empty()){
        m_logger.log("No stream URL set.", COLOR_RED);
        return;
    }

    //  Seed the boxes with what the descriptor remembers. Corrected once the
    //  stream reports for itself.
    refresh_formats();

    m_logger.log("Starting Network Stream: " + url);

    run_on_main_thread_and_wait([&]{
        init(url);
    });
}
void VideoSource_NetworkStream::init(const std::string& url){
    suppress_ffmpeg_log_spam();

    m_metaobject.reset(new QObject());

    m_video_sink.reset(new QVideoSink());
    m_player.reset(new QMediaPlayer());
    m_player->setVideoSink(m_video_sink.get());
    m_player->setPlaybackRate(NETWORK_STREAM_PLAYBACK_RATE);

    m_metaobject->connect(
        m_player.get(), &QMediaPlayer::mediaStatusChanged,
        m_metaobject.get(), [this](QMediaPlayer::MediaStatus status){
            if (status == QMediaPlayer::LoadedMedia){
                on_metadata();
            }
        }
    );
    m_metaobject->connect(
        m_player.get(), &QMediaPlayer::errorOccurred,
        m_metaobject.get(), [this](QMediaPlayer::Error error, const QString& message){
            m_logger.log(
                "QMediaPlayer error " + std::to_string((int)error) + ": " +
                    message.toStdString(),
                COLOR_RED
            );
        }
    );
    m_metaobject->connect(
        m_video_sink.get(), &QVideoSink::videoFrameChanged,
        m_metaobject.get(), [this](const QVideoFrame& frame){
            //  This runs on the Qt main thread, so keep it cheap. The cache push
            //  is a handle swap and SnapshotManager converts off-thread.
            WallClock now = current_time();
            if (!m_last_frame.push_frame(frame, now)){
                return;
            }
            on_first_frame(frame);
            update_fps_measurement(now);
            report_source_frame(std::make_shared<VideoFrame>(now, frame));
        }
    );

    m_player->setSource(QUrl(QString::fromStdString(url)));
    m_player->play();
}



void VideoSource_NetworkStream::on_metadata(){
    QList<QMediaMetaData> tracks = m_player->videoTracks();
    if (tracks.empty()){
        return;
    }
    const QMediaMetaData& track = tracks.front();

    QVariant resolution_value = track.value(QMediaMetaData::Resolution);
    if (!resolution_value.isValid()){
        return;
    }
    QSize size = resolution_value.toSize();
    if (size.width() <= 0 || size.height() <= 0){
        return;
    }

    //  QMediaMetaData::VideoFrameRate is NaN for multipart MJPEG. Don't read it.
    VideoFormat format = VideoFormat::OTHER;
    QVariant codec_value = track.value(QMediaMetaData::VideoCodec);
    if (codec_value.isValid()){
        format = QMediaFormat_to_VideoFormat(
            (QMediaFormat::VideoCodec)codec_value.toInt()
        );
    }

    set_characteristics(Resolution((size_t)size.width(), (size_t)size.height()), format);
}
void VideoSource_NetworkStream::on_first_frame(const QVideoFrame& frame){
    {
        ReadSpinLock lg(m_state_lock, PA_CURRENT_FUNCTION);
        if (m_got_characteristics || !frame.isValid()){
            return;
        }
    }
    set_characteristics(
        Resolution((size_t)frame.width(), (size_t)frame.height()),
        VideoFormat::OTHER
    );
}
void VideoSource_NetworkStream::update_fps_measurement(WallClock now){
    if (!m_fps_estimator.push_frame(now)){
        return;
    }
    set_measured_fps(m_fps_estimator.fps());
}

void VideoSource_NetworkStream::refresh_formats(){
    m_formats.clear();
    if (m_resolution){
        m_formats[m_resolution][m_format] = {m_fps};
    }
}
void VideoSource_NetworkStream::notify_characteristics(){
    if (!m_on_characteristics){
        return;
    }
    Resolution resolution;
    VideoFormat format;
    FramesPerSecond fps;
    {
        ReadSpinLock lg(m_state_lock, PA_CURRENT_FUNCTION);
        resolution = m_resolution;
        format = m_format;
        fps = m_fps;
    }
    m_on_characteristics(resolution, format, fps);
}
void VideoSource_NetworkStream::set_characteristics(Resolution resolution, VideoFormat format){
    {
        WriteSpinLock lg(m_state_lock, PA_CURRENT_FUNCTION);
        if (m_got_characteristics){
            return;
        }
        m_got_characteristics = true;
        m_resolution = resolution;
        m_format = format;
        refresh_formats();
    }

    const EnumEntry* entry = VideoFormat_database().find(format);
    m_logger.log(
        "Stream: Resolution = " + resolution.to_string() +
        ", Format = " + (entry ? entry->display : "Unknown")
    );

    notify_characteristics();
}
void VideoSource_NetworkStream::set_measured_fps(FramesPerSecond fps){
    if (fps == 0){
        return;
    }
    {
        WriteSpinLock lg(m_state_lock, PA_CURRENT_FUNCTION);
        if (m_fps == fps){
            return;
        }
        m_fps = fps;
        refresh_formats();
    }

    m_logger.log("Stream: Measured " + std::to_string(fps) + " fps.");

    notify_characteristics();
}

Resolution VideoSource_NetworkStream::current_resolution() const{
    ReadSpinLock lg(m_state_lock, PA_CURRENT_FUNCTION);
    return m_resolution;
}
VideoFormat VideoSource_NetworkStream::current_format() const{
    ReadSpinLock lg(m_state_lock, PA_CURRENT_FUNCTION);
    return m_format;
}
FramesPerSecond VideoSource_NetworkStream::current_fps() const{
    ReadSpinLock lg(m_state_lock, PA_CURRENT_FUNCTION);
    return m_fps;
}

QWidget* VideoSource_NetworkStream::make_display_QtWidget(QWidget* parent){
    return new VideoDisplay_NetworkStream(parent, *this);
}





VideoDisplay_NetworkStream::~VideoDisplay_NetworkStream(){
    m_source.remove_source_frame_listener(*this);
}
VideoDisplay_NetworkStream::VideoDisplay_NetworkStream(
    QWidget* parent,
    VideoSource_NetworkStream& source
)
    : QWidget(parent)
    , m_source(source)
    , m_sanitizer("VideoDisplay_NetworkStream")
{
    this->setMinimumSize(80, 45);
    source.add_source_frame_listener(*this);
}
void VideoDisplay_NetworkStream::on_frame(std::shared_ptr<const VideoFrame> frame){
    auto scope_check = m_sanitizer.check_scope();
    m_last_frame = std::move(frame);
    this->update();
}
void VideoDisplay_NetworkStream::paintEvent(QPaintEvent* event){
    auto scope_check = m_sanitizer.check_scope();
    QWidget::paintEvent(event);

    if (!m_last_frame){
        return;
    }

    QVideoFrame frame = m_last_frame->frame;
    if (!frame.isValid()){
        return;
    }

    QRect rect(0, 0, this->width(), this->height());
    QVideoFrame::PaintOptions options;
    QPainter painter(this);

    frame.paint(&painter, rect, options);
    m_source.report_rendered_frame(current_time());
}




}
