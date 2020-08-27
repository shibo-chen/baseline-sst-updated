
#ifndef _H_VANADIS_NODE_OS
#define _H_VANADIS_NODE_OS

#include <sst/core/component.h>
#include <sst/core/interfaces/simpleMem.h>

using namespace SST::Interfaces;

namespace SST {
namespace Vanadis {

class VanadisNodeOSComponent : public SST::Component {

public:
	SST_ELI_REGISTER_COMPONENT(
        	VanadisNodeOSComponent,
       	 	"vanadis",
	        "VanadisNodeOS",
        	SST_ELI_ELEMENT_VERSION(1,0,0),
	        "Vanadis Generic Operating System Component",
        	COMPONENT_CATEGORY_PROCESSOR
    	)

	SST_ELI_DOCUMENT_PARAMS(
		{ "verbose", 		"Set the output verbosity, 0 is no output, higher is more." },
		{ "cores",		"Number of cores that can request OS services via a link."  }
	)

	SST_ELI_DOCUMENT_PORTS(
		{ "core%(cores)",	"Connects to a CPU core",	{} }
	)

	VanadisNodeOSComponent( SST::ComponentId_t id, SST::Params& params );
	~VanadisNodeOSComponent();

	virtual void init( unsigned int phase );
	void handleIncomingSysCall();
	void handleIncomingMemory( SimpleMem::Request* ev );

private:
	VanadisNodeOSComponent();  // for serialization only
    	VanadisNodeOSComponent(const VanadisNodeOSComponent&); // do not implement
    	void operator=(const VanadisNodeOSComponent&); // do not implement

	std::vector< SST::Link* > core_links;
	SimpleMem* memDataInterface;
	SST::Output* output;

};

}
}

#endif
