#ifndef SPEECH_H
#define SPEECH_H

/**
 * @brief 语音播报接口
 * 
 * @param direction 方位 (Left / Right / Front)
 * @param object    物体名称 (cup, person 等)
 */
void speech_report(const char *direction, const char *object);

#endif // SPEECH_H