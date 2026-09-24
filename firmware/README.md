# Firmware

We use platformio to build/structure our firmware. Install the vscode extension or use the cli.

## Environments

Each firmware binary is built by using a different platformio "environment." These are defined in platformio.ini.

The main file for each environment exists in a specific folder in src/.

To create a new binary for a new board or just a special purpose, create an associated folder in src/ that defines a main.c. Add a new "env" in platformio.ini based on other existing ones. There are lots of platformio docs to help with this. 

The most important things are to extend the appropriate platform config and change the build_src_filter to include your main file.

To build the appropriate binary, use the dropdown in the platformio vscode tab.

Right now, we also define the .c files for our libraries in the src folder. These will likely soon be moved into a lib/ folder.

## Structure

Libraries are split into different folders depending on who is using them. "common" libraries are intended to be used across different micros / platforms. These should not use platform-specific sdks, etc.

## Testing

The test/ folder includes unit tests. Right now, these are only setup to run on the host. To run tests: ```pio test -e native```

## Host scripts

The host_scripts/ directory is for test python scripts to run on the host. These can interact with the micro over uart, etc. These are just testing scripts, less formal than the host/micro interactions in the higher level software/ folder.

To run host scripts (these will vary slightly depending on os / python installation):
```
$ cd <irltspice>/firmware # replace <irltspice> with wherever you have the repo
$ python3 -m venv .venv
$ source .venv/bin/activate
$ pip install pyserial protobuf protobuf-py protobuf-init protoc-gen-py
$ mkdir -p host_scripts/proto
$ protoc --py_out=host_scripts/proto --init_python_out=host_scripts/proto --proto_path ../proto ../proto/*.proto
$ python3 host_scripts/test_uart_framing.py
```

