/**
 * 
 * 提供的接口:
 * 
 * typedef void (onReadBufferFunc)(void *opaque, uint8_t *buf, int buf_size);
 * typedef void (onWritePacketFunc)(void *opaque, uint8_t *buf, int buf_size);
 * 
 * void init(const char *filename, 
 *                    onReadBufferFunc *readBuffer = NULL);
 * 
 * void setWriteFunc(onWritePacketFunc *writePacket);
 * 
 * void parseStream();
 * 
 * char *generateSDPDescription();
 * 
 * void release();
 * 
 * void startPlaying();
 * 
 * void pause();
 * 
 * void endPlaying();
 * 
 */

