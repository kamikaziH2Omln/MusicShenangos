/*given a new byte and the previous checksum value,
  assigns a new checksum to that value*/

void
flac_crc8(int byte, void *checksum);

void
flac_crc16(int byte, void *checksum);
