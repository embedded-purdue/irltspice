# Boards

This folder contains generated c code from STM32CubeMX.

inc
  - daughterboard
    - boards
      - board a
      - board b

src
  - daughterboard
    - boards
      - board a
      - board b
    - daughterboard a
      - main.c
    - daughterboard b
      - main.c

platfomio.ini:
```
[env:daughterboard_a_board_a]
build_src_filter = -<*> +<daughterboard/daughterboard_a/*> +<boards/board_a/*>
build_flags = ... -Iinclude/boards/board_a
```

this also handles hardware revisions - revision # can be stored in board a / board b files somewhere

to use cubemx
  - makefile
  - basic application structure
  - reference in config file
  - generate .c/.h
  - generate code
  - copy headers into inc/daughterboard/init/board_a
  - copy src into src/daughterboard/boards/board_a
  - remove infinite loop from main.c, rename main function to mx_init, add mx_init to main.h 
  - keep .ioc files in src/daughterboard/boards
  - remove irrelevant files - only need msp, main, gpio, it
    - don't need sysmem, syscalls, system_alsdkfj.c
