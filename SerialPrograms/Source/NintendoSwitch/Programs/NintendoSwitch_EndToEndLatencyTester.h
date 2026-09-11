/*  End-to-End Latency Tester
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_NintendoSwitch_EndToEndLatencyTester_H
#define PokemonAutomation_NintendoSwitch_EndToEndLatencyTester_H

#include "Common/Cpp/Options/SimpleIntegerOption.h"
#include "Common/Cpp/Options/TimeDurationOption.h"
#include "NintendoSwitch/NintendoSwitch_SingleSwitchProgram.h"

namespace PokemonAutomation{
namespace NintendoSwitch{


class EndToEndLatencyTester_Descriptor : public SingleSwitchProgramDescriptor{
public:
    EndToEndLatencyTester_Descriptor();

    struct Stats;
    virtual std::unique_ptr<StatsTracker> make_stats() const override;

    //  Each run measures the setup it was run against. Totalling those across
    //  runs would mix measurements of different setups, which is the opposite of
    //  what this program is for.
    virtual bool persistent_stats() const override{ return false; }
};


//  Measures how long it takes for a controller input to be acknowledged, and
//  then to come back as video and as audio. Nothing here is specific to any one
//  kind of source, so a capture card and a network stream can be compared by
//  running it against each.
//
//  Each trial presses HOME to put the cursor on the leftmost game, then presses
//  DPAD-down to move it onto the first icon of the bottom row. That makes the
//  console print a label under the icon row and play a click, which are the two
//  things we time.
//
//  The console's own input-to-display latency is included in these numbers and
//  cannot be separated from here. The difference between the video and audio
//  times is the more useful figure: the console-side latency is common to both
//  and cancels out of it.
class EndToEndLatencyTester : public SingleSwitchProgramInstance{
public:
    EndToEndLatencyTester();

    virtual void program(SingleSwitchProgramEnvironment& env, CancellableScope& scope) override;


private:
    //  Waits for the label region to stop changing after the reset and returns
    //  how speckled it is once settled. That reading is the baseline the video
    //  detector measures against, so nothing has to be calibrated by hand.
    //  Returns -1 if it never settles.
    double measure_label_baseline(SingleSwitchProgramEnvironment& env, CancellableScope& scope);


private:
    SimpleIntegerOption<uint16_t> TRIALS;
    MillisecondsOption SETTLE_DELAY;
    MillisecondsOption LISTEN_WINDOW;
};



}
}
#endif
