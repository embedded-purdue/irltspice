#include <cstdint>

#include <irl/command.hpp>

#include "command.pb.h"

using google::protobuf::FieldDescriptor;

#ifdef TRANSLATE_PROTO_TRACK_FIELDS
#include <set>
// Used by testing code to ensure we handle each field in the pack/unpack functions
extern std::set<const FieldDescriptor *> handled_fields;
#endif

using ProgrammingCommandPB = ::ProgrammingCommand;
using MotherCommandPB = ::MotherCommand;
using ProgramXbarPB = ::ProgramXbar;
using ProgramSwitchPB = ::ProgramSwitch;
using MotherboardTopologyPB = ::MotherboardTopology;
using DaughterboardTopologyPB = ::DaughterboardTopology;
using TopologyResponsePB = ::TopologyResponse;

// Translate a programming command to protobuf.
// Only meaningful for the following command types (which have a direct protobuf analog):
//  - ProgramXbar
//  - ProgramPhysicalSwitches
void initProgrammingCmd(MotherCommand *out, irl::ProgrammingCommand const &pcmd);

void initStopProgrammingCmd(MotherCommand *out);

void initTopoRequestCmd(MotherCommand *out);

irl::IrlTopology unpackTopology(TopologyResponsePB const &topo);
