import serial
from proto.command_pb import MotherCommand, ProgrammingCommand, ProgrammingPayload, ProgramXbar
from protobuf import Oneof

if __name__ == "__main__":
    port = serial.Serial(port = '/dev/ttyACM0', baudrate = 115200)

    command = MotherCommand(
        cmd=Oneof("dummy_command", ProgrammingCommand(
            motherboard_id=0,
            daughterboard_id=0,
            payload=ProgrammingPayload(
                data=Oneof("xbar", ProgramXbar(
                    crossbar_id=3,
                    row=0,
                    column=0,
                ))
            )
        ))
    )

    encoded = command.to_binary()

    length_encoded = len(encoded).to_bytes(length=2)
    wire_format = bytes([0xab]) + length_encoded + length_encoded + encoded + int(0).to_bytes(length=2)

    port.write(wire_format)

