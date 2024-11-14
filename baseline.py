# Import the SST module
import sst
from sst.merlin.topology import *

# Define SST core options
sst.setProgramOption("timebase", "100ps")
# sst.setProgramOption("stop-at", "10ms")
sst.setProgramOption("exit-after", "20s")
sst.setProgramOption("debug-file", "debug_output.txt")

coherence_protocol = "MESI"
# Number of CPUs
num_cpus = 8
cache_size = "1KB"
cache_line_size = "64"
mem_size = 1024 * 1024 * 1024  # 1024 MiB total memory (in bytes)
memory_per_cpu = mem_size / num_cpus  # Each CPU gets 1/8 of total memory
os.environ["OMP_NUM_THREADS"]=str(num_cpus)
memory_clock = "1GHz"

# Define the latency parameters
cpu_cache_latency = "2ns"         # CPU ->  Cache
cache_router_latency = "5ns"     # Cache -> Router
router_router_latency = "1ns"     # Router <-> Router
router_directory_latency = "2ns"  # Router -> Directory Controller
directory_mem_latency = "2ns"     # Directory Controller -> Memory Controller  
mem_interleave_size = 4096  # Do 4K page level interleaving
memory_capacity = 16384     # Size of memory in MBs
# List of executables (benchmarks) for each core
# heavy: omnetpp, mcf, gcc, bzip2;   light: h264ref, gobmk, sjeng
executables = [
    "/sst/benchmarks/spec06/speccpu2006-overpass/benchspec/CPU2006/403.gcc/exe/gcc_base.amd64-m64-gcc42-nn",
    "/sst/benchmarks/spec06/speccpu2006-overpass/benchspec/CPU2006/403.gcc/exe/gcc_base.amd64-m64-gcc42-nn",
    "/sst/benchmarks/spec06/speccpu2006-overpass/benchspec/CPU2006/471.omnetpp/exe/omnetpp_base.amd64-m64-gcc42-nn",
    "/sst/benchmarks/spec06/speccpu2006-overpass/benchspec/CPU2006/471.omnetpp/exe/omnetpp_base.amd64-m64-gcc42-nn",
    "/sst/benchmarks/spec06/speccpu2006-overpass/benchspec/CPU2006/464.h264ref/exe/h264ref_base.amd64-m64-gcc42-nn",
    "/sst/benchmarks/spec06/speccpu2006-overpass/benchspec/CPU2006/464.h264ref/exe/h264ref_base.amd64-m64-gcc42-nn",
    "/sst/benchmarks/spec06/speccpu2006-overpass/benchspec/CPU2006/445.gobmk/exe/gobmk_base.amd64-m64-gcc42-nn",
    "/sst/benchmarks/spec06/speccpu2006-overpass/benchspec/CPU2006/445.gobmk/exe/gobmk_base.amd64-m64-gcc42-nn"
]

appargs = []

# executables = [
#     "./a.out",
#     "./a.out",
#     "./a.out",
#     "./a.out",
#     "./a.out",
#     "./a.out",
#     "./a.out",
#     "./a.out"
# ]


