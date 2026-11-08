package io.github.eekebin.extrapane;
final class FanoutPackets {
    static byte[] microphoneBytes(byte[] pcm,boolean muted){return muted?new byte[pcm.length]:pcm.clone();}
    // Bound freshness by the native buffer even when a device cannot provide
    // AudioTimestamp. A valid hardware timestamp can only make it older.
    static long microphoneCaptureStart(long readStarted,int bufferFrames,long packetPosition,long hardwarePosition,long hardwareNanos,boolean timestampAvailable){
        if(bufferFrames<1||bufferFrames>48000*60)throw new IllegalArgumentException("Invalid microphone buffer size");
        long conservative=readStarted-((bufferFrames+480L)*1_000_000_000L/48000);
        if(!timestampAvailable||hardwarePosition<0||packetPosition<0)return conservative;
        long frames=packetPosition-hardwarePosition;
        if(frames>48000*60L||frames< -48000*60L)return conservative;
        return Math.min(conservative,hardwareNanos+frames*1_000_000_000L/48000);
    }
    /** Unmute never exposes packets buffered before that transition. */
    static final class MicrophoneMute {
        private record State(boolean muted,long unmutedAfter,long generation){}
        private volatile State state=new State(false,Long.MIN_VALUE,0);
        synchronized boolean set(boolean value,long now){State old=state;if(old.muted()==value)return false;state=new State(value,value?old.unmutedAfter():now,old.generation()+1);return true;}
        boolean silent(long captureStarted){State snapshot=state;return snapshot.muted()||captureStarted<=snapshot.unmutedAfter();}
        long generation(){return state.generation();}
    }
    record Position(long sample,boolean discontinuity){}
    static final class MicrophoneTimeline {
        private long origin=-1,previous=-1,muteGeneration=-1;
        Position next(long captured,long mute){if(captured<0||(previous>=0&&captured<=previous))throw new IllegalArgumentException("Invalid microphone capture position");if(origin<0)origin=captured;
            boolean discontinuity=previous<0||previous>Long.MAX_VALUE-480||captured!=previous+480||mute!=muteGeneration;
            previous=captured;muteGeneration=mute;return new Position(captured-origin,discontinuity);
        }
    }
}
