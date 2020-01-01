/**
 * 
 * 提供的接口:
 * 
 * // 从内存中读取数据
 * typedef void (onReadBufferFunc)(void *opaque, uint8_t *buf, int buf_size);
 * 
 * // 写到内存或者文件中
 * typedef void (onWritePacketFunc)(void *opaque, uint8_t *buf, int buf_size);
 * 
 * muxer *openByFilename(const char *filename);
 * 
 * muxer *openByBuffer(onWritePacketFunc *writePacket);
 * 
 * void setReadFunc(onReadBufferFunc *readBuffer);
 * 
 * void close();
 * 
 * void startPlaying();
 * 
 * void endPlaying();
 * 
 */