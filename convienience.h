/*
 * Copyright (C) 2013-2014 by Kyle Keen <keenerd@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* a collection of user friendly tools */

/*!
 * Convert standard suffixes (k, M, G) to double
 *
 * \param s a string to be parsed
 * \return double
 */

double atofs(char *s);

/*!
 * Convert time suffixes (s, m, h) to double
 *
 * \param s a string to be parsed
 * \return seconds as double
 */

double atoft(char *s);

/*!
 * Convert percent suffixe (%) to double
 *
 * \param s a string to be parsed
 * \return double
 */

double atofp(char *s);

/*!
 * Find nearest supported gain
 *
 * \param dev the device handle given by rtlsdr_open()
 * \param target_gain in tenths of a dB
 * \return 0 on success
 */

int nearest_gain(rtlsdr_dev_t *dev, int target_gain);

/*!
 * Set device frequency and report status on stderr
 *
 * \param dev the device handle given by rtlsdr_open()
 * \param frequency in Hz
 * \return 0 on success
 */

int verbose_set_frequency(rtlsdr_dev_t *dev, uint32_t frequency);

/*!
 * Set device sample rate and report status on stderr
 *
 * \param dev the device handle given by rtlsdr_open()
 * \param samp_rate in samples/second
 * \return 0 on success
 */

int verbose_set_sample_rate(rtlsdr_dev_t *dev, uint32_t samp_rate);

/*!
 * Enable or disable the direct sampling mode and report status on stderr
 *
 * \param dev the device handle given by rtlsdr_open()
 * \param on 0 means disabled, 1 I-ADC input enabled, 2 Q-ADC input enabled
 * \return 0 on success
 */

int verbose_direct_sampling(rtlsdr_dev_t *dev, int on);

/*!
 * Enable offset tuning and report status on stderr
 *
 * \param dev the device handle given by rtlsdr_open()
 * \return 0 on success
 */

int verbose_offset_tuning(rtlsdr_dev_t *dev);

/*!
 * Enable auto gain and report status on stderr
 *
 * \param dev the device handle given by rtlsdr_open()
 * \return 0 on success
 */

int verbose_auto_gain(rtlsdr_dev_t *dev);

/*!
 * Set tuner gain and report status on stderr
 *
 * \param dev the device handle given by rtlsdr_open()
 * \param gain in tenths of a dB
 * \return 0 on success
 */

int verbose_gain_set(rtlsdr_dev_t *dev, int gain);

/*!
 * Set the frequency correction value for the device and report status on stderr.
 *
 * \param dev the device handle given by rtlsdr_open()
 * \param ppm_error correction value in parts per million (ppm)
 * \return 0 on success
 */

int verbose_ppm_set(rtlsdr_dev_t *dev, int ppm_error);

/*!
 * Attempts to extract a correction value from eeprom and store it to an int.
 *
 * \param dev the device handle given by rtlsdr_open()
 * \param ppm_error correction value in parts per million (ppm)
 * \return 0 on success
 */
int verbose_ppm_eeprom(rtlsdr_dev_t *dev, int *ppm_error);

/*!
 * Reset buffer
 *
 * \param dev the device handle given by rtlsdr_open()
 * \return 0 on success
 */

int verbose_reset_buffer(rtlsdr_dev_t *dev);

/*!
 * Find the closest matching device.
 *
 * \param s a string to be parsed
 * \return dev_index int, -1 on error
 */

int verbose_device_search(char *s);
