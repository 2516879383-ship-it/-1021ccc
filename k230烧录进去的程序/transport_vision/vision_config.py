"""K230 / CanMV 1.8 vision only. No motor or PWM output."""
COLOR_MODEL = '/sdcard/transport_vision/models/colors.kmodel'
RING_MODEL = '/sdcard/transport_vision/models/rings.kmodel'
COLOR_LABELS = ['black', 'deep blue', 'green', 'light blue', 'red', 'yellow']
RING_LABELS = ['圈1', '圈2', '圈3']
# Model class index -> STM32 competition color / printed ring number.
COLOR_IDS = (5, 3, 4, 6, 1, 2)
RING_IDS = (1, 2, 3)
AI_SIZE = (640, 360)
MODEL_SIZE = (320, 320)
PROTOCOL_SIZE = (1280, 720)  # Wire coordinates; not calibrated millimetres.
CONFIDENCE = 0.60
NMS_THRESHOLD = 0.45
SENSOR_ID = 2               # 01Studio onboard camera, confirmed earlier.
SENSOR_HMIRROR = True       # Preserve migration main.py orientation.
SENSOR_VFLIP = False
DISPLAY_MODE = 'virt'       # PC preview, not a nonexistent LCD.
# Show BOTH models before the first valid host command. No observations are
# transmitted in this preview; STOP keeps scanning off until SCAN is received.
STARTUP_PREVIEW = True
UART_ID = 2
# User's rear silk labels: 11/TX2, 12/RX2. GPIO IDs, not header positions.
UART_TX_IO = 11
UART_RX_IO = 12
UART_BAUDRATE = 115200
REPORT_MS = 100
HEARTBEAT_MS = 1000
QR_TIMEOUT_MS = 20000
PLACE_OK_PX = 130           # In 1280x720 protocol pixels; requires real calibration.
PLACE_STABLE_FRAMES = 3
PLACE_CHECK_CALIBRATED = False  # The old 130 px rule is NOT competition scoring.
# All ROIs are AI pixels (640x360), never protocol pixels or millimetres.
MATERIAL_ROI = None
PICK_ROI = None                 # Set the actual reachable grasp window first.
RING_ROIS = {1: None, 2: None}   # Rough/storage scene restrictions.
PICK_CALIBRATED = False         # Pixel stability alone must not authorize a grab.
STABLE_MIN_FRAMES = 4
STABLE_MIN_MS = 300
STABLE_WINDOW_MS = 500
STABLE_MAX_GAP_MS = 250
STABLE_MAX_SAMPLES = 24
STABLE_CENTER_PX = 4.0
STABLE_SIZE_FRACTION = 0.08
MAX_OBSERVATION_AGE_MS = 300
QR_STABLE_FRAMES = 3
UART_READ_LIMIT = 256
# YOLO identifies the whole numbered target. Local geometry refines its centre.
RING_REFINE_ENABLED = True
RING_REFINEMENT_CALIBRATED = False
RING_MIN_ASPECT = 0.80          # Oblique ellipses need calibrated plane correction.
RING_MIN_RADIUS_PX = 5
RING_MAX_RADIUS_PX = 140
RING_HOUGH_THRESHOLD = 2000     # Initial SDK value; tune with real images.
RING_MAX_CIRCLES = 48
RING_MIN_DISTINCT_RADII = 3
RING_RADIUS_SEPARATION_PX = 3
RING_CENTER_SPREAD_PX = 3.0
RING_CENTER_LIMIT = 0.25
# Preserve the previous image-bearing formula for the existing host.
# This is NOT a calibrated world heading. Calibrate before chassis servo use.
LEGACY_IMAGE_BEARING = True
DISTANCE_CM = 0             # Unknown; do not use as a measured distance.
