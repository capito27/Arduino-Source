/*  Program Descriptor
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_CommonFramework_ProgramDescriptor_H
#define PokemonAutomation_CommonFramework_ProgramDescriptor_H

#include "PanelDescriptor.h"

namespace PokemonAutomation{

class StatsTracker;


enum class ProgramControllerClass{
    StandardController_NoRestrictions,              //  Blue
    StandardController_PerformanceClassSensitive,   //  Green
    StandardController_RequiresPrecision,           //  Purple
    StandardController_WithRestrictions,            //  Red
    SpecializedController,                          //  Pink
};
enum class AllowCommandsWhenRunning{
    DISABLE_COMMANDS,
    ENABLE_COMMANDS,
};


class ProgramDescriptor : public PanelDescriptor{
public:
    using PanelDescriptor::PanelDescriptor;

    virtual std::unique_ptr<StatsTracker> make_stats() const;

    //  Whether the stats from each run are added to a running total kept on
    //  disk. Programs that measure something rather than accumulate it have
    //  nothing meaningful to total up across runs, so they can turn this off and
    //  show only the run in progress.
    virtual bool persistent_stats() const{ return true; }
};







}
#endif
