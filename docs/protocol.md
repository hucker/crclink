# Protocol Notes

crclink supports two framing modes:

- JSON frame with trailing crc key.
- Text frame with trailing CRC suffix.

CRC algorithm is CRC-16/XMODEM.

Reference check value:
- crc16_xmodem("123456789") = 0x31C3
