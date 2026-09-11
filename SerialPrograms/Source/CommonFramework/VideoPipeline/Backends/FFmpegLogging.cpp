/*  FFmpeg Logging
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include <mutex>
#include "CommonFramework/Logging/Logger.h"
#include "FFmpegLogging.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

//#include <iostream>
//using std::cout;
//using std::endl;

namespace PokemonAutomation{


//  From libavutil/log.h. We aren't linked against FFmpeg, so we reach
//  av_log_set_level() through the avutil that Qt already deploys next to us.
const int AV_LOG_LEVEL_ERROR = 16;

//  Qt 6.8 ships avutil 59. Other Qt versions won't, so try a few.
const char* const AVUTIL_LIBRARY_NAMES[] = {
#ifdef _WIN32
    "avutil-59.dll",
    "avutil-58.dll",
    "avutil-57.dll",
#elif defined(__APPLE__)
    "libavutil.59.dylib",
    "libavutil.58.dylib",
    "libavutil.57.dylib",
#else
    "libavutil.so.59",
    "libavutil.so.58",
    "libavutil.so.57",
#endif
};




namespace{

using av_log_set_level_t = void (*)(int);

void* open_library(const char* name){
#ifdef _WIN32
    return (void*)LoadLibraryA(name);
#else
    return dlopen(name, RTLD_LAZY);
#endif
}
av_log_set_level_t find_log_level_setter(void* library){
#ifdef _WIN32
    return (av_log_set_level_t)GetProcAddress((HMODULE)library, "av_log_set_level");
#else
    return (av_log_set_level_t)dlsym(library, "av_log_set_level");
#endif
}

void lower_log_level(){
    for (const char* name : AVUTIL_LIBRARY_NAMES){
        void* library = open_library(name);
        if (library == nullptr){
            continue;
        }
        av_log_set_level_t setter = find_log_level_setter(library);
        if (setter == nullptr){
            continue;
        }
        setter(AV_LOG_LEVEL_ERROR);
        global_logger_tagged().log("Suppressing FFmpeg decoder warnings via " + std::string(name) + ".");
        return;
    }
    global_logger_tagged().log(
        "Unable to reach avutil. FFmpeg decoder warnings will not be suppressed.",
        COLOR_ORANGE
    );
}

}




void suppress_ffmpeg_log_spam(){
    static std::once_flag flag;
    std::call_once(flag, lower_log_level);
}



}
