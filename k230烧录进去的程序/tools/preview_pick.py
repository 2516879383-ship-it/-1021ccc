"""CanMV: inspect stability of a requested colour; NO UART or actuator output."""
import sys
sys.path.insert(0,'/sdcard/transport_vision')
from vision_app import run
# Change 1 to the required competition colour (1..6) for this bench only.
run(bench=True,test_color=1,test_mode=3)
