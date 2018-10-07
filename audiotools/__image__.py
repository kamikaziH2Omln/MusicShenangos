#!/usr/bin/python

#Audio Tools, a module and set of tools for manipulating audio data
#Copyright (C) 2007-2010  Brian Langenberger

#This program is free software; you can redistribute it and/or modify
#it under the terms of the GNU General Public License as published by
#the Free Software Foundation; either version 2 of the License, or
#(at your option) any later version.

#This program is distributed in the hope that it will be useful,
#but WITHOUT ANY WARRANTY; without even the implied warranty of
#MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#GNU General Public License for more details.

#You should have received a copy of the GNU General Public License
#along with this program; if not, write to the Free Software
#Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

from audiotools import Con
import imghdr
import cStringIO
import gettext

gettext.install("audiotools", unicode=True)


def __jpeg__(h, f):
    if (h[0:3] == "FFD8FF".decode('hex')):
        return 'jpeg'
    else:
        return None


imghdr.tests.append(__jpeg__)


def image_metrics(file_data):
    """Returns an ImageMetrics subclass from a string of file data.

    Raises InvalidImage if there is an error parsing the file
    or its type is unknown."""

    header = imghdr.what(None, file_data)

    file = cStringIO.StringIO(file_data)
    try:
        if (header == 'jpeg'):
            return __JPEG__.parse(file)
        elif (header == 'png'):
            return __PNG__.parse(file)
        elif (header == 'gif'):
            return __GIF__.parse(file)
        elif (header == 'bmp'):
            return __BMP__.parse(file)
        elif (header == 'tiff'):
            return __TIFF__.parse(file)
        else:
            raise InvalidImage(_(u'Unknown image type'))
    finally:
        file.close()


#######################
#JPEG
#######################


class ImageMetrics:
    """A container for image data."""

    def __init__(self, width, height, bits_per_pixel, color_count, mime_type):
        """Fields are as follows:

        width          - image width as an integer number of pixels
        height         - image height as an integer number of pixels
        bits_per_pixel - the number of bits per pixel as an integer
        color_count    - for palette-based images, the total number of colors
        mime_type      - the image's MIME type, as a string

        All of the ImageMetrics subclasses implement these fields.
        In addition, they all implement a parse() classmethod
        used to parse binary string data and return something
        ImageMetrics compatible.
        """

        self.width = width
        self.height = height
        self.bits_per_pixel = bits_per_pixel
        self.color_count = color_count
        self.mime_type = mime_type

    def __repr__(self):
        return "ImageMetrics(%s,%s,%s,%s,%s)" % \
               (repr(self.width),
                repr(self.height),
                repr(self.bits_per_pixel),
                repr(self.color_count),
                repr(self.mime_type))


class InvalidImage(Exception):
    """Raised if an image cannot be parsed correctly."""

    def __init__(self, err):
        self.err = unicode(err)

    def __unicode__(self):
        return self.err


class InvalidJPEG(InvalidImage):
    """Raised if a JPEG cannot be parsed correctly."""

    pass


