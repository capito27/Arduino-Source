/*  FFmpeg Logging
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_VideoPipeline_FFmpegLogging_H
#define PokemonAutomation_VideoPipeline_FFmpegLogging_H

namespace PokemonAutomation{


//  Drop FFmpeg's own log level to errors only. Decoding MJPEG makes libswscale
//  warn once per frame straight to stderr, bypassing Qt's categorized logging
//  where QT_LOGGING_RULES could have silenced it.
//  Safe to call repeatedly. Does nothing if avutil can't be reached.
void suppress_ffmpeg_log_spam();



}
#endif
