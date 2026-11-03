"""Dual YOLO11 vision for the RPIP protocol.
Normal entry previews both models, but reports only after host scan commands.
preview_models.py never opens UART.
No PWM, servo, gripper or motor control is included.
"""
import os
import sys
import time
import gc
import image
from libs.YOLO import YOLO11
from media.sensor import Sensor
from media.display import Display
from media.media import MediaManager
import vision_config as cfg
import vision_protocol as wire
from vision_logic import Controller, detections
from vision_quality import select
from vision_settings import validate
from ring_refinement import refine


class CheckedYOLO(YOLO11):
    def postprocess(self, results):
        # Check before entering the native decoder with a class-count argument.
        if len(results) != 1:
            raise ValueError('Expected one YOLO11 detection output')
        shape = results[0].shape
        if len(shape) != 3 or shape[0] != 1 or shape[1] != 4 + len(self.labels) or shape[2] <= 0:
            raise ValueError('Wrong model output for labels: ' + str(shape))
        return super().postprocess(results)


def load_model(path, labels):
    try:
        size = os.stat(path)[6]
    except OSError:
        raise OSError('YOLO model missing/unreadable: '+path)
    if size <= 0:
        raise ValueError('Empty YOLO model: '+path)
    print('VISION_LOAD', path, size, 'labels', labels)
    model = CheckedYOLO(task_type='detect', mode='video', kmodel_path=path,
                        labels=labels, rgb888p_size=list(cfg.AI_SIZE),
                        model_input_size=list(cfg.MODEL_SIZE), display_size=list(cfg.AI_SIZE),
                        conf_thresh=cfg.CONFIDENCE, nms_thresh=cfg.NMS_THRESHOLD,
                        max_boxes_num=20, debug_mode=0)
    try:
        model.config_preprocess()
    except Exception:
        model.deinit()
        raise
    return model


def draw(osd, items, kind, color):
    for ident, score, box, cx, cy in items:
        x, y, w, h = [int(round(v)) for v in box]
        osd.draw_rectangle(x, y, w, h, color=color, thickness=2)
        osd.draw_cross(int(cx), int(cy), color=color, size=10, thickness=2)
        osd.draw_string_advanced(max(0, x), max(28, y - 23), 20,
                                 kind + str(ident) + ' %.2f' % score, color=color)


