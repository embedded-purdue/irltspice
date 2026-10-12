# Desired python API
# Not yet working, but this is our rough north star for what to create

import sys
import irl.driver as irld
from irld import Driver
import irl.compiler as irlc
import irl.netlist.Netlist

# High level compiler.
# This is what a typical user scripting their circuit should do
# We optimize for this case
def make_lpf():
    # Create a new netlist. A bipartite graph of nets/components
    d = Netlst()
    # Get/create the input net
    input = d.InputNet()
    # Get/create the output net
    output = d.OutputNet()

    r = d.NewResistor(10_000)
    c = d.NewCapacitor(1e-6)
    buf = d.NewBuffer()

    # pre-buffered net
    outnet_raw = d.NewNet()

    # Pins can be indexed by numbers or name
    r.pins[0].connect(input)
    r.pins[1].connect(outnet_raw)

    c.pins[0].connect(outnet_raw)
    # Netlist has special ground/vcc/-vcc nets
    c.pins[1].connect(d.Gnd())

    buf.pins["input"].connect(outnet_raw)
    buf.pins["output"].connect(output)

def connect_to_port(port_path: str):
    d = Driver(port_path, timeout=1000)

def flash_design(design: irlc.Design, driver: Driver):

    onestep = True
    if onestep:
        # Easy happy path!
        # Just hit irlc.compileAndFlash and it'll do all the work!

        tryagain = True
        while tryagain:
            compile_result, programming_result: tuple[irlc.CompileResult, irld.ProgrammingResult] = irlc.compileAndFlash(design, driver)

            # both irlc.CompileResult and irld.CommandResult contain the isError property

            if compile_result.isError:
                print(f"Error kind: {compile_result.errorKind}, msg: {compile_result.errorMsg}")
                tryagain = False
            elif programming_result.isError:
                print("Programming error. Retrying")
                tryagain = True


    else:
        # If you want to do everything manually, you totally can! 
        # You get to act as the intermediary between irlc and irld

        topo_stale = True
        while topo_stale:

            r: irld.CommandResult = driver.getCurrentTopology()

            if r.isError:
                print(f"Error getting topology: {r}")
                return

            # Result is either an error or a response.
            # if it's a response it is either a topology or a stop programming response
            topo = r.response.topology
            # topo will contain in it a netlist of the topology

            if not topo.connected():
                print("Please connect topology")
                return
            elif topo.numDaughters() == 0:
                print("Please connect a daughterboard")
                return

            compile_result: irlc.CompileResult = irlc.compileForTopo(design, topo)
            if compile_result.isError:
                print(f"Compiler error: {compile_result.errorKind} - {compile_result.errorMsg}")
                return
            prog_cmds: list[irld.Command] = compile_result.programmingCommands
            expected_response: irld.Response = compile_result.expectedResponse
            
            # CommandResult != ProgrammingResult
            # Command is for indiv command, Programming probably contains more data abt the whole transaction

            r: irld.CommandResult = irld.startProgramming(topo)
            for cmd in prog_cmds:
                if r.isError == False:
                    r = irld.sendProgrammingCmd(topo, cmd)
            if r.isError == False:
                r = irld.stopProgramming(topo)

            if r.isError:

                if r.errorKind == "StaleTopo":
                    print("Topology was stale, retrying...")
                    continue
                else:
                    print("Unexpected error\n")
                    sys.exit(-1)

            else:
                if r.response != expected_response:
                    print("Unexpected response, retrying...")
                    continue

            topo_stale = False
