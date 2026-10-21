# GP API revision 10

This header is the single declaration reference for the assignments. It contains
types and function prototypes, not production implementations. Ahmed provides
the portable codec definitions; Rodaina uses that exact version for Blackpill TX.
Ali and Farah publish the shared header on Testing before developer branches.

Library ownership: joystick = Boody; uart_sender = Rodaina; gp_protocol and
gateway = Ahmed; command_map = Rabe3; motor_driver = Boda.
Application initialization, hardware callbacks, RTOS tasks and function calls
belong to the maintainers. Do not hard-code board pins or HAL handles in libraries.
