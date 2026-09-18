#include "resident_transport_3ds.h"
// Instantiate the complete template for ARM11 without running a lifecycle or
// taking ownership of the game's NDSP service. Native event tests follow.
template class ResidentDsp::Session<ResidentDsp::CtrTransport>;