class __JPEG__(ImageMetrics):
    SEGMENT_HEADER = Con.Struct('segment_header',
                                Con.Const(Con.Byte('header'), 0xFF),
                                Con.Byte('type'),
                                Con.If(
        lambda ctx: ctx['type'] not in (0xD8, 0xD9),
        Con.UBInt16('length')))

    APP0 = Con.Struct('JFIF_segment_marker',
                      Con.String('identifier', 5),
                      Con.Byte('major_version'),
                      Con.Byte('minor_version'),
                      Con.Byte('density_units'),
                      Con.UBInt16('x_density'),
                      Con.UBInt16('y_density'),
                      Con.Byte('thumbnail_width'),
                      Con.Byte('thumbnail_height'))

    SOF = Con.Struct('start_of_frame',
                     Con.Byte('data_precision'),
                     Con.UBInt16('image_height'),
                     Con.UBInt16('image_width'),
                     Con.Byte('components'))

    def __init__(self, width, height, bits_per_pixel):
        ImageMetrics.__init__(self, width, height, bits_per_pixel,
                              0, u'image/jpeg')

    @classmethod
    def parse(cls, file):
        try:
            header = cls.SEGMENT_HEADER.parse_stream(file)
            if (header.type != 0xD8):
                raise InvalidJPEG(_(u'Invalid JPEG header'))

            segment = cls.SEGMENT_HEADER.parse_stream(file)
            while (segment.type != 0xD9):
                if (segment.type == 0xDA):
                    break

                if (segment.type in (0xC0, 0xC1, 0xC2, 0xC3,
                                     0xC5, 0XC5, 0xC6, 0xC7,
                                     0xC9, 0xCA, 0xCB, 0xCD,
                                     0xCE, 0xCF)):  # start of frame
                    segment_data = cStringIO.StringIO(
                        file.read(segment.length - 2))
                    frame0 = cls.SOF.parse_stream(segment_data)
                    segment_data.close()

                    return __JPEG__(width=frame0.image_width,
                                    height=frame0.image_height,
                                    bits_per_pixel=(frame0.data_precision *
                                                    frame0.components))
                else:
                    file.seek(segment.length - 2, 1)

                segment = cls.SEGMENT_HEADER.parse_stream(file)

            raise InvalidJPEG(_(u'Start of frame not found'))
        except Con.ConstError:
            raise InvalidJPEG(_(u"Invalid JPEG segment marker at 0x%X") % \
                                  (file.tell()))


#######################
#PNG
#######################


class InvalidPNG(InvalidImage):
    """Raised if a PNG cannot be parsed correctly."""

    pass


class __PNG__(ImageMetrics):
    HEADER = Con.Const(Con.String('header', 8),
                       '89504e470d0a1a0a'.decode('hex'))
    CHUNK_HEADER = Con.Struct('chunk',
                              Con.UBInt32('length'),
                              Con.String('type', 4))
    CHUNK_FOOTER = Con.Struct('crc32',
                              Con.UBInt32('crc'))

    IHDR = Con.Struct('IHDR',
                      Con.UBInt32('width'),
                      Con.UBInt32('height'),
                      Con.Byte('bit_depth'),
                      Con.Byte('color_type'),
                      Con.Byte('compression_method'),
                      Con.Byte('filter_method'),
                      Con.Byte('interlace_method'))

    def __init__(self, width, height, bits_per_pixel, color_count):
        ImageMetrics.__init__(self, width, height, bits_per_pixel, color_count,
                              u'image/png')

    @classmethod
    def parse(cls, file):
        ihdr = None
        plte = None

        try:
            header = cls.HEADER.parse_stream(file)

            chunk_header = cls.CHUNK_HEADER.parse_stream(file)
            data = file.read(chunk_header.length)
            chunk_footer = cls.CHUNK_FOOTER.parse_stream(file)
            while (chunk_header.type != 'IEND'):
                if (chunk_header.type == 'IHDR'):
                    ihdr = cls.IHDR.parse(data)
                elif (chunk_header.type == 'PLTE'):
                    plte = data

                chunk_header = cls.CHUNK_HEADER.parse_stream(file)
                data = file.read(chunk_header.length)
                chunk_footer = cls.CHUNK_FOOTER.parse_stream(file)

            if (ihdr.color_type == 0):    # grayscale
                bits_per_pixel = ihdr.bit_depth
                color_count = 0
            elif (ihdr.color_type == 2):  # RGB
                bits_per_pixel = ihdr.bit_depth * 3
                color_count = 0
            elif (ihdr.color_type == 3):  # palette
                bits_per_pixel = 8
                if ((len(plte) % 3) != 0):
                    raise InvalidPNG(_(u'Invalid PLTE chunk length'))
                else:
                    color_count = len(plte) / 3
            elif (ihdr.color_type == 4):  # grayscale + alpha
                bits_per_pixel = ihdr.bit_depth * 2
                color_count = 0
            elif (ihdr.color_type == 6):  # RGB + alpha
                bits_per_pixel = ihdr.bit_depth * 4
                color_count = 0

            return __PNG__(ihdr.width, ihdr.height, bits_per_pixel,
                           color_count)
        except Con.ConstError:
            raise InvalidPNG(_(u'Invalid PNG'))


#######################
#BMP
#######################


class InvalidBMP(InvalidImage):
    """Raised if a BMP cannot be parsed correctly."""

    pass