def run(bench=False, test_color=None, uart_factory=None, test_mode=None, test_pos=None):
    validate()
    sensor = None
    uart = None
    color_model = None
    ring_model = None
    state = Controller()
    # Explicit isolated UART bench only; normal entry still waits for the host.
    if test_color is not None:
        if test_color not in (1, 2, 3, 4, 5, 6):
            raise ValueError('test_color must be 1..6')
        state.mode = 1
        state.color = test_color
    if test_mode is not None:
        if not bench or test_mode not in (1,2,3,4):
            raise ValueError('test_mode is a no-UART preview option (1..4)')
        state.mode = test_mode
    if test_pos is not None:
        if not bench or test_pos not in (1,2,3):
            raise ValueError('test_pos is a no-UART preview option (1..3)')
        state.pos = test_pos
    parser = wire.Parser()
    qr_started = time.ticks_ms()
    parser_last_rx = qr_started

    def send(packet):
        if uart is not None:
            # Frames <=16B, low rate. Treat a short write as a link failure.
            if uart.write(packet) != len(packet):
                raise OSError('UART short write')

    def poll():
        nonlocal qr_started, parser_last_rx
        if uart is None:
            return
        now = time.ticks_ms()
        if parser.buf and time.ticks_diff(now, parser_last_rx) > 200:
            parser.reset()  # Discard a truncated frame, never old observations.
        data = uart.read(cfg.UART_READ_LIMIT)
        if data:
            parser_last_rx = now
            for cmd, params in parser.feed(data):
                previous_version = state.version
                ack = state.control(cmd, params)
                if ack:
                    send(ack)
                    if state.version != previous_version:
                        qr_started = now
                    print('VISION_COMMAND', hex(cmd), list(params), 'mode', state.mode)

    try:
        os.exitpoint(os.EXITPOINT_ENABLE)
        # Load first, so missing files fail clearly without opening UART.
        color_model = load_model(cfg.COLOR_MODEL, cfg.COLOR_LABELS)
        ring_model = load_model(cfg.RING_MODEL, cfg.RING_LABELS)
        sensor = Sensor(id=cfg.SENSOR_ID, width=1920, height=1080, fps=30)
        sensor.reset()
        sensor.set_hmirror(cfg.SENSOR_HMIRROR)
        sensor.set_vflip(cfg.SENSOR_VFLIP)
        width, height = cfg.AI_SIZE
        # Same field of view / dimensions for preview, QR, and both models.
        sensor.set_framesize(width=width, height=height, chn=0)
        sensor.set_pixformat(Sensor.YUV420SP, chn=0)
        sensor.set_framesize(width=width, height=height, chn=1)
        sensor.set_pixformat(Sensor.RGB565, chn=1)
        sensor.set_framesize(width=width, height=height, chn=2)
        sensor.set_pixformat(Sensor.RGBP888, chn=2)
        if cfg.DISPLAY_MODE != 'virt':
            raise ValueError('This integration uses 640x360 virtual display; configure hardware display separately')
        Display.init(Display.VIRT, width=width, height=height, osd_num=1, to_ide=True)
        Display.bind_layer(**sensor.bind_info(x=0, y=0, chn=0), layer=Display.LAYER_VIDEO1)
        osd = image.Image(width, height, image.ARGB8888)
        MediaManager.init()
        sensor.run()
        # Only integrated mode claims these pins. Bench mode never creates UART.
        if not bench:
            from machine import FPIOA, UART
            fpioa = FPIOA()
            if cfg.UART_ID == 1 and (cfg.UART_TX_IO, cfg.UART_RX_IO) == (3, 4):
                tx_func, rx_func = FPIOA.UART1_TXD, FPIOA.UART1_RXD
            elif cfg.UART_ID == 2 and (cfg.UART_TX_IO, cfg.UART_RX_IO) in ((5, 6), (11, 12)):
                tx_func, rx_func = FPIOA.UART2_TXD, FPIOA.UART2_RXD
            else:
                raise ValueError('Unverified UART/FPIOA mapping')
            fpioa.set_function(cfg.UART_TX_IO, tx_func)
            fpioa.set_function(cfg.UART_RX_IO, rx_func)
            factory = uart_factory if uart_factory is not None else UART
            # Same constructor used by the existing CanMV root entry. Its
            # defaults are 8N1; some firmware builds lack the named constants.
            uart = factory(cfg.UART_ID, cfg.UART_BAUDRATE)
        print('VISION_READY', 'BENCH_NO_UART' if bench else
              'UART%d_TX_IO%d_RX_IO%d' % (cfg.UART_ID, cfg.UART_TX_IO, cfg.UART_RX_IO),
              'AI', cfg.AI_SIZE, 'protocol', cfg.PROTOCOL_SIZE)
        print('VISION_BACKEND', 'YOLO11', 'startup_preview', cfg.STARTUP_PREVIEW)
        last_report = time.ticks_ms()
        last_heartbeat = time.ticks_ms()
        last_log = time.ticks_ms()
        frames = 0
        frame_id = 0
        last_refine_error = None
        while True:
            os.exitpoint()
            poll()
            version = state.version
            mode = state.mode
            startup_preview = cfg.STARTUP_PREVIEW and mode == -1 and version == 0
            colors, rings = [], []  # Always fresh; never retain previous boxes.
            qr_messages = []
            refinement = None
            refinement_reason = 'NOT_REQUESTED'
            capture_ms = time.ticks_ms()
            frame_id = (frame_id+1) & 65535
            if bench or startup_preview or mode in (1, 2, 3, 4):
                camera_frame = sensor.snapshot(chn=2)
                tensor = camera_frame.to_numpy_ref()
                colors = detections(color_model.run(tensor), cfg.COLOR_IDS)
                if bench or startup_preview or mode in (2,4):
                    rings = detections(ring_model.run(tensor), cfg.RING_IDS, rings=True)
                if mode in (2,4):
                    target,_ = select(rings,state.pos,cfg.RING_ROIS[state.zone])
                    if target is not None:
                        try:
                            refinement,refinement_reason = refine(tensor,target)
                        except (AttributeError,TypeError,ValueError,RuntimeError,OSError) as error:
                            refinement_reason = 'REFINE_API_ERROR'
                            # Never silently label a box centre as geometric refinement.
                            description = repr(error)
                            if description != last_refine_error:
                                print('VISION_REFINE_ERROR',description)
                                last_refine_error = description
                # The camera frame remains referenced through BOTH inferences.
                del tensor
                del camera_frame
            elif mode == 0 and not state.qr_done:
                qr_image = sensor.snapshot(chn=1)
                qr_messages = state.qr_report([q.payload() for q in qr_image.find_qrcodes()],frame_id)
                del qr_image
            poll()  # STOP / new target received during inference wins immediately.
            now = time.ticks_ms()
            if state.version != version:
                colors, rings, qr_messages = [], [], []
                startup_preview = False
                last_report = now
            else:
                state.observe(colors,rings,frame_id,now,time.ticks_diff(now,capture_ms),refinement)
                for packet in qr_messages:
                    send(packet)
                    print('VISION_TASK',state.task_text,state.task_plan)
                if mode == 0 and not state.qr_done and time.ticks_diff(now, qr_started) >= cfg.QR_TIMEOUT_MS:
                    send(wire.frame(wire.NOTFOUND))
                    state.qr_done = True
                if time.ticks_diff(now, last_report) >= cfg.REPORT_MS:
                    last_report = now
                    for packet in state.report():
                        send(packet)
            if time.ticks_diff(now, last_heartbeat) >= cfg.HEARTBEAT_MS:
                send(wire.frame(wire.HEARTBEAT))
                last_heartbeat = now
            osd.clear()
            draw(osd, colors, 'C', (255, 0, 255, 0))
            draw(osd, rings, 'R', (255, 255, 220, 0))
            if refinement is not None and state.version == version:
                osd.draw_cross(int(round(refinement[0])),int(round(refinement[1])),
                               color=(255,255,0,255),size=16,thickness=2)
            title = 'mode=%d color=%d ring=%d' % (state.mode, state.color, state.pos)
            if bench:
                title = 'YOLO PREVIEW / NO UART'
            elif startup_preview:
                title = 'YOLO PREVIEW / WAIT HOST'
            osd.draw_string_advanced(4, 3, 20, title, color=(255, 0, 255, 255))
            if mode in (2,3,4) and state.version == version:
                osd.draw_string_advanced(4,cfg.AI_SIZE[1]-26,18,
                    state.reason+' / '+refinement_reason,color=(255,255,255,0))
            Display.show_image(osd, 0, 0, Display.LAYER_OSD3)
            frames += 1
            if time.ticks_diff(now, last_log) >= 1000:
                print('VISION_ALIVE', 'fps', round(frames * 1000 / time.ticks_diff(now, last_log), 1),
                      'mode', state.mode, 'colors', [(d[0], round(d[1], 2)) for d in colors],
                      'rings', [(d[0], round(d[1], 2)) for d in rings],
                      'quality',state.reason,'speed_px_s',state.quality.speed,
                      'age_ms',time.ticks_diff(now,capture_ms),'refine',refinement_reason)
                frames = 0
                last_log = now
            gc.collect()
            time.sleep_ms(5)
    except KeyboardInterrupt:
        print('VISION_STOPPED')
    except Exception as error:
        if str(error) == 'IDE interrupt':
            print('VISION_STOPPED')
        else:
            print('VISION_ERROR', repr(error))
            sys.print_exception(error)
            raise
    finally:
        # Revoke the current observation on failure or user stop, if UART exists.
        if uart is not None:
            try:
                send(wire.frame(wire.NOTFOUND))
                if state.mode == 2:
                    send(wire.frame(wire.PLACE_CHECK, [state.zone, state.pos, 0]))
                uart.deinit()
            except Exception as error:
                print('VISION_UART_CLEANUP', repr(error))
        if sensor is not None:
            try:
                sensor.stop()
            except Exception as error:
                print('VISION_SENSOR_CLEANUP', repr(error))
        for model in (ring_model, color_model):
            if model is not None:
                try:
                    model.deinit()
                except Exception as error:
                    print('VISION_MODEL_CLEANUP', repr(error))
        try:
            Display.deinit()
        except Exception as error:
            print('VISION_DISPLAY_CLEANUP', repr(error))
        os.exitpoint(os.EXITPOINT_ENABLE_SLEEP)
        time.sleep_ms(100)
        try:
            MediaManager.deinit()
        except Exception as error:
            print('VISION_MEDIA_CLEANUP', repr(error))


if __name__ == '__main__':
    run()
