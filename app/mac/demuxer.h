/**
 * 
 * 提供的接口:
 * 
 * // 从文件或者内存中读取数据
 * typedef void (onReadBufferFunc)(void *opaque, uint8_t *buf, int buf_size);
 * 
 * // 数据最好是写到一块ringbuffer中, 避免拷贝媒体数据, 参考avformat/avio.h
 * typedef void (onWritePacketFunc)(void *opaque, uint8_t *buf, int buf_size);
 * 
 * demuxer *openByFilename(const char *filename);
 * 
 * demuxer *openByBuffer(onReadBufferFunc *readBuffer);
 * 
 * void setWriteFunc(onWritePacketFunc *writePacket);
 * 
 * void parseStream();
 * 
 * char *generateSDPDescription();
 * 
 * void close();
 * 
 * void startPlaying();
 * 
 * void pause();
 * 
 * void endPlaying();
 * 
 */

