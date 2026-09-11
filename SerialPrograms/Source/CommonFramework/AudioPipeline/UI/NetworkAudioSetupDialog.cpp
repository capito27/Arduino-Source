/*  Network Audio Setup Dialog
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include <algorithm>
#include <QUrl>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QTcpSocket>
#include "NetworkAudioSetupDialog.h"

namespace PokemonAutomation{


//  How long we sample the stream for before judging the byte rate. The rate
//  settles well inside this.
const uint64_t PROBE_MILLISECONDS = 1500;

//  How long we wait for audio to start. These servers commonly spawn an encoder
//  per connection, so the socket connects well before the first bytes arrive.
const uint64_t STARTUP_MILLISECONDS = 5000;

//  How far the measured data rate may sit from the nominal one. The closest two
//  formats are 8% apart (176,400 and 192,000 bytes/second), so this has to stay
//  well under that to tell them apart. Measurement repeats to within 0.1%.
const uint64_t TOLERANCE_PERCENT = 2;


//  Nominal byte rate of a channel format. Used to check what the user declared
//  against what the server actually sends.
uint64_t bytes_per_second(AudioChannelFormat format){
    switch (format){
    case AudioChannelFormat::MONO_48000:
        return 48000 * 1 * 2;
    case AudioChannelFormat::DUAL_44100:
        return 44100 * 2 * 2;
    case AudioChannelFormat::DUAL_48000:
        return 48000 * 2 * 2;
    case AudioChannelFormat::MONO_96000:
    case AudioChannelFormat::INTERLEAVE_LR_96000:
    case AudioChannelFormat::INTERLEAVE_RL_96000:
        return 96000 * 1 * 2;
    default:
        return 0;
    }
}


//  Several formats share a data rate: 2 x 48,000 stereo and all three of the
//  96,000 mono variants are the same 192,000 bytes/second on the wire and differ
//  only in how the samples are read. So this can confirm a rate, and name a
//  format that fits it, but it cannot pick between formats that share one.
const char* format_matching_rate(uint64_t measured, uint64_t tolerance_percent){
    for (size_t c = 1; c < (size_t)AudioChannelFormat::END_LIST; c++){
        uint64_t rate = bytes_per_second((AudioChannelFormat)c);
        if (rate == 0){
            continue;
        }
        if (measured * 100 > rate * (100 - tolerance_percent) &&
            measured * 100 < rate * (100 + tolerance_percent)
        ){
            return AUDIO_FORMAT_LABELS[c];
        }
    }
    return nullptr;
}



NetworkAudioSetupDialog::~NetworkAudioSetupDialog(){
    stop_probe();
}
NetworkAudioSetupDialog::NetworkAudioSetupDialog(QWidget& parent, const AudioStreamInfo& current)
    : QDialog(&parent)
{
    setWindowTitle("Network Audio Stream");
    setMinimumWidth(500);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(make_url_row(*this));
    layout->addWidget(make_format_row(*this));
    layout->addWidget(make_name_row(*this));

    m_status_label = new QLabel(this);
    m_status_label->setWordWrap(true);
    layout->addWidget(m_status_label);

    m_measured_label = new QLabel(this);
    layout->addWidget(m_measured_label);

    //  The probe has to be ended by the clock rather than by data arriving. A
    //  server that accepts the connection and then sends nothing would otherwise
    //  leave the dialog waiting forever.
    m_probe_timer = new QTimer(this);
    m_probe_timer->setSingleShot(true);
    connect(
        m_probe_timer, &QTimer::timeout,
        this, [this]{ on_probe_timeout(); }
    );

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, (int)PROBE_MILLISECONDS);
    m_progress->setTextVisible(false);
    m_progress->setVisible(false);
    layout->addWidget(m_progress);

    {
        QHBoxLayout* buttons = new QHBoxLayout();
        layout->addLayout(buttons);
        buttons->addStretch();

        m_ok_button = new QPushButton("OK", this);
        m_ok_button->setEnabled(false);
        buttons->addWidget(m_ok_button);

        QPushButton* cancel = new QPushButton("Cancel", this);
        buttons->addWidget(cancel);

        connect(
            m_ok_button, &QPushButton::clicked,
            this, [this](bool){ accept(); }
        );
        connect(
            cancel, &QPushButton::clicked,
            this, [this](bool){ reject(); }
        );
    }

    //  Reopening on an already configured stream should come up populated.
    if (current){
        m_url_box->setText(QString::fromStdString(current.url()));
        m_name_box->setText(QString::fromStdString(current.name()));
        for (size_t c = 0; c < m_formats.size(); c++){
            if (m_formats[c] == current.format()){
                m_format_box->setCurrentIndex((int)c);
                break;
            }
        }
        //  It was already working, so don't force the user to re-probe it.
        m_ok_button->setEnabled(true);
        set_status("Press Connect to re-check this stream, or OK to keep it.");
    }else{
        set_status("Enter the stream URL, then press Connect.");
    }
}

QWidget* NetworkAudioSetupDialog::make_url_row(QWidget& parent){
    QWidget* row = new QWidget(&parent);
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(new QLabel("<b>Stream URL:</b>", row), 1);

    m_url_box = new QLineEdit(row);
    m_url_box->setPlaceholderText("tcp://192.168.1.100:1235");
    layout->addWidget(m_url_box, 3);

    m_connect_button = new QPushButton("Connect", row);
    layout->addWidget(m_connect_button, 1);

    connect(
        m_connect_button, &QPushButton::clicked,
        this, [this](bool){ start_probe(); }
    );
    connect(
        m_url_box, &QLineEdit::returnPressed,
        this, [this]{ start_probe(); }
    );

    return row;
}
QWidget* NetworkAudioSetupDialog::make_format_row(QWidget& parent){
    QWidget* row = new QWidget(&parent);
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(new QLabel("<b>Format:</b>", row), 1);

    m_format_box = new QComboBox(row);
    for (size_t c = 1; c < (size_t)AudioChannelFormat::END_LIST; c++){
        m_formats.emplace_back((AudioChannelFormat)c);
        m_format_box->addItem(AUDIO_FORMAT_LABELS[c]);
    }
    //  By far the most common case for a capture stream.
    for (size_t c = 0; c < m_formats.size(); c++){
        if (m_formats[c] == AudioChannelFormat::DUAL_48000){
            m_format_box->setCurrentIndex((int)c);
            break;
        }
    }
    layout->addWidget(m_format_box, 4);

    return row;
}
QWidget* NetworkAudioSetupDialog::make_name_row(QWidget& parent){
    QWidget* row = new QWidget(&parent);
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(new QLabel("<b>Stream Name:</b>", row), 1);

    m_name_box = new QLineEdit(row);
    m_name_box->setPlaceholderText("(optional) shown instead of the URL");
    layout->addWidget(m_name_box, 4);

    return row;
}


AudioChannelFormat NetworkAudioSetupDialog::selected_format() const{
    int index = m_format_box->currentIndex();
    if (index < 0 || index >= (int)m_formats.size()){
        return AudioChannelFormat::NONE;
    }
    return m_formats[index];
}
AudioStreamInfo NetworkAudioSetupDialog::stream() const{
    return AudioStreamInfo(
        m_url_box->text().toStdString(),
        selected_format(),
        m_name_box->text().toStdString()
    );
}

void NetworkAudioSetupDialog::set_status(const std::string& text, bool error){
    std::string html = text;
    if (error){
        html = "<font color=\"red\">" + html + "</font>";
    }
    m_status_label->setText(QString::fromStdString(html));
}


void NetworkAudioSetupDialog::start_probe(){
    stop_probe();

    std::string url = m_url_box->text().toStdString();
    QUrl parsed(QString::fromStdString(url));
    if (url.empty() || !parsed.isValid() || parsed.host().isEmpty() || parsed.port() < 0){
        set_status("Enter a URL of the form tcp://host:port.", true);
        return;
    }

    m_probe_bytes = 0;
    m_connected = false;
    m_measuring = false;
    m_measured_label->clear();
    m_ok_button->setEnabled(false);
    m_connect_button->setEnabled(false);
    m_progress->setValue(0);
    m_progress->setVisible(true);
    set_status("Connecting to " + url + "...");

    m_socket = std::make_unique<QTcpSocket>();
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    connect(
        m_socket.get(), &QAbstractSocket::connected,
        this, [this]{
            m_connected = true;
            set_status("Connected. Waiting for audio...");
        }
    );
    connect(
        m_socket.get(), &QIODevice::readyRead,
        this, [this]{
            qint64 bytes = m_socket->readAll().size();
            if (!m_measuring){
                //  The clock starts at the first byte, not at connect. The gap
                //  between the two is the server spawning its encoder, and
                //  counting it would drag the measured rate below the real one.
                //  This first read is dropped with it since it holds whatever
                //  was buffered during that startup.
                m_measuring = true;
                m_probe_bytes = 0;
                m_probe_start = current_time();
                m_probe_timer->start((int)PROBE_MILLISECONDS);
                set_status("Measuring the stream...");
                return;
            }
            m_probe_bytes += (uint64_t)bytes;
            uint64_t elapsed = std::chrono::duration_cast<Milliseconds>(
                current_time() - m_probe_start
            ).count();
            m_progress->setValue((int)std::min<uint64_t>(elapsed, PROBE_MILLISECONDS));
        }
    );
    connect(
        m_socket.get(), &QAbstractSocket::errorOccurred,
        this, [this](QAbstractSocket::SocketError){
            set_status(m_socket->errorString().toStdString(), true);
            stop_probe();
        }
    );

    m_probe_start = current_time();
    m_probe_timer->start((int)STARTUP_MILLISECONDS);
    m_socket->connectToHost(parsed.host(), (quint16)parsed.port());
}
void NetworkAudioSetupDialog::stop_probe(){
    if (m_socket){
        //  We are usually called from inside the socket's own readyRead handler,
        //  so the socket has to outlive the current stack frame. Detach it from
        //  us and let the event loop delete it.
        disconnect(m_socket.get(), nullptr, this, nullptr);
        m_socket->abort();
        m_socket.release()->deleteLater();
    }
    if (m_probe_timer != nullptr){
        m_probe_timer->stop();
    }
    if (m_progress != nullptr){
        m_progress->setVisible(false);
    }
    if (m_connect_button != nullptr){
        m_connect_button->setEnabled(true);
    }
}
void NetworkAudioSetupDialog::on_probe_timeout(){
    if (!m_connected){
        stop_probe();
        set_status("Timed out connecting to the stream.", true);
        return;
    }
    if (!m_measuring){
        stop_probe();
        set_status("Connected, but the stream sent no audio.", true);
        return;
    }
    on_probe_done();
}
void NetworkAudioSetupDialog::on_probe_done(){
    uint64_t elapsed = std::chrono::duration_cast<Milliseconds>(
        current_time() - m_probe_start
    ).count();
    uint64_t measured = m_probe_bytes * 1000 / std::max<uint64_t>(elapsed, 1);
    uint64_t expected = bytes_per_second(selected_format());

    stop_probe();

    std::string measured_text = "Measured " + std::to_string(measured) + " bytes/second";
    if (expected != 0){
        measured_text += " (expected " + std::to_string(expected) + ")";
    }
    m_measured_label->setText(QString::fromStdString(measured_text + "."));

    m_ok_button->setEnabled(true);

    //  Raw PCM carries no header, so the data rate is the only check available
    //  on a format the user may have declared wrongly.
    if (expected != 0 &&
        measured * 100 > expected * (100 - TOLERANCE_PERCENT) &&
        measured * 100 < expected * (100 + TOLERANCE_PERCENT)
    ){
        set_status("Stream matches the selected data rate.");
        return;
    }

    std::string message = "The data rate does not match the selected format.";
    const char* suggestion = format_matching_rate(measured, TOLERANCE_PERCENT);
    if (suggestion != nullptr){
        message += " It matches \"";
        message += suggestion;
        message += "\".";
    }else{
        message += " Check the sample rate and channel count.";
    }
    message += " You can still press OK to use it anyway.";
    set_status(message, true);
}



}
