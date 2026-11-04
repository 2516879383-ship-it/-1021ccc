"""YOLO identity, fresh observations and optional quality/context reporting."""
import math
import vision_config as cfg
import vision_protocol as wire
from vision_quality import Stability, finite, full_box, select
from vision_tasks import parse_task, expand_task


def detections(result, ids, rings=False):
    out = []
    if not result or len(result) != 3:
        return out
    if len(result[0]) != len(result[1]) or len(result[1]) != len(result[2]):
        return out
    width, height = cfg.AI_SIZE
    for box, cls_value, confidence in zip(result[0],result[1],result[2]):
        try:
            cls_number, score = float(cls_value), float(confidence)
            if not finite(cls_number) or not finite(score):
                continue
            cls = int(cls_number)
            if cls != cls_number or not 0 <= cls < len(ids) or not cfg.CONFIDENCE <= score <= 1:
                continue
            if len(box) != 4:
                continue
            x,y,w,h = [float(v) for v in box]
            if not all(finite(v) for v in (x,y,w,h)):
                continue
            if not (w > 0 and h > 0 and x < width and y < height and x+w > 0 and y+h > 0):
                continue
            if rings and (x <= 0 or y <= 0 or x+w >= width or y+h >= height):
                continue
            out.append((ids[cls],score,(x,y,w,h),max(0,min(width-1,x+w/2)),max(0,min(height-1,y+h/2))))
        except (TypeError,ValueError,OverflowError):
            continue
    return out


def best(items, ident):
    # Compatibility helper. Control paths use select(), rejecting duplicates.
    candidates = [d for d in items if d[0] == ident]
    return max(candidates,key=lambda d:d[1]) if candidates else None


def coordinates(detection):
    w,h = cfg.PROTOCOL_SIZE
    return (max(0,min(w-1,int(round(detection[3]*w/cfg.AI_SIZE[0])))),
            max(0,min(h-1,int(round(detection[4]*h/cfg.AI_SIZE[1])))))


def material_frame(detection):
    cx,cy = coordinates(detection)
    offset = 0
    if cfg.LEGACY_IMAGE_BEARING:
        offset = int(round(math.atan2(cx-cfg.PROTOCOL_SIZE[0]/2,cfg.PROTOCOL_SIZE[1]-cy)*1800/math.pi))
    return wire.found(detection[0],cx,cy,offset,cfg.DISTANCE_CM)


