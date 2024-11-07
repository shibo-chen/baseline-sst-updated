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

#include <sst_config.h>
#include <cstdlib>
#include <iostream>
#include "tree.h"

#include <algorithm>
#include <stdlib.h>
#include <cmath>



using namespace SST::Merlin;

bool topo_tree::map_initialized = false;

int div_ceil(int numerator, int denominator)
{
        std::div_t res = std::div(numerator, denominator);
        return res.rem ? (res.quot + 1) : res.quot;
}

topo_tree::topo_tree(ComponentId_t cid, Params& params, int num_ports, int rtr_id, int vns) :
    Topology(cid),
    router_id(rtr_id),
		num_vns(vns)
	{
		std::clog << "Okay initializing tree"<<std::endl;
    // Get the various parameters
    std::string shape;
    shape = params.find<std::string>("shape");
		std::clog << "reading params"<<std::endl;

    // Need to parse the shape string to get the number of shape
    // and the size of each dimension
    num_levels = std::count(shape.begin(),shape.end(),'x') + 1;
    num_total_hosts = params.find<int>("total_hosts", 1);
		num_routers = 0;
    parseDimString(shape);
		std::clog << "parsing params"<<std::endl;
		std::clog << shape <<std::endl;

		std::clog << "calcualted shape"<<std::endl;
		std::clog <<"reshaping" <<num_routers<<std::endl;

		dest_to_port_map.resize(num_routers);
	
		std::clog << "complete calculating shape"<<std::endl;

		for(int router_id_tmp = 0; router_id_tmp < num_routers; router_id_tmp++){
			int cumulated_rtrs = 0;
			int level = 0;
			for(int i = 0; i < num_levels; i++){
				cumulated_rtrs += num_rtrs_each_level[i];
				if(router_id_tmp < cumulated_rtrs){
					level = i;
					break;
				}
			}
		std::clog << "Okay generating shape"<<std::endl;

			if(level == 0){
				for(int i = 0; i < upward_ports_each_level[0]; i++){
					dest_to_port_map[router_id_tmp][upward_ports_each_level[0]*router_id_tmp+i] = i;
				}
				dest_to_port_map[router_id_tmp][num_total_hosts] = upward_ports_each_level[0];
			}else{
				int rtr_prev_lvl_start_id = 0;
				int rtr_crnt_lvl_start_id = 0;
				for(int i = 0; i < (level-1); i++){
					rtr_prev_lvl_start_id += num_rtrs_each_level[i];
				}
				rtr_crnt_lvl_start_id = rtr_prev_lvl_start_id + num_rtrs_each_level[level-1];
				for(int i = 0; i < upward_ports_each_level[level]; i++){ // iterate thru all upwards ports
					// first figure out which router it is connected to
					int connected_router_id = rtr_prev_lvl_start_id + (router_id_tmp - rtr_crnt_lvl_start_id)*upward_ports_each_level[level] + i;
						for(auto j = dest_to_port_map[connected_router_id].begin(); j != dest_to_port_map[connected_router_id].end(); j++){
							// if the upwards router can reach it, it can reach through this port
							std::clog <<"router id: "<<router_id_tmp << " port: " << i <<" conneceted router: "<<connected_router_id <<std::endl;
							dest_to_port_map[router_id_tmp][j->first] = i;
						}
				}
				dest_to_port_map[router_id_tmp][num_total_hosts] = upward_ports_each_level[level];
			}
		}

			
		std::clog << "Okay finished initiatizing tree"<<std::endl;

	}


topo_tree::~topo_tree()
{
    delete [] upward_ports_each_level;
		delete [] num_rtrs_each_level;
}

void
topo_tree::route_packet(int port, int vc, internal_router_event* ev)
{
    int dest_router = get_dest_router(ev->getDest());
    if ( dest_router == router_id ) {
        ev->setNextPort(get_dest_local_port(ev->getDest()));
    } else {
        topo_tree_event *tt_ev = static_cast<topo_tree_event*>(ev);

       	ev->setNextPort(dest_to_port_map[router_id][ev->getDest()]);
    }
		return;
}



internal_router_event*
topo_tree::process_input(RtrEvent* ev)
{
    topo_tree_event* tt_ev = new topo_tree_event();
    tt_ev->setEncapsulatedEvent(ev);
    tt_ev->setVC(tt_ev->getVN() * 2);
    
    // Need to figure out what the torus address is for easier
    // routing.
    int run_id = get_dest_router(tt_ev->getDest());
    tt_ev->router_dest = run_id;

	return tt_ev;
}


void topo_tree::routeUntimedData(int port, internal_router_event* ev, std::vector<int> &outPorts)
{
    if ( ev->getDest() == UNTIMED_BROADCAST_ADDR ) {
        /* For broadcast, use dest_loc as src_loc */
        topo_tree_event *tt_ev = static_cast<topo_tree_event*>(ev);
				std::clog << "router_dest: "<<tt_ev->router_dest <<" router_id: "<<router_id<<" num_local_ports: "<<num_local_ports<<std::endl;
				if(port == num_local_ports){
					for(int i =0; i< num_local_ports; i++){
						outPorts.push_back(i);
					}
				}else{
						outPorts.push_back(num_local_ports);
				}


    } else {
        route_packet(port, 0, ev);
        outPorts.push_back(ev->getNextPort());
				std::clog << "router_dest targted: "<<ev->getNextPort()<<std::endl;

    }
}


