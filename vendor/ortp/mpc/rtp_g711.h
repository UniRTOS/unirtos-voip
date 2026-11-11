/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2026-04-30 10:52:26
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2026-04-30 11:12:48
 * @FilePath: \undefinedd:\D_P4\ASR\1903SR\R02\PLT\quectel\system\platform\asr_1903\extension\ortp\inc\ortp\ortp_g711.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/*
 * g711.h
 *
 * u-law, A-law and linear PCM conversions.
 * Source: http://www.speech.kth.se/cost250/refsys/latest/src/g711.h
 */

#ifndef _RTP_G711_H_
#define _RTP_G711_H_

#ifdef __cplusplus
extern "C" {
#endif


unsigned char	linear2alaw(short pcm_val);
short		    alaw2linear(unsigned char a_val);
unsigned char	linear2ulaw(short pcm_val);
short		    ulaw2linear(unsigned char u_val);
unsigned char	alaw2ulaw(unsigned char aval);
unsigned char	ulaw2alaw(unsigned char uval);

#ifdef __cplusplus
}
#endif

#endif /* _G711_H_ */
