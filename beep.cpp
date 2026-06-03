#include "beep.h"
#include "globals.h"

void InitBeepWav() {
    const int SR=44100, FREQ=1000, CYCLES=10, SAMPLES=SR/FREQ*CYCLES, DATA_SIZE=SAMPLES*2;
    #pragma pack(push,1)
    struct WavHdr { char riff[4]; DWORD riffSz; char wave[4]; char fmt_[4]; DWORD fmtSz;
        WORD fmtTag,channels; DWORD sampleRate,byteRate; WORD blockAlign,bits;
        char data[4]; DWORD dataSz; };
    #pragma pack(pop)
    int total=(int)sizeof(WavHdr)+DATA_SIZE; g_wavBuf=new BYTE[total]();
    WavHdr* h=(WavHdr*)g_wavBuf;
    memcpy(h->riff,"RIFF",4); h->riffSz=total-8; memcpy(h->wave,"WAVE",4);
    memcpy(h->fmt_,"fmt ",4); h->fmtSz=16; h->fmtTag=1; h->channels=1;
    h->sampleRate=SR; h->byteRate=SR*2; h->blockAlign=2; h->bits=16;
    memcpy(h->data,"data",4); h->dataSz=DATA_SIZE;
    short* s=(short*)(g_wavBuf+sizeof(WavHdr));
    for(int i=0;i<SAMPLES;i++) s[i]=(short)(30000.0*sin(2.0*M_PI*FREQ*i/SR));
}

void StartBeep() {
    if(!BEEP_ENABLED||!g_wavBuf) return;
    PlaySoundW((LPCWSTR)g_wavBuf,nullptr,SND_MEMORY|SND_LOOP|SND_ASYNC|SND_NODEFAULT);
}

void StopBeep() { if(BEEP_ENABLED) PlaySoundW(nullptr,nullptr,0); }
