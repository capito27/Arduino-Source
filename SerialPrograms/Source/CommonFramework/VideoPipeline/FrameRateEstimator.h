/*  Frame Rate Estimator
 *
 *  From: https://github.com/PokemonAutomation/
 *
 *      Not thread-safe. Push frames from one thread.
 *
 */

#ifndef PokemonAutomation_VideoPipeline_FrameRateEstimator_H
#define PokemonAutomation_VideoPipeline_FrameRateEstimator_H

#include <chrono>
#include "Common/Cpp/Time.h"
#include "CommonFramework/VideoPipeline/VideoFormats.h"

namespace PokemonAutomation{


//  Measures a stream's frame rate by counting frames. Needed for containers that
//  carry no timing of their own, where the metadata frame rate comes back as NaN.
//  Counting stops at whichever limit comes first: the frame count settles a fast
//  stream quickly, the time limit stops a slow one from measuring forever.
class FrameRateEstimator{
public:
    FrameRateEstimator(
        std::chrono::milliseconds window = std::chrono::milliseconds(2000),
        size_t frames = 60
    )
        : m_window(window)
        , m_target_frames(frames)
    {}

    bool done() const{ return m_done; }
    FramesPerSecond fps() const{ return m_fps; }

    void reset(){
        m_start = WallClock::min();
        m_frames = 0;
        m_done = false;
        m_fps = 0;
    }

    //  Fraction complete, for a progress bar. Reports whichever limit is closer.
    double progress(WallClock now) const{
        if (m_done){
            return 1.0;
        }
        if (m_start == WallClock::min()){
            return 0.0;
        }
        double by_frames = m_target_frames == 0
            ? 0.0
            : (double)m_frames / (double)m_target_frames;
        double by_time = m_window.count() <= 0
            ? 0.0
            : (double)std::chrono::duration_cast<std::chrono::milliseconds>(now - m_start).count()
                / (double)m_window.count();
        double progress = by_frames > by_time ? by_frames : by_time;
        return progress > 1.0 ? 1.0 : progress;
    }

    //  Returns true on the call that finishes the measurement.
    bool push_frame(WallClock now){
        if (m_done){
            return false;
        }
        if (m_start == WallClock::min()){
            //  Start on the first frame so the connection handshake doesn't
            //  drag the average down.
            m_start = now;
            m_frames = 0;
            return false;
        }

        m_frames++;

        std::chrono::milliseconds elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - m_start);
        if (elapsed < m_window && m_frames < m_target_frames){
            return false;
        }

        m_done = true;
        if (elapsed.count() <= 0 || m_frames == 0){
            return true;
        }

        //  Measured rates land just under nominal (59.8 for 60 fps) and the
        //  format boxes all show whole numbers.
        double measured = (double)m_frames * 1000. / (double)elapsed.count();
        m_fps = (FramesPerSecond)(measured + 0.5);
        return true;
    }


private:
    const std::chrono::milliseconds m_window;
    const size_t m_target_frames;

    WallClock m_start = WallClock::min();
    size_t m_frames = 0;
    bool m_done = false;
    FramesPerSecond m_fps = 0;
};




}
#endif
