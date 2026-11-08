use std::collections::VecDeque;
use std::io::{self, Read};
use std::sync::Mutex;
use std::sync::atomic::{AtomicU64, Ordering};
use usbip_audio::uac2::Uac2Streams;

pub const FRAMES: usize = 480;
pub const PCM_BYTES: usize = FRAMES * 2;
const INITIAL_TARGET: usize = FRAMES * 4; // 40 ms startup reserve.
const MAX_TARGET: usize = FRAMES * 8; // Adapt up to 80 ms after starvation.
const CAPACITY: usize = FRAMES * 12; // Hard 120 ms bound, including arrival bursts.
const FADE: usize = 96; // 2 ms transition at 48 kHz.

#[derive(Debug, PartialEq)]
pub enum Packet { Pcm([u8; PCM_BYTES]), Mute(bool), Stop, Reset }
fn invalid(message: &str) -> io::Error { io::Error::new(io::ErrorKind::InvalidData, message) }
pub fn read_packet(reader: &mut impl Read, expected: &mut u64) -> io::Result<Packet> {
    let mut header = [0u8; 16];
    reader.read_exact(&mut header)?;
    if &header[..4] != b"EPMI" || header[4] != 1 || header[6..8] != [0, 0] {
        return Err(invalid("Unsupported microphone packet header"));
    }
    let size = u32::from_be_bytes(header[8..12].try_into().unwrap()) as usize;
    let seq = u32::from_be_bytes(header[12..16].try_into().unwrap()) as u64;
    if seq != *expected { return Err(invalid("Microphone packet sequence mismatch")); }
    let packet = match (header[5], size) {
        (1, PCM_BYTES) => { let mut pcm = [0; PCM_BYTES]; reader.read_exact(&mut pcm)?; Packet::Pcm(pcm) }
        (2, 1) => { let mut value = [0]; reader.read_exact(&mut value)?; match value[0] { 0 => Packet::Mute(false), 1 => Packet::Mute(true), _ => return Err(invalid("Invalid mute state")) } }
        (3, 0) => Packet::Stop,
        (4, 0) => Packet::Reset,
        _ => return Err(invalid("Invalid microphone packet type or length")),
    };
    *expected += 1; // u64 deliberately rejects the first u32 wrap rather than accepting replay.
    Ok(packet)
}
struct State {
    samples: VecDeque<i32>, muted: bool, dropped: u64, underrun: u64,
    playing: bool, target: usize, last: i32, transition: usize, origin: i32,
}
impl Default for State {
    fn default() -> Self {
        Self { samples: VecDeque::with_capacity(CAPACITY), muted: false,
            dropped: 0, underrun: 0, playing: false, target: INITIAL_TARGET,
            last: 0, transition: 0, origin: 0 }
    }
}
impl State {
    fn reset(&mut self) {
        self.samples.clear(); self.playing=false; self.last=0;
        self.transition=0; self.origin=0; self.target=INITIAL_TARGET;
    }
    fn transition(&mut self) { self.origin=self.last; self.transition=FADE; }
    fn output(&mut self, sample:i32) -> i32 {
        let value=if self.transition>0 {
            self.transition-=1;
            ((self.origin as i64*self.transition as i64 + sample as i64*(FADE-self.transition) as i64)/FADE as i64) as i32
        } else { sample };
        self.last=value;value
    }
}
#[derive(Default)]
pub struct Ring { state: Mutex<State>, contention_samples: AtomicU64 }
impl Ring {
    pub fn accept(&self, packet: Packet) {
        // Convert outside the shared lock; the deadline-driven capture callback
        // must not compete with PCM format conversion or queue allocation.
        let converted=if let Packet::Pcm(ref bytes)=packet {
            Some(std::array::from_fn::<i32,FRAMES,_>(|i|
                i16::from_le_bytes([bytes[i*2],bytes[i*2+1]]) as i32*65536))
        } else {None};
        let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
        match packet {
            Packet::Pcm(_) if !state.muted => {
                if state.samples.len()+FRAMES>CAPACITY {
                    // A late burst must not leave playback permanently far behind.
                    let keep=state.target.saturating_sub(FRAMES);
                    let remove=state.samples.len().saturating_sub(keep);
                    state.samples.drain(..remove);state.dropped+=remove as u64;
                    state.transition();
                }
                state.samples.extend(converted.unwrap());
            }
            Packet::Mute(muted) => { state.muted = muted; state.reset(); }
            Packet::Stop => { state.muted = true; state.reset(); }
            Packet::Reset => { state.reset(); }
            _ => {}
        }
    }
    pub fn statistics(&self) -> (u64,u64,usize,usize,u64) {
        let s=self.state.lock().unwrap_or_else(|e|e.into_inner());
        (s.underrun,s.dropped,s.samples.len()/48,s.target/48,self.contention_samples.load(Ordering::Relaxed))
    }
}
impl Uac2Streams for Ring {
    fn playback(&self, _: u64, _: &[i32]) {}
    fn capture(&self, _: u64, output: &mut [i32]) {
        output.fill(0);
        let Ok(mut state) = self.state.try_lock() else {
            self.contention_samples.fetch_add(output.len() as u64,Ordering::Relaxed);return
        };
        if state.muted { return; }
        if !state.playing && state.samples.len()>=state.target {
            state.playing=true;state.transition();
        }
        for value in output {
            if state.playing {
                if let Some(sample)=state.samples.pop_front() {
                    *value=state.output(sample);continue;
                }
                // Refill a reserve instead of alternately playing each late packet
                // and inserting silence. Longer gaps increase the bounded reserve.
                state.underrun+=1;state.playing=false;
                state.target=(state.target+FRAMES*2).min(MAX_TARGET);
                state.transition();
            }
            *value=state.output(0);
        }
    }
}
pub fn check() -> Result<(), String> {
    let mut input = Vec::from(*b"EPMI\x01\x03\0\0\0\0\0\0\0\0\0\0");
    if read_packet(&mut input.as_slice(), &mut 0).map_err(|e| e.to_string())? != Packet::Stop { return Err("Packet check failed".into()); }
    input[8] = 0xff;
    if read_packet(&mut input.as_slice(), &mut 0).is_ok() { return Err("Length check failed".into()); }
    let ring = Ring::default(); let mut output = [1; 8]; ring.capture(0, &mut output);
    if output != [0; 8] { return Err("Silence check failed".into()); }
    Ok(())
}
#[cfg(test)]
mod tests {
    use super::*;
    fn frame(kind:u8, seq:u32, payload:&[u8]) -> Vec<u8> {
        let mut result=Vec::from(*b"EPMI\x01\0\0\0");result[5]=kind;
        result.extend_from_slice(&(payload.len() as u32).to_be_bytes());result.extend_from_slice(&seq.to_be_bytes());result.extend_from_slice(payload);result
    }
    #[test] fn framing_bounds_and_sequence() {
        let mut seq=0;let bytes=frame(1,0,&[0;PCM_BYTES]);assert!(matches!(read_packet(&mut bytes.as_slice(),&mut seq),Ok(Packet::Pcm(_))));assert_eq!(seq,1);
        assert!(read_packet(&mut bytes.as_slice(),&mut seq).is_err());
        for payload in [&[][..], &[0; PCM_BYTES+1][..]] { assert!(read_packet(&mut frame(1,0,payload).as_slice(),&mut 0).is_err()); }
        assert!(read_packet(&mut frame(2,0,&[2]).as_slice(),&mut 0).is_err());
        assert!(read_packet(&mut frame(3,0,&[])[..15].as_ref(),&mut 0).is_err());
    }
    #[test] fn ring_conversion_mute_and_underrun() {
        let ring=Ring::default();let mut bytes=[0;PCM_BYTES];bytes[..6].copy_from_slice(&[0,128,255,127,1,0]);ring.accept(Packet::Pcm(bytes));
        for _ in 1..4 { ring.accept(Packet::Pcm(bytes)); }
        let mut fade=[0;FRAMES];ring.capture(0,&mut fade);
        let mut out=[0;3];ring.capture(0,&mut out);assert_eq!(out,[i32::MIN,2147418112,65536]);
        ring.accept(Packet::Mute(true));ring.accept(Packet::Pcm(bytes));ring.accept(Packet::Mute(false));ring.capture(3,&mut out);assert_eq!(out,[0;3]);
        for _ in 0..20 { ring.accept(Packet::Pcm(bytes)); }assert!(ring.state.lock().unwrap().samples.len()<=CAPACITY);
        ring.accept(Packet::Stop);ring.capture(0,&mut out);assert_eq!(out,[0;3]);
    }
    #[test] fn sequence_never_wraps() {assert!(read_packet(&mut frame(3,0,&[]).as_slice(),&mut (u32::MAX as u64+1)).is_err());}
    #[test] fn endpoints_keep_independent_audio_and_stop_state() {
        let first=Ring::default();let second=Ring::default();
        let mut a=[0;PCM_BYTES];let mut b=[0;PCM_BYTES];
        for sample in a.chunks_exact_mut(2){sample.copy_from_slice(&100i16.to_le_bytes());}
        for sample in b.chunks_exact_mut(2){sample.copy_from_slice(&200i16.to_le_bytes());}
        for _ in 0..4 {first.accept(Packet::Pcm(a));second.accept(Packet::Pcm(b));}
        let mut fade=[0;FADE];second.capture(0,&mut fade);
        first.accept(Packet::Stop);let mut silence=[1;8];let mut output=[0;8];
        first.capture(0,&mut silence);second.capture(0,&mut output);
        assert_eq!(silence,[0;8]);assert_eq!(output,[200*65536;8]);
        for _ in 0..20{first.accept(Packet::Pcm(a));}
        second.capture(8,&mut output);assert_eq!(output,[200*65536;8]);
    }
    fn constant(value:i16)->Packet {
        let mut bytes=[0;PCM_BYTES];
        for p in bytes.chunks_exact_mut(2){p.copy_from_slice(&value.to_le_bytes());}
        Packet::Pcm(bytes)
    }
    #[test] fn prefill_and_refill_do_not_play_isolated_late_packets() {
        let ring=Ring::default();let mut out=[1;48];
        for _ in 0..3 {ring.accept(constant(100));ring.capture(0,&mut out);assert_eq!(out,[0;48]);}
        ring.accept(constant(100));
        let mut consume=[0;INITIAL_TARGET+FADE];ring.capture(0,&mut consume);
        assert_eq!(consume[FADE],100*65536);
        assert_eq!(*consume.last().unwrap(),0);
        assert_eq!(ring.statistics(),(1,0,0,60,0));
        for _ in 0..5 {ring.accept(constant(200));ring.capture(0,&mut out);assert_eq!(out,[0;48]);}
        ring.accept(constant(200));let mut resumed=[0;FADE+48];ring.capture(0,&mut resumed);
        assert_eq!(resumed[FADE],200*65536);
    }
    #[test] fn bursty_arrival_preserves_continuous_audio() {
        let ring=Ring::default();for _ in 0..4 {ring.accept(constant(100));}
        let mut out=[0;48];
        // 30 ms delivery bursts with a 1 ms USB consumer, for ten seconds.
        for tick in 0..10_000 {
            if tick>0 && tick%30==0 {for _ in 0..3 {ring.accept(constant(100));}}
            ring.capture(0,&mut out);
            if tick>=2 {assert_eq!(out,[100*65536;48]);}
        }
        assert_eq!(ring.statistics().0,0);assert_eq!(ring.statistics().1,0);
    }
    #[test] fn overflow_recovers_recent_audio_and_mute_discards_reserve() {
        let ring=Ring::default();for _ in 0..12 {ring.accept(constant(100));}
        ring.accept(constant(200));
        assert_eq!(ring.statistics(),(0,(CAPACITY-INITIAL_TARGET+FRAMES) as u64,40,40,0));
        let mut recent=[0;INITIAL_TARGET];ring.capture(0,&mut recent);
        assert_eq!(*recent.last().unwrap(),200*65536);
        ring.accept(Packet::Mute(true));let mut out=[1;48];ring.capture(0,&mut out);assert_eq!(out,[0;48]);
        ring.accept(Packet::Mute(false));ring.accept(constant(300));ring.capture(0,&mut out);assert_eq!(out,[0;48]);
    }

    #[test] fn contention_is_nonblocking_and_counted() {
        let ring=Ring::default();let guard=ring.state.lock().unwrap();
        let mut out=[1;48];ring.capture(0,&mut out);assert_eq!(out,[0;48]);
        drop(guard);assert_eq!(ring.statistics().4,48);
    }
    #[test] fn repeated_starvation_caps_reserve_and_stop_clears_fade() {
        let ring=Ring::default();
        for _ in 0..8 {
            for _ in 0..8 {ring.accept(constant(100));}
            let mut drain=[0;FRAMES*9];ring.capture(0,&mut drain);
        }
        assert_eq!(ring.statistics().3,80);
        for _ in 0..8 {ring.accept(constant(100));}
        let mut begin=[0;1];ring.capture(0,&mut begin);assert_ne!(begin,[0]);
        ring.accept(Packet::Stop);ring.capture(0,&mut begin);assert_eq!(begin,[0]);
    }

    #[test] fn discontinuity_discards_previous_generation() {
        assert_eq!(read_packet(&mut frame(4,0,&[]).as_slice(),&mut 0).unwrap(),Packet::Reset);
        let ring=Ring::default();for _ in 0..4 {ring.accept(constant(100));}
        let mut out=[0;48];ring.capture(0,&mut out);
        ring.accept(Packet::Reset);ring.accept(constant(200));ring.capture(0,&mut out);assert_eq!(out,[0;48]);
        for _ in 0..3 {ring.accept(constant(200));}
        let mut resumed=[0;FADE+48];ring.capture(0,&mut resumed);assert_eq!(resumed[FADE],200*65536);
    }

}
