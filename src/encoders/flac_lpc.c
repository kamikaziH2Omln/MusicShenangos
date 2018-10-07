#include "flac_lpc.h"
#include <math.h>
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

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

static fa_data_t
f_abs_max(fa_data_t val, fa_data_t max)
{
    fa_data_t abs = fabs(val);
    return MAX(abs, max);
}

void
FlacEncoder_compute_best_lpc_coeffs(struct i_array *lpc_warm_up_samples,
                                    struct i_array *lpc_residual,
                                    struct i_array *lpc_rice_parameters,
                                    struct i_array *coeffs,
                                    int *shift_needed,

                                    struct flac_encoding_options *options,
                                    int bits_per_sample,
                                    int wasted_bits_per_sample,
                                    struct i_array *samples)
{
    struct f_array tukey_window;
    struct f_array windowed_signal;
    struct f_array autocorrelation_values;
    struct fa_array lp_coefficients;
    struct f_array error_values;
    int lpc_order;

    struct i_array temp_coefficients;
    struct i_array temp_warm_up_samples;
    struct i_array temp_residual;
    struct i_array temp_rice_parameters;
    int temp_shift_needed;
    int i;
    Bitstream *temp_subframe;
    int current_best_subframe = INT_MAX;

    /*window signal*/
    fa_init(&tukey_window, samples->size);
    fa_init(&windowed_signal, samples->size);
    FlacEncoder_tukey_window(&tukey_window, samples->size, 0.5);
    fa_mul_ia(&windowed_signal, &tukey_window, samples);

    /*compute autocorrelation*/
    fa_init(&autocorrelation_values, options->max_lpc_order);
    FlacEncoder_compute_autocorrelation(&autocorrelation_values,
                                        &windowed_signal,
                                        options->max_lpc_order);

    /*if there's less than 2 autocorrelation values,
      use special case values for LPC coeffs and shift

      if all autocorrelation values are 0.0,
      we've got a bunch of 0 samples
      and should also use special case values for LPC coeffs and shift*/
    if ((autocorrelation_values.size < 2) ||
        ((fa_min(&autocorrelation_values) == 0.0) &&
         (fa_max(&autocorrelation_values) == 0.0))) {
        fa_free(&tukey_window);
        fa_free(&windowed_signal);
        fa_free(&autocorrelation_values);
        ia_reset(coeffs);
        ia_append(coeffs, 0);
        *shift_needed = 0;

        FlacEncoder_evaluate_lpc_subframe(lpc_warm_up_samples,
                                          lpc_residual,
                                          lpc_rice_parameters,

                                          options,
                                          bits_per_sample,
                                          samples,
                                          coeffs,
                                          *shift_needed);
        return;
    }

    /*compute LP coefficients*/
    faa_init(&lp_coefficients,
             options->max_lpc_order,
             options->max_lpc_order);
    fa_init(&error_values, options->max_lpc_order);
    FlacEncoder_compute_lp_coefficients(&lp_coefficients,
                                        &error_values,
                                        &autocorrelation_values,
                                        options->max_lpc_order - 1);

