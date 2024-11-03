// -*- mode: c++ -*-

// Copyright 2009-2024 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2024, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// of the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.


#ifndef COMPONENTS_MERLIN_TOPOLOGY_TORUS_H
#define COMPONENTS_MERLIN_TOPOLOGY_TORUS_H

#include <sst/core/event.h>
#include <sst/core/link.h>
#include <sst/core/params.h>

#include <string.h>

#include "sst/elements/merlin/router.h"

namespace SST {
namespace Merlin {

class topo_tree_event : public internal_router_event {
public:
    int router_dest;

    topo_tree_event() {}
    ~topo_tree_event() { }
    virtual internal_router_event* clone(void) override
    {
        topo_tree_event* tte = new topo_tree_event(*this);


        return tte;
    }

    void serialize_order(SST::Core::Serialization::serializer &ser)  override {
        internal_router_event::serialize_order(ser);
        ser & router_dest;
		}


private:

    ImplementSerializable(SST::Merlin::topo_tree_event)
};



class topo_tree: public Topology {

public:
    static bool map_initialized;

    SST_ELI_REGISTER_SUBCOMPONENT(
        topo_tree,
        "merlin",
        "tree",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Tree topology object",
        SST::Merlin::Topology
    )

    SST_ELI_DOCUMENT_PARAMS(
        // Parameters needed for use with old merlin python module
        {"tree.total_hosts", "Total number of host "},
        {"tree.shape", "Number of endpoints attached to top-level router. In shape NxNxNxN, each N represent the number of upstream hosts/ports"},

        {"total_hosts", "Total number of host "},
        {"shape", "Number of endpoints attached to top-level router. In shape NxNxNxN, each N represent the number of upstream hosts/ports"},
    )
/*
0   1   2  3 (8 2x2x2) 
  4    5
    6
*/

/*

*/

private:
    std::vector<std::unordered_map<int, int>> dest_to_port_map;
    int router_id;

    int tree_size;
	int num_levels;
	int num_routers;
	int num_total_hosts;
    int* upward_ports_each_level; // how many upstream each layer
	int* num_rtrs_each_level; // how many routers each layer


    int num_local_ports; 

    int num_vns;
    
public:
    topo_tree(ComponentId_t cid, Params& params, int num_ports, int rtr_id, int vns);
    ~topo_tree();

    virtual void route_packet(int port, int vc, internal_router_event* ev);
    virtual internal_router_event* process_input(RtrEvent* ev);

    virtual void routeUntimedData(int port, internal_router_event* ev, std::vector<int> &outPorts);
    virtual internal_router_event* process_UntimedData_input(RtrEvent* ev);

    virtual PortState getPortState(int port) const;
    virtual int getEndpointID(int port);

    virtual void getVCsPerVN(std::vector<int>& vcs_per_vn) {
        for ( int i = 0; i < num_vns; ++i ) {
            vcs_per_vn[i] = 2;
        }
    }
    
protected:
    virtual int choose_multipath(int start_port, int num_ports, int dest_dist);

private:
    void idToLocation(int id, int *location) const;
    void parseDimString(const std::string &shape) ;
    int get_dest_router(int dest_id) const;
    int get_dest_local_port(int dest_id) const;

};

}
}

#endif // COMPONENTS_MERLIN_TOPOLOGY_TORUS_H
