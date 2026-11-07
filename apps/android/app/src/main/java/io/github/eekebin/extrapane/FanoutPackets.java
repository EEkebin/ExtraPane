package io.github.eekebin.extrapane;
final class FanoutPackets {
    static byte[] microphoneBytes(byte[] pcm,boolean muted){return muted?new byte[pcm.length]:pcm.clone();}
    record Position(long sample,boolean discontinuity){}
    static final class MicrophoneTimeline {
        private long origin=-1,previous=-1,muteGeneration=-1;
        Position next(long captured,long mute){if(captured<0||(previous>=0&&captured<=previous))throw new IllegalArgumentException("Invalid microphone capture position");if(origin<0)origin=captured;
            boolean discontinuity=previous<0||previous>Long.MAX_VALUE-480||captured!=previous+480||mute!=muteGeneration;
            previous=captured;muteGeneration=mute;return new Position(captured-origin,discontinuity);
        }
    }
}