    if (!(options->exhaustive_model_search)) {
        /*if non-exhaustive search, estimate best order*/
        fa_tail(&error_values, &error_values, error_values.size - 1);
        lpc_order = FlacEncoder_compute_best_order(&error_values,
                                                   samples->size,
                                                   bits_per_sample + 5);

        /*quantize coefficients*/
        ia_reset(coeffs);
        FlacEncoder_quantize_coefficients(
                    faa_getitem(&lp_coefficients, lpc_order - 1),
                    options->qlp_coeff_precision,
                    coeffs,
                    shift_needed);

        FlacEncoder_evaluate_lpc_subframe(lpc_warm_up_samples,
                                          lpc_residual,
                                          lpc_rice_parameters,

                                          options,
                                          bits_per_sample,
                                          samples,
                                          coeffs,
                                          *shift_needed);
    } else {
        /*if exhaustive search, calculate best order*/
        temp_subframe = bs_open_accumulator();
        ia_init(&temp_coefficients, options->max_lpc_order);
        ia_init(&temp_warm_up_samples, options->max_lpc_order);
        ia_init(&temp_residual, samples->size);
        ia_init(&temp_rice_parameters, 1);

        for (i = 0; i < options->max_lpc_order - 1; i++) {
            ia_reset(&temp_coefficients);
            ia_reset(&temp_warm_up_samples);
            ia_reset(&temp_residual);
            ia_reset(&temp_rice_parameters);
            temp_subframe->bits_written = 0;


            FlacEncoder_quantize_coefficients(faa_getitem(&lp_coefficients, i),
                                              options->qlp_coeff_precision,
                                              &temp_coefficients,
                                              &temp_shift_needed);

            FlacEncoder_evaluate_lpc_subframe(&temp_warm_up_samples,
                                              &temp_residual,
                                              &temp_rice_parameters,
                                              options,
                                              bits_per_sample,
                                              samples,
                                              &temp_coefficients,
                                              temp_shift_needed);

            FlacEncoder_write_lpc_subframe(temp_subframe,
                                           &temp_warm_up_samples,
                                           &temp_rice_parameters,
                                           &temp_residual,
                                           bits_per_sample,
                                           wasted_bits_per_sample,
                                           &temp_coefficients,
                                           temp_shift_needed);

            if (temp_subframe->bits_written < current_best_subframe) {
                current_best_subframe = temp_subframe->bits_written;
                ia_copy(coeffs, &temp_coefficients);
                *shift_needed = temp_shift_needed;
                ia_copy(lpc_warm_up_samples, &temp_warm_up_samples);
                ia_copy(lpc_residual, &temp_residual);
                ia_copy(lpc_rice_parameters, &temp_rice_parameters);
            }
        }

        ia_free(&temp_coefficients);
        ia_free(&temp_warm_up_samples);
        ia_free(&temp_residual);
        ia_free(&temp_rice_parameters);
        bs_close(temp_subframe);
    }

    /*return best QLP coefficients and shift-needed values*/

    /*free temporary values*/
    fa_free(&tukey_window);
    fa_free(&windowed_signal);
    fa_free(&autocorrelation_values);
    faa_free(&lp_coefficients);
    fa_free(&error_values);
}

void
FlacEncoder_rectangular_window(struct f_array *window, int L) {
    int n;

    for (n = 0; n < L; n++)
        fa_append(window, 1.0);
}

void
FlacEncoder_hann_window(struct f_array *window, int L)
{
    int n;

    for (n = 0; n < L; n++)
        fa_append(window, 0.5 * (1.0 - cos((2 * M_PI * n) /
                                           (double)(L - 1))));
}

/*L is the window length
  p is the ratio of Hann window samples to rectangular window samples
  generates a Tukey window*/
void
FlacEncoder_tukey_window(struct f_array *window, int L, double p)
{
    int hann_length = (int)(p * L) - 1;
    int i;
    struct f_array hann_window;
    struct f_array hann_head;
    struct f_array hann_tail;
    struct f_array rect_window;

    fa_init(&hann_window, hann_length);
    fa_init(&rect_window, L - hann_length);

    FlacEncoder_rectangular_window(&rect_window, L - hann_length);
    FlacEncoder_hann_window(&hann_window, hann_length);
    fa_split(&hann_head, &hann_tail, &hann_window, hann_length / 2);

    for (i = 0; i < hann_head.size; i++)
        fa_append(window, fa_getitem(&hann_head, i));
    for (i = 0; i < rect_window.size; i++)
        fa_append(window, fa_getitem(&rect_window, i));
    for (i = 0; i < hann_tail.size; i++)
        fa_append(window, fa_getitem(&hann_tail, i));

    fa_free(&hann_window);
    fa_free(&rect_window);
}

void
FlacEncoder_compute_autocorrelation(struct f_array *values,
                                    struct f_array *windowed_signal,
                                    int max_lpc_order)
{
    int i, j;
    struct f_array lagged_signal;
    double sum;
    fa_data_t *windowed_signal_data = windowed_signal->data;
    fa_data_t *lagged_signal_data;

    for (i = 0; i < max_lpc_order; i++) {
        sum = 0.0;
        fa_tail(&lagged_signal, windowed_signal, windowed_signal->size - i);
        lagged_signal_data = lagged_signal.data;
        for (j = 0; j < lagged_signal.size; j++)
            sum += (windowed_signal_data[j] * lagged_signal_data[j]);
        fa_append(values, sum);
    }
}

