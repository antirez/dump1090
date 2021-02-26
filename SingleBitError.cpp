//
// Created by timmy on 2021-02-23.
//



__constant int modes_checksum_table[112] = {
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

__constant bitmasks[] = {
        0b10000000, 0b1000000, 0b100000, 0b10000, 0b1000, 0b100, 0b10, 0b1
};
int modesChecksum(unsigned char *msg, int bits) {
    // Take string, j = get_global_id(0)
    int crc = 0;
    int offset = (bits == 112) ? 0 : (112-56);
    int j;
    for(j = 0; j < bits; j++) {
        int byte = j/8;
        int bit = j%8;
        int bitmask = bitmasks[bit];

        /* If bit is set, xor with corresponding table entry. */
        if (msg[byte] & bitmask)
            crc ^= modes_checksum_table[j+offset];
    }
    return crc; /* 24 bit checksum. */
}

__constant int MODES_LONG_MSG_BITS = 112;

/* Try to fix single bit errors using the checksum. On success modifies
 * the original buffer with the fixed version, and saves the position
 * of the error bit. Otherwise if fixing failed nothing is saved leaving returnval as-is. */
__kernel void fixSingleBitErrors(__global unsigned char* msg, __global int* bits,  __global  int* returnval){
    size_t j = get_global_id(0);
    unsigned char aux[MODES_LONG_MSG_BITS/8]; // NOTE: Might have to allocate in main memory and give to pointer.
    size_t byte = j/8;
    int bitmask = 1 << (7 - (j % 8));
    int crc1, crc2;

    for (int i = 0; i < (*bits)/8; i+=2){
        aux[i] = msg[i];
        aux[i+1] = msg[i+1];
    }

    aux[byte] ^= bitmask; /* Flip the J-th bit*/

    crc1 = ((uint)aux[(*bits/8)-3] << 16) |
           ((uint)aux[(*bits/8)-2] << 8) |
           (uint)aux[(*bits/8)-1];
    // Return string
    crc2 = modesChecksum(aux,*bits);
    barrier(CLK_LOCAL_MEM_FENCE);
    if (crc1 == crc2){ // check in CPU
        /* The error is fixed. Overwrite the original buffer with
             * the corrected sequence, and returns the error bit
             * position.
             * This should only happen in one work-unit if the number of errors in the bitf_message was few enough. I see no problem if multiple units finds a solution and overwrites each other however.
             * */
        for (int i = 0; i < (*bits)/8; i+=2){
            msg[i] = aux[i];
            msg[i+1] = aux[i+1];
        }
        returnval[j] = j;
    }
    // if we couldn't fix it, dont touch returnval.
}