class __BMP__(ImageMetrics):
    HEADER = Con.Struct('bmp_header',
                        Con.Const(Con.String('magic_number', 2), 'BM'),
                        Con.ULInt32('file_size'),
                        Con.ULInt16('reserved1'),
                        Con.ULInt16('reserved2'),
                        Con.ULInt32('bitmap_data_offset'))

    INFORMATION = Con.Struct('bmp_information',
                             Con.ULInt32('header_size'),
                             Con.ULInt32('width'),
                             Con.ULInt32('height'),
                             Con.ULInt16('color_planes'),
                             Con.ULInt16('bits_per_pixel'),
                             Con.ULInt32('compression_method'),
                             Con.ULInt32('image_size'),
                             Con.ULInt32('horizontal_resolution'),
                             Con.ULInt32('vertical_resolution'),
                             Con.ULInt32('colors_used'),
                             Con.ULInt32('important_colors_used'))

    def __init__(self, width, height, bits_per_pixel, color_count):
        ImageMetrics.__init__(self, width, height, bits_per_pixel, color_count,
                              u'image/x-ms-bmp')

    @classmethod
    def parse(cls, file):
        try:
            header = cls.HEADER.parse_stream(file)
            information = cls.INFORMATION.parse_stream(file)

            return __BMP__(information.width, information.height,
                           information.bits_per_pixel,
                           information.colors_used)

        except Con.ConstError:
            raise InvalidBMP(_(u'Invalid BMP'))


#######################
#GIF
#######################


class InvalidGIF(InvalidImage):
    """Raised if a GIF cannot be parsed correctly."""

    pass


class __GIF__(ImageMetrics):
    HEADER = Con.Struct('header',
                        Con.Const(Con.String('gif', 3), 'GIF'),
                        Con.String('version', 3))

    SCREEN_DESCRIPTOR = Con.Struct('logical_screen_descriptor',
                                   Con.ULInt16('width'),
                                   Con.ULInt16('height'),
                                   Con.Embed(
        Con.BitStruct('packed_fields',
                      Con.Flag('global_color_table'),
                      Con.Bits('color_resolution', 3),
                      Con.Flag('sort'),
                      Con.Bits('global_color_table_size', 3))),
                                   Con.Byte('background_color_index'),
                                   Con.Byte('pixel_aspect_ratio'))

    def __init__(self, width, height, color_count):
        ImageMetrics.__init__(self, width, height, 8, color_count,
                              u'image/gif')

    @classmethod
    def parse(cls, file):
        try:
            header = cls.HEADER.parse_stream(file)
            descriptor = cls.SCREEN_DESCRIPTOR.parse_stream(file)

            return __GIF__(descriptor.width, descriptor.height,
                           2 ** (descriptor.global_color_table_size + 1))
        except Con.ConstError:
            raise InvalidGIF(_(u'Invalid GIF'))


#######################
#TIFF
#######################


class InvalidTIFF(InvalidImage):
    """Raised if a TIFF cannot be parsed correctly."""

    pass


