#include "speech.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h> /* 引入时间库 */

void speech_report(const char *direction, const char *object) {
    char command[256];
    
    /* 获取微秒级时间戳，确保每次生成的文件名都不一样 */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long timestamp = tv.tv_usec; 
    
    /* 
     * 逻辑：
     * 1. 生成唯一文件名 /tmp/sp_时间戳.wav
     * 2. 播放完毕后 rm -f 删除文件，防止 /tmp 空间爆满
     */
    snprintf(command, sizeof(command), 
             "sh -c 'espeak -v en -s 150 -a 200 \"%s %s ahead\" -w /tmp/sp_%ld.wav && aplay -D plughw:0,0 /tmp/sp_%ld.wav; rm -f /tmp/sp_%ld.wav' 2>/dev/null &", 
             direction, object, timestamp, timestamp, timestamp);
    
    system(command);
}