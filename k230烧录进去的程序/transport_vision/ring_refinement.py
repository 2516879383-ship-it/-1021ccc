"""YOLO gives NUMBERED STATION boxes, not scoring-ring centres.
Optional local circle-family fit on the SAME RGBP888 frame (green plane).
Only a near-front-view candidate; no innermost-ring identity or mm claim.
"""
from vision_quality import hypot
import vision_config as cfg


def family_center(circles, box):
    x, y, w, h = box
    if min(w,h)/max(w,h) < cfg.RING_MIN_ASPECT:
        return None, 'OBLIQUE_VIEW'
    candidates = []
    for cx, cy, radius in circles[:cfg.RING_MAX_CIRCLES]:
        if not all(v == v and abs(v) < 1e6 for v in (cx,cy,radius)):
            continue
        if radius <= 0 or cx-radius < x or cy-radius < y or cx+radius > x+w or cy+radius > y+h:
            continue
        if hypot(cx-(x+w/2), cy-(y+h/2)) > min(w,h)*cfg.RING_CENTER_LIMIT:
            continue
        candidates.append((cx,cy,radius))
    families = []
    for seed in candidates:
        group = [c for c in candidates if hypot(c[0]-seed[0],c[1]-seed[1]) <= cfg.RING_CENTER_SPREAD_PX]
        radii = []
        for circle in sorted(group,key=lambda c:c[2]):
            if not radii or circle[2]-radii[-1] >= cfg.RING_RADIUS_SEPARATION_PX:
                radii.append(circle[2])
        if len(radii) < cfg.RING_MIN_DISTINCT_RADII:
            continue
        cx = sum(c[0] for c in group)/len(group)
        cy = sum(c[1] for c in group)/len(group)
        if max(hypot(c[0]-cx,c[1]-cy) for c in group) > cfg.RING_CENTER_SPREAD_PX:
            continue
        if not any(hypot(cx-f[0],cy-f[1]) <= cfg.RING_CENTER_SPREAD_PX for f in families):
            families.append((cx,cy,len(radii)))
    if len(families) != 1:
        return None, 'NO_FAMILY' if not families else 'AMBIGUOUS_FAMILIES'
    return families[0], 'CIRCLE_FAMILY'


def refine(tensor, detection):
    if not cfg.RING_REFINE_ENABLED or detection is None:
        return None, 'DISABLED'
    x,y,w,h = detection[2]
    if min(w,h)/max(w,h) < cfg.RING_MIN_ASPECT:
        return None, 'OBLIQUE_VIEW'
    import image
    width,height = cfg.AI_SIZE
    # Do not snapshot another sensor channel: it could be a different frame.
    plane = tensor[1]
    gray = image.Image(width,height,image.GRAYSCALE,alloc=image.ALLOC_REF,data=plane)
    roi = (max(0,int(x)),max(0,int(y)),int(w),int(h))
    radius_max = min(int(min(w,h)/2),cfg.RING_MAX_RADIUS_PX)
    if radius_max <= cfg.RING_MIN_RADIUS_PX:
        return None, 'ROI_TOO_SMALL'
    found = gray.find_circles(roi=roi, threshold=cfg.RING_HOUGH_THRESHOLD,
                             x_margin=2,y_margin=2,r_margin=1,
                             r_min=cfg.RING_MIN_RADIUS_PX,r_max=radius_max,r_step=2)
    circles = [(float(c.x()),float(c.y()),float(c.r())) for c in found[:cfg.RING_MAX_CIRCLES]]
    return family_center(circles,detection[2])
