#!/usr/bin/env python
#
# Copyright 2009-2024 NTESS. Under the terms
# of Contract DE-NA0003525 with NTESS, the U.S.
# Government retains certain rights in this software.
#
# Copyright (c) 2009-2024, NTESS
# All rights reserved.
#
# Portions are copyright of other developers:
# See the file CONTRIBUTORS.TXT in the top level directory
# of the distribution for more information.
#
# This file is part of the SST software package. For license
# information, see the LICENSE file in the top level directory of the
# distribution.

import sst
from sst.merlin.base import *


class topoTree(Topology):

    def __init__(self):
        Topology.__init__(self)
        self._declareClassVariables(["link_latency"])
        self._declareParams("main",["total_hosts","shape", "hostLinks", "built", "memLink", "routers",\
                                     "num_total_routers", "routers", "num_uplinks_per_router",\
                                  "interRouterLinks","num_rtrs_each_level", "network_name", "links",\
                                     "memLinkName", "hostLinkNames", "host_names"])
        self._subscribeToPlatformParamSet("topology")
        self.link_latency = "20ns"
        self.network_name = ""
        self.hostLinks = []
        self.memLink = []
        self.built = False
        self.interRouterLinks = []
        self.num_total_routers = 0
        self.routers = []
        self.num_uplinks_per_router = 0
        self.num_rtrs_each_level = []
        self.links = dict()
        self.memLinkName = ""
        self.hostLinkNames = []
        self.host_names = []



    def setHostNames(self, host_names):
        self.host_names = host_names

    def _getTopologyName():
        return "merlin.tree" 

    def getName(self):
        return self.network_name

    def getNumNodes(self):
        return self.total_hosts+1

    def getNumRouters(self):
        return self.total_num_routers
    
    def getNumHosts(self):
        return self.total_hosts

    def getRouterNameForId(self,rtr_id):
        return "rtr_%d"%(rtr_id)
    
    def findRouterById(self, rtr_id):
        return sst.findComponentByName(self.getRouterNameForId(rtr_id))
    
    def getLinkName(self, leftName, rightName):
        return "link_%s_%s"%(leftName, rightName)
        
    def getLink(self,link_name):
        if link_name not in self.links:
            self.links[link_name] = sst.Link(link_name)
        return self.links[link_name]
            
    def getHostLinks(self):
        return self.hostLinks
    
    def getHostLinkNames(self):
        return self.hostLinkNames
    
    def composeHostLinkName(self, host_name, router_name):
        return "link_%s_%s"%(host_name, router_name)
    
    def getMemLink(self):
        return self.memLink
    
    def getMemLinkName(self, router_name):
        return "link_%s_mem"%(router_name)

    def determineLevel(self, idx):
        cumulated_rtrs = 0
        for i in range(0,len(self.num_rtrs_each_level)):
            cumulated_rtrs += self.num_rtrs_each_level[i]
            if idx < cumulated_rtrs:
                return i
        return -1

    def _build_impl(self, endpoint):
        sst.merlin._params["link_lat"] = self.link_latency
        self.num_uplinks_per_router = [int(x) for x in self.shape.split('x')]
        self.num_total_routers = int(self.total_hosts/self.num_uplinks_per_router[0])
        self.num_rtrs_each_level.append(int(self.total_hosts/self.num_uplinks_per_router[0]))
        start_idx_of_each_lvl = []
        start_idx_of_each_lvl.append(0)
        downward_links = {}
        upward_links = {}
        # print("Level %d: %d routers"%(0,self.num_rtrs_each_level[0]))
        for i in range(1, len(self.num_uplinks_per_router)):
            self.num_rtrs_each_level.append( int(self.num_rtrs_each_level[i-1] / self.num_uplinks_per_router[i]))
            # print("Level %d: %d routers"%(i,self.num_rtrs_each_level[i]))
            start_idx_of_each_lvl.append(self.num_total_routers)
            self.num_total_routers += self.num_rtrs_each_level[i]
            for j in range(0,self.num_rtrs_each_level[i]):
                upward_links[start_idx_of_each_lvl[i]+j] = []
                for k in range(0,self.num_uplinks_per_router[i]):
                    upward_links[start_idx_of_each_lvl[i]+j].append(start_idx_of_each_lvl[i-1] + k + (j*self.num_uplinks_per_router[i]))
                    downward_links[start_idx_of_each_lvl[i-1] + k + (j*self.num_uplinks_per_router[i])] = start_idx_of_each_lvl[i]+j

        # debug
        # for (k,v) in upward_links.items():
        #     print("Router %d: %s"%(k,upward_links[k]))

        # for (k,v) in downward_links.items():
        #     print("Router %d: %s"%(k,downward_links[k]))


        for i in range(0, self.num_total_routers):
            rtr = self._instanceRouter(self.num_uplinks_per_router[self.determineLevel(i)]+1,i)
            topo = rtr.setSubComponent(self.router.getTopologySlotName(),"merlin.tree",0)
            self._applyStatisticsSettings(topo)
            topo.addParams(self._getGroupParams("main"))
            rtr.addParams({
                "flit_size":"64b",
                "xbar_bw": "2GB/s",
                "num_ports": str(self.num_uplinks_per_router[self.determineLevel(i)]+1),
                "link_bw": "2GB/s",
                "output_buf_size" : "2KB",
                "topology": "merlin.tree",
                "output_latency" : "100ps",
                "input_buf_size" : "2KB",
                "input_latency" : "100ps"
            })
            self.routers.append(rtr)
            if i == (self.num_total_routers-1): # this is the one that links to the mem
                for j in range(0, self.num_uplinks_per_router[-1]+1):
                    if j < self.num_uplinks_per_router[-1]:
                        rtr.addLink(self.getLink(self.getLinkName(self.getRouterNameForId(upward_links[i][j]), self.getRouterNameForId(i))),\
                                    "port%d"%j, sst.merlin._params["link_lat"])
                        # print("rtr %d port %d connect to router %s with link %s"%(i,j, self.getRouterNameForId(upward_links[i][j]),self.getLinkName(self.getRouterNameForId(upward_links[i][j]), self.getRouterNameForId(i))))
                    else:
                        self.memLinkName = self.getMemLinkName(self.getRouterNameForId(i))
                        self.memLink.append(self.getLink(self.memLinkName))
                        rtr.addLink(self.getLink(self.memLinkName),"port%d"%(self.num_uplinks_per_router[-1]), sst.merlin._params["link_lat"])
                        # print("rtr %d port %d connect to mem with link %s"%(i,j, self.memLinkName))

            # this is the first level
            elif self.determineLevel(i) == 0:
                for j in range(0,self.num_uplinks_per_router[0]+1):
                    if j < self.num_uplinks_per_router[0]:
                        rtr.addLink(self.getLink(self.composeHostLinkName(self.host_names[i*self.num_uplinks_per_router[0]+j], self.getRouterNameForId(i))),\
                                     "port%d"%j, sst.merlin._params["link_lat"])
                        self.hostLinks.append(self.getLink(self.composeHostLinkName(self.host_names[i*self.num_uplinks_per_router[0]+j], self.getRouterNameForId(i))))
                        self.hostLinkNames.append(self.composeHostLinkName(self.host_names[i*self.num_uplinks_per_router[0]+j], self.getRouterNameForId(i)) )
                        # print("rtr %d port %d connect to host %s with link %s"%(i,j, self.host_names[i*self.num_uplinks_per_router[0]+j], self.hostLinkNames[-1]))
                    else:
                        rtr.addLink(self.getLink(self.getLinkName(self.getRouterNameForId(i), self.getRouterNameForId(downward_links[i]))),\
                                     "port%d"%j, sst.merlin._params["link_lat"])
                        # print("rtr %d port %d connect to rtr %s with link %s"%(i,j, self.getRouterNameForId(downward_links[i]),self.getLinkName(self.getRouterNameForId(i), self.getRouterNameForId(downward_links[i]))))

            else:
                for j in range(0, self.num_uplinks_per_router[self.determineLevel(i)]+1):
                    if j < self.num_uplinks_per_router[self.determineLevel(i)]:
                        rtr.addLink(self.getLink(self.getLinkName(self.getRouterNameForId(upward_links[i][j]), self.getRouterNameForId(i))),\
                                    "port%d"%j, sst.merlin._params["link_lat"])
                        print("rtr %d port %d connect to rtr %s with link %s"%(i,j,upward_links[i][j],self.getLinkName(self.getRouterNameForId(upward_links[i][j]), self.getRouterNameForId(i))))
                    else:
                        rtr.addLink(self.getLink(self.getLinkName(self.getRouterNameForId(i), self.getRouterNameForId(downward_links[i]))),\
                                    "port%d"%j, sst.merlin._params["link_lat"])
                        print("rtr %d port %d connect to rtr %s with link %s"%(i,j,downward_links[i],self.getLinkName(self.getRouterNameForId(i), self.getRouterNameForId(downward_links[i]))))

