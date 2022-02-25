import re
# skeleton from https://github.com/simon-engledew/python-chunks/blob/master/chunks/__init__.py
def _from_pattern(pattern, type, *args):
    def coerce(value):
        value = str(value)
        match = pattern.search(value)
        if match is not None:
            return type(match.group(1), *args)
        raise ValueError('unable to coerce "%s" into a %s' %
                         (value, type.__name__))
    return coerce

to_megabytes = lambda n: n * 1024 * 1024
to_hex = _from_pattern(re.compile('([-+]?[0-9A-F]+)', re.IGNORECASE), int, 16)


def decode(fileobj, chunk_limit=to_megabytes(5)):
    """ Removes chunked encoding from fileobj """
    hexsizestr = ''
    len_size_bytes = 0
    while True:
        firstbyte = fileobj.read(1)
        if firstbyte == '\r':
            nextbyte = fileobj.read(1)
            assert nextbyte == '\n'
        else:
            len_size_bytes += 1
            hexsizestr += firstbyte
            if len_size_bytes > 100:
                raise OverflowError('too many digits...')
            continue

        length = to_hex(hexsizestr)
        hexsizestr = ''
        len_size_bytes = 0

        if length > chunk_limit:
            raise OverflowError(
                'invalid chunk size of "%d" requested, max is "%d"' % (length, chunk_limit))

        value = fileobj.read(length)

        assert len(value) == length

        yield value

        tail = fileobj.read(2)

        if not tail:
            raise ValueError('missing \\r\\n after chunk')

        assert tail == '\r\n', 'unexpected characters "%s" after chunk' % tail

        if not length:
            return
