# Automatically generated SST Python input
import sst
import os

# Define SST core options
sst.setProgramOption("timebase", "1ns")
sst.setProgramOption("stopAtCycle", "1000000ns")

# Define the simulation components
comp_cpu = sst.Component("cpu", "prospero.prospero")
comp_cpu.addParams({
      "physlimit" : str(512 * 1024 * 1024),
      "tracetype" : "file",
      "outputlevel" : "0",
      "cache_line" : "64",
      "trace" : os.getcwd() + '/sstprospero-0',
      "traceformat" : "text"
})
comp_l1cache = sst.Component("l1cache", "memHierarchy.Cache")
comp_l1cache.addParams({
      "access_latency_cycles" : "1",
      "cache_frequency" : "2 Ghz",
      "replacement_policy" : "lru",
      "coherence_protocol" : "MESI",
      "associativity" : "8",
      "cache_line_size" : "64",
      "statistics" : "1",
      "L1" : "1",
      "cache_size" : "64 KB"
})
comp_memory = sst.Component("memory", "memHierarchy.MemController")
comp_memory.addParams({
      "coherence_protocol" : "MESI",
      "system_ini" : "system.ini",
      "clock" : "1GHz",
      "access_time" : "1000 ns",
      "device_ini" : "DDR3_micron_32M_8B_x4_sg125.ini",
      "mem_size" : "512",
      "backend" : "memHierarchy.dramsim"
})


# Define the simulation links
link_cpu_cache_link = sst.Link("link_cpu_cache_link")
link_cpu_cache_link.connect( (comp_cpu, "cache_link", "1000ps"), (comp_l1cache, "high_network_0", "1000ps") )
link_mem_bus_link = sst.Link("link_mem_bus_link")
link_mem_bus_link.connect( (comp_l1cache, "low_network_0", "50ps"), (comp_memory, "direct_link", "50ps") )
# End of generated output.
