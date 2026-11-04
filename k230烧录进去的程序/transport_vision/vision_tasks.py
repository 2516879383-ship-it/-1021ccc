"""Validate/expand task text for inspection, not autonomous task execution."""


def parse_task(payload):
    if isinstance(payload, bytes):
        try:
            payload = payload.decode('utf-8')
        except UnicodeError:
            return None
    if not isinstance(payload,str):
        return None
    parts = payload.strip().split('+')
    if len(parts) != 4 or any(len(p) != 3 for p in parts):
        return None
    if any(ch not in '123456' for ch in ''.join(parts)):
        return None
    if sorted(parts[1]) != ['1','2','3'] or sorted(parts[3]) != ['1','2','3']:
        return None
    if len(set(parts[0])) != 3 or sorted(parts[0]) != sorted(parts[2]):
        return None
    return bytes([int(ch) for ch in ''.join(parts)])


def expand_task(payload):
    digits = parse_task(payload)
    if digits is None:
        raise ValueError('Invalid competition task code')
    base = dict(zip(digits[:3],digits[3:6]))
    rows = []
    for batch in range(2):
        for order in range(3):
            color = digits[batch*6+order]
            rows.append({'batch':batch+1,'order':order+1,'color':color,
                         'rough_slot':digits[batch*6+3+order],
                         'storage_slot':base[color],'layer':batch+1})
    return rows
