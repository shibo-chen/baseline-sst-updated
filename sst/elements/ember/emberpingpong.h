
#ifndef _H_EMBER_PING_PONG
#define _H_EMBER_PING_PONG

#include <sst/core/params.h>
#include "embergen.h"

namespace SST {
namespace Ember {

class EmberPingPongGenerator : public EmberGenerator {

public:
	EmberPingPongGenerator(SST::Component* owner, Params& params);
        void generate(const SST::Output* output, const uint32_t phase, const uint32_t rank, std::queue<EmberEvent*>* evQ);

private:
	uint32_t messageSize;
	uint32_t iterations;

};

}
}

#endif