class Controller:
    def __init__(self):
        self.mode,self.color,self.zone,self.pos = -1,1,1,1
        self.version = 0
        self.extended = False
        self.request_id = self.pose_id = 0
        self.frame_id = 0
        self.last_observed_frame = None
        self.quality = Stability()
        self._clear()

    def _clear(self):
        self.observation = None
        self.reason = 'WAIT_FRAME'
        self.place_hits = 0
        self.quality.reset()
        self.qr_done = False
        self.qr_candidate = None
        self.qr_hits = 0
        self.qr_last_frame = None
        self.task_text = None
        self.task_plan = None

    def control(self,cmd,params):
        valid = False
        if cmd == wire.SCAN and len(params) == 1 and params[0] in (0,1,2,3,4):
            self.mode = params[0]
            valid = True
        elif cmd == wire.STOP and not params:
            self.mode = -1
            valid = True
        elif cmd == wire.SET_COLOR and len(params) == 1 and 1 <= params[0] <= 6:
            self.color = params[0]
            valid = True
        elif cmd == wire.SET_TARGET and len(params) == 2 and params[0] in (1,2) and params[1] in (1,2,3):
            self.zone,self.pos = params
            valid = True
        elif cmd == wire.SET_CONTEXT and len(params) == 4:
            self.request_id = params[0]*256+params[1]
            self.pose_id = params[2]*256+params[3]
            self.extended = True
            valid = True
        elif cmd == wire.RESCAN and not params:
            valid = True
        if cmd not in (wire.SCAN,wire.STOP,wire.SET_COLOR,wire.SET_TARGET,wire.SET_CONTEXT,wire.RESCAN):
            return None
        if valid:
            self.version += 1
            self._clear()
        return wire.frame(wire.ACK,[cmd,0 if valid else 1])

    def observe(self,colors,rings,frame_id,now_ms,age_ms=0,refinement=None):
        self.observation = None
        self.frame_id = frame_id & 65535
        if frame_id == self.last_observed_frame:
            self.reason = 'DUPLICATE_FRAME'
            return
        self.last_observed_frame = frame_id
        if age_ms < 0 or age_ms > cfg.MAX_OBSERVATION_AGE_MS:
            self.quality.reset()
            self.place_hits = 0
            self.reason = 'STALE_FRAME'
            return
        if self.mode in (1,3):
            roi = cfg.PICK_ROI if self.mode == 3 else cfg.MATERIAL_ROI
            target,reason = select(colors,self.color,roi)
        elif self.mode in (2,4):
            target,reason = select(rings,self.pos,cfg.RING_ROIS[self.zone])
        else:
            self.reason = 'IDLE'
            return
        if self.mode == 4 and target is not None and refinement is None:
            target,reason = None,'NO_REFINEMENT'
        if target is None:
            self.quality.reset()
            self.place_hits = 0
            self.reason = reason
            return
        coarse = target
        if self.mode in (2,4) and refinement is not None:
            target = target[:3]+(refinement[0],refinement[1])
        stable = self.quality.update(target,frame_id,now_ms)
        flags = wire.FLAG_FOUND
        if full_box(target): flags |= wire.FLAG_FULL_BOX
        if stable: flags |= wire.FLAG_STABLE
        if self.mode in (2,4) and refinement is not None:
            flags |= wire.FLAG_REFINED
            if cfg.RING_REFINEMENT_CALIBRATED: flags |= wire.FLAG_CALIBRATED
        if self.mode == 3 and cfg.PICK_CALIBRATED and cfg.PICK_ROI is not None:
            flags |= wire.FLAG_CALIBRATED
            if stable: flags |= wire.FLAG_PICK_READY
        self.observation = (target,coarse,flags)
        self.reason = 'STABLE' if stable else 'STABILIZING'
        if self.mode == 3 and not cfg.PICK_CALIBRATED:
            self.reason = 'PICK_NOT_CALIBRATED'
        if self.mode == 2:
            x,y,w,h = target[2]
            blocks = [b for b in colors if b[0] == self.color and full_box(b)
                      and x <= b[3] <= x+w and y <= b[4] <= y+h]
            inside = False
            if len(blocks) == 1:
                cx,cy = coordinates(target)
                bx,by = coordinates(blocks[0])
                inside = (bx-cx)**2+(by-cy)**2 <= cfg.PLACE_OK_PX**2
            self.place_hits = min(self.place_hits+1,cfg.PLACE_STABLE_FRAMES) if inside and stable else 0

    def report(self):
        if self.mode not in (1,2,3,4):
            return []
        messages = []
        flags = 0
        ident = self.color if self.mode in (1,3) else self.pos
        cx = cy = 0
        if self.observation is None:
            messages.append(wire.frame(wire.NOTFOUND))
            if self.reason == 'AMBIGUOUS': flags |= wire.FLAG_AMBIGUOUS
            if self.reason == 'STALE_FRAME': flags |= wire.FLAG_STALE
        else:
            target,coarse,flags = self.observation
            cx,cy = coordinates(target)
            if self.mode in (1,3):
                messages.append(material_frame(target))
            else:
                # Mode 2 keeps legacy coarse coordinates; mode 4 is geometry-only.
                rx,ry = coordinates(coarse if self.mode == 2 else target)
                messages.append(wire.frame(wire.RING,bytes([self.pos])+wire.u16(rx)+wire.u16(ry)))
        if self.mode == 2:
            ok = (self.observation is not None and cfg.PLACE_CHECK_CALIBRATED
                  and cfg.RING_REFINEMENT_CALIBRATED and flags & wire.FLAG_REFINED
                  and self.place_hits >= cfg.PLACE_STABLE_FRAMES)
            messages.append(wire.frame(wire.PLACE_CHECK,[self.zone,self.pos,int(bool(ok))]))
        if self.extended:
            params = (wire.u16(self.request_id)+wire.u16(self.pose_id)+wire.u16(self.frame_id)
                      +bytes([flags,ident])+wire.u16(cx)+wire.u16(cy))
            messages.append(wire.frame(wire.OBSERVATION,params))
        return messages

    def qr_report(self,payloads,frame_id):
        if self.mode != 0 or self.qr_done or frame_id == self.qr_last_frame:
            return []
        self.qr_last_frame = frame_id
        tasks = set(task for task in (parse_task(p) for p in payloads) if task is not None)
        if len(tasks) != 1:
            self.qr_candidate,self.qr_hits = None,0
            return []
        task = tasks.pop()
        self.qr_hits = self.qr_hits+1 if task == self.qr_candidate else 1
        self.qr_candidate = task
        if self.qr_hits < cfg.QR_STABLE_FRAMES:
            return []
        self.qr_done = True
        self.task_text = '+'.join(''.join(str(d) for d in task[i:i+3]) for i in (0,3,6,9))
        self.task_plan = expand_task(self.task_text)
        return [wire.frame(wire.TASKCODE,task)]
