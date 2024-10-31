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
        self._declareClassVariables(["link_latency","host_link_latency", "network_name"])
        self._declareParams("main",["total_hosts"], ["shape"])
        self._setCallBackOnWrite("shape",self._shape_callback)
        self._setCallBackOnWrite("total_hosts",self._total_hosts_callback)
        self._setCallBackOnWrite("network_name",self._network_name_callback)
        self._setCallBackOnWrite("host_names",self._host_names_callback)
        self._subscribeToPlatformParamSet("topology")
        self.hostLinks = []
        self.memLink = []
        self.built = False
        self.interRouterLinks = []
        self.num_total_routers = 0
        self.routers = []
        self.num_uplinks_per_router = 0
        self.num_rtrs_each_level = []
        self.network_name = ""
        self.links = dict()
        self.memLinkName = ""
        self.hostLinkNames = []
        self.host_names = []


    def _shape_callback(self,variable_name,value):
        self._lockVariable(variable_name)
        if not self._areVariablesLocked([variable_name]):
            return
        
        self.shape = value

        self.num_uplinks_per_router = [int(x) for x in self.shape.split('x')]


    def _total_hosts_callback(self,variable_name,value):
        self._lockVariable(variable_name)
        if not self._areVariablesLocked([variable_name]):
            return
        self.total_hosts = value

    def _network_name_callback(self,variable_name,value):
        self._lockVariable(variable_name)
        if not self._areVariablesLocked([variable_name]):
            return
        self.network_name = value

    def _host_names_callback(self,variable_name,value):
        self._lockVariable(variable_name)
        if not self._areVariablesLocked([variable_name]):
            return
        self.host_names = value

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
        
    def getLink(self, leftName, rightName):
        name = self.getLinkName(leftName, rightName)
        if name not in self.links:
            self.links[name] = sst.Link(name)
        return self.links[name]
            
    def getHostLinks(self):
        return self.hostLinks
    
    def getHostLinkNames(self, host_name, router_name):
        return self.hostLinkNames
    
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
        if self.host_link_latency is None:
            self.host_link_latency = sst.merlin._params["link_lat"]

        self.num_total_routers = self.total_hosts
        self.num_rtrs_each_level.append(self.num_total_routers)
        start_idx_of_each_lvl = []
        start_idx_of_each_lvl.append(0)
        downward_links = {}
        upward_links = {}
        for i in range(1,len(self.num_uplinks_per_router)):
            self.num_rtrs_each_level.append(self.num_rtrs_each_level[i-1] / self.num_uplinks_per_router[i])
            start_idx_of_each_lvl.append(self.num_total_routers)
            self.num_total_routers += self.num_rtrs_each_level[i]
            for j in range(0,self.num_rtrs_each_level[i]):
                upward_links[start_idx_of_each_lvl[i]+j] = []
                for k in range(0,self.num_uplinks_per_router[i]):
                    upward_links[start_idx_of_each_lvl[i]+j].append(start_idx_of_each_lvl[i-1] + k + (j*self.num_uplinks_per_router[i]))
                    downward_links[start_idx_of_each_lvl[i-1] + k + (j*self.num_uplinks_per_router[i])] = start_idx_of_each_lvl[i]+j

        for i in range(0, self.num_total_routers):
            rtr = self._instanceRouter(self.num_uplinks_per_router[self.determineLevel(i)],i)
            topo = rtr.setSubComponent(self.router.getTopologySlotName(),"merlin.tree",0)
            self._applyStatisticsSettings(topo)
            topo.addParams(self._getGroupParams("main"))
            self.routers.append(rtr)
            if i == self.num_total_routers: # this is the one that links to the mem
                for j in range(0, self.num_uplinks_per_router[-1]):
                    rtr.addLink(self.getLink(self.getLink(self.getRouterNameForId(upward_links[i][j]), self.getRouterNameForId(i))),\
                                    "port%d"%j, sst.merlin._params["link_lat"])
                self.memLinkName = self.getMemLinkname(self.getRouterNameForId(i))
                self.memLink.append(self.getLink(self.memLinkName))
                rtr.addLink(self.getLink(self.memLink),"port%d"%(self.num_uplinks_per_router), sst.merlin._params["link_lat"])
            # this is the first level
            elif self.determineLevel(i) == 0:
                for j in range(0,self.num_uplinks_per_router[0]):
                    if j < self.num_uplinks_per_router[0]:
                        rtr.addLink(self.getLink(self.getHostLinkName(self.host_names[i*self.num_uplinks_per_router[0]+j], self.getRouterNameForId())),\
                                     "port%d"%j, sst.merlin._params["link_lat"])
                        self.hostLinks.append(self.getLink(self.host_names[i*self.num_uplinks_per_router[0]+j], self.getRouterNameForId()))
                        self.hostLinkNames.append(self.getLinkName(self.host_names[i*self.num_uplinks_per_router[0]+j], self.getRouterNameForId()) )
                    else:
                        rtr.addLink(self.getLink(self.getRouterNameForId(i), self.getRouterNameForId(downward_links[i])),\
                                     "port%d"%j, sst.merlin._params["link_lat"])
            else:
                for j in range(0, self.num_uplinks_per_router[self.determineLevel(i)]+1):
                    if j < self.num_uplinks_per_router[self.determineLevel(i)]:
                        rtr.addLink(self.getLink(self.getLink(self.getRouterNameForId(upward_links[i][j]), self.getRouterNameForId(i))),\
                                    "port%d"%j, sst.merlin._params["link_lat"])
                    else:
                        rtr.addLink(self.getLink(self.getLink(self.getRouterNameForId(i), self.getRouterNameForId(downward_links[i]))),\
                                    "port%d"%j, sst.merlin._params["link_lat"])
                    
