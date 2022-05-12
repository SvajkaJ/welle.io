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
#include "qi-processor.h"



QIProcessor::QIProcessor(
        RadioControllerInterface& rci,
        const DABParams& params) :
    rci(rci),
    params(params)
{

}

QIProcessor::~QIProcessor()
{

}

void QIProcessor::onCorrectedData(uint8_t *data, uint8_t ficno)
{
    memcpy(FIC_dep_dec + 768 * ficno * sizeof(uint8_t), data, 768 * sizeof(uint8_t));
}

void QIProcessor::onUncorrectedData(uint8_t *data, uint8_t ficno)
{
    memcpy(FIC + 2304 * ficno * sizeof(uint8_t), data, 2304 * sizeof(uint8_t));
}

/**
 * Always increments variable "receivedFIBs" by 1.
 * Increments variable "faultyFIBs" by 1 when crc is invalid.
 */
void QIProcessor::onFIB(const bool crcValid)
{
    receivedFIBs += 1;
    if (!crcValid) {
        faultyFIBs += 1;
    }
}

/**
 * Does abs() on buffer.
 */
void QIProcessor::onSpectrum(DSPCOMPLEX *buf)
{
    for (int16_t i = 0; i < params.T_u; i++) {
        spectrum[i] = abs(buf[i]);
    }
}

/**
 * Process: Exponential smoothing
 * Notifies RadioControllerInterface by calling onSNR(smoothed_snr)
 */
void QIProcessor::processSNR()
{
    int16_t i;
    float noise = 0, signal = 0;
    const auto T_u = params.T_u;
    const auto K = params.K;
    int16_t low = (params.T_u - params.K) / 2;    // 256th carrier
    int16_t high = params.K + low;                // 1792th carrier
    int16_t x = low / 4;                          // 64

    for (i = x; i < low - x; i++) // 128 samples
        noise += spectrum[(T_u / 2 + i) % T_u];

    for (i = high + x; i < T_u - x; i++) // 128 samples
        noise += spectrum[(T_u / 2 + i) % T_u];

    for (i = T_u / 2 - K / 4;  i < T_u / 2 + K / 4; i ++)
        signal += spectrum[(T_u / 2 + i) % T_u];

    // get_db_over_256 adds +1 to the value and divides by 256
    // because the tuner maps the received signal to 0..255
    // every sample is 8bit value (256)
    // value equal to 0 is also a valid signal but log(0) is undefined
    // and that is why there is +1 => mapped to 1..256
    const float dB_signal = get_db_over_256(signal / (float)(K / 2));
    const float dB_noise = get_db_over_256(noise / (float)(256));
    const float dB_snr = dB_signal - dB_noise;
    const float smoothing_factor_snr = 0.35;

    smoothed_snr = smoothing_factor_snr * dB_snr + (1 - smoothing_factor_snr) * smoothed_snr;

    // rci: Radio Controller Interface
    rci.onSNR(smoothed_snr);
}

/**
 * Data is converted to bytes. Then convolutionaly encoded
 * according to DAB standard. Then punctured.
 * 
 * BER is at the end smoothed. RadioControllerInterface is notified by calling onBER(smoothed_ber)
 */
void QIProcessor::processBER()
{
    int faultyBits = 0;
    float ber = 0.0;
    const float smoothing_factor_ber = 0.35;

    dab_bit_to_byte(&FIC_dep_dec[0], &FIC_dep_dec_byte[0], (768*4));

    encode(&FIC_dep_enc[0*3096], &FIC_dep_dec_byte[0*DEP_DEC_BYTE_SIZE], DEP_DEC_BYTE_SIZE, 0, 0);
    encode(&FIC_dep_enc[1*3096], &FIC_dep_dec_byte[1*DEP_DEC_BYTE_SIZE], DEP_DEC_BYTE_SIZE, 0, 0);
    encode(&FIC_dep_enc[2*3096], &FIC_dep_dec_byte[2*DEP_DEC_BYTE_SIZE], DEP_DEC_BYTE_SIZE, 0, 0);
    encode(&FIC_dep_enc[3*3096], &FIC_dep_dec_byte[3*DEP_DEC_BYTE_SIZE], DEP_DEC_BYTE_SIZE, 0, 0);

    puncture(&FIC_dep_enc[0*3096], &FIC_enc_pun[0*2304]);
    puncture(&FIC_dep_enc[1*3096], &FIC_enc_pun[1*2304]);
    puncture(&FIC_dep_enc[2*3096], &FIC_enc_pun[2*2304]);
    puncture(&FIC_dep_enc[3*3096], &FIC_enc_pun[3*2304]);

    // By comparing  FIC  and  FIC_enc_pun  we are able to estimate BER
    for (unsigned int i = 0; i < 1536 * 2 * 3; i++) {
        if (FIC[i] != FIC_enc_pun[i]) {
            faultyBits++;
        }
    }

    // (pseudo) channel BER
    ber = (float)faultyBits / (float)(1536 * 2 * 3); // 9216
    //std::cout << "BER: " << ber << std::endl;

    smoothed_ber = smoothing_factor_ber * ber + (1 - smoothing_factor_ber) * smoothed_ber;

    // rci: Radio Controller Interface
    rci.onBER(smoothed_ber);
}

