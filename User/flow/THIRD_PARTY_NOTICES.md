# PMW3901 attribution

Only the optimisation register table in pmw3901.c is adapted from Bitcraze_PMW3901.
Upstream: https://github.com/bitcraze/Bitcraze_PMW3901/blob/d322d98d6f61757352d11d922cd194539e165231/src/Bitcraze_PMW3901.cpp
Retrieved 2026-10-21. The original MIT notice is preserved in pmw3901.c.
The HAL transport, state machine, parser, service, UI and tests are local code.
Crazyflie source was consulted for the public 12-byte burst wire layout only;
no platform, bitfield struct, RTOS code or optimisation table was copied from it.
