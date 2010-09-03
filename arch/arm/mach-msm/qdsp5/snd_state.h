#ifndef __SND_STATE_H__
#define __SND_STATE_H__

#define SND_STATE_IDLE      0x0000
#define SND_STATE_PLAYBACK  0x0001
#define SND_STATE_RECORD    0x0002
#define SND_STATE_INCALL    0x0004
#define SND_STATE_SPEAKER   0x0008

#define SND_STATE_HANDSFREE (SND_STATE_SPEAKER|SND_STATE_INCALL|SND_STATE_RECORD)
#define SND_STATE_HANDSMUTE (SND_STATE_SPEAKER|SND_STATE_INCALL)

extern uint8_t snd_state;

extern int call_vol;
 
void msm_setup_audio( void );

#endif
