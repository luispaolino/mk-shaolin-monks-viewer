"""Encode the generated viewer PNG as native icon and About image resources.

Requires Pillow only when regenerating artwork; normal CMake builds use the
checked-in ICO and RGBA files and do not need an image-processing dependency.
"""
import argparse
from pathlib import Path
import struct

from PIL import Image


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=root / 'resources/viewer-icon.png')
    parser.add_argument('--output-dir', type=Path, default=root / 'resources')
    args = parser.parse_args()
    with Image.open(args.source) as source:
        image = source.convert('RGBA')
    if image.width != image.height or image.width < 256:
        parser.error('The icon source must be square and at least 256 pixels wide')
    if image.getchannel('A').getextrema() != (0, 255):
        parser.error('The icon source must contain both transparent and opaque pixels')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    sizes = [(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)]
    image.save(args.output_dir / 'viewer.ico', sizes=sizes)
    about = image.resize((256, 256), Image.Resampling.LANCZOS)
    # Fixed little-endian dimensions followed by straight-alpha RGBA8 pixels.
    (args.output_dir / 'viewer-icon.rgba').write_bytes(
        struct.pack('<II', about.width, about.height) + about.tobytes())


if __name__ == '__main__':
    main()
