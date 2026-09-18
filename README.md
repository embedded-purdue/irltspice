# IRLTSPICE - An ES@P Project

IRL + LTspice = IRLTSPICE!

IRLTSPICE provides the convenience of spice with the accuracy and testability of a real-life analog circuit.

Simply design your circuit in your EDA tool of choice, or use our handy Python API, upload to the board, and bang!
Your circuit now exists in real life, and you can now do whatever you want!

Run FRAs, send audio through it, use it as a testbed, or whatever else you can imagine.
It's now just an analog circuit.

Analog protyping has never been this easy.

## How it works

The IRLTSPICE board features a bank of resistors, capacitors, and op-amps, so every virtual component has a physical counterpart that was already on the board.

These physical components are then connected to each other via specialized switches, such that the nets are the same as in your virtual design.

## About

IRLTSPICE began as a Purdue Electrical and Computer Engineering senior design project, and is now an Embedded Systems at Purdue (ES@P) project.

## Project Structure
The senior design project uses a monorepo architecture with the following sub-directories:
- `hardware/` Hardware schematics
- `firmware/` Microcontroller firmware
- `software/` On-PC software
- `evilware/` Evil Circuits and Code
