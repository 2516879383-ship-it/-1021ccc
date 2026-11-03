"""CanMV: save ONE unannotated RGB565 sample for calibration. No UART/PWM."""
import os
import sys
import time
sys.path.insert(0,'/sdcard/transport_vision')
import vision_config as cfg
from media.sensor import Sensor
from media.media import MediaManager


def capture():
    sensor = None
    try:
        sensor = Sensor(id=cfg.SENSOR_ID,width=1920,height=1080,fps=30)
        sensor.reset()
        sensor.set_hmirror(cfg.SENSOR_HMIRROR)
        sensor.set_vflip(cfg.SENSOR_VFLIP)
        sensor.set_framesize(width=cfg.AI_SIZE[0],height=cfg.AI_SIZE[1])
        sensor.set_pixformat(Sensor.RGB565)
        MediaManager.init()
        sensor.run()
        for _ in range(10):
            frame = sensor.snapshot()
            time.sleep_ms(30)
        try:
            os.stat('/sdcard/capture')
        except OSError:
            os.mkdir('/sdcard/capture')
        stem = '/sdcard/capture/ring_%d' % time.ticks_ms()
        index = 0
        while True:
            path = stem+'_%d.bmp' % index
            try:
                os.stat(path)
            except OSError:
                break
            index += 1
            if index >= 1000:raise OSError('Too many samples with the same timestamp')
        frame.save(path)
        print('SAMPLE_SAVED',path,'Record camera pose, height and target number separately')
    finally:
        if sensor is not None:sensor.stop()
        time.sleep_ms(100)
        MediaManager.deinit()


if __name__ == '__main__':capture()
