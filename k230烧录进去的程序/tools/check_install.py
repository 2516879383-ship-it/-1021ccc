"""No hardware access. CanMV: run as-is. PC: python tools/check_install.py ROOT."""
import os
import sys
try:
    import ujson as json
except ImportError:
    import json
try:
    import uhashlib as hashlib
except ImportError:
    import hashlib
import binascii


def check(root='/sdcard'):
    root = root.rstrip('/\\')
    package = root+'/transport_vision'
    if package not in sys.path:
        sys.path.insert(0,package)
    from vision_settings import validate
    import vision_config as cfg
    validate()
    required = ['main.py','libs/YOLO.py','libs/AIBase.py','libs/AI2D.py',
                'libs/Utils.py','res/font/SourceHanSansSC-Normal-Min.ttf']
    required += ['transport_vision/'+name+'.py' for name in
                 ('vision_app','vision_config','vision_logic','vision_protocol',
                  'vision_quality','vision_tasks','vision_settings','ring_refinement')]
    for relative in required:
        if os.stat(root+'/'+relative)[6] <= 0:
            raise ValueError('Missing/empty file: '+relative)
    with open(package+'/models/manifest.json','rb') as stream:
        manifest = json.loads(stream.read().decode('utf-8'))
    expected = [(cfg.COLOR_MODEL,cfg.COLOR_LABELS,cfg.COLOR_IDS),
                (cfg.RING_MODEL,cfg.RING_LABELS,cfg.RING_IDS)]
    if len(manifest['models']) != 2:
        raise ValueError('Manifest must contain both models')
    for entry,(path,labels,ids) in zip(manifest['models'],expected):
        if path != '/sdcard/'+entry['path'] or entry['labels'] != labels or entry['ids'] != list(ids):
            raise ValueError('Model path/label mapping does not match manifest')
        filename = root+'/'+entry['path']
        if os.stat(filename)[6] != entry['bytes']:
            raise ValueError('Model size mismatch: '+filename)
        digest = hashlib.sha256()
        with open(filename,'rb') as stream:
            while True:
                chunk = stream.read(4096)
                if not chunk:break
                digest.update(chunk)
        actual = binascii.hexlify(digest.digest()).decode()
        if actual != entry['sha256']:
            raise ValueError('Model hash mismatch: '+filename)
        print('MODEL_OK',entry['path'],actual)
    print('INSTALL_OK','UART',cfg.UART_ID,cfg.UART_TX_IO,cfg.UART_RX_IO,
          'PICK_CALIBRATED',cfg.PICK_CALIBRATED,'RING_CALIBRATED',cfg.RING_REFINEMENT_CALIBRATED)


if __name__ == '__main__':
    check(sys.argv[1] if len(sys.argv)>1 else '/sdcard')
