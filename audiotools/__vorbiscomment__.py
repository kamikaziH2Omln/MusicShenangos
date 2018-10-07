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


from audiotools import MetaData, Con, VERSION, re


class VorbisComment(MetaData, dict):
    """A complete Vorbis Comment tag."""

    VORBIS_COMMENT = Con.Struct(
        "vorbis_comment",
        Con.PascalString("vendor_string",
                         length_field=Con.ULInt32("length")),
        Con.PrefixedArray(
            length_field=Con.ULInt32("length"),
            subcon=Con.PascalString("value",
                                    length_field=Con.ULInt32("length"))),
        Con.Const(Con.Byte("framing"), 1))

    ATTRIBUTE_MAP = {'track_name': 'TITLE',
                     'track_number': 'TRACKNUMBER',
                     'track_total': 'TRACKTOTAL',
                     'album_name': 'ALBUM',
                     'artist_name': 'ARTIST',
                     'performer_name': 'PERFORMER',
                     'composer_name': 'COMPOSER',
                     'conductor_name': 'CONDUCTOR',
                     'media': 'SOURCE MEDIUM',
                     'ISRC': 'ISRC',
                     'catalog': 'CATALOG',
                     'copyright': 'COPYRIGHT',
                     'publisher': 'PUBLISHER',
                     'year': 'DATE',
                     'album_number': 'DISCNUMBER',
                     'album_total': 'DISCTOTAL',
                     'comment': 'COMMENT'}

    ITEM_MAP = dict(map(reversed, ATTRIBUTE_MAP.items()))

    def __init__(self, vorbis_data, vendor_string=u""):
        """Initialized with a key->[value1,value2] dict.

        keys are generally upper case.
        values are unicode string.
        vendor_string is an optional unicode string."""

        dict.__init__(self, [(key.upper(), values)
                            for (key, values) in vorbis_data.items()])
        self.vendor_string = vendor_string

    def __setitem__(self, key, value):
        dict.__setitem__(self, key.upper(), value)

    def __getattr__(self, key):
        if (key == 'track_number'):
            match = re.match(r'^\d+$',
                             self.get('TRACKNUMBER', [u''])[0])
            if (match):
                return int(match.group(0))
            else:
                match = re.match('^(\d+)/\d+$',
                                 self.get('TRACKNUMBER', [u''])[0])
                if (match):
                    return int(match.group(1))
                else:
                    return 0
        elif (key == 'track_total'):
            match = re.match(r'^\d+$',
                             self.get('TRACKTOTAL', [u''])[0])
            if (match):
                return int(match.group(0))
            else:
                match = re.match('^\d+/(\d+)$',
                                 self.get('TRACKNUMBER', [u''])[0])
                if (match):
                    return int(match.group(1))
                else:
                    return 0
        elif (key == 'album_number'):
            match = re.match(r'^\d+$',
                             self.get('DISCNUMBER', [u''])[0])
            if (match):
                return int(match.group(0))
            else:
                match = re.match('^(\d+)/\d+$',
                                 self.get('DISCNUMBER', [u''])[0])
                if (match):
                    return int(match.group(1))
                else:
                    return 0
        elif (key == 'album_total'):
            match = re.match(r'^\d+$',
                             self.get('DISCTOTAL', [u''])[0])
            if (match):
                return int(match.group(0))
            else:
                match = re.match('^\d+/(\d+)$',
                                 self.get('DISCNUMBER', [u''])[0])
                if (match):
                    return int(match.group(1))
                else:
                    return 0
        elif (key in self.ATTRIBUTE_MAP):
            return self.get(self.ATTRIBUTE_MAP[key], [u''])[0]
        elif (key in MetaData.__FIELDS__):
            return u''
        else:
            try:
                return self.__dict__[key]
            except KeyError:
                raise AttributeError(key)

    def __delattr__(self, key):
        if (key == 'track_number'):
            track_number = self.get('TRACKNUMBER', [u''])[0]
            if (re.match(r'^\d+$', track_number)):
                del(self['TRACKNUMBER'])
            elif (re.match('^\d+/(\d+)$', track_number)):
                self['TRACKNUMBER'] = u"0/%s" % (
                    re.match('^\d+/(\d+)$', track_number).group(1))
        elif (key == 'track_total'):
            track_number = self.get('TRACKNUMBER', [u''])[0]
            if (re.match('^(\d+)/\d+$', track_number)):
                self['TRACKNUMBER'] = u"%s" % (
                    re.match('^(\d+)/\d+$', track_number).group(1))
            if ('TRACKTOTAL' in self):
                del(self['TRACKTOTAL'])
        elif (key == 'album_number'):
            album_number = self.get('DISCNUMBER', [u''])[0]
            if (re.match(r'^\d+$', album_number)):
                del(self['DISCNUMBER'])
            elif (re.match('^\d+/(\d+)$', album_number)):
                self['DISCNUMBER'] = u"0/%s" % (
                    re.match('^\d+/(\d+)$', album_number).group(1))
        elif (key == 'album_total'):
            album_number = self.get('DISCNUMBER', [u''])[0]
            if (re.match('^(\d+)/\d+$', album_number)):
                self['DISCNUMBER'] = u"%s" % (
                    re.match('^(\d+)/\d+$', album_number).group(1))
            if ('DISCTOTAL' in self):
                del(self['DISCTOTAL'])
        elif (key in self.ATTRIBUTE_MAP):
            if (self.ATTRIBUTE_MAP[key] in self):
                del(self[self.ATTRIBUTE_MAP[key]])
        elif (key in MetaData.__FIELDS__):
            pass
        else:
            try:
                del(self.__dict__[key])
            except KeyError:
                raise AttributeError(key)

    @classmethod
    def supports_images(cls):
        """Returns False."""

        #There's actually a (proposed?) standard to add embedded covers
        #to Vorbis Comments by base64 encoding them.
        #This strikes me as messy and convoluted.
        #In addition, I'd have to perform a special case of
        #image extraction and re-insertion whenever converting
        #to FlacMetaData.  The whole thought gives me a headache.

        return False

    def images(self):
        """Returns an empty list of Image objects."""

        return list()

    #if an attribute is updated (e.g. self.track_name)
    #make sure to update the corresponding dict pair
    def __setattr__(self, key, value):
        if (key in self.ATTRIBUTE_MAP):
            if (key not in MetaData.__INTEGER_FIELDS__):
                self[self.ATTRIBUTE_MAP[key]] = [value]
            else:
                self[self.ATTRIBUTE_MAP[key]] = [unicode(value)]
        else:
            self.__dict__[key] = value

    @classmethod
    def converted(cls, metadata):
        """Converts a MetaData object to a VorbisComment object."""

        if ((metadata is None) or (isinstance(metadata, VorbisComment))):
            return metadata
        elif (metadata.__class__.__name__ == 'FlacMetaData'):
            return cls(vorbis_data=dict(metadata.vorbis_comment.items()),
                       vendor_string=metadata.vorbis_comment.vendor_string)
        else:
            values = {}
            for key in cls.ATTRIBUTE_MAP.keys():
                if (key in cls.__INTEGER_FIELDS__):
                    if (getattr(metadata, key) != 0):
                        values[cls.ATTRIBUTE_MAP[key]] = \
                            [unicode(getattr(metadata, key))]
                elif (getattr(metadata, key) != u""):
                    values[cls.ATTRIBUTE_MAP[key]] = \
                        [unicode(getattr(metadata, key))]

            return VorbisComment(values)

    def merge(self, metadata):
        """Updates any currently empty entries from metadata's values."""

        metadata = self.__class__.converted(metadata)
        if (metadata is None):
            return

        for (key, values) in metadata.items():
            if ((len(values) > 0) and
                (len(self.get(key, [])) == 0)):
                self[key] = values

    def __comment_name__(self):
        return u'Vorbis'

    #takes two (key,value) vorbiscomment pairs
    #returns cmp on the weighted set of them
    #(title first, then artist, album, tracknumber, ... , replaygain)
    @classmethod
    def __by_pair__(cls, pair1, pair2):
        KEY_MAP = {"TITLE": 1,
                   "ALBUM": 2,
                   "TRACKNUMBER": 3,
                   "TRACKTOTAL": 4,
                   "DISCNUMBER": 5,
                   "DISCTOTAL": 6,
                   "ARTIST": 7,
                   "PERFORMER": 8,
                   "COMPOSER": 9,
                   "CONDUCTOR": 10,
                   "CATALOG": 11,
                   "PUBLISHER": 12,
                   "ISRC": 13,
                   "SOURCE MEDIUM": 14,
                   #"YEAR": 15,
                   "DATE": 16,
                   "COPYRIGHT": 17,
                   "REPLAYGAIN_ALBUM_GAIN": 19,
                   "REPLAYGAIN_ALBUM_PEAK": 19,
                   "REPLAYGAIN_TRACK_GAIN": 19,
                   "REPLAYGAIN_TRACK_PEAK": 19,
                   "REPLAYGAIN_REFERENCE_LOUDNESS": 20}
        return cmp((KEY_MAP.get(pair1[0].upper(), 18),
                    pair1[0].upper(),
                    pair1[1]),
                   (KEY_MAP.get(pair2[0].upper(), 18),
                    pair2[0].upper(),
                    pair2[1]))

    def __comment_pairs__(self):
        pairs = []
        for (key, values) in self.items():
            for value in values:
                pairs.append((key, value))

        pairs.sort(VorbisComment.__by_pair__)
        return pairs

    def build(self):
        """Returns this VorbisComment as a binary string."""

        comment = Con.Container(vendor_string=self.vendor_string,
                                framing=1,
                                value=[])

        for (key, values) in self.items():
            for value in values:
                if ((value != u"") and not
                    ((key in ("TRACKNUMBER", "TRACKTOTAL",
                              "DISCNUMBER", "DISCTOTAL")) and
                     (value == u"0"))):
                    comment.value.append("%s=%s" % (key,
                                                    value.encode('utf-8')))
        return self.VORBIS_COMMENT.build(comment)
