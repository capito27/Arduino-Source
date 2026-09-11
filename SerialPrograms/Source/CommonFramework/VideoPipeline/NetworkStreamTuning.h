/*  Network Stream Tuning
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_VideoPipeline_NetworkStreamTuning_H
#define PokemonAutomation_VideoPipeline_NetworkStreamTuning_H

namespace PokemonAutomation{


//  Multipart MJPEG carries no timing, so FFmpeg's demuxer invents a 25 fps rate
//  and QMediaPlayer paces playback to it. Running the player faster than
//  real-time makes it pull frames as fast as they arrive instead. It then
//  starves and self-limits to the stream's true rate.
//  Measured on a 60 fps source: 1.0 -> 25 fps, 2.0 -> 50 fps, 4.0 -> 60 fps.
const double NETWORK_STREAM_PLAYBACK_RATE = 4.0;



}
#endif
