"""CanMV: numbered YOLO boxes + local geometry; NO UART or actuator output."""
import sys
sys.path.insert(0,'/sdcard/transport_vision')
from vision_app import run
# Change test_pos to the printed station number (1..3), NOT a scoring ring.
run(bench=True,test_mode=4,test_pos=1)