internal_router_event* topo_tree::process_UntimedData_input(RtrEvent* ev)
{
    topo_tree_event* tt_ev = new topo_tree_event();
    tt_ev->setEncapsulatedEvent(ev);
    if ( tt_ev->getDest() == UNTIMED_BROADCAST_ADDR ) {
        /* For broadcast, use dest_loc as src_loc */
        tt_ev->router_dest = router_id;
    } else {
        int rtr_id = get_dest_router(tt_ev->getDest());
				tt_ev->router_dest = rtr_id;
    }
    return tt_ev;
}


Topology::PortState
topo_tree::getPortState(int port) const
{
		// std::clog << "router_id:" << router_id << std::endl;
		if(num_levels == 1){
			return R2N;
		}

		if(router_id < num_rtrs_each_level[0]){
			if(port < upward_ports_each_level[0]){
				// std::clog <<"r2n--port:"<<port<<" upward_ports:"<<upward_ports_each_level[0]<<std::endl;
				return R2N;
			}else{
				// std::clog <<"r2r--port:"<<port<<" upward_ports:"<<upward_ports_each_level[0]<<std::endl;
				return R2R;
			}
		}else if(router_id == (num_routers -1)){
			if(port < upward_ports_each_level[num_levels-1]){
				// std::clog <<"r2r--port:"<<port<<" upward_ports:"<<upward_ports_each_level[num_levels-1]<<std::endl;
				return R2R;
			}
				// std::clog <<"r2n--port:"<<port<<" upward_ports:"<<upward_ports_each_level[num_levels-1]<<std::endl;
			return R2N;
		}else{
				// std::clog <<"r2r--port:"<<port<<" defaulted"<<upward_ports_each_level[0]<<std::endl;
			return R2R;
		}
}


void
topo_tree::idToLocation(int run_id, int *location) const
{
	//deprecated DO NO USE

	// for ( int i = dimensions - 1; i > 0; i-- ) {
	// 	int div = 1;
	// 	for ( int j = 0; j < i; j++ ) {
	// 		div *= dim_size[j];
	// 	}
	// 	int value = (run_id / div);
	// 	location[i] = value;
	// 	run_id -= (value * div);
	// }
	// location[0] = run_id;
}

void
topo_tree::parseDimString(const std::string &shape)
{
    size_t start = 0;
    size_t end = 0;
		std::clog << "num levels:"<< num_levels<<std::endl;
		upward_ports_each_level = new int[num_levels];
		num_local_ports = -1;
		num_rtrs_each_level = new int[num_levels];
			std::clog << "total host: "<< num_total_hosts<<std::endl;
    for ( int i = 0; i < num_levels; i++ ) {
			end = shape.find('x',start);
			size_t length = end - start;
			std::string sub = shape.substr(start,length);
			upward_ports_each_level[i] = strtol(sub.c_str(), NULL, 0);
			start = end + 1;
			std::clog << "level: "<<i<<" upward_ports_each_level:"<< upward_ports_each_level[i]<<std::endl;

			if(i == 0){
				num_rtrs_each_level[i] = div_ceil(num_total_hosts,upward_ports_each_level[i]);
			}else{
				num_rtrs_each_level[i] = div_ceil(num_rtrs_each_level[i-1],upward_ports_each_level[i]);
			}
			std::clog << "level: "<<i<<" num_rtrs_each_level:"<< num_rtrs_each_level[i]<<std::endl;
			num_routers += num_rtrs_each_level[i];
			if( (num_local_ports == -1) && (router_id < num_routers)){
				num_local_ports = upward_ports_each_level[i];
			}
    }
		std::clog << "end of param string num_routers: "<< num_routers<<std::endl;
}


int
topo_tree::get_dest_router(int dest_id) const
{
		if(dest_id >= upward_ports_each_level[0]*num_rtrs_each_level[0]){
			return num_routers-1;
		}
    else{
			return dest_id / upward_ports_each_level[0];
		}
}

int
topo_tree::get_dest_local_port(int dest_id) const
{
		if(dest_id >= upward_ports_each_level[0]*num_rtrs_each_level[0]){
			return num_routers-1;
		}
    else{
    	return dest_id% upward_ports_each_level[0];
		}
}


int
topo_tree::choose_multipath(int start_port, int num_ports, int dest_dist)
{
    // if ( num_ports == 1 ) {
    //     return start_port;
    // } else {
    //     return start_port + (dest_dist % num_ports);
    // }
		//Deprecated for this one. DO NOT USE!
		return -1;
}

int
topo_tree::getEndpointID(int port)
{
    if ( !isHostPort(port) ) return -1;
    else if(router_id >= num_rtrs_each_level[0]){
			return num_total_hosts;
		}else{
			return upward_ports_each_level[0]*router_id+port;
		}
}


