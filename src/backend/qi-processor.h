/*
 *    Copyright (C) 2022
 *    Jakub Svajka
 *
 *    This file is extension of the welle.io.
 *    Many of the ideas as implemented in welle.io are derived from
 *    other work, made available through the GNU general Public License.
 *    All copyrights of the original authors are recognized.
 *
 *    welle.io is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    welle.io is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */
#ifndef __QI_PROCESSOR
#define __QI_PROCESSOR

#include <stdint.h>
#include <iostream>
#include "radio-controller.h"
#include "viterbi.h"
#include "dab-constants.h"

#define DEP_DEC_BYTE_SIZE (768 / 8)   // to speed up the program a little bit


class QIProcessor
{
    public:
        QIProcessor(RadioControllerInterface& rci, const DABParams& params);
        ~QIProcessor();

        void            onCorrectedData(uint8_t *data, uint8_t ficno);
        void            onUncorrectedData(uint8_t *data, uint8_t ficno);
        void            onFIB(const bool crcValid);
        void            onSpectrum(DSPCOMPLEX *buf);

        void            processSNR();
        void            processBER();
        void            processPower(uint8_t method);
        void            processFIBER();
    private:
        RadioControllerInterface& rci;
        const DABParams& params;

        // Power spectrum buffer
        float           spectrum[2048];

        // saved as elementary data type instead of dab_quality_indicators_t
        // because of multithreading
        // we can avoid using mutex
        float           smoothed_snr = 0.0;
        float           smoothed_ber = 0.0;
        float           smoothed_power = 0.0;
        float           smoothed_fiber = 0.0;

        uint32_t        receivedFIBs = 0;
        uint32_t        faultyFIBs = 0;

        void            dab_bit_to_byte(uint8_t *in, uint8_t *out, uint32_t len);
        void            puncture(uint8_t *ibuf, uint8_t *obuf);

        // Input data that is still punctured and convolutionaly encoded
        uint8_t         FIC[1536*2*3];

        // Depunctured and convolutionaly decoded data
        uint8_t         FIC_dep_dec[768*4];

        // Depunctured and convolutionaly decoded data in bytes == Corrected data
        uint8_t         FIC_dep_dec_byte[(768*4)/8];

        // Convolutionaly encoded corrected data
        uint8_t         FIC_dep_enc[3096*4];

        // Convolutionaly encoded corrected and punctured data
        uint8_t         FIC_enc_pun[1536*2*3];

    // FIC is made up of 3 OFDM symbols
    // FIC has 4 FIBs in 1 OFDM symbol
    // FIB has 256 bits (with CRC)
    // => 3 * 4 * 256 = 3072
    // this corresponds to a code rate of approximately 1/3
    // => 3072 * 2 is protection
    // total = 3072 * 3

    // FIC_dep_dec: 12 FIBs are divided to 4 qroups by 3 blocks (3*256)
};

#endif