"""SD-card boot entry: both material and ring detection use YOLO11.
Copy this file to /sdcard/main.py and keep transport_vision alongside it.
"""


def main():
    import sys
    package_dir = '/sdcard/transport_vision'
    if package_dir not in sys.path:
        sys.path.insert(0, package_dir)
    from vision_app import run
    run(bench=False)


if __name__ == '__main__':
    main()
