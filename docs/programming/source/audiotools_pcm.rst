:mod:`audiotools.pcm` --- the PCM FrameList Module
==================================================

.. module:: audiotools.pcm
   :synopsis: the PCM FrameList Module


The :mod:`audiotools.pcm` module contains the FrameList and FloatFrameList
classes for handling blobs of raw data.
These classes are immutable and list-like, but provide several additional
methods and attributes to aid in processing PCM data.

.. function:: from_list(list, channels, bits_per_sample, is_signed)

   Given a list of integer values, a number of channels,
   the amount of bits-per-sample and whether the samples are signed,
   returns a new :class:`FrameList` object with those values.
   Raises :exc:`ValueError` if a :class:`FrameList` cannot be built
   from those values.

   >>> f = from_list([-1,0,1,2],2,16,True)
   >>> list(f)
   [-1, 0, 1, 2]

.. function:: from_frames(frame_list)

   Given a list of :class:`FrameList` objects, returns a new
   :class:`FrameList` whose values are built from those objects.
   Raises :exc:`ValueError` if any of the objects are longer than
   1 PCM frame, their number of channels are not consistent
   or their bits_per_sample are not consistent.

   >>> l = [from_list([-1,0],2,16,True),
   ...      from_list([ 1,2],2,16,True)]
   >>> f = from_frames(l)
   >>> list(f)
   [-1, 0, 1, 2]

.. function:: from_channels(frame_list)

   Given a list of :class:`FrameList` objects, returns a new
   :class:`FrameList` whose values are built from those objects.
   Raises :exc:`ValueError` if any of the objects are wider than
   1 channel, their number of frames are not consistent
   or their bits_per_sample are not consistent.

   >>> l = [from_list([-1,1],1,16,True),
   ...      from_list([ 0,2],1,16,True)]
   >>> f = from_channels(l)
   >>> list(f)
   [-1, 0, 1, 2]

.. function:: from_float_frames(float_frame_list)

   Given a list of :class:`FloatFrameList` objects, returns a new
   :class:`FloatFrameList` whose values are built from those objects.
   Raises :exc:`ValueError` if any of the objects are longer than
   1 PCM frame or their number of channels are not consistent.

   >>> l = [FloatFrameList([-1.0,0.0],2),
   ...      FloatFrameList([ 0.5,1.0],2)]
   >>> f = from_float_frames(l)
   >>> list(f)
   [-1.0, 0.0, 0.5, 1.0]

.. function:: from_float_channels(float_frame_list)

   Given a list of :class:`FloatFrameList` objects, returns a new
   :class:`FloatFrameList` whose values are built from those objects.
   Raises :exc:`ValueError` if any of the objects are wider than
   1 channel or their number of frames are not consistent.

   >>> l = [FloatFrameList([-1.0,0.5],1),
   ...      FloatFrameList([ 0.0,1.0],1)]
   >>> f = from_float_channels(l)
   >>> list(f)
   [-1.0, 0.0, 0.5, 1.0]


FrameList Objects
-----------------

.. class:: FrameList(string, channels, bits_per_sample, is_big_endian, is_signed)

   This class implements a PCM FrameList, which can be envisioned as a
   2D array of signed integers where
   each row represents a PCM frame of samples and
   each column represents a channel.

   During initialization, ``string`` is a collection of raw bytes,
   ``bits_per_sample`` is an integer and ``is_big_endian`` and ``is_signed``
   are booleans.
   This provides a convenient way to transforming raw data from
   file-like objects into :class:`FrameList` objects.
   Once instantiated, a :class:`FrameList` object is immutable.

.. data:: FrameList.frames

   The amount of PCM frames within this object, as a non-negative integer.

.. data:: FrameList.channels

   The amount of channels within this object, as a positive integer.

.. data:: FrameList.bits_per_sample

   The size of each sample in bits, as a positive integer.

.. method:: FrameList.frame(frame_number)

   Given a non-negative ``frame_number`` integer,
   returns the samples at the given frame as a new :class:`FrameList` object.
   This new FrameList will be a single frame long, but have the same
   number of channels and bits_per_sample as the original.
   Raises :exc:`IndexError` if one tries to get a frame number outside
   this FrameList's boundaries.

.. method:: FrameList.channel(channel_number)

   Given a non-negative ``channel_number`` integer,
   returns the samples at the given channel as a new :class:`FrameList` object.
   This new FrameList will be a single channel wide, but have the same
   number of frames and bits_per_sample as the original.
   Raises :exc:`IndexError` if one tries to get a channel number outside
   this FrameList's boundaries.

.. method:: FrameList.split(frame_count)

   Returns a pair of :class:`FrameList` objects.
   The first contains up to ``frame_count`` number of PCM frames.
   The second contains the remainder.
   If ``frame_count`` is larger than the number of frames in the FrameList,
   the first will contain all of the frames and the second will be empty.

.. method:: FrameList.to_float()

   Converts this object's values to a new :class:`FloatFrameList` object
   by transforming all samples to the range -1.0 to 1.0.

.. method:: FrameList.to_bytes(is_big_endian, is_signed)

   Given ``is_big_endian`` and ``is_signed`` booleans,
   returns a plain string of raw PCM data.
   This is much like the inverse of :class:`FrameList`'s initialization
   routine.

.. method:: FrameList.frame_count(bytes)

   A convenience method which converts a given byte count to the
   maximum number of frames those bytes could contain, or a minimum of 1.

   >>> FrameList("",2,16,False,True).frame_count(8)
   2

FloatFrameList Objects
----------------------

.. class:: FloatFrameList(floats, channels)

   This class implements a FrameList of floating point samples,
   which can be envisioned as a 2D array of signed floats where
   each row represents a PCM frame of samples,
   each column represents a channel and each value is
   within the range of -1.0 to 1.0.

   During initialization, ``floats`` is a list of float values
   and ``channels`` is an integer number of channels.

.. data:: FloatFrameList.frames

   The amount of PCM frames within this object, as a non-negative integer.

.. data:: FloatFrameList.channels

   The amount of channels within this object, as a positive integer.

.. method:: FloatFrameList.frame(frame_number)

   Given a non-negative ``frame_number`` integer,
   returns the samples at the given frame as a new :class:`FloatFrameList`
   object.
   This new FloatFrameList will be a single frame long, but have the same
   number of channels as the original.
   Raises :exc:`IndexError` if one tries to get a frame number outside
   this FloatFrameList's boundaries.

.. method:: FloatFrameList.channel(channel_number)

   Given a non-negative ``channel_number`` integer,
   returns the samples at the given channel as a new :class:`FloatFrameList`
   object.
   This new FloatFrameList will be a single channel wide, but have the same
   number of frames as the original.
   Raises :exc:`IndexError` if one tries to get a channel number outside
   this FloatFrameList's boundaries.

.. method:: FloatFrameList.split(frame_count)

   Returns a pair of :class:`FloatFrameList` objects.
   The first contains up to ``frame_count`` number of PCM frames.
   The second contains the remainder.
   If ``frame_count`` is larger than the number of frames in the
   FloatFrameList, the first will contain all of the frames and the
   second will be empty.

.. method:: FloatFrameList.to_int(bits_per_sample)

   Given a ``bits_per_sample`` integer, converts this object's
   floating point values to a new :class:`FrameList` object.

