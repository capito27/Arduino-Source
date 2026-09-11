/*  End-to-End Latency Tester
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include <algorithm>
#include <cmath>
#include "Common/Cpp/PrettyPrint.h"
#include "CommonFramework/AudioPipeline/AudioConstants.h"
#include "CommonFramework/AudioPipeline/AudioFeed.h"
#include "CommonFramework/ImageTools/ImageStats.h"
#include "CommonFramework/ProgramStats/StatsTracking.h"
#include "CommonFramework/VideoPipeline/VideoFeed.h"
#include "CommonFramework/VideoPipeline/VideoOverlayScopes.h"
#include "CommonTools/Async/InferenceSession.h"
#include "CommonTools/Images/ImageFilter.h"
#include "CommonTools/InferenceCallbacks/AudioInferenceCallback.h"
#include "CommonTools/InferenceCallbacks/VisualInferenceCallback.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_PushButtons.h"
#include "NintendoSwitch_EndToEndLatencyTester.h"

//#include <iostream>
//using std::cout;
//using std::endl;

namespace PokemonAutomation{
namespace NintendoSwitch{


//  Long enough to pick up the click reliably, short enough that a missed
//  detection doesn't stall the run.
const Milliseconds DPAD_HOLD = Milliseconds(80);
const Milliseconds DPAD_RELEASE = Milliseconds(20);

//  How often we poll the video. The video pivot only ever hands out the newest
//  snapshot, so polling slower than the frame rate would let us skip the first
//  frame carrying the label and overstate the latency. Audio does not need this
//  since spectrums are handed over in batches and none are dropped.
const Milliseconds VIDEO_PERIOD = Milliseconds(5);
const Milliseconds AUDIO_PERIOD = Milliseconds(10);

//  The strip under the bottom icon row where the selected icon's name appears.
//  Taken from a 1080p Switch 1 home screen. It only has to be empty while the
//  cursor is on the game row and hold the name once it moves down, so it is
//  deliberately wider than any one label.
const ImageFloatBox LABEL_BOX(0.100, 0.840, 0.280, 0.055);

//  How much more speckled the label region has to get before we call it text.
//  This is measured against the region as it was just before the press, so it
//  does not care what the menu or the HDR setting do to the absolute level.
const double LABEL_STDDEV_MARGIN = 8.0;

//  The cursor click sits near 1.3kHz. The HOME press that resets each trial
//  thuds near 300Hz, so keeping to this band keeps the reset out of the way.
const size_t CLICK_BAND_LOW_HZ = 1100;
const size_t CLICK_BAND_HIGH_HZ = 1600;

//  How far the band has to rise above its recent level to count as the click.
//  Relative for the same reason as the video: the absolute level depends on the
//  console volume and the capture gain, neither of which we control.
const double CLICK_RATIO = 6.0;



//  Fires on the first frame where text appears inside the box.
//
//  This tests the spread of the pixels rather than their brightness. The label
//  area is flat when empty and speckled when it holds text, which holds up
//  across the Switch 1 and Switch 2 menus and across HDR settings in a way that
//  an absolute brightness would not.
class LabelAppearanceDetector : public VisualInferenceCallback{
public:
    LabelAppearanceDetector(double baseline_stddev)
        : VisualInferenceCallback("LabelAppearanceDetector")
        , m_threshold(baseline_stddev + LABEL_STDDEV_MARGIN)
    {}

    WallClock detected() const{ return m_detected; }
    double last_stddev() const{ return m_last_stddev; }

    virtual void make_overlays(VideoOverlaySet& items) const override{
        items.add(COLOR_CYAN, LABEL_BOX);
    }
    virtual bool process_frame(const ImageViewRGB32& frame, WallClock timestamp) override{
        ImageStats stats = image_stats(extract_box_reference(frame, LABEL_BOX));
        m_last_stddev = stats.stddev.sum();
        if (m_detected == WallClock::min() && m_last_stddev > m_threshold){
            m_detected = timestamp;
        }
        //  Never stop the session. The audio callback needs to keep running
        //  even after this one has seen what it was waiting for.
        return false;
    }

private:
    double m_threshold;
    double m_last_stddev = 0;
    WallClock m_detected = WallClock::min();
};



//  Fires on the first spectrum where the cursor click shows up.
//
//  Only a narrow band is looked at. Moving onto the icon row clicks at about
//  1.3 kHz while the HOME press that resets each trial thuds at about 300 Hz,
//  so restricting the band keeps the reset out of the measurement.
class CursorClickDetector : public AudioInferenceCallback{
public:
    CursorClickDetector()
        : AudioInferenceCallback("CursorClickDetector")
    {}

    WallClock detected() const{ return m_detected; }
    double peak_energy() const{ return m_peak_energy; }

    virtual bool process_spectrums(
        const std::vector<AudioSpectrum>& new_spectrums,
        AudioFeed& audio_feed
    ) override{
        if (new_spectrums.empty()){
            return false;
        }

        //  Spectrums carry a window index instead of a clock, so the time of one
        //  is reconstructed by counting windows back from the newest one we were
        //  handed. Each window is FFT_SLIDING_WINDOW_STEP samples further along.
        WallClock now = current_time();
        uint64_t newest = new_spectrums[0].stamp;
        size_t sample_rate = new_spectrums[0].sample_rate;
        if (sample_rate == 0){
            return false;
        }

        //  Oldest first, so the running level only ever holds what came before
        //  the window being tested.
        for (auto iter = new_spectrums.rbegin(); iter != new_spectrums.rend(); ++iter){
            const AudioSpectrum& spectrum = *iter;
            double energy = band_energy(spectrum);
            m_peak_energy = std::max(m_peak_energy, energy);

            bool is_click = m_samples >= MINIMUM_SAMPLES && energy > m_level * CLICK_RATIO;

            //  Fold into the running level before the test result is used, but
            //  skip the click itself so one trial cannot raise the bar for the
            //  next window.
            if (!is_click){
                m_level = m_samples == 0 ? energy : m_level * 0.8 + energy * 0.2;
                m_samples++;
            }
            if (!is_click || m_detected != WallClock::min()){
                continue;
            }
            uint64_t windows_back = newest - spectrum.stamp;
            double milliseconds =
                (double)windows_back * FFT_SLIDING_WINDOW_STEP * 1000 / sample_rate;
            m_detected = now - std::chrono::microseconds((uint64_t)(milliseconds * 1000));
        }
        return false;
    }


private:
    //  Mean magnitude over the band of interest. The magnitudes run from 0 to
    //  half the sample rate, evenly spaced.
    //  Enough windows to have a level worth comparing against.
    static const uint64_t MINIMUM_SAMPLES = 4;

    double band_energy(const AudioSpectrum& spectrum) const{
        if (!spectrum.magnitudes){
            return 0;
        }
        const AlignedVector<float>& magnitudes = *spectrum.magnitudes;
        size_t count = magnitudes.size();
        if (count == 0){
            return 0;
        }
        size_t half_rate = spectrum.sample_rate / 2;
        size_t start = CLICK_BAND_LOW_HZ * count / half_rate;
        size_t end = CLICK_BAND_HIGH_HZ * count / half_rate + 1;
        start = std::min(start, count);
        end = std::min(end, count);
        if (start >= end){
            return 0;
        }
        double sum = 0;
        for (size_t c = start; c < end; c++){
            sum += magnitudes[c];
        }
        return sum / (end - start);
    }

private:
    double m_level = 0;
    uint64_t m_samples = 0;
    double m_peak_energy = 0;
    WallClock m_detected = WallClock::min();
};



//  Prints the spread rather than a single number. One trial lands somewhere
//  inside the sampling interval of whichever stream it came from, so only the
//  distribution across many of them says anything.
void report(
    SingleSwitchProgramEnvironment& env,
    const std::string& label,
    std::vector<double>& times
){
    if (times.empty()){
        env.log(label + ": no samples.", COLOR_RED);
        env.add_overlay_log(label + ": no samples.", COLOR_RED);
        return;
    }
    std::sort(times.begin(), times.end());

    double sum = 0;
    for (double time : times){
        sum += time;
    }
    double mean = sum / times.size();

    double variance = 0;
    for (double time : times){
        variance += (time - mean) * (time - mean);
    }
    double stddev = std::sqrt(variance / times.size());

    double median = times[times.size() / 2];
    env.log(
        label + ": n = " + std::to_string(times.size()) +
        ", median " + tostr_fixed(median, 1) +
        " ms, mean " + tostr_fixed(mean, 1) +
        " ms, stddev " + tostr_fixed(stddev, 1) +
        " ms, min " + tostr_fixed(times.front(), 1) +
        " ms, max " + tostr_fixed(times.back(), 1) + " ms",
        COLOR_GREEN
    );
    env.add_overlay_log(
        label + ": " + tostr_fixed(median, 1) +
        " ms +/- " + tostr_fixed(stddev, 1) + " ms",
        COLOR_GREEN
    );
}



//  Every stat is an integer counter, so the averages cannot be held directly.
//  A total and a sample count are kept instead and the average is derived when
//  the bar is drawn, which also keeps it exact as trials accumulate.
struct EndToEndLatencyTester_Descriptor::Stats : public StatsTracker{
    Stats()
        : m_trials(m_stats["Trials"])
        , m_input_samples(m_stats["Input Samples"])
        , m_input_total(m_stats["Input Total (us)"])
        , m_video_samples(m_stats["Video Samples"])
        , m_video_total(m_stats["Video Total (ms)"])
        , m_audio_samples(m_stats["Audio Samples"])
        , m_audio_total(m_stats["Audio Total (ms)"])
        , m_missed(m_stats["Missed"])
    {
        m_display_order.emplace_back("Trials");
        m_display_order.emplace_back("Input Samples", ALWAYS_HIDDEN);
        m_display_order.emplace_back("Input Total (us)", ALWAYS_HIDDEN);
        m_display_order.emplace_back("Video Samples", ALWAYS_HIDDEN);
        m_display_order.emplace_back("Video Total (ms)", ALWAYS_HIDDEN);
        m_display_order.emplace_back("Audio Samples", ALWAYS_HIDDEN);
        m_display_order.emplace_back("Audio Total (ms)", ALWAYS_HIDDEN);
        m_display_order.emplace_back("Missed", HIDDEN_IF_ZERO);
    }

    virtual std::string to_str(PrintMode mode) const override{
        //  The other modes are written to the stats file and read back by
        //  parse_and_append_line(), so they have to keep the raw label/value
        //  form. Only the on-screen bar gets the derived numbers.
        if (mode != DISPLAY_ON_SCREEN){
            return StatsTracker::to_str(mode);
        }

        uint64_t input_samples = m_input_samples.load(std::memory_order_relaxed);
        uint64_t video_samples = m_video_samples.load(std::memory_order_relaxed);
        uint64_t audio_samples = m_audio_samples.load(std::memory_order_relaxed);
        uint64_t missed = m_missed.load(std::memory_order_relaxed);

        std::string str = "Trials: " + tostr_u_commas(m_trials.load(std::memory_order_relaxed));

        //  Kept in microseconds. This one runs in the low single-digit
        //  milliseconds, so whole milliseconds would throw away most of it.
        if (input_samples != 0){
            double input_mean =
                (double)m_input_total.load(std::memory_order_relaxed) / input_samples / 1000.;
            str += " - Input: " + tostr_fixed(input_mean, 2) + " ms";
        }

        double video_mean = 0;
        double audio_mean = 0;
        if (video_samples != 0){
            video_mean = (double)m_video_total.load(std::memory_order_relaxed) / video_samples;
            str += " - Video: " + tostr_fixed(video_mean, 1) + " ms";
        }
        if (audio_samples != 0){
            audio_mean = (double)m_audio_total.load(std::memory_order_relaxed) / audio_samples;
            str += " - Audio: " + tostr_fixed(audio_mean, 1) + " ms";
        }
        if (video_samples != 0 && audio_samples != 0){
            str += " - Skew: " + tostr_fixed(audio_mean - video_mean, 1) + " ms";
        }
        if (missed != 0){
            str += " - Missed: " + tostr_u_commas(missed);
        }
        return str;
    }

    std::atomic<uint64_t>& m_trials;
    std::atomic<uint64_t>& m_input_samples;
    std::atomic<uint64_t>& m_input_total;
    std::atomic<uint64_t>& m_video_samples;
    std::atomic<uint64_t>& m_video_total;
    std::atomic<uint64_t>& m_audio_samples;
    std::atomic<uint64_t>& m_audio_total;
    std::atomic<uint64_t>& m_missed;
};
std::unique_ptr<StatsTracker> EndToEndLatencyTester_Descriptor::make_stats() const{
    return std::unique_ptr<StatsTracker>(new Stats());
}


EndToEndLatencyTester_Descriptor::EndToEndLatencyTester_Descriptor()
    : SingleSwitchProgramDescriptor(
        "NintendoSwitch:EndToEndLatencyTester",
        "Nintendo Switch", "End-to-End Latency Tester",
        "Programs/NintendoSwitch/EndToEndLatencyTester.html",
        "Measure how long a controller input takes to come back as video and audio.",
        ProgramControllerClass::StandardController_NoRestrictions,
        FeedbackType::VIDEO_AUDIO,
        AllowCommandsWhenRunning::DISABLE_COMMANDS,
        false,
        {}
    )
{}


EndToEndLatencyTester::EndToEndLatencyTester()
    : TRIALS(
        "<b>Trials:</b><br>"
        "Video and audio are only sampled every 17ms and 21ms respectively, so a "
        "single trial says very little. Run enough of them to read the spread.",
        LockMode::LOCK_WHILE_RUNNING,
        20, 1, 1000
    )
    , SETTLE_DELAY(
        "<b>Settle Delay:</b><br>"
        "Time to let the console settle after the HOME press before timing.",
        LockMode::LOCK_WHILE_RUNNING,
        Milliseconds(0), Milliseconds(10000), "1000 ms"
    )
    , LISTEN_WINDOW(
        "<b>Listen Window:</b><br>"
        "How long to keep watching for the video and audio response.",
        LockMode::LOCK_WHILE_RUNNING,
        Milliseconds(100), Milliseconds(10000), "1000 ms"
    )
{
    PA_ADD_OPTION(TRIALS);
    PA_ADD_OPTION(SETTLE_DELAY);
    PA_ADD_OPTION(LISTEN_WINDOW);
}


double EndToEndLatencyTester::measure_label_baseline(
    SingleSwitchProgramEnvironment& env,
    CancellableScope& scope
){
    //  HOME clears the label on its own schedule, so wait for the region to stop
    //  moving rather than for any particular value. Timing from a state we have
    //  not confirmed would fold the reset into the result, and reading the
    //  baseline off a region still in motion would set the bar in the wrong
    //  place.
    WallClock deadline = current_time() + std::chrono::seconds(5);
    double previous = -1;
    while (current_time() < deadline){
        VideoSnapshot snapshot = env.console.video().snapshot();
        if (snapshot){
            double stddev = image_stats(
                extract_box_reference(snapshot, LABEL_BOX)
            ).stddev.sum();
            if (previous >= 0 && std::abs(stddev - previous) < 1.0){
                return stddev;
            }
            previous = stddev;
        }
        scope.wait_for(std::chrono::milliseconds(50));
    }
    return -1;
}


void EndToEndLatencyTester::program(SingleSwitchProgramEnvironment& env, CancellableScope& scope){
    EndToEndLatencyTester_Descriptor::Stats& stats = env.current_stats<EndToEndLatencyTester_Descriptor::Stats>();
    ProControllerContext context(scope, env.console.controller<ProController>());

    std::vector<double> controller_times;
    std::vector<double> video_times;
    std::vector<double> audio_times;
    std::vector<double> skew_times;

    for (uint16_t trial = 0; trial < TRIALS; trial++){
        //  Put the cursor back on the leftmost game. HOME gets there from
        //  anywhere in the system.
        pbf_press_button(context, BUTTON_HOME, DPAD_HOLD, SETTLE_DELAY);
        context.wait_for_all_requests();

        double baseline = measure_label_baseline(env, scope);
        if (baseline < 0){
            env.log("Trial " + std::to_string(trial) + ": label region never settled. Skipping.", COLOR_RED);
            env.add_overlay_log("Label region never settled.", COLOR_RED);
            stats.m_missed++;
            env.update_stats();
            continue;
        }

        LabelAppearanceDetector video_detector(baseline);
        CursorClickDetector audio_detector;

        WallClock issued;
        WallClock acknowledged;
        {
            InferenceSession session(
                scope, env.console,
                {
                    {video_detector, VIDEO_PERIOD},
                    {audio_detector, AUDIO_PERIOD},
                }
            );

            //  Drain first so the timestamp covers only our own command.
            context.wait_for_all_requests();
            issued = current_time();
            pbf_press_dpad(context, DPAD_DOWN, DPAD_HOLD, DPAD_RELEASE);
            context.wait_for_all_requests();
            acknowledged = current_time();

            scope.wait_for(LISTEN_WINDOW);
        }

        //  The press itself is part of the round trip, so take it back out to
        //  leave just the cost of getting the command to the console.
        double controller_ms =
            std::chrono::duration_cast<std::chrono::microseconds>(acknowledged - issued).count() / 1000.
            - (DPAD_HOLD + DPAD_RELEASE).count();
        controller_times.emplace_back(controller_ms);

        std::string line = "Trial " + std::to_string(trial) + ": controller " +
            tostr_fixed(controller_ms, 1) + " ms";

        bool have_video = video_detector.detected() != WallClock::min();
        bool have_audio = audio_detector.detected() != WallClock::min();

        if (have_video){
            double video_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                video_detector.detected() - issued
            ).count() / 1000.;
            video_times.emplace_back(video_ms);
            line += ", video " + tostr_fixed(video_ms, 1) + " ms";
        }else{
            line += ", video MISSED (stddev peaked at " +
                tostr_fixed(video_detector.last_stddev(), 1) + ")";
        }

        if (have_audio){
            double audio_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                audio_detector.detected() - issued
            ).count() / 1000.;
            audio_times.emplace_back(audio_ms);
            line += ", audio " + tostr_fixed(audio_ms, 1) + " ms";
        }else{
            line += ", audio MISSED (band peaked at " +
                tostr_fixed(audio_detector.peak_energy(), 3) + ")";
        }

        if (have_video && have_audio){
            //  Both sides share the same starting point and the same console
            //  latency, so this is the one figure that is not carrying an
            //  unknown offset.
            skew_times.emplace_back(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    audio_detector.detected() - video_detector.detected()
                ).count() / 1000.
            );
        }

        env.log(line, COLOR_BLUE);
        env.add_overlay_log(line, COLOR_BLUE);

        stats.m_trials++;
        if (controller_ms > 0){
            stats.m_input_samples++;
            stats.m_input_total += (uint64_t)(controller_ms * 1000 + 0.5);
        }
        if (have_video){
            stats.m_video_samples++;
            stats.m_video_total += (uint64_t)(video_times.back() + 0.5);
        }
        if (have_audio){
            stats.m_audio_samples++;
            stats.m_audio_total += (uint64_t)(audio_times.back() + 0.5);
        }
        if (!have_video || !have_audio){
            stats.m_missed++;
        }
        env.update_stats();
    }

    report(env, "Controller acknowledgement", controller_times);
    report(env, "Video feedback", video_times);
    report(env, "Audio feedback", audio_times);
    report(env, "Audio minus video (skew)", skew_times);
}



}
}
