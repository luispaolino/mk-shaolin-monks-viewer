"""Small synthetic game containers for import tests. The XBE stays external."""
import struct


def game_files(xbe):
    files = {'default.xbe': xbe, 'WADCRC.BIN': bytes(4),
             'GAMEDATA.WAD': b'PWF ' + bytes(28)}
    for suffix in 'EFGHIJK':
        files['GAMEDATA.WA' + suffix] = b'fixture-data'
    for movie in ('midway', 'opening', 'end006', 'end011', 'final'):
        files[f'front/movies/{movie}.sfd'] = b'\x00\x00\x01\xba' + bytes(12)
    return files


def write_game_folder(folder, files):
    for name, payload in files.items():
        path = folder / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)


def write_game_image(path, files):
    directories = {'': {}, 'front': {}, 'front/movies': {}}
    sectors = {name: 33 + index for index, name in enumerate(directories)}
    for name in files:
        parent, _, child = name.rpartition('/')
        directories[parent][child] = name
    directories['']['front'] = 'front'
    directories['front']['movies'] = 'front/movies'
    image = bytearray(36 * 2048)
    volume = 32 * 2048
    magic = b'MICROSOFT*XBOX*MEDIA'
    image[volume:volume + 20] = magic
    image[volume + 0x7ec:volume + 2048] = magic
    struct.pack_into('<II', image, volume + 20, 33, 2048)
    for directory, children in directories.items():
        at = sectors[directory] * 2048
        names = sorted(children)
        for index, child in enumerate(names):
            name = children[child]
            encoded = child.encode('ascii')
            length = (14 + len(encoded) + 3) & ~3
            right = (at % 2048 + length) // 4 if index + 1 < len(names) else 0
            if name in sectors:
                sector, size, attributes = sectors[name], 2048, 0x10
            else:
                payload = files[name]
                sector, size, attributes = len(image) // 2048, len(payload), 0x20
                image.extend(payload)
                image.extend(bytes((-len(image)) % 2048))
            struct.pack_into('<HHIIBB', image, at, 0, right, sector, size, attributes, len(encoded))
            image[at + 14:at + 14 + len(encoded)] = encoded
            at += length
    path.write_bytes(image)