# Create the Ariel CPUs and connect them to L1 caches
cpus = []
caches = []
cache_names = []
# NICs = []
# NIC_names = []
for i in range(num_cpus):
    # Set the memory offset for each CPU based on its physical memory range
    cpu_offset = i * memory_per_cpu  # Offset for each CPU

    # Create Ariel CPU components
    cpu = sst.Component(f"cpu{i}", "ariel.ariel")
    cpu.addParams({
        "verbose": "0",
        "clock": "2GHz",
        "maxcorequeue": "128",
        "maxtranscore": "8",
        "maxissuepercycle": "2",
        "pipetimeout": "0",
        "mallocmapfile"       : "malloc.txt",
        "arielinterceptcalls" : "1",
        "executable": executables[i]  # Specify your executable here
## Application Info:
## Executable  -> exe_file
## appargcount -> Number of commandline arguments after <exec_file> name
## apparg<#>   -> arguments
## Commandline execution for the below example would be
## /home/amdeshp/arch/benchmarks/PathFinder_1.0.0/PathFinder_ref/PathFinder.x -x /home/amdeshp/arch/benchmarks/PathFinder_1.0.0/generatedData/small1.adj_list
## AppArgs = ({
##    "executable"  : "/home/amdeshp/arch/benchmarks/PathFinder_1.0.0/PathFinder_ref/PathFinder.x",
##    "appargcount" : "0",
##    "apparg0"     : "-x",
##    "apparg1"     : "/home/amdeshp/arch/benchmarks/PathFinder_1.0.0/generatedData/small1.adj_list",
## })

## Application Info Example
# os.environ['SIM_DESC'] = 'EIGHT_CORES'
# os.environ['OMP_NUM_THREADS'] = str(corecount)

# stream_app = os.getenv("ARIEL_TEST_STREAM_APP")
# if stream_app == None:
#     sst_root = os.getenv( "SST_ROOT" )
#     app = sst_root + "/sst-elements/src/sst/elements/ariel/frontend/simple/examples/stream/stream"
# else:
#     app = stream_app

# if not os.path.exists(app):
#     app = os.getenv( "OMP_EXE" )

    })
    # Create memory manager for ariel cpu offset, translating virtual address to physical address
    memory_manager = cpu.setSubComponent("memmgr", "ariel.MemoryManagerMalloc")
    memory_manager.addParams({
        "memorylevels" : "2",
        "defaultlevel" : 0,
        "pagecount0"   : "524288",
        "pagecount1"   : "524288"
    })

    cpus.append(cpu)

    # Create L1 caches for each CPU
    # stride, nbp, pala
    cache = sst.Component(f"l1cache{i}", "memHierarchy.Cache")
    cache.addParams({
        "verbose": 0,
        "access_latency_cycles": 4,
        "cache_frequency": "2GHz",
        "replacement_policy": "lru",
        "associativity": 8,
        "cache_line_size": cache_line_size,
        "cache_size": cache_size,
        "L1": 1,
        "coherence_protocol": coherence_protocol,
        "prefetcher": "cassini.StridePrefetcher",
        "reach": 16,
        "detect_range" : 1,
        "debug": 0,
        "debug_level": 10,
        "debug_addr": "[0x4600]"
    })
    
    # Link the CPU to its L1 cache using the correct cache_link port ('cache_link_0')
    link_cpu_cache = sst.Link(f"link_cpu{i}_cache")
    link_cpu_cache.connect((cpu, "cache_link_0", cpu_cache_latency), (cache, "high_network_0", cpu_cache_latency))

     # Add MemNIC to the L1 cache for network connectivity
    # cache_memNIC = cache.setSubComponent("memlink", "memHierarchy.MemNIC")
    # cache_memNIC.addParams({
    #     "verbose": 1,
    #     "debug": 1,
    #     "group": 1,  # Group ID for network hierarchy
    #     "network_bw": "80GiB/s",  # Bandwidth between cache and router
    #     "network_input_buffer_size": "1KiB",
    #     "network_output_buffer_size": "1KiB",
    # })

    # Add link control to the NIC for connecting to the router
    # cache_linkctrl = cache_memNIC.setSubComponent("linkcontrol", "merlin.linkcontrol")
    # cache_linkctrl.addParams({
    #     "link_bw": "2GiB/s",  # Bandwidth of the link
    #     "input_buf_size": "1KiB",  # Input buffer size
    #     "output_buf_size": "1KiB",  # Output buffer size
    # })
    
    caches.append(cache)
    cache_names.append(cache.getFullName())
    # NICs.append(cache_memNIC)
    # NIC_names.append(cache.getFullName())

# print(NIC_names)
topo = topoTree()
topo.shape = "2x4"
topo.total_hosts = 8
topo.host_names = cache_names
topo.link_latency = router_router_latency
sst.merlin._params["tree.shape"] = "2x4"
sst.merlin._params["tree.total_hosts"] = 8

sst.merlin._params["link_bw"] = "8GB/s"
sst.merlin._params["link_lat"] = router_router_latency
sst.merlin._params["flit_size"] = "8B"
sst.merlin._params["xbar_bw"] = "8GB/s"
sst.merlin._params["input_latency"] = "1ns"
sst.merlin._params["output_latency"] = "1ns"
sst.merlin._params["input_buf_size"] = "256B"
sst.merlin._params["output_buf_size"] = "256B"

#sst.merlin._params["checkerboard"] = "1"
sst.merlin._params["xbar_arb"] = "merlin.xbar_arb_lru"
topo.build(0)

# for i in range(0):
#     NIcs[i]
# # Create 4 Routers to connect pairs of caches
# L1_routers = []
# for i in range(4):
#     router = sst.Component(f"router{i}", "merlin.hr_router")
#     router.addParams({
#         "id": i,
#         "xbar_bw": "2GB/s",
#         "link_bw": "2GB/s",
#         "input_buf_size": "1KiB",
#         "output_buf_size": "1KiB",
#         "num_ports": 3,  # Each router will have 2 CPU connections and 1 uplink to the next level router
#         "flit_size": "72B",
#         "input_latency": "1ns",
#         "output_latency": "1ns",
#     })

