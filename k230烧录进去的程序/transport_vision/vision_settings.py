"""Fail clearly on invalid configuration before claiming camera/UART resources."""
import vision_config as cfg
from vision_quality import finite


def validate():
    if (cfg.UART_ID,cfg.UART_TX_IO,cfg.UART_RX_IO) not in ((1,3,4),(2,5,6),(2,11,12)):
        raise ValueError('Unsupported UART/pin combination')
    for name in ('CONFIDENCE','NMS_THRESHOLD'):
        value = getattr(cfg,name)
        if not finite(value) or not 0 < value <= 1:
            raise ValueError('Invalid '+name)
    for name in ('AI_SIZE','MODEL_SIZE','PROTOCOL_SIZE'):
        value = getattr(cfg,name)
        if len(value) != 2 or any(not isinstance(v,int) or not 0 < v <= 65535 for v in value):
            raise ValueError('Invalid '+name)
    if cfg.AI_SIZE[0] % 16:
        raise ValueError('AI width must be a multiple of 16')
    if len(cfg.COLOR_LABELS) != 6 or sorted(cfg.COLOR_IDS) != [1,2,3,4,5,6]:
        raise ValueError('Wrong six-colour label mapping')
    if len(cfg.RING_LABELS) != 3 or sorted(cfg.RING_IDS) != [1,2,3]:
        raise ValueError('Wrong station-number label mapping')
    for name in ('REPORT_MS','HEARTBEAT_MS','QR_TIMEOUT_MS','QR_STABLE_FRAMES',
                 'STABLE_MIN_FRAMES','STABLE_MIN_MS','STABLE_WINDOW_MS',
                 'STABLE_MAX_GAP_MS','STABLE_MAX_SAMPLES','MAX_OBSERVATION_AGE_MS',
                 'UART_READ_LIMIT','PLACE_STABLE_FRAMES','RING_MAX_CIRCLES',
                 'RING_MIN_DISTINCT_RADII','RING_HOUGH_THRESHOLD'):
        value = getattr(cfg,name)
        if not isinstance(value,int) or not 0 < value < 1000000:
            raise ValueError('Invalid '+name)
    if cfg.STABLE_MIN_FRAMES < 2 or cfg.STABLE_MIN_MS > cfg.STABLE_WINDOW_MS:
        raise ValueError('Invalid stability window')
    if cfg.STABLE_MAX_SAMPLES < cfg.STABLE_MIN_FRAMES or cfg.RING_MIN_DISTINCT_RADII < 2:
        raise ValueError('Too few independent samples/radii')
    if set(cfg.RING_ROIS) != {1,2}:
        raise ValueError('RING_ROIS must define rough(1) and storage(2)')
    for roi in [cfg.MATERIAL_ROI,cfg.PICK_ROI]+list(cfg.RING_ROIS.values()):
        if roi is not None:
            if len(roi) != 4 or not all(isinstance(v,int) for v in roi):
                raise ValueError('ROI must be integer AI pixels')
            x,y,w,h = roi
            if x < 0 or y < 0 or w <= 0 or h <= 0 or x+w > cfg.AI_SIZE[0] or y+h > cfg.AI_SIZE[1]:
                raise ValueError('ROI outside AI image')
    if cfg.PICK_CALIBRATED and cfg.PICK_ROI is None:
        raise ValueError('PICK_CALIBRATED requires a measured PICK_ROI')
    for name in ('STABLE_CENTER_PX','STABLE_SIZE_FRACTION','PLACE_OK_PX',
                 'RING_MIN_RADIUS_PX','RING_MAX_RADIUS_PX','RING_CENTER_SPREAD_PX',
                 'RING_RADIUS_SEPARATION_PX','RING_CENTER_LIMIT','RING_MIN_ASPECT'):
        value = getattr(cfg,name)
        if not finite(value) or value <= 0:
            raise ValueError('Invalid '+name)
    if cfg.RING_MIN_RADIUS_PX >= cfg.RING_MAX_RADIUS_PX or cfg.RING_MIN_ASPECT > 1:
        raise ValueError('Invalid circle-fit geometry')
    if not isinstance(cfg.DISTANCE_CM,int) or not 0 <= cfg.DISTANCE_CM <= 65535:
        raise ValueError('Invalid DISTANCE_CM')
    return True