void
FlacEncoder_compute_lp_coefficients(struct fa_array *lp_coefficients,
                                    struct f_array *error_values,
                                    struct f_array *autocorrelation_values,
                                    int max_lpc_order)
{
    /*r is autocorrelation_values
      a is lp_coefficients, a list of LP coefficient lists
      E is error_values
      M is max_lpc_order
      q and k are temporary values*/

    double qm;
    double km;
    struct f_array a;
    struct f_array r;
    struct f_array *a_i;
    struct f_array ra_i;
    struct f_array *a_im;
    int m;
    fa_size_t i;

    fa_init(&a, max_lpc_order);
    fa_init(&ra_i, max_lpc_order);


    /*E(0) = r(0)*/
    fa_append(error_values, fa_getitem(autocorrelation_values, 0));

    /*a(1)(1) = k(1) = r(1) / E(0)*/
    km = fa_getitem(autocorrelation_values, 1) / fa_getitem(error_values, 0);
    fa_append(faa_getitem(lp_coefficients, 0), km);

    /*E(1) = E(0) * (1 - (k(1) ^ 2))*/
    fa_append(error_values,
              fa_getitem(error_values, -1) * (1 - (km * km)));

    for (m = 2; m <= max_lpc_order; m++) {
        /*q(m) = r(m) - sum(i = 1 to m - 1, a(i)(m - 1) * r(m - i))*/
        fa_copy(&a, faa_getitem(lp_coefficients, m - 2));
        fa_reverse(&a);
        fa_tail(&r, autocorrelation_values, autocorrelation_values->size - 1);
        fa_mul(&a, &a, &r);
        qm = fa_getitem(autocorrelation_values, m) - fa_sum(&a);

        /*k(m) = q(m) / E(m - 1)*/
        km = qm / fa_getitem(error_values, m - 1);

        /*a(i)(m) = a(i)(m - 1) - k(m) * a(m - i)(m - 1) for i = 1 to m - 1*/
        a_i = faa_getitem(lp_coefficients, m - 2);
        fa_copy(&ra_i, a_i);
        fa_reverse(&ra_i);

        a_im = faa_getitem(lp_coefficients, m - 1);
        for (i = 0; i < ra_i.size; i++) {
            fa_append(a_im, fa_getitem(a_i, i) - (km * fa_getitem(&ra_i, i)));
        }

        /*a(m)(m) = k(m)*/
        fa_append(a_im, km);

        /*E(m) = E(m - 1) * (1 - k(m) ^ 2)*/
        fa_append(error_values,
                  fa_getitem(error_values, -1) * (1 - (km * km)));

        /*continue until m == M*/
    }

    fa_free(&a);
    fa_free(&ra_i);
}

int
FlacEncoder_compute_best_order(struct f_array *error_values,
                               int total_samples,
                               int overhead_bits_per_order)
{
    double error_scale = (M_LN2 * M_LN2) / (double)(total_samples * 2);
    int best_order = 0;
    double best_bits = 1e32;
    double bits;
    int order;
    int i;

    for (i = 0, order = 1; i < error_values->size; i++, order++) {
        bits = FlacEncoder_compute_expected_bits_per_residual_sample(
                    fa_getitem(error_values, i),
                    error_scale) *
            (double)(total_samples - order) +
            (double)(order * overhead_bits_per_order);
        if (bits < best_bits) {
            best_order = order;
            best_bits = bits;
        }
    }

    return best_order;
}

double
FlacEncoder_compute_expected_bits_per_residual_sample(double lpc_error,
                                                      double error_scale)
{
    if (lpc_error > 0.0) {
        return MAX(log(error_scale * lpc_error) / (M_LN2 * 2.0), 0.0);
    } else if (lpc_error < 0.0) {
        return 1e32;
    } else {
        return 0.0;
    }
}

#define SUBFRAME_LPC_QLP_SHIFT_LEN 5

void
FlacEncoder_quantize_coefficients(struct f_array *lp_coefficients,
                                  int precision,
                                  struct i_array *qlp_coefficients,
                                  int *shift_needed)
{
    int log2cmax;
    int32_t qlp_coeff_min;
    int32_t qlp_coeff_max;
    fa_size_t i;
    int32_t qlp;
    double error = 0.0;

    precision--;

    (void)frexp(fa_reduce(lp_coefficients, 0.0, f_abs_max), &log2cmax);

    /*FIXME - handle negative or overly-large shift-needed corretly*/
    *shift_needed = MIN(MAX(precision - (log2cmax - 1) - 1, 0), 0xF);

    qlp_coeff_max = (1 << precision) - 1;
    qlp_coeff_min = -(1 << precision);

    for (i = 0; i < lp_coefficients->size; i++) {
        error += fa_getitem(lp_coefficients, i) * (1 << *shift_needed);
        qlp = MIN(MAX(lround(error), qlp_coeff_min), qlp_coeff_max);
        ia_append(qlp_coefficients, qlp);
        error -= qlp;
    }
}