#     # Set the round-robin arbiter for the crossbar in hr_router
#     arbiter = router.setSubComponent("XbarArb", "merlin.xbar_arb_rr")

#     # Set the topology using setSubComponent (removing it from addParams)
#     router_topology = router.setSubComponent("topology", "merlin.singlerouter")

#     L1_routers.append(router)

# # Create the top-level hr_router to connect the four lower-level routers
# top_router = sst.Component("top_router", "merlin.hr_router")
# top_router.addParams({
#     "id": 100,
#     "xbar_bw": "4GB/s",
#     "link_bw": "2GB/s",
#     "input_buf_size": "1KiB",
#     "output_buf_size": "1KiB",
#     "num_ports": 5,  # 4 lower-level routers + 1 connection to memory
#     "flit_size": "72B",
#     "input_latency": "1ns",
#     "output_latency": "1ns",
# })

# # Set the round-robin arbiter for the crossbar in the top-level hr_router
# top_arbiter = top_router.setSubComponent("XbarArb", "merlin.xbar_arb_rr")

# # Set the topology for the top-level router
# top_router_topology = top_router.setSubComponent("topology", "merlin.singlerouter")

# Create the DirectoryController component
directory = sst.Component("directory_controller", "memHierarchy.DirectoryController")
directory.addParams({
    "verbose": "0",
    "entry_cache_size": 0,  # No caching in the directory
    "debug": 1,  # Debug options, adjust as needed
    "debug_level": 0,  # Debug verbosity level
    "cache_line_size": cache_line_size,  # Cache line size
    "coherence_protocol": coherence_protocol,  # Coherence protocol
    "access_latency_cycles": 0,  # Directory access latency in cycles
    "mshr_num_entries": -1,  # Number of MSHRs, -1 for almost unlimited
    "addr_range_start": 0,  # Start address for the directory
    "addr_range_end": memory_capacity*1024*1024*1024-1,  
    "clock": memory_clock
})

# directory_memNIC = directory.setSubComponent("memlink", "memHierarchy.MemNIC")
# directory_memNIC.addParams({
#     "debug": 1,
#     "group": 1,  # Group ID for network hierarchy
#     "network_bw": "80GiB/s",  # Bandwidth between cache and router
#     "network_input_buffer_size": "1KiB",
#     "network_output_buffer_size": "1KiB",
# })

# Create the MemoryController and Memory
memctrl = sst.Component("memory_controller", "memHierarchy.MemController")
memctrl.addParams({
    "debug": 0,
    "clock": memory_clock,
    "backing": "none",
    "addr_range_start": 0,  # Start at address 0
    "addr_range_end": memory_capacity*1024*1024*1024-1,  
    "clock" : memory_clock

})

memory = memctrl.setSubComponent("backend", "memHierarchy.simpleMem")
memory.addParams({
    "mem_size": str(memory_capacity)+"MiB",
    "access_time": "50ns",
    "clock" : memory_clock
})

links = topo.getHostLinks()
linkNames = topo.getHostLinkNames()

# Connect each CPU's L1 cache to one of the 4 routers using MemNIC
for i in range(0, num_cpus, 2):
    # Connect CPU i and CPU i+1's L1 caches via their MemNICs to the routers
    caches[i].addLink(links[i], "directory", cache_router_latency)
    # print(f"Connecting {caches[i].getFullName()} to {linkNames[i]}")

    caches[i+1].addLink(links[i+1], "directory", cache_router_latency)
    # print(f"Connecting {caches[i+1].getFullName()} to {linkNames[i+1]}")

memLink = topo.getMemLink()
# Connect the top router to the directory controller
directory.addLink(memLink[0], "network", router_directory_latency)

# Connect the directory controller to the memory controller
link_directory_memctrl = sst.Link("link_directory_memctrl")
link_directory_memctrl.connect((directory, "memory", directory_mem_latency), (memctrl, "direct_link", directory_mem_latency))

# Now the full system is connected: 
# 8 CPUs (NIC) -> 4 Routers -> 1 Top Router -> Directory Controller -> Memory Controller -> Memory
# Enable SST Statistics Outputs for this simulation
sst.setStatisticLoadLevel(4)
sst.enableAllStatisticsForAllComponents({"type":"sst.AccumulatorStatistic"})
sst.setStatisticOutput("sst.statOutputCSV")
sst.setStatisticOutputOptions( {
    "filepath"  : "./stats-baseline-ariel.csv",
    "separator" : ", "
} )

print("Completed configuring the SST model")

