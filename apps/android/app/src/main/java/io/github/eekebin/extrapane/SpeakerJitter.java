package io.github.eekebin.extrapane;

/** Stereo frame-aligned jitter buffer, shared by the network and AudioTrack workers. */
final class SpeakerJitter {
    private static final int BLOCK=480,CAPACITY=BLOCK*12,FADE=96;
    private final short[] samples=new short[CAPACITY*2];private int head,count,target=BLOCK*4,fade;
    private long epoch;private boolean playing,muted;private final int[] last=new int[2],origin=new int[2];
    synchronized void mute(boolean value){muted=value;++epoch;clear();target=BLOCK*4;}
    synchronized void reset(){++epoch;clear();target=BLOCK*4;}
    synchronized long epoch(){return epoch;}
    synchronized boolean offerCurrent(byte[] pcm,long expected){if(expected!=epoch)return false;offer(pcm,false);return true;}
    private void clear(){head=count=fade=0;playing=false;last[0]=last[1]=origin[0]=origin[1]=0;}
    private void transition(){origin[0]=last[0];origin[1]=last[1];fade=FADE;}
    synchronized void offer(byte[] pcm,boolean discontinuity){
        if(pcm.length!=1920)throw new IllegalArgumentException("Invalid stereo PCM size");if(discontinuity)clear();if(muted)return;
        if(count+BLOCK>CAPACITY){int drop=count-target;head=(head+drop)%CAPACITY;count-=drop;transition();}
        for(int i=0;i<BLOCK;i++){int at=((head+count+i)%CAPACITY)*2;for(int channel=0;channel<2;channel++){int offset=(i*2+channel)*2;samples[at+channel]=(short)((pcm[offset]&255)|(pcm[offset+1]<<8));}}
        count+=BLOCK;
    }
    synchronized short[] take(){
        short[] output=new short[BLOCK*2];if(muted)return output;
        if(!playing&&count>=target){playing=true;transition();}
        for(int i=0;i<BLOCK;i++){
            if(playing&&count==0){playing=false;target=Math.min(BLOCK*8,target+BLOCK*2);transition();}
            int at=head*2;for(int channel=0;channel<2;channel++){
                int value=playing?samples[at+channel]:0;
                if(fade>0)value=(origin[channel]*(fade-1)+value*(FADE-fade+1))/FADE;
                last[channel]=value;output[i*2+channel]=(short)value;
            }
            if(fade>0)--fade;if(playing){head=(head+1)%CAPACITY;--count;}
        }
        return output;
    }
    synchronized int queuedFrames(){return count;}
    synchronized int targetFrames(){return target;}
}
