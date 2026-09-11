/*  Audio Passthrough Pair (Qt)
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

//#include "Common/Cpp/Exceptions.h"
#include "Common/Cpp/Logging/AbstractLogger.h"
#include "CommonFramework/GlobalSettingsPanel.h"
#include "CommonFramework/AudioPipeline/AudioPipelineOptions.h"
//#include "CommonFramework/AudioPipeline/AudioConstants.h"
//#include "CommonFramework/AudioPipeline/Tools/AudioFormatUtils.h"
#include "CommonFramework/AudioPipeline/AudioStreamInfo.h"
#include "CommonFramework/AudioPipeline/IO/AudioSource.h"
#include "CommonFramework/AudioPipeline/IO/AudioSink.h"
#include "CommonFramework/AudioPipeline/Spectrum/FFTStreamer.h"
#include "AudioPassthroughPairQt.h"

//#include <iostream>
//using std::cout;
//using std::endl;

namespace PokemonAutomation{



void AudioPassthroughPairQt::add_listener(AudioFloatStreamListener& listener){
    auto scope_check = m_sanitizer.check_scope();
    m_stream_listeners.add(listener);
}
void AudioPassthroughPairQt::remove_listener(AudioFloatStreamListener& listener){
    auto scope_check = m_sanitizer.check_scope();
    m_stream_listeners.remove(listener);
}
void AudioPassthroughPairQt::add_listener(FFTListener& listener){
    auto scope_check = m_sanitizer.check_scope();
    WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
    m_fft_listeners.add(listener);
}
void AudioPassthroughPairQt::remove_listener(FFTListener& listener){
    auto scope_check = m_sanitizer.check_scope();
    WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
    m_fft_listeners.remove(listener);
}



class AudioPassthroughPairQt::SampleListener final : public AudioFloatStreamListener{
public:
    //  Keeps its own reference to the source it attaches to. Detaching in the
    //  destructor must not go through the parent's pointer, because the parent
    //  moves that pointer out before tearing the source down.
    SampleListener(AudioPassthroughPairQt& parent, AudioSource& source)
        : AudioFloatStreamListener(source.samples_per_frame())
        , m_parent(parent)
        , m_source(source)
    {
        m_source.add_listener(*this);
    }
    ~SampleListener(){
        m_source.remove_listener(*this);
    }
    virtual void on_samples(const float* data, size_t frames) override{
        {
            AudioPassthroughPairQt& parent = m_parent;
            WriteSpinLock lg(parent.m_lock);
            if (parent.m_writer){
                AudioFloatStreamListener* listener = parent.m_writer->float_stream_listener();
                if (listener){
                    listener->on_samples(data, frames);
                }
            }
            if (parent.m_fft_runner){
                parent.m_fft_runner->on_samples(data, frames);
            }
        }
        m_parent.m_stream_listeners.run_method(
            &AudioFloatStreamListener::on_samples,
            data, frames
        );
    }

private:
    AudioPassthroughPairQt& m_parent;
    AudioSource& m_source;
};

class AudioPassthroughPairQt::InternalFFTListener final : public FFTListener{
public:
    //  Same reasoning as SampleListener above.
    InternalFFTListener(AudioPassthroughPairQt& parent, AudioFloatToFFT& runner)
        : m_parent(parent)
        , m_runner(runner)
    {
        m_runner.add_listener(*this);
    }
    ~InternalFFTListener(){
        m_runner.remove_listener(*this);
    }
    virtual void on_fft(size_t sample_rate, std::shared_ptr<const AlignedVector<float>> fft_output) override{
        m_parent.m_fft_listeners.run_method(
            &FFTListener::on_fft,
            sample_rate, fft_output
        );
    }

private:
    AudioPassthroughPairQt& m_parent;
    AudioFloatToFFT& m_runner;
};





AudioPassthroughPairQt::~AudioPassthroughPairQt(){}

AudioPassthroughPairQt::AudioPassthroughPairQt(Logger& logger)
    : m_logger(logger)
    , m_file_input_multiplier((float)GlobalSettings::instance().AUDIO_PIPELINE->FILE_VOLUME_SCALE)
    , m_device_input_multiplier((float)GlobalSettings::instance().AUDIO_PIPELINE->DEVICE_VOLUME_SCALE)
{}

void AudioPassthroughPairQt::reset(
    const std::string& file,
    const AudioDeviceInfo& output, double output_volume
){
    auto scope_check = m_sanitizer.check_scope();
    QMetaObject::invokeMethod(this, [this, file, output, output_volume]{
        auto scope_check = m_sanitizer.check_scope();
        clear_source();

        WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
        m_writer.reset();   //  Rebuilt below for the new input format.
        m_input_format = AudioChannelFormat::DUAL_48000;
        m_reader.reset(new AudioSource(m_logger, file, m_input_format, m_file_input_multiplier));
        m_sample_listener.reset(new SampleListener(*this, *m_reader));
        m_output_device = output;
        m_output_volume = output_volume;
        init_audio_sink();
        m_fft_runner = make_FFT_streamer(m_input_format);
        m_fft_listener.reset(new InternalFFTListener(*this, *m_fft_runner));
    });
}
void AudioPassthroughPairQt::reset(
    const AudioDeviceInfo& input, AudioChannelFormat format,
    const AudioDeviceInfo& output, double output_volume
){
    auto scope_check = m_sanitizer.check_scope();
//    cout << "AudioPassthroughPairQt::reset(): " << output.display_name() << endl;
    QMetaObject::invokeMethod(this, [this, format, output, output_volume, input]{
        auto scope_check = m_sanitizer.check_scope();
        clear_source();

        WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
        m_writer.reset();   //  Rebuilt below for the new input format.
        m_input_format = format;
        m_output_device = output;
        m_output_volume = output_volume;
        if (input && format != AudioChannelFormat::NONE){
            m_reader.reset(new AudioSource(m_logger, input, m_input_format, m_device_input_multiplier));
            m_sample_listener.reset(new SampleListener(*this, *m_reader));
            init_audio_sink();
            m_fft_runner = make_FFT_streamer(m_input_format);
            m_fft_listener.reset(new InternalFFTListener(*this, *m_fft_runner));
        }
    });
}
void AudioPassthroughPairQt::reset(
    const AudioStreamInfo& input,
    const AudioDeviceInfo& output, double output_volume
){
    auto scope_check = m_sanitizer.check_scope();
    QMetaObject::invokeMethod(this, [this, input, output, output_volume]{
        auto scope_check = m_sanitizer.check_scope();
        clear_source();

        WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
        m_writer.reset();   //  Rebuilt below for the new input format.
        m_input_format = input.format();
        m_output_device = output;
        m_output_volume = output_volume;
        if (input && m_input_format != AudioChannelFormat::NONE){
            m_reader.reset(new AudioSource(m_logger, input, m_device_input_multiplier));
            m_sample_listener.reset(new SampleListener(*this, *m_reader));
            init_audio_sink();
            m_fft_runner = make_FFT_streamer(m_input_format);
            m_fft_listener.reset(new InternalFFTListener(*this, *m_fft_runner));
        }
    });
}
//  Detach the current source under the lock, then destroy it once the lock is
//  released.
//
//  Destroying these blocks until any sample callback already in flight returns,
//  and that callback takes "m_lock" itself. Destroying them under the lock
//  deadlocks the two threads against each other: the audio thread spins for
//  "m_lock" from inside the callback, while we spin for that callback to end.
void AudioPassthroughPairQt::clear_source(){
    auto scope_check = m_sanitizer.check_scope();

    //  Declared in reverse of the order they must be destroyed in.
    std::unique_ptr<AudioSource> reader;
    std::unique_ptr<SampleListener> sample_listener;
    std::unique_ptr<AudioFloatToFFT> fft_runner;
    std::unique_ptr<InternalFFTListener> fft_listener;
    {
        WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
        if (!m_reader){
            return;
        }
        fft_listener = std::move(m_fft_listener);
        fft_runner = std::move(m_fft_runner);
        sample_listener = std::move(m_sample_listener);
        reader = std::move(m_reader);
        m_input_format = AudioChannelFormat::NONE;
    }
}
void AudioPassthroughPairQt::clear_audio_source(){
    auto scope_check = m_sanitizer.check_scope();
    QMetaObject::invokeMethod(this, [this]{
        auto scope_check = m_sanitizer.check_scope();
        clear_source();
    });
}
void AudioPassthroughPairQt::set_audio_source(const std::string& file){
    auto scope_check = m_sanitizer.check_scope();
    QMetaObject::invokeMethod(this, [this, file]{
        auto scope_check = m_sanitizer.check_scope();
        clear_source();

        WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
        m_writer.reset();   //  Rebuilt below for the new input format.
        m_input_format = AudioChannelFormat::DUAL_48000;
        m_reader.reset(new AudioSource(m_logger, file, m_input_format, m_file_input_multiplier));
        m_sample_listener.reset(new SampleListener(*this, *m_reader));
        init_audio_sink();
        m_fft_runner = make_FFT_streamer(m_input_format);
        m_fft_listener.reset(new InternalFFTListener(*this, *m_fft_runner));
    });
}
void AudioPassthroughPairQt::set_audio_source(const AudioDeviceInfo& device, AudioChannelFormat format){
    auto scope_check = m_sanitizer.check_scope();
    QMetaObject::invokeMethod(this, [this, format, device]{
        auto scope_check = m_sanitizer.check_scope();
        clear_source();

        WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
        m_writer.reset();   //  Rebuilt below for the new input format.
        m_input_format = format;
        if (device){
            m_reader.reset(new AudioSource(m_logger, device, m_input_format, m_device_input_multiplier));
            m_sample_listener.reset(new SampleListener(*this, *m_reader));
            init_audio_sink();
            m_fft_runner = make_FFT_streamer(m_input_format);
            m_fft_listener.reset(new InternalFFTListener(*this, *m_fft_runner));
        }
    });
}

void AudioPassthroughPairQt::set_audio_source(const AudioStreamInfo& input){
    auto scope_check = m_sanitizer.check_scope();
    QMetaObject::invokeMethod(this, [this, input]{
        auto scope_check = m_sanitizer.check_scope();
        clear_source();

        WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
        m_writer.reset();   //  Rebuilt below for the new input format.
        m_input_format = input.format();
        if (input && m_input_format != AudioChannelFormat::NONE){
            m_reader.reset(new AudioSource(m_logger, input, m_device_input_multiplier));
            m_sample_listener.reset(new SampleListener(*this, *m_reader));
            init_audio_sink();
            m_fft_runner = make_FFT_streamer(m_input_format);
            m_fft_listener.reset(new InternalFFTListener(*this, *m_fft_runner));
        }
    });
}

void AudioPassthroughPairQt::clear_audio_sink(){
    auto scope_check = m_sanitizer.check_scope();
    QMetaObject::invokeMethod(this, [this]{
        auto scope_check = m_sanitizer.check_scope();
        WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
        m_writer.reset();
        m_output_device = AudioDeviceInfo();
    });
}
void AudioPassthroughPairQt::set_audio_sink(const AudioDeviceInfo& device, double volume){
    auto scope_check = m_sanitizer.check_scope();
    QMetaObject::invokeMethod(this, [this, device, volume]{
        auto scope_check = m_sanitizer.check_scope();
        WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
        m_writer.reset();
        m_output_device = device;
        m_output_volume = volume;
        init_audio_sink();
    });
}
void AudioPassthroughPairQt::init_audio_sink(){
    auto scope_check = m_sanitizer.check_scope();
//    cout << "AudioPassthroughPairQt::init_audio_sink()" << endl;
    QMetaObject::invokeMethod(this, [this]{
        auto scope_check = m_sanitizer.check_scope();
        if (!m_output_device){
//            cout << "AudioPassthroughPairQt::init_audio_sink() - early out" << endl;
            return;
        }
        AudioChannelFormat output_format = m_input_format;
        switch (output_format){
        case AudioChannelFormat::NONE:
            return;
        case AudioChannelFormat::MONO_48000:
        case AudioChannelFormat::MONO_96000:
        case AudioChannelFormat::DUAL_44100:
        case AudioChannelFormat::DUAL_48000:
            break;
        case AudioChannelFormat::INTERLEAVE_LR_96000:
        case AudioChannelFormat::INTERLEAVE_RL_96000:
            output_format = AudioChannelFormat::DUAL_48000;
            break;
        default:
            m_logger.log(std::string("Invalid AudioFormat: ") + AUDIO_FORMAT_LABELS[(size_t)output_format], COLOR_RED);
            return;
        }
//        cout << "AudioPassthroughPairQt::init_audio_sink() - end" << endl;
        m_writer.reset(new AudioSink(m_logger, m_output_device, output_format, m_output_volume));
    });
}


void AudioPassthroughPairQt::set_sink_volume(double volume){
    auto scope_check = m_sanitizer.check_scope();
    QMetaObject::invokeMethod(this, [this, volume]{
        auto scope_check = m_sanitizer.check_scope();
        WriteSpinLock lg(m_lock, PA_CURRENT_FUNCTION);
        m_output_volume = volume;
        if (m_writer){
            m_writer->set_volume(volume);
        }
    });
}







}
