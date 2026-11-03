"""CanMV: view both YOLO models without claiming the serial pins."""
import sys
sys.path.insert(0,'/sdcard/transport_vision')
from vision_app import run
run(bench=True)
