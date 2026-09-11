/*  Network Audio Setup Dialog
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_AudioPipeline_NetworkAudioSetupDialog_H
#define PokemonAutomation_AudioPipeline_NetworkAudioSetupDialog_H

#include <memory>
#include <vector>
#include <QDialog>
#include "Common/Cpp/Time.h"
#include "CommonFramework/AudioPipeline/AudioStreamInfo.h"

class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class QProgressBar;
class QTcpSocket;
class QTimer;

namespace PokemonAutomation{


//  Asks the user for a network audio stream.
//
//  These streams are raw PCM with no header, so unlike a video stream there is
//  nothing to identify: the channel format has to be declared by the user. What
//  we can do is connect and measure the byte rate, which tells us whether the
//  declared format is consistent with what the server is actually sending.
class NetworkAudioSetupDialog : public QDialog{
public:
    ~NetworkAudioSetupDialog();
    NetworkAudioSetupDialog(QWidget& parent, const AudioStreamInfo& current);

    //  Only valid after exec() returns Accepted.
    AudioStreamInfo stream() const;


private:
    QWidget* make_url_row(QWidget& parent);
    QWidget* make_format_row(QWidget& parent);
    QWidget* make_name_row(QWidget& parent);

    void start_probe();
    void stop_probe();
    void on_probe_timeout();
    void on_probe_done();

    void set_status(const std::string& text, bool error = false);
    AudioChannelFormat selected_format() const;


private:
    QLineEdit* m_url_box = nullptr;
    QLineEdit* m_name_box = nullptr;
    QComboBox* m_format_box = nullptr;
    QPushButton* m_connect_button = nullptr;
    QPushButton* m_ok_button = nullptr;
    QLabel* m_status_label = nullptr;
    QLabel* m_measured_label = nullptr;
    QProgressBar* m_progress = nullptr;

    std::vector<AudioChannelFormat> m_formats;

    std::unique_ptr<QTcpSocket> m_socket;
    QTimer* m_probe_timer = nullptr;
    WallClock m_probe_start;
    uint64_t m_probe_bytes = 0;
    bool m_connected = false;
    bool m_measuring = false;
};



}
#endif
