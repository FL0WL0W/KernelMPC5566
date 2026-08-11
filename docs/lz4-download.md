# LZ4 transfer format

The kernel accepts UDS `RequestDownload` (`0x34`) with either data format
identifier `0x00` (uncompressed) or `0x10` (LZ4 compression method 1, no
encryption). The memory size in `RequestDownload` is always the total
**uncompressed** size.

For DFI `0x10`, every `TransferData` (`0x36`) request contains one independent
raw LZ4 block:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 1 | Service ID `0x36` |
| 1 | 1 | Block sequence counter |
| 2 | 2 | Exact uncompressed block length, big-endian |
| 4 | remaining | Raw LZ4 block bytes (not an LZ4 frame) |

The maximum complete `TransferData` request is 255 bytes, so the compressed LZ4
block can occupy at most 251 bytes. A block may expand to at most 4096 bytes.
Blocks do not share a dictionary. The final decompressed byte count must equal
the size requested by `RequestDownload`.

The raw LZ4 format stores match offsets little-endian, as required by LZ4. A
repeated block sequence counter is acknowledged without decompressing or
writing the block again.

## RequestUpload

The kernel also accepts DFI `0x10` for `RequestUpload` (`0x35`). Each positive
`TransferData` response is an independent raw LZ4 block:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 1 | Positive response SID `0x76` |
| 1 | 1 | Block sequence counter |
| 2 | 2 | Exact uncompressed block length, big-endian |
| 4 | remaining | Raw LZ4 block bytes |

The kernel first attempts to encode up to 4096 source bytes. If that result
does not fit in the configured maximum UDS block length, it reduces the source
chunk until the encoded block fits. Upload blocks also have independent
dictionaries and repeated requests replay the exact previous response.