class __TIFF__(ImageMetrics):
    HEADER = Con.Struct('header',
                        Con.String('byte_order', 2),
                        Con.Switch('order',
                                   lambda ctx: ctx['byte_order'],
                                   {"II": Con.Embed(
        Con.Struct('little_endian',
                   Con.Const(Con.ULInt16('version'), 42),
                   Con.ULInt32('offset'))),
                                    "MM": Con.Embed(
        Con.Struct('big_endian',
                   Con.Const(Con.UBInt16('version'), 42),
                   Con.UBInt32('offset')))}))

    L_IFD = Con.Struct('ifd',
                       Con.PrefixedArray(
        length_field=Con.ULInt16('length'),
        subcon=Con.Struct('tags',
                          Con.ULInt16('id'),
                          Con.ULInt16('type'),
                          Con.ULInt32('count'),
                          Con.ULInt32('offset'))),
                       Con.ULInt32('next'))

    B_IFD = Con.Struct('ifd',
                       Con.PrefixedArray(
        length_field=Con.UBInt16('length'),
        subcon=Con.Struct('tags',
                          Con.UBInt16('id'),
                          Con.UBInt16('type'),
                          Con.UBInt32('count'),
                          Con.UBInt32('offset'))),
                       Con.UBInt32('next'))

    def __init__(self, width, height, bits_per_pixel, color_count):
        ImageMetrics.__init__(self, width, height,
                              bits_per_pixel, color_count,
                              u'image/tiff')

    @classmethod
    def b_tag_value(cls, file, tag):
        subtype = {1: Con.Byte("data"),
                   2: Con.CString("data"),
                   3: Con.UBInt16("data"),
                   4: Con.UBInt32("data"),
                   5: Con.Struct("data",
                                 Con.UBInt32("high"),
                                 Con.UBInt32("low"))}[tag.type]

        data = Con.StrictRepeater(tag.count,
                                  subtype)
        if ((tag.type != 2) and (data.sizeof() <= 4)):
            return tag.offset
        else:
            file.seek(tag.offset, 0)
            return data.parse_stream(file)

    @classmethod
    def l_tag_value(cls, file, tag):
        subtype = {1: Con.Byte("data"),
                   2: Con.CString("data"),
                   3: Con.ULInt16("data"),
                   4: Con.ULInt32("data"),
                   5: Con.Struct("data",
                                 Con.ULInt32("high"),
                                 Con.ULInt32("low"))}[tag.type]

        data = Con.StrictRepeater(tag.count,
                                  subtype)
        if ((tag.type != 2) and (data.sizeof() <= 4)):
            return tag.offset
        else:
            file.seek(tag.offset, 0)
            return data.parse_stream(file)

    @classmethod
    def parse(cls, file):
        width = 0
        height = 0
        bits_per_sample = 0
        color_count = 0

        try:
            header = cls.HEADER.parse_stream(file)
            if (header.byte_order == 'II'):
                IFD = cls.L_IFD
                tag_value = cls.l_tag_value
            elif (header.byte_order == 'MM'):
                IFD = cls.B_IFD
                tag_value = cls.b_tag_value
            else:
                raise InvalidTIFF(_(u'Invalid byte order'))

            file.seek(header.offset, 0)

            ifd = IFD.parse_stream(file)

            while (True):
                for tag in ifd.tags:
                    if (tag.id == 0x0100):
                        width = tag_value(file, tag)
                    elif (tag.id == 0x0101):
                        height = tag_value(file, tag)
                    elif (tag.id == 0x0102):
                        try:
                            bits_per_sample = sum(tag_value(file, tag))
                        except TypeError:
                            bits_per_sample = tag_value(file, tag)
                    elif (tag.id == 0x0140):
                        color_count = tag.count / 3
                    else:
                        pass

                if (ifd.next == 0x00):
                    break
                else:
                    file.seek(ifd.next, 0)
                    ifd = IFD.parse_stream(file)

            return __TIFF__(width, height, bits_per_sample, color_count)
        except Con.ConstError:
            raise InvalidTIFF(_(u'Invalid TIFF'))


def can_thumbnail():
    """Returns True if we have the capability to thumbnail images."""

    try:
        import Image as PIL_Image
        return True
    except ImportError:
        return False


def thumbnail_formats():
    """Returns a list of available thumbnail image formats."""

    import Image as PIL_Image
    import cStringIO

    #performing a dummy save seeds PIL_Image.SAVE with possible save types
    PIL_Image.new("RGB", (1, 1)).save(cStringIO.StringIO(), "bmp")

    return PIL_Image.SAVE.keys()


def thumbnail_image(image_data, width, height, format):
    """Generates a new, smaller image from a larger one.

    image_data is a binary string.
    width and height are the requested maximum values.
    format as a binary string, such as 'JPEG'.
    """

    import cStringIO
    import Image as PIL_Image
    import ImageFile as PIL_ImageFile

    PIL_ImageFile.MAXBLOCK = 0x100000

    img = PIL_Image.open(cStringIO.StringIO(image_data))
    img.thumbnail((width, height), PIL_Image.ANTIALIAS)
    output = cStringIO.StringIO()

    if (format.upper() == 'JPEG'):
        #PIL's default JPEG save quality isn't too great
        #so it's best to add a couple of optimizing parameters
        #since this is a common case
        img.save(output, 'JPEG', quality=90, optimize=True)
    else:
        img.save(output, format)

    return output.getvalue()