/**
 * Method:
 * - 0 - 2048 carriers (params.T_u)
 * - 1 - 1536 carriers (params.K)
 */
void QIProcessor::processPower(uint8_t method)
{
    float x = 0.0;
    const float smoothing_factor_power = 0.35;

    int16_t low = 0;
    int16_t high = params.T_u;

    // Calculate average RF power
    for (int16_t i = low; i < high; i++) {
        x += spectrum[i];
    }
    x = x / (float)(high - low);

    /*
    // spectrum is not shifted
    std::cout << "noise: " << spectrum[(params.T_u / 2) - 1] << std::endl;
    std::cout << "signal: " << spectrum[0] << std::endl;
    */

    const float dB_power = get_db_over_256(x);   // dBs (sample)
    smoothed_power = smoothing_factor_power * dB_power + (1 - smoothing_factor_power) * smoothed_power;

    // rci: Radio Controller Interface
    rci.onPower(smoothed_power);
}


/**
 * Process: Exponential smoothing
 * Notifies RadioControllerInterface by calling onFIBER(smoothed_fiber).
 */
void QIProcessor::processFIBER()
{
    float fiber = 0.0;
    const float smoothing_factor_fiber = 0.35;

    fiber = (float)faultyFIBs / (float)receivedFIBs;

    smoothed_fiber = smoothing_factor_fiber * fiber + (1 - smoothing_factor_fiber) * smoothed_fiber;
    // smoothed_fiber is approaching zero (but never reaches it) during normal operation

    rci.onFIBER(smoothed_fiber);

    // Clean up
    receivedFIBs = 0;
    faultyFIBs = 0;
}

/**
 * Converts bits to bytes
 * © rtl-dab @ https://github.com/maydavid/rtl-dab
 */
void QIProcessor::dab_bit_to_byte(uint8_t *in, uint8_t *out,uint32_t len)
{
    uint32_t i;
    uint32_t bc=0;
    for (i=0;i<len;i+=8) {
        out[bc] = (in[i+0]<<7) + (in[i+1]<<6) + (in[i+2]<<5) + (in[i+3]<<4) +   
            (in[i+4]<<3) + (in[i+5]<<2) + (in[i+6]<<1) +(in[i+7]<<0);
        bc++;
    }
}

/**
 * Puncturing procedure
 * © openDAB @ https://sourceforge.net/projects/opendab/
 */
void QIProcessor::puncture(uint8_t *ibuf, uint8_t *obuf)
{
    int i, j, k;
    // 21 blocks, using puncture 1110 1110 1110 1110 1110 1110 1110 1110
    //  3 blocks, using puncture 1110 1110 1110 1110 1110 1110 1110 1100
    // 24 bits,   using puncture 1100 1100 1100 1100 1100 1100
    k = 0;
    for (i=0; i<21*128; i+=32)
    {
        for (j=0; j<8; j++)
        {
            obuf[k+0] = ibuf[i+j*4+0];
            obuf[k+1] = ibuf[i+j*4+1];
            obuf[k+2] = ibuf[i+j*4+2];
            k+=3;
        }
    }
    for (i=21*128; i<24*128; i+=32)
    {
        for (j=0; j<7; j++)
        {
            obuf[k+0] = ibuf[i+j*4+0];
            obuf[k+1] = ibuf[i+j*4+1];
            obuf[k+2] = ibuf[i+j*4+2];
            k+=3;
        }
        obuf[k+0] = ibuf[i+j*4+0];
        obuf[k+1] = ibuf[i+j*4+1];
        k+=2;
    }
    for (j=0; j<6; j++)
    {
        obuf[k+0] = ibuf[i+j*4+0];
        obuf[k+1] = ibuf[i+j*4+1];
        k+=2;
    }
}