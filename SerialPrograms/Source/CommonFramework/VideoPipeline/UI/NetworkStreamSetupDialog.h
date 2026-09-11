/*  Network Stream Setup Dialog
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_VideoPipeline_NetworkStreamSetupDialog_H
#define PokemonAutomation_VideoPipeline_NetworkStreamSetupDialog_H

#include <memory>
#include <string>
#include <QDialog>
#include "Common/Cpp/ImageResolution.h"
#include "CommonFramework/VideoPipeline/VideoFormats.h"
#include "CommonFramework/VideoPipeline/FrameRateEstimator.h"

class QLabel;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QProgressBar;
class QMediaPlayer;
class QVideoSink;
class QVideoFrame;

namespace PokemonAutomation{

class StreamPreviewWidget;


//  Prompts for a stream URL and connects to it to find out what it is serving.
//  A network stream can't be queried without connecting, so this probes while
//  the user is still in the dialog. The resolution/format/fps boxes are then
//  correct the moment the source is selected, rather than filling in later.
class NetworkStreamSetupDialog : public QDialog{
public:
    ~NetworkStreamSetupDialog();
    NetworkStreamSetupDialog(
        QWidget* parent,
        const std::string& initial_url,
        const std::string& initial_name
    );

    std::string url() const;
    std::string name() const;

    //  The rest are only meaningful if probe_succeeded(). Otherwise the URL is
    //  accepted as typed and the characteristics are learned at connect time.
    bool probe_succeeded() const{ return m_probe_succeeded; }
    Resolution resolution() const{ return m_resolution; }
    VideoFormat format() const{ return m_format; }
    FramesPerSecond fps() const{ return m_fps; }


private:
    void start_probe();
    void stop_probe();
    void on_metadata();
    void on_frame(const QVideoFrame& frame);
    void set_preview_visible(bool visible);
    void set_status(const std::string& status);


private:
    QLineEdit* m_url_box;
    QLineEdit* m_name_box;
    QPushButton* m_connect_button;
    QLabel* m_status_label;
    QLabel* m_resolution_label;
    QLabel* m_format_label;
    QLabel* m_fps_label;
    QProgressBar* m_fps_progress;
    QCheckBox* m_preview_check;
    StreamPreviewWidget* m_preview;
    QPushButton* m_ok_button;

    std::unique_ptr<QObject> m_metaobject;
    std::unique_ptr<QMediaPlayer> m_player;
    std::unique_ptr<QVideoSink> m_sink;

    bool m_probe_succeeded = false;
    Resolution m_resolution;
    VideoFormat m_format = VideoFormat::OTHER;
    FramesPerSecond m_fps = 0;

    FrameRateEstimator m_fps_estimator;
};




}
#endif
