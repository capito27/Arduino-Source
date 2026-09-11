/*  Audio Source
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include <QtGlobal>
#if QT_VERSION_MAJOR == 5
#include <QAudioInput>
using NativeAudioSource = QAudioInput;
#elif QT_VERSION_MAJOR == 6
#include <QAudioSource>
using NativeAudioSource = QAudioSource;
#endif

#include <QUrl>
#include <QThread>
#include <QTcpSocket>
#include "Common/Cpp/Exceptions.h"
#include "Common/Cpp/PrettyPrint.h"
#include "Common/Cpp/Time.h"
//#include "Common/Cpp/StreamConverters.h"
#include "CommonFramework/AudioPipeline/AudioStream.h"
#include "CommonFramework/AudioPipeline/AudioStreamInfo.h"
#include "CommonFramework/AudioPipeline/Tools/AudioFormatUtils.h"
#include "AudioFileLoader.h"
#include "AudioSource.h"

//#include <iostream>
//using std::cout;
//using std::endl;

namespace PokemonAutomation{



class AudioInputFile final : public QObject{
public:
    AudioInputFile(
        Logger& logger, AudioStreamToFloat& reader,
        const std::string& file, const QAudioFormat& format
    )
         : m_reader(reader)
    {
        logger.log("AudioInputFile(): " + dump_audio_format(format));
        m_source = std::make_unique<AudioFileLoader>(nullptr, file, format);
        connect(
            m_source.get(), &AudioFileLoader::bufferReady,
            this, [this](const char* data, size_t len){
                m_reader.push_bytes(data, len);
            }
        );
        m_source->start();
    }

private:
    AudioStreamToFloat& m_reader;
    std::unique_ptr<AudioFileLoader> m_source;
};

class AudioInputDevice final : public QIODevice{
public:
    AudioInputDevice(
        Logger& logger, AudioStreamToFloat& reader,
        const NativeAudioInfo& device, const QAudioFormat& format
    )
         : m_reader(reader)
    {
        logger.log("AudioInputDevice(): " + dump_audio_format(format));
        if (!device.isFormatSupported(format)){
//            throw InternalProgramError(&logger, PA_CURRENT_FUNCTION, "Format not supported: " + dump_audio_format(format));
            logger.log("Format not supported: " + dump_audio_format(format), COLOR_RED);
            return;
        }
        m_source = std::make_unique<NativeAudioSource>(device, format);

        this->open(QIODevice::ReadWrite | QIODevice::Unbuffered);

        WallClock start = current_time();
        m_source->start(this);
        WallClock end = current_time();
        double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000.;
        logger.log("Done starting audio... " + tostr_fixed(seconds, 3) + " seconds", COLOR_CYAN);
    }
    ~AudioInputDevice(){
        if (m_source){
            m_source->stop();
        }
    }

    virtual bool isSequential() const override { return true; }
    virtual qint64 readData(char* data, qint64 maxlen) override { return 0; }
    virtual qint64 writeData(const char* data, qint64 len) override{
        m_reader.push_bytes(data, len);
        return len;
    }

private:
    AudioStreamToFloat& m_reader;
    std::unique_ptr<NativeAudioSource> m_source;
};




//  Read raw PCM from a stream served over TCP.
//
//  The stream is headerless, so the sample rate, channel count and sample format
//  cannot be read off it. They must match the format we are given.
//
//  The socket gets its own thread. Qt hands us the samples from an audio device
//  on an internal thread of its own, and everything downstream of push_bytes()
//  - the FFT, the passthrough to the speakers, the inference listeners - was
//  written against that. A socket owned by the UI thread would run all of it
//  there instead, at ~190 wakeups a second.
class AudioInputStreamWorker final : public QObject{
public:
    AudioInputStreamWorker(
        Logger& logger, AudioStreamToFloat& reader,
        const QAudioFormat& format, const QString& host, quint16 port
    )
        : m_logger(logger)
        , m_reader(reader)
        , m_frame_size(format.bytesPerFrame())
        , m_bytes_per_second((qint64)format.bytesPerFrame() * format.sampleRate())
        , m_max_backlog(m_bytes_per_second * MAX_BACKLOG_MILLISECONDS / 1000)
        , m_buffer(65536)
        , m_host(host)
        , m_port(port)
    {}

    //  Both of these must run on the worker thread.
    void open(){
        m_socket = std::make_unique<QTcpSocket>();
        m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

        //  Bound how much the kernel is allowed to hoard for us.
        m_socket->setReadBufferSize(2 * m_max_backlog);

        connect(
            m_socket.get(), &QIODevice::readyRead,
            this, [this]{ on_ready_read(); }
        );
        connect(
            m_socket.get(), &QAbstractSocket::errorOccurred,
            this, [this](QAbstractSocket::SocketError){
                m_logger.log("AudioInputStream(): " + m_socket->errorString().toStdString(), COLOR_RED);
            }
        );

        m_connect_time = current_time();
        m_socket->connectToHost(m_host, m_port);
    }
    void close(){
        m_socket.reset();
    }

private:
    void on_ready_read(){
        if (m_first_bytes){
            m_first_bytes = false;
            double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time() - m_connect_time
            ).count() / 1000.;
            m_logger.log("First audio bytes after " + tostr_fixed(seconds, 3) + " seconds", COLOR_CYAN);
        }

        //  These servers queue for a slow client instead of dropping, so any
        //  hiccup would otherwise leave us permanently behind. Discard all but
        //  the newest samples. Raw PCM can be cut at any frame boundary.
        qint64 available = m_socket->bytesAvailable();
        if (available > m_max_backlog){
            qint64 skip = available - m_max_backlog;
            skip -= skip % m_frame_size;    //  Never cut mid-frame or we swap the channels.
            m_socket->skip(skip);
            m_dropped_bytes += skip;
        }else if (m_dropped_bytes != 0){
            //  Caught up. One stall spans many reads, so it is only worth
            //  reporting once we are live again.
            m_logger.log(
                "Fell behind on the audio stream. Dropped " +
                std::to_string(m_dropped_bytes * 1000 / m_bytes_per_second) + " ms to catch up.",
                COLOR_ORANGE
            );
            m_dropped_bytes = 0;
        }

        while (true){
            qint64 bytes = m_socket->read(m_buffer.data(), (qint64)m_buffer.size());
            if (bytes <= 0){
                break;
            }
            m_reader.push_bytes(m_buffer.data(), (size_t)bytes);
        }
    }

private:
    //  How far behind live we let ourselves fall before discarding.
    static const qint64 MAX_BACKLOG_MILLISECONDS = 100;

    Logger& m_logger;
    AudioStreamToFloat& m_reader;
    qint64 m_frame_size;
    qint64 m_bytes_per_second;
    qint64 m_max_backlog;
    std::vector<char> m_buffer;
    QString m_host;
    quint16 m_port;

    bool m_first_bytes = true;
    WallClock m_connect_time;
    qint64 m_dropped_bytes = 0;

    std::unique_ptr<QTcpSocket> m_socket;
};


//  Owns the worker and the thread it runs on.
class AudioInputStream final{
public:
    ~AudioInputStream(){
        if (m_worker){
            if (m_thread.isRunning()){
                //  The socket was made on the worker thread and has to be shut
                //  down there. Block so that nothing is still reading into the
                //  reader once we return.
                QMetaObject::invokeMethod(
                    m_worker.get(),
                    [this]{ m_worker->close(); },
                    Qt::BlockingQueuedConnection
                );
            }
            m_thread.quit();
            m_thread.wait();
        }
    }
    AudioInputStream(
        Logger& logger, AudioStreamToFloat& reader,
        const std::string& url, const QAudioFormat& format
    ){
        logger.log("AudioInputStream(): " + url + " - " + dump_audio_format(format));

        QUrl parsed(QString::fromStdString(url));
        if (!parsed.isValid() || parsed.host().isEmpty() || parsed.port() < 0){
            logger.log("Invalid stream URL: " + url, COLOR_RED);
            return;
        }

        m_worker.reset(new AudioInputStreamWorker(
            logger, reader, format, parsed.host(), (quint16)parsed.port()
        ));
        m_worker->moveToThread(&m_thread);

        //  Named so it is identifiable in a debugger or a hang dump.
        m_thread.setObjectName("AudioInputStream");
        m_thread.start();

        //  Runs once the thread's event loop is up.
        QMetaObject::invokeMethod(m_worker.get(), [this]{ m_worker->open(); });
    }

private:
    QThread m_thread;
    std::unique_ptr<AudioInputStreamWorker> m_worker;
};



class AudioSource::InternalListener : public AudioFloatStreamListener{
public:
    InternalListener(AudioSource& parent)
        : AudioFloatStreamListener(parent.m_channels * parent.m_multiplier)
        , m_parent(parent)
    {}

private:
    virtual void on_samples(const float* data, size_t objects) override{
//        cout << "objects = " << objects << endl;
        m_parent.m_listeners.run_method(
            &AudioFloatStreamListener::on_samples,
            data, objects
        );
    }

    AudioSource& m_parent;
};




void AudioSource::add_listener(AudioFloatStreamListener& listener){
    auto scope_check = m_sanitizer.check_scope();
    m_listeners.add(listener);
}
void AudioSource::remove_listener(AudioFloatStreamListener& listener){
    auto scope_check = m_sanitizer.check_scope();
    m_listeners.remove(listener);
}


AudioSource::~AudioSource(){}

AudioSource::AudioSource(Logger& logger, const std::string& file, AudioChannelFormat format, float volume_multiplier){
    QAudioFormat native_format;
    set_sample_format_to_float(native_format);
    set_format(native_format, format);
    init(format, AudioSampleFormat::FLOAT32, volume_multiplier);
    m_input_file = std::make_unique<AudioInputFile>(logger, *m_reader, file, native_format);
}
AudioSource::AudioSource(Logger& logger, const AudioDeviceInfo& device, AudioChannelFormat format, float volume_multiplier){
    NativeAudioInfo native_info = device.native_info();
    QAudioFormat native_format = native_info.preferredFormat();

    set_format(native_format, format);

    AudioSampleFormat stream_format = get_sample_format(native_format);
    if (stream_format == AudioSampleFormat::INVALID){
        stream_format = AudioSampleFormat::FLOAT32;
        set_sample_format_to_float(native_format);
    }

    init(format, stream_format, volume_multiplier);
    m_input_device = std::make_unique<AudioInputDevice>(logger, *m_reader, native_info, native_format);
}
AudioSource::AudioSource(Logger& logger, const AudioStreamInfo& stream, float volume_multiplier){
    //  The stream carries no header, so we tell the reader what the user said
    //  the stream is instead of asking the stream.
    QAudioFormat native_format;
    native_format.setSampleFormat(QAudioFormat::Int16);
    set_format(native_format, stream.format());

    init(stream.format(), AudioSampleFormat::SINT16, volume_multiplier);
    m_input_stream = std::make_unique<AudioInputStream>(logger, *m_reader, stream.url(), native_format);
}

void AudioSource::init(AudioChannelFormat format, AudioSampleFormat stream_format, float volume_multiplier){
    auto scope_check = m_sanitizer.check_scope();
    switch (format){
    case AudioChannelFormat::MONO_48000:
        m_sample_rate = 48000;
        m_channels = 1;
        m_multiplier = 1;
        m_reader.reset(new AudioStreamToFloat(stream_format, 1, volume_multiplier, false));
        break;
    case AudioChannelFormat::DUAL_44100:
        m_sample_rate = 44100;
        m_channels = 2;
        m_multiplier = 1;
        m_reader.reset(new AudioStreamToFloat(stream_format, 2, volume_multiplier, false));
        break;
    case AudioChannelFormat::DUAL_48000:
        m_sample_rate = 48000;
        m_channels = 2;
        m_multiplier = 1;
        m_reader.reset(new AudioStreamToFloat(stream_format, 2, volume_multiplier, false));
        break;
    case AudioChannelFormat::MONO_96000:
        //  Treat mono-96000 as 2-sample frames.
        //  The FFT will then average each pair to produce 48000Hz.
        //  The output will push the same stream at the original 4 bytes * 96000Hz.
        m_sample_rate = 96000;
        m_channels = 1;
        m_multiplier = 2;
        m_reader.reset(new AudioStreamToFloat(stream_format, 2, volume_multiplier, false));
        break;
    case AudioChannelFormat::INTERLEAVE_LR_96000:
        m_sample_rate = 48000;
        m_channels = 2;
        m_multiplier = 1;
        m_reader.reset(new AudioStreamToFloat(stream_format, 2, volume_multiplier, false));
        break;
    case AudioChannelFormat::INTERLEAVE_RL_96000:
        m_sample_rate = 48000;
        m_channels = 2;
        m_multiplier = 1;
        m_reader.reset(new AudioStreamToFloat(stream_format, 2, volume_multiplier, true));
        break;
    default:
        throw InternalProgramError(nullptr, PA_CURRENT_FUNCTION, "Invalid AudioFormat: " + std::to_string((size_t)format));
    }

    m_internal_listener = std::make_unique<InternalListener>(*this);
    m_reader->add_listener(*m_internal_listener);
}









}
