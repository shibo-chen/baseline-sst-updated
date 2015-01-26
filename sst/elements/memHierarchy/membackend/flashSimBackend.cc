
#include <sst_config.h>
#include "membackend/flashSimBackend.h"

using namespace SST;
using namespace SST::MemHierarchy;

FlashDIMMSimMemory::FlashDIMMSimMemory(Component *comp, Params &params) : MemBackend(comp, params){
    std::string deviceIniFilename = params.find_string("device_ini", NO_STRING_DEFINED);
    if(NO_STRING_DEFINED == deviceIniFilename)
        _abort(MemController, "Model must define a 'device_ini' file parameter\n");
    std::string systemIniFilename = params.find_string("system_ini", NO_STRING_DEFINED);
    if(NO_STRING_DEFINED == systemIniFilename)
        _abort(MemController, "Model must define a 'system_ini' file parameter\n");

    maxPendingRequests = (uint32_t) params.find_integer("max_pending_reqs", 32);
    pendingRequests = 0;

    // Create the acutal memory system
    memSystem = new FDSim::FlashDIMM(1, deviceIniFilename, systemIniFilename, "", "");

    FDSim::Callback_t* readDataCB  = new FDSim::Callback<FlashDIMMSimMemory, void, uint, uint64_t, uint64_t>(this, &FlashDIMMSimMemory::FlashDIMMSimDone);
    FDSim::Callback_t* writeDataCB = new FDSim::Callback<FlashDIMMSimMemory, void, uint, uint64_t, uint64_t>(this, &FlashDIMMSimMemory::FlashDIMMSimDone);

    memSystem->RegisterCallbacks(readDataCB, writeDataCB);

    output->verbose(CALL_INFO, 2, 0, "Flash DIMM Backend Initialization complete.\n");
}

bool FlashDIMMSimMemory::issueRequest(MemController::DRAMReq *req){
    // If we are up to the max pending requests, tell controller
    // we will not accept this transaction
    if(pendingRequests == maxPendingRequests) {
	return false;
    }

    uint64_t addr = req->baseAddr_ + req->amtInProcess_;
    bool ok = memSystem->addTransaction(req->isWrite_, addr);

    if(!ok) {
	return false;  // This *SHOULD* always be OK
    }

    output->verbose(CALL_INFO, 4, 0, "Backend issuing transaction for address %" PRIx64 "\n", (Addr) addr);
    dramReqs[addr].push_back(req);

    pendingRequests++;

    return true;
}

void FlashDIMMSimMemory::clock() {
    memSystem->update();
}

void FlashDIMMSimMemory::finish() {
    memSystem->printStats();
}

void FlashDIMMSimMemory::FlashDIMMSimDone(unsigned int id, uint64_t addr, uint64_t clockcycle){
    std::deque<MemController::DRAMReq *> &reqs = dramReqs[addr];

    output->verbose(CALL_INFO, 4, 0, "Backend retiring request for address %" PRIx64 ", Reqs: %" PRIu64 "\n",
		(Addr) addr, (uint64_t) reqs.size());

    assert(reqs.size());
    MemController::DRAMReq *req = reqs.front();
    reqs.pop_front();
    if(0 == reqs.size())
        dramReqs.erase(addr);

    ctrl->handleMemResponse(req);
    pendingRequests--;
}