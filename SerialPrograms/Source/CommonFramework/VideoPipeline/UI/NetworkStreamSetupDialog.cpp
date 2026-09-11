/*  Network Stream Setup Dialog
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include <cmath>
#include <QUrl>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPainter>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QProgressBar>
#include <QDialogButtonBox>
#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QMediaFormat>
#include <QVideoSink>
#include <QVideoFrame>
#include "CommonFramework/VideoPipeline/Backends/VideoFrameQt.h"
#include "CommonFramework/VideoPipeline/Backends/FFmpegLogging.h"
#include "CommonFramework/VideoPipeline/NetworkStreamTuning.h"
#include "NetworkStreamSetupDialog.h"

//#include <iostream>
//using std::cout;
//using std::endl;

namespace PokemonAutomation{


//  Paints the latest frame from the probe. Frames keep arriving while this is
//  collapsed since the frame rate measurement needs them.
class StreamPreviewWidget : public QWidget{
public:
    StreamPreviewWidget(QWidget* parent)
        : QWidget(parent)
    {
        this->setMinimumSize(480, 270);
    }

    void set_frame(const QVideoFrame& frame){
        m_frame = frame;
        if (this->isVisible()){
            this->update();
        }
    }

private:
    virtual void paintEvent(QPaintEvent* event) override{
        QWidget::paintEvent(event);

        QPainter painter(this);
        if (!m_frame.isValid()){
            painter.fillRect(this->rect(), Qt::black);
            return;
        }
        QVideoFrame::PaintOptions options;
        m_frame.paint(&painter, this->rect(), options);
    }

private:
    QVideoFrame m_frame;
};





NetworkStreamSetupDialog::~NetworkStreamSetupDialog(){
    stop_probe();
}
NetworkStreamSetupDialog::NetworkStreamSetupDialog(
    QWidget* parent,
    const std::string& initial_url,
    const std::string& initial_name
)
    : QDialog(parent)
{
    this->setWindowTitle("Network Stream");

    QVBoxLayout* layout = new QVBoxLayout(this);

    {
        QHBoxLayout* row = new QHBoxLayout();
        layout->addLayout(row);
        row->addWidget(new QLabel("<b>Stream URL:</b>", this));

        m_url_box = new QLineEdit(QString::fromStdString(initial_url), this);
        m_url_box->setPlaceholderText("http://192.168.1.10:8080/stream");
        m_url_box->setMinimumWidth(360);
        row->addWidget(m_url_box, 1);

        m_connect_button = new QPushButton("Connect", this);
        row->addWidget(m_connect_button);
    }
    {
        QHBoxLayout* row = new QHBoxLayout();
        layout->addLayout(row);
        row->addWidget(new QLabel("<b>Stream Name:</b>", this));

        m_name_box = new QLineEdit(QString::fromStdString(initial_name), this);
        m_name_box->setPlaceholderText("optional, e.g. Living Room Switch");
        row->addWidget(m_name_box, 1);
    }

    m_status_label = new QLabel("Enter a stream URL and press Connect.", this);
    m_status_label->setWordWrap(true);
    layout->addWidget(m_status_label);

    {
        QGridLayout* grid = new QGridLayout();
        layout->addLayout(grid);

        grid->addWidget(new QLabel("<b>Resolution:</b>", this), 0, 0);
        m_resolution_label = new QLabel("-", this);
        grid->addWidget(m_resolution_label, 0, 1);

        grid->addWidget(new QLabel("<b>Format:</b>", this), 1, 0);
        m_format_label = new QLabel("-", this);
        grid->addWidget(m_format_label, 1, 1);

        grid->addWidget(new QLabel("<b>Frame Rate:</b>", this), 2, 0);
        m_fps_label = new QLabel("-", this);
        grid->addWidget(m_fps_label, 2, 1);
        grid->setColumnStretch(1, 1);
    }

    //  Only shown when the stream declares no frame rate of its own.
    m_fps_progress = new QProgressBar(this);
    m_fps_progress->setRange(0, 100);
    m_fps_progress->setTextVisible(false);
    m_fps_progress->setVisible(false);
    layout->addWidget(m_fps_progress);

    m_preview_check = new QCheckBox("Show preview", this);
    m_preview_check->setEnabled(false);
    layout->addWidget(m_preview_check);

    m_preview = new StreamPreviewWidget(this);
    m_preview->setVisible(false);
    layout->addWidget(m_preview, 1);

    {
        QDialogButtonBox* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this
        );
        layout->addWidget(buttons);
        m_ok_button = buttons->button(QDialogButtonBox::Ok);
        m_ok_button->setEnabled(!initial_url.empty());

        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    connect(
        m_connect_button, &QPushButton::clicked,
        this, [this](bool){ start_probe(); }
    );
    connect(
        m_url_box, &QLineEdit::returnPressed,
        this, [this]{ start_probe(); }
    );
    connect(
        m_url_box, &QLineEdit::textChanged,
        this, [this](const QString& text){
            m_ok_button->setEnabled(!text.trimmed().isEmpty());
        }
    );
    connect(
        m_preview_check, &QCheckBox::toggled,
        this, [this](bool checked){ set_preview_visible(checked); }
    );

    if (!initial_url.empty()){
        start_probe();
    }
}



std::string NetworkStreamSetupDialog::url() const{
    return m_url_box->text().trimmed().toStdString();
}
std::string NetworkStreamSetupDialog::name() const{
    return m_name_box->text().trimmed().toStdString();
}

void NetworkStreamSetupDialog::set_preview_visible(bool visible){
    m_preview->setVisible(visible);
    //  Let the dialog shrink back down when the preview is collapsed.
    this->adjustSize();
}
void NetworkStreamSetupDialog::set_status(const std::string& status){
    m_status_label->setText(QString::fromStdString(status));
}



void NetworkStreamSetupDialog::stop_probe(){
    m_metaobject.reset();
    if (m_player){
        m_player->stop();
    }
    m_player.reset();
    m_sink.reset();
}
void NetworkStreamSetupDialog::start_probe(){
    std::string target = url();
    if (target.empty()){
        set_status("Enter a stream URL.");
        return;
    }

    stop_probe();

    m_probe_succeeded = false;
    m_resolution = Resolution();
    m_format = VideoFormat::OTHER;
    m_fps = 0;
    m_fps_estimator.reset();

    m_resolution_label->setText("-");
    m_format_label->setText("-");
    m_fps_label->setText("-");
    m_fps_progress->setVisible(false);
    m_fps_progress->setValue(0);

    //  Reconnecting with the preview open should keep it open.
    if (!m_preview_check->isChecked()){
        m_preview_check->setEnabled(false);
    }
    set_status("Connecting...");

    suppress_ffmpeg_log_spam();

    m_metaobject.reset(new QObject());
    m_sink.reset(new QVideoSink());
    m_player.reset(new QMediaPlayer());
    m_player->setVideoSink(m_sink.get());
    m_player->setPlaybackRate(NETWORK_STREAM_PLAYBACK_RATE);

    m_metaobject->connect(
        m_player.get(), &QMediaPlayer::mediaStatusChanged,
        m_metaobject.get(), [this](QMediaPlayer::MediaStatus status){
            if (status == QMediaPlayer::LoadedMedia){
                on_metadata();
            }else if (status == QMediaPlayer::InvalidMedia){
                set_status("Could not read this stream.");
            }
        }
    );
    m_metaobject->connect(
        m_player.get(), &QMediaPlayer::errorOccurred,
        m_metaobject.get(), [this](QMediaPlayer::Error error, const QString& message){
            set_status("Failed to connect: " + message.toStdString());
        }
    );
    m_metaobject->connect(
        m_sink.get(), &QVideoSink::videoFrameChanged,
        m_metaobject.get(), [this](const QVideoFrame& frame){
            on_frame(frame);
        }
    );

    m_player->setSource(QUrl(QString::fromStdString(target)));
    m_player->play();
}



void NetworkStreamSetupDialog::on_metadata(){
    QList<QMediaMetaData> tracks = m_player->videoTracks();
    if (tracks.empty()){
        set_status("Connected, but the stream carries no video track.");
        return;
    }
    const QMediaMetaData& track = tracks.front();

    QVariant resolution_value = track.value(QMediaMetaData::Resolution);
    QSize size = resolution_value.isValid() ? resolution_value.toSize() : QSize();
    if (size.width() > 0 && size.height() > 0){
        m_resolution = Resolution((size_t)size.width(), (size_t)size.height());
        m_resolution_label->setText(QString::fromStdString(m_resolution.to_string()));
    }

    QVariant codec_value = track.value(QMediaMetaData::VideoCodec);
    if (codec_value.isValid()){
        m_format = QMediaFormat_to_VideoFormat(
            (QMediaFormat::VideoCodec)codec_value.toInt()
        );
    }
    const EnumEntry* entry = VideoFormat_database().find(m_format);
    m_format_label->setText(QString::fromStdString(entry ? entry->display : "Unknown"));

    m_probe_succeeded = (bool)m_resolution;
    m_preview_check->setEnabled(true);

    //  Containers that carry timing report the rate outright. Multipart MJPEG
    //  comes back as NaN, in which case we count frames instead.
    QVariant rate_value = track.value(QMediaMetaData::VideoFrameRate);
    double declared = rate_value.isValid() ? rate_value.toDouble() : 0.;
    if (std::isfinite(declared) && declared > 0.){
        m_fps = (FramesPerSecond)(declared + 0.5);
        m_fps_label->setText(QString::number((qulonglong)m_fps) + " fps");
        set_status("Connected.");
        return;
    }

    m_fps_label->setText("measuring...");
    m_fps_progress->setVisible(true);
    set_status("Connected. This stream does not report a frame rate, measuring it.");
}
void NetworkStreamSetupDialog::on_frame(const QVideoFrame& frame){
    m_preview->set_frame(frame);

    if (!frame.isValid()){
        return;
    }

    //  Fallback for a stream that reported no resolution in its metadata.
    if (!m_resolution && frame.width() > 0 && frame.height() > 0){
        m_resolution = Resolution((size_t)frame.width(), (size_t)frame.height());
        m_resolution_label->setText(QString::fromStdString(m_resolution.to_string()));
        m_probe_succeeded = true;
        m_preview_check->setEnabled(true);
    }

    if (!m_fps_progress->isVisible() || m_fps_estimator.done()){
        return;
    }

    WallClock now = current_time();
    bool finished = m_fps_estimator.push_frame(now);
    m_fps_progress->setValue((int)(m_fps_estimator.progress(now) * 100));
    if (!finished){
        return;
    }

    m_fps = m_fps_estimator.fps();
    m_fps_progress->setVisible(false);
    if (m_fps == 0){
        m_fps_label->setText("unknown");
        set_status("Connected, but the frame rate could not be measured.");
        return;
    }
    m_fps_label->setText(QString::number((qulonglong)m_fps) + " fps (measured)");
    set_status("Connected.");
}




}
