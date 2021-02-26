//
// Created by timmy on 2021-02-22.
// Functions we want to "OpenCL-alize"

/* ===================== Mode S detection and decoding  ===================== */

/* Parity table for MODE S Messages.
 * The table contains 112 elements, every element corresponds to a bit set
 * in the bitf_message, starting from the first bit of actual data after the
 * preamble.
 *
 * For messages of 112 bit, the whole table is used.
 * For messages of 56 bits only the last 56 elements are used.
 *
 * The algorithm is as simple as xoring all the elements in this table
 * for which the corresponding bit on the bitf_message is set to 1.
 *
 * The latest 24 elements in this table are set to 0 as the checksum at the
 * end of the bitf_message should not affect the computation.
 *
 * Note: this function can be used with DF11 and DF17, other modes have
 * the CRC xored with the sender address as they are reply to interrogations,
 * but a casual listener can't split the address from the checksum.
 */
__constant uint modes_checksum_table[112] = {
        0x3935ea, 0x1c9af5, 0xf1b77e, 0x78dbbf, 0xc397db, 0x9e31e9, 0xb0e2f0, 0x587178,
        0x2c38bc, 0x161c5e, 0x0b0e2f, 0xfa7d13, 0x82c48d, 0xbe9842, 0x5f4c21, 0xd05c14,
        0x682e0a, 0x341705, 0xe5f186, 0x72f8c3, 0xc68665, 0x9cb936, 0x4e5c9b, 0xd8d449,
        0x939020, 0x49c810, 0x24e408, 0x127204, 0x093902, 0x049c81, 0xfdb444, 0x7eda22,
        0x3f6d11, 0xe04c8c, 0x702646, 0x381323, 0xe3f395, 0x8e03ce, 0x4701e7, 0xdc7af7,
        0x91c77f, 0xb719bb, 0xa476d9, 0xadc168, 0x56e0b4, 0x2b705a, 0x15b82d, 0xf52612,
        0x7a9309, 0xc2b380, 0x6159c0, 0x30ace0, 0x185670, 0x0c2b38, 0x06159c, 0x030ace,
        0x018567, 0xff38b7, 0x80665f, 0xbfc92b, 0xa01e91, 0xaff54c, 0x57faa6, 0x2bfd53,
        0xea04ad, 0x8af852, 0x457c29, 0xdd4410, 0x6ea208, 0x375104, 0x1ba882, 0x0dd441,
        0xf91024, 0x7c8812, 0x3e4409, 0xe0d800, 0x706c00, 0x383600, 0x1c1b00, 0x0e0d80,
        0x0706c0, 0x038360, 0x01c1b0, 0x00e0d8, 0x00706c, 0x003836, 0x001c1b, 0xfff409,
        0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
        0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
        0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000
};

uint modesChecksum(unsigned char *msg, int bits) {
    uint crc = 0;
    int offset = (bits == 112) ? 0 : (112-56);
    int j;

    for(j = 0; j < bits; j++) {
        int byte = j/8;
        int bit = j%8;
        int bitmask = 1 << (7-bit);

        /* If bit is set, xor with corresponding table entry. */
        if (msg[byte] & bitmask)
            crc ^= modes_checksum_table[j+offset];
    }
    return crc; /* 24 bit checksum. */
}


__constant int masks[16] = {0b10000000,
                            0b01000000,
                            0b00100000,
                            0b00010000,
                            0b00001000,
                            0b00000100,
                            0b00000010,
                            0b00000001};
/* Similar to fixSingleBitErrors() but try every possible two bit combination.
 * This is very slow and should be tried only against DF17 messages that
 * don't pass the checksum, and only in Aggressive Mode.
 * fixSingleBitErrors has already been called if this is run. Don't bother just flipping 1 bit.
 *  */
__kernel void fixTwoBitsErrors(__global unsigned char *msg,__global int *bits, __global int *returnValue) {
    unsigned char aux[112/8];
    uint j = get_global_id(0);
    uint i = get_global_id(0);

    int byte1 = j/8;
    int bitmask1 = masks[j%8];//1 << (7-(j%8));
    /* Don't check the same pairs multiple times, so i starts from j+1 */
    for (int i = j + 1; i < *bits; i++){
        int byte2 = i/8;
        int bitmask2 = masks[i%8];//1 << (7-(i%8));
        uint crc1, crc2;

        for (int k = 0; k < (*bits)/8; k++){
            aux[k] = msg[k];
        }

        aux[byte1] ^= bitmask1; /* Flip j-th bit. */
        aux[byte2] ^= bitmask2; /* Flip i-th bit. */

        crc1 = ((uint)aux[((*bits)/8)-3] << 16) |
               ((uint)aux[((*bits)/8)-2] << 8) |
               (uint)aux[((*bits)/8)-1];
        crc2 = modesChecksum(aux,*bits);

        if (crc1 == crc2) {
            /* The error is fixed. Overwrite the original buffer with
             * the corrected sequence, and returns the error bit
             * position. */
            for (int k = 0; k < (*bits)/8; k++){
                msg[k] = aux[k];
            }

            /* We return the two bits as a 16 bit integer by shifting
             * 'i' on the left. This is possible since 'i' will always
             * be non-zero because i starts from j+1. */
            returnValue =  j | (i<<8);
        }
    }
}
