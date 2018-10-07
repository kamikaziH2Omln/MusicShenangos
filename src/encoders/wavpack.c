#include "wavpack.h"
#include "../common/misc.h"
#include <assert.h>
#include <limits.h>

/********************************************************
 Audio Tools, a module and set of tools for manipulating audio data
 Copyright (C) 2007-2010  Brian Langenberger

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*******************************************************/

#ifndef STANDALONE
PyObject*
encoders_encode_wavpack(PyObject *dummy,
                        PyObject *args, PyObject *keywds) {
    char *filename;
    FILE *file;
    Bitstream *stream;
    PyObject *pcmreader_obj;
    struct pcm_reader *reader;

    struct wavpack_encoder_context context;
    struct ia_array samples;
    ia_size_t i;

    int block_size;
    static char *kwlist[] = {"filename",
                             "pcmreader",
                             "block_size",

                             "false_stereo",
                             "wasted_bits",
                             "joint_stereo",
                             "decorrelation_passes",
                             "wave_header",
                             "wave_footer",
                             NULL};

    /*set some default option values*/
    context.options.joint_stereo = 0;
    context.options.false_stereo = 1;
    context.options.wasted_bits = 1;
    context.options.decorrelation_passes = 0;
    context.wave.header = NULL;
    context.wave.footer = NULL;
    context.wave.header_len = 0;
    context.wave.footer_len = 0;

    if (!PyArg_ParseTupleAndKeywords(args,
                                     keywds,
                                     "sOi|iiiis#s#",
                                     kwlist,
                                     &filename,
                                     &pcmreader_obj,
                                     &block_size,

                                     &(context.options.false_stereo),
                                     &(context.options.wasted_bits),
                                     &(context.options.joint_stereo),
                                     &(context.options.decorrelation_passes),
                                     &(context.wave.header),
                                     &(context.wave.header_len),
                                     &(context.wave.footer),
                                     &(context.wave.footer_len)))
        return NULL;

    if (wavpack_verify_tunables(&context) == ERROR)
        return NULL;

    if (block_size <= 0) {
        PyErr_SetString(PyExc_ValueError, "block_size must be positive");
        return NULL;
    }

    /*open the given filename for writing*/
    if ((file = fopen(filename, "wb")) == NULL) {
        PyErr_SetFromErrnoWithFilename(PyExc_IOError, filename);
        return NULL;
    } else {
        stream = bs_open(file, BS_LITTLE_ENDIAN);
    }

    /*transform the Python PCMReader-compatible object to a pcm_reader struct*/
    if ((reader = pcmr_open(pcmreader_obj)) == NULL) {
        fclose(file);
        return NULL;
    }
#else
void
encoders_encode_wavpack(char *filename,
                        FILE *pcmdata,
                        int block_size,
                        int false_stereo,
                        int wasted_bits,
                        int joint_stereo,
                        int decorrelation_passes) {
    FILE *file;
    Bitstream *stream;
    struct pcm_reader *reader;
    struct wavpack_encoder_context context;
    struct ia_array samples;
    ia_size_t i;

    file = fopen(filename, "wb");
    stream = bs_open(file, BS_LITTLE_ENDIAN);
    reader = pcmr_open(pcmdata, 44100, 2, 0x3, 24, 0, 1);

    context.options.false_stereo = false_stereo;
    context.options.wasted_bits = wasted_bits;
    context.options.joint_stereo = joint_stereo;
    context.options.decorrelation_passes = decorrelation_passes;
    context.wave.header_written = 0;
    context.wave.header_len = 0;
    context.wave.footer_len = 0;
    context.wave.header = NULL;
    context.wave.footer = NULL;
#endif

    context.bits_per_sample = reader->bits_per_sample;
    context.sample_rate = reader->sample_rate;
    context.total_channels = reader->channels;
    context.channel_mask = reader->channel_mask;

    context.channel_info_written = 0;

    context.block_index = 0;
    context.byte_count = 0;
    ia_init(&(context.block_offsets), 1);

    context.wrap.passes = wavpack_init_decorrelation_passes(
                                                      reader->channels);


    context.cache.sub_blocks = bs_open_recorder();
    context.cache.residual_data = bs_open_recorder();
    ia_init(&(context.cache.input_A), 1);
    ia_init(&(context.cache.input_B), 1);

    audiotools__MD5Init(&(context.md5));

    context.pcm_bytes = 0;
    context.wave.header_written = 0;

    pcmr_add_callback(reader, wavpack_count_pcm_bytes, &(context.pcm_bytes),
                      context.bits_per_sample != 8, 1);
    pcmr_add_callback(reader, wavpack_md5_callback, &(context.md5),
                      context.bits_per_sample != 8, 1);
    bs_add_callback(stream, wavpack_count_bytes, &(context.byte_count));

    iaa_init(&samples, reader->channels, block_size);

    /*build frames until reader is empty
      (WavPack doesn't have actual frames as such; it has sets of
       blocks joined by first-block/last-block bits in the header.
       However, I'll call that arrangement a "frame" for clarity.)*/
    if (!pcmr_read(reader, block_size, &samples))
        goto error;

    while (samples.arrays[0].size > 0) {
        wavpack_write_frame(stream, &context, &samples, reader->channel_mask);

        if (!pcmr_read(reader, block_size, &samples))
            goto error;
    }

    /*add wave footer/MD5 sub-blocks to end of stream*/
    wavpack_write_footer_block(stream, &context);

    /*update wave header, if necessary*/
    if ((context.wave.header_len == 0) && context.wave.header_written) {
        fseek(file, context.wave.header_offset, SEEK_SET);
        wavpack_write_wave_header_sub_block(stream,
                                            &context,
                                            context.pcm_bytes);
    }

    /*go back and set block header data as necessary*/
    for (i = 0; i < context.block_offsets.size; i++) {
        fseek(file, context.block_offsets.data[i] + 12, SEEK_SET);
        stream->write_bits(stream, 32, context.block_index);
    }

    /*close open file handles and deallocate temporary space*/
    bs_close(context.cache.sub_blocks);
    bs_close(context.cache.residual_data);
    ia_free(&(context.cache.input_A));
    ia_free(&(context.cache.input_B));

    ia_free(&(context.block_offsets));
    wavpack_free_decorrelation_passes(context.wrap.passes,
                                      context.total_channels);
    pcmr_close(reader);
    bs_close(stream);
    iaa_free(&samples);

#ifndef STANDALONE
    Py_INCREF(Py_None);
    return Py_None;
#else
    return;
#endif

 error:
    bs_close(context.cache.sub_blocks);
    bs_close(context.cache.residual_data);
    ia_free(&(context.cache.input_A));
    ia_free(&(context.cache.input_B));

    ia_free(&(context.block_offsets));
    wavpack_free_decorrelation_passes(context.wrap.passes,
                                      context.total_channels);
    pcmr_close(reader);
    bs_close(stream);
    iaa_free(&samples);

#ifndef STANDALONE
    return NULL;
#endif
}

static int
count_bits(int i) {
    int bits;

    for (bits = 0; i != 0; i >>= 1)
        bits++;

    return bits;
}


void
wavpack_write_frame(Bitstream *bs,
                    struct wavpack_encoder_context *context,
                    struct ia_array *samples,
                    long channel_mask) {
    struct i_array counts;
    int current_channel;
    int i;

    ia_init(&counts, 1);

    channel_mask_splits(&counts, samples->size, channel_mask);

    for (i = current_channel = 0; i < counts.size; i++) {
        wavpack_write_block(bs,
                            context,
                            &(samples->arrays[current_channel]),
                            counts.data[i] == 2 ?
                            &(samples->arrays[current_channel + 1]) :
                            NULL,
                            i,
                            counts.data[i],
                            i == 0,
                            i == (counts.size - 1));
        current_channel += counts.data[i];
    }

    context->block_index += samples->arrays[0].size;
    ia_free(&counts);
}

ia_data_t
wavpack_abs_maximum(ia_data_t sample, ia_data_t current_max) {
    return MAX(abs(sample), current_max);
}

static inline uint32_t
wavpack_crc(ia_data_t sample, uint32_t crc) {
    return ((3 * crc) + sample) & 0xFFFFFFFF;
}

void
wavpack_initialize_block_header(struct wavpack_block_header* header,
                                struct wavpack_encoder_context* context,
                                int channel_count,
                                int pcm_frames,
                                int first_block,
                                int last_block) {
    assert((channel_count == 1) || (channel_count == 2));

    /*must set block_size*/
    header->version = WAVPACK_VERSION;
    header->track_number = 0;
    header->index_number = 0;
    header->total_samples = 0;         /*placeholder*/
    header->block_index = context->block_index;
    header->block_samples = pcm_frames;
    header->bits_per_sample = context->bits_per_sample;
    header->mono_output = channel_count == 1;
    header->hybrid_mode = 0;
    header->joint_stereo = 0;
    header->cross_channel_decorrelation = 0;
    header->hybrid_noise_shaping = 0;
    header->floating_point_data = 0;
    header->extended_size_integers = 0;
    header->hybrid_parameters_control_bitrate = 0;
    header->hybrid_noise_balanced = 0;
    header->initial_block_in_sequence = first_block;
    header->final_block_in_sequence = last_block;
    header->left_shift = 0;
    header->maximum_data_magnitude = 0;
    header->sample_rate = context->sample_rate;
    header->use_IIR = 0;
    header->false_stereo = 0;
    header->crc = 0xFFFFFFFF;
}

void
wavpack_write_block(Bitstream* bs,
                    struct wavpack_encoder_context* context,
                    struct i_array* channel_A,
                    struct i_array* channel_B,
                    int channel_number,
                    int channel_count,
                    int first_block,
                    int last_block) {
    struct wavpack_block_header block_header;

    struct i_array decorrelation_terms;
    struct i_array decorrelation_deltas;
    struct i_array decorrelation_weights_A;
    struct i_array decorrelation_weights_B;
    struct ia_array decorrelation_samples_A;
    struct ia_array decorrelation_samples_B;
    struct i_array entropy_variables_A;
    struct i_array entropy_variables_B;
    int wasted_bits = 0;

    Bitstream* sub_blocks = context->cache.sub_blocks;
    int i;

    bs_reset_recorder(sub_blocks);

    ia_init(&decorrelation_terms, 1);
    ia_init(&decorrelation_deltas, 1);
    ia_init(&decorrelation_weights_A, 1);
    ia_init(&decorrelation_weights_B, 1);
    iaa_init(&decorrelation_samples_A,
             context->options.decorrelation_passes, 0);
    iaa_init(&decorrelation_samples_B,
             context->options.decorrelation_passes, 0);
    ia_init(&entropy_variables_A, 3);
    ia_init(&entropy_variables_B, 3);

    ia_append(&(context->block_offsets), context->byte_count);

    /*initialize the WavPack block header fields*/

    wavpack_initialize_block_header(&block_header,
                                    context,
                                    channel_count,
                                    channel_A->size,
                                    first_block,
                                    last_block);

    assert((channel_count == 1) || (channel_count == 2));

    if (channel_count == 1)
        block_header.maximum_data_magnitude = count_bits(
                        ia_reduce(channel_A, 0, wavpack_abs_maximum));
    else
        block_header.maximum_data_magnitude = MAX(
            count_bits(ia_reduce(channel_A, 0, wavpack_abs_maximum)),
            count_bits(ia_reduce(channel_B, 0, wavpack_abs_maximum)));

    /*determine false stereo*/
    if (context->options.false_stereo &&
        (channel_count == 2) &&
        ia_equal(channel_A, channel_B)) {
        block_header.false_stereo = 1;
    } else {
        block_header.false_stereo = 0;
    }

    /*determine extended integers (wasted bits), if any*/
    if (context->options.wasted_bits) {
        if ((channel_count == 2) &&
            !block_header.false_stereo) {
            wasted_bits = MIN(wavpack_max_wasted_bits_per_sample(channel_A),
                              wavpack_max_wasted_bits_per_sample(channel_B));
            if (wasted_bits > 0) {
                for (i = 0; i < channel_A->size; i++) {
                    channel_A->data[i] >>= wasted_bits;
                    channel_B->data[i] >>= wasted_bits;
                }
            }
            block_header.extended_size_integers = 1;
        } else {
            wasted_bits = wavpack_max_wasted_bits_per_sample(channel_A);
            if (wasted_bits > 0) {
                for (i = 0; i < channel_A->size; i++) {
                    channel_A->data[i] >>= wasted_bits;
                }
            }
            block_header.extended_size_integers = 1;
        }
    } else {
        block_header.extended_size_integers = 0;
    }

    /*calculate checksum of data after application of
      false stereo and extended integers,
      but before application of joint stereo.*/
    if ((channel_count == 1) || block_header.false_stereo) {
        for (i = 0; i < channel_A->size; i++) {
            block_header.crc = wavpack_crc(channel_A->data[i],
                                           block_header.crc);
        }
    } else {
        for (i = 0; i < channel_A->size; i++) {
            block_header.crc = wavpack_crc(channel_B->data[i],
                                           wavpack_crc(channel_A->data[i],
                                                       block_header.crc));
        }
    }

    /*perform joint stereo calculation if possible and requested*/
    if (context->options.joint_stereo &&
        (channel_count == 2) &&
        !block_header.false_stereo) {
        wavpack_perform_joint_stereo(channel_A, channel_B);
        block_header.joint_stereo = 1;
    } else {
        block_header.joint_stereo = 0;
    }

    /*The cross channel decorrelation bit seems to be set
      any time the number of block channels is 2
      regardless of whether there's any decorrelation terms.
      I don't think decoders use it at all.*/
    block_header.cross_channel_decorrelation = (channel_count == 2);

    /*assign tunables for block data*/
    wavpack_calculate_tunables(context,
                               channel_A,
                               channel_B,
                               channel_number,
                               channel_count -
                               block_header.false_stereo,
                               &decorrelation_terms,
                               &decorrelation_deltas,
                               &decorrelation_weights_A,
                               &decorrelation_weights_B,
                               &decorrelation_samples_A,
                               &decorrelation_samples_B,
                               &entropy_variables_A,
                               &entropy_variables_B);

    if (!context->wave.header_written) {
        if (context->wave.header_len == 0) {
            context->wave.header_offset = context->byte_count + 32;
            wavpack_write_wave_header_sub_block(sub_blocks, context, 0);
        } else {
            wavpack_write_subblock_header(sub_blocks, WV_WAVE_HEADER, 1,
                                          context->wave.header_len);
            for (i = 0; i < context->wave.header_len; i++)
                sub_blocks->write_bits(sub_blocks, 8, context->wave.header[i]);
        }
        context->wave.header_written = 1;
    }

    if (decorrelation_terms.size > 0) {
        wavpack_write_decorr_terms(sub_blocks,
                                   &decorrelation_terms,
                                   &decorrelation_deltas);

        wavpack_write_decorr_weights(sub_blocks,
                                     channel_count -
                                     block_header.false_stereo,
                                     decorrelation_terms.size,
                                     &decorrelation_weights_A,
                                     &decorrelation_weights_B);

        wavpack_write_decorr_samples(sub_blocks,
                                     channel_count -
                                     block_header.false_stereo,
                                     &decorrelation_terms,
                                     &decorrelation_samples_A,
                                     &decorrelation_samples_B);
    }

    if (block_header.extended_size_integers) {
        wavpack_write_int32_info(sub_blocks, 0, wasted_bits, 0, 0);
    }

    wavpack_write_entropy_variables(sub_blocks,
                                    &entropy_variables_A,
                                    &entropy_variables_B,
                                    channel_count -
                                    block_header.false_stereo);

    if (!context->channel_info_written &&
        (context->total_channels > 2)) {
        wavpack_write_channel_info(sub_blocks,
                                   context->total_channels,
                                   context->channel_mask);
        context->channel_info_written = 1;
    }

    /*apply decorrelation passes to samples in reverse order, if requested*/
    for (i = decorrelation_terms.size - 1; i >= 0; i--) {
        wavpack_perform_decorrelation_pass(
                                        channel_A,
                                        channel_B,
                                        decorrelation_terms.data[i],
                                        decorrelation_deltas.data[i],
                                        &(decorrelation_weights_A.data[i]),
                                        &(decorrelation_weights_B.data[i]),
                                        &(decorrelation_samples_A.arrays[i]),
                                        &(decorrelation_samples_B.arrays[i]),
                                        channel_count -
                                        block_header.false_stereo,
                                        context);

        /*and place the last few decorrelated samples into the
          decorrelation_samples buffer for the next block*/
        wavpack_wrap_decorrelation_samples(
                                        &(decorrelation_samples_A.arrays[i]),
                                        &(decorrelation_samples_B.arrays[i]),
                                        decorrelation_terms.data[i],
                                        channel_A,
                                        channel_B,
                                        channel_count -
                                        block_header.false_stereo);
    }

    wavpack_write_residuals(sub_blocks,
                            channel_A,
                            channel_B,
                            &entropy_variables_A,
                            &entropy_variables_B,
                            channel_count -
                            block_header.false_stereo,
                            context);

    wavpack_store_tunables(context,
                           channel_number,
                           channel_count -
                           block_header.false_stereo,
                           &decorrelation_terms,
                           &decorrelation_deltas,
                           &decorrelation_weights_A,
                           &decorrelation_weights_B,
                           &decorrelation_samples_A,
                           &decorrelation_samples_B,
                           &entropy_variables_A,
                           &entropy_variables_B);

    /*update block header fields*/
    block_header.block_size = 24 + (sub_blocks->bits_written / 8);

    /*write block header*/
    wavpack_write_block_header(bs, &block_header);

    /*write sub-block data*/
    bs_dump_records(bs, sub_blocks);

    /*clear temporary space*/
    ia_free(&decorrelation_terms);
    ia_free(&decorrelation_deltas);
    ia_free(&decorrelation_weights_A);
    ia_free(&decorrelation_weights_B);
    iaa_free(&decorrelation_samples_A);
    iaa_free(&decorrelation_samples_B);
    ia_free(&entropy_variables_A);
    ia_free(&entropy_variables_B);
}

void
wavpack_write_block_header(Bitstream *bs,
                           struct wavpack_block_header *header) {
    bs->write_bits64(bs, 32, 0x6B707677); /*block header*/
    bs->write_bits64(bs, 32, header->block_size);
    bs->write_bits(bs, 16, header->version);
    bs->write_bits(bs, 8,  header->track_number);
    bs->write_bits(bs, 8,  header->index_number);
    bs->write_bits64(bs, 32, header->total_samples);
    bs->write_bits64(bs, 32, header->block_index);
    bs->write_bits64(bs, 32, header->block_samples);
    switch (header->bits_per_sample) {
    case 8:  bs->write_bits(bs, 2, 0); break;
    case 16: bs->write_bits(bs, 2, 1); break;
    case 24: bs->write_bits(bs, 2, 2); break;
    case 32: bs->write_bits(bs, 2, 3); break;
    }
    bs->write_bits(bs, 1,  header->mono_output);
    bs->write_bits(bs, 1,  header->hybrid_mode);
    bs->write_bits(bs, 1,  header->joint_stereo);
    bs->write_bits(bs, 1,  header->cross_channel_decorrelation);
    bs->write_bits(bs, 1,  header->hybrid_noise_shaping);
    bs->write_bits(bs, 1,  header->floating_point_data);
    bs->write_bits(bs, 1,  header->extended_size_integers);
    bs->write_bits(bs, 1,  header->hybrid_parameters_control_bitrate);
    bs->write_bits(bs, 1,  header->hybrid_noise_balanced);
    bs->write_bits(bs, 1,  header->initial_block_in_sequence);
    bs->write_bits(bs, 1,  header->final_block_in_sequence);
    bs->write_bits(bs, 5,  header->left_shift);
    bs->write_bits(bs, 5,  header->maximum_data_magnitude);
    switch (header->sample_rate) {
    case 6000:   bs->write_bits(bs, 4, 0x0); break;
    case 8000:   bs->write_bits(bs, 4, 0x1); break;
    case 9600:   bs->write_bits(bs, 4, 0x2); break;
    case 11025:  bs->write_bits(bs, 4, 0x3); break;
    case 12000:  bs->write_bits(bs, 4, 0x4); break;
    case 16000:  bs->write_bits(bs, 4, 0x5); break;
    case 22050:  bs->write_bits(bs, 4, 0x6); break;
    case 24000:  bs->write_bits(bs, 4, 0x7); break;
    case 32000:  bs->write_bits(bs, 4, 0x8); break;
    case 44100:  bs->write_bits(bs, 4, 0x9); break;
    case 48000:  bs->write_bits(bs, 4, 0xA); break;
    case 64000:  bs->write_bits(bs, 4, 0xB); break;
    case 88200:  bs->write_bits(bs, 4, 0xC); break;
    case 96000:  bs->write_bits(bs, 4, 0xD); break;
    case 192000: bs->write_bits(bs, 4, 0xE); break;
    default:     bs->write_bits(bs, 4, 0xF); break;
    }
    bs->write_bits(bs, 2,  0);
    bs->write_bits(bs, 1,  header->use_IIR);
    bs->write_bits(bs, 1,  header->false_stereo);
    bs->write_bits(bs, 1,  0);
    bs->write_bits64(bs, 32, header->crc);
}

void
wavpack_write_subblock_header(Bitstream *bs,
                              wv_metadata_function metadata_function,
                              uint8_t nondecoder_data,
                              uint32_t block_size) {
    bs->write_bits(bs, 5, metadata_function);
    bs->write_bits(bs, 1, nondecoder_data);
    bs->write_bits(bs, 1, block_size % 2);

    /*convert block_size bytes to WavPack's 16-bit block size field*/
    block_size = (block_size / 2) + (block_size % 2);

    if (block_size > 0xFF) {
        bs->write_bits(bs, 1,  1);
        bs->write_bits(bs, 24, block_size);
    } else {
        bs->write_bits(bs, 1,  0);
        bs->write_bits(bs, 8,  block_size);
    }
}

void
wavpack_write_decorr_terms(Bitstream *bs,
                           struct i_array* decorr_terms,
                           struct i_array* decorr_deltas) {
    int i;

    wavpack_write_subblock_header(bs, 2, 0, decorr_terms->size);

    for (i = decorr_terms->size - 1; i >= 0; i--) {
        bs->write_bits(bs, 5, decorr_terms->data[i] + 5);
        bs->write_bits(bs, 3, decorr_deltas->data[i]);
    }

    if ((decorr_terms->size % 2) == 1)
        bs->write_bits(bs, 8, 0);
}

static int
wavpack_store_weight(int weight) {
    weight = MIN(MAX(weight, -1024), 1024);

    if (weight > 0) {
        weight -= (weight + 64) >> 7;
        return (weight + 4) >> 3;
    } else {
        return (weight + 4) >> 3;
    }
}

static int
wavpack_restore_weight(int weight) {
    if (weight > 0) {
        return (weight << 3) + (((weight << 3) + 64) >> 7);
    } else {
        return weight << 3;
    }
}

void
wavpack_write_decorr_weights(Bitstream *bs,
                             int channel_count,
                             int term_count,
                             struct i_array* weights_A,
                             struct i_array* weights_B) {
    int i;
    int block_size = weights_A->size +
        (channel_count > 1 ? weights_B->size : 0);

    assert((channel_count == 1) || (channel_count == 2));

    wavpack_write_subblock_header(bs, 3, 0, block_size);

    /*FIXME - don't write 0 weights, as per reference encoder*/

    for (i = weights_A->size - 1; i >= 0; i--) {
        bs->write_signed_bits(bs, 8,
                              wavpack_store_weight(weights_A->data[i]));
        if (channel_count > 1)
            bs->write_signed_bits(bs, 8,
                                  wavpack_store_weight(weights_B->data[i]));

    }

    if ((block_size % 2) == 1)
        bs->write_bits(bs, 8, 0);
}

void
wavpack_write_int32_info(Bitstream *bs,
                         uint8_t sent_bits,
                         uint8_t zeroes,
                         uint8_t ones,
                         uint8_t dupes) {
    wavpack_write_subblock_header(bs, WV_INT32_INFO, 0, 4);
    bs->write_bits(bs, 8, sent_bits);
    bs->write_bits(bs, 8, zeroes);
    bs->write_bits(bs, 8, ones);
    bs->write_bits(bs, 8, dupes);
}

void
wavpack_write_channel_info(Bitstream *bs,
                           int channel_count,
                           int channel_mask) {
    Bitstream* sub_block = bs_open_recorder();

    sub_block->write_bits(sub_block, 8, channel_count);
    sub_block->write_bits(sub_block, count_bits(channel_mask), channel_mask);
    sub_block->byte_align(sub_block);
    while (sub_block->bits_written % 16)
        sub_block->write_bits(sub_block, 8, 0);

    wavpack_write_subblock_header(bs, WV_CHANNEL_INFO, 0,
                                  sub_block->bits_written / 8);
    bs_dump_records(bs, sub_block);
    bs_close(sub_block);
}

void
wavpack_write_decorr_samples(Bitstream *bs,
                             int channel_count,
                             struct i_array* decorr_terms,
                             struct ia_array* samples_A,
                             struct ia_array* samples_B) {
    int i;
    int k;
    ia_data_t term;
    struct i_array* term_samples_A;
    struct i_array* term_samples_B;
    int sub_block_size = 0;

    assert((channel_count == 1) || (channel_count == 2));

    /*calculate the sub-block's total size*/
    for (i = decorr_terms->size - 1; i >= 0; i--) {
        term = decorr_terms->data[i];
        if ((17 <= term) && (term <= 18)) {
            sub_block_size += (4 * channel_count);
        } else if ((1 <= term) && (term <= 8)) {
            sub_block_size += (2 * term * channel_count);
        } else if ((-3 <= term) && (term <= -1)) {
            sub_block_size += 4;
        }
    }

    wavpack_write_subblock_header(bs, 4, 0, sub_block_size);

    /*FIXME - avoid writing 0 samples, as per reference encoder*/

    if (channel_count == 2) {  /*2 channel block*/
        for (i = decorr_terms->size - 1; i >= 0; i--) {
            term = decorr_terms->data[i];
            term_samples_A = &(samples_A->arrays[i]);
            term_samples_B = &(samples_B->arrays[i]);

            if ((term == 17) || (term == 18)) {
                assert(term_samples_A->size >= 2);
                assert(term_samples_B->size >= 2);

                bs->write_signed_bits(bs, 16,
                                      wavpack_log2(term_samples_A->data[1]));
                bs->write_signed_bits(bs, 16,
                                      wavpack_log2(term_samples_A->data[0]));
                bs->write_signed_bits(bs, 16,
                                      wavpack_log2(term_samples_B->data[1]));
                bs->write_signed_bits(bs, 16,
                                      wavpack_log2(term_samples_B->data[0]));
            } else if ((1 <= term) && (term <= 8)) {
                assert(term_samples_A->size >= term);
                assert(term_samples_B->size >= term);
                for (k = 0; k < term; k++) {
                    bs->write_signed_bits(bs, 16,
                                    wavpack_log2(term_samples_A->data[k]));
                    bs->write_signed_bits(bs, 16,
                                    wavpack_log2(term_samples_B->data[k]));
                }
            } else if ((-3 <= term) && (term <= -1)) {
                assert(term_samples_A->size >= 1);
                assert(term_samples_B->size >= 1);
                bs->write_signed_bits(bs, 16,
                                      wavpack_log2(term_samples_B->data[0]));
                bs->write_signed_bits(bs, 16,
                                      wavpack_log2(term_samples_A->data[0]));
            } else {
                assert(0); /*unsupported term*/
            }
        }
    } else {                        /*1 channel block*/
        for (i = decorr_terms->size - 1; i >= 0; i--) {
            term = decorr_terms->data[i];
            term_samples_A = &(samples_A->arrays[i]);

            if ((17 <= term) && (term <= 18)) {
                assert(term_samples_A->size >= 2);

                bs->write_signed_bits(bs, 16,
                                      wavpack_log2(term_samples_A->data[1]));
                bs->write_signed_bits(bs, 16,
                                      wavpack_log2(term_samples_A->data[0]));
            } else if ((1 <= term) && (term <= 8)) {
                assert(term_samples_A->size >= term);

                for (k = 0; k < term; k++) {
                    bs->write_signed_bits(bs, 16,
                                    wavpack_log2(term_samples_A->data[k]));
                }
            } else {
                assert(0); /*unsupported term*/
            }
        }
    }
}

int32_t
wavpack_log2(int32_t sample) {
    int32_t asample = abs(sample) + (abs(sample) >> 9);
    int bitcount = count_bits(asample);
    static const uint8_t log2_table[] = {
        0x00, 0x01, 0x03, 0x04, 0x06, 0x07, 0x09, 0x0a,
        0x0b, 0x0d, 0x0e, 0x10, 0x11, 0x12, 0x14, 0x15,
        0x16, 0x18, 0x19, 0x1a, 0x1c, 0x1d, 0x1e, 0x20,
        0x21, 0x22, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2a,
        0x2c, 0x2d, 0x2e, 0x2f, 0x31, 0x32, 0x33, 0x34,
        0x36, 0x37, 0x38, 0x39, 0x3b, 0x3c, 0x3d, 0x3e,
        0x3f, 0x41, 0x42, 0x43, 0x44, 0x45, 0x47, 0x48,
        0x49, 0x4a, 0x4b, 0x4d, 0x4e, 0x4f, 0x50, 0x51,
        0x52, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
        0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63,
        0x64, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c,
        0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x74, 0x75,
        0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d,
        0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
        0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d,
        0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95,
        0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9b, 0x9c,
        0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4,
        0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xa9, 0xaa, 0xab,
        0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb2,
        0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xb9,
        0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc0,
        0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc6, 0xc7,
        0xc8, 0xc9, 0xca, 0xcb, 0xcb, 0xcc, 0xcd, 0xce,
        0xcf, 0xd0, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd4,
        0xd5, 0xd6, 0xd7, 0xd8, 0xd8, 0xd9, 0xda, 0xdb,
        0xdc, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe0, 0xe1,
        0xe2, 0xe3, 0xe4, 0xe4, 0xe5, 0xe6, 0xe7, 0xe7,
        0xe8, 0xe9, 0xea, 0xea, 0xeb, 0xec, 0xed, 0xee,
        0xee, 0xef, 0xf0, 0xf1, 0xf1, 0xf2, 0xf3, 0xf4,
        0xf4, 0xf5, 0xf6, 0xf7, 0xf7, 0xf8, 0xf9, 0xf9,
        0xfa, 0xfb, 0xfc, 0xfc, 0xfd, 0xfe, 0xff, 0xff};

    if ((0 <= asample) && (asample < 256) && (sample >= 0)) {
        return (bitcount << 8) + log2_table[(asample << (9 - bitcount)) % 256];
    } else if ((256 <= asample) && (sample >= 0)) {
        return (bitcount << 8) + log2_table[(asample >> (bitcount - 9)) % 256];
    } else if ((0 <= asample) && (asample < 256) && (sample < 0)) {
        return -((bitcount << 8) +
                 log2_table[(asample << (9 - bitcount)) % 256]);
    } else if ((256 <= asample) && (sample < 0)) {
        return -((bitcount << 8) +
                 log2_table[(asample >> (bitcount - 9)) % 256]);
    }

    assert(0);
    return 0;  /*it shouldn't be possible to get here*/
}

int
wavpack_exp2(int log) {
    int value;
    static const uint8_t exp2_table[] = {
        0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05,
        0x06, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0a, 0x0b,
        0x0b, 0x0c, 0x0d, 0x0e, 0x0e, 0x0f, 0x10, 0x10,
        0x11, 0x12, 0x13, 0x13, 0x14, 0x15, 0x16, 0x16,
        0x17, 0x18, 0x19, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
        0x1d, 0x1e, 0x1f, 0x20, 0x20, 0x21, 0x22, 0x23,
        0x24, 0x24, 0x25, 0x26, 0x27, 0x28, 0x28, 0x29,
        0x2a, 0x2b, 0x2c, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x35, 0x36,
        0x37, 0x38, 0x39, 0x3a, 0x3a, 0x3b, 0x3c, 0x3d,
        0x3e, 0x3f, 0x40, 0x41, 0x41, 0x42, 0x43, 0x44,
        0x45, 0x46, 0x47, 0x48, 0x48, 0x49, 0x4a, 0x4b,
        0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x51, 0x52,
        0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
        0x5b, 0x5c, 0x5d, 0x5e, 0x5e, 0x5f, 0x60, 0x61,
        0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
        0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71,
        0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
        0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81,
        0x82, 0x83, 0x84, 0x85, 0x87, 0x88, 0x89, 0x8a,
        0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92,
        0x93, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b,
        0x9c, 0x9d, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4,
        0xa5, 0xa6, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad,
        0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb6, 0xb7,
        0xb8, 0xb9, 0xba, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0,
        0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc8, 0xc9, 0xca,
        0xcb, 0xcd, 0xce, 0xcf, 0xd0, 0xd2, 0xd3, 0xd4,
        0xd6, 0xd7, 0xd8, 0xd9, 0xdb, 0xdc, 0xdd, 0xde,
        0xe0, 0xe1, 0xe2, 0xe4, 0xe5, 0xe6, 0xe8, 0xe9,
        0xea, 0xec, 0xed, 0xee, 0xf0, 0xf1, 0xf2, 0xf4,
        0xf5, 0xf6, 0xf8, 0xf9, 0xfa, 0xfc, 0xfd, 0xff};

    if (log < 0) {
        return -wavpack_exp2(-log);
    }

    value = exp2_table[log & 0xFF] | 0x100;
    log >>= 8;
    if (log <= 9) {
        return (value >> (9 - log));
    } else {
        return (value << (log - 9));
    }
}

void
wavpack_write_entropy_variables(Bitstream *bs,
                                struct i_array *variables_A,
                                struct i_array *variables_B,
                                int channel_count) {
    assert((channel_count == 1) || (channel_count == 2));

    wavpack_write_subblock_header(bs, 5, 0, 6 * channel_count);
    bs->write_signed_bits(bs, 16, wavpack_log2(variables_A->data[0]));
    bs->write_signed_bits(bs, 16, wavpack_log2(variables_A->data[1]));
    bs->write_signed_bits(bs, 16, wavpack_log2(variables_A->data[2]));
    if (channel_count > 1) {
        bs->write_signed_bits(bs, 16, wavpack_log2(variables_B->data[0]));
        bs->write_signed_bits(bs, 16, wavpack_log2(variables_B->data[1]));
        bs->write_signed_bits(bs, 16, wavpack_log2(variables_B->data[2]));
    }
}

void
wavpack_write_residuals(Bitstream *bs,
                        struct i_array* channel_A,
                        struct i_array* channel_B,
                        struct i_array* medians_A,
                        struct i_array* medians_B,
                        int channel_count,
                        struct wavpack_encoder_context* context) {
    Bitstream* residual_data = context->cache.residual_data;
    ia_size_t total_samples = channel_A->size * channel_count;
    ia_size_t sample;
    int current_channel;

    int32_t residual;
    struct wavpack_residual residual_accumulator;
    struct i_array *channels[] = {channel_A, channel_B};
    struct i_array *medians[] = {medians_A, medians_B};

    assert((channel_count == 1) || (channel_count == 2));

    bs_reset_recorder(residual_data);

    /*initialize our running median values*/
    residual_accumulator.zeroes.present =
        residual_accumulator.golomb.present =
        residual_accumulator.input_holding_one = 0;
    residual_accumulator.output_holding_zero =
        residual_accumulator.output_holding_one = 0;
    residual_accumulator.input_holding_zero = 1;

    for (sample = 0; sample < total_samples; sample++) {
        current_channel = sample % channel_count;
        residual = channels[current_channel]->data[sample / channel_count];
        wavpack_write_residual(residual_data,
                               &residual_accumulator,
                               medians,
                               current_channel,
                               residual);
    }

    if (residual_accumulator.input_holding_zero) {
        residual_accumulator.output_holding_zero = 0;
        residual_accumulator.output_holding_one = 0;
    } else {
        residual_accumulator.output_holding_zero = 0;
        residual_accumulator.output_holding_one = 1;
    }

    wavpack_flush_residual(residual_data,
                           &residual_accumulator);

    /*once all the residual data has been written,
      pad the output buffer to a multiple of 16 bits*/
    while ((residual_data->bits_written % 16) != 0)
        residual_data->write_bits(residual_data, 1, 1);

    /*write the sub-block header*/
    wavpack_write_subblock_header(bs, WV_BITSTREAM, 0,
                                  residual_data->bits_written / 8);

    /*write out the residual data*/
    bs_dump_records(bs, residual_data);
}

/*The actual median values are stored as fractions of integers.
  This chops off the fractional portion and returns its
  integer value which has a minimum value of 1.*/
static inline int32_t
get_median(struct i_array *medians, int i) {
    return (medians->data[i] >> 4) + 1;
}

static inline void
inc_median(struct i_array *medians, int i) {
    medians->data[i] += (((medians->data[i] + (128 >> i)) /
                          (128 >> i)) * 5);
}

static inline void
dec_median(struct i_array *medians, int i) {
    medians->data[i] -= (((medians->data[i] + (128 >> i) - 2) /
                          (128 >> i)) * 2);
}

void
wavpack_write_residual(Bitstream* bs,
                       struct wavpack_residual* residual_accumulator,
                       struct i_array** medians_pair,
                       int current_channel,
                       ia_data_t value) {
    struct wavpack_residual residual;
    struct i_array* medians = medians_pair[current_channel];
    int ones_count;
    int high;
    int low;
    int max_code;
    int code;
    int bit_count;
    int extras;

    assert((residual_accumulator->input_holding_zero == 0) ||
           (residual_accumulator->input_holding_zero == 1));
    assert((residual_accumulator->input_holding_one == 0) ||
           (residual_accumulator->input_holding_one == 1));
    assert((medians_pair[0]->size > 0));
    assert((medians_pair[1]->size > 0));

    if ((medians_pair[0]->data[0] < 2) &&
        (medians_pair[1]->data[0] < 2)) {
        /*we may need to handle a block of zeroes in some fashion*/

        if (residual_accumulator->zeroes.present &&
            !(residual_accumulator->golomb.present)) {
            if (value == 0) {
                /*we're continuing an existing block of zeroes*/
                residual_accumulator->zeroes.count++;
                return;
            } else {
                /*we're finishing an existing block of zeroes*/
                residual.zeroes.present = 0;
            }
        } else {
            if ((residual_accumulator->input_holding_zero == 1) &&
                (residual_accumulator->input_holding_one == 0)) {
                if (value == 0) {
                    /*we're beginning a new block of zeroes,
                      so flush the previous residual before beginning*/
                    residual_accumulator->output_holding_zero = 0;
                    residual_accumulator->output_holding_one = 0;
                    wavpack_flush_residual(bs, residual_accumulator);

                    residual.zeroes.present = 1;
                    residual.golomb.present = 0;
                    residual.zeroes.count = 1;
                    residual.input_holding_zero = 0;
                    residual.input_holding_one = 0;
                    residual.output_holding_zero = 0; /*placeholder*/
                    residual.output_holding_one = 0;  /*placeholder*/

                    /*Some placeholders to prevent compiler warnings
                      since none of these will be used unless
                      golomb.present is 1.*/
                    residual.golomb.value = INT_MAX;
                    residual.golomb.unary =
                        residual.golomb.fixed_value =
                        residual.golomb.fixed_size =
                        residual.golomb.has_extra_bit =
                        residual.golomb.extra_bit =
                        residual.golomb.sign = -1;

                    *residual_accumulator = residual;

                    wavpack_clear_medians(medians_pair[0],
                                          medians_pair[1],
                                          2);
                    return;
                } else {
                    /*generate a "false-alarm" escape code of zeroes*/
                    residual.zeroes.present = 1;
                    residual.zeroes.count = 0;
                    residual.input_holding_zero = 0;
                    residual.input_holding_one = 0;
                }
            } else
                residual.zeroes.present = 0;
        }
    } else
        residual.zeroes.present = 0;

    residual.golomb.present = 1;
    residual.golomb.value = value;

    /*more placeholders for values that should be set
      by the next call to write_residual*/
    residual.output_holding_one =
        residual.output_holding_zero = -1;

    /*Determine sign bit.*/
    if (value < 0) {
        residual.golomb.sign = 1;
        value = -value - 1;
    } else
        residual.golomb.sign = 0;

    /*Next, figure out which medians our value falls between
      and get the "ones_count", "low" and "high" values.
      Note that "ones_count" is the unary-0 value preceeding
      each coded residual, per the documentation:

    | range                                             | prob. | coding    |
    |---------------------------------------------------+-------+-----------|
    |              0 <= residual < m(0)                 | 1/2   | 0(ab)S    |
    |           m(0) <= residual < m(0)+m(1)            | 1/4   | 10(ab)S   |
    |      m(0)+m(1) <= residual < m(0)+m(1)+m(2)       | 1/8   | 110(ab)S  |
    | m(0)+m(1)+m(2) <= residual < m(0)+m(1)+(2 * m(2)) | 1/16  | 1110(ab)S |
    |                      ...                          | ...   | ...       |

    "high" and "low" are the medians on each side of the residual.
    At the same time, increment or decrement medians as necessary.
    However, don't expect to send out the "ones_count" value as-is -
    one must adjust its value based on the *next* residual's unary value
    such that "holding_zero" and "holding_one" are set correctly.
    */
    if (value < get_median(medians, 0)) {
        /*value below the 1st median*/

        ones_count = 0;
        low = 0;
        high = get_median(medians, 0) - 1;
        dec_median(medians, 0);
    } else if ((value - get_median(medians, 0)) < get_median(medians, 1)) {
        /*value between the 1st and 2nd medians*/

        ones_count = 1;
        low = get_median(medians, 0);
        high = low + get_median(medians, 1) - 1;
        inc_median(medians, 0);
        dec_median(medians, 1);
    } else if ((value - (get_median(medians, 0) +
                         get_median(medians, 1))) < get_median(medians, 2)) {
        /*value between the 2nd and 3rd medians*/

        ones_count = 2;
        low = get_median(medians, 0) + get_median(medians, 1);
        high = low + get_median(medians, 2) - 1;
        inc_median(medians, 0);
        inc_median(medians, 1);
        dec_median(medians, 2);
    } else {
        /*value above the 3rd median*/
        ones_count = 2 + ((value - (get_median(medians, 0) +
                                    get_median(medians, 1))) /
                          get_median(medians, 2));
        low = (get_median(medians, 0) +
               get_median(medians, 1)) + ((ones_count - 2) *
                                          get_median(medians, 2));
        high = low + get_median(medians, 2) - 1;
        inc_median(medians, 0);
        inc_median(medians, 1);
        inc_median(medians, 2);
    }

    residual.golomb.unary = ones_count;

    /*Then, calculate our fixed value, its size and any extra bit.*/
    if (high != low) {
        max_code = high - low;
        code = value - low;
        bit_count = count_bits(max_code);
        extras = (1 << bit_count) - max_code - 1;

        if (code < extras) {
            residual.golomb.fixed_value = code;
            residual.golomb.fixed_size = bit_count - 1;
            residual.golomb.has_extra_bit = 0;
            residual.golomb.extra_bit = -1; /*placeholder*/
        } else {
            residual.golomb.fixed_value = (code + extras) >> 1;
            residual.golomb.fixed_size = bit_count - 1;
            residual.golomb.has_extra_bit = 1;
            residual.golomb.extra_bit = (code + extras) & 1;
        }
    } else {
        residual.golomb.fixed_value = 0;
        residual.golomb.fixed_size = 0;
        residual.golomb.has_extra_bit = 0;
        residual.golomb.extra_bit = -1; /*placeholder*/
    }

    /*Next, determine the residual accumulator's output holding values
      and our new residual's input holding values
      based on their unary values.*/
    if (residual_accumulator->golomb.present) {
        if ((residual_accumulator->golomb.unary > 0) &&
            (residual.golomb.unary > 0)) {
            /*going from positive unary to positive unary,
              so set holding_one = 1 and holding_zero = 0*/
            residual_accumulator->output_holding_zero =
                residual.input_holding_zero = 0;
            residual_accumulator->output_holding_one =
                residual.input_holding_one = 1;

        } else if ((residual_accumulator->golomb.unary == 0) &&
                   (residual.golomb.unary > 0)) {
            /*going from zero unary to positive unary,
              so set holding_zero = 0 and holding_one to 0 or 1*/
            residual_accumulator->output_holding_zero =
                residual.input_holding_zero = 0;
            residual_accumulator->output_holding_one =
                residual.input_holding_one =
                !(residual_accumulator->input_holding_zero);

        } else if ((residual_accumulator->golomb.unary > 0) &&
                   (residual.golomb.unary == 0)) {
            /*going from positive unary to zero unary,
              so set holding_one = 0 and holding_zero = 1*/
            residual_accumulator->output_holding_zero =
                residual.input_holding_zero = 1;
            residual_accumulator->output_holding_one =
                residual.input_holding_one = 0;

        } else {
            /*going from zero unary to zero unary,
             so set holding_zero to 0 or 1 and holding_one to 0*/
            residual_accumulator->output_holding_zero =
                residual.input_holding_zero =
                !(residual_accumulator->input_holding_zero);
            residual_accumulator->output_holding_one =
                residual.input_holding_one = 0;
        }
    } else {
        /*previous residual has no golomb value,
          so its holding_zero and holding_one must be 0*/
        assert(residual_accumulator->output_holding_zero == 0);
        assert(residual_accumulator->output_holding_one == 0);
        residual.input_holding_zero = 0;
        residual.input_holding_one = 0;
    }

    /*Flush the residual accumulator to the stream.*/
    wavpack_flush_residual(bs, residual_accumulator);

    /*And replace the accumulator with our new residual.*/
    *residual_accumulator = residual;
}

void
wavpack_perform_joint_stereo(struct i_array *channel_A,
                             struct i_array *channel_B) {
    ia_size_t i;
    ia_data_t mid;
    ia_data_t side;

    for (i = 0; i < channel_A->size; i++) {
        side = (channel_A->data[i] + channel_B->data[i]) >> 1;
        mid = channel_A->data[i] - channel_B->data[i];
        channel_A->data[i] = mid;
        channel_B->data[i] = side;
    }
}


void
wavpack_clear_medians(struct i_array *medians_A,
                      struct i_array *medians_B,
                      int channel_count) {
    assert((channel_count == 1) || (channel_count == 2));

    medians_A->data[0] = 0;
    medians_A->data[1] = 0;
    medians_A->data[2] = 0;
    if (channel_count > 1) {
        medians_B->data[0] = 0;
        medians_B->data[1] = 0;
        medians_B->data[2] = 0;
    }
}


void
wavpack_write_egc(Bitstream* bs, int value) {
    int fixed_size;

    assert(value > 0);
    fixed_size = count_bits(value) - 1;
    bs->write_unary(bs, 0, fixed_size + 1);
    bs->write_bits(bs, fixed_size, value % (1 << fixed_size));
}

void
wavpack_flush_residual(Bitstream *bs,
                       struct wavpack_residual *residual) {
    int unary;

    /*sanity checks*/
    assert((residual->input_holding_zero == 0) ||
           (residual->input_holding_zero == 1));
    assert((residual->input_holding_one == 0) ||
           (residual->input_holding_one == 1));
    assert((residual->output_holding_zero == 0) ||
           (residual->output_holding_zero == 1));
    assert((residual->output_holding_one == 0) ||
           (residual->output_holding_one == 1));

    if (residual->zeroes.present) {
        if (residual->zeroes.count == 0) {
            /*a "false-alarm" block of zero residuals*/
            bs->write_unary(bs, 0, 0);
        } else {
            /*a typical block of zeroes case*/
            wavpack_write_egc(bs, residual->zeroes.count);
        }
    }

    if (residual->golomb.present) {
        if (residual->input_holding_zero == 0) {
            /*only write unary if input_holding_zero is 0*/

            /*adjust unary based on input_holding_one and
              output holding_zero/holding_one values*/
            if (residual->input_holding_one == 0) {
                if (residual->output_holding_one == 1) {
                    /*H0-0/H1-0 -> H0-0/H1-1*/
                    assert(residual->output_holding_zero == 0);
                    assert(residual->golomb.unary >= 0);
                    unary = (residual->golomb.unary * 2) + 1;
                } else {
                    /*H0-0/H1-0 -> H0-1/H1-0*/
                    assert(residual->output_holding_zero == 1);
                    assert(residual->golomb.unary >= 0);
                    unary = residual->golomb.unary * 2;
                }
            } else {
                if (residual->output_holding_one == 1) {
                    /*H0-0/H1-1 -> H0-0/H1-1*/
                    assert(residual->output_holding_zero == 0);
                    assert(residual->golomb.unary > 0);
                    unary = (residual->golomb.unary * 2) - 1;
                } else {
                    /*H0-0/H1-1 -> H0-1/H1-0*/
                    assert(residual->output_holding_zero == 1);
                    assert(residual->golomb.unary > 0);
                    unary = (residual->golomb.unary - 1) * 2;
                }
            }

            assert(unary >= 0);

            if (unary >= WV_UNARY_LIMIT) {
                /*generate an escape code if the unary value is too large*/
                bs->write_unary(bs, 0, 16);
                unary -= WV_UNARY_LIMIT;
                if (unary > 1) {
                    wavpack_write_egc(bs, unary);
                } else {
                    bs->write_unary(bs, 0, unary);
                }
            } else {
                bs->write_unary(bs, 0, unary);
            }
        } else {
            /*skip unary generation altogether*/
            assert(residual->golomb.unary == 0);
            assert(residual->input_holding_one == 0);
            assert(residual->output_holding_zero == 0);
            assert(residual->output_holding_one == 0);
        }

        /*then generate the fixed value bits*/
        if (residual->golomb.fixed_size > 0)
            bs->write_bits(bs,
                           residual->golomb.fixed_size,
                           residual->golomb.fixed_value);
        if (residual->golomb.has_extra_bit)
            bs->write_bits(bs, 1, residual->golomb.extra_bit);
        bs->write_bits(bs, 1, residual->golomb.sign);
    }
}


void
wavpack_print_residual(FILE* output,
                       struct wavpack_residual* residual) {
    if (residual->zeroes.present) {
        fprintf(output, "zeroes %d , ",
                residual->zeroes.count);
    }
    if (residual->golomb.present) {
        fprintf(output, "[%d] , ", residual->golomb.value);
        if (residual->input_holding_zero == 0)
            fprintf(output, "unary %d , ", residual->golomb.unary);
        if (residual->golomb.fixed_size > 0) {
            fprintf(output, "fixed %d (%d bits) , ",
                    residual->golomb.fixed_size,
                    residual->golomb.fixed_value);
        }
        if (residual->golomb.has_extra_bit) {
            fprintf(output, "extra %d , ", residual->golomb.has_extra_bit);
        }
        fprintf(output, "sign %d , ", residual->golomb.sign);
    }
    fprintf(output, "(H0 %d / H1 %d -> H0 %d / H1 %d)",
            residual->input_holding_zero,
            residual->input_holding_one,
            residual->output_holding_zero,
            residual->output_holding_one);
}


static inline int
apply_weight(int weight, int64_t sample) {
    return ((weight * sample) + 512) >> 10;
}

static inline int
update_weight(int64_t source, int result, int delta) {
    if ((source == 0) || (result == 0))
        return 0;
    else if ((source ^ result) >= 0)
        return delta;
    else
        return -delta;
}

void wavpack_perform_decorrelation_pass(
                                    struct i_array* channel_A,
                                    struct i_array* channel_B,
                                    int decorrelation_term,
                                    int decorrelation_delta,
                                    int* decorrelation_weight_A,
                                    int* decorrelation_weight_B,
                                    struct i_array* decorrelation_samples_A,
                                    struct i_array* decorrelation_samples_B,
                                    int channel_count,
                                    struct wavpack_encoder_context* context) {
    struct i_array* input_A = &(context->cache.input_A);
    struct i_array* input_B = &(context->cache.input_B);
    ia_data_t temp_A;
    ia_data_t temp_B;
    ia_size_t i;
    int weight_A;
    int weight_B;

    assert((channel_count == 1) || (channel_count == 2));

    if (channel_count == 1) {
        assert(decorrelation_term > 0);
        wavpack_perform_decorrelation_pass_1ch(channel_A,
                                               decorrelation_term,
                                               decorrelation_delta,
                                               decorrelation_weight_A,
                                               decorrelation_samples_A,
                                               context);
    } else if (decorrelation_term >= 1) {
        wavpack_perform_decorrelation_pass_1ch(channel_A,
                                               decorrelation_term,
                                               decorrelation_delta,
                                               decorrelation_weight_A,
                                               decorrelation_samples_A,
                                               context);
        wavpack_perform_decorrelation_pass_1ch(channel_B,
                                               decorrelation_term,
                                               decorrelation_delta,
                                               decorrelation_weight_B,
                                               decorrelation_samples_B,
                                               context);
    } else {
        weight_A = *decorrelation_weight_A;
        weight_B = *decorrelation_weight_B;

        ia_reset(input_A);
        ia_reset(input_B);
        ia_extend(input_A, decorrelation_samples_A);
        ia_extend(input_A, channel_A);
        ia_extend(input_B, decorrelation_samples_B);
        ia_extend(input_B, channel_B);
        ia_reset(channel_A);
        ia_reset(channel_B);

        switch (decorrelation_term) {
        case -1:
            for (i = decorrelation_samples_A->size;
                 i < input_A->size; i++) {
                temp_A = input_B->data[i - 1];
                temp_B = input_A->data[i];

                /*apply weight*/
                ia_append(channel_A,
                          input_A->data[i] -
                          apply_weight(weight_A, temp_A));

                /*update weight*/
                weight_A = MAX(MIN(
                                weight_A +
                                update_weight(temp_A,
                                              ia_getitem(channel_A, -1),
                                              decorrelation_delta),
                                WEIGHT_MAXIMUM), WEIGHT_MINIMUM);



                /*apply weight*/
                ia_append(channel_B,
                          input_B->data[i] -
                          apply_weight(weight_B, temp_B));

                /*update weight*/
                weight_B = MAX(MIN(
                                weight_B +
                                update_weight(temp_B,
                                              ia_getitem(channel_B, -1),
                                              decorrelation_delta),
                                WEIGHT_MAXIMUM), WEIGHT_MINIMUM);
            }
            break;
        case -2:
            for (i = decorrelation_samples_A->size;
                 i < input_A->size; i++) {
                temp_A = input_B->data[i];
                temp_B = input_A->data[i - 1];

                /*apply weight*/
                ia_append(channel_A,
                          input_A->data[i] -
                          apply_weight(weight_A, temp_A));

                /*update weight*/
                weight_A = MAX(MIN(
                                weight_A +
                                update_weight(temp_A,
                                              ia_getitem(channel_A, -1),
                                              decorrelation_delta),
                                WEIGHT_MAXIMUM), WEIGHT_MINIMUM);



                /*apply weight*/
                ia_append(channel_B,
                          input_B->data[i] -
                          apply_weight(weight_B, temp_B));

                /*update weight*/
                weight_B = MAX(MIN(
                                weight_B +
                                update_weight(temp_B,
                                              ia_getitem(channel_B, -1),
                                              decorrelation_delta),
                                WEIGHT_MAXIMUM), WEIGHT_MINIMUM);
            }
            break;
        case -3:
            for (i = decorrelation_samples_A->size;
                 i < input_A->size; i++) {
                temp_A = input_B->data[i - 1];
                temp_B = input_A->data[i - 1];

                /*apply weight*/
                ia_append(channel_A,
                          input_A->data[i] -
                          apply_weight(weight_A, temp_A));

                /*update weight*/
                weight_A = MAX(MIN(
                                weight_A +
                                update_weight(temp_A,
                                              ia_getitem(channel_A, -1),
                                              decorrelation_delta),
                                WEIGHT_MAXIMUM), WEIGHT_MINIMUM);



                /*apply weight*/
                ia_append(channel_B,
                          input_B->data[i] -
                          apply_weight(weight_B, temp_B));

                /*update weight*/
                weight_B = MAX(MIN(
                                weight_B +
                                update_weight(temp_B,
                                              ia_getitem(channel_B, -1),
                                              decorrelation_delta),
                                WEIGHT_MAXIMUM), WEIGHT_MINIMUM);
            }
            break;
        }

        /*send back new weights to caller*/
        *decorrelation_weight_A = weight_A;
        *decorrelation_weight_B = weight_B;
    }
}

void wavpack_perform_decorrelation_pass_1ch(
                                    struct i_array* channel,
                                    int decorrelation_term,
                                    int decorrelation_delta,
                                    int* decorrelation_weight,
                                    struct i_array* decorrelation_samples,
                                    struct wavpack_encoder_context* context) {
    struct i_array* input = &(context->cache.input_A);
    int64_t temp;
    ia_size_t i;
    int weight = *decorrelation_weight;

    ia_reset(input);
    ia_extend(input, decorrelation_samples);
    ia_extend(input, channel);
    ia_reset(channel);

    switch (decorrelation_term) {
    case 18:
        for (i = decorrelation_samples->size;
             i < input->size; i++) {
            temp = ((3 * input->data[i - 1]) -
                    input->data[i - 2]) >> 1;
            ia_append(channel, input->data[i] - apply_weight(weight, temp));
            weight += update_weight(temp,
                                    ia_getitem(channel, -1),
                                    decorrelation_delta);
        }
        break;
    case 17:
        for (i = decorrelation_samples->size;
             i < input->size; i++) {
            temp = (2 * input->data[i - 1]) - input->data[i - 2];
            ia_append(channel, input->data[i] - apply_weight(weight, temp));
            weight += update_weight(temp,
                                    ia_getitem(channel, -1),
                                    decorrelation_delta);
        }
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
        for (i = decorrelation_samples->size;
             i < input->size; i++) {
            temp = input->data[i - decorrelation_term];
            ia_append(channel, input->data[i] - apply_weight(weight, temp));
            weight += update_weight(temp,
                                    ia_getitem(channel, -1),
                                    decorrelation_delta);
        }
        break;
    default:
        assert(0); /*unsupported decorrelation term*/
        break;
    }

    *decorrelation_weight = weight;
}

status
wavpack_verify_tunables(struct wavpack_encoder_context* context) {
    switch (context->options.decorrelation_passes) {
    case 0:
    case 1:
    case 2:
    case 5:
    case 10:
    case 16:
        break;
    default:
#ifndef STANDALONE
        PyErr_SetString(PyExc_ValueError,
                        "unsupported number of decorrelation passes");
#endif
        return ERROR;
    }

    return OK;
}

void
wavpack_calculate_tunables(struct wavpack_encoder_context* context,
                           struct i_array* channel_A,
                           struct i_array* channel_B,
                           int channel_number,
                           int channel_count,
                           struct i_array* decorrelation_terms,
                           struct i_array* decorrelation_deltas,
                           struct i_array* decorrelation_weights_A,
                           struct i_array* decorrelation_weights_B,
                           struct ia_array* decorrelation_samples_A,
                           struct ia_array* decorrelation_samples_B,
                           struct i_array* entropy_variables_A,
                           struct i_array* entropy_variables_B) {
    ia_size_t i;
    struct i_array* samples_A;
    struct i_array* samples_B;
    struct wavpack_decorrelation_pass* wrapped_pass;
    int decorrelation_passes;

    assert((channel_count == 1) || (channel_count == 2));

    if (channel_count == 2)
        decorrelation_passes = context->options.decorrelation_passes;
    else
        decorrelation_passes = MIN(5, context->options.decorrelation_passes);

    wrapped_pass = &(context->wrap.passes[channel_number]);

    if ((decorrelation_passes > 0) &&
        (wrapped_pass->decorrelation_terms.size == decorrelation_passes) &&
        (wrapped_pass->channel_count == channel_count)) {
        /*If the wrapped passes for the given channel number
          has the same channel count and number of passes,
          pull values from the previous pass.*/

        ia_copy(decorrelation_terms,
                &(wrapped_pass->decorrelation_terms));
        ia_copy(decorrelation_deltas,
                &(wrapped_pass->decorrelation_deltas));
        ia_copy(decorrelation_weights_A,
                &(wrapped_pass->decorrelation_weights_A));

        for (i = 0; i < decorrelation_passes; i++)
            ia_copy(&(decorrelation_samples_A->arrays[i]),
                    &(wrapped_pass->decorrelation_samples_A.arrays[i]));

        ia_copy(entropy_variables_A,
                &(wrapped_pass->entropy_variables_A));
        if (channel_count == 2) {
            ia_copy(decorrelation_weights_B,
                    &(wrapped_pass->decorrelation_weights_B));


            for (i = 0; i < decorrelation_passes; i++)
                ia_copy(&(decorrelation_samples_B->arrays[i]),
                        &(wrapped_pass->decorrelation_samples_B.arrays[i]));

            ia_copy(entropy_variables_B,
                    &(wrapped_pass->entropy_variables_B));
        } else {
            ia_vappend(entropy_variables_B, 3, 0, 0, 0);
        }
    } else {
        /*Otherwise, build a fresh set of values.*/

        ia_vappend(entropy_variables_A, 3, 0, 0, 0);
        ia_vappend(entropy_variables_B, 3, 0, 0, 0);
        for (i = 0; i < decorrelation_passes; i++)
            ia_append(decorrelation_weights_A, 0);
        samples_A = decorrelation_samples_A->arrays;
        switch (decorrelation_passes) {
        case 0:
            break;
        case 1:
            ia_append(decorrelation_terms, 18);
            ia_append(decorrelation_deltas, 2);
            ia_vappend(&(samples_A[0]), 2, 0, 0);
            break;
        case 2:
            ia_vappend(decorrelation_terms, 2, 17, 18);
            ia_vappend(decorrelation_deltas, 2, 2, 2);
            ia_vappend(&(samples_A[0]), 2, 0, 0);
            ia_vappend(&(samples_A[1]), 2, 0, 0);
            break;
        case 5:
            ia_vappend(decorrelation_terms, 5, 3, 17, 2, 18, 18);
            ia_vappend(decorrelation_deltas, 5, 2, 2, 2, 2, 2);
            ia_vappend(&(samples_A[0]), 3, 0, 0, 0);
            ia_vappend(&(samples_A[1]), 2, 0, 0);
            ia_vappend(&(samples_A[2]), 2, 0, 0);
            ia_vappend(&(samples_A[3]), 2, 0, 0);
            ia_vappend(&(samples_A[4]), 2, 0, 0);
            break;
        case 10:
            ia_vappend(decorrelation_terms, 10, 4, 17, -1, 5, 3,
                       2, -2, 18, 18, 18);
            ia_vappend(decorrelation_deltas, 10, 2, 2, 2, 2, 2,
                       2, 2, 2, 2, 2);
            ia_vappend(&(samples_A[0]), 4, 0, 0, 0, 0);
            ia_vappend(&(samples_A[1]), 2, 0, 0);
            ia_vappend(&(samples_A[2]), 1, 0);
            ia_vappend(&(samples_A[3]), 5, 0, 0, 0, 0, 0);
            ia_vappend(&(samples_A[4]), 3, 0, 0, 0);
            ia_vappend(&(samples_A[5]), 2, 0, 0);
            ia_vappend(&(samples_A[6]), 1, 0);
            ia_vappend(&(samples_A[7]), 2, 0, 0);
            ia_vappend(&(samples_A[8]), 2, 0, 0);
            ia_vappend(&(samples_A[9]), 2, 0, 0);
            break;
        case 16:
            ia_vappend(decorrelation_terms, 16, 2, 18, -1, 8, 6, 3, 5, 7,
                       4, 2, 18, -2, 3, 2, 18, 18);
            ia_vappend(decorrelation_deltas, 16, 2, 2, 2, 2, 2, 2, 2, 2,
                       2, 2, 2, 2, 2, 2, 2, 2);
            ia_vappend(&(samples_A[0]), 2, 0, 0);
            ia_vappend(&(samples_A[1]), 2, 0, 0);
            ia_vappend(&(samples_A[2]), 1, 0);
            ia_vappend(&(samples_A[3]), 8, 0, 0, 0, 0, 0, 0, 0, 0);
            ia_vappend(&(samples_A[4]), 6, 0, 0, 0, 0, 0, 0);
            ia_vappend(&(samples_A[5]), 3, 0, 0, 0);
            ia_vappend(&(samples_A[6]), 5, 0, 0, 0, 0, 0);
            ia_vappend(&(samples_A[7]), 7, 0, 0, 0, 0, 0, 0, 0);
            ia_vappend(&(samples_A[8]), 4, 0, 0, 0, 0);
            ia_vappend(&(samples_A[9]), 2, 0, 0);
            ia_vappend(&(samples_A[10]), 2, 0, 0);
            ia_vappend(&(samples_A[11]), 1, 0);
            ia_vappend(&(samples_A[12]), 3, 0, 0, 0);
            ia_vappend(&(samples_A[13]), 2, 0, 0);
            ia_vappend(&(samples_A[14]), 2, 0, 0);
            ia_vappend(&(samples_A[15]), 2, 0, 0);
            break;
        default:
            assert(0);
        }

        if (channel_count == 2) {
            for (i = 0; i < decorrelation_passes; i++)
                ia_append(decorrelation_weights_B, 0);
            samples_B = decorrelation_samples_B->arrays;
            switch (decorrelation_passes) {
            case 0:
                break;
            case 1:
                ia_vappend(&(samples_B[0]), 2, 0, 0);
                break;
            case 2:
                ia_vappend(&(samples_B[0]), 2, 0, 0);
                ia_vappend(&(samples_B[1]), 2, 0, 0);
                break;
            case 5:
                ia_vappend(&(samples_B[0]), 3, 0, 0, 0);
                ia_vappend(&(samples_B[1]), 2, 0, 0);
                ia_vappend(&(samples_B[2]), 2, 0, 0);
                ia_vappend(&(samples_B[3]), 2, 0, 0);
                ia_vappend(&(samples_B[4]), 2, 0, 0);
                break;
            case 10:
                ia_vappend(&(samples_B[0]), 4, 0, 0, 0, 0);
                ia_vappend(&(samples_B[1]), 2, 0, 0);
                ia_vappend(&(samples_B[2]), 1, 0);
                ia_vappend(&(samples_B[3]), 5, 0, 0, 0, 0, 0);
                ia_vappend(&(samples_B[4]), 3, 0, 0, 0);
                ia_vappend(&(samples_B[5]), 2, 0, 0);
                ia_vappend(&(samples_B[6]), 1, 0);
                ia_vappend(&(samples_B[7]), 2, 0, 0);
                ia_vappend(&(samples_B[8]), 2, 0, 0);
                ia_vappend(&(samples_B[9]), 2, 0, 0);
                break;
            case 16:
                ia_vappend(&(samples_B[0]), 2, 0, 0);
                ia_vappend(&(samples_B[1]), 2, 0, 0);
                ia_vappend(&(samples_B[2]), 1, 0);
                ia_vappend(&(samples_B[3]), 8, 0, 0, 0, 0, 0, 0, 0, 0);
                ia_vappend(&(samples_B[4]), 6, 0, 0, 0, 0, 0, 0);
                ia_vappend(&(samples_B[5]), 3, 0, 0, 0);
                ia_vappend(&(samples_B[6]), 5, 0, 0, 0, 0, 0);
                ia_vappend(&(samples_B[7]), 7, 0, 0, 0, 0, 0, 0, 0);
                ia_vappend(&(samples_B[8]), 4, 0, 0, 0, 0);
                ia_vappend(&(samples_B[9]), 2, 0, 0);
                ia_vappend(&(samples_B[10]), 2, 0, 0);
                ia_vappend(&(samples_B[11]), 1, 0);
                ia_vappend(&(samples_B[12]), 3, 0, 0, 0);
                ia_vappend(&(samples_B[13]), 2, 0, 0);
                ia_vappend(&(samples_B[14]), 2, 0, 0);
                ia_vappend(&(samples_B[15]), 2, 0, 0);
                break;
            default:
                assert(0);
            }
        }
    }
}

void
wavpack_store_tunables(struct wavpack_encoder_context* context,
                       int channel_number,
                       int channel_count,
                       struct i_array* decorrelation_terms,
                       struct i_array* decorrelation_deltas,
                       struct i_array* decorrelation_weights_A,
                       struct i_array* decorrelation_weights_B,
                       struct ia_array* decorrelation_samples_A,
                       struct ia_array* decorrelation_samples_B,
                       struct i_array* entropy_variables_A,
                       struct i_array* entropy_variables_B) {
    ia_size_t i;
    struct wavpack_decorrelation_pass* pass;
    struct i_array* array;

    assert((channel_count == 1) || (channel_count == 2));

    pass = &(context->wrap.passes[channel_number]);
    pass->channel_count = channel_count;

    /*Store decorrelation terms and deltas as-is*/
    ia_copy(&(pass->decorrelation_terms), decorrelation_terms);
    ia_copy(&(pass->decorrelation_deltas), decorrelation_deltas);

    /*Store decorrelation weights in context
      after round-tripping them through the 8-bit conversion routines
      for use by the next block on the same set of channels.*/
    array = &(pass->decorrelation_weights_A);
    ia_reset(array);
    for (i = 0; i < decorrelation_weights_A->size; i++)
        ia_append(array,
                  wavpack_restore_weight(wavpack_store_weight(
                                           decorrelation_weights_A->data[i])));
    if (channel_count > 1) {
        array = &(pass->decorrelation_weights_B);
        ia_reset(array);
        for (i = 0; i < decorrelation_weights_B->size; i++)
            ia_append(
                  array,
                  wavpack_restore_weight(wavpack_store_weight(
                                           decorrelation_weights_B->data[i])));
    }

    /*Store decorrelation samples in context
      after round-tripping them through the log2/exp2 conversion routines
      for use by the next block on the same set of channels.*/
    for (i = 0; i < decorrelation_samples_A->size; i++)
        ia_map(&(pass->decorrelation_samples_A.arrays[i]),
               &(decorrelation_samples_A->arrays[i]),
               wavpack_log2_roundtrip);
    if (channel_count > 1) {
        for (i = 0; i < decorrelation_samples_B->size; i++)
            ia_map(&(pass->decorrelation_samples_B.arrays[i]),
                   &(decorrelation_samples_B->arrays[i]),
                   wavpack_log2_roundtrip);
    }

    /*Store entropy variables in context
      after round-tripping them through the log2/exp2 conversion routines
      for use by the next block on the same set of channels.*/
    ia_map(&(pass->entropy_variables_A),
           entropy_variables_A,
           wavpack_log2_roundtrip);
    if (channel_count > 1) {
        ia_map(&(pass->entropy_variables_B),
               entropy_variables_B,
               wavpack_log2_roundtrip);
    }
}

ia_data_t
wavpack_log2_roundtrip(ia_data_t i) {
    return wavpack_exp2(wavpack_log2(i));
}

void
wavpack_wrap_decorrelation_samples(struct i_array* decorrelation_samples_A,
                                   struct i_array* decorrelation_samples_B,
                                   int decorrelation_term,
                                   struct i_array* channel_A,
                                   struct i_array* channel_B,
                                   int channel_count) {
    struct i_array tail;

    assert((channel_count == 1) || (channel_count == 2));
    tail.data = NULL;
    tail.size = tail.total_size = 0;

    switch (decorrelation_term) {
    case 18:
    case 17:
        ia_tail(&tail, channel_A, 2);
        ia_copy(decorrelation_samples_A, &tail);
        if (channel_count > 1) {
            ia_tail(&tail, channel_B, 2);
            ia_copy(decorrelation_samples_B, &tail);
        }
        break;
    case 8:
    case 7:
    case 6:
    case 5:
    case 4:
    case 3:
    case 2:
    case 1:
        ia_tail(&tail, channel_A, decorrelation_term);
        ia_copy(decorrelation_samples_A, &tail);
        if (channel_count > 1) {
            ia_tail(&tail, channel_B, decorrelation_term);
            ia_copy(decorrelation_samples_B, &tail);
        }
        break;
    case -1:
    case -2:
    case -3:
        assert(channel_count == 2);
        ia_tail(&tail, channel_A, 1);
        ia_copy(decorrelation_samples_A, &tail);
        ia_tail(&tail, channel_B, 1);
        ia_copy(decorrelation_samples_B, &tail);
        break;
    }
}

void
wavpack_count_bytes(int byte, void* value) {
    uint32_t* int_value = (uint32_t*)value;
    *int_value += 1;
}

void
wavpack_write_footer_block(Bitstream *bs,
                           struct wavpack_encoder_context *context) {
    Bitstream* block_data = bs_open_recorder();
    struct wavpack_block_header block_header;
    uint8_t md5sum[16];
    int i;

    ia_append(&(context->block_offsets), context->byte_count);
    audiotools__MD5Final(md5sum, &(context->md5));
    wavpack_initialize_block_header(&block_header, context, 1, 0, 1, 1);

    /*write MD5 sub-block*/
    wavpack_write_subblock_header(block_data, WV_MD5, 1, 16);
    for (i = 0; i < 16; i++)
        block_data->write_bits(block_data, 8, md5sum[i]);

    /*write wave footer sub-block, if present*/
    if (context->wave.footer_len > 0) {
        wavpack_write_subblock_header(block_data, WV_WAVE_FOOTER, 1,
                                      context->wave.footer_len);
        for (i = 0; i < context->wave.footer_len; i++)
            block_data->write_bits(block_data, 8, context->wave.footer[i]);
    }

    block_header.block_size = 24 + (block_data->bits_written / 8);
    wavpack_write_block_header(bs, &block_header);
    bs_dump_records(bs, block_data);
    bs_close(block_data);
}

void
wavpack_print_medians(FILE *output,
                      struct i_array* medians_A,
                      struct i_array* medians_B,
                      int channel_count) {
    assert((channel_count == 1) || (channel_count == 2));

    fprintf(output, "Medians A : %d %d %d\n",
            medians_A->data[0], medians_A->data[1], medians_A->data[2]);
    if (channel_count > 1)
        fprintf(output, "Medians B : %d %d %d\n",
                medians_B->data[0], medians_B->data[1], medians_B->data[2]);
}

void
wavpack_md5_callback(void* md5, unsigned char* data, unsigned long data_len) {
    audiotools__MD5Update(md5,
                          data,
                          data_len);
}

void
wavpack_count_pcm_bytes(void* data, unsigned char* buffer, unsigned long len) {
    uint32_t* total_bytes = (uint32_t*)data;
    *total_bytes += len;
}

void
wavpack_write_wave_header_sub_block(Bitstream* stream,
                                    struct wavpack_encoder_context* context,
                                    uint32_t pcm_bytes) {
    Bitstream* wave_header = bs_open_recorder();
    uint8_t extensible_sub_format[] = {0x01, 0x00, 0x00, 0x00, 0x00,
                                       0x00, 0x10, 0x00, 0x80, 0x00,
                                       0x00, 0xaa, 0x00, 0x38, 0x9b,
                                       0x71};
    int i;

    if ((context->total_channels <= 2) && (context->bits_per_sample <= 16)) {
        /*build a standard WAVE header if channels <= 2 and bps <= 16*/

        wave_header->write_bits64(wave_header, 32, 0x46464952); /*ID*/
        wave_header->write_bits64(wave_header, 32, 4 + 8 + 16 + 8 + pcm_bytes);
        wave_header->write_bits64(wave_header, 32, 0x45564157); /*Type*/
        wave_header->write_bits64(wave_header, 32, 0x20746D66); /*chunk ID*/
        wave_header->write_bits64(wave_header, 32, 16); /*fmt size*/

        wave_header->write_bits(wave_header, 16, 1);  /*uncompressed*/
        wave_header->write_bits(wave_header, 16, context->total_channels);
        wave_header->write_bits64(wave_header, 32, context->sample_rate);
        wave_header->write_bits64(wave_header, 32,
                                  (context->sample_rate *
                                   context->total_channels *
                                   context->bits_per_sample) / 8);
        wave_header->write_bits(wave_header, 16,
                                (context->total_channels *
                                 context->bits_per_sample) / 8);
        wave_header->write_bits(wave_header, 16, context->bits_per_sample);
    } else {
        /*otherwise, build a WAVEFORMATEXTENSIBLE header*/

        wave_header->write_bits64(wave_header, 32, 0x46464952); /*ID*/
        wave_header->write_bits64(wave_header, 32, 4 + 8 + 40 + 8 + pcm_bytes);
        wave_header->write_bits64(wave_header, 32, 0x45564157); /*Type*/
        wave_header->write_bits64(wave_header, 32, 0x20746D66); /*chunk ID*/
        wave_header->write_bits64(wave_header, 32, 40);     /*fmt size*/

        wave_header->write_bits(wave_header, 16, 0xFFFE); /*extensible*/
        wave_header->write_bits(wave_header, 16, context->total_channels);
        wave_header->write_bits64(wave_header, 32, context->sample_rate);
        wave_header->write_bits64(wave_header, 32,
                                  (context->sample_rate *
                                   context->total_channels *
                                   context->bits_per_sample) / 8);
        wave_header->write_bits(wave_header, 16,
                                (context->total_channels *
                                 context->bits_per_sample) / 8);
        wave_header->write_bits(wave_header, 16, context->bits_per_sample);
        wave_header->write_bits(wave_header, 16, 22);
        wave_header->write_bits(wave_header, 16, context->bits_per_sample);
        wave_header->write_bits64(wave_header, 32, context->channel_mask);
        for (i = 0; i < 16; i++)
            wave_header->write_bits(wave_header, 8, extensible_sub_format[i]);
    }

    /*then write a 'data' chunk header*/
    wave_header->write_bits64(wave_header, 32, 0x61746164); /*'data' chunk ID*/
    wave_header->write_bits64(wave_header, 32, pcm_bytes);  /*data size*/

    wavpack_write_subblock_header(stream, WV_WAVE_HEADER, 1,
                                  wave_header->bits_written / 8);
    bs_dump_records(stream, wave_header);
    bs_close(wave_header);
}

int
wavpack_max_wasted_bits_per_sample(struct i_array *samples)
{
    /*this seems like a good opportunity to use ia_reduce,
      except we want to quit once wasted bits-per-sample hits 0
      which will happen very early in the vast majority of cases*/
    ia_size_t i;
    ia_data_t sample;
    int wasted_bits;
    int wasted_bits_per_sample = INT_MAX;

    if (samples->size > 0) {
        for (i = 0; i < samples->size; i++) {
            sample = samples->data[i];
            if (sample != 0) {
                for (wasted_bits = 0;
                     ((sample & 1) == 0) && (sample != 0);
                     sample >>= 1)
                    wasted_bits++;
                wasted_bits_per_sample = MIN(wasted_bits_per_sample,
                                             wasted_bits);
                if (wasted_bits_per_sample == 0)
                    return 0;
            }
        }
        return wasted_bits_per_sample;
    } else {
        return 0;
    }
}

struct wavpack_decorrelation_pass*
wavpack_init_decorrelation_passes(int channel_count) {
    int i;
    struct wavpack_decorrelation_pass* passes;

    passes = malloc(sizeof(struct wavpack_decorrelation_pass) *
                     channel_count);

    for (i = 0; i < channel_count; i++) {
        ia_init(&(passes[i].decorrelation_terms), 0);
        ia_init(&(passes[i].decorrelation_deltas), 0);
        ia_init(&(passes[i].decorrelation_weights_A), 0);
        ia_init(&(passes[i].decorrelation_weights_B), 0);
        iaa_init(&(passes[i].decorrelation_samples_A), MAXIMUM_TERM_COUNT, 0);
        iaa_init(&(passes[i].decorrelation_samples_B), MAXIMUM_TERM_COUNT, 0);
        ia_init(&(passes[i].entropy_variables_A), 0);
        ia_init(&(passes[i].entropy_variables_B), 0);
    }

    return passes;
}

void
wavpack_free_decorrelation_passes(struct wavpack_decorrelation_pass* passes,
                                  int channel_count) {
    int i;

    for (i = 0; i < channel_count; i++) {
        ia_free(&(passes[i].decorrelation_terms));
        ia_free(&(passes[i].decorrelation_deltas));
        ia_free(&(passes[i].decorrelation_weights_A));
        ia_free(&(passes[i].decorrelation_weights_B));
        iaa_free(&(passes[i].decorrelation_samples_A));
        iaa_free(&(passes[i].decorrelation_samples_B));
        ia_free(&(passes[i].entropy_variables_A));
        ia_free(&(passes[i].entropy_variables_B));
    }
    free(passes);
}

#ifdef STANDALONE
int main(int argc, char *argv[]) {
    encoders_encode_wavpack(argv[1], stdin, 32, 1, 1, 1, 10);
    return 0;
}

#endif
